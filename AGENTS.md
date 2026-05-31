# AGENTS.md

## Project Overview

Loom is a small immediate-mode tiled graphics renderer for embedded displays.
The portable core records draw commands for a frame, rasterizes intersecting
commands into fixed-height RGB888 tiles, and delegates tile flushing to the host
platform.

The core library is platform-neutral. ESP-IDF integration lives separately in
the ESP-IDF backend.

## Repository Layout

- `include/loom/` contains public API headers.
- `src/` contains the portable renderer, command recording, rasterization,
  platform abstraction, and built-in fonts.
- `src/platform/esp_idf/` contains ESP-IDF-specific backend code.
- `tests/` contains portable CMake tests.
- `CMakeLists.txt` supports both portable CMake builds and ESP-IDF component
  builds.
- `Kconfig` contains ESP-IDF menuconfig options.
- `idf_component.yml` contains ESP-IDF component metadata.

## Build and Test

For the portable build:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

For a portable performance-log build:

```sh
cmake -S . -B build-perf -DLOOM_ENABLE_PERF_LOG=ON -DLOOM_PERF_LOG_LEVEL=INFO
cmake --build build-perf
```

Run the portable CMake test path after changes to the core renderer, public
headers, rasterizer, command handling, or platform abstraction.

For ESP-IDF-only changes, verify that code is correctly guarded for ESP-IDF
builds and note whether an ESP-IDF hardware/build test was run.

## Coding Guidelines

- Keep the portable core free of direct ESP-IDF dependencies.
- Put ESP-IDF-specific logic in `src/platform/esp_idf/` or behind existing
  compile-time guards such as `LOOM_ENABLE_ESP_IDF`.
- Preserve the current C style: C11, two-space indentation, compact helpers,
  and explicit error returns using `loom_err_t`.
- Public API changes belong in `include/loom/` and should be reflected in the
  implementation and tests.
- Prefer small, focused changes over broad refactors.
- Avoid adding allocations or expensive work to per-frame or per-tile paths
  unless the tradeoff is intentional and documented in code.
- Keep performance/debug logging controlled by the existing compile-time flags
  and log-level settings.

## Testing Expectations

Add or update focused tests when changing behavior around:

- argument validation
- frame lifecycle
- command recording
- clipping and dirty rectangles
- tile rendering
- rasterization output
- platform flush behavior
- public API contracts

If a change cannot be covered by the portable smoke test, document the manual or
ESP-IDF verification performed.
