interface MetricCardProps {
  label: string;
  value: number | string | null | undefined;
  unit?: string;
}

const skeletonStyle: React.CSSProperties = {
  display: 'inline-block',
  width: '60px',
  height: '1em',
  borderRadius: '4px',
  background: 'linear-gradient(90deg, #e5e7eb 25%, #f3f4f6 50%, #e5e7eb 75%)',
  backgroundSize: '200% 100%',
  animation: 'shimmer 1.4s infinite',
  verticalAlign: 'middle',
};

/**
 * Displays a single metric: label + value + optional unit.
 * Renders a loading skeleton when value is undefined or null.
 */
export function MetricCard({ label, value, unit }: MetricCardProps) {
  const isLoading = value === undefined || value === null;
  const displayValue = isLoading ? null : typeof value === 'number' ? value : value;

  return (
    <div
      aria-label={`${label}${unit ? ` in ${unit}` : ''}`}
      style={{
        padding: '12px 16px',
        borderRadius: '8px',
        background: '#fff',
        boxShadow: '0 1px 3px rgba(0,0,0,0.1)',
        minWidth: '140px',
      }}
    >
      <div style={{ fontSize: '0.75rem', color: '#6b7280', marginBottom: '4px' }}>
        {label}
      </div>
      <div style={{ fontSize: '1.25rem', fontWeight: 700, color: '#111827' }}>
        {isLoading ? (
          <span style={skeletonStyle} aria-busy="true" aria-label="Loading" />
        ) : (
          <>
            {displayValue}
            {unit && (
              <span style={{ fontSize: '0.75rem', fontWeight: 400, color: '#6b7280', marginLeft: '4px' }}>
                {unit}
              </span>
            )}
          </>
        )}
      </div>
    </div>
  );
}
