# Implementation plan

## 1. Pure policy and configuration

- [ ] Add a small Server policy header for complementary visibility, single-state glyph mapping, and punctuation-with-input-mode text mapping.
- [ ] Add focused unit tests for toolbar-on suppression, preference-off suppression, IME inactive suppression, punctuation/input-mode mapping, single-state glyph mapping, and invalid events.
- [ ] Add `general.caret_state_indicator = true` to both shipped config templates.
- [ ] Add Server config load/get/set support and preserve the key through template merging.
- [ ] Add the field to both settings snapshot writers and both config-update dispatchers.

Validation:

```powershell
cmake --build server/build-release --config Release --target MetasequoiaImeServerTests
ctest --test-dir server/build-release -C Release --output-on-failure
```

Rollback point: policy/config changes compile and tests pass before window or TSF work begins.

## 2. Native indicator surface

- [ ] Add a dedicated native Server painter for one glyph; reuse existing theme and DPI helpers without adding a WebView2 or retained-mode scene.
- [ ] Create one no-activate layered tool window in the existing Server window lifecycle.
- [ ] Implement per-show DPI sizing, above-caret placement with text/caret clearance, work-area flipping/clamping, 1500 ms restartable hide timer, and immediate lifecycle/config hides.
- [ ] Integrate with the existing small-window topmost ordering without adding `WS_EX_TOPMOST` at creation.
- [ ] Handle theme, display, and DPI changes without taking focus or accepting pointer actions.

Validation:

```powershell
cmake --build server/build-release --config Release --target MetasequoiaImeServer
```

Manual probe: show each glyph on 100%, 125%, 150%, and 200% scaling and on a secondary monitor; verify the caret remains unobscured and the HWND never activates.

Rollback point: the Server window can be disabled by config without touching TSF state or candidate rendering.

## 3. TSF switch notification path

- [ ] Restore the existing `IMESwitch`, `PuncSwitch`, and `DoubleSingleByteSwitch` sends only for real compartment edges, not initialization/focus/reconnect synchronization.
- [ ] Trace and cover existing input-mode, input-scheme, and punctuation shortcut paths; add the smallest compatible event mapping for `Ctrl+Shift+F` simplified/traditional changes without changing shortcut behavior.
- [ ] Resolve the current selection's physical caret anchor in an edit session on the TSF owner thread at switch time. Clone rather than mutate the real selection; prefer adjacent one-character extents over the host's collapsed-range extent, derive the caret from the previous character's right edge or next character's left edge, and fail closed on clipped, degenerate, cross-line, bidi/overlapping, or unavailable geometry.
- [ ] Populate the existing switch event's `keycode` and `point[2]` fields; keep protocol struct layout and event ids unchanged.
- [ ] Preserve existing complete `StatusSnapshot` sends as the authoritative state synchronization path.
- [ ] Ensure stale focus sessions, missing layouts, empty/non-editable contexts, and failed pipe sends silently suppress only the badge notification.

Validation:

```powershell
cmake --build windows/build --config Release --target windows_ipc_contract
cmake --build windows/build --config Release
```

Also build x86 using the repository's Windows preset/script because the shared wire struct is used by both architectures.

Rollback point: comment/removal of explicit switch sends restores current behavior without changing the status snapshot path.

## 4. Server event dispatch

- [ ] Keep existing active-client and activation-epoch gates for switch events.
- [ ] Route accepted language tasks to the indicator as a single input-mode glyph; route punctuation tasks with punctuation plus current input mode and anchor; keep width and character-set tasks as single-state text.
- [ ] Hide on client suspension/deactivation, focus-session replacement, Server shutdown, preference disable, or floating-toolbar enable.
- [ ] Do not trigger from `StatusSnapshot`, `FocusRestored`, activation, or plain focus changes.

Validation:

- [ ] Unit tests prove the pure trigger/visibility mapping.
- [ ] Manual focus churn confirms stale events cannot display beside a newly focused application.
- [ ] Manual Telegram testing confirms the transient cue shares the correct text line with the caret when the normal composition-range candidate window is correctly positioned; cover mid-line, line start/end, wrapped lines, empty input, and mixed-direction text.
- [ ] Manual Windows Terminal testing confirms the indicator appears when a valid anchor is available, or fails closed without affecting text input when it is not.

