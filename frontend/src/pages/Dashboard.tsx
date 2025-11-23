import { useState, useEffect, useRef } from 'react'
import { useAppStore } from '@/store/useAppStore'
import { api } from '@/lib/api'
import { usePolling } from '@/hooks/usePolling'
import type { Camera, Pipeline, PipelineConfig } from '@/types'
import { MJPEGStream } from '@/components/shared/MJPEGStream'
import { Button } from '@/components/ui/button'
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '@/components/ui/card'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle, DialogFooter } from '@/components/ui/dialog'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { Badge } from '@/components/ui/badge'
import { Switch } from '@/components/ui/switch'
import { Plus, Edit2, Trash2 } from 'lucide-react'
import { useToast } from '@/hooks/use-toast'

// Default configurations
const APRILTAG_DEFAULTS: PipelineConfig = {
  family: 'tag36h11',
  tag_size_m: 0.165,
  threads: 4,
  auto_threads: true,
  decimate: 1,
  blur: 0,
  refine_edges: true,
  decision_margin: 35,
  pose_iterations: 40,
  decode_sharpening: 0.25,
  min_weight: 0,
  edge_threshold: 0,
  multi_tag_enabled: false,
  ransac_reproj_threshold: 1.2,
  ransac_confidence: 0.999,
  min_inliers: 12,
  use_prev_guess: true,
  publish_field_pose: true,
  output_quaternion: true,
  multi_tag_error_threshold: 6.0,
}

const COLOURED_DEFAULTS: PipelineConfig = {
  hue_min: 0,
  hue_max: 179,
  saturation_min: 0,
  saturation_max: 255,
  value_min: 0,
  value_max: 255,
  min_area: 100,
  max_area: 10000,
  min_aspect_ratio: 0.5,
  max_aspect_ratio: 2.0,
  min_fullness: 0.4,
}

const ML_DEFAULTS: PipelineConfig = {
  model_type: 'yolo',
  confidence_threshold: 0.5,
  nms_iou_threshold: 0.45,
  target_classes: [],
  onnx_provider: 'CPUExecutionProvider',
  accelerator: 'none',
  max_detections: 100,
  img_size: 640,
  model_filename: '',
  labels_filename: '',
  tflite_delegate: null,
}

interface CameraControls {
  orientation: number
  exposure_mode: 'auto' | 'manual'
  exposure_value: number
  gain_mode: 'auto' | 'manual'
  gain_value: number
}

interface PipelineResults {
  apriltag: any[]
  ml: any[]
  multiTag: any | null
}

