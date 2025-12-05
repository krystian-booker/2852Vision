import { memo } from 'react'
import type { PipelineConfig, MLDetection } from '@/types'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Button } from '@/components/ui/button'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'

interface MLFormProps {
  config: PipelineConfig
  onChange: (updates: Partial<PipelineConfig>) => void
  results: MLDetection[]
  labelOptions: string[]
  onFileUpload: (event: React.ChangeEvent<HTMLInputElement>, type: 'model' | 'labels') => void
  onFileDelete: (type: 'model' | 'labels') => void
}

export const MLForm = memo(function MLForm({
  config,
  onChange,
  results,
  labelOptions,
  onFileUpload,
  onFileDelete,
}: MLFormProps) {
  return (
    <div className="space-y-6">
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <div className="space-y-4">
          <h3 className="text-lg font-semibold">Object Detection (ML)</h3>

          <div className="space-y-2">
            <Label>ML Model File</Label>
            <div className="flex gap-2">
              <Input type="file" accept=".onnx,.tflite" onChange={(e) => onFileUpload(e, 'model')} />
              {config.model_filename && (
                <Button variant="destructive" onClick={() => onFileDelete('model')}>
                  Remove
                </Button>
              )}
            </div>
            {config.model_filename && (
              <p className="text-sm text-muted-foreground">Current: {config.model_filename as string}</p>
            )}
          </div>

          <div className="space-y-2">
            <Label>Labels File</Label>
            <div className="flex gap-2">
              <Input type="file" accept=".txt,.names" onChange={(e) => onFileUpload(e, 'labels')} />
              {config.labels_filename && (
                <Button variant="destructive" onClick={() => onFileDelete('labels')}>
                  Remove
                </Button>
              )}
            </div>
            {config.labels_filename && (
              <p className="text-sm text-muted-foreground">Current: {config.labels_filename as string}</p>
            )}
          </div>

          <div className="space-y-2">
            <Label>Confidence Threshold</Label>
            <Input
              type="number"
              min="0"
              max="1"
              step="0.01"
              value={config.confidence_threshold ?? 0.5}
              onChange={(e) => onChange({ confidence_threshold: parseFloat(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>NMS Threshold</Label>
            <Input
              type="number"
              min="0"
              max="1"
              step="0.01"
              value={config.nms_iou_threshold ?? 0.45}
              onChange={(e) => onChange({ nms_iou_threshold: parseFloat(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>Image Size (px)</Label>
            <Input
              type="number"
              min="32"
              max="2048"
              step="32"
              value={config.img_size ?? 640}
              onChange={(e) => onChange({ img_size: parseInt(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>Target Classes</Label>
            <select
              multiple
              size={6}
              className="w-full border rounded-md p-2"
              value={(config.target_classes as string[]) || []}
              onChange={(e) => {
                const selected = Array.from(e.target.selectedOptions, (option) => option.value)
                onChange({ target_classes: selected })
              }}
            >
              {labelOptions.map((label) => (
                <option key={label} value={label}>
                  {label}
                </option>
              ))}
            </select>
            {labelOptions.length === 0 && (
              <p className="text-sm text-muted-foreground">Upload labels file to enable filtering</p>
            )}
          </div>
        </div>

        <div className="space-y-4">
          <h4 className="font-semibold">Live Detections</h4>
          <div className="overflow-x-auto">
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead>Label</TableHead>
                  <TableHead>Confidence</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {results.length === 0 ? (
                  <TableRow>
                    <TableCell colSpan={2} className="text-center text-muted-foreground">
                      No detections
                    </TableCell>
                  </TableRow>
                ) : (
                  results.map((det, index) => (
                    <TableRow key={det.id ?? `${det.label}-${index}`}>
                      <TableCell>{det.label || 'unknown'}</TableCell>
                      <TableCell>{det.confidence?.toFixed(3) ?? 'N/A'}</TableCell>
                    </TableRow>
                  ))
                )}
              </TableBody>
            </Table>
          </div>
        </div>
      </div>
    </div>
  )
})
