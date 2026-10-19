interface ErrorBannerProps {
  error: Error | null;
  onRetry?: () => void;
}

/**
 * Displays a disconnected/error state banner with an optional retry button.
 * Renders nothing when error is null.
 */
export function ErrorBanner({ error, onRetry }: ErrorBannerProps) {
  if (!error) return null;

  return (
    <div
      role="alert"
      aria-live="assertive"
      style={{
        display: 'flex',
        alignItems: 'center',
        gap: '12px',
        padding: '12px 16px',
        borderRadius: '8px',
        background: '#fef2f2',
        border: '1px solid #fecaca',
        color: '#991b1b',
        marginBottom: '16px',
      }}
    >
      <span style={{ flex: 1 }}>
        <strong>Disconnected</strong> — could not reach the EdgeMesh-C API.{' '}
        <span style={{ fontSize: '0.85em', opacity: 0.85 }}>{error.message}</span>
      </span>
      {onRetry && (
        <button
          onClick={onRetry}
          aria-label="Retry connection"
          style={{
            padding: '6px 14px',
            borderRadius: '6px',
            border: '1px solid #fca5a5',
            background: '#fee2e2',
            color: '#7f1d1d',
            cursor: 'pointer',
            fontWeight: 600,
            fontSize: '0.875rem',
          }}
        >
          Retry
        </button>
      )}
    </div>
  );
}
