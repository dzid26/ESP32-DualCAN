<script lang="ts">
  import { app } from '../../lib/store.svelte';
  import SectionHead from '../SectionHead.svelte';

  let prevView = $state(app.view);

  $effect(() => {
    if (app.view === 'radio' && prevView !== 'radio' && !localStorage.getItem('radio-wiggled')) {
      prevView = 'radio';
      wiggle();
      try { localStorage.setItem('radio-wiggled', '1'); } catch {}
    } else {
      prevView = app.view;
    }
  });

  function wiggle() {
    // Google CSS "6 7" meme — full-page wiggle on radio enter
    const style = document.createElement('style');
    style.id = 'radio-wiggle';
    style.textContent = `@keyframes radio-wiggle {
      0% { transform: skewY(0deg); }
      15% { transform: skewY(10deg); }
      35% { transform: skewY(-10deg); }
      55% { transform: skewY(10deg); }
      75% { transform: skewY(-10deg); }
      100% { transform: skewY(0deg); }
    }
    html.radio-wiggle {
      transform-origin: center center;
      animation: radio-wiggle 1.8s cubic-bezier(0.6,0.43,0.68,1.11) 1;
    }`;
    document.head.appendChild(style);
    document.documentElement.classList.add('radio-wiggle');
    setTimeout(() => {
      document.documentElement.classList.remove('radio-wiggle');
      document.getElementById('radio-wiggle')?.remove();
    }, 2000);
  }
</script>

<div class="radio-view">
  <SectionHead title="Radio" />
  <div class="radio-frame">
    <iframe
      src="https://null.perchance.org/radio-box-2-lite"
      title="Perchance Radio"
      allow="autoplay"
      sandbox="allow-scripts allow-same-origin allow-popups"
    ></iframe>
  </div>
</div>

<style>
  .radio-view {
    padding: 12px;
    overflow-y: auto;
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 10px;
    height: 100%;
  }
  .radio-frame {
    flex: 1;
    min-height: 0;
    border-radius: 6px;
    overflow: hidden;
    border: 1px solid var(--dc-border, #333);
  }
  .radio-frame iframe {
    width: 100%;
    height: 100%;
    border: none;
    background: #000;
  }
</style>
