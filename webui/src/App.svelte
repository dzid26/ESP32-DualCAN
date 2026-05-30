<script lang="ts">
  import { app } from './lib/store.svelte';
  import Shell from './components/Shell.svelte';
  import CarPicker from './components/CarPicker.svelte';
  import Toast from './components/Toast.svelte';

  $effect(() => {
    if (!app.connected) return;
    function guard(e: BeforeUnloadEvent) {
      e.preventDefault();
      e.returnValue = '';
    }
    window.addEventListener('beforeunload', guard);
    return () => window.removeEventListener('beforeunload', guard);
  });

  import DashboardView from './components/views/DashboardView.svelte';
  import ScriptsView from './components/views/ScriptsView.svelte';
  import AIView from './components/views/AIView.svelte';
  import GalleryView from './components/views/GalleryView.svelte';
  import SignalsView from './components/views/SignalsView.svelte';
  import DbcView from './components/views/DbcView.svelte';
  import TraceView from './components/views/TraceView.svelte';
  import CaptureView from './components/views/CaptureView.svelte';
  import SettingsView from './components/views/SettingsView.svelte';
  import TeslaView from './components/views/TeslaView.svelte';
  import EngineView from './components/views/EngineView.svelte';
</script>

<Shell>
  {#snippet children()}
    <div style:display={app.view === 'events' ? '' : 'none'}><DashboardView /></div>
    <div style:display={app.view === 'scripts' ? '' : 'none'}><ScriptsView /></div>
    <div style:display={app.view === 'ai' ? '' : 'none'}><AIView /></div>
    <div style:display={app.view === 'gallery' ? '' : 'none'}><GalleryView /></div>
    <div style:display={app.view === 'signals' ? '' : 'none'}><SignalsView /></div>
    <div style:display={app.view === 'dbc' ? '' : 'none'}><DbcView /></div>
    <div style:display={app.view === 'trace' ? '' : 'none'}><TraceView /></div>
    <div style:display={app.view === 'capture' ? '' : 'none'}><CaptureView /></div>
    <div style:display={app.view === 'settings' ? '' : 'none'}><SettingsView /></div>
    <div style:display={app.view === 'tesla' ? '' : 'none'}><TeslaView /></div>
    <div style:display={app.view === 'engine' ? '' : 'none'}><EngineView /></div>
  {/snippet}
</Shell>

<CarPicker
  open={app.carPickerOpen}
  current={app.car}
  onPick={(c) => app.pickCar(c)}
  onClear={() => app.clearCar()}
  onClose={() => (app.carPickerOpen = false)}
/>

<Toast />
