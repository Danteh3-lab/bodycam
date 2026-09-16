add_library(nova_core STATIC
  src/ReadOnlyMemory.cpp
  src/UnrealTypes.cpp
  src/Projection.cpp
  src/BoxMath.cpp
  src/Scan.cpp
  src/NamePool.cpp
  src/WorldResolver.cpp
  src/Skeleton.cpp
  src/SnapshotCollector.cpp
  src/Config.cpp
  src/Logging.cpp
  src/RuntimeDiagnostics.cpp
)
add_library(nova::core ALIAS nova_core)

target_include_directories(nova_core
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/.."       # Offsets.hpp (single offset source)
  PRIVATE "${NOVA_THIRD_PARTY_DIR}"              # nlohmann/json.hpp (vendored)
)

target_compile_features(nova_core PUBLIC cxx_std_20)

nova_apply_project_settings(nova_core)
