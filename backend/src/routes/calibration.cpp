// Windows compatibility - must be included before any Drogon headers
#include "platform/win32_compat.hpp"

#include "routes/calibration.hpp"
#include "services/camera_service.hpp"
#include "threads/thread_manager.hpp"
#include <spdlog/spdlog.h>
#include <opencv2/objdetect/aruco_detector.hpp>
#include <opencv2/objdetect/charuco_detector.hpp>
#include <opencv2/objdetect/aruco_dictionary.hpp>
#include <drogon/utils/Utilities.h>
#include <hpdf.h>
#include <cmath>

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

std::vector<unsigned char> CalibrationService::generateBoardPdf(
    int squaresX, int squaresY,
    float squareLength, float markerLength,
    float pageWidthMm, float pageHeightMm,
    float marginMm,
    const std::string& dictionary) {

    // Constants
    const float MM_TO_POINTS = 72.0f / 25.4f;  // 1 point = 1/72 inch, 1 inch = 25.4mm

    // Calculate desired board physical size in mm (squareLength is in meters)
    float desiredBoardWidthMm = squaresX * squareLength * 1000.0f;
    float desiredBoardHeightMm = squaresY * squareLength * 1000.0f;

    // Calculate printable area in mm
    float printableWidthMm = pageWidthMm - 2.0f * marginMm;
    float printableHeightMm = pageHeightMm - 2.0f * marginMm;

    // Only scale down if board is too large for printable area (never scale up)
    float scale = std::min({
        printableWidthMm / desiredBoardWidthMm,
        printableHeightMm / desiredBoardHeightMm,
        1.0f
    });

    // Final PDF embedding size in mm (this is the physical print size)
    float pdfBoardWidthMm = desiredBoardWidthMm * scale;
    float pdfBoardHeightMm = desiredBoardHeightMm * scale;

    // Generate high-resolution board image
    // The pixel dimensions don't affect physical print size - PDF will scale to fit
    // We just need enough resolution for good print quality (300+ DPI)
    const int PIXELS_PER_SQUARE = 400;  // High resolution for print quality
    int imageWidth = squaresX * PIXELS_PER_SQUARE;
    int imageHeight = squaresY * PIXELS_PER_SQUARE;

    // Create the CharucoBoard
    cv::aruco::Dictionary dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_250);
    if (dictionary == "DICT_4X4_50") dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
    else if (dictionary == "DICT_5X5_50") dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_5X5_50);
    else if (dictionary == "DICT_6X6_50") dict = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_6X6_50);

    cv::aruco::CharucoBoard board(
        cv::Size(squaresX, squaresY),
        squareLength,
        markerLength,
        dict
    );

    // Generate board image with no margin - the entire image IS the board
    cv::Mat boardImage;
    board.generateImage(cv::Size(imageWidth, imageHeight), boardImage, 0, 1);

    // Encode as PNG for embedding in PDF
    std::vector<uchar> pngBuffer;
    cv::imencode(".png", boardImage, pngBuffer);

    // Create PDF document
    HPDF_Doc pdf = HPDF_New(nullptr, nullptr);
    if (!pdf) {
        spdlog::error("Failed to create PDF document");
        return {};
    }

    // Set compression
    HPDF_SetCompressionMode(pdf, HPDF_COMP_ALL);

    // Add a page with custom size (in points)
    HPDF_Page page = HPDF_AddPage(pdf);
    HPDF_Page_SetWidth(page, pageWidthMm * MM_TO_POINTS);
    HPDF_Page_SetHeight(page, pageHeightMm * MM_TO_POINTS);

    // Load the PNG image from memory
    HPDF_Image image = HPDF_LoadPngImageFromMem(pdf, pngBuffer.data(), pngBuffer.size());
    if (!image) {
        spdlog::error("Failed to load PNG image into PDF");
        HPDF_Free(pdf);
        return {};
    }

    // Convert board dimensions to PDF points and center on page
    float imageWidthPt = pdfBoardWidthMm * MM_TO_POINTS;
    float imageHeightPt = pdfBoardHeightMm * MM_TO_POINTS;
    float xPos = (pageWidthMm * MM_TO_POINTS - imageWidthPt) / 2.0f;
    float yPos = (pageHeightMm * MM_TO_POINTS - imageHeightPt) / 2.0f;

    // Draw the image at the exact physical dimensions we want
    // The PDF renderer will scale the high-res image to fit these dimensions
    HPDF_Page_DrawImage(page, image, xPos, yPos, imageWidthPt, imageHeightPt);

    // Save PDF to memory
    HPDF_SaveToStream(pdf);
    HPDF_ResetStream(pdf);

    // Read PDF data from stream
    std::vector<unsigned char> pdfBuffer;
    HPDF_UINT32 size = HPDF_GetStreamSize(pdf);
    pdfBuffer.resize(size);
    HPDF_ReadFromStream(pdf, pdfBuffer.data(), &size);

    HPDF_Free(pdf);

    return pdfBuffer;
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

    result["image_width"] = image.cols;
    result["image_height"] = image.rows;

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

    // GET /api/calibration/board/pdf - Generate calibration board as PDF for printing
    app.registerHandler(
        "/api/calibration/board/pdf",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            int squaresX = 7;
            int squaresY = 5;
            float squareLength = 0.04f;
            float markerLength = 0.03f;
            float pageWidthMm = 210.0f;   // A4 default
            float pageHeightMm = 297.0f;  // A4 default
            float marginMm = 15.0f;       // Default margin for printer bleed
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
            param = req->getParameter("pageWidth");
            if (!param.empty()) {
                pageWidthMm = std::stof(param);
            }
            param = req->getParameter("pageHeight");
            if (!param.empty()) {
                pageHeightMm = std::stof(param);
            }
            param = req->getParameter("margin");
            if (!param.empty()) {
                marginMm = std::stof(param);
            }

            auto pdfBuffer = generateBoardPdf(squaresX, squaresY, squareLength, markerLength,
                                               pageWidthMm, pageHeightMm, marginMm, dictionary);

            if (pdfBuffer.empty()) {
                auto resp = HttpResponse::newHttpResponse();
                resp->setStatusCode(k500InternalServerError);
                resp->setContentTypeCode(CT_APPLICATION_JSON);
                resp->setBody(R"({"error": "Failed to generate PDF"})");
                callback(resp);
                return;
            }

            auto resp = HttpResponse::newHttpResponse();
            resp->setStatusCode(k200OK);
            resp->setContentTypeString("application/pdf");
            resp->setBody(std::string(pdfBuffer.begin(), pdfBuffer.end()));
            callback(resp);
        },
        {Get});

    // POST /api/calibration/detect - Detect markers in image
    app.registerHandler(
        "/api/calibration/detect",
        [](const HttpRequestPtr& req,
           std::function<void(const HttpResponsePtr&)>&& callback) {
            try {
                auto body = nlohmann::json::parse(req->getBody());

                // Check if camera_id is provided
                cv::Mat image;
                if (body.contains("camera_id")) {
                    int cameraId = body.at("camera_id").get<int>();
                    auto frame = ThreadManager::instance().getCameraFrame(cameraId);
                    if (!frame || frame->empty()) {
                        auto resp = HttpResponse::newHttpResponse();
                        resp->setStatusCode(k400BadRequest);
                        resp->setContentTypeCode(CT_APPLICATION_JSON);
                        resp->setBody(R"({"error": "Failed to capture frame from camera. Is it running?"})");
                        callback(resp);
                        return;
                    }
                    image = frame->color().clone();
                } else {
                    auto resp = HttpResponse::newHttpResponse();
                    resp->setStatusCode(k400BadRequest);
                    resp->setContentTypeCode(CT_APPLICATION_JSON);
                    resp->setBody(R"({"error": "Missing camera_id parameter"})");
                    callback(resp);
                    return;
                }

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

                // If we captured from camera, include the original image in the response
                if (body.contains("camera_id")) {
                    std::vector<uchar> buffer;
                    cv::imencode(".jpg", image, buffer);
                    std::string bufferStr(buffer.begin(), buffer.end());
                    result["original_image_base64"] = drogon::utils::base64Encode(
                        reinterpret_cast<const unsigned char*>(bufferStr.data()), bufferStr.size());
                }

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
                    // Push calibration to any running pipelines for this camera
                    try {
                        auto matrixJson = body["camera_matrix"];
                        cv::Mat cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
                        if (matrixJson.is_array() && matrixJson.size() == 3) {
                            for (int r = 0; r < 3; r++) {
                                for (int c = 0; c < 3; c++) {
                                    cameraMatrix.at<double>(r, c) = matrixJson[r][c].get<double>();
                                }
                            }
                        }

                        auto distJson = body["dist_coeffs"];
                        cv::Mat distCoeffs = cv::Mat::zeros(static_cast<int>(distJson.size()), 1, CV_64F);
                        for (size_t i = 0; i < distJson.size(); i++) {
                            distCoeffs.at<double>(static_cast<int>(i)) = distJson[i].get<double>();
                        }

                        ThreadManager::instance().updateCalibration(cameraId, cameraMatrix, distCoeffs);
                    } catch (const std::exception& e) {
                        spdlog::warn("Failed to push calibration to running pipelines: {}", e.what());
                        // Don't fail the request - calibration was saved to DB successfully
                    }

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
