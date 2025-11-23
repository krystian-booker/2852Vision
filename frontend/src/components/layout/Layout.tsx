import { useState } from 'react'
import { Link, Outlet, useLocation } from 'react-router-dom'
import {
  LayoutDashboard,
  Camera,
  Settings,
  Activity,
  Menu,
  X,
  Scan
} from 'lucide-react'

const navigation = [
  { name: 'Dashboard', href: '/', icon: LayoutDashboard },
  { name: 'Monitoring', href: '/monitoring', icon: Activity },
  { name: 'Cameras', href: '/cameras', icon: Camera },
  { name: 'Calibration', href: '/calibration', icon: Scan },
  { name: 'Settings', href: '/settings', icon: Settings },
]

export default function Layout() {
  const [sidebarOpen, setSidebarOpen] = useState(false)
  const location = useLocation()

  return (
    <div className="min-h-screen">
      {/* Mobile sidebar overlay */}
      {sidebarOpen && (
        <div
          className="fixed inset-0 z-40 bg-black/50 lg:hidden"
          onClick={() => setSidebarOpen(false)}
        />
      )}

      {/* Sidebar */}
      <aside
        className={`fixed top-0 left-0 z-50 h-full w-[280px] gradient-sidebar border-r border-[var(--glass-border)] backdrop-blur-lg transform transition-transform lg:translate-x-0 ${sidebarOpen ? 'translate-x-0' : '-translate-x-full'
          }`}
      >
        <div className="flex flex-col h-full">
          {/* Logo */}
          <div className="flex items-center justify-between p-6 border-b border-[var(--glass-border)]">
            <div className="flex items-center gap-3">
              <div className="brand-mark">
                <span className="brand-mark__pip"></span>
                <span className="brand-mark__pip"></span>
                <span className="brand-mark__pip"></span>
              </div>
              <div>
                <h1 className="text-xl font-bold text-primary text-technical-wide">2852Vision</h1>
                <p className="text-xs text-muted uppercase tracking-wide">Vision Control</p>
              </div>
            </div>
            <button
              onClick={() => setSidebarOpen(false)}
              className="lg:hidden text-muted hover:text-text transition-colors"
            >
              <X size={24} />
            </button>
          </div>

          {/* Navigation */}
          <nav className="flex-1 p-4 space-y-1">
            {navigation.map((item) => {
              const isActive = location.pathname === item.href
              const Icon = item.icon
              return (
                <Link
                  key={item.name}
                  to={item.href}
                  onClick={() => setSidebarOpen(false)}
                  className={`flex items-center gap-3 px-4 py-2.5 rounded-[var(--radius-sm)] text-sm font-medium uppercase tracking-wide transition-all ${isActive
                      ? 'bg-[var(--color-surface-alt)] text-[var(--color-teal)] shadow-[0_0_12px_rgba(0,194,168,0.15)]'
                      : 'text-muted hover:bg-[var(--color-surface-alt)] hover:text-text'
                    }`}
                >
                  <Icon size={20} />
                  {item.name}
                </Link>
              )
            })}
          </nav>

          {/* Footer */}
          <div className="p-4 border-t border-[var(--glass-border)]">
            <p className="text-xs text-subtle uppercase tracking-wide">
              FRC Vision System
            </p>
          </div>
        </div>
      </aside>

      {/* Main content */}
      <div className="lg:pl-[280px]">
        {/* Top bar (mobile) */}
        <header className="sticky top-0 z-30 bg-[var(--glass-bg-heavy)] backdrop-blur-lg border-b border-[var(--glass-border)] lg:hidden">
          <div className="flex items-center justify-between px-4 h-16">
            <button
              onClick={() => setSidebarOpen(true)}
              className="text-muted hover:text-text transition-colors"
            >
              <Menu size={24} />
            </button>
            <h2 className="text-sm font-medium uppercase tracking-wide">2852Vision Control</h2>
            <div className="w-6" /> {/* Spacer for centering */}
          </div>
        </header>

        {/* Page content */}
        <main>
          <Outlet />
        </main>
      </div>
    </div>
  )
}
