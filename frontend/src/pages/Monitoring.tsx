import { Activity, Cpu, HardDrive, Thermometer, AlertCircle } from 'lucide-react'
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { Badge } from '@/components/ui/badge'
import { Progress } from '@/components/ui/progress'
import { useVisionSubscription, useVisionWSConnected } from '@/hooks/useVisionWebSocket'
import type { MetricsSummary, PipelineMetrics, MetricsThresholds } from '@/types'

export default function Monitoring() {
  const metrics = useVisionSubscription<MetricsSummary>('metrics')
  const wsConnected = useVisionWSConnected()
  const isLoading = !metrics && wsConnected

  // Helper to determine pipeline status based on metrics
  const getPipelineStatus = (p: PipelineMetrics, thresholds: MetricsThresholds) => {
    if (p.drops.window_total > 0) return { label: 'Drops', variant: 'destructive' as const }
    if (p.latency_ms.total.p95_ms > thresholds.latency_warning_ms) return { label: 'Slow', variant: 'warning' as const }
    if (p.queue.current_depth > thresholds.queue_warning) return { label: 'Queue Full', variant: 'warning' as const }
    return { label: 'Nominal', variant: 'success' as const }
  }

  if (isLoading) {
    return (
      <div className="p-6">
        <div className="flex items-center justify-center h-96">
          <div className="flex flex-col items-center gap-2">
            <div className="h-8 w-8 animate-spin rounded-full border-4 border-[var(--color-border-strong)] border-t-[var(--color-primary)]"></div>
            <p className="text-sm text-muted">Loading metrics...</p>
          </div>
        </div>
      </div>
    )
  }

  if (!wsConnected) {
    return (
      <div className="p-6">
        <div className="flex items-center justify-center h-96">
          <div className="text-center">
            <AlertCircle className="h-12 w-12 text-[var(--color-danger)] mx-auto mb-4" />
            <h2 className="text-xl font-semibold mb-2">Connection Lost</h2>
            <p className="text-muted">WebSocket disconnected. Reconnecting...</p>
          </div>
        </div>
      </div>
    )
  }

  if (!metrics) {
    return null
  }

  const { system, pipelines, thresholds } = metrics

  return (
    <div className="p-6 space-y-6">
      <div>
        <h1 className="text-3xl font-semibold mb-2">Monitoring</h1>
        <p className="text-muted">Real-time system and pipeline performance metrics</p>
      </div>

      {/* System Resources */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
        <Card>
          <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
            <CardTitle className="text-sm font-medium">CPU Usage</CardTitle>
            <Cpu className="h-4 w-4 text-muted" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">{(system.cpu_usage_percent ?? 0).toFixed(1)}%</div>
            <Progress value={system.cpu_usage_percent ?? 0} className="mt-2" />
            <p className="text-xs text-muted mt-2">
              {(system.cpu_usage_percent ?? 0) > 80 ? 'High usage' : 'Normal'}
            </p>
          </CardContent>
        </Card>

        <Card>
          <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
            <CardTitle className="text-sm font-medium">RAM Usage</CardTitle>
            <HardDrive className="h-4 w-4 text-muted" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">{(system.ram_usage_percent ?? 0).toFixed(1)}%</div>
            <Progress value={system.ram_usage_percent ?? 0} className="mt-2" />
            <p className="text-xs text-muted mt-2">
              {(system.ram_used_mb ?? 0).toFixed(0)} / {(system.ram_total_mb ?? 0).toFixed(0)} MB
            </p>
          </CardContent>
        </Card>

        <Card>
          <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
            <CardTitle className="text-sm font-medium">CPU Temperature</CardTitle>
            <Thermometer className="h-4 w-4 text-muted" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">
              {system.cpu_temperature > 0 ? `${system.cpu_temperature.toFixed(1)}°C` : 'N/A'}
            </div>
            {system.cpu_temperature > 0 && (
              <Progress
                value={Math.min((system.cpu_temperature / 85) * 100, 100)}
                className="mt-2"
              />
            )}
            <p className="text-xs text-muted mt-2">
              {system.cpu_temperature > 70 ? 'Hot' : 'Normal'}
            </p>
          </CardContent>
        </Card>

        <Card>
          <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
            <CardTitle className="text-sm font-medium">Active Pipelines</CardTitle>
            <Activity className="h-4 w-4 text-muted" />
          </CardHeader>
          <CardContent>
            <div className="text-2xl font-bold">{system.active_pipelines ?? 0}</div>
          </CardContent>
        </Card>
      </div>


      {/* Pipeline Metrics Table */}
      {pipelines.length > 0 ? (
        <Card>
          <CardHeader>
            <CardTitle>Pipeline Performance</CardTitle>
            <CardDescription>
              Real-time metrics for active vision pipelines
            </CardDescription>
          </CardHeader>
          <CardContent>
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead>Pipeline</TableHead>
                  <TableHead>FPS</TableHead>
                  <TableHead>Latency (avg)</TableHead>
                  <TableHead>Latency (p95)</TableHead>
                  <TableHead>Latency (max)</TableHead>
                  <TableHead>Queue Depth</TableHead>
                  <TableHead>Drops</TableHead>
                  <TableHead>Status</TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {pipelines.map((pipeline) => {
                  const status = getPipelineStatus(pipeline, thresholds)
                  const displayName = `${pipeline.camera_identifier} · ${pipeline.pipeline_type}`
                  
                  return (
                    <TableRow key={`${pipeline.camera_identifier}-${pipeline.pipeline_id}`}>
                      <TableCell className="font-medium">
                        <div className="flex flex-col">
                            <span>{displayName}</span>
                            <span className="text-xs text-muted">ID: {pipeline.pipeline_id}</span>
                        </div>
                      </TableCell>
                      <TableCell>{(pipeline.fps ?? 0).toFixed(1)}</TableCell>
                      <TableCell>{(pipeline.latency_ms?.total?.avg_ms ?? 0).toFixed(1)}ms</TableCell>
                      <TableCell>{(pipeline.latency_ms?.total?.p95_ms ?? 0).toFixed(1)}ms</TableCell>
                      <TableCell>{(pipeline.latency_ms?.total?.max_ms ?? 0).toFixed(1)}ms</TableCell>
                      <TableCell>
                        <div className="flex flex-col">
                            <span className={pipeline.queue.current_depth > 5 ? 'text-[var(--color-warning)]' : ''}>
                            {pipeline.queue.current_depth} / {pipeline.queue.max_size || '∞'}
                            </span>
                            <span className="text-xs text-muted">{pipeline.queue.utilization_pct.toFixed(0)}%</span>
                        </div>
                      </TableCell>
                      <TableCell>
                        <div className="flex flex-col">
                            <span className={pipeline.drops.window_total > 0 ? 'text-[var(--color-danger)]' : ''}>
                            {pipeline.drops.window_total}
                            </span>
                            <span className="text-xs text-muted">{pipeline.drops.per_minute.toFixed(1)}/min</span>
                        </div>
                      </TableCell>
                      <TableCell>
                        <Badge variant={status.variant}>
                          {status.label}
                        </Badge>
                      </TableCell>
                    </TableRow>
                  )
                })}
              </TableBody>
            </Table>
          </CardContent>
        </Card>
      ) : (
        <Card>
          <CardContent className="py-8">
            <div className="text-center">
              <Activity className="h-12 w-12 text-muted mx-auto mb-4 opacity-50" />
              <p className="text-muted">No active pipelines</p>
              <p className="text-sm text-subtle mt-1">
                Configure pipelines in the Dashboard to see performance metrics
              </p>
            </div>
          </CardContent>
        </Card>
      )}

      {/* Thresholds Info */}
      <Card>
        <CardHeader>
          <CardTitle>Monitoring Configuration</CardTitle>
        </CardHeader>
        <CardContent>
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4 text-sm">
            <div>
              <span className="text-muted">Latency Warning:</span>
              <span className="ml-2 font-medium">{thresholds.latency_warning_ms}ms</span>
            </div>
            <div>
              <span className="text-muted">Latency Critical:</span>
              <span className="ml-2 font-medium">{thresholds.latency_critical_ms}ms</span>
            </div>
            <div>
              <span className="text-muted">Queue Warning:</span>
              <span className="ml-2 font-medium">{thresholds.queue_warning}</span>
            </div>
            <div>
              <span className="text-muted">Queue Critical:</span>
              <span className="ml-2 font-medium">{thresholds.queue_critical}</span>
            </div>
          </div>
        </CardContent>
      </Card>
    </div>
  )
}