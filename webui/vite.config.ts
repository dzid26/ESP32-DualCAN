import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

export default defineConfig({
  base: process.env.VITE_BASE ?? '/',
  plugins: [svelte()],
  server: {
    allowedHosts: true,
  },
})
