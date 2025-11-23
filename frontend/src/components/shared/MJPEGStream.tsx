import { useState } from 'react'
import { cn } from '@/lib/utils'

interface MJPEGStreamProps {
  src: string
  alt?: string
  className?: string
  onError?: () => void
  onLoad?: () => void
}

/**
 * MJPEG Stream viewer component for displaying live camera feeds
 * Handles loading states and error conditions
 */
export function MJPEGStream({
  src,
  alt = 'MJPEG Stream',
  className,
  onError,
  onLoad,
}: MJPEGStreamProps) {
  const [isLoading, setIsLoading] = useState(true)
  const [hasError, setHasError] = useState(false)

  const handleLoad = () => {
    setIsLoading(false)
    setHasError(false)
    onLoad?.()
  }

  const handleError = () => {
    setIsLoading(false)
    setHasError(true)
    onError?.()
  }

  return (
    <div
      className={cn(
        'relative overflow-hidden rounded-md bg-[var(--color-surface)]',
        className
      )}
    >
      {isLoading && !hasError && (
        <div className="absolute inset-0 flex items-center justify-center bg-[var(--color-surface-alt)]">
          <div className="flex flex-col items-center gap-2">
            <div className="h-8 w-8 animate-spin rounded-full border-4 border-[var(--color-border-strong)] border-t-[var(--color-primary)]"></div>
            <p className="text-sm text-muted">Loading stream...</p>
          </div>
        </div>
      )}

      {hasError && (
        <div className="absolute inset-0 flex items-center justify-center bg-[var(--color-surface-alt)]">
          <div className="flex flex-col items-center gap-2 p-4 text-center">
            <svg
              className="h-12 w-12 text-[var(--color-danger)]"
              fill="none"
              viewBox="0 0 24 24"
              stroke="currentColor"
            >
              <path
                strokeLinecap="round"
                strokeLinejoin="round"
                strokeWidth={2}
                d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z"
              />
            </svg>
            <p className="text-sm font-medium text-[var(--color-danger)]">
              Stream unavailable
            </p>
            <p className="text-xs text-muted">
              Camera may be disconnected or stream URL is invalid
            </p>
          </div>
        </div>
      )}

      <img
        src={src}
        alt={alt}
        className={cn(
          'h-full w-full object-contain',
          (isLoading || hasError) && 'invisible'
        )}
        onLoad={handleLoad}
        onError={handleError}
      />
    </div>
  )
}
