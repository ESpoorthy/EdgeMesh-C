import type { Backend } from '../api/client.ts';

interface TopologyDiagramProps {
  backends: Backend[];
  policy: string;
  requestsPerSecond: number;
}

function stateColor(state: Backend['state']) {
  if (state === 'healthy')   return 'var(--accent-teal)';
  if (state === 'unhealthy') return 'var(--accent-red)';
  return 'var(--accent-amber)';
}

function stateLabel(state: Backend['state']) {
  if (state === 'healthy')   return 'Healthy';
  if (state === 'unhealthy') return 'Failed';
  return 'Unknown';
}

function policyLabel(p: string) {
  if (p === 'round_robin')       return 'ROUND ROBIN / CURRENT ALLOCATION';
  if (p === 'least_connections') return 'LEAST CONNECTIONS / CURRENT ALLOCATION';
  return p.toUpperCase() + ' / CURRENT ALLOCATION';
}

/**
 * Traffic topology diagram matching the Sigma design:
 * Ingress → EdgeMesh-C → 3 backends with per-backend % labels.
 * Pool status panel on the right.
 */
export function TopologyDiagram({ backends, policy, requestsPerSecond }: TopologyDiagramProps) {
  const healthy = backends.filter(b => b.state === 'healthy').length;
  const total = backends.length;

  const totalReqs = backends.reduce((s, b) => s + b.requests_total, 0) || 1;

  // Pool is degraded if any backend is unhealthy
  const degraded = backends.some(b => b.state === 'unhealthy');

  return (
    <div
      style={{
        background: 'var(--bg-surface)',
        border: '1px solid var(--border)',
        borderRadius: 'var(--radius-lg)',
        padding: '16px 20px',
        marginBottom: '16px',
      }}
    >
      {/* Header */}
      <div
        style={{
          display: 'flex',
          justifyContent: 'space-between',
          alignItems: 'center',
          marginBottom: '16px',
        }}
      >
        <span style={{ fontSize: '13px', fontWeight: 600, color: 'var(--text-primary)' }}>
          Traffic topology
        </span>
        <span style={{ fontSize: '10px', color: 'var(--text-muted)', letterSpacing: '0.06em' }}>
          {policyLabel(policy)}
        </span>
      </div>

      {/* Diagram body */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '0', minHeight: '160px' }}>

        {/* Ingress node */}
        <NodeBox
          title="Ingress"
          sub={`${requestsPerSecond.toFixed(1)} req/s`}
          color="var(--text-secondary)"
          borderColor="var(--border)"
          minWidth={90}
        />

        <Arrow />

        {/* EdgeMesh-C proxy node */}
        <NodeBox
          title="EdgeMesh-C"
          sub1="127.0.0.1:8080"
          sub2="HTTP gateway · live"
          color="var(--accent-teal)"
          borderColor="var(--accent-teal)"
          icon="⬡"
          minWidth={130}
        />

        {/* Fan-out connector */}
        <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-start', gap: '12px', margin: '0 4px', flex: 1, minWidth: 0 }}>
          {backends.map((b, idx) => {
            const pct = total > 0 ? Math.round((b.requests_total / totalReqs) * 100) : 0;
            const lineStyle = b.state === 'unhealthy' ? 'dashed' : 'solid';
            return (
              <div key={b.id} style={{ display: 'flex', alignItems: 'center', width: '100%' }}>
                {/* Connector line */}
                <div
                  style={{
                    flex: '0 0 32px',
                    height: '1px',
                    borderTop: `1px ${lineStyle} ${stateColor(b.state)}`,
                    opacity: b.state === 'unhealthy' ? 0.4 : 0.7,
                  }}
                />
                <span style={{ fontSize: '8px', color: stateColor(b.state), margin: '0 4px', opacity: 0.7 }}>
                  {idx === 0 ? '▶' : b.state === 'unhealthy' ? '⊘' : '▶'}
                </span>
                {/* Backend node */}
                <div
                  style={{
                    flex: 1,
                    background: 'var(--bg-elevated)',
                    border: `1px solid ${stateColor(b.state)}`,
                    borderRadius: 'var(--radius-md)',
                    padding: '6px 10px',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'space-between',
                    gap: '8px',
                    opacity: b.state === 'unhealthy' ? 0.6 : 1,
                    minWidth: 0,
                  }}
                >
                  <div style={{ minWidth: 0 }}>
                    <div
                      style={{
                        fontSize: '11px',
                        fontFamily: 'var(--font-mono)',
                        color: stateColor(b.state),
                        whiteSpace: 'nowrap',
                        overflow: 'hidden',
                        textOverflow: 'ellipsis',
                      }}
                    >
                      {b.address}
                    </div>
                    <div style={{ fontSize: '10px', color: 'var(--text-muted)', marginTop: '1px' }}>
                      {stateLabel(b.state)} · {b.active_connections} active
                    </div>
                  </div>
                  <div
                    style={{
                      fontSize: '13px',
                      fontWeight: 700,
                      fontFamily: 'var(--font-mono)',
                      color: stateColor(b.state),
                      flexShrink: 0,
                    }}
                  >
                    {pct}%
                  </div>
                </div>
              </div>
            );
          })}
        </div>

        {/* Pool status panel */}
        <div
          style={{
            width: '180px',
            flexShrink: 0,
            background: 'var(--bg-elevated)',
            border: `1px solid ${degraded ? 'rgba(210,153,34,0.4)' : 'var(--border)'}`,
            borderRadius: 'var(--radius-lg)',
            padding: '14px',
            marginLeft: '12px',
          }}
        >
          {degraded && (
            <div
              style={{
                display: 'inline-flex',
                alignItems: 'center',
                gap: '5px',
                background: 'rgba(210,153,34,0.12)',
                border: '1px solid rgba(210,153,34,0.3)',
                borderRadius: 'var(--radius-sm)',
                padding: '2px 8px',
                marginBottom: '10px',
                fontSize: '10px',
                color: 'var(--accent-amber)',
              }}
            >
              ⚠ Degraded pool · live
            </div>
          )}
          <div
            style={{
              fontSize: '28px',
              fontWeight: 700,
              fontFamily: 'var(--font-mono)',
              color: degraded ? 'var(--accent-amber)' : 'var(--accent-teal)',
              lineHeight: 1.1,
              marginBottom: '6px',
            }}
          >
            {healthy} of {total} available
          </div>
          <div style={{ fontSize: '11px', color: 'var(--text-muted)', lineHeight: 1.4 }}>
            {degraded
              ? `Failed backend receives 0% of current routed traffic.`
              : 'All backends healthy and serving traffic.'}
          </div>
        </div>
      </div>
    </div>
  );
}

