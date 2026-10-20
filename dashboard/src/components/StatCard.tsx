interface StatCardProps {
  label: string;
  value: string | number | null | undefined;
  unit?: string;
  sub?: string;
  accent?: 'default' | 'red' | 'amber' | 'teal';
}

/**
 * Dark stat card matching the Sigma design:
 * small label top-left, large bold value, optional unit, optional subtitle.
 */
export function StatCard({ label, unit, sub, accent = 'default', value }: StatCardProps) {
  const isLoading = value === undefined || value === null;

  const accentColor =
    accent === 'red'   ? 'var(--accent-red)'   :
    accent === 'amber' ? 'var(--accent-amber)'  :
    accent === 'teal'  ? 'var(--accent-teal)'   :
    'var(--text-primary)';

  return (
    <div
      style={{
        background: 'var(--bg-surface)',
        border: '1px solid var(--border)',
        borderRadius: 'var(--radius-lg)',
        padding: '14px 16px',
        minWidth: '130px',
        flex: '1 1 130px',
      }}
    >
      <div
        style={{
          fontSize: '11px',
          color: 'var(--text-secondary)',
          marginBottom: '6px',
          letterSpacing: '0.01em',
        }}
      >
        {label}
      </div>

      {isLoading ? (
        <>
          <span className="skeleton" style={{ width: 80, height: 28, display: 'block', marginBottom: 6 }} />
          <span className="skeleton" style={{ width: 60, height: 10, display: 'block' }} />
        </>
      ) : (
        <>
          <div
            style={{
              fontSize: '26px',
              fontWeight: 700,
              color: accentColor,
              lineHeight: 1.1,
              fontFamily: 'var(--font-mono)',
              letterSpacing: '-0.02em',
              marginBottom: '4px',
            }}
          >
            {typeof value === 'number'
              ? value.toLocaleString()
              : value}
            {unit && (
              <span
                style={{
                  fontSize: '12px',
                  fontWeight: 400,
                  color: 'var(--text-secondary)',
                  marginLeft: '4px',
                  fontFamily: 'var(--font-sans)',
                }}
              >
                {unit}
              </span>
            )}
          </div>
          {sub && (
            <div style={{ fontSize: '11px', color: 'var(--text-muted)', lineHeight: 1.3 }}>
              {sub}
            </div>
          )}
        </>
      )}
    </div>
  );
}
