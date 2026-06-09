// Inline-SVG icon registry — stroke-only, 24×24 grid, 1.75 stroke.
// Mirrors the design bundle's icons.jsx so the bundle stays tiny (no Lucide).
export type IconName =
  | 'scripts' | 'dbc' | 'dash' | 'trace' | 'capture' | 'settings' | 'tesla'
  | 'gallery' | 'log' | 'play' | 'pause' | 'stop' | 'down' | 'up' | 'filter'
  | 'power' | 'ble' | 'wifi' | 'sim' | 'check' | 'x' | 'chevD' | 'search'
  | 'plug' | 'trash' | 'engine' | 'volume' | 'mute' | 'events' | 'ai'
  | 'bolt' | 'sparkle' | 'tool' | 'radio';

export const ICON_PATHS: Record<IconName, string[]> = {
  scripts:  ['M8 4h9l3 3v13H8z', 'M4 8h12v12H4z', 'M7 12h6', 'M7 15h4'],
  dbc:      ['M4 6 C4 3.3,20 3.3,20 6 C20 8.7,4 8.7,4 6', 'M4 6v4a8 2 0 0 0 16 0v-4', 'M4 10v5a8 2 0 0 0 16 0v-5', 'M4 15v5a8 2 0 0 0 16 0v-5'],
  dash:     ['M3 18a9 9 0 0 1 18 0', 'M12 18l5-6'],
  trace:    ['M3 12h3l2-6 4 12 2-6h7'],
  capture:  ['M5 7h3l1-2h6l1 2h3v12H5z', 'M12 16a3 3 0 1 0 0-6 3 3 0 0 0 0 6z'],
  settings: [
    'M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z',
    'M19 12a7 7 0 0 0-.1-1.2l2-1.5-2-3.4-2.3.9a7 7 0 0 0-2.1-1.2L14 3h-4l-.5 2.6a7 7 0 0 0-2.1 1.2l-2.3-.9-2 3.4 2 1.5A7 7 0 0 0 5 12c0 .4 0 .8.1 1.2l-2 1.5 2 3.4 2.3-.9a7 7 0 0 0 2.1 1.2L10 21h4l.5-2.6a7 7 0 0 0 2.1-1.2l2.3.9 2-3.4-2-1.5c.1-.4.1-.8.1-1.2z'
  ],
  // Stylized T silhouette — top crossbar with end-drops + center stem.
  tesla:    ['M5 6h14', 'M12 6v14', 'M5 6v3', 'M19 6v3'],
  gallery:  ['M4 4h7v7H4z', 'M13 4h7v7h-7z', 'M4 13h7v7H4z', 'M13 13h7v7h-7z'],
  log:      ['M4 5h16', 'M4 10h16', 'M4 15h10', 'M4 20h12'],
  play:     ['M6 4l14 8-14 8z'],
  pause:    ['M7 4v16', 'M17 4v16'],
  stop:     ['M6 6h12v12H6z'],
  down:     ['M12 4v12', 'M7 12l5 5 5-5', 'M4 20h16'],
  up:       ['M12 20V8', 'M7 12l5-5 5 5', 'M4 4h16'],
  filter:   ['M4 5h16l-6 8v6l-4-2v-4z'],
  power:    ['M12 4v8', 'M7 7a7 7 0 1 0 10 0'],
  ble:      ['M8 7l8 10-4 3V4l4 3-8 10'],
  wifi:     ['M4 10a14 14 0 0 1 16 0', 'M7 13a10 10 0 0 1 10 0', 'M10 16a6 6 0 0 1 4 0', 'M12 19v.01'],
  sim:      ['M4 12a8 8 0 1 0 16 0 8 8 0 1 0-16 0', 'M12 8v4l3 2'],
  check:    ['M5 12l5 5 9-11'],
  x:        ['M6 6l12 12', 'M18 6L6 18'],
  chevD:    ['M6 9l6 6 6-6'],
  search:   ['M10 4a6 6 0 1 1 0 12 6 6 0 0 1 0-12z', 'M20 20l-5-5'],
  plug:     ['M9 3v6', 'M15 3v6', 'M6 9h12v3a6 6 0 0 1-6 6 6 6 0 0 1-6-6z', 'M12 18v3'],
  trash:    ['M4 7h16', 'M9 7V4h6v3', 'M6 7l1 13h10l1-13', 'M10 11v6', 'M14 11v6'],
  engine:   ['M5 14h2l1-3h8l1 3h2a1 1 0 0 1 1 1v3H4v-3a1 1 0 0 1 1-1z', 'M9 11V8h6v3', 'M12 5v3', 'M7 17v2', 'M17 17v2'],
  volume:   ['M5 9h3l5-4v14l-5-4H5z', 'M16 9a4 4 0 0 1 0 6'],
  mute:     ['M5 9h3l5-4v14l-5-4H5z', 'M17 9l4 6', 'M21 9l-4 6'],
  events:   ['M4 4h7v7H4z', 'M13 4h7v7h-7z', 'M4 13h7v7H4z', 'M13 13h7v7h-7z'],
  ai:       ['M12 3v3', 'M12 18v3', 'M3 12h3', 'M18 12h3', 'M5.6 5.6l2.1 2.1', 'M16.3 16.3l2.1 2.1', 'M5.6 18.4l2.1-2.1', 'M16.3 7.7l2.1-2.1', 'M12 8.5l1.4 2.6 2.6.4-1.9 1.8.5 2.7L12 14.7l-2.6 1.3.5-2.7-1.9-1.8 2.6-.4z'],
  bolt:     ['M13 3L4 14h6l-1 7 9-11h-6z'],
  sparkle:  ['M12 6l1.6 4.6L18 12l-4.4 1.4L12 18l-1.6-4.6L6 12l4.4-1.4z', 'M19 6l.6 1.4L21 8l-1.4.6L19 10l-.6-1.4L17 8l1.4-.6z'],
  tool:     ['M14.7 4.3a4 4 0 00-5.4 5.4l-6 6a2 2 0 102.8 2.8l6-6a4 4 0 005.4-5.4l-2.5 2.5-2.3-2.3z'],
  radio:    ['M3 9h18v9H3z', 'M3 9L20 3', 'M19.2 3A0.8 0.8 0 1 0 20.8 3A0.8 0.8 0 1 0 19.2 3', 'M6 12.5A2.5 2.5 0 1 0 11 13.5A2.5 2.5 0 1 0 6 13.5', 'M14 12h5', 'M14 15h5'],
};
