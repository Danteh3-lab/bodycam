# MYTHOS — Bodycam Overlay

MYTHOS is an Unreal Engine 5 overlay for **Bodycam** (Steam app `2406770`,
build `25368976`), targeting offline/private play on Windows x64 in windowed
and borderless windowed modes. It provides a read-only ESP plus optional aim
assist and an engine line-of-sight visibility check.

Release artifacts:

| Artifact          | Description |
|-------------------|-------------|
| `MYTHOS.dll`        | Injected overlay: resolver, snapshot worker, D3D11 ESP + control panel |
| `MYTHOS.Loader.exe` | Minimal-rights DLL loader with validation and distinct exit codes |
| `mythos_core.lib` / `mythos_imgui.lib` | Static core and vendored UI libraries used by the release targets |

The world model is strictly read-only: `mythos_core` never writes game memory,
patches code or calls engine functions, and the static contract test enforces
this for every `core/` file. The only engine interaction — the resolved
`AddYawInput`/`AddPitchInput` calls, the guarded `RotationInput` /
`ControlRotation` writes and the `ProcessEvent` visibility query — is
quarantined in the dedicated `dll/` modules `EngineCalls`, `VisCheck` and
`AimController`; the same test rejects those tokens everywhere else and bans
patching, hook and injection APIs across `core/` and `dll/`. Injection stays in
`MYTHOS.Loader.exe` alone. No anti-cheat or stealth behaviour is implemented.

---

## Architecture

```
MYTHOS.Loader.exe
    |  validates DLL (x64), target (x64), build when discoverable, not loaded
    |  OpenProcess(minimum rights) -> VirtualAllocEx/WriteProcessMemory ->
    |  CreateRemoteThread(LoadLibraryW) -> MYTHOS.dll
    v
MYTHOS.dll  (bootstrap thread; DllMain only disables thread notifications)
    |
    +-- ProcessMemory ........ Win32 implementation of ReadOnlyMemory (SEH-guarded reads)
    +-- NamePool ............. FNamePool via known RVA, then bounded signature scan
    +-- WorldResolver ........ GWorld via known RVA, then bounded data-section scan;
    |                          full pointer/container validation per stage
    +-- SnapshotCollector .... 60 Hz worker -> immutable GameSnapshot (pointer-free)
    +-- GameThread .......... verifies a game-thread APC path (no hooks); fail-closed
    +-- EngineCalls .......... resolved AddYawInput/AddPitchInput + guarded rotation writes
    +-- VisCheck ............. opt-in direct-worker ProcessEvent, 50 ms cache, fail-open
    +-- AimController ........ target selection + per-tick aim step application
    +-- OverlayWindow ........ D3D11 top-level transparent window + ImGui lifecycle
    +-- EspRenderer .......... boxes, names, health, distance, skeletons, head dots, snaplines
    +-- MythosUi ............... Overview / Players / Aim / Visuals / Overlay / Diagnostics
    +-- SettingsStore ........ OverlayConfig schema v2, debounced atomic saves
```

The render loop only ever consumes published snapshots. Invalid snapshots render
nothing instead of falling back to stale pointers.

---

## Repository layout

```
Offsets.hpp            single offset source + profile metadata (app/build)
DESIGN.md              theme tokens, layout, states, accessibility notes
CMakeLists.txt         MYTHOS.dll + MYTHOS.Loader.exe + mythos_core + tests
core/                  read-only core library (no ImGui, no Win32 UI)
  include/mythos/        ReadOnlyMemory, WorldResolver, SnapshotCollector, Config, ...
  src/
dll/                   the injected overlay (D3D11 + ImGui)
loader/                MYTHOS.Loader.exe
tests/                 non-shipping test target with fake-memory fixtures
third_party/
  imgui/               Dear ImGui v1.92.9 (MIT, vendored)
  nlohmann/            nlohmann/json v3.12.0 (MIT, vendored)
```

Dependencies are vendored; CMake performs **no** downloads.

---

## Building

Requirements: Visual Studio 18 2026 (Build Tools or IDE) with the MSVC x64
toolset and a Windows 10/11 SDK; CMake 3.28+ (bundled with the VS installation
works). Project code is C++20 with `/W4 /WX`, conformance mode and the static
MSVC runtime; third-party sources are compiled with isolated warnings.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Outputs:

```
build\dll\Release\MYTHOS.dll
build\loader\Release\MYTHOS.Loader.exe
build\core\Release\mythos_core.lib
build\Release\mythos_imgui.lib
build\tests\Release\mythos_tests.exe
```

Debug builds (`--config Debug`) and the test target are supported and verified.

---

## Tests

`mythos_tests` is a non-shipping target with no framework dependency. Coverage:

- pointer/range guards, guarded reads, UE TArray bounds, FTransform layout;
- narrow/wide FName decoding, pool plausibility, signature matcher;
- resolver: known RVA, every reachable chain-stage failure, data-section anchor
  fallback, FNamePool signature fallback, outer-world rescue + re-anchor, map
  transition, fail-closed offsets;
