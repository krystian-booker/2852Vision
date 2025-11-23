import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  server: {
    port: 8080,
    host: '0.0.0.0', // Bind to all interfaces (IPv4 and IPv6)
    proxy: {
      // Proxy API calls to backend
      '^/(api|cameras|calibration|settings|monitoring)/': {
        target: 'http://localhost:5001',
        changeOrigin: true,
      },
      '/video_feed': {
        target: 'http://localhost:5001',
        changeOrigin: true,
      },
      '/processed_video_feed': {
        target: 'http://localhost:5001',
        changeOrigin: true,
      },
    },
  },
})
