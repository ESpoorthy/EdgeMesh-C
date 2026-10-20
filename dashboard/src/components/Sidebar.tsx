import type { Page } from '../App.tsx';

interface SidebarProps {
  page: Page;
  onNavigate: (p: Page) => void;
}

const NAV: { id: Page; label: string; icon: string }[] = [
  { id: 'overview',  label: 'Overview',       icon: '⬡' },
  { id: 'backends',  label: 'Backend Health',  icon: '⬢' },
  { id: 'benchmark', label: 'Benchmark Lab',   icon: '◎' },
];

export function Sidebar({ page, onNavigate }: SidebarProps) {
  return (
    <aside
      style={{
        width: 'var(--sidebar-width)',
        minWidth: 'var(--sidebar-width)',
        background: 'var(--bg-surface)',
        borderRight: '1px solid var(--border)',
        display: 'flex',
        flexDirection: 'column',
        height: '100%',
        overflow: 'hidden',
      }}
    >
      {/* Logo */}
      <div
        style={{
          padding: '14px 14px 10px',
          borderBottom: '1px solid var(--border)',
        }}
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '2px' }}>
          <div
            style={{
              width: 28,
              height: 28,
              borderRadius: 'var(--radius-sm)',
              background: 'var(--accent-teal)',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              flexShrink: 0,
            }}
          >
            <svg width="16" height="16" viewBox="0 0 16 16" fill="none" aria-hidden="true">
              <circle cx="8" cy="8" r="5" stroke="#0d1117" strokeWidth="2"/>
              <path d="M3 8h10M8 3v10" stroke="#0d1117" strokeWidth="1.5"/>
            </svg>
          </div>
          <div>
            <div style={{ fontWeight: 700, fontSize: '13px', color: 'var(--text-primary)', letterSpacing: '0.02em' }}>
              EdgeMesh-C
            </div>
            <div style={{ fontSize: '10px', color: 'var(--text-secondary)', letterSpacing: '0.06em', textTransform: 'uppercase' }}>
              Traffic Control / C17
            </div>
          </div>
        </div>
      </div>

      {/* Nav section */}
      <div style={{ padding: '12px 8px 8px' }}>
        <div
          style={{
            fontSize: '10px',
            color: 'var(--text-muted)',
            letterSpacing: '0.08em',
            textTransform: 'uppercase',
            padding: '0 6px',
            marginBottom: '6px',
          }}
        >
          Workspace
        </div>
        <nav aria-label="Main navigation">
          <ul style={{ listStyle: 'none' }}>
            {NAV.map(item => {
              const active = page === item.id;
              return (
                <li key={item.id} style={{ marginBottom: '2px' }}>
                  <button
                    onClick={() => onNavigate(item.id)}
                    aria-current={active ? 'page' : undefined}
                    style={{
                      width: '100%',
                      display: 'flex',
                      alignItems: 'center',
                      gap: '8px',
                      padding: '7px 8px',
                      borderRadius: 'var(--radius-md)',
                      border: 'none',
                      background: active ? 'rgba(61,214,140,0.12)' : 'transparent',
                      color: active ? 'var(--accent-teal)' : 'var(--text-secondary)',
                      fontWeight: active ? 600 : 400,
                      fontSize: '13px',
                      textAlign: 'left',
                      transition: 'background 0.12s, color 0.12s',
                      cursor: 'pointer',
                    }}
                    onMouseEnter={e => {
                      if (!active) {
                        (e.currentTarget as HTMLButtonElement).style.background = 'rgba(255,255,255,0.04)';
                        (e.currentTarget as HTMLButtonElement).style.color = 'var(--text-primary)';
                      }
                    }}
                    onMouseLeave={e => {
                      if (!active) {
                        (e.currentTarget as HTMLButtonElement).style.background = 'transparent';
                        (e.currentTarget as HTMLButtonElement).style.color = 'var(--text-secondary)';
                      }
                    }}
                  >
                    <span style={{ fontSize: '14px', lineHeight: 1 }} aria-hidden="true">{item.icon}</span>
                    {item.label}
                  </button>
                </li>
              );
            })}
          </ul>
        </nav>
      </div>

      {/* Local environment section */}
      <div style={{ padding: '12px 8px 8px', borderTop: '1px solid var(--border-light)', marginTop: 'auto' }}>
        <div
          style={{
            fontSize: '10px',
            color: 'var(--text-muted)',
            letterSpacing: '0.08em',
            textTransform: 'uppercase',
            padding: '0 6px',
            marginBottom: '8px',
          }}
        >
          Local Environment
        </div>
        <div style={{ padding: '0 6px' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '6px', marginBottom: '4px' }}>
            <span style={{ color: 'var(--accent-teal)', fontSize: '10px' }}>⬡</span>
            <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>loopback / development</span>
          </div>
          <div style={{ fontSize: '11px', color: 'var(--text-muted)', paddingLeft: '16px' }}>
            HTTP reverse proxy
          </div>
        </div>
      </div>

      {/* Footer */}
      <div
        style={{
          borderTop: '1px solid var(--border)',
          padding: '10px 14px',
        }}
      >
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            gap: '6px',
            padding: '5px 8px',
            borderRadius: 'var(--radius-sm)',
            background: 'var(--bg-elevated)',
            marginBottom: '8px',
          }}
        >
          <span
            style={{
              width: 6,
              height: 6,
              borderRadius: '50%',
              background: 'var(--accent-teal)',
              flexShrink: 0,
              display: 'inline-block',
            }}
          />
          <span style={{ fontSize: '11px', color: 'var(--text-secondary)' }}>Static design preview</span>
        </div>
        <div style={{ fontSize: '10px', color: 'var(--text-muted)', lineHeight: 1.6 }}>
          Signal Topography<br />
          Operations console
        </div>
        <div style={{ fontSize: '10px', color: 'var(--text-muted)', marginTop: '6px', letterSpacing: '0.04em' }}>
          EDGEMESH-C / LOCAL
        </div>
      </div>
    </aside>
  );
}
