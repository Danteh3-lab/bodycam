add_executable(NOVA.Loader src/main.cpp)
add_executable(nova::loader ALIAS NOVA.Loader)

set_target_properties(NOVA.Loader PROPERTIES
  OUTPUT_NAME "NOVA.Loader"
  PREFIX ""
)

# Offsets.hpp only: the loader stays independent of the read-only core.
target_include_directories(NOVA.Loader PRIVATE "${CMAKE_SOURCE_DIR}")
target_compile_features(NOVA.Loader PRIVATE cxx_std_20)
target_link_libraries(NOVA.Loader PRIVATE version user32)

# Ship NOVA.dll next to NOVA.Loader.exe so the loader works with no arguments
# (double-click). The generator expression also adds the build dependency.
add_custom_command(TARGET NOVA.Loader POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:NOVA>" "$<TARGET_FILE_DIR:NOVA.Loader>"
  COMMENT "Placing NOVA.dll next to NOVA.Loader.exe"
)

nova_apply_project_settings(NOVA.Loader)
