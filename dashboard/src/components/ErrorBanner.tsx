interface ErrorBannerProps {
  error: Error | null;
  onRetry?: () => void;
}

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
        padding: '10px 14px',
        borderRadius: 'var(--radius-md)',
        background: 'rgba(248,81,73,0.08)',
        border: '1px solid rgba(248,81,73,0.25)',
        color: 'var(--accent-red)',
        marginBottom: '16px',
        fontSize: '12px',
      }}
    >
      <span style={{ flex: 1 }}>
        <strong>Disconnected</strong> — could not reach the EdgeMesh-C API.{' '}
        <span style={{ opacity: 0.8 }}>{error.message}</span>
      </span>
      {onRetry && (
        <button
          onClick={onRetry}
          aria-label="Retry connection"
          style={{
            padding: '4px 12px',
            borderRadius: 'var(--radius-sm)',
            border: '1px solid rgba(248,81,73,0.3)',
            background: 'rgba(248,81,73,0.1)',
            color: 'var(--accent-red)',
            cursor: 'pointer',
            fontSize: '11px',
            fontWeight: 600,
          }}
        >
          Retry
        </button>
      )}
    </div>
  );
}
