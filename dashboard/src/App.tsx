import { useState } from 'react';
import { Overview } from './pages/Overview.tsx';
import { BackendHealth } from './pages/BackendHealth.tsx';
import { BenchmarkLab } from './pages/BenchmarkLab.tsx';
import { Sidebar } from './components/Sidebar.tsx';
import { TopBar } from './components/TopBar.tsx';
import { usePolling } from './hooks/usePolling.ts';
import { getHealth } from './api/client.ts';

export type Page = 'overview' | 'backends' | 'benchmark';

export default function App() {
  const [page, setPage] = useState<Page>('overview');

  // Health check drives the top bar reachability indicator
  const { data: health, error: healthError } = usePolling(getHealth, 5000);
  const reachable = healthError ? false : health ? true : null;

  return (
    <div
      style={{
        display: 'flex',
        height: '100vh',
        overflow: 'hidden',
        background: 'var(--bg-base)',
      }}
    >
      {/* Left sidebar */}
      <Sidebar page={page} onNavigate={setPage} />

      {/* Right: topbar + content */}
      <div style={{ flex: 1, display: 'flex', flexDirection: 'column', overflow: 'hidden', minWidth: 0 }}>
        <TopBar
          healthy={reachable}
          apiAddress="127.0.0.1:9090"
          reachable={reachable}
        />

        {/* Scrollable page content */}
        <main
          style={{
            flex: 1,
            overflowY: 'auto',
            padding: '24px 28px',
          }}
        >
          {page === 'overview'  && <Overview />}
          {page === 'backends'  && <BackendHealth />}
          {page === 'benchmark' && <BenchmarkLab />}
        </main>
      </div>
    </div>
  );
}
