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
}

export interface Pipeline {
  id: number
  camera_id: number
  name: string
  pipeline_type: string // 'AprilTag' | 'Coloured Shape' | 'Object Detection (ML)'
  config: string // JSON string of PipelineConfig
}

export interface PipelineConfig {
  // AprilTag config
  family?: string
  tag_size_m?: number
  threads?: number
  auto_threads?: boolean
  decimate?: number
  blur?: number
  refine_edges?: boolean
  decision_margin?: number
  pose_iterations?: number
  decode_sharpening?: number
  min_weight?: number
  edge_threshold?: number
  multi_tag_enabled?: boolean
  ransac_reproj_threshold?: number
  ransac_confidence?: number
  min_inliers?: number
  use_prev_guess?: boolean
  publish_field_pose?: boolean
  output_quaternion?: boolean
  multi_tag_error_threshold?: number

  // ColoredShape config
  hue_min?: number
  hue_max?: number
  saturation_min?: number
  saturation_max?: number
  value_min?: number
  value_max?: number
  min_area?: number
  max_area?: number
  min_aspect_ratio?: number
  max_aspect_ratio?: number
  min_fullness?: number

  // ML config
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
  [key: string]: any // Allow additional properties
}

export interface CameraStatus {
  connected: boolean
  error?: string
}

export interface CameraControls {
  orientation: number
  exposure_mode: string
  exposure_value: number
  gain_mode: string
  gain_value: number
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
