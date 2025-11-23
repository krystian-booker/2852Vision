import { Suspense, lazy } from 'react'
import { BrowserRouter, Routes, Route } from 'react-router-dom'
import { Toaster } from './components/ui/toaster'
import Layout from './components/layout/Layout'
import LoadingSpinner from './components/shared/LoadingSpinner'

// Lazy load pages for code splitting
const Dashboard = lazy(() => import('./pages/Dashboard'))
const Cameras = lazy(() => import('./pages/Cameras'))
const Settings = lazy(() => import('./pages/Settings'))
const Monitoring = lazy(() => import('./pages/Monitoring'))
const Calibration = lazy(() => import('./pages/Calibration'))

function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<Layout />}>
          <Route
            index
            element={
              <Suspense fallback={<LoadingSpinner />}>
                <Dashboard />
              </Suspense>
            }
          />
          <Route
            path="cameras"
            element={
              <Suspense fallback={<LoadingSpinner />}>
                <Cameras />
              </Suspense>
            }
          />
          <Route
          />
          <Route
            path="settings"
            element={
              <Suspense fallback={<LoadingSpinner />}>
                <Settings />
              </Suspense>
            }
          />
          <Route
            path="monitoring"
            element={
              <Suspense fallback={<LoadingSpinner />}>
                <Monitoring />
              </Suspense>
            }
          />
          <Route
            path="calibration"
            element={
              <Suspense fallback={<LoadingSpinner />}>
                <Calibration />
              </Suspense>
            }
          />
        </Route>
      </Routes>
      <Toaster />
    </BrowserRouter>
  )
}

export default App
