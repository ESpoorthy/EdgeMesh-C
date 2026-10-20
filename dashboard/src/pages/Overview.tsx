import { useState } from 'react';
import { getOverview, getMetrics, getBackends, putPolicy } from '../api/client.ts';
import { usePolling } from '../hooks/usePolling.ts';
import { StatCard } from '../components/StatCard.tsx';
import { TopologyDiagram } from '../components/TopologyDiagram.tsx';
import { Sparkline } from '../components/Sparkline.tsx';
import { ErrorBanner } from '../components/ErrorBanner.tsx';

const POLICY_OPTIONS = ['round_robin', 'least_connections'] as const;

function formatPolicy(p: string) {
  return p === 'round_robin' ? 'Round Robin' : p === 'least_connections' ? 'Least Connections' : p;
}

export function Overview() {
  const { data: overview, error: overviewError } = usePolling(getOverview, 2000);
  const { data: metrics }                        = usePolling(getMetrics,  2000);
  const { data: backends }                       = usePolling(getBackends, 2000);

  const [selectedPolicy, setSelectedPolicy] = useState<string>('round_robin');
  const [policyStatus,   setPolicyStatus]   = useState<string | null>(null);
  const [policyError,    setPolicyError]    = useState<string | null>(null);
  const [submitting,     setSubmitting]     = useState(false);

  async function handlePolicySubmit(e: React.FormEvent) {
    e.preventDefault();
    setSubmitting(true);
    setPolicyStatus(null);
    setPolicyError(null);
    try {
      await putPolicy(selectedPolicy);
      setPolicyStatus(`Policy updated to "${formatPolicy(selectedPolicy)}"`);
    } catch (err) {
      setPolicyError(err instanceof Error ? err.message : 'Failed to update policy.');
    } finally {
      setSubmitting(false);
    }
  }

  // Sparkline data from time-series
  const rpsPoints   = metrics?.points.map(p => p.requests_per_second)    ?? [];
  const p95Points   = metrics?.points.map(p => p.latency_p95_ms ?? 0)    ?? [];

  const currentRps  = overview?.metrics.requests_per_second ?? 0;
  const currentP95  = overview?.metrics.latency_p95_ms ?? null;
  const healthy     = overview?.backends.healthy ?? 0;
  const totalB      = overview?.backends.total   ?? 3;

  return (
    <div>
      {/* Page title */}
      <div style={{ marginBottom: '20px' }}>
        <h2 style={{ fontSize: '22px', fontWeight: 700, color: 'var(--text-primary)', marginBottom: '4px' }}>
          Overview
        </h2>
        <p style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>
          A precise view of the proxy, its paths, and the work in motion.
        </p>

        {/* CONSOLE breadcrumb */}
        <div style={{ float: 'right', marginTop: '-38px', fontSize: '10px', color: 'var(--text-muted)', letterSpacing: '0.06em' }}>
          CONSOLE / 01
        </div>
      </div>

      {/* Error banner when disconnected */}
      <ErrorBanner error={overviewError} />

      {/* Live indicator banner */}
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: '10px',
          padding: '8px 14px',
          borderRadius: 'var(--radius-md)',
          background: 'rgba(61,214,140,0.07)',
          border: '1px solid rgba(61,214,140,0.2)',
          marginBottom: '20px',
          fontSize: '12px',
          color: 'var(--accent-teal)',
        }}
      >
        <span>⬡</span>
        <strong>LIVE DATA</strong>
        <span style={{ color: 'var(--text-secondary)' }}>
          Polling 127.0.0.1:9090 every 2 s. All metrics from the C admin API.
        </span>
      </div>

      {/* ── Stat cards row ─────────────────────────────────────── */}
      <div
        style={{
          display: 'flex',
          gap: '10px',
          flexWrap: 'wrap',
          marginBottom: '16px',
        }}
      >
        <StatCard
          label="Requests / second"
          value={currentRps.toFixed(1)}
          unit="req/s"
          sub="Current live rate"
        />
        <StatCard
          label="Total requests"
          value={overview?.metrics.requests_total ?? null}
          sub="Since proxy start"
        />
        <StatCard
          label="Forwarded responses"
          value={overview?.metrics.responses_forwarded_total ?? null}
          sub={
            overview
              ? `${((overview.metrics.responses_forwarded_total / Math.max(overview.metrics.requests_total, 1)) * 100).toFixed(2)}% of requests`
              : undefined
          }
        />
        <StatCard
          label="Proxy failures"
          value={overview?.metrics.proxy_failures_total ?? null}
          sub={
            overview
              ? `${((overview.metrics.proxy_failures_total / Math.max(overview.metrics.requests_total, 1)) * 100).toFixed(2)}% of requests`
              : undefined
          }
          accent={overview && overview.metrics.proxy_failures_total > 0 ? 'red' : 'default'}
        />
        <StatCard
          label="p95 latency"
          value={currentP95 !== null ? currentP95.toFixed(1) : null}
          unit="ms"
          sub={currentP95 !== null ? 'Rolling sample window' : 'No samples yet'}
        />
        <StatCard
          label="Available backends"
          value={overview ? `${healthy} / ${totalB}` : null}
          sub={healthy === totalB ? 'All healthy' : `${totalB - healthy} degraded`}
          accent={healthy < totalB ? 'amber' : 'teal'}
        />
      </div>

      {/* ── Traffic topology ───────────────────────────────────── */}
      {backends && overview ? (
        <TopologyDiagram
          backends={backends.backends}
          policy={overview.policy}
          requestsPerSecond={currentRps}
        />
      ) : (
        <div
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border)',
            borderRadius: 'var(--radius-lg)',
            height: 200,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            marginBottom: '16px',
            color: 'var(--text-muted)',
          }}
        >
          Loading topology…
        </div>
      )}

      {/* ── Sparkline charts ───────────────────────────────────── */}
      <div style={{ display: 'flex', gap: '12px', flexWrap: 'wrap', marginBottom: '24px' }}>
        <Sparkline
          label="Request traffic"
          subLabel="60-second rolling window — live"
          points={rpsPoints}
          currentValue={`${currentRps.toFixed(1)} req/s`}
          color="var(--accent-teal)"
        />
        <Sparkline
          label="p95 latency"
          subLabel="60-second rolling window — live"
          points={p95Points}
          currentValue={currentP95 !== null ? `${currentP95.toFixed(1)} ms` : 'N/A'}
          color="var(--accent-amber)"
        />
      </div>

      {/* ── Policy control ─────────────────────────────────────── */}
      <div
        style={{
          background: 'var(--bg-surface)',
          border: '1px solid var(--border)',
          borderRadius: 'var(--radius-lg)',
          padding: '16px 20px',
        }}
      >
        <div style={{ fontSize: '13px', fontWeight: 600, marginBottom: '12px', color: 'var(--text-primary)' }}>
          Load balancing policy
        </div>
        <form
          onSubmit={(e) => { void handlePolicySubmit(e); }}
          style={{ display: 'flex', alignItems: 'center', gap: '10px', flexWrap: 'wrap' }}
        >
          <label htmlFor="policy-select" style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>
            Active policy
          </label>
          <select
            id="policy-select"
            value={selectedPolicy}
            onChange={e => setSelectedPolicy(e.target.value)}
            style={{
              padding: '5px 10px',
              borderRadius: 'var(--radius-md)',
              border: '1px solid var(--border)',
              background: 'var(--bg-elevated)',
              color: 'var(--text-primary)',
              fontSize: '12px',
              outline: 'none',
            }}
          >
            {POLICY_OPTIONS.map(p => (
              <option key={p} value={p}>{formatPolicy(p)}</option>
            ))}
          </select>
          <button
            type="submit"
            disabled={submitting}
            style={{
              padding: '5px 16px',
              borderRadius: 'var(--radius-md)',
              border: '1px solid rgba(61,214,140,0.3)',
              background: 'rgba(61,214,140,0.1)',
              color: 'var(--accent-teal)',
              fontWeight: 600,
              fontSize: '12px',
              cursor: submitting ? 'not-allowed' : 'pointer',
              opacity: submitting ? 0.6 : 1,
            }}
          >
            {submitting ? 'Updating…' : 'Apply'}
          </button>
          {policyStatus && (
            <span style={{ fontSize: '12px', color: 'var(--accent-teal)' }} role="status">
              ✓ {policyStatus}
            </span>
          )}
          {policyError && (
            <span style={{ fontSize: '12px', color: 'var(--accent-red)' }} role="alert">
              ✗ {policyError}
            </span>
          )}
        </form>
        {overview && (
          <div style={{ marginTop: '8px', fontSize: '11px', color: 'var(--text-muted)' }}>
            Current active policy: <span style={{ color: 'var(--accent-teal)' }}>{formatPolicy(overview.policy)}</span>
          </div>
        )}
      </div>
    </div>
  );
}
