import { useState, useEffect } from 'react'

// Message types from server
interface WebSocketMessage {
  type: 'metrics' | 'nt_status' | 'camera_status' | 'pipeline_results' | 'pong'
  data?: unknown
  cameraId?: number
  pipelineId?: number
}

// Subscription key generator
function makeSubKey(topic: string, params?: Record<string, unknown>): string {
  if (!params || Object.keys(params).length === 0) {
    return topic
  }
  return `${topic}:${JSON.stringify(params)}`
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

  private static instance: VisionWebSocketManager | null = null

  static getInstance(): VisionWebSocketManager {
    if (!VisionWebSocketManager.instance) {
      VisionWebSocketManager.instance = new VisionWebSocketManager()
    }
    return VisionWebSocketManager.instance
  }

  private constructor() {
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

    this.isConnecting = true
    const wsUrl = this.getWebSocketUrl()

    try {
      this.ws = new WebSocket(wsUrl)

      this.ws.onopen = () => {
        console.log('[VisionWS] Connected')
        this.isConnecting = false
        this.notifyConnectionListeners(true)

        // Re-subscribe all active subscriptions
        for (const [key] of this.subscribers) {
          this.sendSubscription(key, true)
        }

        // Start ping interval
        this.pingInterval = window.setInterval(() => {
          if (this.ws?.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify({ type: 'ping' }))
          }
        }, 30000)
      }

      this.ws.onmessage = (event) => {
        try {
          const msg: WebSocketMessage = JSON.parse(event.data)
          this.handleMessage(msg)
        } catch (e) {
          console.error('[VisionWS] Failed to parse message:', e)
        }
      }

      this.ws.onerror = (error) => {
        console.error('[VisionWS] Error:', error)
      }

      this.ws.onclose = () => {
        console.log('[VisionWS] Disconnected, reconnecting...')
        this.isConnecting = false
        this.notifyConnectionListeners(false)
        this.cleanup()

        // Schedule reconnection
        this.reconnectTimeoutId = setTimeout(() => {
          this.connect()
        }, 3000)
      }
    } catch (e) {
      console.error('[VisionWS] Failed to create WebSocket:', e)
      this.isConnecting = false
      this.reconnectTimeoutId = setTimeout(() => {
        this.connect()
      }, 3000)
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

  private handleMessage(msg: WebSocketMessage): void {
    if (msg.type === 'pong') return

    // Route message to subscribers
    let subKey: string

    switch (msg.type) {
      case 'metrics':
        subKey = 'metrics'
        break
      case 'nt_status':
        subKey = 'nt_status'
        break
      case 'camera_status':
        subKey = makeSubKey('camera_status', { cameraId: msg.cameraId })
        break
      case 'pipeline_results':
        subKey = makeSubKey('pipeline_results', { cameraId: msg.cameraId, pipelineId: msg.pipelineId })
        break
      default:
        return
    }

    const callbacks = this.subscribers.get(subKey)
    if (callbacks) {
      for (const callback of callbacks) {
        callback(msg.data)
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
      const params = JSON.parse(paramsStr)
      Object.assign(msg, params)
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
  const [paramsKey, setParamsKey] = useState(() =>
    params ? JSON.stringify(params) : ''
  )

  // Update paramsKey only when the stringified value actually changes
  const currentParamsKey = params ? JSON.stringify(params) : ''
  if (currentParamsKey !== paramsKey) {
    setParamsKey(currentParamsKey)
  }

  useEffect(() => {
    const ws = getVisionWS()
    const parsedParams = paramsKey ? JSON.parse(paramsKey) : undefined
    const unsubscribe = ws.subscribe(topic, parsedParams, (newData) => {
      setData(newData as T)
    })

    return unsubscribe
  }, [topic, paramsKey])

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
