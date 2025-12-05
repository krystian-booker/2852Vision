import { memo } from 'react'
import type { Pipeline } from '@/types'
import { PIPELINE_TYPES } from '@/constants/pipeline'
import { Button } from '@/components/ui/button'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Plus, Edit2, Trash2 } from 'lucide-react'

interface PipelineSelectorProps {
  pipelines: Pipeline[]
  selectedPipelineId: string
  pipelineType: string
  disabled: boolean
  onPipelineChange: (pipelineId: string) => void
  onPipelineTypeChange: (type: string) => void
  onAddPipeline: () => void
  onEditPipeline: () => void
  onDeletePipeline: () => void
}

/**
 * Pipeline selection dropdown with type selector and CRUD actions.
 */
export const PipelineSelector = memo(function PipelineSelector({
  pipelines,
  selectedPipelineId,
  pipelineType,
  disabled,
  onPipelineChange,
  onPipelineTypeChange,
  onAddPipeline,
  onEditPipeline,
  onDeletePipeline,
}: PipelineSelectorProps) {
  return (
    <div className="space-y-4">
      {/* Pipeline Select */}
      <div className="space-y-2">
        <Label>Pipeline</Label>
        <div className="flex gap-2">
          <Select
            value={selectedPipelineId}
            onValueChange={onPipelineChange}
            disabled={!pipelines.length}
          >
            <SelectTrigger className="flex-1">
              <SelectValue placeholder="Select pipeline..." />
            </SelectTrigger>
            <SelectContent>
              {pipelines.map((pipeline) => (
                <SelectItem key={pipeline.id} value={pipeline.id.toString()}>
                  {pipeline.name}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          <Button
            size="icon"
            variant="outline"
            onClick={onAddPipeline}
            disabled={disabled}
            title="Add pipeline"
            aria-label="Add pipeline"
          >
            <Plus className="h-4 w-4" />
          </Button>
          <Button
            size="icon"
            variant="outline"
            onClick={onEditPipeline}
            disabled={!selectedPipelineId}
            title="Rename pipeline"
            aria-label="Rename pipeline"
          >
            <Edit2 className="h-4 w-4" />
          </Button>
          <Button
            size="icon"
            variant="outline"
            onClick={onDeletePipeline}
            disabled={!selectedPipelineId}
            title="Delete pipeline"
            aria-label="Delete pipeline"
          >
            <Trash2 className="h-4 w-4" />
          </Button>
        </div>
      </div>

      {/* Pipeline Type */}
      <div className="space-y-2">
        <Label htmlFor="pipeline-type">Pipeline Type</Label>
        <Select
          value={pipelineType}
          onValueChange={onPipelineTypeChange}
          disabled={!selectedPipelineId}
        >
          <SelectTrigger id="pipeline-type">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value={PIPELINE_TYPES.APRILTAG}>AprilTag</SelectItem>
            <SelectItem value={PIPELINE_TYPES.OBJECT_DETECTION_ML}>Object Detection (ML)</SelectItem>
          </SelectContent>
        </Select>
      </div>
    </div>
  )
})
