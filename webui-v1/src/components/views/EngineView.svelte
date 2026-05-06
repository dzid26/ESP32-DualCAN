<script lang="ts">
  // Web Audio engine-sound gimmick — phone speaker noisemaker driven by
  // a sim slider or a stub "live signal" stream.
  import { app } from '../../lib/store.svelte';
  import SectionHead from '../SectionHead.svelte';
  import Icon from '../Icon.svelte';
  import { onDestroy } from 'svelte';

  type Profile = {
    name: string; idle: number; redline: number;
    harmonics: number[]; wave: OscillatorType;
    cutoffMul: number; intake: number; rumble: number;
  };

  const ENGINE_PROFILES: Record<string, Profile> = {
    v8:     { name: 'Detroit V8',     idle: 700,  redline: 6500,  harmonics: [1, 0.5, 1.4, 0.8, 0.6], wave: 'sawtooth', cutoffMul: 4, intake: 0.35, rumble: 0.55 },
    i4:     { name: 'Inline-4 turbo', idle: 850,  redline: 7200,  harmonics: [1, 1.1, 0.6, 0.7, 0.4], wave: 'square',   cutoffMul: 5, intake: 0.55, rumble: 0.20 },
    ev:     { name: 'EV pod whine',   idle: 0,    redline: 16000, harmonics: [1, 0.2, 0.05, 1.2, 0.4], wave: 'sine',     cutoffMul: 8, intake: 0.05, rumble: 0.05 },
    scifi:  { name: 'Sci-fi hover',   idle: 200,  redline: 4000,  harmonics: [1, 1.5, 0.3, 2.0, 0.8], wave: 'triangle', cutoffMul: 3, intake: 0.25, rumble: 0.40 },
    diesel: { name: 'Old diesel',     idle: 600,  redline: 3500,  harmonics: [1, 0.3, 1.2, 0.4, 0.9], wave: 'sawtooth', cutoffMul: 2, intake: 0.45, rumble: 0.85 },
  };
  const PROFILE_IDS = Object.keys(ENGINE_PROFILES);

  type Engine = {
    setProfile: (p: Profile) => void;
    update: (rpm: number, throttle: number) => void;
    setMasterGain: (v: number) => void;
    out: GainNode;
  };

  function createEngine(ctx: AudioContext): Engine {
    const out = ctx.createGain();
    out.gain.value = 0;

    let oscs: OscillatorNode[] = [];
    let gains: GainNode[] = [];
    const lp = ctx.createBiquadFilter();
    lp.type = 'lowpass'; lp.Q.value = 0.7;

    // Pink-ish noise for intake roar
    const bufSize = 2 * ctx.sampleRate;
    const noiseBuf = ctx.createBuffer(1, bufSize, ctx.sampleRate);
    const data = noiseBuf.getChannelData(0);
    let b0 = 0, b1 = 0, b2 = 0;
    for (let i = 0; i < bufSize; i++) {
      const w = Math.random() * 2 - 1;
      b0 = 0.99765 * b0 + w * 0.0990460;
      b1 = 0.96300 * b1 + w * 0.2965164;
      b2 = 0.57000 * b2 + w * 1.0526913;
      data[i] = (b0 + b1 + b2 + w * 0.1848) * 0.15;
    }
    const noise = ctx.createBufferSource();
    noise.buffer = noiseBuf; noise.loop = true;
    const noiseGain = ctx.createGain(); noiseGain.gain.value = 0;
    const noiseLP = ctx.createBiquadFilter();
    noiseLP.type = 'lowpass'; noiseLP.frequency.value = 800;
    noise.connect(noiseLP).connect(noiseGain).connect(out);
    noise.start();

    const rumble = ctx.createOscillator();
    rumble.type = 'sine'; rumble.frequency.value = 40;
    const rumbleGain = ctx.createGain(); rumbleGain.gain.value = 0;
    rumble.connect(rumbleGain).connect(out);
    rumble.start();

    let profile: Profile = ENGINE_PROFILES.v8;

    function setProfile(p: Profile): void {
      profile = p;
      oscs.forEach(o => { try { o.stop(); } catch { /* ignore */ } });
      gains.forEach(g => g.disconnect());
      oscs = []; gains = [];
      profile.harmonics.forEach(amp => {
        const osc = ctx.createOscillator();
        osc.type = profile.wave;
        const g = ctx.createGain();
        g.gain.value = amp / profile.harmonics.length;
        osc.connect(g).connect(lp);
        osc.start();
        oscs.push(osc); gains.push(g);
      });
      lp.disconnect();
      lp.connect(out);
    }

    function update(rpm: number, throttle: number): void {
      const t = ctx.currentTime;
      const f0 = Math.max(8, rpm / 60);
      profile.harmonics.forEach((_, i) => {
        const o = oscs[i]; if (!o) return;
        o.frequency.setTargetAtTime(f0 * (i + 1), t, 0.04);
      });
      const maxCutoff = Math.min(12000, f0 * profile.cutoffMul * (1 + throttle * 1.5) + 200);
      lp.frequency.setTargetAtTime(maxCutoff, t, 0.05);
      noiseGain.gain.setTargetAtTime(profile.intake * throttle * 0.6, t, 0.05);
      noiseLP.frequency.setTargetAtTime(400 + throttle * 2400, t, 0.06);
      rumble.frequency.setTargetAtTime(35 + Math.min(40, rpm / 200), t, 0.1);
      rumbleGain.gain.setTargetAtTime(profile.rumble * Math.min(1, rpm / 2000) * 0.25, t, 0.08);
    }

    function setMasterGain(v: number): void {
      out.gain.setTargetAtTime(v, ctx.currentTime, 0.04);
    }

    out.connect(ctx.destination);
    setProfile(profile);
    return { setProfile, update, setMasterGain, out };
  }

  // ---- View state ----
  let running = $state(false);
  let profileId = $state<keyof typeof ENGINE_PROFILES>('v8');
  let source = $state<'sim' | 'live'>('sim');
  let simRpm = $state(900);
  let throttle = $state(0);
  let volume = $state(0.5);
  let muteBelow = $state(false);

  let liveSpeed = $state(0);
  let liveTorque = $state(0);

  let ctxRef: AudioContext | null = null;
  let engRef: Engine | null = null;
  let canvasEl: HTMLCanvasElement | undefined = $state();
  let analyser: AnalyserNode | null = null;
  let raf = 0;

  const profile = $derived(ENGINE_PROFILES[profileId]);

  // Live signal sim — fake vehicleSpeed + motorTorque drive cycle.
  $effect(() => {
    if (source !== 'live' || !app.connected) return;
    const t0 = performance.now();
    const id = setInterval(() => {
      const t = (performance.now() - t0) / 1000;
      const v = 18 + 22 * Math.sin(t * 0.3) + 12 * Math.sin(t * 0.07);
      const tq = 0.55 * Math.cos(t * 0.3) + 0.25 * Math.cos(t * 0.07) + 0.1 * Math.sin(t * 1.3);
      liveSpeed = Math.max(0, v);
      liveTorque = Math.max(-1, Math.min(1, tq));
    }, 100);
    return () => clearInterval(id);
  });

  const rpm = $derived.by(() => {
    if (source === 'live') {
      const span = profile.redline - profile.idle;
      return profile.idle + (Math.min(120, liveSpeed) / 100) * span * 0.7;
    }
    return simRpm;
  });

  const driveThrottle = $derived.by(() => {
    if (source !== 'live') return throttle;
    const torqueAmt = Math.min(1, Math.abs(liveTorque));
    const blend = Math.max(0, Math.min(1, (liveSpeed - 3) / 5));
    return throttle * (1 - blend) + torqueAmt * blend;
  });

  $effect(() => {
    if (!running) {
      if (engRef) engRef.setMasterGain(0);
      cancelAnimationFrame(raf);
      return;
    }
    if (!ctxRef) {
      const Ctor = window.AudioContext;
      ctxRef = new Ctor();
      engRef = createEngine(ctxRef);
      const a = ctxRef.createAnalyser();
      a.fftSize = 1024;
      engRef.out.connect(a);
      analyser = a;
    }
    void ctxRef.resume?.();
    engRef!.setProfile(profile);
    engRef!.setMasterGain(volume);
    drawScope();
    return () => cancelAnimationFrame(raf);
  });

  $effect(() => {
    if (!engRef) return;
    engRef.setProfile(profile);
  });

  $effect(() => {
    if (!engRef || !running) return;
    const muted = muteBelow && source === 'live' && liveSpeed < 5;
    const rpmEff = muted ? 0 : rpm;
    const gainEff = muted ? 0 : volume;
    engRef.update(rpmEff, driveThrottle);
    engRef.setMasterGain(gainEff);
  });

  function drawScope(): void {
    if (!canvasEl || !analyser) return;
    const ctx = canvasEl.getContext('2d')!;
    const a = analyser;
    const buf = new Uint8Array(a.fftSize);
    const css = getComputedStyle(document.documentElement);
    const accent = css.getPropertyValue('--dc-accent').trim() || '#e07b1f';
    const dim    = css.getPropertyValue('--dc-text-fade').trim() || '#a08555';
    const bg     = css.getPropertyValue('--dc-bg').trim() || '#221a0f';

    function tick(): void {
      a.getByteTimeDomainData(buf);
      const w = canvasEl!.width, h = canvasEl!.height;
      ctx.fillStyle = bg; ctx.fillRect(0, 0, w, h);
      ctx.strokeStyle = dim + '33'; ctx.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const y = (i / 4) * h;
        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
      }
      ctx.strokeStyle = accent; ctx.lineWidth = 1.5;
      ctx.beginPath();
      for (let i = 0; i < buf.length; i++) {
        const x = (i / buf.length) * w;
        const y = (buf[i] / 255) * h;
        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      }
      ctx.stroke();
      raf = requestAnimationFrame(tick);
    }
    tick();
  }

  onDestroy(() => {
    cancelAnimationFrame(raf);
    try { void ctxRef?.close(); } catch { /* ignore */ }
  });

  const rpmPct = $derived(Math.min(1, rpm / profile.redline));
  const inRedZone = $derived(rpm > profile.redline * 0.9);

  // For the throttle row — torque-driven blend annotation.
  const live = $derived(source === 'live');
  const blend = $derived(live ? Math.max(0, Math.min(1, (liveSpeed - 3) / 5)) : 0);
  const torqueDriven = $derived(blend > 0.05);
