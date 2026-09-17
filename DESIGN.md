# MYTHOS — Design

MYTHOS's visual identity is a rounded, dark control panel with restrained oracle-indigo status accents and
semantic ESP colours. This document mirrors the exact values used at runtime.

> **Source of truth:** the C++ theme tokens in `dll/src/Theme.hpp` and the layout
> metrics in `dll/src/MythosUi.cpp`. DESIGN.md does not generate C++; it documents
> the values and the rationale behind them. When the two disagree, the code wins.

---

## 1. Palette

All values are straight RGBA, drawn without gamma tricks. The ESP palette is
deliberately high-contrast so it stays readable over the game's bright frames.

### Surfaces

| Token        | RGBA                  | Use                                  | Rationale |
|--------------|-----------------------|--------------------------------------|-----------|
| `kSurface0`  | `13, 11, 19, 246` (`#0D0B13`) | Window background              | Oracle-black, 96% opaque so the game remains faintly visible as context without hurting text contrast. |
| `kSurface1`  | `20, 17, 29, 255` (`#14111D`) | Child panels (nav, content)    | A violet-black layer that makes the frame read as dimensional. |
| `kSurface2`  | `30, 26, 42, 255` (`#1E1A2A`) | Frames, buttons, sliders       | Interactive surfaces. |
| `kSurface3`  | `43, 36, 59, 255` (`#2B243B`) | Hovered frames                 | Hover is a restrained indigo brightness step, never competing with semantic colours. |
| `kBorder`    | `66, 55, 84, 190` (`#423754`)  | Panel borders, separators      | 1 px violet structure without a hard grid. |

### Text

| Token        | RGBA                  | Use                                  |
|--------------|-----------------------|--------------------------------------|
| `kText`      | `236, 239, 242, 255`  | Primary labels and values            |
| `kTextDim`   | `164, 156, 177, 255` (`#A49CB1`) | Help markers, secondary status |
| `kTextFaint` | `112, 103, 125, 255` (`#70677D`) | Disabled copy                  |

### Accents and semantics

| Token        | RGBA                  | Use                                  | Rationale |
|--------------|-----------------------|--------------------------------------|-----------|
| `kAccent`    | `177, 144, 255, 255` (`#B190FF`) | Checkmarks, active sliders, group headings, nav cursor | Oracle-indigo is the MYTHOS signature: luminous enough for focus, quiet enough to leave ESP semantics dominant. |
| `kAccentDim` | `105, 81, 159, 255` (`#69519F`)  | Slider tracks, active buttons        | A dim indigo companion prevents flicker between states. |
| `kSuccess`   | `74, 210, 118, 255`   | Ready state                          | Confirmation, also used for the health ramp's top band. |
| `kWarn`      | `255, 176, 46, 255`   | Resolving / empty roster             | Amber, never used for destructive meaning. |
| `kDanger`    | `255, 76, 76, 255`    | Invalid offsets, renderer failure    | Reserved for fail-closed conditions only. |
| `kEspEnemy`  | `255, 62, 62, 255`    | Enemy boxes, skeleton, head dot      | Red = hostile, matches genre convention. |
| `kEspTeam`   | `82, 150, 255, 255`   | Team boxes                           | Blue = friendly. |
| `kEspDrone`  | `0, 220, 255, 255`    | `[DRONE]` label                      | Cyan distinguishes the drone entity from the enemy red. |
| `kEspOccluded`| `158, 158, 158, 255` | Dim occluded players                 | Neutral grey reads as "known but not visible" without implying threat. |
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

The panel is a single window with a fixed six-section structure:

```
+---------------------------------------------------------------+
| search…                [Clear]            [ ESP: ON ]          |
| ● Runtime: Ready | Saved 12:01:33 | 141 FPS | recovering…      |
+----------+----------------------------------------------------+
| Overview |  (particle field, clipped to this window)           |
| Players  |                                                    |
| Aim      |                                                    |
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
| `Renderer failure`            | red   | D3D11 device could not be recovered; MYTHOS stops. |

ESP is drawn only when the state is `Ready` **and** the latest snapshot is valid.
Invalid snapshots render nothing rather than stale pointers.

---

## 5. Particle field

The particle field is MYTHOS's constellation signature and only decorative element.

* Contained: clipped to the menu window, drawn behind the panel content.
* Disabled when Windows client-area animations are off (`SPI_GETCLIENTAREAANIMATION`)
  or when **Overlay → Reduce motion** is enabled.
* Bounded: 100 particles at start, +1 per 0.5 s up to 150; wrap-around motion with
  a soft mouse repulsion (50–150 px influence).
* Points use `199, 176, 255` (`#C7B0FF`) with a 30–45% alpha pulse; radius 1.8 px.
* Each particle connects to at most one nearest later neighbour within 70 logical
  pixels. Lines are drawn behind points, capped at 80 connections per frame, and
  use distance-scaled alpha up to 26.

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

