import * as React from "react"
import { cva, type VariantProps } from "class-variance-authority"

import { cn } from "@/lib/utils"

const badgeVariants = cva(
  "inline-flex items-center rounded-[var(--radius-sm)] border px-2.5 py-0.5 text-xs font-semibold uppercase tracking-wide transition-colors focus:outline-none focus:ring-2 focus:ring-ring focus:ring-offset-2",
  {
    variants: {
      variant: {
        default:
          "border-[var(--color-primary)] bg-[var(--color-primary)]/12 text-[var(--color-primary)]",
        secondary:
          "border-[var(--color-border-strong)] bg-transparent text-[var(--color-text)]",
        destructive:
          "border-[var(--color-danger)] bg-[var(--color-danger)]/12 text-[var(--color-danger)]",
        success:
          "border-[var(--color-success)] bg-[var(--color-success)]/12 text-[var(--color-success)]",
        warning:
          "border-[var(--color-warning)] bg-[var(--color-warning)]/12 text-[var(--color-warning)]",
        outline:
          "border-[var(--color-border-strong)] bg-transparent text-[var(--color-text)]",
      },
    },
    defaultVariants: {
      variant: "default",
    },
  }
)

export interface BadgeProps
  extends React.HTMLAttributes<HTMLDivElement>,
    VariantProps<typeof badgeVariants> {}

function Badge({ className, variant, ...props }: BadgeProps) {
  return (
    <div className={cn(badgeVariants({ variant }), className)} {...props} />
  )
}

export { Badge, badgeVariants }
