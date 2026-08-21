cmake_minimum_required(VERSION 3.20)
project(JOYZ_EquZTone_KH_4K LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Daftarkan jalur include dari folder deps
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/clap/include
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/pugl/include
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/pugl
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nanovg/src
)

# Kumpulkan implementasi C Pugl & NanoVG
file(GLOB_RECURSE PUGL_C_FILES "${CMAKE_CURRENT_SOURCE_DIR}/deps/pugl/src/*.c")

add_library(JoyzEquZTone SHARED
    JOYZEquZTone4K64BandDynamic_Monolith.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nanovg/src/nanovg.c
    ${PUGL_C_FILES}
)

set_target_properties(JoyzEquZTone PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

target_link_libraries(JoyzEquZTone PRIVATE
    GL
    X11
    Xcursor
    Xrandr
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

