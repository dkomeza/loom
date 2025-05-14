# 2D Buffer Rendering Library - HTML/TypeScript Prototyping Environment (Bun Edition)

This directory contains a lightweight HTML and TypeScript environment, powered by Bun, designed for rapidly prototyping and testing 2D rendering techniques before their implementation in the main C-based 2D buffer rendering library.

## Overview

The primary goal of this prototyping environment is to leverage the speed and ease of use of Bun along with web technologies (HTML, CSS, and TypeScript) to experiment with and visualize various rendering algorithms and visual effects. By testing concepts here first, we can:

* Quickly validate the feasibility and visual output of new techniques.
* Iterate on algorithms and parameters with immediate visual feedback.
* More easily debug visual artifacts and logical errors in a more forgiving environment.
* Create a visual reference for the expected output when implementing the techniques in C.

This environment is **not** intended to be a direct 1:1 port of the C library's functionality but rather a sandbox for conceptual exploration.

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.

### Prerequisites

* A modern web browser (e.g., Chrome, Firefox, Edge, Safari).
* **Bun** installed. You can download and install it from [https://bun.sh/](https://bun.sh/).
* A code editor with TypeScript support (e.g., VS Code, WebStorm).

### Installation & Setup

1.  **Clone the repository (if you haven't already):**
    ```bash
    git clone https://github.com/dkomeza/loom.git
    cd loom/development
    ```

2.  **Install dependencies:**
    Navigate to this directory in your terminal and run:
    ```bash
    bun install
    ```
    This will install any development dependencies defined in `package.json` using Bun's fast installer.
3.  **Development Server / Building:**
    Bun has a built-in development server and can transpile TypeScript on the fly.

    * **For a development server that handles TypeScript and auto-reloads:**
        You can often run your main TypeScript file directly if it sets up a server, or use Bun's built-in HTTP server capabilities. If you are serving static HTML that loads TypeScript modules, Bun can also help here.

        ```bash
        bun run dev
        ```
        Or, if you're just serving static HTML that references compiled JS:
        ```bash
        bun run build
        bun run preview
        ```
**Note:** Adapt this structure to fit your actual project layout. Bun is flexible.

## How to Add a New Rendering Test

1.  **Create a new TypeScript file** in the `src/rendering/` (or equivalent) directory for your new technique (e.g., `myNewEffect.ts`).
2.  **Implement your rendering logic** within this file. You will typically draw onto an HTML5 Canvas element.
3.  **Expose necessary functions** from your new module.
4.  **Import and call your new test** from `src/main.ts` (or your main TypeScript entry point). This might involve:
    * Creating a new canvas element or selecting an existing one dynamically.
    * Setting up UI controls (sliders, buttons) if needed to tweak parameters.
    * Calling your rendering function.
5.  If you are using a development server like `bun --hot src/main.ts`, changes should be reflected automatically in the browser. Otherwise, you might need to re-run your build script (`bun run build`) and refresh the `index.html` page.

## Relationship to the Main C Project

The code written in this TypeScript environment serves as a conceptual blueprint and visual validation tool. It is expected that:

* Algorithms and mathematical logic prototyped here will be translated into C.
* The visual output achieved here will guide the implementation and debugging of the C library.
* Performance considerations will be minimal in this prototyping environment; the C implementation will focus on optimization.
* Not all features or intricacies of the C library (e.g., specific memory management, low-level buffer access) will be replicated here. The focus is on the *rendering techniques themselves*.

## Contributing

If you wish to contribute to the prototyping efforts:

1.  Fork the repository.
2.  Create a new branch (`git checkout -b feature/my-new-rendering-idea`).
3.  Implement your prototype.
4.  Ensure your TypeScript code runs correctly with Bun (e.g., `bun run dev` or `bun src/main.ts`).
5.  Commit your changes (`git commit -am 'Add some amazing new rendering prototype'`).
6.  Push to the branch (`git push origin feature/my-new-rendering-idea`).
7.  Create a new Pull Request.

Please ensure that new prototypes are clearly documented within their respective TypeScript files and, if necessary, update this README or `index.html` (or the main server logic) to make them accessible.