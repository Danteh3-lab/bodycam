add_executable(nova_tests
  src/test_main.cpp
  src/world_fixture.cpp
  src/test_memory.cpp
  src/test_names.cpp
  src/test_projection.cpp
  src/test_resolver.cpp
  src/test_collector.cpp
  src/test_config.cpp
  src/test_contract.cpp
)

target_link_libraries(nova_tests PRIVATE nova_core)
target_include_directories(nova_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(nova_tests PRIVATE cxx_std_20)
target_compile_definitions(nova_tests PRIVATE NOVA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

nova_apply_project_settings(nova_tests)

add_test(NAME nova_tests COMMAND nova_tests)
