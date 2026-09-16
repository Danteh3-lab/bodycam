# NOVA — Bodycam Internal Overlay

An educational Unreal Engine research project for the game **Bodycam**. It demonstrates an
internal DirectX 9 overlay (ESP + aim assist), dynamic pattern-based resolution of engine
symbols (world anchor, name pool, input functions), and a LoadLibrary-based injector.

> **DISCLAIMER — FOR EDUCATIONAL PURPOSES ONLY**
>
> This repository exists solely to demonstrate Windows reverse-engineering and game-hacking
> techniques: memory layout of the Unreal Engine object model, runtime signature scanning,
> internal rendering overlays, and DLL injection. It is **not** intended for use against
> real multiplayer services, and using it in online matches violates most games' terms of
> service and may result in a permanent ban. The author is not responsible for any misuse.
> **Use at your own risk, in offline / private environments only.**

## What it demonstrates

- **Unreal Engine object model walk** — `UWorld` anchor discovery by scanning data sections
  and validating the full chain (`GameInstance -> LocalPlayers -> PlayerController ->
  CameraManager -> GameState -> PlayerArray`), with map-change recovery and re-anchoring.
- **FNamePool (GNames) resolution** — verified static hint plus a fallback signature scan
  over the executable's code sections.
- **Internal overlay** — a transparent DirectX 9 (D3D9Ex) window with an ImGui menu,
  box/skeleton/health/distance ESP, and an aim-assist demo that moves the view through the
  engine's own `AddYawInput`/`AddPitchInput` functions (verified by signature).
- **Injection** — a small x64 loader that maps the DLL into the target process via
  `CreateRemoteThread` + `LoadLibraryA`.

## Repository layout

```
ImGuiExternal.sln          Visual Studio 2022 solution (NOVA.dll + Loader)
ImGuiExternal/             The internal cheat (DLL)
  Source.cpp               Overlay, menu, ESP, aim assist
  reader.hpp               SEH-guarded in-process memory reads/writes, world scan
  game_names.hpp           FNamePool resolution + signature scan
  game_calls.hpp           AddPitch/AddYawInput resolution + calibration
  esp_render.hpp           Rendering helpers
  WorldToScreen.hpp        Projection
  HookFunc.h               Game offsets (UObject, world chain, components, …)
  Libraries/               Vendored DirectX headers and Dear ImGui
Loader/                    Injection loader (EXE)
  main.cpp                 Process discovery + LoadLibraryA injection
```

## Building

Requirements: Visual Studio 2022 (v143), Windows 10/11 SDK, x64.

1. Open `ImGuiExternal.sln`.
2. Build **Release | x64**.
   - Output: `x64\Release\NOVA.dll` and `x64\Release\Loader.exe`.

## Usage

1. Build **Release | x64** and start the game (`Bodycam-Win64-Shipping.exe`).
2. Run `Loader.exe` **as administrator** (optionally pass a DLL path as argument).
3. In-game: **INSERT** toggles the menu, **DELETE** unloads.

The overlay is confirmed every frame by checking that your own `PlayerState` is inside the
world's roster, so a stale anchor is recovered automatically after map changes.

## Notes on offsets

Game offsets live in `ImGuiExternal/HookFunc.h` and the FNamePool hint in
`ImGuiExternal/game_names.hpp`. These are tied to a specific game build and will go stale
after an update. Where possible the code falls back to runtime signature scans rather than
trusting hardcoded addresses.

## License / responsibility

Provided for learning only. Do not use it to gain an unfair advantage in live games.