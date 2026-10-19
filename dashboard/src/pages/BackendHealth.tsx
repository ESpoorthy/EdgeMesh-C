import { getBackends } from '../api/client.ts';
import { usePolling } from '../hooks/usePolling.ts';
import { StatusBadge } from '../components/StatusBadge.tsx';
import { ErrorBanner } from '../components/ErrorBanner.tsx';

function formatTimestamp(ts: string | null): string {
  if (!ts) return 'N/A';
  try {
    return new Date(ts).toLocaleString();
  } catch {
    return ts;
  }
}

/**
 * Backend Health page — polls GET /api/v1/backends every 2 s.
 * Renders an accessible table with one row per backend.
 */
export function BackendHealth() {
  const { data, loading, error } = usePolling(getBackends, 2000);

  return (
    <section aria-labelledby="backends-heading">
      <h2 id="backends-heading">Backend Health</h2>

      <ErrorBanner error={error} />

      {loading && !data && (
        <p aria-busy="true" aria-label="Loading backends">Loading…</p>
      )}

      {!loading && data && data.backends.length === 0 && (
        <p>No backends configured.</p>
      )}

      {data && data.backends.length > 0 && (
        <div style={{ overflowX: 'auto' }}>
          <table
            aria-label="Backend health table"
            style={{
              borderCollapse: 'collapse',
              width: '100%',
              fontSize: '0.875rem',
            }}
          >
            <thead>
              <tr style={{ background: '#f9fafb', textAlign: 'left' }}>
                <th scope="col" style={thStyle}>ID</th>
                <th scope="col" style={thStyle}>Address</th>
                <th scope="col" style={thStyle}>State</th>
                <th scope="col" style={thStyle}>Active Conns</th>
                <th scope="col" style={thStyle}>Requests Total</th>
                <th scope="col" style={thStyle}>Forwarded</th>
                <th scope="col" style={thStyle}>Failures</th>
                <th scope="col" style={thStyle}>Last Check</th>
                <th scope="col" style={thStyle}>Last Response (ms)</th>
              </tr>
            </thead>
            <tbody>
              {data.backends.map((b, i) => (
                <tr
                  key={b.id}
                  style={{ background: i % 2 === 0 ? '#fff' : '#f9fafb', borderTop: '1px solid #e5e7eb' }}
                >
                  <td style={tdStyle}>{b.id}</td>
                  <td style={tdStyle}>{b.address}</td>
                  <td style={tdStyle}>
                    <StatusBadge state={b.state} />
                  </td>
                  <td style={tdStyle}>{b.active_connections}</td>
                  <td style={tdStyle}>{b.requests_total}</td>
                  <td style={tdStyle}>{b.responses_forwarded_total}</td>
                  <td style={tdStyle}>{b.proxy_failures_total}</td>
                  <td style={tdStyle}>{formatTimestamp(b.last_check_at)}</td>
                  <td style={tdStyle}>{b.last_response_ms ?? 'N/A'}</td>
                </tr>
              ))}
            </tbody>
          </table>
          <p style={{ fontSize: '0.75rem', color: '#9ca3af', marginTop: '8px' }}>
            Generated at: {data.generated_at}
          </p>
        </div>
      )}
    </section>
  );
}

const thStyle: React.CSSProperties = {
  padding: '10px 12px',
  fontWeight: 600,
  color: '#374151',
  whiteSpace: 'nowrap',
};

const tdStyle: React.CSSProperties = {
  padding: '10px 12px',
  color: '#374151',
  whiteSpace: 'nowrap',
};
