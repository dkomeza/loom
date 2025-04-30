# loom

> A small, fast, platform-agnostic C library for rendering graphics primitives directly to memory buffers.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT) [![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

**loom** is a lightweight graphics library designed for performance and portability. It allows you to draw shapes, lines, text, and more directly into a raw memory buffer, making it suitable for embedded systems with limited resources (like ESP32), custom display drivers, game development, or graphical simulations where direct pixel manipulation is needed.

## ✨ Features

- **Minimalist Core:** Focused on essential 2D drawing operations.
- **Platform Agnostic:** Core rendering logic written in standard C with no platform-specific dependencies.
- **Memory Buffer Rendering:** Renders directly to a user-provided memory buffer (e.g., `uint8_t*`, `uint16_t*`).
- **Basic Primitives:** Includes functions for drawing pixels, lines, rectangles (filled/outline), circles, and basic text (using bitmap fonts).
- **Extensible:** Designed to be simple to integrate and potentially extend.
- **Cross-Platform Build:** Uses CMake for easy building on Linux, macOS, and Windows.

## 🎯 Goals

- **Speed:** Optimize drawing algorithms for performance.
- **Small Footprint:** Keep code size and memory usage low, especially for embedded targets.
- **Portability:** Ensure the core library compiles and runs correctly across different architectures (ESP32, ARM, x86).
- **Ease of Use:** Provide a simple and clear C API.

## 📂 Project Structure
```
loom/
├── src/ # Core source files (.c)
├── include/ # Public header files (.h)
├── examples/ # Usage examples (ESP32, Desktop)
├── tests/ # Unit tests
├── docs/ # Documentation
├── CMakeLists.txt # Main build script
├── LICENSE # License file
└── README.md # This file
```
## 🚀 Getting Started

**Prerequisites:**

- CMake (version X.Y or higher)
- A C compiler (GCC, Clang, MSVC)
