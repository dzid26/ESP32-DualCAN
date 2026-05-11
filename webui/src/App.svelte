<script lang="ts">
  import { app } from './lib/store.svelte';
  import Shell from './components/Shell.svelte';
  import CarPicker from './components/CarPicker.svelte';

  $effect(() => {
    if (!app.connected) return;
    function guard(e: BeforeUnloadEvent) {
      e.preventDefault();
      e.returnValue = '';
    }
    window.addEventListener('beforeunload', guard);
    return () => window.removeEventListener('beforeunload', guard);
  });

  import EventsView from './components/views/EventsView.svelte';
  import ScriptsView from './components/views/ScriptsView.svelte';
  import AIView from './components/views/AIView.svelte';
  import GalleryView from './components/views/GalleryView.svelte';
  import DashboardView from './components/views/DashboardView.svelte';
  import DbcView from './components/views/DbcView.svelte';
  import TraceView from './components/views/TraceView.svelte';
  import CaptureView from './components/views/CaptureView.svelte';
  import SettingsView from './components/views/SettingsView.svelte';
  import TeslaView from './components/views/TeslaView.svelte';
  import EngineView from './components/views/EngineView.svelte';
</script>

<Shell>
  {#snippet children()}
    {#if app.view === 'events'}<EventsView />
    {:else if app.view === 'scripts'}<ScriptsView />
    {:else if app.view === 'ai'}<AIView />
    {:else if app.view === 'gallery'}<GalleryView />
    {:else if app.view === 'dashboard'}<DashboardView />
    {:else if app.view === 'dbc'}<DbcView />
    {:else if app.view === 'trace'}<TraceView />
    {:else if app.view === 'capture'}<CaptureView />
    {:else if app.view === 'settings'}<SettingsView />
    {:else if app.view === 'tesla'}<TeslaView />
    {:else if app.view === 'engine'}<EngineView />
    {/if}
  {/snippet}
</Shell>

<CarPicker
  open={app.carPickerOpen}
  current={app.car}
  onPick={(c) => app.pickCar(c)}
  onClear={() => app.clearCar()}
  onClose={() => (app.carPickerOpen = false)}
/>
