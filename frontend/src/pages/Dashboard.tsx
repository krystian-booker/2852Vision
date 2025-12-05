import { useState, useEffect, useCallback, useMemo } from 'react'
import { useAppStore } from '@/store/useAppStore'
import type { PipelineConfig, PipelineResults, CameraControls as CameraControlsType, AprilTagDetection } from '@/types'
import { PIPELINE_TYPES } from '@/constants/pipeline'
import { APRILTAG_DEFAULTS, ML_DEFAULTS, MJPEG_PORT } from '@/constants/dashboard'
import { quaternionToEuler } from '@/lib/math'
import {
  useCameras,
  useCameraStatus,
  useCameraControls,
  useUpdateCameraControls,
  usePipelines,
  useCreatePipeline,
  useUpdatePipeline,
  useDeletePipeline,
  useUpdatePipelineConfig,
  usePipelineResults,
  usePipelineLabels,
  useMLAvailability,
  useUploadPipelineFile,
  useDeletePipelineFile,
} from '@/lib/queries'
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '@/components/ui/card'
import { Badge } from '@/components/ui/badge'
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert'
import { TriangleAlert } from 'lucide-react'
import { useToast } from '@/hooks/use-toast'
import { Link } from 'react-router-dom'
import {
  LiveFeed,
  PipelineModal,
  DeletePipelineModal,
  CameraSelector,
  CameraControls,
  PipelineSelector,
  PipelineConfigPanel,
} from '@/components/dashboard'
import { DEFAULT_CAMERA_CONTROLS } from '@/constants/dashboard'

