import { memo, useRef, useCallback, useEffect } from 'react'
import type { CameraControls as CameraControlsType } from '@/types'
import { DEBOUNCE_DELAYS } from '@/constants/dashboard'
import { Label } from '@/components/ui/label'
import { Input } from '@/components/ui/input'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'

interface CameraControlsProps {
  controls: CameraControlsType
  disabled: boolean
  onControlsChange: (updates: Partial<CameraControlsType>) => void
  onSave: (controls: CameraControlsType) => void
}

/**
 * Camera control panel for exposure, gain, and orientation settings.
 * Changes are debounced before saving to the backend.
 */
export const CameraControls = memo(function CameraControls({
  controls,
  disabled,
  onControlsChange,
  onSave,
}: CameraControlsProps) {
  const controlsTimerRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined)
  const controlsRef = useRef(controls)

  // Keep ref in sync
  useEffect(() => {
    controlsRef.current = controls
  }, [controls])

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (controlsTimerRef.current) {
        clearTimeout(controlsTimerRef.current)
      }
    }
  }, [])

  const queueSave = useCallback(
    (updates: Partial<CameraControlsType>) => {
      onControlsChange(updates)

      if (controlsTimerRef.current) {
        clearTimeout(controlsTimerRef.current)
      }

      controlsTimerRef.current = setTimeout(() => {
        onSave({ ...controlsRef.current, ...updates })
      }, DEBOUNCE_DELAYS.CONTROLS)
    },
    [onControlsChange, onSave]
  )

  return (
    <div className="space-y-4">
      {/* Orientation */}
      <div className="space-y-2">
        <Label htmlFor="orientation">Orientation</Label>
        <Select
          value={controls.orientation.toString()}
          onValueChange={(value) => queueSave({ orientation: parseInt(value) })}
          disabled={disabled}
        >
          <SelectTrigger id="orientation">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="0">Normal (0°)</SelectItem>
            <SelectItem value="90">Rotate 90°</SelectItem>
            <SelectItem value="180">Rotate 180°</SelectItem>
            <SelectItem value="270">Rotate 270°</SelectItem>
          </SelectContent>
        </Select>
      </div>

      {/* Exposure Mode */}
      <div className="space-y-2">
        <Label>Exposure Mode</Label>
        <Select
          value={controls.exposure_mode}
          onValueChange={(value) => queueSave({ exposure_mode: value as 'auto' | 'manual' })}
          disabled={disabled}
        >
          <SelectTrigger>
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="auto">Auto</SelectItem>
            <SelectItem value="manual">Manual</SelectItem>
          </SelectContent>
        </Select>
      </div>

      {/* Exposure Value */}
      <div className="space-y-2">
        <Label>Exposure Value</Label>
        <div className="flex gap-2">
          <input
            type="range"
            min={controls.exposure_min ?? 0}
            max={controls.exposure_max ?? 10000}
            step={controls.exposure_step ?? 1}
            value={controls.exposure_value}
            onChange={(e) => queueSave({ exposure_value: parseInt(e.target.value) })}
            disabled={controls.exposure_mode !== 'manual'}
            className="flex-1"
            aria-label="Exposure value slider"
          />
          <Input
            type="number"
            min={controls.exposure_min ?? 0}
            max={controls.exposure_max ?? 10000}
            step={controls.exposure_step ?? 1}
            value={controls.exposure_value}
            onChange={(e) => queueSave({ exposure_value: parseInt(e.target.value) })}
            disabled={controls.exposure_mode !== 'manual'}
            className="w-20"
            aria-label="Exposure value"
          />
        </div>
      </div>

      {/* Gain Mode */}
      <div className="space-y-2">
        <Label>Gain Mode</Label>
        <Select
          value={controls.gain_mode}
          onValueChange={(value) => queueSave({ gain_mode: value as 'auto' | 'manual' })}
          disabled={disabled}
        >
          <SelectTrigger>
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="auto">Auto</SelectItem>
            <SelectItem value="manual">Manual</SelectItem>
          </SelectContent>
        </Select>
      </div>

      {/* Gain Value */}
      <div className="space-y-2">
        <Label>Gain Value</Label>
        <div className="flex gap-2">
          <input
            type="range"
            min={controls.gain_min ?? 0}
            max={controls.gain_max ?? 100}
            step={controls.gain_step ?? 1}
            value={controls.gain_value}
            onChange={(e) => queueSave({ gain_value: parseInt(e.target.value) })}
            disabled={controls.gain_mode !== 'manual'}
            className="flex-1"
            aria-label="Gain value slider"
          />
          <Input
            type="number"
            min={controls.gain_min ?? 0}
            max={controls.gain_max ?? 100}
            step={controls.gain_step ?? 1}
            value={controls.gain_value}
            onChange={(e) => queueSave({ gain_value: parseInt(e.target.value) })}
            disabled={controls.gain_mode !== 'manual'}
            className="w-20"
            aria-label="Gain value"
          />
        </div>
      </div>
    </div>
  )
})
