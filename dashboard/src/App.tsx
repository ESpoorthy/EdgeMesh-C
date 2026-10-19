import { useState } from 'react';
import { Overview } from './pages/Overview.tsx';
import { BackendHealth } from './pages/BackendHealth.tsx';
import { BenchmarkLab } from './pages/BenchmarkLab.tsx';

type Page = 'overview' | 'backends' | 'benchmark';

const NAV_ITEMS: { id: Page; label: string }[] = [
  { id: 'overview', label: 'Overview' },
  { id: 'backends', label: 'Backend Health' },
  { id: 'benchmark', label: 'Benchmark Lab' },
];

/**
 * Root application component.
 * Nav bar with 3 links + conditional page rendering. No external router.
 */
export default function App() {
  const [page, setPage] = useState<Page>('overview');

  return (
    <div style={{ fontFamily: 'system-ui, sans-serif', minHeight: '100vh', background: '#f3f4f6' }}>
      {/* Header */}
      <header
        style={{
          background: '#1e3a5f',
          color: '#fff',
          padding: '0 24px',
          display: 'flex',
          alignItems: 'center',
          gap: '32px',
          height: '56px',
          boxShadow: '0 2px 4px rgba(0,0,0,0.2)',
        }}
      >
        <h1 style={{ margin: 0, fontSize: '1.1rem', fontWeight: 700, letterSpacing: '0.02em', whiteSpace: 'nowrap' }}>
          EdgeMesh-C Dashboard
        </h1>

        <nav aria-label="Main navigation">
          <ul style={{ display: 'flex', gap: '4px', listStyle: 'none', margin: 0, padding: 0 }}>
            {NAV_ITEMS.map(item => (
              <li key={item.id}>
                <button
                  onClick={() => setPage(item.id)}
                  aria-current={page === item.id ? 'page' : undefined}
                  style={{
                    padding: '6px 14px',
                    border: 'none',
                    borderRadius: '6px',
                    cursor: 'pointer',
                    fontWeight: page === item.id ? 700 : 400,
                    background: page === item.id ? 'rgba(255,255,255,0.2)' : 'transparent',
                    color: '#fff',
                    fontSize: '0.875rem',
                    transition: 'background 0.15s',
                  }}
                >
                  {item.label}
                </button>
              </li>
            ))}
          </ul>
        </nav>
      </header>

      {/* Main content */}
      <main style={{ padding: '24px', maxWidth: '1200px', margin: '0 auto' }}>
        {page === 'overview' && <Overview />}
        {page === 'backends' && <BackendHealth />}
        {page === 'benchmark' && <BenchmarkLab />}
      </main>
    </div>
  );
}
