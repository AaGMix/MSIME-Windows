# IME state switch indicator design

## Summary

Add a small native badge near the active text caret when the user changes one of these states:

- language/input mode: `中`, `英`, `日`
- punctuation mode: `，。  中` / `,.  中`, `，。  英` / `,.  英`, or `，。  日` / `,.  日`
- width mode: `全`, `半`
- simplified/traditional output mode: `简`, `繁`

The badge is a transient Server-owned popup. It is enabled only when the new preference is on and the existing floating toolbar is off. It hides 1.5 seconds after the latest accepted switch.

## Boundaries

### Windows TSF

Windows remains responsible for observing actual TSF compartment changes and resolving the caret rectangle on the owning TSF thread inside an edit session.

A switch notification must carry:

- switch category: language, width, punctuation, or an existing simplified/traditional output-mode transition
- resulting state
- physical screen anchor derived from `ITfContextView::GetTextExt`
- current focus token through the existing active Main-pipe session

The notification is emitted only after a real compartment edge. Initialization, focus restoration, reconnect snapshots, and unchanged writes establish/synchronize state but do not trigger a badge.

Caret lookup uses the focused context's current selection collapsed to the active end. It does not require an active composition and does not reuse COM objects across threads. If TSF cannot provide a usable extent, the switch still updates normal state synchronization but no badge is shown.

The existing append-only Main-pipe event kinds and fields are reused where they already represent the transition. Existing input-mode, input-scheme, and punctuation shortcut paths must be traced to their actual state-change events; the simplified/traditional transition must use the smallest existing event/state representation available. Their existing `keycode` result field is retained and their existing `point[2]` fields carry the resolved physical anchor. No struct layout, opcode, protocol version, or capability change is required unless repository evidence proves no existing representation can carry the transition.

### Server

Server validates that a switch event belongs to the active client/activation epoch before posting UI work. Language changes map directly to `中`, `英`, or `日`. Punctuation changes combine the resulting punctuation state with the latest authoritative input mode:

| Event / resulting state | Badge |
|---|---|
| Chinese input mode | `中` |
| English input mode | `英` |
| Japanese input mode | `日` |
| Chinese punctuation + Chinese mode | `，。  中` |
| Chinese punctuation + English mode | `，。  英` |
| Chinese punctuation + Japanese mode | `，。  日` |
| English punctuation + Chinese mode | `,.  中` |
| English punctuation + English mode | `,.  英` |
| English punctuation + Japanese mode | `,.  日` |
| Width switch | `全` / `半` |
| Character-set switch | `简` / `繁` |

A small policy module owns the pure decisions:

- whether the badge may show (`indicator_enabled && !floating_toolbar_enabled && ime_active`)
- event-to-glyph mapping, including shortcut/state categories and simplified/traditional output mode
- above-caret placement and fallback geometry
- rejection of invalid anchors/stale events

The indicator HWND is owned by `server/src/window/`, uses `WS_POPUP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED`, and never accepts input. Rendering uses the native Win32 text path regardless of the candidate/toolbar `ui_backend`; a short status surface does not justify another WebView2 controller, HTML page, or dedicated retained-mode scene.

Placement reuses the candidate-window monitor, DPI, and work-area helpers. The badge is positioned above the text extent with a vertical clearance that keeps the caret and following text unobscured; it flips below only when the work area leaves no room above, then clamps inside the anchor monitor's work area. It recomputes size from the anchor monitor DPI on every show. Single-state badges remain icon-sized; punctuation badges use identical outer dimensions and separately render punctuation and input mode into fixed left/right regions, so ASCII punctuation cannot shift or resize the trailing `中` / `英` / `日`. It is themed for light/dark mode and uses text rather than color as the sole state distinction.

For the badge palette, follow the active **candidate-window** skin and its resolved `theme_cand` light/dark variant, not the floating toolbar's `theme_ftb`. Use the same candidate surface, border and final text colors (including a configured text-color override) for built-in skins; custom skins use their `skin.toml` color overrides with the same fallback behavior as the candidate presenter. Keep the badge's existing Win32 rendering, geometry, font size and text layout; skin background images, CSS effects, shadows and corner shapes are out of scope. Resolve the palette when painting/showing so a subsequent switch reflects a skin or theme change; no extra WebView2 controller or settings field is needed.

Every accepted trigger replaces the glyph and anchor, shows without activation, raises through the existing small-window topmost lifecycle, and restarts a 1500 ms timer. Timer expiry hides the badge. IME deactivation, client suspension/focus-session replacement, enabling the floating toolbar, disabling the preference, and shutdown hide it immediately.

### Configuration and settings

Add one boolean under `[general]`, defaulting to true:

`caret_state_indicator = true`

Semantics: allow the transient caret badge when the persistent floating toolbar is disabled. It is not an independent always-show mode.

The key is added to:

- `installer/default_config/config.default.toml`
- `server/assets/config/config.toml`
- Server config loading/getter/setter
- both settings snapshot writers and update dispatchers
- the existing floating-toolbar settings page as a switch below the toolbar enable switch

Suggested label: `关闭悬浮工具栏时显示状态提示`.

Turning on the floating toolbar hides an active badge immediately. Turning off the toolbar does not itself show a badge; the next supported state change does.

The existing floating-toolbar settings page keeps both features discoverable without adding another sidebar destination, but separates them into two cards. The toolbar card owns only its switch and toolbar preview. A dedicated caret-indicator card owns the indicator switch, position selector, and a browser-side preview that uses the same candidate-skin preview tokens; it is a visual sample only and does not duplicate runtime state or positioning logic. Each of the four samples includes a simulated text caret and preserves the native badge-to-caret offset. A four-column grid keeps all samples on one row when the preview is wide enough; a container query switches directly to two columns below that width, guaranteeing two rows rather than allowing four one-item rows.

## Data Flow

```text
TSF compartment edge
  -> owner-thread edit session resolves collapsed selection text extent
  -> existing switch event + resulting state + point[2]
  -> Main pipe active-client/epoch gate
  -> Server event task
  -> indicator policy checks config, toolbar state, IME activity, anchor
  -> Server native D2D badge show/update
  -> 1500 ms timer or lifecycle/config event hides badge
```

Normal `StatusSnapshot` traffic remains the authoritative full-state synchronization path for the floating toolbar. It does not trigger the transient badge. Host-specific failures to expose a usable caret anchor, including Windows Terminal limitations, fail closed for the badge only and must not disrupt the existing input path.

## Compatibility

- No IPC ABI change: existing event ids and existing payload fields are reused.
- Older Server versions already understand the switch event ids and will only update their existing UI path.
- New Server behavior remains gated by its local config and active-client validation.
- Existing user configs gain the default-enabled key through template merge; actual display remains suppressed while the default-enabled floating toolbar is visible.
- The existing floating toolbar's behavior, defaults, layout, and component settings do not change.

## Trade-offs

- A native-only badge creates a small dedicated painter but avoids a new WebView2 environment/controller lifecycle and avoids duplicating HTML or a retained-mode scene for a single glyph.
- Explicit switch events are preferred over diffing full status snapshots because snapshots also occur during focus restoration, reconnect, and client handoff; diffing would produce false notifications.
- Failed caret lookup drops only the transient notification. Falling back to the last candidate position could show the badge beside the wrong editor, which is worse than omitting it.

## Rollback

The feature is isolated behind `caret_state_indicator`. Runtime rollback is disabling the key. Code rollback removes the dedicated presenter/window and switch-event emission while leaving existing status snapshots and floating-toolbar behavior unchanged.
