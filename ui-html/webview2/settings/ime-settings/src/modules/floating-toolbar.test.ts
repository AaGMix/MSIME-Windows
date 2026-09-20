/// <reference types="node" />
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { expect, it } from 'vitest';
import partial from '../partials/floating-toolbar.html?raw';

const styles = readFileSync(fileURLToPath(new URL('../styles/modules/floating-toolbar.css', import.meta.url)), 'utf8');

it('keeps toolbar and caret controls in separate cards with separate previews', () => {
  const toolbarCard = partial.match(/<div class="section floating-toolbar-card">([\s\S]*?)<div class="section caret-state-indicator-card">/)?.[1] ?? '';
  const caretCard = partial.match(/<div class="section caret-state-indicator-card">([\s\S]*?)<div class="section floating-toolbar-appearance">/)?.[1] ?? '';

  expect(toolbarCard).toContain('id="ftbToggleBtn"');
  expect(toolbarCard).toContain('id="ftbPreviewHost"');
  expect(toolbarCard).not.toContain('caretStateIndicator');
  expect(caretCard).toContain('id="caretStateIndicatorToggleBtn"');
  expect(caretCard).toContain('id="caretStateIndicatorPositionBtn"');
  expect(caretCard).toContain('id="caretStatePreviewHost"');
  expect(caretCard).not.toContain('id="ftbPreviewHost"');
  expect(caretCard).toContain('role="img" aria-label="光标状态提示预览：中、中文标点和中文模式、全角、简体"');
  expect(caretCard).toContain('class="cand-preview" aria-hidden="true"');
});

it('shows the four runtime samples and preserves fixed punctuation slots', () => {
  const preview = partial.match(/<div class="candidate caret-state-preview-host"[\s\S]*?<\/div>\s*<\/div>\s*<\/div>/)?.[0] ?? '';
  expect(preview).toContain('>中</div>');
  expect(preview).toContain('aria-label="，。  中"');
  expect(preview).toContain('>全</div>');
  expect(preview).toContain('>简</div>');
  expect(preview.match(/class="caret-state-badge(?: |")/g)).toHaveLength(4);
  expect(styles).toMatch(/\.caret-state-badge\s*\{[^}]*width:\s*30px;[^}]*height:\s*30px;/);
  expect(styles).toMatch(/\.caret-state-badge-punctuation\s*\{[^}]*grid-template-columns:\s*72px 30px;[^}]*column-gap:\s*2px;[^}]*width:\s*104px;/);
  expect(styles).toMatch(/\.caret-state-preview-host\s*\{[^}]*flex-wrap:\s*wrap;/);
});
