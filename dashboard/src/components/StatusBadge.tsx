import type { BackendState } from '../api/client.ts';

interface StatusBadgeProps {
  state: BackendState;
}

const STATE_STYLES: Record<BackendState, { bg: string; label: string }> = {
  healthy: { bg: '#22c55e', label: 'Healthy' },
  unhealthy: { bg: '#ef4444', label: 'Unhealthy' },
  unknown: { bg: '#9ca3af', label: 'Unknown' },
};

/**
 * Inline badge that displays backend state.
 * healthy → green, unhealthy → red, unknown → gray.
 * Accessible: role="status" + aria-label.
 */
export function StatusBadge({ state }: StatusBadgeProps) {
  const { bg, label } = STATE_STYLES[state];
  return (
    <span
      role="status"
      aria-label={`Backend state: ${label}`}
      style={{
        display: 'inline-block',
        padding: '2px 10px',
        borderRadius: '12px',
        backgroundColor: bg,
        color: '#fff',
        fontSize: '0.75rem',
        fontWeight: 600,
        letterSpacing: '0.02em',
        textTransform: 'uppercase',
      }}
    >
      {label}
    </span>
  );
}
