import { afterEach, beforeEach, expect, it, vi } from 'vitest';

// 捕获 setupConfigSync 注册的 configSnapshot 处理器后直接喂快照，验证回填守卫。
const handlers = vi.hoisted(() => new Map<string, (payload: { data?: Record<string, unknown> }) => void>());

vi.mock('../utils/host-messages', () => ({
  onHostMessage: (type: string, handler: (payload: { data?: Record<string, unknown> }) => void) => {
    handlers.set(type, handler);
  }
}));
vi.mock('./shared', () => ({
  applyCandidateArrange: vi.fn(),
  applyDropdownValue: vi.fn(),
  applyToggleState: vi.fn(),
  setFuzzyRuleOptionsDisabled: vi.fn(),
  setSmartPunctuationOptionsDisabled: vi.fn()
}));

import { applyToggleState } from './shared';
import { setupConfigSync } from './config-sync';

beforeEach(() => {
  handlers.clear();
  vi.clearAllMocks();
  vi.stubGlobal('document', {
    getElementById: () => ({}),
    querySelector: () => null,
    querySelectorAll: () => [],
    addEventListener: vi.fn()
  });
  vi.stubGlobal('window', { chrome: { webview: { postMessage: vi.fn() } } });
  setupConfigSync();
});

afterEach(() => vi.unstubAllGlobals());

it('backfills the word-to-character switch only from a boolean', () => {
  const snapshot = handlers.get('configSnapshot')!;
  snapshot({ data: { input: { word_to_character: true } } });
  expect(applyToggleState).toHaveBeenCalledWith('wordToCharacterToggleBtn', true);
  snapshot({ data: { input: { word_to_character: false } } });
  expect(applyToggleState).toHaveBeenLastCalledWith('wordToCharacterToggleBtn', false);

  vi.mocked(applyToggleState).mockClear();
  snapshot({ data: { input: { word_to_character: 'yes' } } });
  expect(applyToggleState).not.toHaveBeenCalled();
});
