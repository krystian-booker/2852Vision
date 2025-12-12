import { useState, useEffect, useMemo } from 'react'

// Message types from server
interface WebSocketMessage {
  type: 'metrics' | 'nt_status' | 'camera_status' | 'pipeline_results' | 'pong'
  data?: unknown
  cameraId?: number
  pipelineId?: number
}

// Valid message types for runtime validation
const VALID_MESSAGE_TYPES = ['metrics', 'nt_status', 'camera_status', 'pipeline_results', 'pong'] as const

// Type guard for validating incoming messages
function isValidWebSocketMessage(data: unknown): data is WebSocketMessage {
  if (typeof data !== 'object' || data === null) return false
  const msg = data as Record<string, unknown>
  return typeof msg.type === 'string' && VALID_MESSAGE_TYPES.includes(msg.type as typeof VALID_MESSAGE_TYPES[number])
}

// Subscription key generator
function makeSubKey(topic: string, params?: Record<string, unknown>): string {
  if (!params || Object.keys(params).length === 0) {
    return topic
  }
  // Sort keys to ensure deterministic stringify
  const sortedParams: Record<string, unknown> = {}
  Object.keys(params).sort().forEach(key => {
    sortedParams[key] = params[key]
  })
  return `${topic}:${JSON.stringify(sortedParams)}`
}

type SubscriptionCallback = (data: unknown) => void

// Singleton WebSocket connection manager
class VisionWebSocketManager {
  private ws: WebSocket | null = null
  private subscribers = new Map<string, Set<SubscriptionCallback>>()
  private reconnectTimeoutId: ReturnType<typeof setTimeout> | null = null
  private pingInterval: number | null = null
  private isConnecting = false
  private connectionListeners = new Set<(connected: boolean) => void>()
  private reconnectAttempts = 0
  private readonly maxReconnectDelay = 30000  // Max 30 seconds between retries
  private lastPongTime = 0
  private readonly pongTimeoutMs = 35000  // Consider connection dead if no pong within 35s (ping every 30s)
  private readonly handleVisibilityChange: () => void

  private static instance: VisionWebSocketManager | null = null

  static getInstance(): VisionWebSocketManager {
    if (!VisionWebSocketManager.instance) {
      VisionWebSocketManager.instance = new VisionWebSocketManager()
    }
    return VisionWebSocketManager.instance
  }

  private constructor() {
    // Set up visibility change handler to disconnect when tab is hidden
    this.handleVisibilityChange = () => {
      if (document.hidden) {
        console.log('[VisionWS] Tab hidden, disconnecting to save resources')
        this.disconnect()
      } else {
        console.log('[VisionWS] Tab visible, reconnecting')
        this.connect()
      }
    }
    document.addEventListener('visibilitychange', this.handleVisibilityChange)

    // Auto-connect when instantiated
    this.connect()
  }

  private getWebSocketUrl(): string {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
    const host = window.location.host
    return `${protocol}//${host}/ws/vision`
  }

