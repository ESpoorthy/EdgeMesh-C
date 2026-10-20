interface MetricCardProps {
  label: string;
  value: number | string | null | undefined;
  unit?: string;
}

/**
 * Legacy MetricCard — kept for any remaining imports.
 * New pages use StatCard instead.
 */
export function MetricCard({ label, value, unit }: MetricCardProps) {
  const isLoading = value === undefined || value === null;

  return (
    <div
      aria-label={`${label}${unit ? ` in ${unit}` : ''}`}
      style={{
        padding: '12px 16px',
        borderRadius: 'var(--radius-lg)',
        background: 'var(--bg-surface)',
        border: '1px solid var(--border)',
        minWidth: '130px',
      }}
    >
      <div style={{ fontSize: '11px', color: 'var(--text-secondary)', marginBottom: '4px' }}>
        {label}
      </div>
      <div style={{ fontSize: '1.2rem', fontWeight: 700, color: 'var(--text-primary)', fontFamily: 'var(--font-mono)' }}>
        {isLoading ? (
          <span
            className="skeleton"
            style={{ display: 'inline-block', width: 60, height: '1em', verticalAlign: 'middle' }}
            aria-busy="true"
            aria-label="Loading"
          />
        ) : (
          <>
            {value}
            {unit && (
              <span style={{ fontSize: '11px', fontWeight: 400, color: 'var(--text-secondary)', marginLeft: '4px' }}>
                {unit}
              </span>
            )}
          </>
        )}
      </div>
    </div>
  );
}
