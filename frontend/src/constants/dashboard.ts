import type { PipelineConfig } from '@/types'

/**
 * Debounce delays for various dashboard operations (in milliseconds).
 */
export const DEBOUNCE_DELAYS = {
  /** Delay for camera control changes (exposure, gain, orientation) */
  CONTROLS: 400,
  /** Delay for pipeline configuration changes */
  CONFIG: 600,
} as const

/**
 * Polling intervals for dashboard data fetching (in milliseconds).
 */
export const POLLING_INTERVALS = {
  /** Interval for fetching pipeline results */
  RESULTS: 1000,
} as const

/**
 * Default camera control values.
 */
export const DEFAULT_CAMERA_CONTROLS = {
  orientation: 0,
  exposure_mode: 'auto' as const,
  exposure_value: 500,
  exposure_min: 0,
  exposure_max: 10000,
  exposure_step: 1,
  gain_mode: 'auto' as const,
  gain_value: 50,
  gain_min: 0,
  gain_max: 100,
  gain_step: 1,
}

/**
 * Default AprilTag pipeline configuration.
 */
export const APRILTAG_DEFAULTS: PipelineConfig = {
  family: 'tag36h11',
  tag_size_m: 0.165,
  threads: 4,
  decimate: 1,
  blur: 0,
  refine_edges: true,
  decision_margin: 35,
  pose_iterations: 40,
  decode_sharpening: 0.25,
  min_weight: 0,
  edge_threshold: 0,
  ransac_reproj_threshold: 1.2,
  ransac_confidence: 0.999,
  min_inliers: 12,
  use_prev_guess: true,
  publish_field_pose: true,
  output_quaternion: true,
}

/**
 * Default ML pipeline configuration.
 */
export const ML_DEFAULTS: PipelineConfig = {
  model_type: 'yolo',
  confidence_threshold: 0.5,
  nms_iou_threshold: 0.45,
  target_classes: [],
  onnx_provider: 'CPUExecutionProvider',
  accelerator: 'none',
  max_detections: 100,
  img_size: 640,
  model_filename: '',
  labels_filename: '',
  tflite_delegate: null,
}

/**
 * MJPEG streaming port.
 */
export const MJPEG_PORT = 5805
