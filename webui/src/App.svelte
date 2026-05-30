<script lang="ts">
  import { app } from './lib/store.svelte';
  import type { ViewId } from './lib/store.svelte';
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

  function tabStyle(v: ViewId): string {
    return app.view === v ? 'flex: 1; display: flex; flex-direction: column; min-height: 0;' : 'display: none;';
  }
</script>

<Shell>
  {#snippet children()}
    <div style={tabStyle('events')}><DashboardView /></div>
    <div style={tabStyle('scripts')}><ScriptsView /></div>
    <div style={tabStyle('ai')}><AIView /></div>
    <div style={tabStyle('gallery')}><GalleryView /></div>
    <div style={tabStyle('signals')}><SignalsView /></div>
    <div style={tabStyle('dbc')}><DbcView /></div>
    <div style={tabStyle('trace')}><TraceView /></div>
    <div style={tabStyle('capture')}><CaptureView /></div>
    <div style={tabStyle('settings')}><SettingsView /></div>
    <div style={tabStyle('tesla')}><TeslaView /></div>
    <div style={tabStyle('engine')}><EngineView /></div>
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
