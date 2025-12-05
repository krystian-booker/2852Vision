/**
 * Pipeline type constants and related types
 */

export const PIPELINE_TYPES = {
  APRILTAG: 'AprilTag',
  OBJECT_DETECTION_ML: 'Object Detection (ML)',
} as const

export type PipelineType = (typeof PIPELINE_TYPES)[keyof typeof PIPELINE_TYPES]

export const TAG_FAMILIES = ['tag36h11', 'tag16h5', 'tag25h9', 'tagCircle21h7'] as const
export type TagFamily = (typeof TAG_FAMILIES)[number]

export const ORIENTATION_OPTIONS = [
  { value: '0', label: 'Normal (0°)' },
  { value: '90', label: 'Rotate 90°' },
  { value: '180', label: 'Rotate 180°' },
  { value: '270', label: 'Rotate 270°' },
] as const

export const EXPOSURE_MODES = ['auto', 'manual'] as const
export type ExposureMode = (typeof EXPOSURE_MODES)[number]

export const GAIN_MODES = ['auto', 'manual'] as const
export type GainMode = (typeof GAIN_MODES)[number]