- projection modes, capsule/pose box generation, health ramp;
- snapshot capture: filtering (self/team/dead/drone/distance), health, names,
  pose, skeleton hierarchy/cache sharing, mesh mismatch rejection, FOV fallback,
  visibility probe integration;
- aim math (angle wrapping, smoothing, hard step cap) and target selection
  (self/drone/dead/team/visibility/FOV filters, head vs mid-height);
- configuration round-trip, clamping, migration, corruption backup, atomic save;
- static contract scan: `core/` strictly read-only, engine interaction confined
  to the quarantine modules, patching/injection banned everywhere.

---

## Usage

1. Start Bodycam (windowed or borderless windowed).
2. Run `MYTHOS.Loader.exe` (same integrity level as the game):
   - `--dll <path>` optional, defaults to `MYTHOS.dll` next to the loader;
   - `--pid <id>` optional, defaults to finding `Bodycam-Win64-Shipping.exe`.
   - injection is rejected when either `MYTHOS.dll` or the legacy `NOVA.dll` is
     already mapped; restart the game before trying again.
   - the loader always enforces the pinned build gate before injection; there
     is no bypass flag. Dumping/RE injection lives in the separate
     analysis-only tool `tools/analysis-injector`
     (`MYTHOS.Analysis.exe --dll <path> [--pid <id>]`), which performs no
     build validation by design.
3. In game: `INSERT` toggles the MYTHOS panel; `DELETE` stops MYTHOS (the overlay,
   sampling and input stop). Opening the game-thread path pins `MYTHOS.dll`, so
   it stays mapped until the game exits and the loader refuses a second
   injection: **restart the game to load MYTHOS again**.

The loader's exit codes:

| Code | Meaning |
|------|---------|
| 0    | Injection succeeded (or `--help`) |
| 2    | Invalid arguments |
| 3    | DLL not found |
| 4    | DLL is not a valid AMD64 image |
| 5    | Target process not found |
| 6    | Target process is not x64 |
| 7    | Known build mismatch (profile pins a version) |
| 8    | Access denied while opening the target |
| 9    | Remote allocation failed |
| 10   | Remote write failed |
| 11   | Remote thread failed |
| 12   | Injection timed out |
| 13   | `MYTHOS.dll` or legacy `NOVA.dll` is already loaded (restart the game to inject again) |
| 14   | `LoadLibraryW` returned NULL in the target |

---

## Settings, logs and privacy

- Settings: `%LOCALAPPDATA%\MYTHOS\settings.json` (schema v2). Edits apply
  immediately; saves are atomic and debounced by 500 ms, and flush on stop.
  Invalid values are clamped; a corrupt file is preserved as
  `settings.json.corrupt-<timestamp>.json` before defaults are restored.
- Logs: `%LOCALAPPDATA%\MYTHOS\logs\mythos.log`, bounded to 512 KB with one
  rotation. Lifecycle stages, timings, failures and build fingerprints only —
  no player names or gameplay data.
- First-run migration: if `%LOCALAPPDATA%\MYTHOS\settings.json` is absent, MYTHOS
  reads `%LOCALAPPDATA%\NOVA\settings.json` once, validates/clamps it, and writes
  an atomic copy into the MYTHOS directory. The legacy file is read-only and is
  never renamed, deleted, backed up, or imported as a log. The panel reports
  `Imported NOVA settings` for that session; missing or invalid legacy data leaves
  MYTHOS defaults in place with a warning.

---

## Offsets and build gating

`Offsets.hpp` is the single offset source and carries the profile metadata
(Steam app `2406770`, build `25368976`, UE 5.5.4). Both globals were re-measured
against the installed build: the runtime's validated signature scan resolves
the FName pool at RVA `0x099E3040` (Dumper-7's decorative `GNames` value
`0x099AB188` is not the pool), while Dumper-7 reports the `GWorld` anchor slot
at RVA `0x09C42738`. They are resolved from their RVAs first and only trusted
after full validation; bounded signature and data-section scans are the
fallbacks, with retry backoff and a bounded rescan policy.

The `ProcessEvent` identity was re-measured on the same build by scanning the
installed image: the exact 31-byte semantic prologue occurs exactly once in
`.text`, at RVA `0x034E4A60`, and the controller's `UObject::ProcessEvent`
vtable slot `0x4F` must resolve to that same function. VisCheck requires that
equality **and** the prologue match (it embeds
`test dword ptr [rdx+0xB0], 0x400`, i.e. `UFunction::FunctionFlags`); a failed
check disables the vischeck instead of invoking an unrelated function. The
`AddPitchInput`/`AddYawInput` RVAs (`0x3CB9DF0`/`0x3CBA000`) were re-measured
the same way via their tail signatures; the embedded `RotationInput` field
offsets are unchanged.

Build gating is enforced on the PE identity, not just a version string:

- The profile pins `SizeOfImage`, `TimeDateStamp` and `CheckSum`
  (`0x0A720000` / `0x8E1C799A` / `0x0A2E9763`). The loader reads these from the
  target image and **fails closed (exit 7) before injection** on any mismatch.
