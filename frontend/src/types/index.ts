/**
 * API Types for 2852Vision application
 */

export interface Camera {
  id: number
  name: string
  camera_type: 'USB' | 'Spinnaker' | 'RealSense'
  identifier: string
  orientation: 0 | 90 | 180 | 270
  exposure_mode: 'auto' | 'manual'
  exposure_value: number
  gain_mode: 'auto' | 'manual'
  gain_value: number
  camera_matrix_json: string | null
  dist_coeffs_json: string | null
  reprojection_error: number | null
  resolution_json: string | null
  framerate: number | null
  depth_enabled: boolean
  device_info_json: string | null
  horizontal_fov: number | null    // degrees
  vertical_fov: number | null      // degrees
}

export interface Pipeline {
  id: number
  camera_id: number
  name: string
  pipeline_type: string // 'AprilTag' | 'Object Detection (ML)'
  config: string // JSON string of PipelineConfig
}

/**
 * AprilTag pipeline configuration.
 */
export interface AprilTagConfig {
  family?: string
  tag_size_m?: number
  threads?: number
  decimate?: number
  blur?: number
  refine_edges?: boolean
  decision_margin?: number
  pose_iterations?: number
  decode_sharpening?: number
  min_weight?: number
  edge_threshold?: number
  ransac_reproj_threshold?: number
  ransac_confidence?: number
  min_inliers?: number
  use_prev_guess?: boolean
  publish_field_pose?: boolean
  output_quaternion?: boolean
}

/**
 * Machine Learning pipeline configuration.
 */
export interface MLConfig {
  model_type?: string
  model_filename?: string
  labels_filename?: string
  confidence_threshold?: number
  nms_iou_threshold?: number
  target_classes?: string[]
  accelerator?: string
  onnx_provider?: string
  tflite_delegate?: string | null
  max_detections?: number
  img_size?: number
}

/**
 * Combined pipeline configuration type.
 * Supports both AprilTag and ML configurations.
 */
export type PipelineConfig = AprilTagConfig & MLConfig

export interface CameraStatus {
  connected: boolean
  error?: string
}

export interface CameraControls {
  orientation: number
  exposure_mode: 'auto' | 'manual'
  exposure_value: number
  exposure_min?: number
  exposure_max?: number
  exposure_step?: number
  gain_mode: 'auto' | 'manual'
  gain_value: number
  gain_min?: number
  gain_max?: number
  gain_step?: number
}

export interface DeviceInfo {
  identifier: string
  name: string
  camera_type: string
}

export interface MetricsSummary {
  pipelines: PipelineMetrics[]
  system: SystemMetrics
  thresholds: MetricsThresholds
}

export interface PipelineMetrics {
  camera_identifier: string
  pipeline_id: number
  pipeline_type: string
  fps: number
  latency_ms: {
    total: {
      avg_ms: number
      p95_ms: number
      max_ms: number
    }
    queue_wait: {
      avg_ms: number
    }
    processing: {
      avg_ms: number
    }
  }
  queue: {
    current_depth: number
    max_size: number
    utilization_pct: number
    high_watermark_pct: number
  }
  drops: {
    total: number
    window_total: number
    per_minute: number
  }
}

export interface SystemMetrics {
  cpu_usage_percent: number
  ram_usage_percent: number
  ram_used_mb: number
  ram_total_mb: number
  cpu_temperature: number
  active_pipelines: number
}

export interface MetricsThresholds {
  latency_warning_ms: number
  latency_critical_ms: number
  queue_warning: number
  queue_critical: number
}



export interface AprilTagField {
  name: string
  is_default: boolean
  field_json: string
}

// Detection Result Types

export interface Translation3D {
  x: number
  y: number
  z: number
}

export interface Quaternion {
  W: number
  X: number
  Y: number
  Z: number
}

export interface EulerAngles {
  pitch: number
  roll: number
  yaw: number
}

export interface Pose3D {
  translation: Translation3D
  rotation: {
    quaternion: Quaternion
  }
}

export interface AprilTagDetection {
  id: number
  pose_relative: Pose3D | null
  euler?: EulerAngles
  decision_margin?: number
  hamming?: number
}

export interface MLDetection {
  id?: number
  label: string
  confidence: number
  box?: [number, number, number, number]  // [x1, y1, x2, y2]
  bbox?: {
    x: number
    y: number
    width: number
    height: number
  }
  // Targeting data
  tx?: number       // Horizontal offset from crosshair in degrees
  ty?: number       // Vertical offset from crosshair in degrees
  ta?: number       // Target area as percentage of image (0-100)
  tv?: number       // Valid target (1 = valid, 0 = invalid)
  td?: number       // Distance to target in meters (from depth camera)
}

export interface RobotPose {
  translation: Translation3D
  rotation: {
    quaternion: Quaternion
  }
}

export interface PipelineResults {
  apriltag: AprilTagDetection[]
  ml: MLDetection[]
  robotPose: RobotPose | null
  processingTimeMs: number | null
}
