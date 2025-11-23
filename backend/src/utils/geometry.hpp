#pragma once

#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>
#include <Eigen/Dense>
#include <cmath>

namespace vision {

// 3D translation vector
struct Translation3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Translation3d() = default;
    Translation3d(double x, double y, double z) : x(x), y(y), z(z) {}

    Eigen::Vector3d toVector() const {
        return Eigen::Vector3d(x, y, z);
    }

    static Translation3d fromVector(const Eigen::Vector3d& v) {
        return Translation3d(v.x(), v.y(), v.z());
    }

    nlohmann::json toJson() const {
        return {{"x", x}, {"y", y}, {"z", z}};
    }

    static Translation3d fromJson(const nlohmann::json& j) {
        return Translation3d(
            j.at("x").get<double>(),
            j.at("y").get<double>(),
            j.at("z").get<double>()
        );
    }
};

// Quaternion for rotation representation
struct Quaternion {
    double w = 1.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Quaternion() = default;
    Quaternion(double w, double x, double y, double z) : w(w), x(x), y(y), z(z) {}

    // Convert to rotation matrix
    Eigen::Matrix3d toRotationMatrix() const {
        Eigen::Quaterniond q(w, x, y, z);
        q.normalize();
        return q.toRotationMatrix();
    }

    // Create from rotation matrix
    static Quaternion fromRotationMatrix(const Eigen::Matrix3d& R) {
        Eigen::Quaterniond q(R);
        return Quaternion(q.w(), q.x(), q.y(), q.z());
    }

    // Create from axis-angle (rodrigues)
    static Quaternion fromAxisAngle(const Eigen::Vector3d& axis, double angle) {
        double halfAngle = angle / 2.0;
        double s = std::sin(halfAngle);
        Eigen::Vector3d normalizedAxis = axis.normalized();
        return Quaternion(
            std::cos(halfAngle),
            normalizedAxis.x() * s,
            normalizedAxis.y() * s,
            normalizedAxis.z() * s
        );
    }

    nlohmann::json toJson() const {
        return {{"W", w}, {"X", x}, {"Y", y}, {"Z", z}};
    }

    static Quaternion fromJson(const nlohmann::json& j) {
        return Quaternion(
            j.at("W").get<double>(),
            j.at("X").get<double>(),
            j.at("Y").get<double>(),
            j.at("Z").get<double>()
        );
    }
};

// 3D rotation using rotation matrix internally
struct Rotation3d {
    Eigen::Matrix3d matrix = Eigen::Matrix3d::Identity();

    Rotation3d() = default;
    explicit Rotation3d(const Eigen::Matrix3d& m) : matrix(m) {}
    explicit Rotation3d(const Quaternion& q) : matrix(q.toRotationMatrix()) {}

    // Create from rodrigues vector (OpenCV format)
    static Rotation3d fromRodrigues(const cv::Vec3d& rvec) {
        cv::Mat R;
        cv::Rodrigues(rvec, R);
        Eigen::Matrix3d mat;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                mat(i, j) = R.at<double>(i, j);
            }
        }
        return Rotation3d(mat);
    }

    cv::Vec3d toRodrigues() const {
        cv::Mat R(3, 3, CV_64F);
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                R.at<double>(i, j) = matrix(i, j);
            }
        }
        cv::Mat rvec;
        cv::Rodrigues(R, rvec);
        return cv::Vec3d(rvec.at<double>(0), rvec.at<double>(1), rvec.at<double>(2));
    }

    Quaternion toQuaternion() const {
        return Quaternion::fromRotationMatrix(matrix);
    }

    Rotation3d inverse() const {
        return Rotation3d(matrix.transpose());
    }

    // Compose rotations
    Rotation3d operator*(const Rotation3d& other) const {
        return Rotation3d(matrix * other.matrix);
    }

    // Rotate a vector
    Eigen::Vector3d operator*(const Eigen::Vector3d& v) const {
        return matrix * v;
    }
};

// Complete 3D pose (position + orientation)
struct Pose3d {
    Translation3d translation;
    Rotation3d rotation;

    Pose3d() = default;
    Pose3d(const Translation3d& t, const Rotation3d& r)
        : translation(t), rotation(r) {}

