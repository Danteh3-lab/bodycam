add_executable(mythos_tests
  src/test_main.cpp
  src/world_fixture.cpp
  src/test_memory.cpp
  src/test_names.cpp
  src/test_projection.cpp
  src/test_resolver.cpp
  src/test_collector.cpp
  src/test_aim.cpp
  src/test_native_signature.cpp
  src/test_config.cpp
  src/test_contract.cpp
)

target_link_libraries(mythos_tests PRIVATE mythos_core)
target_include_directories(mythos_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(mythos_tests PRIVATE cxx_std_20)
target_compile_definitions(mythos_tests PRIVATE MYTHOS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

mythos_apply_project_settings(mythos_tests)

add_test(NAME mythos_tests COMMAND mythos_tests)
