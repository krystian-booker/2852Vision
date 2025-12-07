import { useVisionSubscription } from './useVisionWebSocket'

export interface CameraStatus {
  cameraId: number
  connected: boolean
  streaming: boolean
}

export function useCameraStatus(cameraId: number): CameraStatus | undefined {
  return useVisionSubscription<CameraStatus>('camera_status', { cameraId })
}
