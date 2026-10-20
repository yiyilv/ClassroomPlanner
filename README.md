# ClassroomPlanner

An interactive 3D classroom layout editor built with **C++20 and Vulkan**. Add furniture from an icon toolbar, select objects in the scene, adjust their position and rotation, and save a layout for later use.

## Features

- Furniture spawning from a 2D icon toolbar, with multiple instances per type.
- Mouse-ray selection of existing furniture.
- Translation, vertical movement, rotation, and deletion of selected objects.
- Room-boundary constraints on object movement.
- Orbit-style camera controls with movement and zoom.
- JSON layout persistence through F5 / F9.
- GLSL shaders for the lit scene, icons, and framework overlays.

## Build and run

Requirements: CMake 3.21+, a C++20 compiler, Git, and a Vulkan SDK/runtime with `glslc`. On macOS, a compatible Vulkan portability implementation such as MoltenVK is needed. CMake fetches GLFW 3.4 and GLM 1.0.1 during configuration, so the first build needs network access.

From the repository root:

```bash
git clone https://github.com/yiyilv/ClassroomPlanner.git
cd ClassroomPlanner
cmake --preset default
cmake --build --preset default
cd build
./SkeletonToChange
```

The current CMake target retains the starter name `SkeletonToChange`. On Windows or with a multi-configuration generator, use the executable location produced by that generator and keep its copied `assets/` and `shaders/` directories beside it.

On a Unix-like system, `bash run.sh` performs the default configure, build, and launch sequence. Launch from the build output directory so relative asset paths resolve correctly.

## Controls

| Action | Input |
|---|---|
| Move camera target | W / A / S / D |
| Rotate camera | Arrow keys or mouse drag |
| Zoom | R / F |
| Add furniture | Left-click an icon |
| Select furniture | Right-click an object |
| Clear selection | 0 |
| Move selected object horizontally | I / J / K / L |
| Move selected object vertically | U / O |
| Rotate selected object | Q / E |
| Delete selected object | Delete / Backspace |
| Save layout | F5 |
| Load layout | F9 |
| Exit | Escape |

Layouts are written to `saved_layout.json` in the working directory. Saving replaces that file; loading restores furniture instances from it. On keyboards with media-function keys, the Fn modifier may be necessary.

## Implementation

```text
source/src/main.cpp          planner state, interaction, camera, save/load
source/src/Libs.cpp          bundled library implementations
source/shaders/             scene and icon shaders
source/include/modules/     course rendering framework
source/assets/              furniture, scene, textures, and icons
CMakeLists.txt              dependency setup, shader compilation, asset copying
```

The planner keeps reusable instance slots for each furniture type. A right-click is projected into a world-space ray to select nearby furniture. Per-frame transformations update the rendering data; movement is clamped to the room limits. JSON persistence records the active furniture state.

## My contribution and project context

This is a computer graphics coursework project built on a professor-provided starter framework. My implementation extends that framework with the classroom planner, furniture interaction, object selection and transformation, icon toolbar integration, camera behaviour, room constraints, and layout saving/loading.

The starter framework, bundled libraries, models, and textures have separate origins and are not claimed as my original work. The commit history preserves the initial course skeleton and subsequent implementation changes.

## Current status

Source code, shaders, and tracked assets are included. The layout persistence update is present in commit [7997bd3](https://github.com/yiyilv/ClassroomPlanner/commit/7997bd35fd1c0a7ef7b352af17022253c689d837). Cross-platform builds and a recorded visual demo have not yet been verified for this public release.

## Credits and reuse

The project uses Vulkan, GLFW, GLM, and bundled third-party headers. Preserve the notices contained in those files. No repository-wide licence has been added: the publication and reuse terms for the course framework and asset pack still need to be documented individually.

