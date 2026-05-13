export const isMac   = typeof navigator !== 'undefined' && /Mac/.test(navigator.platform);
export const isTouch = typeof window   !== 'undefined' && window.matchMedia('(pointer: coarse)').matches;
export const modKey  = isMac ? '⌘' : 'Ctrl+';
