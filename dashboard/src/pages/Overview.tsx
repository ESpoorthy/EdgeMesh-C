import { useState } from 'react';
import { getOverview, putPolicy } from '../api/client.ts';
import { usePolling } from '../hooks/usePolling.ts';
import { ErrorBanner } from '../components/ErrorBanner.tsx';
import { MetricCard } from '../components/MetricCard.tsx';

const POLICY_OPTIONS = ['round_robin', 'least_connections'] as const;

function formatPolicy(p: string): string {
  return p === 'round_robin' ? 'Round Robin' : p === 'least_connections' ? 'Least Connections' : p;
}

function formatLatency(v: number | null): string {
  return v === null ? 'N/A' : String(v);
}

/**
 * Overview page — polls GET /api/v1/overview every 2 s.
 */
export function Overview() {
  const { data, loading, error } = usePolling(getOverview, 2000);

  const [selectedPolicy, setSelectedPolicy] = useState<string>('round_robin');
  const [policyStatus, setPolicyStatus] = useState<string | null>(null);
  const [policyError, setPolicyError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  async function handlePolicySubmit(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault();
    setSubmitting(true);
    setPolicyStatus(null);
    setPolicyError(null);
    try {
      await putPolicy(selectedPolicy);
      setPolicyStatus(`Policy updated to "${formatPolicy(selectedPolicy)}".`);
    } catch (err) {
      setPolicyError(err instanceof Error ? err.message : 'Failed to update policy.');
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <section aria-labelledby="overview-heading">
      <h2 id="overview-heading">Overview</h2>

      <ErrorBanner error={error} />

      {/* Proxy status */}
      <div style={{ marginBottom: '24px' }}>
        <h3>Proxy Status</h3>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: '12px' }}>
          <MetricCard
            label="Status"
            value={loading && !data ? undefined : data?.proxy_running ? 'Running' : 'Stopped'}
          />
          <MetricCard
            label="Policy"
            value={loading && !data ? undefined : formatPolicy(data?.policy ?? '')}
          />
          <MetricCard
            label="Uptime"
            value={loading && !data ? undefined : data?.uptime_seconds ?? null}
            unit="s"
          />
        </div>
      </div>

      {/* Backend summary */}
      <div style={{ marginBottom: '24px' }}>
        <h3>Backends</h3>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: '12px' }}>
          <MetricCard label="Total" value={loading && !data ? undefined : data?.backends.total ?? null} />
          <MetricCard label="Healthy" value={loading && !data ? undefined : data?.backends.healthy ?? null} />
          <MetricCard label="Unhealthy" value={loading && !data ? undefined : data?.backends.unhealthy ?? null} />
          <MetricCard label="Unknown" value={loading && !data ? undefined : data?.backends.unknown ?? null} />
        </div>
      </div>

      {/* Metrics */}
      <div style={{ marginBottom: '24px' }}>
        <h3>Metrics</h3>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: '12px' }}>
          <MetricCard label="Requests Total" value={loading && !data ? undefined : data?.metrics.requests_total ?? null} />
          <MetricCard label="Forwarded" value={loading && !data ? undefined : data?.metrics.responses_forwarded_total ?? null} />
          <MetricCard label="Failures" value={loading && !data ? undefined : data?.metrics.proxy_failures_total ?? null} />
          <MetricCard label="Req/s" value={loading && !data ? undefined : data?.metrics.requests_per_second ?? null} />
          <MetricCard label="Bytes/s" value={loading && !data ? undefined : data?.metrics.response_bytes_per_second ?? null} unit="B/s" />
          <MetricCard label="Active Requests" value={loading && !data ? undefined : data?.metrics.active_requests ?? null} />
          <MetricCard label="Latency p50" value={loading && !data ? undefined : formatLatency(data?.metrics.latency_p50_ms ?? null)} unit="ms" />
          <MetricCard label="Latency p95" value={loading && !data ? undefined : formatLatency(data?.metrics.latency_p95_ms ?? null)} unit="ms" />
          <MetricCard label="Latency p99" value={loading && !data ? undefined : formatLatency(data?.metrics.latency_p99_ms ?? null)} unit="ms" />
        </div>
      </div>

      {/* Policy change control */}
      <div style={{ marginBottom: '24px' }}>
        <h3>Change Policy</h3>
        <form onSubmit={(e) => { void handlePolicySubmit(e); }} style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
          <label htmlFor="policy-select" style={{ fontWeight: 600 }}>Policy</label>
          <select
            id="policy-select"
            value={selectedPolicy}
            onChange={e => setSelectedPolicy(e.target.value)}
            style={{ padding: '6px 10px', borderRadius: '6px', border: '1px solid #d1d5db' }}
          >
            {POLICY_OPTIONS.map(p => (
              <option key={p} value={p}>{formatPolicy(p)}</option>
            ))}
          </select>
          <button
            type="submit"
            disabled={submitting}
            style={{
              padding: '6px 16px',
              borderRadius: '6px',
              border: 'none',
              background: '#3b82f6',
              color: '#fff',
              fontWeight: 600,
              cursor: submitting ? 'not-allowed' : 'pointer',
              opacity: submitting ? 0.7 : 1,
            }}
          >
            {submitting ? 'Updating…' : 'Submit'}
          </button>
        </form>
        {policyStatus && (
          <p style={{ color: '#16a34a', marginTop: '8px' }} role="status">{policyStatus}</p>
        )}
        {policyError && (
          <p style={{ color: '#dc2626', marginTop: '8px' }} role="alert">{policyError}</p>
        )}
      </div>
    </section>
  );
}
