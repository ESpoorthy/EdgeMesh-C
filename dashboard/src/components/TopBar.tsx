interface TopBarProps {
  healthy: boolean | null;   // null = loading
  apiAddress: string;
  reachable: boolean | null;
}

export function TopBar({ healthy, apiAddress, reachable }: TopBarProps) {
  return (
    <div
      style={{
        height: 'var(--topbar-height)',
        background: 'var(--bg-surface)',
        borderBottom: '1px solid var(--border)',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        padding: '0 18px',
        flexShrink: 0,
      }}
    >
      {/* Left: service health */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
        {healthy === null ? (
          <span className="skeleton" style={{ width: 120, height: 14 }} />
        ) : (
          <>
            <span
              style={{
                width: 7,
                height: 7,
                borderRadius: '50%',
                background: healthy ? 'var(--accent-teal)' : 'var(--accent-red)',
                display: 'inline-block',
                flexShrink: 0,
              }}
            />
            <span style={{ fontSize: '12px', color: healthy ? 'var(--accent-teal)' : 'var(--accent-red)', fontWeight: 500 }}>
              {healthy ? 'Service healthy' : 'Service error'}
            </span>
            <span style={{ fontSize: '11px', color: 'var(--text-muted)' }}>/ LIVE</span>
          </>
        )}
      </div>

      {/* Right: API address + reachability */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
        <span style={{ fontSize: '11px', color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)' }}>
          Admin API {apiAddress}
        </span>
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: '5px',
            padding: '2px 10px',
            borderRadius: 'var(--radius-sm)',
            background: reachable === false
              ? 'rgba(248,81,73,0.12)'
              : reachable === null
              ? 'var(--bg-elevated)'
              : 'rgba(61,214,140,0.12)',
            border: `1px solid ${
              reachable === false
                ? 'rgba(248,81,73,0.3)'
                : reachable === null
                ? 'var(--border)'
                : 'rgba(61,214,140,0.3)'
            }`,
          }}
        >
          <span
            style={{
              width: 6,
              height: 6,
              borderRadius: '50%',
              background: reachable === false
                ? 'var(--accent-red)'
                : reachable === null
                ? 'var(--text-muted)'
                : 'var(--accent-teal)',
              display: 'inline-block',
            }}
          />
          <span
            style={{
              fontSize: '11px',
              color: reachable === false
                ? 'var(--accent-red)'
                : reachable === null
                ? 'var(--text-secondary)'
                : 'var(--accent-teal)',
              fontWeight: 500,
            }}
          >
            {reachable === null ? 'Connecting…' : reachable ? 'Reachable · live' : 'Unreachable'}
          </span>
        </div>
      </div>
    </div>
  );
}
