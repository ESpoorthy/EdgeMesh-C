import { useEffect, useRef, useState } from 'react';

const MIN_INTERVAL_MS = 2000;

export interface PollingState<T> {
  data: T | null;
  loading: boolean;
  error: Error | null;
  lastUpdated: Date | null;
}

/**
 * Generic polling hook.
 * Calls fetchFn every intervalMs milliseconds (minimum 2000 ms).
 * Clears the interval on unmount.
 * On fetch error, sets error state and retains last known data.
 */
export function usePolling<T>(
  fetchFn: () => Promise<T>,
  intervalMs: number,
): PollingState<T> {
  const effectiveInterval = Math.max(intervalMs, MIN_INTERVAL_MS);

  const [data, setData] = useState<T | null>(null);
  const [loading, setLoading] = useState<boolean>(true);
  const [error, setError] = useState<Error | null>(null);
  const [lastUpdated, setLastUpdated] = useState<Date | null>(null);

  // Use a ref so the interval callback always sees the latest fetchFn
  const fetchFnRef = useRef(fetchFn);
  fetchFnRef.current = fetchFn;

  useEffect(() => {
    let cancelled = false;

    async function run() {
      try {
        const result = await fetchFnRef.current();
        if (!cancelled) {
          setData(result);
          setError(null);
          setLastUpdated(new Date());
          setLoading(false);
        }
      } catch (err) {
        if (!cancelled) {
          setError(err instanceof Error ? err : new Error(String(err)));
          setLoading(false);
        }
      }
    }

    // Fire immediately, then on interval
    void run();
    const id = setInterval(() => void run(), effectiveInterval);

    return () => {
      cancelled = true;
      clearInterval(id);
    };
  }, [effectiveInterval]);

  return { data, loading, error, lastUpdated };
}
