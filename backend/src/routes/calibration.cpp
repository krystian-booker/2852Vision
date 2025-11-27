// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/calibration.hpp"
#include "services/camera_service.hpp"
#include <spdlog/spdlog.h>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <opencv2/objdetect/charuco_detector.hpp>
#include <opencv2/objdetect/aruco_dictionary.hpp>
#include <drogon/utils/Utilities.h>

namespace vision {

cv::Mat CalibrationService::generateBoard(int squaresX, int squaresY, float squareLength,
                                          float markerLength, int imageSquareSizePixels,
                                          const std::string& dictionary) {
    // Get ArUco dictionary
    cv::aruco::PredefinedDictionaryType dictType = cv::aruco::DICT_6X6_250;
    
    if (dictionary == "DICT_4X4_50") dictType = cv::aruco::DICT_4X4_50;
    else if (dictionary == "DICT_5X5_50") dictType = cv::aruco::DICT_5X5_50;
    else if (dictionary == "DICT_6X6_50") dictType = cv::aruco::DICT_6X6_50;
    
    cv::aruco::Dictionary dict = cv::aruco::getPredefinedDictionary(dictType);

    // Create CharucoBoard
    cv::aruco::CharucoBoard board(
        cv::Size(squaresX, squaresY),
        squareLength,
        markerLength,
        dict
    );

    // Generate board image
    cv::Mat boardImage;
    int imageWidth = squaresX * imageSquareSizePixels;
    int imageHeight = squaresY * imageSquareSizePixels;
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
    // Create CharucoBoard
    cv::aruco::CharucoBoard board(
        cv::Size(squaresX, squaresY),
        0.04f,  // square length (will be overwritten during calibration)
        0.03f,  // marker length
        dict
    );

    // Create CharucoDetector
    cv::aruco::CharucoParameters charucoParams;
    cv::aruco::DetectorParameters detectorParams;
    cv::aruco::CharucoDetector detector(board, charucoParams, detectorParams);

    // Detect markers and Charuco corners
    std::vector<int> markerIds;
    std::vector<std::vector<cv::Point2f>> markerCorners;
    std::vector<cv::Point2f> charucoCorners;
    std::vector<int> charucoIds;

    detector.detectBoard(image, charucoCorners, charucoIds, markerCorners, markerIds);

    if (markerIds.empty()) {
        result["error"] = "No markers detected";
        return result;
    }

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
    result["annotated_image_base64"] = drogon::utils::base64Encode(
        reinterpret_cast<const unsigned char*>(bufferStr.data()), bufferStr.size());

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

    // Collect object points and image points
    std::vector<std::vector<cv::Point3f>> allObjPoints;
    std::vector<std::vector<cv::Point2f>> allImgPoints;

    for (size_t i = 0; i < allCorners.size(); i++) {
        std::vector<cv::Point3f> objPoints;
        std::vector<cv::Point2f> imgPoints;
        board.matchImagePoints(allCorners[i], allIds[i], objPoints, imgPoints);
        
        if (!objPoints.empty()) {
            allObjPoints.push_back(objPoints);
            allImgPoints.push_back(imgPoints);
        }
    }

    if (allObjPoints.empty()) {
        result["error"] = "Not enough valid frames for calibration";
        return result;
    }

    // Calibrate camera
    cv::Mat cameraMatrix, distCoeffs;
    std::vector<cv::Mat> rvecs, tvecs;

    double reprojectionError = cv::calibrateCamera(
        allObjPoints, allImgPoints, imageSize,
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

void CalibrationService::registerRoutes(drogon::HttpAppFramework& app) {
    using namespace drogon;

    // GET /api/calibration/board - Generate calibration board
    app.registerHandler(
        "/api/calibration/board",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            int squaresX = 7;
            int squaresY = 5;
            float squareLength = 0.04f;
            float markerLength = 0.03f;
            std::string dictionary = "DICT_6X6_50";

            std::string param;
            param = req->getParameter("squaresX");
            if (!param.empty()) {
                squaresX = std::stoi(param);
            }
            param = req->getParameter("squaresY");
            if (!param.empty()) {
                squaresY = std::stoi(param);
            }
            param = req->getParameter("squareLength");
            if (!param.empty()) {
                squareLength = std::stof(param);
            }
            param = req->getParameter("markerLength");
            if (!param.empty()) {
                markerLength = std::stof(param);
            }
            param = req->getParameter("dictionary");
            if (!param.empty()) {
                dictionary = param;
            }

            cv::Mat board = generateBoard(squaresX, squaresY, squareLength, markerLength, 100, dictionary);

            // Encode as PNG
            std::vector<uchar> buffer;
            cv::imencode(".png", board, buffer);

            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeString("image/png");
            resp->setBody(std::string(buffer.begin(), buffer.end()));
            callback(resp);
        },
        {Get});

    // POST /api/calibration/detect - Detect markers in image
    app.registerHandler(
        "/api/calibration/detect",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                // Decode base64 image
                auto body = nlohmann::json::parse(req->getBody());
                std::string imageBase64 = body.at("image").get<std::string>();

                // Remove data URL prefix if present
                size_t commaPos = imageBase64.find(',');
                if (commaPos != std::string::npos) {
                    imageBase64 = imageBase64.substr(commaPos + 1);
                }

                std::string imageData = drogon::utils::base64Decode(imageBase64);
                std::vector<uchar> imageBytes(imageData.begin(), imageData.end());
                cv::Mat image = cv::imdecode(imageBytes, cv::IMREAD_COLOR);

                if (image.empty()) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Failed to decode image"})");
                    callback(resp);
                    return;
                }

                int squaresX = body.value("squaresX", 7);
                int squaresY = body.value("squaresY", 5);

                auto result = detectMarkers(image, squaresX, squaresY);
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(result.dump());
                callback(resp);

            } catch (const std::exception& e) {
                nlohmann::json error = {{"error", e.what()}};
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(error.dump());
                callback(resp);
            }
        },
        {Post});

    // POST /api/calibration/calibrate - Perform calibration
    app.registerHandler(
        "/api/calibration/calibrate",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = nlohmann::json::parse(req->getBody());

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

                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(result.dump());
                callback(resp);

            } catch (const std::exception& e) {
                nlohmann::json error = {{"error", e.what()}};
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(error.dump());
                callback(resp);
            }
        },
        {Post});

    // POST /api/calibration/save - Save calibration to camera
    app.registerHandler(
        "/api/calibration/save",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = nlohmann::json::parse(req->getBody());

                int cameraId = body.at("camera_id").get<int>();
                std::string cameraMatrixJson = body["camera_matrix"].dump();
                std::string distCoeffsJson = body["dist_coeffs"].dump();
                double reprojectionError = body.at("reprojection_error").get<double>();

                bool success = CameraService::instance().saveCalibration(
                    cameraId, cameraMatrixJson, distCoeffsJson, reprojectionError
                );

                if (success) {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k200OK);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"success": true})");
                    callback(resp);
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k404NotFound);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Camera not found"})");
                    callback(resp);
                }

            } catch (const std::exception& e) {
                nlohmann::json error = {{"error", e.what()}};
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k400BadRequest);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(error.dump());
                callback(resp);
            }
        },
        {Post});

    spdlog::info("Calibration routes registered");
}

} // namespace vision
