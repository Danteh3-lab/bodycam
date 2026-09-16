# NOVA — Design

NOVA's visual identity is a rounded, dark control panel with cyan status accents and
semantic ESP colours. This document mirrors the exact values used at runtime.

> **Source of truth:** the C++ theme tokens in `dll/src/Theme.hpp` and the layout
> metrics in `dll/src/NovaUi.cpp`. DESIGN.md does not generate C++; it documents
> the values and the rationale behind them. When the two disagree, the code wins.

---

## 1. Palette

All values are straight RGBA, drawn without gamma tricks. The ESP palette is
deliberately high-contrast so it stays readable over the game's bright frames.

### Surfaces

| Token        | RGBA                  | Use                                  | Rationale |
|--------------|-----------------------|--------------------------------------|-----------|
| `kSurface0`  | `10, 11, 13, 246`     | Window background                    | Near-black, slightly blue; 96% opaque keeps the game faintly visible as context without hurting text contrast. |
| `kSurface1`  | `16, 18, 21, 255`     | Child panels (nav, content)          | One step lighter so the frame reads as layered. |
| `kSurface2`  | `24, 27, 31, 255`     | Frames, buttons, sliders             | Interactive surfaces. |
| `kSurface3`  | `33, 37, 42, 255`     | Hovered frames                       | Hover is a brightness step, not a hue change, so it never competes with semantic colours. |
| `kBorder`    | `44, 49, 56, 190`     | Panel borders, separators            | 1 px structure without a hard grid. |

### Text

| Token        | RGBA                  | Use                                  |
|--------------|-----------------------|--------------------------------------|
| `kText`      | `236, 239, 242, 255`  | Primary labels and values            |
| `kTextDim`   | `150, 158, 168, 255`  | Help markers, secondary status       |
| `kTextFaint` | `104, 112, 122, 255`  | Disabled copy                        |

### Accents and semantics

| Token        | RGBA                  | Use                                  | Rationale |
|--------------|-----------------------|--------------------------------------|-----------|
| `kAccent`    | `64, 214, 224, 255`   | Checkmarks, active sliders, group headings, nav cursor | Cyan reads as "instrumentation", not as a warning, and stays distinct from the ESP blue. |
| `kAccentDim` | `38, 129, 137, 255`   | Slider tracks, active buttons        | Same hue at lower energy prevents flicker between states. |
| `kSuccess`   | `74, 210, 118, 255`   | Ready state                          | Confirmation, also used for the health ramp's top band. |
| `kWarn`      | `255, 176, 46, 255`   | Resolving / empty roster             | Amber, never used for destructive meaning. |
| `kDanger`    | `255, 76, 76, 255`    | Invalid offsets, renderer failure    | Reserved for fail-closed conditions only. |
| `kEspEnemy`  | `255, 62, 62, 255`    | Enemy boxes, skeleton, head dot      | Red = hostile, matches genre convention. |
| `kEspTeam`   | `82, 150, 255, 255`   | Team boxes                           | Blue = friendly. |
| `kEspDrone`  | `0, 220, 255, 255`    | `[DRONE]` label                      | Cyan distinguishes the drone entity from the enemy red. |
| `kEspInfo`   | `220, 220, 220, 255`  | Distance text                        | Neutral so it never implies threat. |
| `kEspOutline`| `0, 0, 0, 255`        | ESP outline pass                     | Black outline keeps coloured shapes legible on white surfaces. |

**Never colour alone.** Every runtime state also carries a text label
(`RuntimeStateName`, `RuntimeStateDescription`), and the health indicator in the
header renders a dot *and* the state name. The same rule applies to the
settings status ("Saved 12:01:33", "Save failed", ...).

---

## 2. Typography

| Role      | Face        | Base size (logical px @ 96 DPI) | Notes |
|-----------|-------------|----------------------------------|-------|
| Controls  | Segoe UI    | 15                               | Windows default; excellent hinting at small sizes. Missing file falls back to the ImGui default face. |
| Headings  | Bahnschrift | 19                               | Semi-condensed; gives group headings a distinct voice without a second colour. |
| Diagnostics | Consolas  | 14                               | Monospaced counters and offsets align vertically. |

* `ImGuiStyle::FontScaleDpi` follows `ImGui_ImplWin32_GetDpiScaleForHwnd`, so the
  panel is correct on 125%/150% displays and after a monitor move.
* `theme::PushHeading()` scales the current font by 1.18; group headings use
  0.95 inside that; diagnostics use 0.92 of the current size.
* The ESP uses `Visuals → Text size` (0.6×–2.5×) over the current font; the
  outline pass draws four 1 px offsets so text stays readable on any backdrop.