export default function Dashboard() {
  const { toast } = useToast()
  const cameras = useAppStore((state) => state.cameras)
  const setCameras = useAppStore((state) => state.setCameras)

  const [selectedCameraId, setSelectedCameraId] = useState<string>('')
  const [isCameraConnected, setIsCameraConnected] = useState(false)
  const [pipelines, setPipelines] = useState<Pipeline[]>([])
  const [selectedPipelineId, setSelectedPipelineId] = useState<string>('')
  const [pipelineType, setPipelineType] = useState<string>('')
  const [feedType, setFeedType] = useState<'default' | 'processed'>('default')
  const [feedSrc, setFeedSrc] = useState('')

  const [controls, setControls] = useState<CameraControls>({
    orientation: 0,
    exposure_mode: 'auto',
    exposure_value: 500,
    gain_mode: 'auto',
    gain_value: 50,
  })

  const [pipelineConfig, setPipelineConfig] = useState<PipelineConfig>({})
  const [results, setResults] = useState<PipelineResults>({
    apriltag: [],
    ml: [],
    multiTag: null,
  })

  // Modal states
  const [pipelineModalOpen, setPipelineModalOpen] = useState(false)
  const [pipelineModalMode, setPipelineModalMode] = useState<'add' | 'edit'>('add')
  const [pipelineModalName, setPipelineModalName] = useState('')
  const [pipelineModalType, setPipelineModalType] = useState('AprilTag')
  const [pipelineModalSaving, setPipelineModalSaving] = useState(false)

  const [deleteModalOpen, setDeleteModalOpen] = useState(false)
  const [deleteModalName, setDeleteModalName] = useState('')
  const [deleteModalSaving, setDeleteModalSaving] = useState(false)

  const [labelOptions, setLabelOptions] = useState<string[]>([])
  const [mlAvailability, setMlAvailability] = useState<any>(null)

  // Debounce timers
  const controlsTimerRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined)
  const configTimerRef = useRef<ReturnType<typeof setTimeout> | undefined>(undefined)

  const selectedPipeline = pipelines.find((p) => p.id.toString() === selectedPipelineId)

  // Load cameras on mount
  useEffect(() => {
    loadCameras()
  }, [])

  const loadCameras = async () => {
    try {
      const data = await api.get<Camera[]>('/api/cameras')
      setCameras(data)
    } catch (error) {
      toast({ title: 'Error', description: 'Failed to load cameras', variant: 'destructive' })
    }
  }

  // Handle camera selection change
  useEffect(() => {
    if (!selectedCameraId) {
      setPipelines([])
      setSelectedPipelineId('')
      setPipelineType('')
      setFeedSrc('')
      setIsCameraConnected(false)
      return
    }
    loadCameraData()
  }, [selectedCameraId])

  const loadCameraData = async () => {
    await Promise.all([loadPipelines(), loadControls()])
    updateFeedSource()
  }

  const loadPipelines = async () => {
    if (!selectedCameraId) return
    try {
      const data = await api.get<Pipeline[]>(`/api/cameras/${selectedCameraId}/pipelines`)
      setPipelines(data)
    } catch (error) {
      toast({ title: 'Error', description: 'Failed to load pipelines', variant: 'destructive' })
      setPipelines([])
    }
  }

  const loadControls = async () => {
    if (!selectedCameraId) return
    try {
      const data = await api.get<CameraControls>(`/api/cameras/controls/${selectedCameraId}`)
      setControls({
        orientation: data.orientation ?? 0,
        exposure_mode: data.exposure_mode || 'auto',
        exposure_value: data.exposure_value ?? 500,
        gain_mode: data.gain_mode || 'auto',
        gain_value: data.gain_value ?? 50,
      })
    } catch (error) {
      console.error('Failed to load controls:', error)
    }
  }

  // Handle pipeline selection change
  useEffect(() => {
    if (!selectedPipelineId) {
      setPipelineType('')
      setPipelineConfig({})
      setResults({ apriltag: [], ml: [], multiTag: null })
      updateFeedSource()
      return
    }
    loadPipelineData()
  }, [selectedPipelineId])

  const loadPipelineData = async () => {
    if (!selectedPipeline) return

    setPipelineType(selectedPipeline.pipeline_type)

    // Load config
    try {
      const config = JSON.parse(selectedPipeline.config || '{}')
      let defaults = {}
      if (selectedPipeline.pipeline_type === 'AprilTag') {
        defaults = APRILTAG_DEFAULTS
      } else if (selectedPipeline.pipeline_type === 'Coloured Shape') {
        defaults = COLOURED_DEFAULTS
      } else if (selectedPipeline.pipeline_type === 'Object Detection (ML)') {
        defaults = ML_DEFAULTS
        // Load ML-specific data
        await loadMlAvailability()
        await loadLabels()
      }
      setPipelineConfig({ ...defaults, ...config })
    } catch (error) {
      console.error('Failed to parse pipeline config:', error)
      setPipelineConfig({})
    }

    updateFeedSource()
  }

  const loadMlAvailability = async () => {
    if (mlAvailability) return
    try {
      const data = await api.get('/api/pipelines/ml/availability')
      setMlAvailability(data)
    } catch (error) {
      console.error('Failed to load ML availability:', error)
      setMlAvailability({})
    }
  }

  const loadLabels = async () => {
    if (!selectedPipelineId) return
    try {
      const data = await api.get<{ labels: string[] }>(`/api/pipelines/${selectedPipelineId}/labels`)
      setLabelOptions(data.labels || [])
    } catch (error) {
      console.error('Failed to load labels:', error)
      setLabelOptions([])
    }
  }

  const updateFeedSource = async () => {
    if (!selectedCameraId) {
      setFeedSrc('')
      setIsCameraConnected(false)
      return
    }

    // Check camera connection status
    try {
      const status = await api.get<{ connected: boolean }>(`/api/cameras/status/${selectedCameraId}`)
      setIsCameraConnected(status.connected)
    } catch (error) {
      setIsCameraConnected(false)
      return
    }

    if (!isCameraConnected) {
      setFeedSrc('')
      return
    }

    const cacheBuster = Date.now()
    if (feedType === 'processed' && selectedPipelineId) {
      setFeedSrc(`/api/processed_video_feed/${selectedPipelineId}?t=${cacheBuster}`)
    } else {
      setFeedSrc(`/api/video_feed/${selectedCameraId}?t=${cacheBuster}`)
    }
  }

  useEffect(() => {
    updateFeedSource()
  }, [feedType, isCameraConnected])

  // Save controls with debounce
  const queueControlsSave = (updates: Partial<CameraControls>) => {
    setControls((prev) => ({ ...prev, ...updates }))

    if (controlsTimerRef.current) {
      clearTimeout(controlsTimerRef.current)
    }

    controlsTimerRef.current = setTimeout(() => {
      saveControls({ ...controls, ...updates })
    }, 400)
  }

  const saveControls = async (updatedControls: CameraControls) => {
    if (!selectedCameraId) return
    try {
      await api.post(`/api/cameras/update_controls/${selectedCameraId}`, updatedControls)
      updateFeedSource()
    } catch (error: any) {
      toast({ title: 'Error', description: error.message || 'Failed to save controls', variant: 'destructive' })
    }
  }

  // Save pipeline config with debounce
  const queueConfigSave = (updates: Partial<PipelineConfig>) => {
    setPipelineConfig((prev) => ({ ...prev, ...updates }))

    if (configTimerRef.current) {
      clearTimeout(configTimerRef.current)
    }

    configTimerRef.current = setTimeout(() => {
      savePipelineConfig({ ...pipelineConfig, ...updates })
    }, 600)
  }

  const savePipelineConfig = async (config: PipelineConfig) => {
    if (!selectedPipelineId) return
    try {
      await api.put(`/api/pipelines/${selectedPipelineId}/config`, config)
    } catch (error: any) {
      toast({ title: 'Error', description: error.message || 'Failed to save pipeline config', variant: 'destructive' })
    }
  }

  // Pipeline modal handlers
  const openAddPipelineModal = () => {
    setPipelineModalMode('add')
    setPipelineModalName('')
    setPipelineModalType('AprilTag')
    setPipelineModalOpen(true)
  }

  const openRenamePipelineModal = () => {
    if (!selectedPipeline) return
    setPipelineModalMode('edit')
    setPipelineModalName(selectedPipeline.name)
    setPipelineModalType(selectedPipeline.pipeline_type)
    setPipelineModalOpen(true)
  }

  const submitPipelineModal = async () => {
    if (!pipelineModalName.trim()) {
      toast({ title: 'Error', description: 'Pipeline name is required', variant: 'destructive' })
      return
    }

    if (!selectedCameraId) {
      toast({ title: 'Error', description: 'Select a camera first', variant: 'destructive' })
      return
    }

    setPipelineModalSaving(true)

    try {
      if (pipelineModalMode === 'add') {
        await api.post(`/api/cameras/${selectedCameraId}/pipelines`, {
          name: pipelineModalName,
          pipeline_type: pipelineModalType,
        })
        toast({ title: 'Success', description: 'Pipeline created' })
      } else if (selectedPipeline) {
        await api.put(`/api/pipelines/${selectedPipeline.id}`, {
          name: pipelineModalName,
          pipeline_type: pipelineModalType,
        })
        toast({ title: 'Success', description: 'Pipeline updated' })
      }
      await loadPipelines()
      setPipelineModalOpen(false)
    } catch (error: any) {
      toast({ title: 'Error', description: error.message || 'Failed to save pipeline', variant: 'destructive' })
    } finally {
      setPipelineModalSaving(false)
    }
  }

  // Delete modal handlers
  const openDeleteModal = () => {
    if (!selectedPipeline) return
    setDeleteModalName(selectedPipeline.name)
    setDeleteModalOpen(true)
  }

  const confirmDelete = async () => {
    if (!selectedPipeline) return
    setDeleteModalSaving(true)

    try {
      await api.delete(`/api/pipelines/${selectedPipeline.id}`)
      toast({ title: 'Success', description: 'Pipeline deleted' })
      setSelectedPipelineId('')
      await loadPipelines()
      setDeleteModalOpen(false)
    } catch (error: any) {
      toast({ title: 'Error', description: error.message || 'Failed to delete pipeline', variant: 'destructive' })
    } finally {
      setDeleteModalSaving(false)
    }
  }

  // Poll for results
  const fetchResults = async () => {
    if (!selectedCameraId || !selectedPipelineId) return
    try {
      const data = await api.get<any>(`/api/cameras/results/${selectedCameraId}`)
      const pipelineResults = data?.[selectedPipelineId]
      if (!pipelineResults) {
        setResults({ apriltag: [], ml: [], multiTag: null })
        return
      }

      if (pipelineType === 'AprilTag') {
        setResults({
          apriltag: pipelineResults.detections || [],
          ml: [],
          multiTag: pipelineResults.multi_tag_pose || null,
        })
      } else if (pipelineType === 'Object Detection (ML)') {
        setResults({
          apriltag: [],
          ml: pipelineResults.detections || [],
          multiTag: null,
        })
      }
    } catch (error) {
      // Silent fail
    }
  }

  usePolling(fetchResults, 1000, !!selectedCameraId && !!selectedPipelineId)

  const handleFileUpload = async (event: React.ChangeEvent<HTMLInputElement>, type: 'model' | 'labels') => {
    if (!selectedPipeline) return
    const file = event.target.files?.[0]
    if (!file) return

    try {
      const formData = new FormData()
      formData.append('file', file)
      formData.append('type', type)

      await api.uploadFile(`/api/pipelines/${selectedPipeline.id}/files`, formData)
      toast({ title: 'Success', description: `${type === 'model' ? 'Model' : 'Labels'} uploaded` })
      await loadPipelineData()
    } catch (error: any) {
      toast({ title: 'Error', description: error.message || 'Failed to upload file', variant: 'destructive' })
    }
    event.target.value = ''
  }

  const handleFileDelete = async (type: 'model' | 'labels') => {
    if (!selectedPipeline) return
    if (!window.confirm(`Remove ${type} file from pipeline "${selectedPipeline.name}"?`)) return

    try {
      await api.post(`/api/pipelines/${selectedPipeline.id}/files/delete`, { type })
      toast({ title: 'Success', description: `${type === 'model' ? 'Model' : 'Labels'} removed` })
      await loadPipelineData()
    } catch (error: any) {
      toast({ title: 'Error', description: error.message || 'Failed to delete file', variant: 'destructive' })
    }
  }

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
            Feed selector: <strong>{feedType === 'processed' ? 'Processed' : 'Default'}</strong>
          </span>
        </div>
      </div>

      {/* Main Content: Setup and Live Feed */}
      <div className="grid grid-cols-1 lg:grid-cols-3 gap-6">
        {/* Pipeline & Camera Setup (1/3 width) */}
        <Card>
          <CardHeader>
            <CardTitle>Pipeline & Camera Setup</CardTitle>
            <CardDescription>Configure your camera source and detection pipeline</CardDescription>
          </CardHeader>
          <CardContent className="space-y-4">
            {/* Camera Select */}
            <div className="space-y-2">
              <Label htmlFor="camera-select">Camera</Label>
              <Select value={selectedCameraId} onValueChange={setSelectedCameraId}>
                <SelectTrigger id="camera-select">
                  <SelectValue placeholder={cameras.length ? 'Select a camera…' : 'No cameras configured'} />
                </SelectTrigger>
                <SelectContent>
                  {cameras.map((camera) => (
                    <SelectItem key={camera.id} value={camera.id.toString()}>
                      {camera.name}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            {/* Pipeline Select */}
            <div className="space-y-2">
              <Label>Pipeline</Label>
              <div className="flex gap-2">
                <Select
                  value={selectedPipelineId}
                  onValueChange={setSelectedPipelineId}
                  disabled={!pipelines.length}
                >
                  <SelectTrigger className="flex-1">
                    <SelectValue placeholder="Select pipeline…" />
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
                  onClick={openAddPipelineModal}
                  disabled={!selectedCameraId}
                  title="Add pipeline"
                >
                  <Plus className="h-4 w-4" />
                </Button>
                <Button
                  size="icon"
                  variant="outline"
                  onClick={openRenamePipelineModal}
                  disabled={!selectedPipelineId}
                  title="Rename pipeline"
                >
                  <Edit2 className="h-4 w-4" />
                </Button>
                <Button
                  size="icon"
                  variant="outline"
                  onClick={openDeleteModal}
                  disabled={!selectedPipelineId}
                  title="Delete pipeline"
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
                onValueChange={(value) => {
                  if (
                    selectedPipeline &&
                    value !== pipelineType &&
                    window.confirm('Changing the pipeline type will reset its configuration. Continue?')
                  ) {
                    setPipelineType(value)
                    // Update via API
                    api
                      .put(`/api/pipelines/${selectedPipeline.id}`, {
                        name: selectedPipeline.name,
                        pipeline_type: value,
                      })
                      .then(() => {
                        loadPipelines()
                        loadPipelineData()
                      })
                      .catch((error: any) => {
                        toast({ title: 'Error', description: error.message, variant: 'destructive' })
                      })
                  }
                }}
                disabled={!selectedPipelineId}
              >
                <SelectTrigger id="pipeline-type">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="AprilTag">AprilTag</SelectItem>
                  <SelectItem value="Coloured Shape">Coloured Shape</SelectItem>
                  <SelectItem value="Object Detection (ML)">Object Detection (ML)</SelectItem>
                </SelectContent>
              </Select>
            </div>

            {/* Camera Controls */}
            <div className="space-y-2">
              <Label htmlFor="orientation">Orientation</Label>
              <Select
                value={controls.orientation.toString()}
                onValueChange={(value) => queueControlsSave({ orientation: parseInt(value) })}
                disabled={!selectedCameraId}
              >
                <SelectTrigger id="orientation">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="0">Normal (0°)</SelectItem>
                  <SelectItem value="90">Rotate 90°</SelectItem>
                  <SelectItem value="180">Rotate 180°</SelectItem>
                  <SelectItem value="270">Rotate 270°</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="space-y-2">
              <Label>Exposure Mode</Label>
              <Select
                value={controls.exposure_mode}
                onValueChange={(value: 'auto' | 'manual') => queueControlsSave({ exposure_mode: value })}
                disabled={!selectedCameraId}
              >
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="auto">Auto</SelectItem>
                  <SelectItem value="manual">Manual</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="space-y-2">
              <Label>Exposure Value</Label>
              <div className="flex gap-2">
                <input
                  type="range"
                  min="0"
                  max="1000"
                  value={controls.exposure_value}
                  onChange={(e) => queueControlsSave({ exposure_value: parseInt(e.target.value) })}
                  disabled={controls.exposure_mode !== 'manual'}
                  className="flex-1"
                />
                <Input
                  type="number"
                  min="0"
                  max="1000"
                  value={controls.exposure_value}
                  onChange={(e) => queueControlsSave({ exposure_value: parseInt(e.target.value) })}
                  disabled={controls.exposure_mode !== 'manual'}
                  className="w-20"
                />
              </div>
            </div>

            <div className="space-y-2">
              <Label>Gain Mode</Label>
              <Select
                value={controls.gain_mode}
                onValueChange={(value: 'auto' | 'manual') => queueControlsSave({ gain_mode: value })}
                disabled={!selectedCameraId}
              >
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="auto">Auto</SelectItem>
                  <SelectItem value="manual">Manual</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="space-y-2">
              <Label>Gain Value</Label>
              <div className="flex gap-2">
                <input
                  type="range"
                  min="0"
                  max="100"
                  value={controls.gain_value}
                  onChange={(e) => queueControlsSave({ gain_value: parseInt(e.target.value) })}
                  disabled={controls.gain_mode !== 'manual'}
                  className="flex-1"
                />
                <Input
                  type="number"
                  min="0"
                  max="100"
                  value={controls.gain_value}
                  onChange={(e) => queueControlsSave({ gain_value: parseInt(e.target.value) })}
                  disabled={controls.gain_mode !== 'manual'}
                  className="w-20"
                />
              </div>
            </div>
          </CardContent>
        </Card>

        {/* Live Feed (2/3 width) */}
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
                    onChange={() => setFeedType('default')}
                    disabled={!selectedCameraId}
                  />
                  <span>Default feed</span>
                </label>
                <label className="flex items-center gap-2 cursor-pointer">
                  <input
                    type="radio"
                    name="feedType"
                    value="processed"
                    checked={feedType === 'processed'}
                    onChange={() => setFeedType('processed')}
                    disabled={!selectedPipelineId}
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
              <div className="flex items-center justify-center h-96 text-muted-foreground">Loading feed…</div>
            )}
          </CardContent>
        </Card>
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
          {pipelineType === 'AprilTag' && (
            <AprilTagForm config={pipelineConfig} onChange={queueConfigSave} results={results.apriltag} multiTag={results.multiTag} />
          )}
          {pipelineType === 'Coloured Shape' && <ColouredShapeForm config={pipelineConfig} onChange={queueConfigSave} />}
          {pipelineType === 'Object Detection (ML)' && (
            <MLForm
              config={pipelineConfig}
              onChange={queueConfigSave}
              results={results.ml}
              labelOptions={labelOptions}
              mlAvailability={mlAvailability}
              onFileUpload={handleFileUpload}
              onFileDelete={handleFileDelete}
            />
          )}
          {!pipelineType && (
            <div className="text-center text-muted-foreground py-12">Select a pipeline to configure its settings</div>
          )}
        </CardContent>
      </Card>

      {/* Pipeline Modal */}
      <Dialog open={pipelineModalOpen} onOpenChange={setPipelineModalOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>{pipelineModalMode === 'add' ? 'Add Pipeline' : 'Edit Pipeline'}</DialogTitle>
            <DialogDescription>
              {pipelineModalMode === 'add'
                ? 'Define a fresh pipeline for the selected camera.'
                : 'Update the current pipeline.'}
            </DialogDescription>
          </DialogHeader>
          <div className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="pipeline-name">Name</Label>
              <Input
                id="pipeline-name"
                value={pipelineModalName}
                onChange={(e) => setPipelineModalName(e.target.value)}
                placeholder="Enter pipeline name"
              />
            </div>
            <div className="space-y-2">
              <Label htmlFor="pipeline-modal-type">Type</Label>
              <Select value={pipelineModalType} onValueChange={setPipelineModalType}>
                <SelectTrigger id="pipeline-modal-type">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="AprilTag">AprilTag</SelectItem>
                  <SelectItem value="Coloured Shape">Coloured Shape</SelectItem>
                  <SelectItem value="Object Detection (ML)">Object Detection (ML)</SelectItem>
                </SelectContent>
              </Select>
              {pipelineModalMode === 'edit' && (
                <p className="text-sm text-muted-foreground">Changing the pipeline type will reset its configuration.</p>
              )}
            </div>
          </div>
          <DialogFooter>
            <Button variant="outline" onClick={() => setPipelineModalOpen(false)} disabled={pipelineModalSaving}>
              Cancel
            </Button>
            <Button onClick={submitPipelineModal} disabled={pipelineModalSaving}>
              {pipelineModalSaving ? 'Saving…' : 'Save'}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* Delete Modal */}
      <Dialog open={deleteModalOpen} onOpenChange={setDeleteModalOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Delete Pipeline</DialogTitle>
            <DialogDescription>Confirm removal to free the slot for another configuration.</DialogDescription>
          </DialogHeader>
          <p>
            Are you sure you want to remove <strong>{deleteModalName}</strong>? This cannot be undone.
          </p>
          <DialogFooter>
            <Button variant="outline" onClick={() => setDeleteModalOpen(false)} disabled={deleteModalSaving}>
              Cancel
            </Button>
            <Button variant="destructive" onClick={confirmDelete} disabled={deleteModalSaving}>
              {deleteModalSaving ? 'Deleting…' : 'Delete'}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  )
}

