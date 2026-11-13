cmake_minimum_required(VERSION 3.28.3)

project(Spoink VERSION 0.1.0 LANGUAGES CXX)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schema/001_initial.sql" SPOINK_INITIAL_SCHEMA_SQL)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/initial_schema_sql.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/generated/spoink/offline/initial_schema_sql.h" @ONLY)

include(CTest)

option(SPOINK_WARNINGS_AS_ERRORS "Treat Spoink compiler warnings as errors" ON)
option(SPOINK_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SPOINK_ENABLE_CLANG_TIDY "Run clang-tidy while compiling Spoink targets" OFF)

add_library(spoink_project_options INTERFACE)
target_compile_features(spoink_project_options INTERFACE cxx_std_17)
target_compile_options(spoink_project_options INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-Wshadow;-Wformat=2>
)

if(SPOINK_WARNINGS_AS_ERRORS)
  target_compile_options(spoink_project_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
  )
endif()

if(SPOINK_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "SPOINK_ENABLE_SANITIZERS requires GCC or Clang")
  endif()
  target_compile_options(spoink_project_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(spoink_project_options INTERFACE -fsanitize=address,undefined)
endif()

if(SPOINK_ENABLE_CLANG_TIDY)
  find_program(SPOINK_CLANG_TIDY_EXECUTABLE NAMES clang-tidy-18 clang-tidy REQUIRED)
  set(CMAKE_CXX_CLANG_TIDY "${SPOINK_CLANG_TIDY_EXECUTABLE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
endif()

add_library(spoink_support
  src/support/checked_math.cpp
  src/support/error.cpp
  src/support/types.cpp
)
add_library(spoink::support ALIAS spoink_support)
target_include_directories(spoink_support
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(spoink_support PUBLIC spoink_project_options)
set_target_properties(spoink_support PROPERTIES CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(utf8proc CONFIG REQUIRED)

add_library(spoink_offline
  src/offline/offline.cpp
)
add_library(spoink::offline ALIAS spoink_offline)
target_include_directories(spoink_offline PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_include_directories(spoink_offline PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(spoink_offline PUBLIC spoink::support OpenSSL::Crypto SQLite::SQLite3 utf8proc::utf8proc)
set_target_properties(spoink_offline PROPERTIES CXX_EXTENSIONS OFF)

add_library(spoink_protocol
  src/protocol/framing.cpp
  src/protocol/codec.cpp
)
add_library(spoink::protocol ALIAS spoink_protocol)
target_include_directories(spoink_protocol PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(spoink_protocol PUBLIC spoink::support)
set_target_properties(spoink_protocol PROPERTIES CXX_EXTENSIONS OFF)

add_library(spoink_identity
  src/identity/identity.cpp
)
add_library(spoink::identity ALIAS spoink_identity)
target_include_directories(spoink_identity PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(spoink_identity PUBLIC spoink::support OpenSSL::SSL OpenSSL::Crypto)
set_target_properties(spoink_identity PROPERTIES CXX_EXTENSIONS OFF)

add_library(spoink_pairing
  src/pairing/pairing.cpp
)
add_library(spoink::pairing ALIAS spoink_pairing)
target_include_directories(spoink_pairing PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(spoink_pairing PUBLIC spoink::identity spoink::protocol OpenSSL::SSL SQLite::SQLite3)
set_target_properties(spoink_pairing PROPERTIES CXX_EXTENSIONS OFF)

add_library(spoink_session
  src/session/state_machine.cpp
)
add_library(spoink::session ALIAS spoink_session)
target_include_directories(spoink_session PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(spoink_session PUBLIC spoink::protocol spoink::support)
set_target_properties(spoink_session PROPERTIES CXX_EXTENSIONS OFF)

add_library(spoink_transport
  src/transport/connection.cpp
)
add_library(spoink::transport ALIAS spoink_transport)
target_include_directories(spoink_transport PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(spoink_transport PUBLIC spoink::identity spoink::pairing OpenSSL::SSL SQLite::SQLite3)
set_target_properties(spoink_transport PROPERTIES CXX_EXTENSIONS OFF)

add_executable(spoink src/cli/main.cpp)
target_link_libraries(spoink PRIVATE
  spoink::identity spoink::pairing spoink::session spoink::transport spoink::offline
  spoink_project_options)
set_target_properties(spoink PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(spoink PRIVATE SPOINK_VERSION="${PROJECT_VERSION}")

install(TARGETS spoink RUNTIME DESTINATION bin)

if(BUILD_TESTING)
  find_package(Catch2 3 CONFIG REQUIRED)

  add_executable(spoink_support_tests
    tests/unit/checked_math_test.cpp
    tests/unit/clock_test.cpp
    tests/unit/error_test.cpp
    tests/unit/raii_test.cpp
    tests/unit/types_test.cpp
  )
  target_link_libraries(spoink_support_tests PRIVATE spoink::support Catch2::Catch2WithMain)
  set_target_properties(spoink_support_tests PROPERTIES CXX_EXTENSIONS OFF)

  include(Catch)
  catch_discover_tests(spoink_support_tests)

  add_executable(spoink_offline_tests
    tests/unit/offline_test.cpp
    tests/unit/offline_property_test.cpp
    tests/unit/offline_fault_test.cpp
  )
  target_link_libraries(spoink_offline_tests PRIVATE spoink::offline Catch2::Catch2WithMain)
  set_target_properties(spoink_offline_tests PROPERTIES CXX_EXTENSIONS OFF)
  catch_discover_tests(spoink_offline_tests)

  add_executable(spoink_offline_integration_test tests/integration/offline_integration_test.cpp)
  target_link_libraries(spoink_offline_integration_test PRIVATE spoink::offline Catch2::Catch2WithMain)
  set_target_properties(spoink_offline_integration_test PROPERTIES CXX_EXTENSIONS OFF)
  catch_discover_tests(spoink_offline_integration_test)

  add_executable(spoink_m2_unit_tests
    tests/unit/identity_test.cpp
    tests/unit/framing_test.cpp
    tests/unit/codec_test.cpp
    tests/unit/state_machine_test.cpp
  )
  target_link_libraries(spoink_m2_unit_tests PRIVATE
    spoink::identity spoink::protocol spoink::session Catch2::Catch2WithMain)
  set_target_properties(spoink_m2_unit_tests PROPERTIES CXX_EXTENSIONS OFF)
  catch_discover_tests(spoink_m2_unit_tests)

  add_executable(spoink_m2_integration_test tests/integration/m2_integration_test.cpp)
  target_link_libraries(spoink_m2_integration_test PRIVATE
    spoink::pairing spoink::session spoink::transport spoink::offline Catch2::Catch2WithMain)
  set_target_properties(spoink_m2_integration_test PROPERTIES CXX_EXTENSIONS OFF)
  catch_discover_tests(spoink_m2_integration_test)
endif()

# EXP-CDC-001 benchmark and golden-vector generator — built unconditionally
add_executable(exp_cdc_001 benchmarks/exp_cdc_001/exp_cdc_001.cpp)
target_link_libraries(exp_cdc_001 PRIVATE spoink::offline)
set_target_properties(exp_cdc_001 PROPERTIES CXX_EXTENSIONS OFF)

add_executable(exp_cdc_001_golden benchmarks/exp_cdc_001/gen_golden_vectors.cpp)
target_link_libraries(exp_cdc_001_golden PRIVATE spoink::offline)
set_target_properties(exp_cdc_001_golden PROPERTIES CXX_EXTENSIONS OFF)
