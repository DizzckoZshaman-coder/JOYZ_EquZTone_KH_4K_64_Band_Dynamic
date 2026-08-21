cmake_minimum_required(VERSION 3.15)
project(JOYZ_EquZTone_KH_4K VERSION 4.0 LANGUAGES CXX C)

# Pakai standar C++17 (Ini otomatis ngebuka akses ke <atomic>, <array>, <cmath> bawaan compiler lu!)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) 

# ============================================================================
# SIHIR KEMALASAN WARLORD: AUTO-DOWNLOAD DARI GITLAB
# ============================================================================
include(FetchContent)

# 1. Sedot CLAP Pusat
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG main
)

# 2. Sedot PUGL (Ganti master jadi main!)
FetchContent_Declare(
    pugl
    GIT_REPOSITORY https://github.com/lv2/pugl.git
    GIT_TAG main
)

# 3. Sedot NANOVG Pusat
FetchContent_Declare(
    nanovg
    GIT_REPOSITORY https://github.com/memononen/nanovg.git
    GIT_TAG master
)

# Perintahkan CMake buat narik 3 framework ini SEKARANG!
FetchContent_MakeAvailable(clap pugl nanovg)

# ============================================================================
# SETUP TARGET PABRIK (.CLAP)
# ============================================================================
set(CMAKE_SHARED_LIBRARY_SUFFIX ".clap")
set(CMAKE_SHARED_MODULE_SUFFIX ".clap")

# Masak kode monolitik lu DITAMBAH file .c punya NanoVG
add_library(JoyzEquZTone MODULE 
    JOYZEquZTone4K64BandDynamic_Monolith.cpp 
    ${nanovg_SOURCE_DIR}/src/nanovg.c 
)

set_target_properties(JoyzEquZTone PROPERTIES PREFIX "")

# ============================================================================
# AUTO-ROUTING FOLDER INCLUDE (TANPA BIKIN FOLDER MANUAL)
# ============================================================================
target_include_directories(JoyzEquZTone PRIVATE 
    ${clap_SOURCE_DIR}/include 
    ${pugl_SOURCE_DIR}/include 
    ${nanovg_SOURCE_DIR}/src
)

target_link_libraries(JoyzEquZTone PRIVATE
    pugl::pugl_gl
    pugl::pugl
)

# ============================================================================
# LINKING OS (LINUX MURNI) + MENDUKUNG ATOMIC THREADING
# ============================================================================
if(UNIX AND NOT APPLE)
    # Wajib tambahin 'pthread' buat ngebantu proses multi-threading & atomic di Linux
    target_link_libraries(JoyzEquZTone PRIVATE m GL X11 pthread)
endif()

