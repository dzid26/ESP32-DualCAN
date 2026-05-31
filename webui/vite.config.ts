import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'
import path from 'path'
import fs from 'fs'

/** Serve the repo-root `dbc/` folder as URL path so the
 *  webui can fetch DBC files without needing a junction or duplicate copies. */
function serveDbc(): import('vite').Plugin {
  const dbcDir = path.resolve(__dirname, '..', 'dbc')
  return {
    name: 'serve-dbc',
    configureServer(server) {
      server.middlewares.use('/dbc', (req, res, next) => {
        const file = path.join(dbcDir, req.url ?? '')
        if (fs.existsSync(file) && fs.statSync(file).isFile()) {
          res.setHeader('Content-Type', 'application/octet-stream')
          fs.createReadStream(file).pipe(res)
        } else {
          next()
        }
      })
    },
    closeBundle() {
      const outDir = path.resolve(__dirname, 'dist', 'dbc')
      if (!fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true })
      for (const f of fs.readdirSync(dbcDir)) {
        const src = path.join(dbcDir, f)
        if (fs.statSync(src).isFile())
          fs.copyFileSync(src, path.join(outDir, f))
      }
    },
  }
}

export default defineConfig({
  base: process.env.VITE_BASE ?? '/',
  plugins: [svelte(), serveDbc()],
  server: {
    allowedHosts: true,
  },
})
