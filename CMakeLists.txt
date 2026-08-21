cmake_minimum_required(VERSION 3.20)
project(JOYZ_EquZTone_KH_4K LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# 1. CLAP
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG main
)

# 2. PUGL
FetchContent_Declare(
    pugl
    GIT_REPOSITORY https://github.com/lv2/pugl.git
    GIT_TAG main
)

# 3. NANOVG
FetchContent_Declare(
    nanovg
    GIT_REPOSITORY https://github.com/memononen/nanovg.git
    GIT_TAG master
)

# Matikan opsi dokumentasi & tes bawaan pugl biar build awan ngebut
set(PUGL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PUGL_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(clap pugl nanovg)

# Build Monolith Shared Library (.clap)
add_library(JoyzEquZTone SHARED
    JOYZEquZTone4K64BandDynamic_Monolith.cpp
    ${nanovg_SOURCE_DIR}/src/nanovg.c
)

set_target_properties(JoyzEquZTone PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

# Tautkan direktori include dan wrapper Pugl
target_include_directories(JoyzEquZTone PRIVATE
    ${clap_SOURCE_DIR}/include
    ${pugl_SOURCE_DIR}/include
    ${pugl_SOURCE_DIR}/include/pugl
    ${nanovg_SOURCE_DIR}/src
)

# Link library target resmi pugl
target_link_libraries(JoyzEquZTone PRIVATE
    pugl_gl-0
    pugl-0
    GL
    X11
    pthread
    m
    dl
)

# ============================================================================
# LINKING OS (LINUX MURNI) + MENDUKUNG ATOMIC THREADING
# ============================================================================
if(UNIX AND NOT APPLE)
    # Wajib tambahin 'pthread' buat ngebantu proses multi-threading & atomic di Linux
    target_link_libraries(JoyzEquZTone PRIVATE m GL X11 pthread)
endif()

