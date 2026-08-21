cmake_minimum_required(VERSION 3.20)
project(JOYZ_EquZTone_KH_4K LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# Sedot CLAP resmi (ini 100% lancar tanpa rewel)
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG main
)

FetchContent_MakeAvailable(clap)

# Masak DSP Monolith langsung jadi .clap
add_library(JoyzEquZTone SHARED
    JOYZEquZTone4K64BandDynamic_Monolith.cpp
)

set_target_properties(JoyzEquZTone PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

target_include_directories(JoyzEquZTone PRIVATE
    ${clap_SOURCE_DIR}/include
)

target_link_libraries(JoyzEquZTone PRIVATE
    pthread
    m
)

# ============================================================================
# LINKING OS (LINUX MURNI) + MENDUKUNG ATOMIC THREADING
# ============================================================================
if(UNIX AND NOT APPLE)
    # Wajib tambahin 'pthread' buat ngebantu proses multi-threading & atomic di Linux
    target_link_libraries(JoyzEquZTone PRIVATE m GL X11 pthread)
endif()