## 5. Settings UI

- [ ] Add `关闭悬浮工具栏时显示状态提示` to the existing floating-toolbar settings section and verify the installed/local RC package contains the updated settings assets.
- [ ] Bind it to `general.caret_state_indicator` through existing toggle/config-sync helpers.
- [ ] Ensure snapshot refresh and external config changes update the switch.
- [ ] Keep the control available while the toolbar is enabled so users can preconfigure fallback behavior; its label explains that it applies only when the toolbar is off.

Validation:

```powershell
Set-Location ui-html/webview2/settings/ime-settings
corepack enable
pnpm install --frozen-lockfile
pnpm build
pnpm test
```

## 6. Full verification

- [ ] Run clang-format 18.1.8 dry-run on changed C++ files.
- [ ] Run Server tests and x64/x86 TSF builds.
- [ ] Run settings TypeScript build/tests.
- [ ] Verify contract/shared-copy checks remain clean; no WebView contract change is expected.
- [ ] Manually verify Telegram, Win32 EDIT, Chromium/Electron, Office-style editors, and Windows Terminal.
- [ ] Build and install a local RC package using the locally installed Inno Setup 6 compiler at the machine-specific path, supplied only as a command argument and never committed.
- [ ] For each host, check Shift/language-bar mode switching, full/half switching, punctuation switching, rapid repeated switching, focus loss during the 1.5 s timer, IME switch-away, Server restart, and multi-monitor DPI movement.
- [ ] Confirm the badge never appears while the floating toolbar is enabled and that turning both settings off shows neither surface.

## 7. Candidate-skin palette for the badge (follow-up)

- [ ] Reuse the candidate presenter's effective surface, border and final text-color resolution for built-in skins, the resolved candidate light/dark mode, and custom `skin.toml` color overrides; avoid a second drifting palette table.
- [ ] Keep punctuation badges compact and synchronized between native rendering and the settings preview: 64 DIP punctuation slot + 2 DIP gap + fixed 30 DIP input-mode slot = 96 DIP total, with identical Chinese/ASCII outer dimensions and unchanged 20 DIP font size.
- [ ] Confirm that switching candidate skin/theme is reflected at the next badge display, including when the floating toolbar has a different theme.

Validation: run clang-format 18.1.8 dry-run on changed C++, build Server and Server tests, run `ctest --test-dir server/build-release -C Release --output-on-failure`, and manually inspect built-in/custom skins in light/dark variants at supported DPI scales.

## 8. Separate settings card and preview (follow-up)

- [ ] On the existing floating-toolbar page, move the caret-indicator switch and position selector into a separate card; keep the floating-toolbar card/preview focused on the toolbar.
- [ ] Add a compact browser-side preview of the actual indicator geometry and candidate-skin palette without adding a sidebar page or new persisted configuration. Show all four samples in one row when space permits, switch directly to two rows of two when constrained, never allow four rows, and include a simulated text caret in a clearly separated visual area for every sample. Drive all four preview positions immediately from the existing position dropdown and configuration snapshot.
- [ ] Reuse the settings page's candidate-skin/theme preview state rather than adding a second skin catalog or runtime state machine.
- [ ] Add focused UI tests for card separation, four-column/two-column responsive layout, visually separated samples, one simulated caret per sample, all four selector positions, initial/refreshed snapshot application, and preview updates when candidate skin/theme/text color changes.

Validation: run the settings TypeScript build/tests and package the generated `dist`; manually compare the preview with the native indicator for built-in light/dark skins.

## Risk notes

- TSF text extent must be requested in an edit session and all COM objects must stay on their owner thread.
- Initialization callbacks can resemble user switches; explicit edge tracking is required to avoid startup/focus notifications.
- Server window creation must follow the existing non-topmost-first lifecycle because `uiAccess` and small-window z-order are timing-sensitive.
- The repository has unrelated uncommitted files; only explicit task paths may be staged later.