export default function Dashboard() {
  const { toast } = useToast()
  const setCameras = useAppStore((state) => state.setCameras)

  // Selection state
  const [selectedCameraId, setSelectedCameraId] = useState<string>('')
  const [selectedPipelineId, setSelectedPipelineId] = useState<string>('')
  const [feedType, setFeedType] = useState<'default' | 'processed'>('default')

  // Local state for controls and config (for optimistic updates)
  const [localControls, setLocalControls] = useState<CameraControlsType>(DEFAULT_CAMERA_CONTROLS)
  const [pipelineConfig, setPipelineConfig] = useState<PipelineConfig>({})
  const [pipelineType, setPipelineType] = useState<string>('')
  const [labelOptions, setLabelOptions] = useState<string[]>([])

  // Modal states
  const [pipelineModalOpen, setPipelineModalOpen] = useState(false)
  const [pipelineModalMode, setPipelineModalMode] = useState<'add' | 'edit'>('add')
  const [pipelineModalName, setPipelineModalName] = useState('')
  const [pipelineModalType, setPipelineModalType] = useState<string>(PIPELINE_TYPES.APRILTAG)
  const [deleteModalOpen, setDeleteModalOpen] = useState(false)

  // React Query hooks
  const { data: cameras = [] } = useCameras()
  const { data: cameraStatus } = useCameraStatus(selectedCameraId, !!selectedCameraId)
  const { data: controlsData } = useCameraControls(selectedCameraId, !!selectedCameraId)
  const { data: pipelines = [] } = usePipelines(selectedCameraId, !!selectedCameraId)
  const { data: rawResults } = usePipelineResults(
    selectedCameraId,
    selectedPipelineId,
    !!selectedCameraId && !!selectedPipelineId
  )
  const { data: labelsData } = usePipelineLabels(
    selectedPipelineId,
    !!selectedPipelineId && pipelineType === PIPELINE_TYPES.OBJECT_DETECTION_ML
  )
  // ML availability is fetched but used by child components via the query cache
  useMLAvailability()

  // Mutations
  const updateControlsMutation = useUpdateCameraControls(selectedCameraId)
  const createPipelineMutation = useCreatePipeline(selectedCameraId)
  const updatePipelineMutation = useUpdatePipeline(selectedCameraId)
  const deletePipelineMutation = useDeletePipeline(selectedCameraId)
  const updateConfigMutation = useUpdatePipelineConfig(selectedPipelineId)
  const uploadFileMutation = useUploadPipelineFile(selectedPipelineId)
  const deleteFileMutation = useDeletePipelineFile(selectedPipelineId)

  const isCameraConnected = cameraStatus?.connected ?? false

  // Sync cameras to global store
  useEffect(() => {
    setCameras(cameras)
  }, [cameras, setCameras])

  // Auto-select first camera
  useEffect(() => {
    if (!cameras.length) {
      setSelectedCameraId('')
      return
    }

    const currentExists = cameras.some((camera) => camera.id.toString() === selectedCameraId)

    if (!selectedCameraId || !currentExists) {
      setSelectedCameraId(cameras[0].id.toString())
    }
  }, [cameras, selectedCameraId])

  // Sync controls from server
  useEffect(() => {
    if (controlsData) {
      setLocalControls({
        orientation: controlsData.orientation ?? 0,
        exposure_mode: controlsData.exposure_mode || 'auto',
        exposure_value: controlsData.exposure_value ?? 500,
        exposure_min: controlsData.exposure_min ?? 0,
        exposure_max: controlsData.exposure_max ?? 10000,
        exposure_step: controlsData.exposure_step ?? 1,
        gain_mode: controlsData.gain_mode || 'auto',
        gain_value: controlsData.gain_value ?? 50,
        gain_min: controlsData.gain_min ?? 0,
        gain_max: controlsData.gain_max ?? 100,
        gain_step: controlsData.gain_step ?? 1,
      })
    }
  }, [controlsData])

  // Auto-select first pipeline
  useEffect(() => {
    if (pipelines.length > 0) {
      const currentExists = pipelines.find((p) => p.id.toString() === selectedPipelineId)
      if (!selectedPipelineId || !currentExists) {
        setSelectedPipelineId(pipelines[0].id.toString())
      }
    } else {
      setSelectedPipelineId('')
      setPipelineType('')
      setPipelineConfig({})
    }
  }, [pipelines, selectedPipelineId])

  // Sync labels
  useEffect(() => {
    if (labelsData) {
      setLabelOptions(labelsData.labels || [])
    }
  }, [labelsData])

  // Selected entities
  const selectedPipeline = useMemo(
    () => pipelines.find((p) => p.id.toString() === selectedPipelineId),
    [pipelines, selectedPipelineId]
  )

  const selectedCamera = useMemo(
    () => cameras.find((c) => c.id.toString() === selectedCameraId),
    [cameras, selectedCameraId]
  )

  // Load pipeline config when selection changes
  useEffect(() => {
    if (!selectedPipeline) return

    setPipelineType(selectedPipeline.pipeline_type)

    try {
      const config = JSON.parse(selectedPipeline.config || '{}')
      let defaults: PipelineConfig = {}
      if (selectedPipeline.pipeline_type === PIPELINE_TYPES.APRILTAG) {
        defaults = APRILTAG_DEFAULTS
      } else if (selectedPipeline.pipeline_type === PIPELINE_TYPES.OBJECT_DETECTION_ML) {
        defaults = ML_DEFAULTS
      }
      setPipelineConfig({ ...defaults, ...config })
    } catch {
      setPipelineConfig({})
    }
  }, [selectedPipeline])

  // Process results with euler angles
  const results: PipelineResults = useMemo(() => {
    if (!rawResults) {
      return { apriltag: [], ml: [], robotPose: null, processingTimeMs: null }
    }

    const resultsData = rawResults as Record<string, unknown>

    if (pipelineType === PIPELINE_TYPES.APRILTAG) {
      const detections = ((resultsData.detections as AprilTagDetection[]) || []).map((det) => {
        const euler = det.pose_relative?.rotation?.quaternion
          ? quaternionToEuler(det.pose_relative.rotation.quaternion)
          : undefined
        return { ...det, euler }
      })
      return {
        apriltag: detections,
        ml: [],
        robotPose: (resultsData.robot_pose as PipelineResults['robotPose']) || null,
        processingTimeMs: (resultsData.processing_time_ms as number) || null,
      }
    }

    if (pipelineType === PIPELINE_TYPES.OBJECT_DETECTION_ML) {
      return {
        apriltag: [],
        ml: (resultsData.detections as PipelineResults['ml']) || [],
        robotPose: null,
        processingTimeMs: (resultsData.processing_time_ms as number) || null,
      }
    }

    return { apriltag: [], ml: [], robotPose: null, processingTimeMs: null }
  }, [rawResults, pipelineType])

  // Feed source
  const feedSrc = useMemo(() => {
    if (!selectedCameraId || !isCameraConnected) return ''
    if (feedType === 'processed' && selectedPipelineId) {
      return `http://${window.location.hostname}:${MJPEG_PORT}/pipeline/${selectedPipelineId}`
    }
    return `http://${window.location.hostname}:${MJPEG_PORT}/camera/${selectedCameraId}`
  }, [selectedCameraId, selectedPipelineId, feedType, isCameraConnected])

  // Handlers
  const handleCameraChange = useCallback((id: string) => {
    setSelectedCameraId(id)
    setSelectedPipelineId('')
    setPipelineType('')
    setPipelineConfig({})
  }, [])

  const handleControlsChange = useCallback((updates: Partial<CameraControlsType>) => {
    setLocalControls((prev) => ({ ...prev, ...updates }))
  }, [])

  const handleControlsSave = useCallback(
    async (controls: CameraControlsType) => {
      try {
        await updateControlsMutation.mutateAsync(controls)
      } catch (error: unknown) {
        const message = error instanceof Error ? error.message : 'Failed to save controls'
        toast({ title: 'Error', description: message, variant: 'destructive' })
      }
    },
    [updateControlsMutation, toast]
  )

  const handleConfigChange = useCallback((updates: Partial<PipelineConfig>) => {
    setPipelineConfig((prev) => ({ ...prev, ...updates }))
  }, [])

  const handleConfigSave = useCallback(
    async (config: PipelineConfig) => {
      try {
        await updateConfigMutation.mutateAsync(config)
      } catch (error: unknown) {
        const message = error instanceof Error ? error.message : 'Failed to save config'
        toast({ title: 'Error', description: message, variant: 'destructive' })
      }
    },
    [updateConfigMutation, toast]
  )

  const handlePipelineTypeChange = useCallback(
    async (value: string) => {
      if (
        selectedPipeline &&
        value !== pipelineType &&
        window.confirm('Changing the pipeline type will reset its configuration. Continue?')
      ) {
        setPipelineType(value)
        try {
          await updatePipelineMutation.mutateAsync({
            id: selectedPipeline.id,
            name: selectedPipeline.name,
            pipeline_type: value,
          })
        } catch (error: unknown) {
          const message = error instanceof Error ? error.message : 'Failed to update pipeline type'
          toast({ title: 'Error', description: message, variant: 'destructive' })
        }
      }
    },
    [selectedPipeline, pipelineType, updatePipelineMutation, toast]
  )

  const handleFileUpload = useCallback(
    async (event: React.ChangeEvent<HTMLInputElement>, type: 'model' | 'labels') => {
      const file = event.target.files?.[0]
      if (!file) return

      try {
        const formData = new FormData()
        formData.append('file', file)
        formData.append('type', type)

        await uploadFileMutation.mutateAsync(formData)
        toast({ title: 'Success', description: `${type === 'model' ? 'Model' : 'Labels'} uploaded` })
      } catch (error: unknown) {
        const message = error instanceof Error ? error.message : 'Failed to upload file'
        toast({ title: 'Error', description: message, variant: 'destructive' })
      }
      event.target.value = ''
    },
    [uploadFileMutation, toast]
  )

  const handleFileDelete = useCallback(
    async (type: 'model' | 'labels') => {
      if (!selectedPipeline) return
      if (!window.confirm(`Remove ${type} file from pipeline "${selectedPipeline.name}"?`)) return

      try {
        await deleteFileMutation.mutateAsync(type)
        toast({ title: 'Success', description: `${type === 'model' ? 'Model' : 'Labels'} removed` })
      } catch (error: unknown) {
        const message = error instanceof Error ? error.message : 'Failed to delete file'
        toast({ title: 'Error', description: message, variant: 'destructive' })
      }
    },
    [selectedPipeline, deleteFileMutation, toast]
  )

  // Pipeline modal handlers
  const openAddPipelineModal = useCallback(() => {
    setPipelineModalMode('add')
    setPipelineModalName('')
    setPipelineModalType(PIPELINE_TYPES.APRILTAG)
    setPipelineModalOpen(true)
  }, [])

  const openRenamePipelineModal = useCallback(() => {
    if (!selectedPipeline) return
    setPipelineModalMode('edit')
    setPipelineModalName(selectedPipeline.name)
    setPipelineModalType(selectedPipeline.pipeline_type)
    setPipelineModalOpen(true)
  }, [selectedPipeline])

  const submitPipelineModal = useCallback(async () => {
    if (!pipelineModalName.trim()) {
      toast({ title: 'Error', description: 'Pipeline name is required', variant: 'destructive' })
      return
    }

    if (!selectedCameraId) {
      toast({ title: 'Error', description: 'Select a camera first', variant: 'destructive' })
      return
    }

    try {
      if (pipelineModalMode === 'add') {
        await createPipelineMutation.mutateAsync({
          name: pipelineModalName,
          pipeline_type: pipelineModalType,
        })
        toast({ title: 'Success', description: 'Pipeline created' })
      } else if (selectedPipeline) {
        await updatePipelineMutation.mutateAsync({
          id: selectedPipeline.id,
          name: pipelineModalName,
          pipeline_type: pipelineModalType,
        })
        toast({ title: 'Success', description: 'Pipeline updated' })
      }
      setPipelineModalOpen(false)
    } catch (error: unknown) {
      const message = error instanceof Error ? error.message : 'Failed to save pipeline'
      toast({ title: 'Error', description: message, variant: 'destructive' })
    }
  }, [
    pipelineModalName,
    pipelineModalType,
    pipelineModalMode,
    selectedCameraId,
    selectedPipeline,
    createPipelineMutation,
    updatePipelineMutation,
    toast,
  ])

  // Delete modal handlers
  const openDeleteModal = useCallback(() => {
    if (!selectedPipeline) return
    setDeleteModalOpen(true)
  }, [selectedPipeline])

  const confirmDelete = useCallback(async () => {
    if (!selectedPipeline) return

    try {
      await deletePipelineMutation.mutateAsync(selectedPipeline.id)
      toast({ title: 'Success', description: 'Pipeline deleted' })
      setSelectedPipelineId('')
      setDeleteModalOpen(false)
    } catch (error: unknown) {
      const message = error instanceof Error ? error.message : 'Failed to delete pipeline'
      toast({ title: 'Error', description: message, variant: 'destructive' })
    }
  }, [selectedPipeline, deletePipelineMutation, toast])

  const isUncalibrated = selectedCamera && !selectedCamera.camera_matrix_json

  return (
    <div className="p-6 space-y-6">
      {/* Header */}
      <div className="space-y-2">
        <h1 className="text-3xl font-bold">Dashboard</h1>
        <div className="flex gap-6 text-sm text-muted-foreground">
          <span>
            Registered cameras: <strong>{cameras.length}</strong>
          </span>
          <span>
            Pipelines: <strong>{pipelines.length}</strong>
          </span>
          <span>
            Feed: <strong>{feedType === 'processed' ? 'Processed' : 'Default'}</strong>
          </span>
        </div>
      </div>

      {/* Calibration Warning */}
      {isUncalibrated && (
        <Alert variant="destructive" className="bg-red-600 text-white border-red-700 [&>svg]:text-white">
          <TriangleAlert className="h-4 w-4" />
          <AlertTitle>Camera Uncalibrated</AlertTitle>
          <AlertDescription>
            The selected camera has not been calibrated. Please go to the{' '}
            <Link to="/calibration" className="underline font-medium hover:text-white/80">
              calibration page
            </Link>{' '}
            to calibrate it for accurate results.
          </AlertDescription>
        </Alert>
      )}

      {/* Main Content */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Setup Panel */}
        <Card>
          <CardHeader>
            <CardTitle>Pipeline & Camera Setup</CardTitle>
            <CardDescription>Configure your camera source and detection pipeline</CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            <CameraSelector
              cameras={cameras}
              selectedCameraId={selectedCameraId}
              onCameraChange={handleCameraChange}
              isConnected={isCameraConnected}
            />

            <PipelineSelector
              pipelines={pipelines}
              selectedPipelineId={selectedPipelineId}
              pipelineType={pipelineType}
              disabled={!selectedCameraId}
              onPipelineChange={setSelectedPipelineId}
              onPipelineTypeChange={handlePipelineTypeChange}
              onAddPipeline={openAddPipelineModal}
              onEditPipeline={openRenamePipelineModal}
              onDeletePipeline={openDeleteModal}
            />

            <CameraControls
              controls={localControls}
              disabled={!selectedCameraId}
              onControlsChange={handleControlsChange}
              onSave={handleControlsSave}
            />
          </CardContent>
        </Card>

        {/* Live Feed */}
        <LiveFeed
          selectedCameraId={selectedCameraId}
          selectedPipelineId={selectedPipelineId}
          isCameraConnected={isCameraConnected}
          feedType={feedType}
          feedSrc={feedSrc}
          onFeedTypeChange={setFeedType}
        />
      </div>

      {/* Pipeline Settings */}
      <Card>
        <CardHeader>
          <div className="flex justify-between items-start">
            <div>
              <CardTitle>Pipeline Settings</CardTitle>
              <CardDescription>Adjust parameters for the selected pipeline. Changes apply live.</CardDescription>
            </div>
            <Badge variant={selectedPipeline ? 'default' : 'secondary'}>
              {selectedPipeline ? selectedPipeline.name : 'No pipeline selected'}
            </Badge>
          </div>
        </CardHeader>
        <CardContent>
          <PipelineConfigPanel
            pipelineType={pipelineType}
            config={pipelineConfig}
            results={results}
            labelOptions={labelOptions}
            onConfigChange={handleConfigChange}
            onSave={handleConfigSave}
            onFileUpload={handleFileUpload}
            onFileDelete={handleFileDelete}
          />
        </CardContent>
      </Card>

      {/* Modals */}
      <PipelineModal
        open={pipelineModalOpen}
        onOpenChange={setPipelineModalOpen}
        mode={pipelineModalMode}
        name={pipelineModalName}
        onNameChange={setPipelineModalName}
        type={pipelineModalType}
        onTypeChange={setPipelineModalType}
        saving={createPipelineMutation.isPending || updatePipelineMutation.isPending}
        onSubmit={submitPipelineModal}
      />

      <DeletePipelineModal
        open={deleteModalOpen}
        onOpenChange={setDeleteModalOpen}
        pipelineName={selectedPipeline?.name ?? ''}
        saving={deletePipelineMutation.isPending}
        onConfirm={confirmDelete}
      />
    </div>
  )
}
