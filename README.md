# SDFShaderBuilder

**SDFShaderBuilder** is a standalone, hardware-accelerated desktop IDE and visual node-based shader compiler for 3D Signed Distance Field (SDF) raymarching. Built with **C++20**, **WebGPU (`wgpu-native`)**, and **Dear ImGui**, it allows technical artists and graphics programmers to author complex procedural geometry visually or through live code, previewing results in real-time before exporting self-contained shaders for web or engine deployment.

---

## Current Features (v0.1 Prototype)

* **Native WebGPU Rendering Pipeline:** Offscreen render-target architecture utilizing `wgpu-native` with automatic viewport resizing and swapchain lifecycle management.
* **Live WGSL Code Editor & Hot-Reloading:** Integrated multi-line shader editor with synchronous error interception (`wgpuDeviceSetUncapturedErrorCallback`). Syntax and pipeline validation errors are caught without crashing the host application and routed to a dedicated diagnostic console.
* **Interactive Uniform & Timeline Control:** Real-time GPU uniform buffer staging for camera orbit control (`yaw`, `pitch`), quality tier switching, custom parameter crossfading, and timeline scrubbing (Play, Pause, frame-stepping, and reset).
* **Project Serialization:** JSON-based project state saving and loading (`nlohmann/json`), preserving shader source code, camera state, and uniform parameters across sessions.
* **Dockable Workspace:** Built on Dear ImGui's docking branch with an integrated `imgui-node-editor` canvas context ready for graph topology construction.

---

## Architecture & Development Roadmap

The project is actively being expanded from a live WGSL runner into a full multi-target node-to-shader transpiler. Planned architectural milestones include:

### 1. Dual-Modality Workflow (Node Mode & Code Mode)
* **Visual Graph Compositor:** Construct complex SDF hierarchies using Primitive nodes (Sphere, Box, Torus, Vesica, Cone), Domain Repetition/Rotation operators, and Smooth Boolean operations (`smin`, `smax`).
* **One-Way Code Export:** Seamlessly transition from visual node authoring to raw Code Mode, generating clean, human-readable shader scripts ready for manual fine-tuning or web deployment.

### 2. Live Uniform Staging vs. Export-Time Constant Folding
* **Zero-Stutter Graph Tweaking:** In Node Mode, spatial transforms (translation, rotation, scale) and primitive dimensions are dynamically mapped to an internal GPU uniform buffer, allowing 60+ FPS slider manipulation without triggering expensive shader recompilations.
* **Constant Baking:** When exporting the graph to a standalone script, the compiler performs a constant-folding pass-replacing internal uniform references with hardcoded literal values (`vec3f`, `f32`) so the final output is 100% self-contained.

### 3. Intermediate Representation (IR) & Multi-Language Emitters
* **Topological Graph Traversal:** Evaluates the Directed Acyclic Graph (DAG) from the terminal root node, resolving upstream pin dependencies and detecting cycles.
* **Cross-Language Transpilation:** Rather than hardcoding language strings inside UI nodes, the graph lowers into a lightweight Intermediate Representation (IR) before passing through modular backend emitters:
  * **WGSL Emitter:** Targets modern WebGPU canvases.
  * **GLSL Emitter:** Targets WebGL, OpenGL, and Shadertoy-compatible pipelines.
* **Custom Codable Nodes:** Support for user-authored inline function nodes with dynamic input/output pin reflection and automatic lexical scoping to prevent variable name collisions.

### 4. Dynamic Parameter Registry & Semantic Input Routing
* **UI-Driven Uniform Creation:** Users can define custom named parameters (`float`, `vec2`, `vec3`, `color`) directly through the inspector.
* **Automatic STD140 / WGSL Memory Alignment:** A dynamic byte-packer automatically sorts custom parameters and inserts necessary struct padding to satisfy strict 16-byte GPU memory alignment rules.
* **Semantic Bindings:** Bind custom uniform variables directly to hardware and engine inputs via dropdowns (Viewport Resolution, Delta/Accumulated Time, Mouse Coordinates, and Mouse Click States).

### 5. Pluggable Shading & Material Synthesis
* **Material ID Packing:** Standardizes `vec2` distance field returns (`.x` = signed distance, `.y` = material ID + fractional gradient data).
* **Master Lighting Templates:** Selectable rendering backends (Fast Studio Clay, Volumetric Glow & Soft Penumbra Shadows, or Cook-Torrance PBR) that automatically wrap the generated `map()` geometry function and synthesize material branching logic.

---

## Tech Stack

* **Language:** C++20
* **Graphics API:** WebGPU (`wgpu-native` v0.19.x)
* **Windowing & Input:** GLFW 3.3.8
* **GUI Framework:** Dear ImGui (`v1.91.0-docking`)
* **Node Graph UI:** `imgui-node-editor`
* **Serialization:** `nlohmann/json` (v3.11.3)
* **Build System:** CMake (3.24+) with automated `FetchContent` dependency management

---

## Building from Source

### Prerequisites
* **CMake** (v3.24 or higher)
* **Git**
* **C++20 Compatible Compiler** (MSVC 2022 on Windows, Clang/GCC on Linux/macOS)

### Build Instructions

1. **Clone the repository:**
   ```bash
   git clone [https://github.com/](https://github.com/)dDEPIS/SDFShaderBuilder.git
   cd SDFShaderBuilder