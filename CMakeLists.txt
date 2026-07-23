cmake_minimum_required(VERSION 3.26)
project(dusklight-assets C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)

# The Dusklight version to build against: a release tag (e.g. "v1.5.0") or a commit SHA.
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "95608438c107fc321b82c5878f70a811e7c5913c")
endif ()
# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
# Use -DDUSKLIGHT_DIR= to point at an existing Dusklight checkout.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/dusklight/" EXCLUDE_FROM_ALL)

# add_subdirectory("${DUSKLIGHT_DIR}/sdk" dusklight-sdk EXCLUDE_FROM_ALL)
# add_mod(my_mod
        # FEATURES game          # remove for service/asset-only mods; add webgpu for GfxService
        # SOURCES src/mod.cpp
        # MOD_JSON mod.json
        # RES_DIR res            # mod resources, including icon.png and banner.png
        # OVERLAY_DIR overlay    # game file overlays; remove if unused
        # TEXTURES_DIR textures  # texture replacements; remove if unused
# )

set(ASSETS_LIB_FILES
    src/lib/arc.cpp
    src/lib/assets_main.cpp
    src/lib/assets_pack.cpp
    src/lib/assets_unpack.cpp
    src/lib/ast.cpp
    src/lib/bmd.cpp
    src/lib/iso.cpp
    src/lib/timg.cpp
    src/lib/yaz0_compress.cpp
    dusklight/src/dusk/io.cpp
    dusklight/src/dusk/audio/Adpcm.cpp
    src/lib/OSUtf.c
    dusklight/libs/JSystem/src/JKernel/JKRDecomp.cpp
    dusklight/src/helpers/endian.cpp
)

include_directories(src/lib dusklight/include
    dusklight/
    dusklight/libs/JSystem/include/
    dusklight/assets/GZ2E01
    dusklight/extern/aurora/include/dolphin
    dusklight/extern/aurora/include/
    dusklight/src
    dusklight/extern
    )

add_library(dusklight-assets-lib STATIC ${ASSETS_LIB_FILES})
add_definitions(-DTARGET_PC -DWIDESCREEN_SUPPORT)
target_compile_definitions(dusklight-assets-lib PRIVATE -DTARGET_PC=1 -DWIDESCREEN_SUPPORT=1)
target_precompile_headers(dusklight-assets-lib PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/dusklight/include/dusk_pch.hpp>")
target_link_libraries(dusklight-assets-lib PRIVATE cxxopts::cxxopts nlohmann_json::nlohmann_json PNG::PNG)

add_executable(dusklight-assets src/main.cpp)
target_link_libraries(dusklight-assets PRIVATE dusklight-assets-lib)
