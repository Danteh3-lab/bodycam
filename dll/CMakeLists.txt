add_library(NOVA SHARED
  src/dllmain.cpp
  src/Runtime.cpp
  src/OverlayWindow.cpp
  src/EspRenderer.cpp
  src/NovaUi.cpp
  src/Theme.cpp
  src/Particles.cpp
  src/SettingsStore.cpp
  src/BuildIdentity.cpp
  src/Platform.cpp
  src/ProcessMemory.cpp
)
add_library(nova::dll ALIAS NOVA)

set_target_properties(NOVA PROPERTIES
  OUTPUT_NAME "NOVA"
  PREFIX ""
)

target_link_libraries(NOVA PRIVATE
  nova_core
  nova_imgui
  d3d11
  dxgi
  dwmapi
  user32
  gdi32
  ole32
  psapi
  version
)

nova_apply_project_settings(NOVA)