- The DLL re-measures the same fields in-process; a conflict reports the
  terminal `Offsets invalid` state and ESP stays disabled.
- An entirely unpinned profile still injects (unknown builds) and is gated by
  the complete world/controller/camera/roster/name-pool/local-player
  invariants instead.

---

## Aim and visibility check

- Hard aim engages while the right mouse button is held; soft aim corrects
  continuously while the fire button is held. Targets are picked by screen
  distance to the crosshair inside the configured FOV circle, with team, dead,
  drone and occlusion filters; the head bone comes from the model's reference
  pose. Visible only and Dim occluded are mutually exclusive in the panel.
- The `AddYawInput`/`AddPitchInput` engine method runs only on a verified
  game-thread path: MYTHOS proves a user-mode APC round trip to the window-owning
  thread at startup, queues those calls there with a bounded wait, and cancels
  tasks that miss their deadline. No hooks or thread suspension are involved.
- To match bodycam-master, the `ProcessEvent` vischeck runs synchronously from
  MYTHOS's worker thread. It does not depend on the APC path, but it can re-enter
  engine code at an unsafe phase. It is **off by default** and runs only when
  the owner enables **Allow engine calls (unsafe)** in the Aim section.
- The default direct methods mirror bodycam-master: guarded read/modify/write
  operations update `RotationInput` or `ControlRotation` synchronously from
  MYTHOS's worker thread and do not depend on the optional APC path. The engine
  method additionally calls the game's own functions
  (resolved by RVA, verified by signature, with a bounded executable-section
  scan fallback and a runtime-measured input scale). Every method uses the
  same per-tick step hard-capped by **Max step**; roll is never touched.
  The legacy project called those engine functions directly from its own
  thread; this rewrite keeps them behind the optional game-thread path while
  retaining its direct rotation-write behavior for the two reference-compatible
  methods.
- While aim is active, capture retains candidates even when ESP hides their
  team, death state, drone class, or distance. Those settings are applied only
  by ESP rendering; aim still applies its own teammate and visibility filters.
- The visibility check calls the engine's own `LineOfSightTo` (fallback:
  `WasRecentlyRendered`) through the opt-in direct-worker `ProcessEvent` path,
  cached for 50 ms. It fails open: unresolved or faulting checks report visible,
  so nothing silently disappears from the ESP. Diagnostics expose query,
  visible, hidden, fault and cache counts.

---

## Boundaries

- The aim assist only calls the game's own input functions or performs the
  guarded rotation writes above; there are no detours, code patches or render
  hooks, and no render-engine interaction.
- No anti-cheat bypass, no capture hiding, no stealth injection.
- Offline/private play only; no online multiplayer support.
- Exclusive fullscreen is unsupported; use windowed or borderless windowed.

---

## Live acceptance status

Verified live against the installed Steam build `25368976`
(`Bodycam-Win64-Shipping.exe`, borderless windowed 1920x1200):

- Loader validated the pinned PE identity, injected exactly once, exit 0.
- Overlay created; state reached `Ready` ("world, camera, roster and name pool
  validated") in the offline shooting range, stable, with the panel showing the
  health indicator, config status and FPS.
- Name pool and world anchor resolved through the known RVAs; the bounded
  data-section and signature fallbacks were also observed resolving when the
  stale dump RVAs were still in use.
- Settings persisted to `%LOCALAPPDATA%\MYTHOS\settings.json` and reloaded on the
  next injection; the corrupt/type-invalid recovery path is unit-tested.
- `DELETE` stopped MYTHOS cleanly (`settings flushed on stop` / `MYTHOS stopped` in
  the log) and the game stayed alive and responsive. Once the game-thread path
  is opened the module is pinned: it stays mapped until the game exits, so a
  restart is required to inject again.
- Alt-Tab away from the game hides the overlay (no ESP over other windows).

Remaining manual pass (owner-run, game running with MYTHOS injected): ESP over
other pawns in a private/bots match (team/drone/dead filters), projection
tuning, search/clear and keyboard navigation in the panel, map-transition and
death/spectating recovery, minimize/restore, and DPI/monitor moves.

Vischeck live pass: enable **Aim → Allow engine calls (unsafe)** and
**Players → Dim occluded** first. Expected in Diagnostics and `mythos.log`:
`LineOfSightTo via ProcessEvent on MYTHOS worker (ProcessEvent verified at RVA
0x034E4A60...)`, `queries > 0`, `visible > 0`, `hidden > 0`, `faults = 0`. A
player moving behind solid cover should turn grey; with **Visible only** they
should disappear. If `faults` becomes nonzero, disable engine calls
immediately. If resolution succeeds but every result stays visible, the next
investigation point is the reflected `LineOfSightTo` parameter layout, not
`ProcessEvent`. The aim and vischeck paths need their live pass too (targeting
feel, input-scale calibration); their pure parts are unit-tested. The
deterministic test target covers the logic behind every one of these paths.
