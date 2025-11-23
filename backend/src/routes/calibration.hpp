#pragma once

#include <crow.h>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <vector>

namespace vision {

class CalibrationService {
public:
    // Generate calibration board image
    static cv::Mat generateBoard(int squaresX, int squaresY, int squareSize,
                                 int markerSize, const std::string& dictionary = "DICT_6X6_250");

    // Detect CharucoBoard corners in image
    static nlohmann::json detectMarkers(const cv::Mat& image,
                                        int squaresX, int squaresY,
                                        const std::string& dictionary = "DICT_6X6_250");

    // Calibrate camera from multiple detections
    static nlohmann::json calibrate(const std::vector<std::vector<cv::Point2f>>& allCorners,
                                    const std::vector<std::vector<int>>& allIds,
                                    const cv::Size& imageSize,
                                    int squaresX, int squaresY,
                                    float squareLength, float markerLength);

    // Register calibration routes
    static void registerRoutes(crow::SimpleApp& app);
};

} // namespace vision