  connect(): void {
    if (this.ws?.readyState === WebSocket.OPEN || this.isConnecting) {
      return
    }

    const wsUrl = this.getWebSocketUrl()

    try {
      this.ws = new WebSocket(wsUrl)
      // Set isConnecting after successful WebSocket creation to avoid race condition
      this.isConnecting = true

      this.ws.onopen = () => {
        console.log('[VisionWS] Connected')
        this.isConnecting = false
        this.reconnectAttempts = 0  // Reset backoff on successful connection
        this.lastPongTime = Date.now()  // Initialize pong time on connection
        this.notifyConnectionListeners(true)

        // Re-subscribe all active subscriptions
        for (const [key] of this.subscribers) {
          this.sendSubscription(key, true)
        }

        // Start ping interval with pong timeout detection
        this.pingInterval = window.setInterval(() => {
          if (this.ws?.readyState === WebSocket.OPEN) {
            // Check if we received a pong recently (connection health check)
            const timeSinceLastPong = Date.now() - this.lastPongTime
            if (timeSinceLastPong > this.pongTimeoutMs) {
              console.warn('[VisionWS] No pong received, connection appears dead. Reconnecting...')
              this.ws.close()  // Will trigger onclose and reconnect
              return
            }
            this.ws.send(JSON.stringify({ type: 'ping' }))
          }
        }, 30000)
      }

      this.ws.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data)
          if (!isValidWebSocketMessage(data)) {
            console.warn('[VisionWS] Invalid message format:', data)
            return
          }
          this.handleMessage(data)
        } catch (e) {
          console.error('[VisionWS] Failed to parse message:', e)
        }
      }

      this.ws.onerror = (error) => {
        console.error('[VisionWS] Error:', error)
      }

      this.ws.onclose = () => {
        const delay = this.getReconnectDelay()
        console.log(`[VisionWS] Disconnected, reconnecting in ${delay}ms...`)
        this.isConnecting = false
        this.notifyConnectionListeners(false)
        this.cleanup()

        // Schedule reconnection with exponential backoff
        this.reconnectTimeoutId = setTimeout(() => {
          this.connect()
        }, delay)
      }
    } catch (e) {
      console.error('[VisionWS] Failed to create WebSocket:', e)
      this.isConnecting = false
      const delay = this.getReconnectDelay()
      this.reconnectTimeoutId = setTimeout(() => {
        this.connect()
      }, delay)
    }
  }

  private cleanup(): void {
    if (this.pingInterval) {
      clearInterval(this.pingInterval)
      this.pingInterval = null
    }
    if (this.reconnectTimeoutId) {
      clearTimeout(this.reconnectTimeoutId)
      this.reconnectTimeoutId = null
    }
  }

  private getReconnectDelay(): number {
    // Exponential backoff: 1s, 2s, 4s, 8s, 16s, then cap at maxReconnectDelay
    const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts), this.maxReconnectDelay)
    this.reconnectAttempts++
    return delay
  }

  // Disconnect without scheduling reconnect (used for visibility-based disconnection)
  disconnect(): void {
    this.cleanup()
    if (this.ws) {
      this.ws.onclose = null  // Prevent automatic reconnection
      this.ws.close()
      this.ws = null
    }
    this.isConnecting = false
    this.notifyConnectionListeners(false)
  }

  destroy(): void {
    document.removeEventListener('visibilitychange', this.handleVisibilityChange)
    this.cleanup()
    if (this.ws) {
      this.ws.onclose = null  // Prevent reconnection attempt
      this.ws.close()
      this.ws = null
    }
    this.subscribers.clear()
    this.connectionListeners.clear()
    this.isConnecting = false
    this.reconnectAttempts = 0
    VisionWebSocketManager.instance = null
  }

  private handleMessage(msg: WebSocketMessage): void {
    if (msg.type === 'pong') {
      this.lastPongTime = Date.now()
      return
    }

    // Route message to subscribers
    let subKey: string | undefined

    switch (msg.type) {
      case 'metrics':
        subKey = 'metrics'
        break
      case 'nt_status':
        subKey = 'nt_status'
        break
      case 'camera_status':
        if (msg.cameraId !== undefined) {
          subKey = makeSubKey('camera_status', { cameraId: msg.cameraId })
        }
        break
      case 'pipeline_results':
        if (msg.cameraId !== undefined && msg.pipelineId !== undefined) {
          subKey = makeSubKey('pipeline_results', { cameraId: msg.cameraId, pipelineId: msg.pipelineId })
        }
        break
      default:
        return
    }

    if (subKey) {
      const callbacks = this.subscribers.get(subKey)
      if (callbacks) {
        for (const callback of callbacks) {
          callback(msg.data)
        }
      }
    }
  }

  private sendSubscription(subKey: string, subscribe: boolean): void {
    if (this.ws?.readyState !== WebSocket.OPEN) return

    const [topic, paramsStr] = subKey.includes(':')
      ? [subKey.split(':')[0], subKey.slice(subKey.indexOf(':') + 1)]
      : [subKey, null]

    const msg: Record<string, unknown> = {
      type: subscribe ? 'subscribe' : 'unsubscribe',
      topic
    }

    if (paramsStr) {
      try {
        const params = JSON.parse(paramsStr)
        Object.assign(msg, params)
      } catch (e) {
        console.error('[VisionWS] Failed to parse subscription params:', e)
        return
      }
    }

    this.ws.send(JSON.stringify(msg))
  }

  subscribe(topic: string, params: Record<string, unknown> | undefined, callback: SubscriptionCallback): () => void {
    const subKey = makeSubKey(topic, params)

    if (!this.subscribers.has(subKey)) {
      this.subscribers.set(subKey, new Set())
      // Send subscribe message if connected
      this.sendSubscription(subKey, true)
    }

    this.subscribers.get(subKey)!.add(callback)

    // Return unsubscribe function
    return () => {
      const callbacks = this.subscribers.get(subKey)
      if (callbacks) {
        callbacks.delete(callback)
        if (callbacks.size === 0) {
          this.subscribers.delete(subKey)
          this.sendSubscription(subKey, false)
        }
      }
    }
  }

  onConnectionChange(callback: (connected: boolean) => void): () => void {
    this.connectionListeners.add(callback)
    // Immediately notify current state
    callback(this.ws?.readyState === WebSocket.OPEN)
    return () => {
      this.connectionListeners.delete(callback)
    }
  }

  private notifyConnectionListeners(connected: boolean): void {
    for (const listener of this.connectionListeners) {
      listener(connected)
    }
  }

  isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN
  }
}

// Export singleton instance getter
export const getVisionWS = () => VisionWebSocketManager.getInstance()

// Hook to subscribe to a topic
export function useVisionSubscription<T>(
  topic: string,
  params?: Record<string, unknown>
): T | undefined {
  const [data, setData] = useState<T>()

  // Stabilize params to prevent unnecessary re-subscriptions when inline objects are passed
  const paramsKey = params ? JSON.stringify(params) : ''
  const stableParams = useMemo(
    () => (paramsKey ? JSON.parse(paramsKey) : undefined),
    [paramsKey]
  )

  useEffect(() => {
    const ws = getVisionWS()
    const unsubscribe = ws.subscribe(topic, stableParams, (newData) => {
      setData(newData as T)
    })

    return unsubscribe
  }, [topic, stableParams])

  return data
}

// Hook to track WebSocket connection state
export function useVisionWSConnected(): boolean {
  const [connected, setConnected] = useState(false)

  useEffect(() => {
    const ws = getVisionWS()
    return ws.onConnectionChange(setConnected)
  }, [])

  return connected
}
