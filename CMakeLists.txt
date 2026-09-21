cmake_minimum_required(VERSION 3.28)

project(MYTHOS VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT MSVC)
  message(FATAL_ERROR "MYTHOS is built with MSVC on Windows x64 only.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "MYTHOS requires the x64 generator (-A x64).")
endif()

option(MYTHOS_BUILD_TESTS "Build the non-shipping test target" ON)
option(MYTHOS_WARNINGS_AS_ERRORS "Treat project warnings as errors" ON)

set(MYTHOS_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---------------------------------------------------------------------------
# Project warning policy: /W4 /WX for MYTHOS code, isolated for third-party.
# ---------------------------------------------------------------------------
function(mythos_apply_project_settings target)
  target_compile_options(${target} PRIVATE
    /W4
    /permissive-
    /utf-8
    /Zc:__cplusplus
    /EHsc
    /MP
    "/external:I${MYTHOS_THIRD_PARTY_DIR}"
    /external:W0
  )
  if(MYTHOS_WARNINGS_AS_ERRORS)
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
add_library(mythos_imgui STATIC
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_dx11.cpp
  third_party/imgui/backends/imgui_impl_win32.cpp
)
add_library(mythos::imgui ALIAS mythos_imgui)
target_include_directories(mythos_imgui PUBLIC
  "${MYTHOS_THIRD_PARTY_DIR}/imgui"
  "${MYTHOS_THIRD_PARTY_DIR}/imgui/backends"
)
target_compile_definitions(mythos_imgui PRIVATE
  UNICODE
  _UNICODE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)
target_compile_options(mythos_imgui PRIVATE /W0 /EHsc /utf-8)
target_link_libraries(mythos_imgui PUBLIC d3d11 dxgi dwmapi user32)

# ---------------------------------------------------------------------------
# Subprojects
# ---------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(dll)
add_subdirectory(loader)
add_subdirectory(tools/analysis-injector)

if(MYTHOS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
