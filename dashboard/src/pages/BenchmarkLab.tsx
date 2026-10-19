import { useCallback, useEffect, useState } from 'react';
import { getBenchmarkLatest } from '../api/client.ts';
import type { BenchmarkLatestResponse } from '../api/client.ts';
import { ErrorBanner } from '../components/ErrorBanner.tsx';

function formatLatency(v: number | null): string {
  return v === null ? 'N/A' : String(v);
}

/**
 * Benchmark Lab page.
 * Fetches GET /api/v1/benchmarks/latest once on mount and on manual Refresh.
 * No auto-polling — benchmark results don't change continuously.
 */
export function BenchmarkLab() {
  const [data, setData] = useState<BenchmarkLatestResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<Error | null>(null);

  const fetchData = useCallback(() => {
    setLoading(true);
    setError(null);
    getBenchmarkLatest()
      .then(result => {
        setData(result);
        setLoading(false);
      })
      .catch((err: unknown) => {
        setError(err instanceof Error ? err : new Error(String(err)));
        setLoading(false);
      });
  }, []);

  useEffect(() => {
    fetchData();
  }, [fetchData]);

  return (
    <section aria-labelledby="benchmark-heading">
      <h2 id="benchmark-heading">Benchmark Lab</h2>

      <div style={{ marginBottom: '16px' }}>
        <button
          onClick={fetchData}
          disabled={loading}
          aria-label="Refresh benchmark results"
          style={{
            padding: '6px 16px',
            borderRadius: '6px',
            border: 'none',
            background: '#3b82f6',
            color: '#fff',
            fontWeight: 600,
            cursor: loading ? 'not-allowed' : 'pointer',
            opacity: loading ? 0.7 : 1,
          }}
        >
          {loading ? 'Loading…' : 'Refresh'}
        </button>
      </div>

      <ErrorBanner error={error} onRetry={fetchData} />

      {loading && !data && (
        <p aria-busy="true">Loading benchmark results…</p>
      )}

      {!loading && !error && data && !data.available && (
        <p style={{ color: '#6b7280', padding: '24px', textAlign: 'center', background: '#f9fafb', borderRadius: '8px' }}>
          No benchmark results available. Run the benchmark client to generate results.
        </p>
      )}

      {!loading && data && data.available && (
        <div style={{ background: '#fff', borderRadius: '8px', boxShadow: '0 1px 3px rgba(0,0,0,0.1)', padding: '20px' }}>
          <h3 style={{ marginTop: 0 }}>Latest Run</h3>
          <dl style={{ display: 'grid', gridTemplateColumns: 'max-content 1fr', gap: '8px 24px' }}>
            <FieldRow label="Run ID" value={data.run.run_id} />
            <FieldRow label="Completed At" value={data.run.completed_at} />
            <FieldRow label="Policy" value={data.run.policy} />
            <FieldRow label="Target" value={data.run.target} />
            <FieldRow label="Requested Requests" value={String(data.run.requested_requests)} />
            <FieldRow label="Concurrency" value={String(data.run.concurrency)} />
            <FieldRow label="Completed Requests" value={String(data.run.completed_requests)} />
            <FieldRow label="Failed Requests" value={String(data.run.failed_requests)} />
            <FieldRow label="Duration" value={`${data.run.duration_seconds} s`} />
            <FieldRow label="Requests/s" value={String(data.run.requests_per_second)} />
            <FieldRow label="Bytes/s" value={String(data.run.response_bytes_per_second)} />
            <FieldRow label="Latency p50" value={`${formatLatency(data.run.latency_p50_ms)} ms`} />
            <FieldRow label="Latency p95" value={`${formatLatency(data.run.latency_p95_ms)} ms`} />
            <FieldRow label="Latency p99" value={`${formatLatency(data.run.latency_p99_ms)} ms`} />
            <FieldRow label="Result File" value={data.run.result_file} />
          </dl>
        </div>
      )}
    </section>
  );
}

function FieldRow({ label, value }: { label: string; value: string }) {
  return (
    <>
      <dt style={{ fontWeight: 600, color: '#374151' }}>{label}</dt>
      <dd style={{ margin: 0, color: '#111827' }}>{value}</dd>
    </>
  );
}
