cmake_minimum_required(VERSION 3.28.3)

project(Spoink VERSION 0.1.0 LANGUAGES CXX)

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

add_executable(spoink src/cli/main.cpp)
target_link_libraries(spoink PRIVATE spoink::support spoink_project_options)
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
endif()
