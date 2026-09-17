add_library(MYTHOS SHARED
  src/dllmain.cpp
  src/Runtime.cpp
  src/OverlayWindow.cpp
  src/EspRenderer.cpp
  src/MythosUi.cpp
  src/Theme.cpp
  src/Particles.cpp
  src/SettingsStore.cpp
  src/BuildIdentity.cpp
  src/Platform.cpp
  src/ProcessMemory.cpp
  src/GameThread.cpp
  src/EngineCalls.cpp
  src/VisCheck.cpp
  src/AimController.cpp
)
add_library(mythos::dll ALIAS MYTHOS)

set_target_properties(MYTHOS PROPERTIES
  OUTPUT_NAME "MYTHOS"
  PREFIX ""
)

target_link_libraries(MYTHOS PRIVATE
  mythos_core
  mythos_imgui
  d3d11
  dxgi
  dwmapi
  user32
  gdi32
  ole32
  psapi
  version
)

mythos_apply_project_settings(MYTHOS)
