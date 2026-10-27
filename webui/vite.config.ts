import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'
import { viteSingleFile } from 'vite-plugin-singlefile'

// One self-contained index.html: the app serves it from its resources
// (tumbler://app/), so there's nothing else to bundle or keep in step.
export default defineConfig({
  plugins: [react(), tailwindcss(), viteSingleFile()],
  base: './',
  server: { port: 5173, strictPort: true },
  build: { outDir: 'dist', emptyOutDir: true, assetsInlineLimit: 100_000_000, chunkSizeWarningLimit: 4096 },
})
