import * as React from "react"
import { cva, type VariantProps } from "class-variance-authority"

import { cn } from "@/lib/utils"

const buttonVariants = cva(
  "inline-flex items-center justify-center gap-2 whitespace-nowrap text-sm font-medium transition-all duration-200 focus-visible:outline-none disabled:pointer-events-none disabled:opacity-50 [&_svg]:pointer-events-none [&_svg]:size-4 [&_svg]:shrink-0",
  {
    variants: {
      variant: {
        default:
          "bg-[image:var(--gradient-red)] text-white shadow-[0_0_20px_rgba(212,46,18,0.4)] hover:bg-[image:var(--gradient-red-hover)] hover:shadow-[0_0_30px_rgba(212,46,18,0.6)] border-0 rounded-[var(--radius-sm)]",
        destructive:
          "bg-[var(--color-danger)] text-white shadow-sm hover:bg-[var(--color-danger)]/90 rounded-[var(--radius-sm)]",
        outline:
          "border border-[var(--color-border-strong)] bg-transparent shadow-sm hover:bg-[var(--color-surface-alt)] hover:text-[var(--color-text)] rounded-[var(--radius-sm)]",
        secondary:
          "bg-[var(--color-surface-alt)] text-[var(--color-text)] shadow-sm hover:bg-[var(--color-panel)] rounded-[var(--radius-sm)]",
        ghost:
          "bg-transparent border border-[var(--ghost-border)] text-[var(--color-text)] hover:bg-[var(--ghost-hover-bg)] hover:border-[var(--ghost-hover-border)] hover:text-[var(--color-teal)] hover:shadow-[0_0_12px_rgba(0,194,168,0.15)] rounded-[var(--radius-sm)]",
        link:
          "text-[var(--color-primary)] underline-offset-4 hover:underline",
      },
      size: {
        default: "h-9 px-4 py-2",
        sm: "h-8 px-3 text-xs",
        lg: "h-10 px-8",
        icon: "h-9 w-9",
      },
    },
    defaultVariants: {
      variant: "default",
      size: "default",
    },
  }
)

export interface ButtonProps
  extends React.ButtonHTMLAttributes<HTMLButtonElement>,
    VariantProps<typeof buttonVariants> {}

const Button = React.forwardRef<HTMLButtonElement, ButtonProps>(
  ({ className, variant, size, ...props }, ref) => {
    return (
      <button
        className={cn(buttonVariants({ variant, size, className }))}
        ref={ref}
        {...props}
      />
    )
  }
)
Button.displayName = "Button"

export { Button, buttonVariants }
