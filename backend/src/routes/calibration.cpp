#include "routes/calibration.hpp"
#include "services/camera_service.hpp"
#include <spdlog/spdlog.h>
#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>

namespace vision {

cv::Mat CalibrationService::generateBoard(int squaresX, int squaresY, int squareSize,
                                          int markerSize, const std::string& dictionary) {
    // Get ArUco dictionary
    cv::aruco::Dictionary dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);

    // Create CharucoBoard
    cv::aruco::CharucoBoard board(
        cv::Size(squaresX, squaresY),
        static_cast<float>(squareSize),
        static_cast<float>(markerSize),
        dict
    );

    // Generate board image
    cv::Mat boardImage;
    int imageWidth = squaresX * squareSize;
    int imageHeight = squaresY * squareSize;
    board.generateImage(cv::Size(imageWidth, imageHeight), boardImage, 10, 1);

    return boardImage;
}

nlohmann::json CalibrationService::detectMarkers(const cv::Mat& image,
                                                  int squaresX, int squaresY,
                                                  const std::string& dictionary) {
    nlohmann::json result;
    result["success"] = false;

    // Get ArUco dictionary and create detector
    cv::aruco::Dictionary dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    cv::aruco::DetectorParameters detectorParams;
    cv::aruco::ArucoDetector detector(dict, detectorParams);

    // Detect markers
    std::vector<int> markerIds;
    std::vector<std::vector<cv::Point2f>> markerCorners;
    detector.detectMarkers(image, markerCorners, markerIds);

    if (markerIds.empty()) {
        result["error"] = "No markers detected";
        return result;
    }

    // Create CharucoBoard
    cv::aruco::CharucoBoard board(
        cv::Size(squaresX, squaresY),
        0.04f,  // square length (will be overwritten during calibration)
        0.03f,  // marker length
        dict
    );

    // Interpolate Charuco corners
    std::vector<cv::Point2f> charucoCorners;
    std::vector<int> charucoIds;
    cv::aruco::interpolateCornersCharuco(
        markerCorners, markerIds, image, &board,
        charucoCorners, charucoIds
    );

    if (charucoCorners.empty()) {
        result["error"] = "Could not interpolate Charuco corners";
        return result;
    }

    // Build result
    result["success"] = true;
    result["markers_detected"] = static_cast<int>(markerIds.size());
    result["charuco_corners"] = static_cast<int>(charucoCorners.size());

    nlohmann::json cornersJson = nlohmann::json::array();
    for (size_t i = 0; i < charucoCorners.size(); i++) {
        cornersJson.push_back({
            {"id", charucoIds[i]},
            {"x", charucoCorners[i].x},
            {"y", charucoCorners[i].y}
        });
    }
    result["corners"] = cornersJson;

    // Generate annotated image
    cv::Mat annotated = image.clone();
    if (annotated.channels() == 1) {
        cv::cvtColor(annotated, annotated, cv::COLOR_GRAY2BGR);
    }
    cv::aruco::drawDetectedMarkers(annotated, markerCorners, markerIds);
    cv::aruco::drawDetectedCornersCharuco(annotated, charucoCorners, charucoIds);

    // Encode as JPEG
    std::vector<uchar> buffer;
    cv::imencode(".jpg", annotated, buffer);
    std::string bufferStr(buffer.begin(), buffer.end());
    result["annotated_image_base64"] = crow::utility::base64encode(
        bufferStr.data(), bufferStr.size());

    return result;
}

nlohmann::json CalibrationService::calibrate(
    const std::vector<std::vector<cv::Point2f>>& allCorners,
    const std::vector<std::vector<int>>& allIds,
    const cv::Size& imageSize,
    int squaresX, int squaresY,
    float squareLength, float markerLength) {

    nlohmann::json result;
    result["success"] = false;

    if (allCorners.empty() || allCorners.size() != allIds.size()) {
        result["error"] = "Invalid calibration data";
        return result;
    }

    // Create CharucoBoard
    cv::aruco::Dictionary dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    cv::aruco::CharucoBoard board(
        cv::Size(squaresX, squaresY),
        squareLength,
        markerLength,
        dict
    );

    // Calibrate camera
    cv::Mat cameraMatrix, distCoeffs;
    std::vector<cv::Mat> rvecs, tvecs;

    double reprojectionError = cv::aruco::calibrateCameraCharuco(
        allCorners, allIds, &board, imageSize,
        cameraMatrix, distCoeffs, rvecs, tvecs
    );

    // Build result
    result["success"] = true;
    result["reprojection_error"] = reprojectionError;

    // Camera matrix as array
    nlohmann::json matrixJson = nlohmann::json::array();
    for (int i = 0; i < 3; i++) {
        nlohmann::json row = nlohmann::json::array();
        for (int j = 0; j < 3; j++) {
            row.push_back(cameraMatrix.at<double>(i, j));
        }
        matrixJson.push_back(row);
    }
    result["camera_matrix"] = matrixJson;

    // Distortion coefficients as array
    nlohmann::json distJson = nlohmann::json::array();
    for (int i = 0; i < distCoeffs.cols; i++) {
        distJson.push_back(distCoeffs.at<double>(0, i));
    }
    result["dist_coeffs"] = distJson;

    return result;
}

