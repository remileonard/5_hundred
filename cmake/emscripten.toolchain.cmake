# Emscripten toolchain for WebAssembly
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/emscripten.toolchain.cmake -DBUILD_WASM=ON ..
#
# Requires the EMSDK environment variable to point to your emsdk installation,
# or emcc/em++ to be on your PATH (i.e. `source emsdk_env.sh` was run).

set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_SYSTEM_VERSION 1)

# Locate emcc
if(DEFINED ENV{EMSDK})
    set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
else()
    find_program(_EMCC emcc REQUIRED)
    get_filename_component(EMSCRIPTEN_ROOT "${_EMCC}" DIRECTORY)
endif()

set(CMAKE_C_COMPILER   "${EMSCRIPTEN_ROOT}/emcc")
set(CMAKE_CXX_COMPILER "${EMSCRIPTEN_ROOT}/em++")
set(CMAKE_AR           "${EMSCRIPTEN_ROOT}/emar")
set(CMAKE_RANLIB       "${EMSCRIPTEN_ROOT}/emranlib")

# Skip compiler tests (they don't work in cross-compile mode without extra setup)
set(CMAKE_C_COMPILER_WORKS   1)
set(CMAKE_CXX_COMPILER_WORKS 1)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
