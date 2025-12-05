import { memo } from 'react'
import type { PipelineResults, AprilTagDetection, MLDetection, RobotPose } from '@/types'
import { PIPELINE_TYPES } from '@/constants/pipeline'
import { quaternionToEuler } from '@/lib/math'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'

interface ResultsPanelProps {
  pipelineType: string
  results: PipelineResults
}

/**
 * Results panel displaying detection data based on pipeline type.
 */
export const ResultsPanel = memo(function ResultsPanel({
  pipelineType,
  results,
}: ResultsPanelProps) {
  if (pipelineType === PIPELINE_TYPES.APRILTAG) {
    return (
      <AprilTagResults
        detections={results.apriltag}
        robotPose={results.robotPose}
        processingTimeMs={results.processingTimeMs}
      />
    )
  }

  if (pipelineType === PIPELINE_TYPES.OBJECT_DETECTION_ML) {
    return (
      <MLResults
        detections={results.ml}
        processingTimeMs={results.processingTimeMs}
      />
    )
  }

  return null
})

interface AprilTagResultsProps {
  detections: AprilTagDetection[]
  robotPose: RobotPose | null
  processingTimeMs: number | null
}

const AprilTagResults = memo(function AprilTagResults({
  detections,
  robotPose,
  processingTimeMs,
}: AprilTagResultsProps) {
  return (
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
            {detections.length === 0 ? (
              <TableRow>
                <TableCell colSpan={7} className="text-center text-muted-foreground">
                  No targets detected
                </TableCell>
              </TableRow>
            ) : (
              detections.map((target) => (
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
        <RobotPoseDisplay
          robotPose={robotPose}
          tagCount={detections.length}
          processingTimeMs={processingTimeMs}
        />
      )}
    </div>
  )
})

interface RobotPoseDisplayProps {
  robotPose: RobotPose
  tagCount: number
  processingTimeMs: number | null
}

const RobotPoseDisplay = memo(function RobotPoseDisplay({
  robotPose,
  tagCount,
  processingTimeMs,
}: RobotPoseDisplayProps) {
  const euler = robotPose.rotation?.quaternion
    ? quaternionToEuler(robotPose.rotation.quaternion)
    : null

  return (
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
      {euler && (
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
      )}
      <div className="grid grid-cols-2 gap-3 text-sm border-t pt-2">
        <div>
          <span className="text-muted-foreground">Tags used:</span> {tagCount}
        </div>
        {processingTimeMs !== null && (
          <div>
            <span className="text-muted-foreground">Processing:</span> {processingTimeMs.toFixed(1)} ms
          </div>
        )}
      </div>
    </div>
  )
})

interface MLResultsProps {
  detections: MLDetection[]
  processingTimeMs: number | null
}

const MLResults = memo(function MLResults({
  detections,
  processingTimeMs,
}: MLResultsProps) {
  return (
    <div className="space-y-4">
      <div className="flex items-center justify-between">
        <h4 className="font-semibold">Detections</h4>
        {processingTimeMs !== null && (
          <span className="text-sm text-muted-foreground">
            {processingTimeMs.toFixed(1)} ms
          </span>
        )}
      </div>
      <div className="overflow-x-auto">
        <Table>
          <TableHeader>
            <TableRow>
              <TableHead>Label</TableHead>
              <TableHead>Confidence</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {detections.length === 0 ? (
              <TableRow>
                <TableCell colSpan={2} className="text-center text-muted-foreground">
                  No objects detected
                </TableCell>
              </TableRow>
            ) : (
              detections.map((detection, index) => (
                <TableRow key={detection.id ?? index}>
                  <TableCell>{detection.label}</TableCell>
                  <TableCell>{(detection.confidence * 100).toFixed(1)}%</TableCell>
                </TableRow>
              ))
            )}
          </TableBody>
        </Table>
      </div>
    </div>
  )
})
