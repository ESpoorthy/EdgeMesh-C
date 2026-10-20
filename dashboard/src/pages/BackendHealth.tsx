import { getBackends } from '../api/client.ts';
import { usePolling } from '../hooks/usePolling.ts';
import type { Backend } from '../api/client.ts';

function formatTimestamp(ts: string | null): string {
  if (!ts) return '—';
  try { return new Date(ts).toLocaleTimeString(); } catch { return ts; }
}

function stateColor(state: Backend['state']) {
  if (state === 'healthy')   return 'var(--accent-teal)';
  if (state === 'unhealthy') return 'var(--accent-red)';
  return 'var(--accent-amber)';
}

function stateLabel(state: Backend['state']) {
  if (state === 'healthy')   return 'Healthy';
  if (state === 'unhealthy') return 'Unhealthy';
  return 'Unknown';
}

export function BackendHealth() {
  const { data, loading, error } = usePolling(getBackends, 2000);

  return (
    <div>
      {/* Page header */}
      <div style={{ marginBottom: '20px' }}>
        <h2 style={{ fontSize: '22px', fontWeight: 700, color: 'var(--text-primary)', marginBottom: '4px' }}>
          Backend Health
        </h2>
        <p style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>
          Real-time state of all configured backends. Probed every 5 s.
        </p>
      </div>

      {/* Error state */}
      {error && (
        <div
          role="alert"
          style={{
            padding: '10px 14px',
            borderRadius: 'var(--radius-md)',
            background: 'rgba(248,81,73,0.08)',
            border: '1px solid rgba(248,81,73,0.3)',
            color: 'var(--accent-red)',
            fontSize: '12px',
            marginBottom: '16px',
          }}
        >
          ✗ Could not reach admin API — {error.message}
        </div>
      )}

      {/* Loading skeleton */}
      {loading && !data && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          {[1, 2, 3].map(i => (
            <div
              key={i}
              className="skeleton"
              style={{
                height: 88,
                borderRadius: 'var(--radius-lg)',
                width: '100%',
              }}
            />
          ))}
        </div>
      )}

      {/* Backend cards */}
      {data && data.backends.length > 0 && (
        <div role="list" aria-label="Configured backends" style={{ display: 'flex', flexDirection: 'column', gap: '10px' }}>
          {data.backends.map(b => (
            <BackendCard key={b.id} backend={b} />
          ))}
        </div>
      )}

      {data && data.backends.length === 0 && (
        <div style={{ color: 'var(--text-muted)', padding: '32px', textAlign: 'center', fontSize: '13px' }}>
          No backends configured.
        </div>
      )}

      {data && (
        <div style={{ marginTop: '12px', fontSize: '11px', color: 'var(--text-muted)' }}>
          Generated at: {data.generated_at}
        </div>
      )}
    </div>
  );
}

function BackendCard({ backend: b }: { backend: Backend }) {
  const color = stateColor(b.state);
  const totalReqs = Math.max(b.requests_total, 1);
  const fwdPct = ((b.responses_forwarded_total / totalReqs) * 100).toFixed(1);

  return (
    <div
      role="listitem"
      style={{
        background: 'var(--bg-surface)',
        border: `1px solid var(--border)`,
        borderLeft: `3px solid ${color}`,
        borderRadius: 'var(--radius-lg)',
        padding: '14px 18px',
        display: 'grid',
        gridTemplateColumns: '1fr auto',
        gap: '12px',
        alignItems: 'center',
      }}
    >
      {/* Left: identity + metrics */}
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: '20px', alignItems: 'center' }}>
        {/* Identity */}
        <div style={{ minWidth: 160 }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '4px' }}>
            <span
              style={{
                display: 'inline-block',
                width: 8,
                height: 8,
                borderRadius: '50%',
                background: color,
                flexShrink: 0,
              }}
            />
            <span style={{ fontWeight: 600, fontSize: '13px', color: 'var(--text-primary)' }}>
              {b.id}
            </span>
          </div>
          <div style={{ fontSize: '11px', fontFamily: 'var(--font-mono)', color: 'var(--text-secondary)', paddingLeft: 16 }}>
            {b.address}
          </div>
        </div>

        {/* State badge */}
        <div>
          <div style={{ fontSize: '10px', color: 'var(--text-muted)', marginBottom: '3px' }}>State</div>
          <span
            style={{
              padding: '2px 10px',
              borderRadius: 'var(--radius-sm)',
              background: `${color}18`,
              border: `1px solid ${color}40`,
              fontSize: '11px',
              fontWeight: 600,
              color,
              letterSpacing: '0.04em',
            }}
          >
            {stateLabel(b.state)}
          </span>
        </div>

        {/* Metrics */}
        <MetricPill label="Active" value={String(b.active_connections)} />
        <MetricPill label="Requests" value={b.requests_total.toLocaleString()} />
        <MetricPill label="Forwarded" value={`${b.responses_forwarded_total.toLocaleString()} (${fwdPct}%)`} />
        <MetricPill label="Failures" value={String(b.proxy_failures_total)} accent={b.proxy_failures_total > 0} />
        <MetricPill label="Last check" value={formatTimestamp(b.last_check_at)} />
        <MetricPill
          label="Probe latency"
          value={b.last_response_ms !== null ? `${b.last_response_ms} ms` : '—'}
        />
      </div>

      {/* Right: large state indicator */}
      <div
        style={{
          textAlign: 'right',
          flexShrink: 0,
        }}
      >
        <div
          style={{
            fontSize: '11px',
            fontWeight: 700,
            color,
            letterSpacing: '0.06em',
            textTransform: 'uppercase',
          }}
        >
          {stateLabel(b.state)}
        </div>
      </div>
    </div>
  );
}

function MetricPill({ label, value, accent }: { label: string; value: string; accent?: boolean }) {
  return (
    <div>
      <div style={{ fontSize: '10px', color: 'var(--text-muted)', marginBottom: '2px' }}>{label}</div>
      <div
        style={{
          fontSize: '12px',
          fontFamily: 'var(--font-mono)',
          color: accent ? 'var(--accent-red)' : 'var(--text-primary)',
          fontWeight: accent ? 600 : 400,
        }}
      >
        {value}
      </div>
    </div>
  );
}
