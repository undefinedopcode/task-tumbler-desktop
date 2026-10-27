import { useProp } from '../bridge/hooks'

export function usePlatform(): string {
  return useProp('shell', 'platform', 'linux')
}

// "⌘N" on a Mac, "Ctrl+N" elsewhere.
export function keys(platform: string, combo: string): string {
  if (platform !== 'mac') return combo.replace('Mod', 'Ctrl')
  return combo.replace('Mod+', '⌘').replace('Shift+', '⇧').replace('Alt+', '⌥')
}

export function isMod(e: KeyboardEvent | React.KeyboardEvent, platform: string): boolean {
  return platform === 'mac' ? e.metaKey : e.ctrlKey
}
