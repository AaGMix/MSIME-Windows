# Add IME input mode status indicator

## Goal

Help users know the active input mode before they type into the wrong mode, addressing issue #229 without duplicating the existing floating toolbar.

## Background

- Issue #229 asks for a visible Chinese/English status cue near the active typing area; its examples place a small colored symbol near the text cursor.
- The repository already ships a persistent floating toolbar. It is enabled by default, can be hidden, can be dragged, always shows the Chinese/English/Japanese state, and can optionally show punctuation and full-width/half-width state.
- The existing toolbar already has a settings page, configurable components, scale, font size, light/dark rendering, WebView2 and D2D implementations, and per-client status synchronization.
- Some users intentionally hide the persistent floating toolbar, so that toolbar alone does not satisfy the original near-cursor use case.

## Requirements

- Reuse the existing TSF caret/text-extent and focus-session information rather than creating a second source of cursor position or input-mode state. For transient switch events, do not trust a host's collapsed-selection `GetTextExt` as the primary anchor: measure cloned adjacent character ranges and recover the caret from the previous character's right edge or next character's left edge without mutating the actual selection; ambiguous, clipped, degenerate, or unavailable geometry must fail closed.
- Keep input-mode state authoritative in the existing TSF/Server flow; the indicator is presentation only and must not implement its own mode state machine.
- Match the active candidate-window skin's background, border, and text colors (including the resolved light/dark variant and custom skin color definitions); keep the indicator's size, placement, glyph layout, and lifetime unchanged. Do not import skin images, CSS effects, or shape changes.
- Do not change the behavior, defaults, or component set of the existing persistent floating toolbar unless the chosen UX explicitly requires it.
- The indicator must never take keyboard focus, intercept typing, or obscure the active text caret.
- Positioning and sizing must remain usable at 100%, 125%, 150%, and 200% DPI and when the focused editor is on another monitor.
- Hide stale UI after focus loss, input-method deactivation, or a superseding focus session.
- Add a separate compact indicator near the active text caret; do not repurpose or reposition the persistent floating toolbar.
- Show the indicator only when language/input mode, full-width/half-width mode, or Chinese/English punctuation mode changes; focusing an editable control without changing one of these states must not show it.
- Render the cue as a compact badge that shows only the changed input mode, but adds current input mode context when punctuation changes.
- Show only `中`, `英`, or `日` when language/input mode changes.
- Show `，。  中`, `，。  英`, or `，。  日` for Chinese punctuation, and `,.  中`, `,.  英`, or `,.  日` for English punctuation; spaces separate punctuation from input mode. Both punctuation variants use a 96 DIP badge composed of a 64 DIP punctuation slot, 2 DIP gap, and fixed 30 DIP input-mode slot, so the input-mode glyph stays at a fixed size and position.
- Continue to show `全` or `半` for width mode and `简` or `繁` for simplified/traditional output-mode changes.
- Automatically hide the indicator approximately 1.5 seconds after the latest show trigger; a newer trigger restarts the timeout.
- Place the indicator above the caret, with enough vertical clearance that it does not cover the caret or text immediately following it.
- Emit the indicator for the existing input-mode, input-scheme, and Chinese/English punctuation shortcut paths when they cause a real state change; do not invent a second shortcut mapping.
- Emit a suitable indicator when `Ctrl+Shift+F` changes the simplified/traditional Chinese output mode.
- Work in Windows Terminal in addition to Win32 EDIT, Chromium/Electron, and Office-style text hosts; host-specific caret/layout limitations must fail closed without breaking input.
- Provide a user-facing setting named for showing the caret indicator when the floating toolbar is disabled, persist it through both Server settings hosts and both shipped config templates, and include it in locally generated/installable builds so the installed settings page exposes the control.
- Keep the caret-indicator settings on the existing floating-toolbar page, but move its enable switch and position selector out of the floating-toolbar card into a separate caret-indicator card with its own preview; do not add another sidebar page. Lay out the four preview samples in one row when space permits and in two rows of two when constrained, never as four single-item rows; show a simulated text caret with every sample and give each sample a clearly separated visual area. Changing the existing position selector must immediately move every preview badge to the selected `top-left`, `top`, `top-right`, or `bottom` position relative to its caret.
- Enable the new setting by default for new and upgraded installations.
- Apply an automatic complementary visibility policy: show the caret indicator only when its setting is enabled and the persistent floating toolbar is disabled; never show both status surfaces at the same time.
- Allow users to disable both status surfaces by turning off both settings.