/* ----- Sub-components ----- */

function NodeBox({
  title, sub, sub1, sub2, color, borderColor, icon, minWidth,
}: {
  title: string;
  sub?: string;
  sub1?: string;
  sub2?: string;
  color: string;
  borderColor: string;
  icon?: string;
  minWidth?: number;
}) {
  return (
    <div
      style={{
        background: 'var(--bg-elevated)',
        border: `1px solid ${borderColor}`,
        borderRadius: 'var(--radius-lg)',
        padding: '10px 14px',
        minWidth,
        flexShrink: 0,
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: '6px', marginBottom: '4px' }}>
        {icon && <span style={{ color, fontSize: '14px' }}>{icon}</span>}
        <span style={{ fontSize: '12px', fontWeight: 600, color }}>{title}</span>
      </div>
      {sub  && <div style={{ fontSize: '11px', color: 'var(--text-muted)' }}>{sub}</div>}
      {sub1 && <div style={{ fontSize: '11px', fontFamily: 'var(--font-mono)', color: 'var(--text-secondary)' }}>{sub1}</div>}
      {sub2 && <div style={{ fontSize: '10px', color: 'var(--text-muted)', marginTop: '2px' }}>{sub2}</div>}
    </div>
  );
}

function Arrow() {
  return (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        padding: '0 6px',
        flexShrink: 0,
      }}
    >
      <div style={{ width: 20, height: 1, background: 'var(--border)', position: 'relative' }}>
        <span
          style={{
            position: 'absolute',
            right: -4,
            top: '50%',
            transform: 'translateY(-50%)',
            color: 'var(--border)',
            fontSize: '10px',
            lineHeight: 1,
          }}
        >▶</span>
      </div>
    </div>
  );
}
