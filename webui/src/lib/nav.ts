import type { ViewId } from './store.svelte';
import type { IconName } from './icons';

export type NavItem = {
  id: ViewId;
  label: string;
  icon: IconName;
  group: string;
};

export const NAV_ITEMS: NavItem[] = [
  { id: 'events',    label: 'Events',        icon: 'bolt',     group: 'Automations' },
  { id: 'scripts',   label: 'Automations',   icon: 'scripts',  group: 'Automations' },
  { id: 'dbc',       label: 'DBC',           icon: 'dbc',      group: 'Automations' },
  { id: 'ai',        label: 'AI assistant',  icon: 'sparkle',  group: 'Automations' },
  { id: 'gallery',   label: 'Gallery',       icon: 'gallery',  group: 'Automations' },
  { id: 'dashboard', label: 'Dashboard',     icon: 'dash',     group: 'Signals' },
  { id: 'trace',     label: 'Trace',         icon: 'trace',    group: 'Reverse eng.' },
  { id: 'capture',   label: 'Capture',       icon: 'capture',  group: 'Reverse eng.' },
  { id: 'settings',  label: 'Settings',      icon: 'settings', group: 'Device' },
  { id: 'tesla',     label: 'Tesla-BLE',     icon: 'tesla',    group: 'Device' },
  { id: 'engine',    label: 'Engine sound',  icon: 'engine',   group: 'Fun' },
];
