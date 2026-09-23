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

# The Godot extension (below) links the core; configure with -DSDF_BUILD_GODOT=OFF to build
# only the engine-agnostic parts.
option(SDF_BUILD_GODOT "Build the Godot GDExtension (needs extern/godot-cpp)" ON)
if(SDF_BUILD_GODOT)
	# godot-cpp links the static MSVC runtime, but only picks it once its directory is
	# added; choosing it here, before any target exists, keeps every library on the same one.
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# Engine-agnostic core: no Godot headers anywhere under src/core.
add_library(sdf_core STATIC
	src/core/adf/adf.cpp
	src/core/body/body.cpp
	src/core/body/materials.cpp
	src/core/compile/octree.cpp
	src/core/edit/session.cpp
	src/core/eval/image.cpp
	src/core/eval/query.cpp
	src/core/eval/reference_renderer.cpp
	src/core/tools/tools.cpp
)
target_include_directories(sdf_core PUBLIC src/core)
set_target_properties(sdf_core PROPERTIES POSITION_INDEPENDENT_CODE ON) # linked into the GDExtension
find_package(Threads REQUIRED)
target_link_libraries(sdf_core PUBLIC Threads::Threads)

# The demo scenes: golden images, tools and the Godot node's test bodies.
add_library(sdf_demo STATIC src/demo/gallery.cpp)
target_include_directories(sdf_demo PUBLIC src)
set_target_properties(sdf_demo PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(sdf_demo PUBLIC sdf_core)

add_executable(sdf_bench tools/sdf_bench.cpp)
target_link_libraries(sdf_bench PRIVATE sdf_core sdf_demo)

# PNG tooling and the tests need zlib; without it only the core and the extension build.
find_package(ZLIB)
if(ZLIB_FOUND)
	add_library(sdf_io STATIC src/io/png.cpp)
	target_include_directories(sdf_io PUBLIC src)
	target_link_libraries(sdf_io PUBLIC sdf_core ZLIB::ZLIB)

	add_executable(sdf_gallery tools/sdf_gallery.cpp)
	target_link_libraries(sdf_gallery PRIVATE sdf_demo sdf_io)

	add_executable(sdf_render tools/sdf_render.cpp)
	target_link_libraries(sdf_render PRIVATE sdf_demo sdf_io)

	add_executable(sdf_tests
		tests/test_main.cpp
		tests/test_primitives.cpp
		tests/test_blend.cpp
		tests/test_body.cpp
		tests/test_golden.cpp
		tests/test_octree.cpp
		tests/test_adf.cpp
		tests/test_tools.cpp
		tests/test_session.cpp
	)
	target_link_libraries(sdf_tests PRIVATE sdf_core sdf_demo sdf_io)
	target_compile_definitions(sdf_tests PRIVATE SDF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

	enable_testing()
	add_test(NAME sdf_tests COMMAND sdf_tests)
else()
	message(STATUS "zlib not found: skipping sdf_gallery, sdf_render and sdf_tests")
endif()

# The Godot extension: the core plus thin bindings, built with the pinned godot-cpp (v10)
# against the Godot 4.7 API, so it loads in 4.7 and later 4.x releases.
if(SDF_BUILD_GODOT)
	set(GODOTCPP_DEFAULT_API_VERSION "4.7") # godot-cpp v10 ships API definitions for 4.3-4.7
	set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE) # keep our warnings to our code
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../extern/godot-cpp ${CMAKE_BINARY_DIR}/godot-cpp EXCLUDE_FROM_ALL)
	add_library(sdf_godot SHARED
		src/godot/register_types.cpp
		src/godot/sdf_body.cpp
	)
	target_link_libraries(sdf_godot PRIVATE sdf_core sdf_demo godot::cpp)
	# Straight into the project, named as game/sdf.gdextension expects: sdf_godot.<os>.<ext>.
	# ($<1:...> stops multi-config generators from appending a per-config directory.)
	set_target_properties(sdf_godot PROPERTIES
		CXX_VISIBILITY_PRESET hidden
		PREFIX ""
		LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/../game/bin>"
		RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}/../game/bin>"
		OUTPUT_NAME "sdf_godot.$<LOWER_CASE:${CMAKE_SYSTEM_NAME}>")
endif()
