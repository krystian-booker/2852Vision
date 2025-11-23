import { Badge } from '@/components/ui/badge'
import { cn } from '@/lib/utils'

export type Status = 'online' | 'offline' | 'error' | 'warning' | 'idle'

interface StatusBadgeProps {
  status: Status
  label?: string
  className?: string
  showDot?: boolean
}

const statusConfig: Record<
  Status,
  { label: string; variant: 'success' | 'destructive' | 'warning' | 'secondary'; dotColor: string }
> = {
  online: {
    label: 'Online',
    variant: 'success',
    dotColor: 'bg-[var(--color-success)]',
  },
  offline: {
    label: 'Offline',
    variant: 'secondary',
    dotColor: 'bg-[var(--color-subtle)]',
  },
  error: {
    label: 'Error',
    variant: 'destructive',
    dotColor: 'bg-[var(--color-danger)]',
  },
  warning: {
    label: 'Warning',
    variant: 'warning',
    dotColor: 'bg-[var(--color-warning)]',
  },
  idle: {
    label: 'Idle',
    variant: 'secondary',
    dotColor: 'bg-[var(--color-muted)]',
  },
}

/**
 * Status badge component for showing connection/operational states
 */
export function StatusBadge({
  status,
  label,
  className,
  showDot = true,
}: StatusBadgeProps) {
  const config = statusConfig[status]
  const displayLabel = label || config.label

  return (
    <Badge variant={config.variant} className={cn('gap-1.5', className)}>
      {showDot && (
        <span
          className={cn('h-2 w-2 rounded-full', config.dotColor)}
          aria-hidden="true"
        />
      )}
      {displayLabel}
    </Badge>
  )
}