    // Create from OpenCV pose (rvec, tvec)
    static Pose3d fromOpenCV(const cv::Vec3d& rvec, const cv::Vec3d& tvec) {
        return Pose3d(
            Translation3d(tvec[0], tvec[1], tvec[2]),
            Rotation3d::fromRodrigues(rvec)
        );
    }

    // Convert to OpenCV format
    void toOpenCV(cv::Vec3d& rvec, cv::Vec3d& tvec) const {
        rvec = rotation.toRodrigues();
        tvec = cv::Vec3d(translation.x, translation.y, translation.z);
    }

    // Get as 4x4 homogeneous transformation matrix
    Eigen::Matrix4d toMatrix() const {
        Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
        T.block<3, 3>(0, 0) = rotation.matrix;
        T.block<3, 1>(0, 3) = translation.toVector();
        return T;
    }

    // Create from 4x4 transformation matrix
    static Pose3d fromMatrix(const Eigen::Matrix4d& T) {
        Pose3d pose;
        pose.rotation = Rotation3d(T.block<3, 3>(0, 0));
        pose.translation = Translation3d::fromVector(T.block<3, 1>(0, 3));
        return pose;
    }

    // Inverse of this pose
    Pose3d inverse() const {
        Rotation3d invR = rotation.inverse();
        Eigen::Vector3d invT = -(invR * translation.toVector());
        return Pose3d(Translation3d::fromVector(invT), invR);
    }

    // Compose poses: this * other
    Pose3d transformBy(const Pose3d& other) const {
        Eigen::Matrix4d result = toMatrix() * other.toMatrix();
        return fromMatrix(result);
    }

    // Transform a point
    Eigen::Vector3d transformPoint(const Eigen::Vector3d& point) const {
        return rotation * point + translation.toVector();
    }

    nlohmann::json toJson() const {
        return {
            {"translation", translation.toJson()},
            {"rotation", {{"quaternion", rotation.toQuaternion().toJson()}}}
        };
    }

    static Pose3d fromJson(const nlohmann::json& j) {
        Translation3d t = Translation3d::fromJson(j.at("translation"));
        Quaternion q = Quaternion::fromJson(j.at("rotation").at("quaternion"));
        return Pose3d(t, Rotation3d(q));
    }
};

// Coordinate system conversion utilities
namespace CoordinateSystem {

// OpenCV camera frame: X-right, Y-down, Z-forward
// FRC field frame: X-forward, Y-left, Z-up

// Matrix to convert from OpenCV camera frame to FRC field frame
inline Eigen::Matrix3d openCVToFRC() {
    Eigen::Matrix3d m;
    // Maps: OpenCV (X,Y,Z) -> FRC (Z,-X,-Y)
    m << 0, 0, 1,
        -1, 0, 0,
         0,-1, 0;
    return m;
}

// Matrix to convert from FRC field frame to OpenCV camera frame
inline Eigen::Matrix3d frcToOpenCV() {
    return openCVToFRC().transpose();
}

// Convert a pose from camera frame (OpenCV) to field frame (FRC)
inline Pose3d cameraToField(const Pose3d& cameraPose) {
    Eigen::Matrix3d conversion = openCVToFRC();

    // Convert rotation
    Eigen::Matrix3d newR = conversion * cameraPose.rotation.matrix * conversion.transpose();

    // Convert translation
    Eigen::Vector3d newT = conversion * cameraPose.translation.toVector();

    return Pose3d(Translation3d::fromVector(newT), Rotation3d(newR));
}

// Convert a pose from field frame (FRC) to camera frame (OpenCV)
inline Pose3d fieldToCamera(const Pose3d& fieldPose) {
    Eigen::Matrix3d conversion = frcToOpenCV();

    // Convert rotation
    Eigen::Matrix3d newR = conversion * fieldPose.rotation.matrix * conversion.transpose();

    // Convert translation
    Eigen::Vector3d newT = conversion * fieldPose.translation.toVector();

    return Pose3d(Translation3d::fromVector(newT), Rotation3d(newR));
}

} // namespace CoordinateSystem

} // namespace vision
