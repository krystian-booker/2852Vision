import { memo, useRef, useCallback, useEffect } from 'react'
import type { PipelineConfig, PipelineResults } from '@/types'
import { PIPELINE_TYPES } from '@/constants/pipeline'
import { DEBOUNCE_DELAYS } from '@/constants/dashboard'
import { AprilTagForm, MLForm } from './forms'

interface PipelineConfigPanelProps {
  pipelineType: string
  config: PipelineConfig
  results: PipelineResults
  labelOptions: string[]
  onConfigChange: (updates: Partial<PipelineConfig>) => void
  onSave: (config: PipelineConfig) => void
  onFileUpload: (event: React.ChangeEvent<HTMLInputElement>, type: 'model' | 'labels') => void
  onFileDelete: (type: 'model' | 'labels') => void
}

/**
 * Pipeline configuration panel that displays the appropriate form
 * based on pipeline type (AprilTag or ML).
 */
export const PipelineConfigPanel = memo(function PipelineConfigPanel({
  pipelineType,
  config,
  results,
  labelOptions,
  onConfigChange,
  onSave,
  onFileUpload,
  onFileDelete,
}: PipelineConfigPanelProps) {
  const configTimerRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined)
  const configRef = useRef(config)

  // Keep ref in sync
  useEffect(() => {
    configRef.current = config
  }, [config])

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (configTimerRef.current) {
        clearTimeout(configTimerRef.current)
      }
    }
  }, [])

  const queueSave = useCallback(
    (updates: Partial<PipelineConfig>) => {
      onConfigChange(updates)

      if (configTimerRef.current) {
        clearTimeout(configTimerRef.current)
      }

      configTimerRef.current = setTimeout(() => {
        onSave({ ...configRef.current, ...updates })
      }, DEBOUNCE_DELAYS.CONFIG)
    },
    [onConfigChange, onSave]
  )

  if (pipelineType === PIPELINE_TYPES.APRILTAG) {
    return (
      <AprilTagForm
        config={config}
        onChange={queueSave}
        results={results.apriltag}
        robotPose={results.robotPose}
        processingTimeMs={results.processingTimeMs}
      />
    )
  }

  if (pipelineType === PIPELINE_TYPES.OBJECT_DETECTION_ML) {
    return (
      <MLForm
        config={config}
        onChange={queueSave}
        results={results.ml}
        labelOptions={labelOptions}
        onFileUpload={onFileUpload}
        onFileDelete={onFileDelete}
      />
    )
  }

  return (
    <div className="text-center text-muted-foreground py-12">
      Select a pipeline to configure its settings
    </div>
  )
})
