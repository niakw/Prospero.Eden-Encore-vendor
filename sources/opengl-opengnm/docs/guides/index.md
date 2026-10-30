# Guides

Tutorial-style walkthroughs for each major subsystem of opengnm. These guides
focus on how to use the API in practice, with code examples and explanations of
the underlying concepts.

---

## Guide List

| Guide | Topics |
|-------|--------|
| [Rendering Pipeline](rendering-pipeline.md) | VideoOut setup, command buffer, draw, submit, flip |
| [Textures & Formats](textures-and-formats.md) | GnmDataFormat, GnmTexture, tiling, BC compression |
| [Render Targets](render-targets.md) | Color RT + Depth RT setup, sizing, alignment |
| [Shaders](shaders.md) | Shader binary format, stage registers, fetch shaders |
| [Command Buffers](command-buffers.md) | GnmCommandBuffer + DrawCmd building, PM4 packets |
| [Surface Computation](surface-computation.md) | sceGpa* / AddrLib: surface info, tiling, decompression |

---

## Prerequisites

All guides assume you have built and installed opengnm. See
[Getting Started](../getting-started.md) for build instructions.

## Including the API

```c
#include <gnm.h>  // Master include — pulls in everything
```

Or include individual headers as needed:

```c
#include <gnm_types.h>
#include <gnm_drawcommandbuffer.h>
#include <gnmdriver.h>
#include <gpuaddr.h>
#include <gnm_helpers.h>
```