</script>

<div style="padding: 12px; overflow-y: auto; flex: 1; display: flex; flex-direction: column; gap: 12px">
  <SectionHead
    title="Engine sound"
    sub="A gimmick. Plays through your phone speaker — the car never hears it."
  >
    {#snippet actions()}
      <div class="row-flex">
        <button class={'btn btn--sm ' + (running ? 'btn--danger' : 'btn--primary')} onclick={() => (running = !running)}>
          {#if running}<Icon name="pause" size={13} />Stop{:else}<Icon name="play" size={13} />Start{/if}
        </button>
      </div>
    {/snippet}
  </SectionHead>

  <div class="frame" style="background: var(--dc-info-bg); border-color: var(--dc-info-border)">
    <div class="frame__body" style="display: flex; align-items: center; gap: 10px; padding: 8px 12px">
      <Icon name="engine" size={16} />
      <div style="flex: 1; font-size: 12px; color: var(--dc-info-text); line-height: 1.45">
        Pure browser-side noisemaker. Web Audio oscillators driven by
        <span class="mono">vehicleSpeed</span> when connected, or a slider when not.
        Your phone in the cup holder is the speaker. Don't expect the pedestrians to hear it.
      </div>
    </div>
  </div>

  <div style="display: grid; grid-template-columns: minmax(280px, 1.2fr) 1fr; gap: 12px">
    <div class="frame">
      <div class="frame__head">
        <span>Output</span>
        <span class="ghost mono" style="font-size: 10px; margin-left: auto">
          {running ? '◉ live' : '○ stopped'}
        </span>
      </div>
      <div class="frame__body" style="display: flex; flex-direction: column; gap: 10px">
        <canvas
          bind:this={canvasEl}
          width="520" height="120"
          style="width: 100%; height: 120px; background: var(--dc-bg); border: 1px solid var(--dc-border); border-radius: 4px; display: block"
        ></canvas>

        <div>
          <div class="row-flex" style="justify-content: space-between; margin-bottom: 4px">
            <span class="muted mono" style="font-size: 11px">RPM</span>
            <span class="mono" style:font-size="13px" style:color={inRedZone ? 'var(--dc-err-text)' : 'var(--dc-text)'}>
              {Math.round(rpm).toLocaleString()}
              <span class="ghost" style="font-size: 10px">/ {profile.redline.toLocaleString()}</span>
            </span>
          </div>
          <div style="position: relative; height: 14px; background: var(--dc-bg); border: 1px solid var(--dc-border); border-radius: 7px; overflow: hidden">
            <div
              style:position="absolute" style:left="0" style:top="0" style:bottom="0"
              style:width={(rpmPct * 100) + '%'}
              style:background={inRedZone
                ? 'linear-gradient(90deg, var(--dc-accent), var(--dc-err-text))'
                : 'linear-gradient(90deg, var(--dc-accent-hi), var(--dc-accent))'}
              style:transition="width 80ms linear"
            ></div>
            <div style="position: absolute; left: 90%; top: -2px; bottom: -2px; width: 1px; background: var(--dc-err-text)"></div>
          </div>
        </div>

        <div>
          <div class="row-flex" style="justify-content: space-between; margin-bottom: 4px">
            <span class="muted mono" style="font-size: 11px">
              Throttle
              {#if live}
                <span class="ghost mono" style="font-size: 10px; margin-left: 6px">
                  {torqueDriven ? `· torque-driven (${Math.round(blend * 100)}%)` : '· manual (standstill)'}
                </span>
              {/if}
            </span>
            <span class="mono" style="font-size: 11px; color: var(--dc-text-dim)">{Math.round(driveThrottle * 100)}%</span>
          </div>
          <input
            type="range" min="0" max="1" step="0.01"
            bind:value={throttle}
            onmouseup={() => (throttle = 0)}
            ontouchend={() => (throttle = 0)}
            disabled={live && blend > 0.95}
            style:width="100%"
            style:opacity={live && blend > 0.95 ? '0.4' : '1'}
          />
          <div class="ghost mono" style="font-size: 10px; margin-top: 2px">
            {#if live}
              Below 3 km/h: pedal is the synth input (rev). Above 8 km/h: motor torque takes over. Blend in between.
            {:else}
              Release to spring back. Affects intake noise + filter cutoff.
            {/if}
          </div>
        </div>
      </div>
    </div>

    <div style="display: flex; flex-direction: column; gap: 12px">
      <div class="frame">
        <div class="frame__head">Profile</div>
        <div class="frame__body" style="display: flex; flex-direction: column; gap: 6px">
          {#each PROFILE_IDS as id}
            {@const p = ENGINE_PROFILES[id]}
            {@const sel = id === profileId}
            <button
              class={'btn btn--sm ' + (sel ? '' : 'btn--ghost')}
              style={'justify-content: space-between;' + (sel ? ' background: var(--dc-surface-3); border-color: var(--dc-border-hi);' : '')}
              onclick={() => (profileId = id as keyof typeof ENGINE_PROFILES)}
            >
              <span>{p.name}</span>
              <span class="ghost mono" style="font-size: 10px">{p.idle}–{p.redline}</span>
            </button>
          {/each}
        </div>
      </div>

      <div class="frame">
        <div class="frame__head">Source</div>
        <div class="frame__body" style="display: flex; flex-direction: column; gap: 8px">
          <div class="row-flex" role="tablist" style="gap: 2px; background: var(--dc-surface); border: 1px solid var(--dc-border); border-radius: 4px; padding: 3px">
            {#each [{ id: 'sim' as const, label: 'Sim slider' }, { id: 'live' as const, label: 'Live signal' }] as t}
              <button
                class={'btn btn--sm ' + (source === t.id ? '' : 'btn--ghost')}
                style={'flex: 1; justify-content: center;' + (source === t.id ? ' background: var(--dc-surface-3); border-color: var(--dc-border-hi);' : '')}
                onclick={() => (source = t.id)}
              >
                {t.label}
              </button>
            {/each}
          </div>

          {#if source === 'sim'}
            <div>
              <div class="row-flex" style="justify-content: space-between; margin-bottom: 4px">
                <span class="muted mono" style="font-size: 11px">Idle ←→ Redline</span>
                <span class="mono" style="font-size: 11px; color: var(--dc-text-dim)">{simRpm.toLocaleString()}</span>
              </div>
              <input
                type="range"
                min={profile.idle} max={profile.redline} step="10"
                value={Math.min(profile.redline, Math.max(profile.idle, simRpm))}
                oninput={(e) => (simRpm = Number((e.target as HTMLInputElement).value))}
                style="width: 100%"
              />
            </div>
          {:else}
            <div style="display: flex; flex-direction: column; gap: 6px">
              <div class="row-flex" style="justify-content: space-between">
                <span class="muted mono" style="font-size: 11px">vehicleSpeed</span>
                <span class="mono" style:font-size="13px" style:color={app.connected ? 'var(--dc-text)' : 'var(--dc-text-fade)'}>
                  {app.connected ? `${liveSpeed.toFixed(1)} km/h` : 'not connected'}
                </span>
              </div>
              <div class="row-flex" style="justify-content: space-between">
                <span class="muted mono" style="font-size: 11px">
                  motorTorque
                  <span class="ghost mono" style="font-size: 10px; margin-left: 4px">
                    {liveTorque < -0.05 ? '· regen' : liveTorque > 0.05 ? '· drive' : '· coast'}
                  </span>
                </span>
                <span class="mono" style:font-size="13px" style:color={app.connected ? 'var(--dc-text)' : 'var(--dc-text-fade)'}>
                  {app.connected ? `${liveTorque >= 0 ? '+' : ''}${(liveTorque * 100).toFixed(0)}%` : '—'}
                </span>
              </div>
              {#if app.connected}
                <div style="position: relative; height: 6px; background: var(--dc-bg); border: 1px solid var(--dc-border); border-radius: 3px; overflow: hidden">
                  <div style="position: absolute; left: 50%; top: 0; bottom: 0; width: 1px; background: var(--dc-border-hi)"></div>
                  <div
                    style:position="absolute" style:top="0" style:bottom="0"
                    style:left={liveTorque >= 0 ? '50%' : (50 + liveTorque * 50) + '%'}
                    style:width={Math.abs(liveTorque) * 50 + '%'}
                    style:background={liveTorque >= 0 ? 'var(--dc-accent)' : 'var(--dc-info-text)'}
                  ></div>
                </div>
              {:else}
                <div class="ghost mono" style="font-size: 10px">Connect a device to hear it move.</div>
              {/if}
              <label class="row-flex" style="gap: 6px; font-size: 12px; color: var(--dc-text-dim)">
                <input type="checkbox" bind:checked={muteBelow} />
                Mute below 5 km/h
              </label>
            </div>
          {/if}
        </div>
      </div>

      <div class="frame">
        <div class="frame__head">Master</div>
        <div class="frame__body" style="display: flex; align-items: center; gap: 8px">
          {#if volume === 0}<Icon name="mute" size={16} />{:else}<Icon name="volume" size={16} />{/if}
          <input
            type="range" min="0" max="1" step="0.01"
            value={volume}
            oninput={(e) => (volume = Number((e.target as HTMLInputElement).value))}
            style="flex: 1"
          />
          <span class="mono" style="font-size: 11px; color: var(--dc-text-dim); min-width: 32px; text-align: right">
            {Math.round(volume * 100)}
          </span>
        </div>
      </div>

      <div class="frame">
        <div class="frame__body" style="font-size: 11px; color: var(--dc-text-fade); line-height: 1.5">
          <strong style="color: var(--dc-text-dim)">How it works.</strong>
          {profile.harmonics.length} {profile.wave} oscillators stacked at integer harmonics, run through
          a lowpass that opens with load. RPM follows <span class="mono">vehicleSpeed</span>;
          load is your pedal at standstill and <span class="mono">|motorTorque|</span> on the move,
          crossfaded between 3 and 8 km/h. Plus a pink-noise intake layer and sub-bass rumble.
        </div>
      </div>
    </div>
  </div>
</div>
