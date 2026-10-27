import react from '@vitejs/plugin-react'
import { defineConfig, type ProxyOptions } from 'vite'

// The dashboard talks to the daemon through this proxy, so the browser only
// ever sees one origin. AGENTSMON_URL points it at another daemon (the TCP
// listener); AGENTSMON_TOKEN is added when that daemon asks for one.
const target = process.env.AGENTSMON_URL ?? 'http://127.0.0.1:7788'
const token = process.env.AGENTSMON_TOKEN

const proxy: Record<string, ProxyOptions> = {
  '/v1': {
    target,
    ws: true,
    changeOrigin: true,
    headers: token ? { authorization: `Bearer ${token}` } : undefined,
  },
}

export default defineConfig({
  plugins: [react()],
  server: { port: 5173, host: '127.0.0.1', proxy },
  preview: { port: 4173, host: '127.0.0.1', proxy },
})
