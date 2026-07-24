cmake_minimum_required(VERSION 3.26)
project(dusklight-assets C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)

# The Dusklight version to build against: a release tag (e.g. "v1.5.0") or a commit SHA.
if (NOT DUSKLIGHT_VERSION)
    set(DUSKLIGHT_VERSION "95608438c107fc321b82c5878f70a811e7c5913c")
endif ()
# Fetches the Dusklight sources into ./dusklight and provides DUSKLIGHT_DIR.
# Use -DDUSKLIGHT_DIR= to point at an existing Dusklight checkout.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchDusklight.cmake")


include(FetchContent)

if (NOT TARGET ZLIB::ZLIB)
    message(STATUS "dusklight assets: Fetching zlib")
    FetchContent_Declare(
      ZLIB
      URL https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz
      URL_HASH SHA256=b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      FIND_PACKAGE_ARGS NAMES ZLIB
    )
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ZLIB)

   if (TARGET zlibstatic)
        set(_actual_zlib zlibstatic)
    elseif (TARGET zlib)
        set(_actual_zlib zlib)
    endif ()

    add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    
    target_link_libraries(ZLIB::ZLIB INTERFACE ${_actual_zlib})
    target_include_directories(ZLIB::ZLIB INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
    
    set(ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} CACHE PATH "" FORCE)
    set(ZLIB_FOUND TRUE CACHE BOOL "" FORCE)
endif ()


# Declare all dependencies first so CMake can download them in parallel
message(STATUS "dusklight assets: Fetching cxxopts")
FetchContent_Declare(cxxopts
    URL https://github.com/jarro2783/cxxopts/archive/refs/tags/v3.3.1.tar.gz
    URL_HASH SHA256=3bfc70542c521d4b55a46429d808178916a579b28d048bd8c727ee76c39e2072
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
message(STATUS "dusklight assets: Fetching nlohmann/json")
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_INSTALL OFF CACHE BOOL "" FORCE)
message(STATUS "dusklight assets: Fetching libpng")
FetchContent_Declare(
      PNG
      URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.58.tar.gz
      URL_HASH SHA256=A9D4DF463D36A6E5F9C29BD6F4967312D17E996C1854F3511F833924EB1993CF
      DOWNLOAD_EXTRACT_TIMESTAMP FALSE
      EXCLUDE_FROM_ALL
    )

FetchContent_MakeAvailable(cxxopts json PNG)

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
target_link_libraries(dusklight-assets-lib PRIVATE cxxopts::cxxopts nlohmann_json::nlohmann_json png_static)

add_executable(dusklight-assets src/main.cpp)
target_link_libraries(dusklight-assets PRIVATE dusklight-assets-lib)
