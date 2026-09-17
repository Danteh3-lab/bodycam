add_executable(MYTHOS.Loader src/main.cpp)
add_executable(mythos::loader ALIAS MYTHOS.Loader)

set_target_properties(MYTHOS.Loader PROPERTIES
  OUTPUT_NAME "MYTHOS.Loader"
  PREFIX ""
)

# Offsets.hpp only: the loader stays independent of the read-only core.
target_include_directories(MYTHOS.Loader PRIVATE "${CMAKE_SOURCE_DIR}")
target_compile_features(MYTHOS.Loader PRIVATE cxx_std_20)
target_link_libraries(MYTHOS.Loader PRIVATE version user32)

# Ship MYTHOS.dll next to MYTHOS.Loader.exe so the loader works with no arguments
# (double-click). The generator expression also adds the build dependency.
add_custom_command(TARGET MYTHOS.Loader POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:MYTHOS>" "$<TARGET_FILE_DIR:MYTHOS.Loader>"
  COMMENT "Placing MYTHOS.dll next to MYTHOS.Loader.exe"
)

mythos_apply_project_settings(MYTHOS.Loader)
