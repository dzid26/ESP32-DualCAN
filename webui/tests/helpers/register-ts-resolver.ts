import './test-globals.ts';
import { registerHooks } from 'node:module';

// Resolve bundler-style extensionless specifiers (e.g. `../transport/ble`,
// `./toast.svelte`) to their real `.ts` files so `node --test` can load app
// modules that were written for Vite's resolver.
registerHooks({
  resolve(specifier, context, nextResolve) {
    try {
      return nextResolve(specifier, context);
    } catch (error) {
      if (specifier.startsWith('.') || specifier.startsWith('/')) {
        try {
          return nextResolve(`${specifier}.ts`, context);
        } catch { /* fall through to the original error */ }
      }
      throw error;
    }
  },
});