---

## 3. Layout

The panel is a single window with a fixed five-section structure:

```
+---------------------------------------------------------------+
| search…                [Clear]            [ ESP: ON ]          |
| ● Runtime: Ready | Saved 12:01:33 | 141 FPS | recovering…      |
+----------+----------------------------------------------------+
| Overview |  (particle field, clipped to this window)           |
| Players  |                                                    |
| Visuals  |  content for the selected section                   |
| Overlay  |                                                    |
| Diagnostics                                                 |
+----------+----------------------------------------------------+
```

* Default size `760 × 520`; minimum `640 × 420`; never collapsed.
* Nav column is fixed at `150 px`; content fills the remainder.
* Labels sit in a `170 px` column so toggles and sliders left-align.
* Rounding: window `12`, child `8`, frame `6`, popup `8`, scrollbar `12`.
* Padding: window `12/12`, frame `9/5`, item spacing `10/7`, item inner `6/4`.
* Section order is stable and persisted (`menu.section`). The menu position is
  persisted in ImGui coordinates relative to the game client area, which is
  exactly how it is restored.

---

## 4. Runtime states

The header health indicator renders one of these labels (text, not colour):

| State text                    | Colour  | Meaning |
|-------------------------------|---------|---------|
| `Starting`                    | accent  | Worker threads starting. |
| `Waiting for match`           | dim     | Window found, no validated world yet. |
| `Resolving offsets`           | amber   | Locating/validating the read-only chain. |
| `Ready`                       | green   | World, camera, roster and name pool validated. |
| `Ready` (roster 0)            | amber   | Valid world, empty roster. |
| `Recovering after map change` | amber   | Previously ready, caches cleared. |
| `Offsets invalid`             | red     | Known build mismatch; ESP stays disabled. |
| `Renderer failure`            | red   | D3D11 device could not be recovered; NOVA unloads. |

ESP is drawn only when the state is `Ready` **and** the latest snapshot is valid.
Invalid snapshots render nothing rather than stale pointers.

---

## 5. Particle field

The particle field is NOVA's only decorative element.

* Contained: clipped to the menu window, drawn behind the panel content.
* Disabled when Windows client-area animations are off (`SPI_GETCLIENTAREAANIMATION`)
  or when **Overlay → Reduce motion** is enabled.
* Bounded: 100 particles at start, +1 per 0.5 s up to 150; wrap-around motion with
  a soft mouse repulsion (50–150 px influence).
* Colour: `120, 220, 228` with a 30–45% alpha pulse; radius 1.8 px.

Rationale: one signature visual keeps the panel recognisable without adding
motion that competes with the ESP or harms accessibility.

---

## 6. Window behaviour

* Top-level `WS_POPUP` window bound to the target process; the largest visible
  game window is selected (not the foreground window).
* DWM composition (`DwmExtendFrameIntoClientArea`) provides transparency; the
  swap chain is `DXGI_SWAP_EFFECT_DISCARD` with `B8G8R8A8_UNORM`.
* While the menu is hidden the window carries
  `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE`, and the layered
  attributes are initialised at full opacity
  (`SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA)`) every time that
  style is applied, including startup. This keeps the overlay visible while
  Windows skips it for hit-testing; `HTTRANSPARENT` alone only forwards to
  windows on the same thread, so the layered-plus-transparent path is what
  gives the game window mouse input. The window is created without
  `WS_VISIBLE` and shown only after the style is set; startup fails (rather
  than showing an unstyled window) if any style call fails.
* Cursor: the window class cursor is `NULL`, `WM_NCHITTEST` answers
  `HTTRANSPARENT` and `WM_SETCURSOR` blanks the system cursor, so the game owns
  the cursor while the menu is hidden.
* While the menu is open the layering/transparency styles are dropped and the
  window takes focus so search, keyboard navigation and sliders work; ImGui
  draws the software cursor, and focus is returned to the game on close only
  when the overlay actually holds it.
* Alt-Tab away or minimize hides the overlay entirely (no ESP over other apps).
* VSync (`Present(1, 0)`) keeps the overlay at the monitor refresh rate.
* Exclusive fullscreen is unsupported by design; windowed and borderless
  windowed are the supported modes.

---

## 7. Accessibility checklist

- Text contrast: `kText` on `kSurface0` is ~14:1.
- All state communicated by text plus colour.
- Motion can be disabled by the user or the OS preference.
- Keyboard navigation is enabled (`ImGuiConfigFlags_NavEnableKeyboard`).
- Search has an explicit **Clear** button; an empty result set says so in text.
- A corrupt settings file never breaks startup: it is backed up and defaults
  are restored with a visible status message.
