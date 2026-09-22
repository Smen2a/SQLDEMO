cmake_minimum_required(VERSION 3.20)
project(sdf_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
	add_compile_options(/W4)
else()
	add_compile_options(-Wall -Wextra)
endif()

# Engine-agnostic core: no Godot headers anywhere under src/core.
add_library(sdf_core STATIC
	src/core/body/body.cpp
)
target_include_directories(sdf_core PUBLIC src/core)

find_package(Threads REQUIRED)

add_executable(sdf_tests
	tests/test_main.cpp
	tests/test_primitives.cpp
	tests/test_blend.cpp
	tests/test_body.cpp
)
target_link_libraries(sdf_tests PRIVATE sdf_core Threads::Threads)
target_compile_definitions(sdf_tests PRIVATE SDF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

enable_testing()
add_test(NAME sdf_tests COMMAND sdf_tests)
