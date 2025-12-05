import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query'
import { api } from './api'
import type { Camera, Pipeline, PipelineConfig, PipelineResults, CameraControls } from '@/types'
import { POLLING_INTERVALS } from '@/constants/dashboard'

// Query Keys
export const queryKeys = {
  cameras: ['cameras'] as const,
  camera: (id: string) => ['cameras', id] as const,
  cameraStatus: (id: string) => ['cameras', id, 'status'] as const,
  cameraControls: (id: string) => ['cameras', id, 'controls'] as const,
  pipelines: (cameraId: string) => ['pipelines', cameraId] as const,
  pipeline: (id: string) => ['pipelines', 'detail', id] as const,
  pipelineResults: (cameraId: string, pipelineId: string) =>
    ['pipelines', cameraId, pipelineId, 'results'] as const,
  pipelineLabels: (id: string) => ['pipelines', id, 'labels'] as const,
  mlAvailability: ['ml', 'availability'] as const,
}

/**
 * Fetch all cameras.
 */
export function useCameras() {
  return useQuery({
    queryKey: queryKeys.cameras,
    queryFn: () => api.get<Camera[]>('/api/cameras'),
  })
}

/**
 * Fetch camera connection status.
 */
export function useCameraStatus(cameraId: string, enabled = true) {
  return useQuery({
    queryKey: queryKeys.cameraStatus(cameraId),
    queryFn: () => api.get<{ connected: boolean }>(`/api/cameras/status/${cameraId}`),
    enabled: enabled && !!cameraId,
    refetchInterval: 5000,
  })
}

/**
 * Fetch camera controls (exposure, gain, orientation).
 */
export function useCameraControls(cameraId: string, enabled = true) {
  return useQuery({
    queryKey: queryKeys.cameraControls(cameraId),
    queryFn: () => api.get<CameraControls>(`/api/cameras/controls/${cameraId}`),
    enabled: enabled && !!cameraId,
  })
}

/**
 * Update camera controls mutation.
 */
export function useUpdateCameraControls(cameraId: string) {
  const queryClient = useQueryClient()

  return useMutation({
    mutationFn: (controls: Partial<CameraControls>) =>
      api.post(`/api/cameras/update_controls/${cameraId}`, controls),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.cameraControls(cameraId) })
    },
  })
}

/**
 * Fetch pipelines for a camera.
 */
export function usePipelines(cameraId: string, enabled = true) {
  return useQuery({
    queryKey: queryKeys.pipelines(cameraId),
    queryFn: () => api.get<Pipeline[]>(`/api/cameras/${cameraId}/pipelines`),
    enabled: enabled && !!cameraId,
  })
}

/**
 * Create a new pipeline.
 */
export function useCreatePipeline(cameraId: string) {
  const queryClient = useQueryClient()

  return useMutation({
    mutationFn: (data: { name: string; pipeline_type: string }) =>
      api.post(`/api/cameras/${cameraId}/pipelines`, data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.pipelines(cameraId) })
    },
  })
}

/**
 * Update a pipeline.
 */
export function useUpdatePipeline(cameraId: string) {
  const queryClient = useQueryClient()

  return useMutation({
    mutationFn: ({ id, ...data }: { id: number; name: string; pipeline_type: string }) =>
      api.put(`/api/pipelines/${id}`, data),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.pipelines(cameraId) })
    },
  })
}

/**
 * Delete a pipeline.
 */
export function useDeletePipeline(cameraId: string) {
  const queryClient = useQueryClient()

  return useMutation({
    mutationFn: (pipelineId: number) => api.delete(`/api/pipelines/${pipelineId}`),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.pipelines(cameraId) })
    },
  })
}

/**
 * Update pipeline configuration.
 */
export function useUpdatePipelineConfig(pipelineId: string) {
  return useMutation({
    mutationFn: (config: PipelineConfig) =>
      api.put(`/api/pipelines/${pipelineId}/config`, config),
  })
}

/**
 * Fetch pipeline results with polling.
 */
export function usePipelineResults(
  cameraId: string,
  pipelineId: string,
  enabled = true
) {
  return useQuery({
    queryKey: queryKeys.pipelineResults(cameraId, pipelineId),
    queryFn: async () => {
      const data = await api.get<Record<string, unknown>>(`/api/cameras/results/${cameraId}`)

      const pipelineResults = Array.isArray(data)
        ? data.find((r: { pipeline_id?: number }) => String(r.pipeline_id) === String(pipelineId))
        : (data as Record<string, unknown>)?.[pipelineId]

      if (!pipelineResults) {
        return { apriltag: [], ml: [], robotPose: null, processingTimeMs: null } as PipelineResults
      }

      return pipelineResults as Record<string, unknown>
    },
    enabled: enabled && !!cameraId && !!pipelineId,
    refetchInterval: POLLING_INTERVALS.RESULTS,
  })
}

/**
 * Fetch labels for an ML pipeline.
 */
export function usePipelineLabels(pipelineId: string, enabled = true) {
  return useQuery({
    queryKey: queryKeys.pipelineLabels(pipelineId),
    queryFn: () => api.get<{ labels: string[] }>(`/api/pipelines/${pipelineId}/labels`),
    enabled: enabled && !!pipelineId,
  })
}

/**
 * Fetch ML availability info.
 */
export function useMLAvailability() {
  return useQuery({
    queryKey: queryKeys.mlAvailability,
    queryFn: () => api.get<Record<string, unknown>>('/api/pipelines/ml/availability'),
  })
}

/**
 * Upload file to pipeline (model or labels).
 */
export function useUploadPipelineFile(pipelineId: string) {
  const queryClient = useQueryClient()

  return useMutation({
    mutationFn: (formData: FormData) =>
      api.uploadFile(`/api/pipelines/${pipelineId}/files`, formData),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.pipelineLabels(pipelineId) })
    },
  })
}

/**
 * Delete file from pipeline.
 */
export function useDeletePipelineFile(pipelineId: string) {
  const queryClient = useQueryClient()

  return useMutation({
    mutationFn: (type: 'model' | 'labels') =>
      api.post(`/api/pipelines/${pipelineId}/files/delete`, { type }),
    onSuccess: () => {
      queryClient.invalidateQueries({ queryKey: queryKeys.pipelineLabels(pipelineId) })
    },
  })
}
