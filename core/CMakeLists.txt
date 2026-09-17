add_library(mythos_core STATIC
  src/ReadOnlyMemory.cpp
  src/UnrealTypes.cpp
  src/Projection.cpp
  src/BoxMath.cpp
  src/Scan.cpp
  src/NamePool.cpp
  src/WorldResolver.cpp
  src/Skeleton.cpp
  src/SnapshotCollector.cpp
  src/Aim.cpp
  src/NativeFunctionSignature.cpp
  src/Config.cpp
  src/Logging.cpp
  src/RuntimeDiagnostics.cpp
)
add_library(mythos::core ALIAS mythos_core)

target_include_directories(mythos_core
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/.."       # Offsets.hpp (single offset source)
  PRIVATE "${MYTHOS_THIRD_PARTY_DIR}"              # nlohmann/json.hpp (vendored)
)

target_compile_features(mythos_core PUBLIC cxx_std_20)

mythos_apply_project_settings(mythos_core)
