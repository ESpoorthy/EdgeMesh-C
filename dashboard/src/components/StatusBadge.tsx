import type { BackendState } from '../api/client.ts';

interface StatusBadgeProps {
  state: BackendState;
}

const STATE_STYLES: Record<BackendState, { color: string; label: string }> = {
  healthy:   { color: 'var(--accent-teal)',  label: 'Healthy'   },
  unhealthy: { color: 'var(--accent-red)',   label: 'Unhealthy' },
  unknown:   { color: 'var(--accent-amber)', label: 'Unknown'   },
};

export function StatusBadge({ state }: StatusBadgeProps) {
  const { color, label } = STATE_STYLES[state];
  return (
    <span
      role="status"
      aria-label={`Backend state: ${label}`}
      style={{
        display: 'inline-block',
        padding: '2px 10px',
        borderRadius: 'var(--radius-sm)',
        backgroundColor: `${color}18`,
        border: `1px solid ${color}40`,
        color,
        fontSize: '11px',
        fontWeight: 600,
        letterSpacing: '0.04em',
        textTransform: 'uppercase',
      }}
    >
      {label}
    </span>
  );
}
