import { memo } from 'react'
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '@/components/ui/card'
import { MJPEGStream } from '@/components/shared/MJPEGStream'

type FeedType = 'default' | 'processed'

interface LiveFeedProps {
  selectedCameraId: string
  selectedPipelineId: string
  isCameraConnected: boolean
  feedType: FeedType
  feedSrc: string
  onFeedTypeChange: (type: FeedType) => void
}

export const LiveFeed = memo(function LiveFeed({
  selectedCameraId,
  selectedPipelineId,
  isCameraConnected,
  feedType,
  feedSrc,
  onFeedTypeChange,
}: LiveFeedProps) {
  return (
    <Card className="lg:col-span-2">
      <CardHeader>
        <div className="flex justify-between items-center">
          <div>
            <CardTitle>Live Feed</CardTitle>
            <CardDescription>Inspect the incoming stream or switch to the processed output</CardDescription>
          </div>
          <div className="flex gap-4 text-sm">
            <label className="flex items-center gap-2 cursor-pointer">
              <input
                type="radio"
                name="feedType"
                value="default"
                checked={feedType === 'default'}
                onChange={() => onFeedTypeChange('default')}
                disabled={!selectedCameraId}
                aria-label="Show default camera feed"
              />
              <span>Default feed</span>
            </label>
            <label className="flex items-center gap-2 cursor-pointer">
              <input
                type="radio"
                name="feedType"
                value="processed"
                checked={feedType === 'processed'}
                onChange={() => onFeedTypeChange('processed')}
                disabled={!selectedPipelineId}
                aria-label="Show processed pipeline feed"
              />
              <span>Processed feed</span>
            </label>
          </div>
        </div>
      </CardHeader>
      <CardContent>
        {!selectedCameraId ? (
          <div className="flex items-center justify-center h-96 text-muted-foreground">
            Select a camera to view its feed
          </div>
        ) : !isCameraConnected ? (
          <div className="flex items-center justify-center h-96 text-destructive">Camera is not connected</div>
        ) : feedSrc ? (
          <MJPEGStream src={feedSrc} alt="Camera Feed" className="w-full" />
        ) : (
          <div className="flex items-center justify-center h-96 text-muted-foreground">Loading feed...</div>
        )}
      </CardContent>
    </Card>
  )
})
