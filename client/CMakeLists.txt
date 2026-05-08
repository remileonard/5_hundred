cmake_minimum_required(VERSION 3.20)
project(five_hundred_client VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── cJSON (vendored via FetchContent) ─────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG        v1.7.18
    GIT_SHALLOW    TRUE
)
set(ENABLE_CJSON_TEST   OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS  OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(cjson)

# ── Sources ───────────────────────────────────────────────────────────────────
file(GLOB_RECURSE CLIENT_SOURCES "src/*.c")

if(BUILD_WASM)
    # Exclude the native-only WebSocket implementation
    list(FILTER CLIENT_SOURCES EXCLUDE REGEX "net_native\\.c$")
else()
    # Exclude the WASM-only WebSocket implementation
    list(FILTER CLIENT_SOURCES EXCLUDE REGEX "net_wasm\\.c$")
endif()

if(BUILD_WASM)
    # ── WebAssembly / Emscripten target ──────────────────────────────────────
    add_executable(five_hundred ${CLIENT_SOURCES})

    target_include_directories(five_hundred PRIVATE
        ${cjson_SOURCE_DIR}
        src
    )

    # SDL2 is bundled with Emscripten
    target_compile_options(five_hundred PRIVATE
        -sUSE_SDL=2
        -sUSE_SDL_IMAGE=2
        -sUSE_SDL_TTF=2
        -sUSE_SDL_MIXER=2
    )

    set(EM_LINK_FLAGS
        "-sUSE_SDL=2 -sUSE_SDL_IMAGE=2 -sUSE_SDL_TTF=2 -sUSE_SDL_MIXER=2"
        "-sWASM=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sASYNCIFY=1"
        "-sWEBSOCKET_SUBPROTOCOL=five-hundred"
        "--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/shell.html"
        "--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    )
    string(JOIN " " EM_LINK_FLAGS_STR ${EM_LINK_FLAGS})
    set_target_properties(five_hundred PROPERTIES
        SUFFIX ".html"
        LINK_FLAGS "${EM_LINK_FLAGS_STR}"
    )

    target_link_libraries(five_hundred PRIVATE cjson)
else()
    # ── Native target ────────────────────────────────────────────────────────
    add_executable(five_hundred ${CLIENT_SOURCES})

    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    find_package(SDL2_mixer REQUIRED)

    # Add parent dirs for each SDL2 lib so that #include <SDL2/SDL_xxx.h> works
    foreach(_sdl_target SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf SDL2_mixer::SDL2_mixer)
        get_target_property(_sdl_inc ${_sdl_target} INTERFACE_INCLUDE_DIRECTORIES)
        if(_sdl_inc)
            get_filename_component(_sdl_inc_parent "${_sdl_inc}" DIRECTORY)
            list(APPEND _SDL_PARENT_DIRS "${_sdl_inc_parent}")
        endif()
    endforeach()

    # libwebsockets (required for native WebSocket support)
    find_package(Threads REQUIRED)
    find_package(libwebsockets CONFIG QUIET)
    if(libwebsockets_FOUND)
        set(_LWS_TARGET websockets)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)
        set(_LWS_TARGET ${LIBWEBSOCKETS_LIBRARIES})
    endif()

    target_include_directories(five_hundred PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${_SDL_PARENT_DIRS}
        ${LIBWEBSOCKETS_INCLUDE_DIRS}
        ${cjson_SOURCE_DIR}
        src
    )

    target_link_libraries(five_hundred PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf
        SDL2_mixer::SDL2_mixer
        ${_LWS_TARGET}
        Threads::Threads
        cjson
    )

    # Warnings
    target_compile_options(five_hundred PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

