export default function LoadingSpinner() {
  return (
    <div className="flex items-center justify-center min-h-screen">
      <div className="flex flex-col items-center gap-4">
        <div className="w-12 h-12 border-4 border-[var(--color-teal)] border-t-transparent rounded-full animate-spin shadow-[0_0_20px_rgba(0,194,168,0.3)]" />
        <p className="text-sm text-muted uppercase tracking-wide">Loading...</p>
      </div>
    </div>
  )
}
