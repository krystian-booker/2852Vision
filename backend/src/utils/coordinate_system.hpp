#pragma once

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <Eigen/Dense>
#include "utils/geometry.hpp"

namespace vision {

class CoordinateUtils {
public:
    /**
     * Converts an OpenCV solvePnP result (Model-to-Camera transform)
     * directly into an FRC Field Coordinate Pose (Camera position on Field).
     * * Handles:
     * 1. Converting rvec/tvec to a 4x4 Matrix
     * 2. Inverting the Matrix (to get Camera in Field space)
     * 3. Rotating the Coordinate System (OpenCV EDN -> FRC NWU)
     */
    static Pose3d solvePnPToFieldPose(const cv::Vec3d& rvec, const cv::Vec3d& tvec) {
        // 1. Convert OpenCV rvec/tvec to a 4x4 Transformation Matrix
        // This matrix represents: "The position of the Tag relative to the Camera"
        cv::Mat R_cv;
        cv::Rodrigues(rvec, R_cv);

        Eigen::Matrix4d T_camera_from_world; // T_cw
        T_camera_from_world.setIdentity();
        
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) {
                T_camera_from_world(r, c) = R_cv.at<double>(r, c);
            }
            T_camera_from_world(r, 3) = tvec[r];
        }

        // 2. Invert to get "The position of the Camera relative to the Tag/World"
        Eigen::Matrix4d T_world_from_camera = T_camera_from_world.inverse();

        // 3. Coordinate System Basis Transform
        // OpenCV uses: Z-Forward, X-Right, Y-Down (EDN)
        // FRC uses: X-Forward, Y-Left, Z-Up (NWU)
        
        // We need to rotate the result into the NWU frame.
        // Basis transform: X_new = Z_old, Y_new = -X_old, Z_new = -Y_old
        
        // Let's do the permutation explicitly on the translation:
        double x_cv = T_world_from_camera(0, 3);
        double y_cv = T_world_from_camera(1, 3);
        double z_cv = T_world_from_camera(2, 3);

        // Map to FRC Field (NWU)
        // Camera Forward is Z in OpenCV -> Becomes X in FRC
        // Camera Right is X in OpenCV -> Becomes -Y (Left) in FRC
        // Camera Down is Y in OpenCV -> Becomes -Z (Up) in FRC
        
        Translation3d translation(z_cv, -x_cv, -y_cv);

        // Extract Rotation for FRC (Roll/Pitch/Yaw)
        // We need the rotation matrix of the camera in the FRC frame.
        Eigen::Matrix3d R_wc = T_world_from_camera.block<3,3>(0,0);
        
        // Apply basis transform to rotation matrix: R_nwu = Basis * R_cv * Basis.T
        Eigen::Matrix3d BasisRot;
        BasisRot << 0, 0, 1,
                   -1, 0, 0,
                    0,-1, 0;
                    
        Eigen::Matrix3d R_nwu = BasisRot * R_wc * BasisRot.transpose();

        // Create Rotation3d from the matrix
        Rotation3d rotation(R_nwu);

        return Pose3d(translation, rotation);
    }
};

} // namespace vision
