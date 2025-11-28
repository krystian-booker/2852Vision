import { useState, useEffect } from 'react'
import { Camera as CameraIcon, Plus, Edit2, Trash2, RefreshCw, ChevronDown, ChevronUp } from 'lucide-react'
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog'
import { StatusBadge } from '@/components/shared'
import { toast } from '@/hooks/use-toast'
import { api } from '@/lib/api'
import { useAppStore } from '@/store/useAppStore'
import type { Camera, CameraStatus, DeviceInfo } from '@/types'

interface CameraProfile {
  width: number
  height: number
  fps: number
}

export default function Cameras() {
  const cameras = useAppStore((state) => state.cameras)
  const setCameras = useAppStore((state) => state.setCameras)
  const updateCamera = useAppStore((state) => state.updateCamera)
  const deleteCamera = useAppStore((state) => state.deleteCamera)

  const [cameraStatuses, setCameraStatuses] = useState<Record<number, CameraStatus>>({})
  const [addModalOpen, setAddModalOpen] = useState(false)
  const [editModalOpen, setEditModalOpen] = useState(false)
  const [deleteModalOpen, setDeleteModalOpen] = useState(false)
  const [selectedCamera, setSelectedCamera] = useState<Camera | null>(null)

  // Add camera form state
  const [newCameraName, setNewCameraName] = useState('')
  const [newCameraType, setNewCameraType] = useState<string>('')
  const [availableDevices, setAvailableDevices] = useState<DeviceInfo[]>([])
  const [selectedDevice, setSelectedDevice] = useState<string>('')
  const [isDiscovering, setIsDiscovering] = useState(false)
  const [isLoading, setIsLoading] = useState(false)

  // Camera profile state
  const [availableProfiles, setAvailableProfiles] = useState<CameraProfile[]>([])
  const [selectedProfile, setSelectedProfile] = useState<string>('')
  const [isLoadingProfiles, setIsLoadingProfiles] = useState(false)

  // Edit camera state
  const [editCameraName, setEditCameraName] = useState('')
  const [editAvailableProfiles, setEditAvailableProfiles] = useState<CameraProfile[]>([])
  const [editSelectedProfile, setEditSelectedProfile] = useState<string>('')
  const [isLoadingEditProfiles, setIsLoadingEditProfiles] = useState(false)
  const [advancedOpen, setAdvancedOpen] = useState(false)

  // Advanced params state
  const [matrixParams, setMatrixParams] = useState({ fx: 0, fy: 0, cx: 0, cy: 0 })
  const [distParams, setDistParams] = useState({ k1: 0, k2: 0, p1: 0, p2: 0, k3: 0 })

  // Fetch cameras on mount
  useEffect(() => {
    fetchCameras()
  }, [])

  // Poll camera statuses
  useEffect(() => {
    if (cameras.length === 0) return

    const fetchStatuses = async () => {
      const statusPromises = cameras.map(async (cam) => {
        try {
          const status = await api.get<CameraStatus>(`/api/cameras/status/${cam.id}`)
          return { id: cam.id, status }
        } catch {
          return { id: cam.id, status: { connected: false, error: 'Failed to fetch status' } }
        }
      })

      const results = await Promise.all(statusPromises)
      const statusMap: Record<number, CameraStatus> = {}
      results.forEach(({ id, status }) => {
        statusMap[id] = status
      })
      setCameraStatuses(statusMap)
    }

    fetchStatuses()
    const interval = setInterval(fetchStatuses, 3000)
    return () => clearInterval(interval)
  }, [cameras])

  const fetchCameras = async () => {
    try {
      const data = await api.get<Camera[]>('/api/cameras')
      setCameras(data)
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to fetch cameras',
      })
    }
  }

  const handleDiscoverDevices = async () => {
    if (!newCameraType) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Please select a camera type first',
      })
      return
    }

    // Prevent rapid repeated clicks (debounce)
    if (isDiscovering) {
      return
    }

    setIsDiscovering(true)
    try {
      // Collect existing camera identifiers to filter them out
      const existingIdentifiers = cameras.map((cam) => cam.identifier).join(',')

      const devices = await api.get<DeviceInfo[]>('/api/cameras/discover', {
        params: {
          type: newCameraType,
          existing: existingIdentifiers,
        },
      })
      setAvailableDevices(devices)
      if (devices.length === 0) {
        toast({
          title: 'No devices found',
          description: `No ${newCameraType} cameras detected`,
        })
      }
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to discover devices',
      })
    } finally {
      setIsDiscovering(false)
    }
  }

  const handleLoadProfiles = async (cameraType: string, identifier: string) => {
    if (!cameraType || !identifier) {
      return
    }

    setIsLoadingProfiles(true)
    setAvailableProfiles([])
    setSelectedProfile('')

    try {
      const profiles = await api.get<CameraProfile[]>('/api/cameras/profiles', {
        params: {
          type: cameraType,
          identifier: identifier,
        },
      })

      setAvailableProfiles(profiles || [])

      if (!profiles || profiles.length === 0) {
        toast({
          title: 'No profiles available',
          description: 'Could not query supported resolutions for this camera',
        })
      }
    } catch (error) {
      console.error('Error loading camera profiles:', error)
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to load camera profiles',
      })
    } finally {
      setIsLoadingProfiles(false)
    }
  }

  // Automatically discover devices when camera type changes
  useEffect(() => {
    if (newCameraType) {
      handleDiscoverDevices()
    } else {
      setAvailableDevices([])
      setSelectedDevice('')
    }
  }, [newCameraType])

  // Automatically select device if there's only one
  useEffect(() => {
    if (availableDevices.length === 1) {
      setSelectedDevice(availableDevices[0].identifier)
    }
  }, [availableDevices])

  // Automatically load profiles when device is selected
  useEffect(() => {
    if (selectedDevice && newCameraType) {
      handleLoadProfiles(newCameraType, selectedDevice)
    } else {
      setAvailableProfiles([])
      setSelectedProfile('')
    }
  }, [selectedDevice, newCameraType])

  const handleAddCamera = async () => {
    if (!newCameraName || !newCameraType || !selectedDevice) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Please fill in all fields',
      })
      return
    }

    // Require profile selection for RealSense cameras
    if (newCameraType === 'RealSense' && !selectedProfile) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Please select a resolution and framerate for RealSense cameras',
      })
      return
    }

    setIsLoading(true)
    try {
      // Build camera data as JSON object
      const cameraData: {
        name: string
        camera_type: string
        identifier: string
        resolution?: { width: number; height: number }
        framerate?: number
      } = {
        name: newCameraName,
        camera_type: newCameraType,
        identifier: selectedDevice,
      }

      // Add resolution and framerate if a profile was selected
      if (selectedProfile && availableProfiles.length > 0) {
        // Parse the selected profile (format: "1920x1080@30")
        const match = selectedProfile.match(/(\d+)x(\d+)@(\d+)/)
        if (match) {
          const width = parseInt(match[1])
          const height = parseInt(match[2])
          const fps = parseInt(match[3])

          cameraData.resolution = { width, height }
          cameraData.framerate = fps
        }
      }

      await api.post('/api/cameras/add', cameraData)

      toast({
        title: 'Camera added',
        description: `${newCameraName} successfully configured`,
      })

      setAddModalOpen(false)
      setNewCameraName('')
      setNewCameraType('')
      setSelectedDevice('')
      setAvailableDevices([])
      setAvailableProfiles([])
      setSelectedProfile('')
      await fetchCameras()
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to add camera',
      })
    } finally {
      setIsLoading(false)
    }
  }

  const handleEditCamera = async () => {
    if (!selectedCamera || !editCameraName) return

    setIsLoading(true)
    try {
      // Build update data
      const updateData: {
        name: string
        resolution?: { width: number; height: number }
        framerate?: number
        camera_matrix?: any
        dist_coeffs?: any
      } = {
        name: editCameraName,
      }

      // Add resolution and framerate if a profile was selected
      if (editSelectedProfile && editAvailableProfiles.length > 0) {
        const match = editSelectedProfile.match(/(\d+)x(\d+)@(\d+)/)
        if (match) {
          const width = parseInt(match[1])
          const height = parseInt(match[2])
          const fps = parseInt(match[3])

          updateData.resolution = { width, height }
          updateData.framerate = fps
        }
      }

      // Construct Camera Matrix
      const cameraMatrix = [
        [Number(matrixParams.fx), 0, Number(matrixParams.cx)],
        [0, Number(matrixParams.fy), Number(matrixParams.cy)],
        [0, 0, 1]
      ]
      updateData.camera_matrix = cameraMatrix

      // Construct Distortion Coefficients
      const distCoeffs = [
        Number(distParams.k1),
        Number(distParams.k2),
        Number(distParams.p1),
        Number(distParams.p2),
        Number(distParams.k3)
      ]
      updateData.dist_coeffs = distCoeffs

      await api.post(`/api/cameras/update/${selectedCamera.id}`, updateData)

      // Update local state
      const updatedCameraData: Partial<Camera> = { name: editCameraName }
      if (updateData.resolution && updateData.framerate) {
        updatedCameraData.resolution_json = JSON.stringify(updateData.resolution)
        updatedCameraData.framerate = updateData.framerate
      }
      if (updateData.camera_matrix) {
        updatedCameraData.camera_matrix_json = JSON.stringify(updateData.camera_matrix)
      }
      if (updateData.dist_coeffs) {
        updatedCameraData.dist_coeffs_json = JSON.stringify(updateData.dist_coeffs)
      }
      updateCamera(selectedCamera.id, updatedCameraData)

      toast({
        title: 'Camera updated',
        description: 'Camera settings changed successfully',
      })

      setEditModalOpen(false)
      setSelectedCamera(null)
      setEditCameraName('')
      setEditAvailableProfiles([])
      setEditSelectedProfile('')
      setAdvancedOpen(false)
      setMatrixParams({ fx: 0, fy: 0, cx: 0, cy: 0 })
      setDistParams({ k1: 0, k2: 0, p1: 0, p2: 0, k3: 0 })
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to update camera',
      })
    } finally {
      setIsLoading(false)
    }
  }

  const handleDeleteCamera = async () => {
    if (!selectedCamera) return

    setIsLoading(true)
    try {
      await api.post(`/api/cameras/delete/${selectedCamera.id}`)
      deleteCamera(selectedCamera.id)
      toast({
        title: 'Camera deleted',
        description: `${selectedCamera.name} removed successfully`,
      })

      setDeleteModalOpen(false)
      setSelectedCamera(null)
    } catch (error) {
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to delete camera',
      })
    } finally {
      setIsLoading(false)
    }
  }

  const openEditModal = async (camera: Camera) => {
    setSelectedCamera(camera)
    setEditCameraName(camera.name)
    setEditModalOpen(true)
    setAdvancedOpen(false)

    // Parse Camera Matrix
    try {
      const matrix = camera.camera_matrix_json ? JSON.parse(camera.camera_matrix_json) : null
      if (matrix && Array.isArray(matrix) && matrix.length === 3) {
        setMatrixParams({
          fx: matrix[0][0] || 0,
          fy: matrix[1][1] || 0,
          cx: matrix[0][2] || 0,
          cy: matrix[1][2] || 0
        })
      } else {
        setMatrixParams({ fx: 0, fy: 0, cx: 0, cy: 0 })
      }
    } catch {
      setMatrixParams({ fx: 0, fy: 0, cx: 0, cy: 0 })
    }

    // Parse Distortion Coefficients
    try {
      const dist = camera.dist_coeffs_json ? JSON.parse(camera.dist_coeffs_json) : null
      if (dist && Array.isArray(dist) && dist.length >= 5) {
        setDistParams({
          k1: dist[0] || 0,
          k2: dist[1] || 0,
          p1: dist[2] || 0,
          p2: dist[3] || 0,
          k3: dist[4] || 0
        })
      } else {
        setDistParams({ k1: 0, k2: 0, p1: 0, p2: 0, k3: 0 })
      }
    } catch {
      setDistParams({ k1: 0, k2: 0, p1: 0, p2: 0, k3: 0 })
    }

    // Load available profiles for this camera
    setIsLoadingEditProfiles(true)
    setEditAvailableProfiles([])
    setEditSelectedProfile('')

    try {
      const profiles = await api.get<CameraProfile[]>('/api/cameras/profiles', {
        params: {
          type: camera.camera_type,
          identifier: camera.identifier,
        },
      })

      setEditAvailableProfiles(profiles || [])

      // Set current profile as selected if camera has resolution/framerate
      if (camera.resolution_json && camera.framerate) {
        try {
          const resolution = JSON.parse(camera.resolution_json)
          const currentProfile = `${resolution.width}x${resolution.height}@${camera.framerate}`
          // Check if current profile exists in available profiles
          const profileExists = profiles?.some(
            (p) => `${p.width}x${p.height}@${p.fps}` === currentProfile
          )
          if (profileExists) {
            setEditSelectedProfile(currentProfile)
          }
        } catch {
          // Ignore parsing errors
        }
      }
    } catch (error) {
      console.error('Error loading camera profiles:', error)
      toast({
        variant: 'destructive',
        title: 'Error',
        description: 'Failed to load camera profiles',
      })
    } finally {
      setIsLoadingEditProfiles(false)
    }
  }

  const openDeleteModal = (camera: Camera) => {
    setSelectedCamera(camera)
    setDeleteModalOpen(true)
  }

  const handleAddModalClose = (open: boolean) => {
    setAddModalOpen(open)
    if (!open) {
      // Reset form state when modal closes
      setNewCameraName('')
      setNewCameraType('')
      setSelectedDevice('')
      setAvailableDevices([])
      setAvailableProfiles([])
      setSelectedProfile('')
    }
  }

  return (
    <div className="p-6 space-y-6">
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-3xl font-semibold mb-2" data-testid="page-title-cameras">
            Cameras
          </h1>
          <p className="text-muted">Manage camera devices and configuration</p>
        </div>
        <Button onClick={() => setAddModalOpen(true)}>
          <Plus className="h-4 w-4 mr-2" />
          Add Camera
        </Button>
      </div>

      <Card>
        <CardHeader>
          <CardTitle>Camera Devices</CardTitle>
          <CardDescription>
            Configured cameras and their connection status
          </CardDescription>
        </CardHeader>
        <CardContent>
          {cameras.length === 0 ? (
            <div className="text-center py-8">
              <CameraIcon className="h-12 w-12 text-muted mx-auto mb-4 opacity-50" />
              <p className="text-muted">No cameras configured</p>
              <p className="text-sm text-subtle mt-1">
                Click "Add Camera" to get started
              </p>
            </div>
          ) : (
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead>Name</TableHead>
                  <TableHead>Type</TableHead>
                  <TableHead>Status</TableHead>
                  <TableHead>Resolution/FPS</TableHead>
                  <TableHead className="text-right">Actions</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {cameras.map((camera) => {
                  const status = cameraStatuses[camera.id]
                  return (
                    <TableRow
                      key={camera.id}
                      data-testid="camera-row"
                      data-camera-name={camera.name}
                    >
                      <TableCell className="font-medium">{camera.name}</TableCell>
                      <TableCell>{camera.camera_type}</TableCell>
                      <TableCell>
                        <StatusBadge
                          status={status?.connected ? 'online' : 'offline'}
                          label={status?.connected ? 'Connected' : 'Disconnected'}
                        />
                      </TableCell>
                      <TableCell className="text-sm">
                        {camera.resolution_json && camera.framerate ? (
                          (() => {
                            try {
                              const resolution = JSON.parse(camera.resolution_json)
                              return `${resolution.width}x${resolution.height} @ ${camera.framerate} FPS`
                            } catch {
                              return '—'
                            }
                          })()
                        ) : (
                          <span className="text-muted">Not configured</span>
                        )}
                      </TableCell>
                      <TableCell className="text-right">
                        <div className="flex justify-end gap-2">
                          <Button
                            variant="ghost"
                            size="icon"
                            onClick={() => openEditModal(camera)}
                            aria-label={`Edit ${camera.name}`}
                            data-testid={`edit-camera-${camera.id}`}
                          >
                            <Edit2 className="h-4 w-4" />
                          </Button>
                          <Button
                            variant="ghost"
                            size="icon"
                            onClick={() => openDeleteModal(camera)}
                            aria-label={`Delete ${camera.name}`}
                            data-testid={`delete-camera-${camera.id}`}
                          >
                            <Trash2 className="h-4 w-4 text-[var(--color-danger)]" />
                          </Button>
                        </div>
                      </TableCell>
                    </TableRow>
                  )
                })}
              </TableBody>
            </Table>
          )}
        </CardContent>
      </Card>

      {/* Add Camera Modal */}
      <Dialog open={addModalOpen} onOpenChange={handleAddModalClose}>
        <DialogContent className="max-w-md" data-testid="add-camera-modal">
          <DialogHeader>
            <DialogTitle>Add Camera</DialogTitle>
            <DialogDescription>
              Configure a new camera device
            </DialogDescription>
          </DialogHeader>

          <div className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="camera-name">Camera Name</Label>
              <Input
                id="camera-name"
                value={newCameraName}
                onChange={(e) => setNewCameraName(e.target.value)}
                placeholder="Front Camera"
              />
            </div>

            <div className="space-y-2">
              <Label htmlFor="camera-type">Camera Type</Label>
              <Select value={newCameraType} onValueChange={setNewCameraType}>
                <SelectTrigger id="camera-type">
                  <SelectValue placeholder="Select type" />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="USB">USB Camera</SelectItem>
                  <SelectItem value="Spinnaker">Spinnaker Camera (FLIR)</SelectItem>
                  <SelectItem value="RealSense">Intel RealSense</SelectItem>
                </SelectContent>
              </Select>
            </div>

            <div className="space-y-2">
              <div className="flex items-center justify-between">
                <Label>Available Devices</Label>
                <Button
                  variant="outline"
                  size="sm"
                  onClick={handleDiscoverDevices}
                  disabled={!newCameraType || isDiscovering}
                >
                  <RefreshCw className={`h-4 w-4 mr-2 ${isDiscovering ? 'animate-spin' : ''}`} />
                  {isDiscovering ? 'Refreshing...' : 'Refresh'}
                </Button>
              </div>

              {availableDevices.length > 0 ? (
                <Select value={selectedDevice} onValueChange={setSelectedDevice}>
                  <SelectTrigger>
                    <SelectValue placeholder="Select device" />
                  </SelectTrigger>
                  <SelectContent>
                    {availableDevices.map((device) => (
                      <SelectItem key={device.identifier} value={device.identifier}>
                        {device.name}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              ) : (
                <p className="text-sm text-muted">
                  Click "Discover" to find available devices
                </p>
              )}
            </div>

            {/* Resolution and Framerate Selection */}
            {selectedDevice && availableProfiles.length > 0 && (
              <div className="space-y-2">
                <Label htmlFor="camera-profile">
                  Resolution & Framerate
                  {newCameraType === 'RealSense' && <span className="text-[var(--color-danger)]"> *</span>}
                </Label>
                {isLoadingProfiles ? (
                  <p className="text-sm text-muted">Loading available profiles...</p>
                ) : (
                  <Select value={selectedProfile} onValueChange={setSelectedProfile}>
                    <SelectTrigger id="camera-profile">
                      <SelectValue placeholder="Select resolution and framerate" />
                    </SelectTrigger>
                    <SelectContent>
                      {availableProfiles.map((profile, index) => (
                        <SelectItem
                          key={index}
                          value={`${profile.width}x${profile.height}@${profile.fps}`}
                        >
                          {profile.width}x{profile.height} @ {profile.fps} FPS
                        </SelectItem>
                      ))}
                    </SelectContent>
                  </Select>
                )}
              </div>
            )}
          </div>

          <DialogFooter>
            <Button variant="outline" onClick={() => handleAddModalClose(false)}>
              Cancel
            </Button>
            <Button onClick={handleAddCamera} disabled={isLoading}>
              {isLoading ? 'Adding...' : 'Add Camera'}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* Edit Camera Modal */}
      <Dialog open={editModalOpen} onOpenChange={setEditModalOpen}>
        <DialogContent className="max-w-md">
          <DialogHeader>
            <DialogTitle>Edit Camera</DialogTitle>
            <DialogDescription>
              Change camera settings
            </DialogDescription>
          </DialogHeader>

          <div className="space-y-4">
            <div className="space-y-2">
              <Label htmlFor="edit-camera-name">Camera Name</Label>
              <Input
                id="edit-camera-name"
                value={editCameraName}
                onChange={(e) => setEditCameraName(e.target.value)}
                placeholder="Camera name"
              />
            </div>

            {/* Resolution and Framerate Selection */}
            <div className="space-y-2">
              <Label htmlFor="edit-camera-profile">Resolution & Framerate</Label>
              {isLoadingEditProfiles ? (
                <p className="text-sm text-muted">Loading available profiles...</p>
              ) : editAvailableProfiles.length > 0 ? (
                <Select value={editSelectedProfile} onValueChange={setEditSelectedProfile}>
                  <SelectTrigger id="edit-camera-profile">
                    <SelectValue placeholder="Select resolution and framerate" />
                  </SelectTrigger>
                  <SelectContent>
                    {editAvailableProfiles.map((profile, index) => (
                      <SelectItem
                        key={index}
                        value={`${profile.width}x${profile.height}@${profile.fps}`}
                      >
                        {profile.width}x{profile.height} @ {profile.fps} FPS
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              ) : (
                <p className="text-sm text-muted">No profiles available for this camera</p>
              )}
            </div>

            {/* Advanced Settings */}
            <div className="pt-2">
              <button
                type="button"
                className="flex w-full items-center gap-2 py-2 text-sm text-muted hover:text-[var(--color-text)] transition-colors"
                onClick={() => setAdvancedOpen(!advancedOpen)}
              >
                {advancedOpen ? (
                  <ChevronUp className="h-4 w-4" />
                ) : (
                  <ChevronDown className="h-4 w-4" />
                )}
                <span>Advanced Settings</span>
              </button>

              {advancedOpen && (
                <div className="space-y-6 pt-3 pb-1 animate-in slide-in-from-top-2 duration-200">
                  {/* Camera Matrix */}
                  <div className="space-y-3">
                    <Label className="text-xs font-semibold uppercase tracking-wider text-muted">Camera Matrix</Label>
                    <div className="grid grid-cols-2 gap-4">
                      <div className="space-y-1.5">
                        <Label htmlFor="fx" className="text-xs text-subtle">Focal Length X (fx)</Label>
                        <Input
                          id="fx"
                          type="number"
                          value={matrixParams.fx}
                          onChange={(e) => setMatrixParams({ ...matrixParams, fx: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                      <div className="space-y-1.5">
                        <Label htmlFor="fy" className="text-xs text-subtle">Focal Length Y (fy)</Label>
                        <Input
                          id="fy"
                          type="number"
                          value={matrixParams.fy}
                          onChange={(e) => setMatrixParams({ ...matrixParams, fy: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                      <div className="space-y-1.5">
                        <Label htmlFor="cx" className="text-xs text-subtle">Principal Point X (cx)</Label>
                        <Input
                          id="cx"
                          type="number"
                          value={matrixParams.cx}
                          onChange={(e) => setMatrixParams({ ...matrixParams, cx: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                      <div className="space-y-1.5">
                        <Label htmlFor="cy" className="text-xs text-subtle">Principal Point Y (cy)</Label>
                        <Input
                          id="cy"
                          type="number"
                          value={matrixParams.cy}
                          onChange={(e) => setMatrixParams({ ...matrixParams, cy: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                    </div>
                  </div>

                  {/* Distortion Coefficients */}
                  <div className="space-y-3">
                    <Label className="text-xs font-semibold uppercase tracking-wider text-muted">Distortion Coefficients</Label>
                    <div className="grid grid-cols-3 gap-3">
                      <div className="space-y-1.5">
                        <Label htmlFor="k1" className="text-xs text-subtle">k1</Label>
                        <Input
                          id="k1"
                          type="number"
                          value={distParams.k1}
                          onChange={(e) => setDistParams({ ...distParams, k1: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                      <div className="space-y-1.5">
                        <Label htmlFor="k2" className="text-xs text-subtle">k2</Label>
                        <Input
                          id="k2"
                          type="number"
                          value={distParams.k2}
                          onChange={(e) => setDistParams({ ...distParams, k2: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                      <div className="space-y-1.5">
                        <Label htmlFor="p1" className="text-xs text-subtle">p1</Label>
                        <Input
                          id="p1"
                          type="number"
                          value={distParams.p1}
                          onChange={(e) => setDistParams({ ...distParams, p1: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                      <div className="space-y-1.5">
                        <Label htmlFor="p2" className="text-xs text-subtle">p2</Label>
                        <Input
                          id="p2"
                          type="number"
                          value={distParams.p2}
                          onChange={(e) => setDistParams({ ...distParams, p2: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                      <div className="space-y-1.5">
                        <Label htmlFor="k3" className="text-xs text-subtle">k3</Label>
                        <Input
                          id="k3"
                          type="number"
                          value={distParams.k3}
                          onChange={(e) => setDistParams({ ...distParams, k3: parseFloat(e.target.value) })}
                          className="font-mono text-xs"
                        />
                      </div>
                    </div>
                  </div>
                </div>
              )}
            </div>
          </div>

          <DialogFooter>
            <Button variant="outline" onClick={() => setEditModalOpen(false)}>
              Cancel
            </Button>
            <Button onClick={handleEditCamera} disabled={isLoading}>
              {isLoading ? 'Saving...' : 'Save Changes'}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {/* Delete Confirmation Modal */}
      <Dialog open={deleteModalOpen} onOpenChange={setDeleteModalOpen}>
        <DialogContent className="max-w-md">
          <DialogHeader>
            <DialogTitle>Delete Camera</DialogTitle>
            <DialogDescription>
              Are you sure you want to delete "{selectedCamera?.name}"? This will also remove
              all associated pipelines. This action cannot be undone.
            </DialogDescription>
          </DialogHeader>

          <DialogFooter>
            <Button variant="outline" onClick={() => setDeleteModalOpen(false)}>
              Cancel
            </Button>
            <Button variant="destructive" onClick={handleDeleteCamera} disabled={isLoading}>
              {isLoading ? 'Deleting...' : 'Delete Camera'}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  )
}
