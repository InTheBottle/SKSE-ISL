cmake_minimum_required(VERSION 3.21)

project(
    SKSE_ISL
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# CommonLibSSE-NG (vendored as a git submodule at extern/CommonLibSSE-NG)
# ---------------------------------------------------------------------------
set(COMMONLIBSSE_DIR "${CMAKE_SOURCE_DIR}/extern/CommonLibSSE-NG")
if (NOT EXISTS "${COMMONLIBSSE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "CommonLibSSE-NG submodule is missing. Run:\n"
        "    git submodule update --init --recursive")
endif()

# Turn off CommonLibSSE's bundled tests (drags in catch2 at configure time).
set(BUILD_TESTS OFF CACHE BOOL "Build CommonLibSSE tests" FORCE)

# Multi-runtime targeting: default to all three.
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)

add_subdirectory("${COMMONLIBSSE_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
)

add_library(${PROJECT_NAME} SHARED ${SRC})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"
)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /MP /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor
        /utf-8 /EHsc
        /Zi        # full debug info, even in Release
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        /DEBUG:FULL
        /OPT:REF   # still strip unused code/data
        /OPT:ICF   # fold identical COMDATs
    )
endif()

# Put the DLL where SKSE expects it if CommunityShadersOutputDir-style env var is set
set(ISL_OUTPUT_DIRS "" CACHE STRING
    "Semicolon-separated Skyrim Data directories to auto-deploy the built plugin to")

if (ISL_OUTPUT_DIRS)
    foreach (DIR ${ISL_OUTPUT_DIRS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${DIR}/SKSE/Plugins"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${PROJECT_NAME}>"
                "${DIR}/SKSE/Plugins/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
                "${DIR}/SKSE/Plugins/"
            COMMENT "Deploying ${PROJECT_NAME}.dll/.pdb to ${DIR}/SKSE/Plugins"
        )
    endforeach()
endif()
