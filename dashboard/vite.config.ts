import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      // Proxy /api/* to the C admin API during development.
      // This avoids CORS issues in dev — the C server's CORS whitelist
      // still enforces allowed origins for direct browser access.
      '/api': 'http://127.0.0.1:9090',
    },
  },
})