## Out of Scope

- Replacing or redesigning the existing persistent floating toolbar.
- Adding new input modes or changing mode-switch shortcuts; existing shortcut paths are in scope only for observing their resulting state changes.
- Moving input-state authority into UI HTML.
- Showing character-set, emoji, or other toolbar actions in the compact near-cursor indicator.

## Acceptance Criteria

- [ ] Changing language/input mode briefly shows only `中`, `英`, or `日`.
- [ ] Changing punctuation mode briefly shows punctuation plus the current input mode: `，。  中`, `，。  英`, or `，。  日` for Chinese punctuation; `,.  中`, `,.  英`, or `,.  日` for English punctuation.
- [ ] Chinese- and English-punctuation badges have identical 96 DIP outer dimensions using a 64 DIP punctuation slot, 2 DIP gap, and 30 DIP trailing input-mode slot; their trailing input-mode glyph remains at the same size and position despite the punctuation glyphs' different widths.
- [ ] Changing width mode briefly shows `全` or `半`, and changing simplified/traditional output mode briefly shows `简` or `繁`.
- [ ] The punctuation badge reflects the resulting punctuation state and current input mode, including punctuation modes that do not match the language mode.
- [ ] Focusing or switching between editable controls without changing input mode does not show the cue.
- [ ] With the floating toolbar enabled, the near-cursor cue stays hidden even when its own setting is enabled.
- [ ] With both settings disabled, neither status surface is shown.
- [ ] Each supported state change immediately updates and re-shows the cue without requiring Server restart or refocusing the application.
- [ ] The cue hides approximately 1.5 seconds after the latest supported state-change trigger.
- [ ] The cue is positioned above the focused editor's caret without covering the caret or the text immediately following it, follows the caret/monitor, and remains within the monitor work area across supported DPI scales.
- [ ] The cue does not activate, steal focus, consume keystrokes, or alter composition/candidate behavior.
- [ ] The cue is removed when the focus session becomes stale, the input method deactivates, or the feature is disabled.
- [ ] Existing input-mode, input-scheme, punctuation, and `Ctrl+Shift+F` simplified/traditional shortcuts produce the appropriate cue when their state changes.
- [ ] The cue works in Telegram, Windows Terminal, Win32 EDIT, Chromium/Electron, and Office-style focus transitions, or fails closed without disrupting input when the host cannot expose a usable caret anchor. Telegram's collapsed-selection extent must not place the cue above the real text line when its normal composition-range candidate anchor is correct.
- [ ] The installed/local RC settings page exposes and persists the caret-indicator setting.
- [ ] The floating-toolbar settings page presents the caret indicator in a separate card containing its enable switch, position selector, and a skin-aware preview of `中`, `，。  中`, `全`, and `简`; each sample includes a simulated text caret in a clearly separated visual area, and the samples use one row when space permits or two rows of two when constrained, never four rows. Changing the position selector immediately moves all four preview badges to the selected position relative to their carets, including initial and refreshed configuration snapshots; the floating-toolbar card and preview contain only floating-toolbar controls/content.
- [ ] Existing floating-toolbar behavior and settings continue to work unchanged.
- [ ] The badge uses the current candidate skin's resolved background, border, and text colors in light/dark modes, including custom skin colors and the configured candidate text-color override; switching skins does not change its existing geometry or glyph alignment.
- [ ] Automated checks cover visibility/state policy, configuration persistence, shortcut event mapping, and anchor placement; manual checks cover all supported host categories and the installed RC package.

## Open Product Decisions

None.
