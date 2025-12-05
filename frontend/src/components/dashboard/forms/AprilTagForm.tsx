import { memo } from 'react'
import type { PipelineConfig, AprilTagDetection, RobotPose } from '@/types'
import { TAG_FAMILIES } from '@/constants/pipeline'
import { quaternionToEuler } from '@/lib/math'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { Switch } from '@/components/ui/switch'

interface AprilTagFormProps {
  config: PipelineConfig
  onChange: (updates: Partial<PipelineConfig>) => void
  results: AprilTagDetection[]
  robotPose: RobotPose | null
  processingTimeMs: number | null
}

export const AprilTagForm = memo(function AprilTagForm({
  config,
  onChange,
  results,
  robotPose,
  processingTimeMs,
}: AprilTagFormProps) {
  return (
    <div className="space-y-6">
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Configuration Inputs */}
        <div className="space-y-4">
          <h3 className="text-lg font-semibold">AprilTag Configuration</h3>

          <div className="space-y-2">
            <Label>Target Family</Label>
            <Select value={config.family as string} onValueChange={(value) => onChange({ family: value })}>
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {TAG_FAMILIES.map((family) => (
                  <SelectItem key={family} value={family}>
                    {family.replace('tag', '')}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>

          <div className="space-y-2">
            <Label>Tag Size (m)</Label>
            <Input
              type="number"
              step="0.001"
              min="0.001"
              value={config.tag_size_m ?? 0.165}
              onChange={(e) => onChange({ tag_size_m: parseFloat(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>Threads (1-8)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="1"
                max="8"
                value={config.threads ?? 4}
                onChange={(e) => onChange({ threads: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="1"
                max="8"
                value={config.threads ?? 4}
                onChange={(e) => onChange({ threads: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Decimate (1-8)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="1"
                max="8"
                step="0.1"
                value={config.decimate ?? 1}
                onChange={(e) => onChange({ decimate: parseFloat(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="1"
                max="8"
                step="0.1"
                value={config.decimate ?? 1}
                onChange={(e) => onChange({ decimate: parseFloat(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Blur (0-5)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="0"
                max="5"
                step="0.1"
                value={config.blur ?? 0}
                onChange={(e) => onChange({ blur: parseFloat(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="5"
                step="0.1"
                value={config.blur ?? 0}
                onChange={(e) => onChange({ blur: parseFloat(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Decision Margin Cutoff (0-250)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="0"
                max="250"
                value={config.decision_margin ?? 35}
                onChange={(e) => onChange({ decision_margin: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="250"
                value={config.decision_margin ?? 35}
                onChange={(e) => onChange({ decision_margin: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="flex items-center gap-2">
            <Switch
              checked={config.refine_edges ?? true}
              onCheckedChange={(checked) => onChange({ refine_edges: checked })}
            />
            <Label>Refine edges</Label>
          </div>

          <div className="space-y-2">
            <Label>Decode Sharpening (0-1)</Label>
            <Input
              type="number"
              min="0"
              max="1"
              step="0.01"
              value={config.decode_sharpening ?? 0.25}
              onChange={(e) => onChange({ decode_sharpening: parseFloat(e.target.value) })}
            />
          </div>
        </div>

        {/* Live Targets */}
        <div className="space-y-4">
          <h4 className="font-semibold">Live Targets</h4>
          <div className="overflow-x-auto">
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead>ID</TableHead>
                  <TableHead>X (m)</TableHead>
                  <TableHead>Y (m)</TableHead>
                  <TableHead>Z (m)</TableHead>
                  <TableHead>Pitch</TableHead>
                  <TableHead>Roll</TableHead>
                  <TableHead>Yaw</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {results.length === 0 ? (
                  <TableRow>
                    <TableCell colSpan={7} className="text-center text-muted-foreground">
                      No targets detected
                    </TableCell>
                  </TableRow>
                ) : (
                  results.map((target) => (
                    <TableRow key={target.id}>
                      <TableCell>{target.id}</TableCell>
                      <TableCell>{target.pose_relative?.translation?.x?.toFixed(3) ?? 'N/A'}</TableCell>
                      <TableCell>{target.pose_relative?.translation?.y?.toFixed(3) ?? 'N/A'}</TableCell>
                      <TableCell>{target.pose_relative?.translation?.z?.toFixed(3) ?? 'N/A'}</TableCell>
                      <TableCell>{target.euler?.pitch?.toFixed(2) ?? 'N/A'}</TableCell>
                      <TableCell>{target.euler?.roll?.toFixed(2) ?? 'N/A'}</TableCell>
                      <TableCell>{target.euler?.yaw?.toFixed(2) ?? 'N/A'}</TableCell>
                    </TableRow>
                  ))
                )}
              </TableBody>
            </Table>
          </div>

          {robotPose && (
            <div className="p-4 bg-surface rounded-lg space-y-3">
              <h4 className="font-semibold text-sm">Robot Pose (Field Coordinates)</h4>
              <div className="grid grid-cols-3 gap-3 text-sm">
                <div>
                  <span className="text-muted-foreground">X:</span> {robotPose.translation?.x?.toFixed(3) ?? 'N/A'} m
                </div>
                <div>
                  <span className="text-muted-foreground">Y:</span> {robotPose.translation?.y?.toFixed(3) ?? 'N/A'} m
                </div>
                <div>
                  <span className="text-muted-foreground">Z:</span> {robotPose.translation?.z?.toFixed(3) ?? 'N/A'} m
                </div>
              </div>
              {robotPose.rotation?.quaternion &&
                (() => {
                  const euler = quaternionToEuler(robotPose.rotation.quaternion)
                  return (
                    <div className="grid grid-cols-3 gap-3 text-sm">
                      <div>
                        <span className="text-muted-foreground">Pitch:</span> {euler.pitch.toFixed(2)}°
                      </div>
                      <div>
                        <span className="text-muted-foreground">Roll:</span> {euler.roll.toFixed(2)}°
                      </div>
                      <div>
                        <span className="text-muted-foreground">Yaw:</span> {euler.yaw.toFixed(2)}°
                      </div>
                    </div>
                  )
                })()}
              <div className="grid grid-cols-2 gap-3 text-sm border-t pt-2">
                <div>
                  <span className="text-muted-foreground">Tags used:</span> {results.length}
                </div>
                {processingTimeMs !== null && (
                  <div>
                    <span className="text-muted-foreground">Processing:</span> {processingTimeMs.toFixed(1)} ms
                  </div>
                )}
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  )
})