// AprilTag Configuration Form Component
function AprilTagForm({
  config,
  onChange,
  results,
  multiTag,
}: {
  config: PipelineConfig
  onChange: (updates: Partial<PipelineConfig>) => void
  results: any[]
  multiTag: any | null
}) {
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
                <SelectItem value="tag36h11">36h11</SelectItem>
                <SelectItem value="tag16h5">16h5</SelectItem>
                <SelectItem value="tag25h9">25h9</SelectItem>
                <SelectItem value="tagCircle21h7">Circle21h7</SelectItem>
              </SelectContent>
            </Select>
          </div>

          <div className="space-y-2">
            <Label>Tag Size (m)</Label>
            <Input
              type="number"
              step="0.001"
              value={config.tag_size_m as number}
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
                value={config.threads as number}
                onChange={(e) => onChange({ threads: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="1"
                max="8"
                value={config.threads as number}
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
                value={config.decimate as number}
                onChange={(e) => onChange({ decimate: parseFloat(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="1"
                max="8"
                step="0.1"
                value={config.decimate as number}
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
                value={config.blur as number}
                onChange={(e) => onChange({ blur: parseFloat(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="5"
                step="0.1"
                value={config.blur as number}
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
                value={config.decision_margin as number}
                onChange={(e) => onChange({ decision_margin: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="250"
                value={config.decision_margin as number}
                onChange={(e) => onChange({ decision_margin: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="flex items-center gap-2">
            <Switch
              checked={config.refine_edges as boolean}
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
              value={config.decode_sharpening as number}
              onChange={(e) => onChange({ decode_sharpening: parseFloat(e.target.value) })}
            />
          </div>

          <div className="p-4 bg-surface rounded-lg space-y-3">
            <h4 className="font-semibold text-sm">Multi-Tag Pose</h4>
            <p className="text-xs text-muted-foreground">Enable when a WPILib field layout is available</p>
            <div className="flex items-center gap-2">
              <Switch
                checked={config.multi_tag_enabled as boolean}
                onCheckedChange={(checked) => onChange({ multi_tag_enabled: checked })}
              />
              <Label className="text-sm">Enable multi-tag solver</Label>
            </div>
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
                  <TableHead>Yaw (°)</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {results.length === 0 ? (
                  <TableRow>
                    <TableCell colSpan={5} className="text-center text-muted-foreground">
                      No targets detected
                    </TableCell>
                  </TableRow>
                ) : (
                  results.map((target: any, index: number) => (
                    <TableRow key={target.id ?? index}>
                      <TableCell>{target.id ?? index}</TableCell>
                      <TableCell>{target.camera_to_tag?.translation?.x?.toFixed(3) ?? 'N/A'}</TableCell>
                      <TableCell>{target.camera_to_tag?.translation?.y?.toFixed(3) ?? 'N/A'}</TableCell>
                      <TableCell>{target.camera_to_tag?.translation?.z?.toFixed(3) ?? 'N/A'}</TableCell>
                      <TableCell>{target.camera_to_tag?.rotation?.euler_deg?.yaw?.toFixed(2) ?? 'N/A'}</TableCell>
                    </TableRow>
                  ))
                )}
              </TableBody>
            </Table>
          </div>

          {multiTag && (
            <div className="p-4 bg-surface rounded-lg space-y-2">
              <h4 className="font-semibold text-sm">Multi-Tag Pose</h4>
              <div className="grid grid-cols-2 gap-2 text-sm">
                <div>
                  <span className="text-muted-foreground">Tags used:</span> {multiTag.tag_ids_used?.length ?? 0}
                </div>
                <div>
                  <span className="text-muted-foreground">Inliers:</span> {multiTag.num_inliers ?? 0}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </div>
  )
}

// Coloured Shape Configuration Form Component
function ColouredShapeForm({ config, onChange }: { config: PipelineConfig; onChange: (updates: Partial<PipelineConfig>) => void }) {
  return (
    <div className="space-y-6">
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <div className="space-y-4">
          <h3 className="text-lg font-semibold">HSV Thresholds</h3>

          <div className="space-y-2">
            <Label>Hue Min (0-179)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="0"
                max="179"
                value={config.hue_min as number}
                onChange={(e) => onChange({ hue_min: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="179"
                value={config.hue_min as number}
                onChange={(e) => onChange({ hue_min: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Hue Max (0-179)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="0"
                max="179"
                value={config.hue_max as number}
                onChange={(e) => onChange({ hue_max: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="179"
                value={config.hue_max as number}
                onChange={(e) => onChange({ hue_max: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Saturation Min (0-255)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="0"
                max="255"
                value={config.saturation_min as number}
                onChange={(e) => onChange({ saturation_min: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="255"
                value={config.saturation_min as number}
                onChange={(e) => onChange({ saturation_min: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Saturation Max (0-255)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="0"
                max="255"
                value={config.saturation_max as number}
                onChange={(e) => onChange({ saturation_max: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="255"
                value={config.saturation_max as number}
                onChange={(e) => onChange({ saturation_max: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Value Min (0-255)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="0"
                max="255"
                value={config.value_min as number}
                onChange={(e) => onChange({ value_min: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="255"
                value={config.value_min as number}
                onChange={(e) => onChange({ value_min: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>

          <div className="space-y-2">
            <Label>Value Max (0-255)</Label>
            <div className="flex gap-2">
              <input
                type="range"
                min="0"
                max="255"
                value={config.value_max as number}
                onChange={(e) => onChange({ value_max: parseInt(e.target.value) })}
                className="flex-1"
              />
              <Input
                type="number"
                min="0"
                max="255"
                value={config.value_max as number}
                onChange={(e) => onChange({ value_max: parseInt(e.target.value) })}
                className="w-20"
              />
            </div>
          </div>
        </div>

        <div className="space-y-4">
          <h3 className="text-lg font-semibold">Contour Filtering</h3>

          <div className="space-y-2">
            <Label>Min Area</Label>
            <Input
              type="number"
              min="0"
              max="10000"
              value={config.min_area as number}
              onChange={(e) => onChange({ min_area: parseInt(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>Max Area</Label>
            <Input
              type="number"
              min="0"
              max="100000"
              value={config.max_area as number}
              onChange={(e) => onChange({ max_area: parseInt(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>Min Aspect Ratio</Label>
            <Input
              type="number"
              min="0"
              max="20"
              step="0.1"
              value={config.min_aspect_ratio as number}
              onChange={(e) => onChange({ min_aspect_ratio: parseFloat(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>Max Aspect Ratio</Label>
            <Input
              type="number"
              min="0"
              max="20"
              step="0.1"
              value={config.max_aspect_ratio as number}
              onChange={(e) => onChange({ max_aspect_ratio: parseFloat(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>Min Fullness</Label>
            <Input
              type="number"
              min="0"
              max="1"
              step="0.01"
              value={config.min_fullness as number}
              onChange={(e) => onChange({ min_fullness: parseFloat(e.target.value) })}
            />
          </div>

          <p className="text-sm text-muted-foreground">
            Processed feed overlay highlights detected contours in real time.
          </p>
        </div>
      </div>
    </div>
  )
}

// ML Configuration Form Component
function MLForm({
  config,
  onChange,
  results,
  labelOptions,
  onFileUpload,
  onFileDelete,
}: {
  config: PipelineConfig
  onChange: (updates: Partial<PipelineConfig>) => void
  results: any[]
  labelOptions: string[]
  mlAvailability: any
  onFileUpload: (event: React.ChangeEvent<HTMLInputElement>, type: 'model' | 'labels') => void
  onFileDelete: (type: 'model' | 'labels') => void
}) {
  return (
    <div className="space-y-6">
      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        <div className="space-y-4">
          <h3 className="text-lg font-semibold">Object Detection (ML)</h3>

          <div className="space-y-2">
            <Label>ML Model File</Label>
            <div className="flex gap-2">
              <Input type="file" onChange={(e) => onFileUpload(e, 'model')} />
              {config.model_filename && (
                <Button variant="destructive" onClick={() => onFileDelete('model')}>
                  Remove
                </Button>
              )}
            </div>
            {config.model_filename && <p className="text-sm text-muted-foreground">Current: {config.model_filename as string}</p>}
          </div>

          <div className="space-y-2">
            <Label>Labels File</Label>
            <div className="flex gap-2">
              <Input type="file" onChange={(e) => onFileUpload(e, 'labels')} />
              {config.labels_filename && (
                <Button variant="destructive" onClick={() => onFileDelete('labels')}>
                  Remove
                </Button>
              )}
            </div>
            {config.labels_filename && <p className="text-sm text-muted-foreground">Current: {config.labels_filename as string}</p>}
          </div>

          <div className="space-y-2">
            <Label>Confidence Threshold</Label>
            <Input
              type="number"
              min="0"
              max="1"
              step="0.01"
              value={config.confidence_threshold as number}
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
              value={config.nms_iou_threshold as number}
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
              value={config.img_size as number}
              onChange={(e) => onChange({ img_size: parseInt(e.target.value) })}
            />
          </div>

          <div className="space-y-2">
            <Label>Target Classes</Label>
            <select
              multiple
              size={6}
              className="w-full border rounded-md p-2"
              value={config.target_classes as string[]}
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
            {labelOptions.length === 0 && <p className="text-sm text-muted-foreground">Upload labels file to enable filtering</p>}
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
                  results.map((det: any, index: number) => (
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
}
