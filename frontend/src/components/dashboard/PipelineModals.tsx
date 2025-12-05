import { memo } from 'react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
  DialogFooter,
} from '@/components/ui/dialog'
import { PIPELINE_TYPES } from '@/constants/pipeline'

interface PipelineModalProps {
  open: boolean
  onOpenChange: (open: boolean) => void
  mode: 'add' | 'edit'
  name: string
  onNameChange: (name: string) => void
  type: string
  onTypeChange: (type: string) => void
  saving: boolean
  onSubmit: () => void
}

export const PipelineModal = memo(function PipelineModal({
  open,
  onOpenChange,
  mode,
  name,
  onNameChange,
  type,
  onTypeChange,
  saving,
  onSubmit,
}: PipelineModalProps) {
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>{mode === 'add' ? 'Add Pipeline' : 'Edit Pipeline'}</DialogTitle>
          <DialogDescription>
            {mode === 'add'
              ? 'Define a fresh pipeline for the selected camera.'
              : 'Update the current pipeline.'}
          </DialogDescription>
        </DialogHeader>
        <div className="space-y-4">
          <div className="space-y-2">
            <Label htmlFor="pipeline-name">Name</Label>
            <Input
              id="pipeline-name"
              value={name}
              onChange={(e) => onNameChange(e.target.value)}
              placeholder="Enter pipeline name"
            />
          </div>
          <div className="space-y-2">
            <Label htmlFor="pipeline-modal-type">Type</Label>
            <Select value={type} onValueChange={onTypeChange}>
              <SelectTrigger id="pipeline-modal-type">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectItem value={PIPELINE_TYPES.APRILTAG}>AprilTag</SelectItem>
                <SelectItem value={PIPELINE_TYPES.OBJECT_DETECTION_ML}>Object Detection (ML)</SelectItem>
              </SelectContent>
            </Select>
            {mode === 'edit' && (
              <p className="text-sm text-muted-foreground">Changing the pipeline type will reset its configuration.</p>
            )}
          </div>
        </div>
        <DialogFooter>
          <Button variant="outline" onClick={() => onOpenChange(false)} disabled={saving}>
            Cancel
          </Button>
          <Button onClick={onSubmit} disabled={saving}>
            {saving ? 'Saving...' : 'Save'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
})

interface DeleteModalProps {
  open: boolean
  onOpenChange: (open: boolean) => void
  pipelineName: string
  saving: boolean
  onConfirm: () => void
}

export const DeletePipelineModal = memo(function DeletePipelineModal({
  open,
  onOpenChange,
  pipelineName,
  saving,
  onConfirm,
}: DeleteModalProps) {
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Delete Pipeline</DialogTitle>
          <DialogDescription>Confirm removal to free the slot for another configuration.</DialogDescription>
        </DialogHeader>
        <p>
          Are you sure you want to remove <strong>{pipelineName}</strong>? This cannot be undone.
        </p>
        <DialogFooter>
          <Button variant="outline" onClick={() => onOpenChange(false)} disabled={saving}>
            Cancel
          </Button>
          <Button variant="destructive" onClick={onConfirm} disabled={saving}>
            {saving ? 'Deleting...' : 'Delete'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
})