## 7. Aim and visibility check

Aim assist and the visibility check are the only features that interact with
the game; they live in the quarantined MYTHOS.dll modules
`EngineCalls` / `VisCheck` / `AimController`. `mythos_core` remains read-only and
the static contract test rejects engine interaction tokens anywhere else.

* Targeting is a pure function over the immutable snapshot
  (`mythos::SelectAimTarget`): self, drones and dead players are skipped;
  teammates and occluded players are filtered per settings; the closest
  projection to the crosshair inside the FOV circle wins.
* The view delta comes from `ControlRotation` and the target angle, divided by
  the smoothing and hard-capped by **Max step**. Soft aim uses the tighter FOV,
  the head bone and a 180° cap, and engages only while the fire button is held.
  Every method writes the same capped step; the legacy `ControlRotation` path
  included.
* Aim capture retains candidates independently of ESP's team, dead, drone and
  distance presentation filters. The overlay applies those filters while
  rendering, and aim selection applies only its own teammate/visibility rules.
* `AddYawInput` / `AddPitchInput` run only on the game thread and only when the
  owner enables **Allow engine calls (unsafe)**. `GameThreadExecutor` proves a
  user-mode APC round trip to the window-owning thread at startup (no hooks)
  and queues those input calls; a timeout cancels a not-yet-started task
  and latches failure. Thread identity alone does not prove a safe engine phase,
  so the method stays off by default and is never retried through another
  method. The direct `RotationInput` and `ControlRotation` methods intentionally
  mirror bodycam-master and use guarded worker-thread writes instead, so they
  remain available when the APC path is unavailable.
* Application methods: the engine's `AddYawInput`/`AddPitchInput` (verified by
  prologue + tail signature, bounded executable-section scan fallback), a
  direct `RotationInput` write, or the legacy `ControlRotation` overwrite. The
  input scale is measured at runtime (0.05 probe) so a patch cannot silently
  invert the direction. Roll is never written.
* Vischeck accepts only a cross-checked `ProcessEvent`: the measured RVA
  (`0x034E3320` on Steam build 25228199) and the controller's vtable slot
  `0x4F` must resolve to the same function, and that function must match the
  exact 31-byte current-build prologue. A mismatch disables the check, and the
  check itself is off until **Allow engine calls (unsafe)** is enabled. To
  match bodycam-master, `LineOfSightTo` is invoked synchronously from MYTHOS's
  worker thread, with `WasRecentlyRendered` as fallback. This path does not
  depend on the APC executor and can re-enter the engine at an unsafe phase.
  Reflected parameter offsets are individually bounds-checked before the
  parameter block is built. Results are cached for 50 ms and fail open: an
  unresolved or faulting check reports visible, so nothing silently
  disappears. Query faults are counted in Diagnostics. The cached controller,
  function and results are dropped on a controller change or map transition.
* The module is pinned when the optional game-thread path is opened, so an APC
  delivered late can never execute in unmapped code; shutdown joins the worker
  and cancels queued tasks first. `DELETE` therefore stops MYTHOS but leaves the
  pinned module mapped: restart the game to inject again.
* Aim telemetry (target, crosshair distance, step) and the engine/vischeck
  status strings are published in the worker frame and surfaced in the Aim and
  Diagnostics sections.

---

## 8. Accessibility checklist

- Text contrast: `kText` on `kSurface0` is ~14:1.
- All state communicated by text plus colour.
- Motion can be disabled by the user or the OS preference.
- Keyboard navigation is enabled (`ImGuiConfigFlags_NavEnableKeyboard`).
- Search has an explicit **Clear** button; an empty result set says so in text.
- A corrupt settings file never breaks startup: it is backed up and defaults
  are restored with a visible status message.
