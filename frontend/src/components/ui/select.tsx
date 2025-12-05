import * as React from 'react'
import { Check, ChevronDown } from 'lucide-react'

import { cn } from '@/lib/utils'

type BivariantCallback<T extends (...args: any[]) => any> = {
  bivarianceHack: T
}['bivarianceHack']

interface SelectContextValue {
  value?: string
  disabled?: boolean
  open: boolean
  setOpen: (open: boolean) => void
  onValueChange?: (value: string) => void
}

const SelectContext = React.createContext<SelectContextValue | null>(null)

function useSelectContext(component: string): SelectContextValue {
  const ctx = React.useContext(SelectContext)
  if (!ctx) {
    throw new Error(`${component} must be used within a Select`)
  }
  return ctx
}

interface SelectProps {
  value?: string
  defaultValue?: string
  onValueChange?: BivariantCallback<(value: string) => void>
  disabled?: boolean
  children: React.ReactNode
}

const Select = ({ value: controlledValue, defaultValue, onValueChange, disabled, children }: SelectProps) => {
  const [uncontrolledValue, setUncontrolledValue] = React.useState<string | undefined>(defaultValue)
  const [open, setOpen] = React.useState(false)

  const value = controlledValue !== undefined ? controlledValue : uncontrolledValue

  const handleValueChange = React.useCallback(
    (newValue: string) => {
      if (controlledValue === undefined) {
        setUncontrolledValue(newValue)
      }
      onValueChange?.(newValue)
    },
    [controlledValue, onValueChange]
  )

  const contextValue = React.useMemo(
    () => ({
      value,
      disabled,
      open,
      setOpen,
      onValueChange: handleValueChange,
    }),
    [value, disabled, open, handleValueChange]
  )

  return (
    <SelectContext.Provider value={contextValue}>
      <div className="relative w-full inline-block">{children}</div>
    </SelectContext.Provider>
  )
}

const SelectGroup = ({ children }: { children: React.ReactNode }) => <div className="space-y-1">{children}</div>

interface SelectValueProps extends React.HTMLAttributes<HTMLSpanElement> {
  placeholder?: string
}

const SelectValue = React.forwardRef<HTMLSpanElement, SelectValueProps>(
  ({ className, placeholder, children, ...props }, ref) => {
    const { value } = useSelectContext('SelectValue')

    const display = children ?? (value ?? placeholder ?? '')

    return (
      <span
        ref={ref}
        className={cn('flex-1 text-left truncate [&>span]:line-clamp-1', className)}
        {...props}
      >
        {display}
      </span>
    )
  }
)
SelectValue.displayName = 'SelectValue'

const SelectTrigger = React.forwardRef<HTMLButtonElement, React.ButtonHTMLAttributes<HTMLButtonElement>>(
  ({ className, children, onClick, type, ...props }, ref) => {
    const { open, setOpen, disabled } = useSelectContext('SelectTrigger')

    const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
      if (disabled) {
        event.preventDefault()
        return
      }
      setOpen(!open)
      onClick?.(event)
    }

    return (
      <button
        ref={ref}
        type={type ?? 'button'}
        className={cn(
          'flex h-9 w-full items-center justify-between rounded-[var(--radius-sm)] border border-[var(--input-border)] bg-[var(--input-bg)] px-3 py-2 text-sm transition-all duration-200 placeholder:text-[var(--color-subtle)] focus:outline-none focus:border-[var(--color-teal)] focus:shadow-[0_0_0_3px_rgba(0,194,168,0.3),0_0_20px_rgba(0,194,168,0.2)] disabled:cursor-not-allowed disabled:opacity-50',
          className
        )}
        aria-haspopup="listbox"
        aria-expanded={open}
        disabled={disabled}
        onClick={handleClick}
        {...props}
      >
        {children}
        <ChevronDown className="ml-2 h-4 w-4 opacity-50 shrink-0" />
      </button>
    )
  }
)
SelectTrigger.displayName = 'SelectTrigger'

