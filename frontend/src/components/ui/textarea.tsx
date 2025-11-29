import * as React from "react"

import { cn } from "@/lib/utils"

export interface TextareaProps
    extends React.TextareaHTMLAttributes<HTMLTextAreaElement> { }

const Textarea = React.forwardRef<HTMLTextAreaElement, TextareaProps>(
    ({ className, ...props }, ref) => {
        return (
            <textarea
                className={cn(
                    "flex min-h-[60px] w-full rounded-[var(--radius-sm)] border border-[var(--input-border)] bg-[var(--input-bg)] px-3 py-2 text-sm transition-all duration-200 placeholder:text-[var(--color-subtle)] focus-visible:outline-none focus-visible:border-[var(--color-teal)] focus-visible:shadow-[0_0_0_3px_rgba(0,194,168,0.3),0_0_20px_rgba(0,194,168,0.2)] disabled:cursor-not-allowed disabled:opacity-50",
                    className
                )}
                ref={ref}
                {...props}
            />
        )
    }
)
Textarea.displayName = "Textarea"

export { Textarea }
