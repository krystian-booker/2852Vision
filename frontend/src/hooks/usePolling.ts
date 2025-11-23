import { useEffect, useRef } from 'react'

/**
 * Hook for polling an API endpoint at regular intervals
 * @param callback Function to call on each poll
 * @param interval Polling interval in milliseconds
 * @param enabled Whether polling is enabled
 */
export function usePolling(
  callback: () => void | Promise<void>,
  interval: number,
  enabled = true
) {
  const savedCallback = useRef(callback)

  // Update ref when callback changes
  useEffect(() => {
    savedCallback.current = callback
  }, [callback])

  // Set up the interval
  useEffect(() => {
    if (!enabled) return

    const tick = () => {
      savedCallback.current()
    }

    // Call immediately on mount
    tick()

    // Then set up interval
    const id = setInterval(tick, interval)

    return () => clearInterval(id)
  }, [interval, enabled])
}