interface SelectContentProps extends React.HTMLAttributes<HTMLDivElement> {
  position?: 'popper' | 'item-aligned'
}

const SelectContent = React.forwardRef<HTMLDivElement, SelectContentProps>(
  ({ className, children, position: _position, ...props }, ref) => {
    const { open } = useSelectContext('SelectContent')

    if (!open) return null

    return (
      <div
        ref={ref}
        className={cn(
          'absolute z-50 mt-1 max-h-96 min-w-[8rem] w-full overflow-auto rounded-[var(--radius-sm)] border border-[var(--glass-border)] bg-[var(--glass-bg-heavy)] backdrop-blur-xl text-[var(--color-text)] shadow-md',
          className
        )}
        role="listbox"
        {...props}
      >
        {children}
      </div>
    )
  }
)
SelectContent.displayName = 'SelectContent'

const SelectLabel = React.forwardRef<HTMLDivElement, React.HTMLAttributes<HTMLDivElement>>(
  ({ className, ...props }, ref) => (
    <div ref={ref} className={cn('px-2 py-1.5 text-sm font-semibold', className)} {...props} />
  )
)
SelectLabel.displayName = 'SelectLabel'

interface SelectItemProps extends React.HTMLAttributes<HTMLDivElement> {
  value: string
}

const SelectItem = React.forwardRef<HTMLDivElement, SelectItemProps>(
  ({ className, children, value, onClick, ...props }, ref) => {
    const { value: selectedValue, onValueChange, setOpen, disabled } = useSelectContext('SelectItem')

    const isSelected = selectedValue === value

    const handleClick = (event: React.MouseEvent<HTMLDivElement>) => {
      if (disabled) {
        event.preventDefault()
        return
      }
      onValueChange?.(value)
      setOpen(false)
      onClick?.(event)
    }

    return (
      <div
        ref={ref}
        role="option"
        aria-selected={isSelected}
        className={cn(
          'relative flex w-full cursor-default select-none items-center rounded-sm py-1.5 pl-2 pr-8 text-sm outline-none hover:bg-[var(--color-surface-alt)] data-[disabled]:pointer-events-none data-[disabled]:opacity-50',
          className
        )}
        onClick={handleClick}
        {...props}
      >
        <span className="absolute right-2 flex h-3.5 w-3.5 items-center justify-center">
          {isSelected && <Check className="h-4 w-4" />}
        </span>
        <span>{children}</span>
      </div>
    )
  }
)
SelectItem.displayName = 'SelectItem'

const SelectSeparator = React.forwardRef<HTMLDivElement, React.HTMLAttributes<HTMLDivElement>>(
  ({ className, ...props }, ref) => (
    <div ref={ref} className={cn('-mx-1 my-1 h-px bg-[var(--color-border)]', className)} {...props} />
  )
)
SelectSeparator.displayName = 'SelectSeparator'

// Stub scroll buttons for API compatibility (not used in this project)
const SelectScrollUpButton = React.forwardRef<HTMLDivElement, React.HTMLAttributes<HTMLDivElement>>(
  ({ className, ...props }, ref) => (
    <div ref={ref} className={cn('hidden', className)} {...props} />
  )
)
SelectScrollUpButton.displayName = 'SelectScrollUpButton'

const SelectScrollDownButton = React.forwardRef<HTMLDivElement, React.HTMLAttributes<HTMLDivElement>>(
  ({ className, ...props }, ref) => (
    <div ref={ref} className={cn('hidden', className)} {...props} />
  )
)
SelectScrollDownButton.displayName = 'SelectScrollDownButton'

export {
  Select,
  SelectGroup,
  SelectValue,
  SelectTrigger,
  SelectContent,
  SelectLabel,
  SelectItem,
  SelectSeparator,
  SelectScrollUpButton,
  SelectScrollDownButton,
}
