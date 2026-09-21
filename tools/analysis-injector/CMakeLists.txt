add_executable(MYTHOS.Analysis src/main.cpp)
add_executable(mythos::analysis ALIAS MYTHOS.Analysis)

set_target_properties(MYTHOS.Analysis PROPERTIES
  OUTPUT_NAME "MYTHOS.Analysis"
  PREFIX ""
)

# Offsets.hpp only (default target process name): the analysis injector stays
# independent of the read-only core and performs no build validation.
target_include_directories(MYTHOS.Analysis PRIVATE "${CMAKE_SOURCE_DIR}")

target_compile_features(MYTHOS.Analysis PRIVATE cxx_std_20)

mythos_apply_project_settings(MYTHOS.Analysis)
