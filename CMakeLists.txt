cmake_minimum_required(VERSION 3.28)

project(NOVA VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC)
  message(FATAL_ERROR "NOVA is built with MSVC on Windows x64 only.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "NOVA requires the x64 generator (-A x64).")
endif()

option(NOVA_BUILD_TESTS "Build the non-shipping test target" ON)
option(NOVA_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)

set(NOVA_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---------------------------------------------------------------------------
# Project warning policy: /W4 /WX for NOVA code, isolated for third-party.
# ---------------------------------------------------------------------------
function(nova_apply_project_settings target)
  target_compile_options(${target} PRIVATE
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /EHsc
    /MP
    "/external:I${NOVA_THIRD_PARTY_DIR}"
    /external:W0
  )
  if(NOVA_WARNINGS_AS_ERRORS)
    target_compile_options(${target} PRIVATE /WX)
  endif()
  target_compile_definitions(${target} PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
endfunction()

# ---------------------------------------------------------------------------
# Third-party: Dear ImGui v1.92.9 (vendored, licensed MIT).
# Built as its own target with warnings fully isolated from /W4 /WX.
# ---------------------------------------------------------------------------
add_library(nova_imgui STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_dx11.cpp
  third_party/imgui/backends/imgui_impl_win32.cpp
)
add_library(nova::imgui ALIAS nova_imgui)
target_include_directories(nova_imgui PUBLIC
  "${NOVA_THIRD_PARTY_DIR}/imgui"
  "${NOVA_THIRD_PARTY_DIR}/imgui/backends"
)
target_compile_definitions(nova_imgui PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)
target_compile_options(nova_imgui PRIVATE /W0 /EHsc /utf-8)
target_link_libraries(nova_imgui PUBLIC d3d11 dxgi dwmapi user32)

# ---------------------------------------------------------------------------
# Subprojects
# ---------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(dll)
add_subdirectory(loader)

if(NOVA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
