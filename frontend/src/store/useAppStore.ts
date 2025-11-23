import { create } from 'zustand'
import type { Camera, Pipeline } from '@/types'

interface AppState {
  // Cameras
  cameras: Camera[]
  selectedCameraId: number | null
  setCameras: (cameras: Camera[]) => void
  addCamera: (camera: Camera) => void
  updateCamera: (id: number, updates: Partial<Camera>) => void
  deleteCamera: (id: number) => void
  selectCamera: (id: number | null) => void

  // Pipelines
  pipelines: Pipeline[]
  selectedPipelineId: number | null
  setPipelines: (pipelines: Pipeline[]) => void
  addPipeline: (pipeline: Pipeline) => void
  updatePipeline: (id: number, updates: Partial<Pipeline>) => void
  deletePipeline: (id: number) => void
  selectPipeline: (id: number | null) => void
  getPipelinesForCamera: (cameraId: number) => Pipeline[]

  // UI State
  sidebarOpen: boolean
  toggleSidebar: () => void
  setSidebarOpen: (open: boolean) => void
}

export const useAppStore = create<AppState>((set, get) => ({
  // Initial state
  cameras: [],
  selectedCameraId: null,
  pipelines: [],
  selectedPipelineId: null,
  sidebarOpen: false,

  // Camera actions
  setCameras: (cameras) => set({ cameras }),

  addCamera: (camera) =>
    set((state) => ({ cameras: [...state.cameras, camera] })),

  updateCamera: (id, updates) =>
    set((state) => ({
      cameras: state.cameras.map((cam) =>
        cam.id === id ? { ...cam, ...updates } : cam
      ),
    })),

  deleteCamera: (id) =>
    set((state) => ({
      cameras: state.cameras.filter((cam) => cam.id !== id),
      selectedCameraId: state.selectedCameraId === id ? null : state.selectedCameraId,
      pipelines: state.pipelines.filter((pipe) => pipe.camera_id !== id),
    })),

  selectCamera: (id) => set({ selectedCameraId: id }),

  // Pipeline actions
  setPipelines: (pipelines) => set({ pipelines }),

  addPipeline: (pipeline) =>
    set((state) => ({ pipelines: [...state.pipelines, pipeline] })),

  updatePipeline: (id, updates) =>
    set((state) => ({
      pipelines: state.pipelines.map((pipe) =>
        pipe.id === id ? { ...pipe, ...updates } : pipe
      ),
    })),

  deletePipeline: (id) =>
    set((state) => ({
      pipelines: state.pipelines.filter((pipe) => pipe.id !== id),
      selectedPipelineId: state.selectedPipelineId === id ? null : state.selectedPipelineId,
    })),

  selectPipeline: (id) => set({ selectedPipelineId: id }),

  getPipelinesForCamera: (cameraId) => {
    const state = get()
    return state.pipelines.filter((pipe) => pipe.camera_id === cameraId)
  },

  // UI actions
  toggleSidebar: () => set((state) => ({ sidebarOpen: !state.sidebarOpen })),

  setSidebarOpen: (open) => set({ sidebarOpen: open }),
}))
