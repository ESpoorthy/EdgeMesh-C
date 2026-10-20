import { useCallback, useEffect, useState } from 'react';
import { getBenchmarkLatest } from '../api/client.ts';
import type { BenchmarkLatestResponse } from '../api/client.ts';

function fmt(v: number | null, unit = ''): string {
  return v === null ? 'N/A' : `${v}${unit ? ' ' + unit : ''}`;
}

export function BenchmarkLab() {
  const [data, setData]       = useState<BenchmarkLatestResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError]     = useState<Error | null>(null);

  const fetchData = useCallback(() => {
    setLoading(true);
    setError(null);
    getBenchmarkLatest()
      .then(r => { setData(r); setLoading(false); })
      .catch((e: unknown) => {
        setError(e instanceof Error ? e : new Error(String(e)));
        setLoading(false);
      });
  }, []);

  useEffect(() => { fetchData(); }, [fetchData]);

  return (
    <div>
      {/* Header */}
      <div style={{ marginBottom: '20px', display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between', flexWrap: 'wrap', gap: '12px' }}>
        <div>
          <h2 style={{ fontSize: '22px', fontWeight: 700, color: 'var(--text-primary)', marginBottom: '4px' }}>
            Benchmark Lab
          </h2>
          <p style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>
            Latest completed benchmark run results. Run from CLI to generate new results.
          </p>
        </div>
        <button
          onClick={fetchData}
          disabled={loading}
          style={{
            padding: '6px 16px',
            borderRadius: 'var(--radius-md)',
            border: '1px solid var(--border)',
            background: 'var(--bg-elevated)',
            color: loading ? 'var(--text-muted)' : 'var(--text-primary)',
            fontWeight: 500,
            fontSize: '12px',
            cursor: loading ? 'not-allowed' : 'pointer',
          }}
        >
          {loading ? 'Loading…' : '↺ Refresh'}
        </button>
      </div>

      {/* CLI hint */}
      <div
        style={{
          padding: '10px 14px',
          borderRadius: 'var(--radius-md)',
          background: 'var(--bg-elevated)',
          border: '1px solid var(--border)',
          marginBottom: '16px',
          fontSize: '12px',
          color: 'var(--text-secondary)',
        }}
      >
        <span style={{ color: 'var(--accent-teal)', marginRight: '8px' }}>$</span>
        <span style={{ fontFamily: 'var(--font-mono)' }}>
          ./build/benchmark_client -n 1000 -c 20 -t http://127.0.0.1:8080/ -p round_robin
        </span>
      </div>

      {/* Error */}
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

      {/* No results */}
      {!loading && !error && data && !data.available && (
        <div
          style={{
            padding: '48px 24px',
            textAlign: 'center',
            background: 'var(--bg-surface)',
            border: '1px solid var(--border)',
            borderRadius: 'var(--radius-lg)',
            color: 'var(--text-muted)',
            fontSize: '13px',
          }}
        >
          <div style={{ fontSize: '28px', marginBottom: '12px', opacity: 0.4 }}>◎</div>
          No benchmark results available yet.
          <div style={{ fontSize: '12px', marginTop: '6px' }}>
            Run the benchmark client from the terminal to generate results.
          </div>
        </div>
      )}

      {/* Results */}
      {!loading && data && data.available && (
        <div
          style={{
            background: 'var(--bg-surface)',
            border: '1px solid var(--border)',
            borderRadius: 'var(--radius-lg)',
            overflow: 'hidden',
          }}
        >
          {/* Run header */}
          <div
            style={{
              padding: '12px 18px',
              borderBottom: '1px solid var(--border)',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              flexWrap: 'wrap',
              gap: '8px',
            }}
          >
            <div>
              <span style={{ fontFamily: 'var(--font-mono)', fontSize: '12px', color: 'var(--accent-teal)' }}>
                {data.run.run_id}
              </span>
              <span style={{ fontSize: '11px', color: 'var(--text-muted)', marginLeft: '12px' }}>
                completed {data.run.completed_at}
              </span>
            </div>
            <span
              style={{
                padding: '2px 10px',
                borderRadius: 'var(--radius-sm)',
                background: 'rgba(61,214,140,0.1)',
                border: '1px solid rgba(61,214,140,0.25)',
                fontSize: '11px',
                color: 'var(--accent-teal)',
              }}
            >
              {data.run.policy}
            </span>
          </div>

          {/* Stat grid */}
          <div
            style={{
              display: 'grid',
              gridTemplateColumns: 'repeat(auto-fill, minmax(160px, 1fr))',
              gap: '1px',
              background: 'var(--border)',
            }}
          >
            {[
              { label: 'Target',         value: data.run.target },
              { label: 'Concurrency',    value: String(data.run.concurrency) },
              { label: 'Requested',      value: data.run.requested_requests.toLocaleString() },
              { label: 'Completed',      value: data.run.completed_requests.toLocaleString() },
              { label: 'Failed',         value: String(data.run.failed_requests), accent: data.run.failed_requests > 0 },
              { label: 'Duration',       value: `${data.run.duration_seconds} s` },
              { label: 'Req / second',   value: data.run.requests_per_second.toFixed(1) },
              { label: 'Bytes / second', value: data.run.response_bytes_per_second.toLocaleString() },
              { label: 'Latency p50',    value: fmt(data.run.latency_p50_ms, 'ms') },
              { label: 'Latency p95',    value: fmt(data.run.latency_p95_ms, 'ms') },
              { label: 'Latency p99',    value: fmt(data.run.latency_p99_ms, 'ms') },
              { label: 'Result file',    value: data.run.result_file },
            ].map(({ label, value, accent }) => (
              <div
                key={label}
                style={{
                  background: 'var(--bg-surface)',
                  padding: '14px 16px',
                }}
              >
                <div style={{ fontSize: '10px', color: 'var(--text-muted)', marginBottom: '4px' }}>
                  {label}
                </div>
                <div
                  style={{
                    fontSize: '14px',
                    fontFamily: 'var(--font-mono)',
                    fontWeight: 600,
                    color: accent ? 'var(--accent-red)' : 'var(--text-primary)',
                    wordBreak: 'break-all',
                  }}
                >
                  {value}
                </div>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
