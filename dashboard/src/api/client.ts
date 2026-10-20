// EdgeMesh-C Admin API client
// Base URL: http://127.0.0.1:9090
// Field names and enum values MUST match docs/api-contract.md exactly.

const BASE_URL = 'http://127.0.0.1:9090';

// ---------------------------------------------------------------------------
// Shared error type
// ---------------------------------------------------------------------------

export class ApiError extends Error {
  readonly status: number;
  readonly body: string;

  constructor(status: number, body: string) {
    super(`API error ${status}: ${body}`);
    this.name = 'ApiError';
    this.status = status;
    this.body = body;
  }
}

// ---------------------------------------------------------------------------
// Response interfaces — field names match api-contract.md exactly
// ---------------------------------------------------------------------------

export interface HealthResponse {
  status: string;
  service: string;
  version: string;
  uptime_seconds: number;
}

export interface OverviewMetrics {
  requests_total: number;
  responses_forwarded_total: number;
  proxy_failures_total: number;
  requests_per_second: number;
  response_bytes_per_second: number;
  active_requests: number;
  latency_p50_ms: number | null;
  latency_p95_ms: number | null;
  latency_p99_ms: number | null;
}

export interface OverviewBackends {
  total: number;
  healthy: number;
  unhealthy: number;
  unknown: number;
}

export interface OverviewResponse {
  proxy_running: boolean;
  policy: string;
  uptime_seconds: number;
  backends: OverviewBackends;
  metrics: OverviewMetrics;
  sample_window_seconds: number;
  generated_at: string;
}

export type BackendState = 'healthy' | 'unhealthy' | 'unknown';

export interface Backend {
  id: string;
  address: string;
  state: BackendState;
  active_connections: number;
  requests_total: number;
  responses_forwarded_total: number;
  proxy_failures_total: number;
  last_check_at: string | null;
  last_response_ms: number | null;
}

export interface BackendsResponse {
  backends: Backend[];
  generated_at: string;
}

export interface PolicyResponse {
  policy: string;
  updated: boolean;
}

export interface MetricsPoint {
  timestamp: string;
  requests_total: number;
  responses_forwarded_total: number;
  proxy_failures_total: number;
  requests_per_second: number;
  response_bytes_per_second: number;
  latency_p50_ms: number | null;
  latency_p95_ms: number | null;
  latency_p99_ms: number | null;
}

export interface MetricsResponse {
  window_seconds: number;
  points: MetricsPoint[];
  generated_at: string;
}

export interface BenchmarkRun {
  run_id: string;
  completed_at: string;
  policy: string;
  target: string;
  requested_requests: number;
  concurrency: number;
  completed_requests: number;
  failed_requests: number;
  duration_seconds: number;
  requests_per_second: number;
  response_bytes_per_second: number;
  latency_p50_ms: number | null;
  latency_p95_ms: number | null;
  latency_p99_ms: number | null;
  result_file: string;
}

export type BenchmarkLatestResponse =
  | { available: false; run: null }
  | { available: true; run: BenchmarkRun };

// ---------------------------------------------------------------------------
// Internal fetch helper
// ---------------------------------------------------------------------------

async function apiFetch<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(`${BASE_URL}${path}`, {
    ...init,
    headers: {
      'Content-Type': 'application/json',
      ...(init?.headers ?? {}),
    },
  });

  if (!response.ok) {
    const body = await response.text().catch(() => '');
    throw new ApiError(response.status, body);
  }

  return response.json() as Promise<T>;
}

// ---------------------------------------------------------------------------
// Typed endpoint wrappers
// ---------------------------------------------------------------------------

/** GET /api/v1/health */
export function getHealth(): Promise<HealthResponse> {
  return apiFetch<HealthResponse>('/api/v1/health');
}

/** GET /api/v1/overview */
export function getOverview(): Promise<OverviewResponse> {
  return apiFetch<OverviewResponse>('/api/v1/overview');
}

/** GET /api/v1/backends */
export function getBackends(): Promise<BackendsResponse> {
  return apiFetch<BackendsResponse>('/api/v1/backends');
}

/** PUT /api/v1/policy */
export function putPolicy(policy: string): Promise<PolicyResponse> {
  return apiFetch<PolicyResponse>('/api/v1/policy', {
    method: 'PUT',
    body: JSON.stringify({ policy }),
  });
}

/** GET /api/v1/metrics?window_seconds=N */
export function getMetrics(window_seconds?: number): Promise<MetricsResponse> {
  const qs = window_seconds !== undefined ? `?window_seconds=${window_seconds}` : '';
  return apiFetch<MetricsResponse>(`/api/v1/metrics${qs}`);
}

/** GET /api/v1/benchmarks/latest */
export function getBenchmarkLatest(): Promise<BenchmarkLatestResponse> {
  return apiFetch<BenchmarkLatestResponse>('/api/v1/benchmarks/latest');
}
