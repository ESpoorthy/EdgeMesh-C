interface SparklineProps {
  points: number[];          // raw values
  color?: string;            // line colour, default teal
  height?: number;
  label?: string;            // top-left label
  currentValue?: string;     // top-right current value
  subLabel?: string;         // below the chart title
}

/**
 * SVG sparkline chart — matches the dark "Request traffic" and
 * "p95 latency" charts from the Sigma design.
 * No external charting library needed.
 */
export function Sparkline({
  points,
  color = 'var(--accent-teal)',
  height = 90,
  label,
  currentValue,
  subLabel,
}: SparklineProps) {
  const W = 100; // percentage-based viewBox width
  const H = height;
  const PAD = 4;

  const hasData = points.length >= 2;

  // Normalise points to SVG coordinates
  const min = hasData ? Math.min(...points) : 0;
  const max = hasData ? Math.max(...points) : 1;
  const range = max - min || 1;

  function toSVG(i: number, v: number): [number, number] {
    const x = PAD + ((i / (points.length - 1)) * (W - PAD * 2));
    const y = H - PAD - ((v - min) / range) * (H - PAD * 2);
    return [x, y];
  }

  const pathD = hasData
    ? points
        .map((v, i) => {
          const [x, y] = toSVG(i, v);
          return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`;
        })
        .join(' ')
    : '';

  // Area fill path (close back along the bottom)
  const areaD = hasData
    ? [
        ...points.map((v, i) => {
          const [x, y] = toSVG(i, v);
          return `${i === 0 ? 'M' : 'L'}${x.toFixed(2)},${y.toFixed(2)}`;
        }),
        `L${(PAD + (W - PAD * 2)).toFixed(2)},${(H - PAD).toFixed(2)}`,
        `L${PAD},${(H - PAD).toFixed(2)}`,
        'Z',
      ].join(' ')
    : '';

  const gradId = `spark-${label?.replace(/\s/g, '') ?? Math.random().toString(36).slice(2)}`;

  return (
    <div
      style={{
        background: 'var(--bg-surface)',
        border: '1px solid var(--border)',
        borderRadius: 'var(--radius-lg)',
        padding: '14px 16px 10px',
        flex: '1 1 300px',
        minWidth: 0,
      }}
    >
      {/* Header */}
      <div
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'flex-start',
          marginBottom: '4px',
        }}
      >
        <div>
          <div style={{ fontSize: '13px', fontWeight: 600, color: 'var(--text-primary)' }}>
            {label}
          </div>
          {subLabel && (
            <div style={{ fontSize: '10px', color: 'var(--text-muted)', marginTop: '2px' }}>
              {subLabel}
            </div>
          )}
        </div>
        {currentValue && (
          <div
            style={{
              fontSize: '13px',
              fontWeight: 600,
              color: color,
              fontFamily: 'var(--font-mono)',
            }}
          >
            {currentValue}
          </div>
        )}
      </div>

      {/* SVG chart */}
      <div style={{ marginTop: '8px' }}>
        {!hasData ? (
          <div
            style={{
              height,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              color: 'var(--text-muted)',
              fontSize: '11px',
            }}
          >
            No data yet
          </div>
        ) : (
          <svg
            viewBox={`0 0 ${W} ${H}`}
            preserveAspectRatio="none"
            style={{ width: '100%', height, display: 'block' }}
            aria-label={label}
            role="img"
          >
            <defs>
              <linearGradient id={gradId} x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stopColor={color} stopOpacity="0.18" />
                <stop offset="100%" stopColor={color} stopOpacity="0" />
              </linearGradient>
            </defs>

            {/* Area fill */}
            <path d={areaD} fill={`url(#${gradId})`} />

            {/* Line */}
            <path
              d={pathD}
              fill="none"
              stroke={color}
              strokeWidth="1.5"
              strokeLinejoin="round"
              strokeLinecap="round"
            />
          </svg>
        )}
      </div>

      {/* X-axis tick labels */}
      {hasData && (
        <div
          style={{
            display: 'flex',
            justifyContent: 'space-between',
            marginTop: '4px',
            fontSize: '10px',
            color: 'var(--text-muted)',
          }}
        >
          <span>0 s</span>
          <span>{Math.round(points.length / 2)} s</span>
          <span>{points.length} s</span>
        </div>
      )}
    </div>
  );
}
