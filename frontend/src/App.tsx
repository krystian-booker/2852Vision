import { Suspense, lazy } from 'react'
import { createBrowserRouter, RouterProvider } from 'react-router-dom'
import { Toaster } from './components/ui/toaster'
import Layout from './components/layout/Layout'
import LoadingSpinner from './components/shared/LoadingSpinner'

// Lazy load pages for code splitting
const Dashboard = lazy(() => import('./pages/Dashboard'))
const Cameras = lazy(() => import('./pages/Cameras'))
const Settings = lazy(() => import('./pages/Settings'))
const Monitoring = lazy(() => import('./pages/Monitoring'))
const Calibration = lazy(() => import('./pages/Calibration'))
const NotFound = lazy(() => import('./pages/NotFound'))

const router = createBrowserRouter([
  {
    path: '/',
    element: <Layout />,
    children: [
      {
        index: true,
        element: (
          <Suspense fallback={<LoadingSpinner />}>
            <Dashboard />
          </Suspense>
        ),
      },
      {
        path: 'cameras',
        element: (
          <Suspense fallback={<LoadingSpinner />}>
            <Cameras />
          </Suspense>
        ),
      },
      {
        path: 'settings',
        element: (
          <Suspense fallback={<LoadingSpinner />}>
            <Settings />
          </Suspense>
        ),
      },
      {
        path: 'monitoring',
        element: (
          <Suspense fallback={<LoadingSpinner />}>
            <Monitoring />
          </Suspense>
        ),
      },
      {
        path: 'calibration',
        element: (
          <Suspense fallback={<LoadingSpinner />}>
            <Calibration />
          </Suspense>
        ),
      },
      {
        path: '*',
        element: (
          <Suspense fallback={<LoadingSpinner />}>
            <NotFound />
          </Suspense>
        ),
      },
    ],
  },
])

function App() {
  return (
    <>
      <RouterProvider router={router} />
      <Toaster />
    </>
  )
}

export default App
