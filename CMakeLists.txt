cmake_minimum_required(VERSION 3.20)
project(five_hundred VERSION 0.1.0 LANGUAGES C)

# ── Options ─────────────────────────────────────────────────────────────────
option(BUILD_WASM "Build WebAssembly target via Emscripten" OFF)

# ── Sub-projects ─────────────────────────────────────────────────────────────
add_subdirectory(client)