void CalibrationService::registerRoutes(crow::SimpleApp& app) {
    // GET /api/calibration/board - Generate calibration board
    CROW_ROUTE(app, "/api/calibration/board")
    ([](const crow::request& req) {
        int squaresX = 7;
        int squaresY = 5;
        int squareSize = 100;
        int markerSize = 80;

        if (req.url_params.get("squaresX")) {
            squaresX = std::stoi(req.url_params.get("squaresX"));
        }
        if (req.url_params.get("squaresY")) {
            squaresY = std::stoi(req.url_params.get("squaresY"));
        }
        if (req.url_params.get("squareSize")) {
            squareSize = std::stoi(req.url_params.get("squareSize"));
        }
        if (req.url_params.get("markerSize")) {
            markerSize = std::stoi(req.url_params.get("markerSize"));
        }

        cv::Mat board = generateBoard(squaresX, squaresY, squareSize, markerSize);

        // Encode as PNG
        std::vector<uchar> buffer;
        cv::imencode(".png", board, buffer);

        crow::response res;
        res.code = 200;
        res.set_header("Content-Type", "image/png");
        res.body = std::string(buffer.begin(), buffer.end());
        return res;
    });

    // POST /api/calibration/detect - Detect markers in image
    CROW_ROUTE(app, "/api/calibration/detect").methods("POST"_method)
    ([](const crow::request& req) {
        try {
            // Decode base64 image
            auto body = nlohmann::json::parse(req.body);
            std::string imageBase64 = body.at("image").get<std::string>();

            // Remove data URL prefix if present
            size_t commaPos = imageBase64.find(',');
            if (commaPos != std::string::npos) {
                imageBase64 = imageBase64.substr(commaPos + 1);
            }

            std::string imageData = crow::utility::base64decode(imageBase64);
            std::vector<uchar> imageBytes(imageData.begin(), imageData.end());
            cv::Mat image = cv::imdecode(imageBytes, cv::IMREAD_COLOR);

            if (image.empty()) {
                return crow::response(400, "application/json", R"({"error": "Failed to decode image"})");
            }

            int squaresX = body.value("squaresX", 7);
            int squaresY = body.value("squaresY", 5);

            auto result = detectMarkers(image, squaresX, squaresY);
            return crow::response(200, "application/json", result.dump());

        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            return crow::response(400, "application/json", error.dump());
        }
    });

    // POST /api/calibration/calibrate - Perform calibration
    CROW_ROUTE(app, "/api/calibration/calibrate").methods("POST"_method)
    ([](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);

            // Parse calibration data
            std::vector<std::vector<cv::Point2f>> allCorners;
            std::vector<std::vector<int>> allIds;

            for (const auto& frame : body["frames"]) {
                std::vector<cv::Point2f> corners;
                std::vector<int> ids;

                for (const auto& corner : frame["corners"]) {
                    corners.push_back(cv::Point2f(
                        corner["x"].get<float>(),
                        corner["y"].get<float>()
                    ));
                    ids.push_back(corner["id"].get<int>());
                }

                allCorners.push_back(corners);
                allIds.push_back(ids);
            }

            cv::Size imageSize(
                body["image_width"].get<int>(),
                body["image_height"].get<int>()
            );

            int squaresX = body.value("squaresX", 7);
            int squaresY = body.value("squaresY", 5);
            float squareLength = body.value("square_length", 0.04f);
            float markerLength = body.value("marker_length", 0.03f);

            auto result = calibrate(allCorners, allIds, imageSize,
                                    squaresX, squaresY, squareLength, markerLength);

            return crow::response(200, "application/json", result.dump());

        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            return crow::response(400, "application/json", error.dump());
        }
    });

    // POST /api/calibration/save - Save calibration to camera
    CROW_ROUTE(app, "/api/calibration/save").methods("POST"_method)
    ([](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);

            int cameraId = body.at("camera_id").get<int>();
            std::string cameraMatrixJson = body["camera_matrix"].dump();
            std::string distCoeffsJson = body["dist_coeffs"].dump();
            double reprojectionError = body.at("reprojection_error").get<double>();

            bool success = CameraService::instance().saveCalibration(
                cameraId, cameraMatrixJson, distCoeffsJson, reprojectionError
            );

            if (success) {
                return crow::response(200, "application/json", R"({"success": true})");
            } else {
                return crow::response(404, "application/json", R"({"error": "Camera not found"})");
            }

        } catch (const std::exception& e) {
            nlohmann::json error = {{"error", e.what()}};
            return crow::response(400, "application/json", error.dump());
        }
    });

    spdlog::info("Calibration routes registered");
}

} // namespace vision
