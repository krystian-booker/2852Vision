import { memo } from 'react'
import type { Camera } from '@/types'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Badge } from '@/components/ui/badge'

interface CameraSelectorProps {
  cameras: Camera[]
  selectedCameraId: string
  onCameraChange: (cameraId: string) => void
  isConnected: boolean
}

/**
 * Camera selection dropdown with connection status indicator.
 */
export const CameraSelector = memo(function CameraSelector({
  cameras,
  selectedCameraId,
  onCameraChange,
  isConnected,
}: CameraSelectorProps) {
  const hasSelection = !!selectedCameraId && cameras.some((camera) => camera.id.toString() === selectedCameraId)
  const hasCameras = cameras.length > 0

  return (
    <div className="space-y-2">
      <div className="flex items-center justify-between">
        <Label htmlFor="camera-select">Camera</Label>
        {hasSelection && (
          <Badge variant={isConnected ? 'success' : 'secondary'}>
            {isConnected ? 'Connected' : 'Disconnected'}
          </Badge>
        )}
      </div>
      <Select
        value={hasSelection ? selectedCameraId : undefined}
        onValueChange={onCameraChange}
        disabled={!hasCameras}
      >
        <SelectTrigger id="camera-select">
          <SelectValue placeholder={cameras.length ? 'Select a camera...' : 'No cameras configured'} />
        </SelectTrigger>
        <SelectContent>
          {cameras.map((camera) => {
            const value = camera.id.toString()
            return (
              <SelectItem key={camera.id} value={value}>
                {camera.name}
              </SelectItem>
            )
          })}
        </SelectContent>
      </Select>
    </div>
  )
})
