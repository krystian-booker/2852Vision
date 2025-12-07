import { useVisionSubscription } from './useVisionWebSocket'
import type { PipelineResults } from '@/types'

export function usePipelineResults(cameraId: number, pipelineId: number): PipelineResults | undefined {
  return useVisionSubscription<PipelineResults>('pipeline_results', { cameraId, pipelineId })
}
