# NOVA — Read-Only Bodycam Overlay

NOVA is a read-only Unreal Engine 5 research overlay for **Bodycam**
(Steam app `2406770`, build `25228199`), targeting offline/private play on
Windows x64 in windowed and borderless windowed modes.

Release artifacts:

| Artifact          | Description |
|-------------------|-------------|
| `NOVA.dll`        | Injected overlay: resolver, snapshot worker, D3D11 ESP + control panel |
| `NOVA.Loader.exe` | Minimal-rights DLL loader with validation and distinct exit codes |

There is no preview application and no aim assistance. NOVA never writes game
memory, never patches code, never calls engine functions, never hooks rendering,
and performs no anti-cheat or stealth behaviour. The static contract test
(`tests/src/test_contract.cpp`) enforces this for every `core/` and `dll/`
translation unit.

---

## Architecture

```
NOVA.Loader.exe
    |  validates DLL (x64), target (x64), build when discoverable, not loaded
    |  OpenProcess(minimum rights) -> VirtualAllocEx/WriteProcessMemory ->
    |  CreateRemoteThread(LoadLibraryW) -> NOVA.dll
    v
NOVA.dll  (bootstrap thread; DllMain only disables thread notifications)
    |
    +-- ProcessMemory ........ Win32 implementation of ReadOnlyMemory (SEH-guarded reads)
    +-- NamePool ............. FNamePool via known RVA, then bounded signature scan
    +-- WorldResolver ........ GWorld via known RVA, then bounded data-section scan;
    |                          full pointer/container validation per stage
    +-- SnapshotCollector .... 60 Hz worker -> immutable GameSnapshot (pointer-free)
    +-- OverlayWindow ........ D3D11 top-level transparent window + ImGui lifecycle
    +-- EspRenderer .......... boxes, names, health, distance, skeletons, head dots, snaplines
    +-- NovaUi ............... Overview / Players / Visuals / Overlay / Diagnostics
    +-- SettingsStore ........ OverlayConfig schema v1, debounced atomic saves
```

The render loop only ever consumes published snapshots. Invalid snapshots render
nothing instead of falling back to stale pointers.

---

## Repository layout

```
Offsets.hpp            single offset source + profile metadata (app/build)
DESIGN.md              theme tokens, layout, states, accessibility notes
CMakeLists.txt         NOVA.dll + NOVA.Loader.exe + nova_core + tests
core/                  read-only core library (no ImGui, no Win32 UI)
  include/nova/        ReadOnlyMemory, WorldResolver, SnapshotCollector, Config, ...
  src/
dll/                   the injected overlay (D3D11 + ImGui)
loader/                NOVA.Loader.exe
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
build\dll\Release\NOVA.dll
build\loader\Release\NOVA.Loader.exe
build\core\Release\nova_core.lib
build\tests\Release\nova_tests.exe
```

Debug builds (`--config Debug`) and the test target are supported and verified.

---

## Tests

`nova_tests` is a non-shipping target with no framework dependency. Coverage:

- pointer/range guards, guarded reads, UE TArray bounds, FTransform layout;
- narrow/wide FName decoding, pool plausibility, signature matcher;
- resolver: known RVA, every reachable chain-stage failure, data-section anchor
  fallback, FNamePool signature fallback, outer-world rescue + re-anchor, map
  transition, fail-closed offsets;
- projection modes, capsule/pose box generation, health ramp;
- snapshot capture: filtering (self/team/dead/drone/distance), health, names,
  pose, skeleton hierarchy/cache sharing, mesh mismatch rejection, FOV fallback;
- configuration round-trip, clamping, migration, corruption backup, atomic save;
- static read-only contract scan of `core/` and `dll/`.

---

## Usage

1. Start Bodycam (windowed or borderless windowed).
2. Run `NOVA.Loader.exe` (same integrity level as the game):
   - `--dll <path>` optional, defaults to `NOVA.dll` next to the loader;
   - `--pid <id>` optional, defaults to finding `Bodycam-Win64-Shipping.exe`.
3. In game: `INSERT` toggles the NOVA panel, `DELETE` unloads NOVA cleanly.

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
| 13   | `NOVA.dll` is already loaded |
| 14   | `LoadLibraryW` returned NULL in the target |

---

## Settings, logs and privacy

- Settings: `%LOCALAPPDATA%\NOVA\settings.json` (schema v1). Edits apply
  immediately; saves are atomic and debounced by 500 ms, and flush on unload.
  Invalid values are clamped; a corrupt file is preserved as
  `settings.json.corrupt-<timestamp>.json` before defaults are restored.
- Logs: `%LOCALAPPDATA%\NOVA\logs\nova.log`, bounded to 512 KB with one
  rotation. Lifecycle stages, timings, failures and build fingerprints only —
  no player names or gameplay data.

---

## Offsets and build gating

`Offsets.hpp` is the single offset source and carries the profile metadata
(Steam app `2406770`, build `25228199`). Both globals were re-measured against
the installed build: `GNames` at RVA `0x099C3AC0` and the stable `GWorld` anchor
slot at RVA `0x09C231B8`. They are resolved from their RVAs first and only
trusted after full validation; bounded signature and data-section scans are the
fallbacks, with retry backoff and a bounded rescan policy.

Build gating is enforced on the PE identity, not just a version string:

- The profile pins `SizeOfImage`, `TimeDateStamp` and `CheckSum`
  (`0x0A6FE000` / `0xCF9AA4C2` / `0x0A2C6CC0`). The loader reads these from the
  target image and **fails closed (exit 7) before injection** on any mismatch.
- The DLL re-measures the same fields in-process; a conflict reports the
  terminal `Offsets invalid` state and ESP stays disabled.
- An entirely unpinned profile still injects (unknown builds) and is gated by
  the complete world/controller/camera/roster/name-pool/local-player
  invariants instead.

---

## Boundaries

- No aim assist, soft aim, input manipulation or gameplay memory writes.
- No engine-function calls, no detours/code patches, no render hooks.
- No anti-cheat bypass, no capture hiding, no stealth injection.
- Offline/private play only; no online multiplayer support.
- Exclusive fullscreen is unsupported; use windowed or borderless windowed.

---

## Live acceptance status

Verified live against the installed Steam build `25228199`
(`Bodycam-Win64-Shipping.exe`, borderless windowed 1920x1200):

- Loader validated the pinned PE identity, injected exactly once, exit 0.
- Overlay created; state reached `Ready` ("world, camera, roster and name pool
  validated") in the offline shooting range, stable, with the panel showing the
  health indicator, config status and FPS.
- Name pool and world anchor resolved through the known RVAs; the bounded
  data-section and signature fallbacks were also observed resolving when the
  stale dump RVAs were still in use.
- Settings persisted to `%LOCALAPPDATA%\NOVA\settings.json` and reloaded on the
  next injection; the corrupt/type-invalid recovery path is unit-tested.
- `DELETE` unloaded cleanly (`settings flushed` / `NOVA unloaded` in the log),
  `NOVA.dll` disappeared from the process module list, and the game stayed
  alive and responsive.
- Alt-Tab away from the game hides the overlay (no ESP over other windows).

Remaining manual pass (owner-run, game running with NOVA injected): ESP over
other pawns in a private/bots match (team/drone/dead filters), projection
tuning, search/clear and keyboard navigation in the panel, map-transition and
death/spectating recovery, minimize/restore, and DPI/monitor moves. The
deterministic test target covers the logic behind every one of these paths.
