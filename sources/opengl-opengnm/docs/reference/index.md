# API Reference

Complete reference for every public function, struct, and enum in opengnm.

---

## Reference Pages

### Core Types

| Page | Header | Contents |
|------|--------|----------|
| [Types & Enums](types.md) | `gnm_types.h` | 51 enums, constants, limits, draw arg structs |
| [Error Handling](error.md) | `gnm_error.h` | `GnmError` codes, message handler API |
| [Data Formats](dataformat.md) | `gnm_dataformat.h` | `GnmDataFormat`, `sceGnmDf*`, `GNM_FMT_*` constants |

### Resource Descriptors

| Page | Header | Contents |
|------|--------|----------|
| [Buffer](buffer.md) | `gnm_buffer.h` | `GnmBuffer` (0x10), create/get/set |
| [Texture](texture.md) | `gnm_texture.h` | `GnmTexture` (0x20), create + 23 accessors |
| [Sampler](sampler.md) | `gnm_sampler.h` | `GnmSampler` (0x10) |
| [Render Target](rendertarget.md) | `gnm_rendertarget.h` | `GnmRenderTarget` (0x40) |
| [Depth Render Target](depthrendertarget.md) | `gnm_depthrendertarget.h` | `GnmDepthRenderTarget` (0x34) |
| [Control Registers](controls.md) | `gnm_controls.h` | Blend, depth-stencil, primitive setup, viewport |

### Shaders

| Page | Header | Contents |
|------|--------|----------|
| [Shader](shader.md) | `gnm_shader.h` | Stage registers, fetch shader, input/export semantics |
| [Shader Binary](shaderbinary.md) | `gnm_shaderbinary.h` | File header, VS/PS containers, binary info |

### Command Buffers

| Page | Header | Contents |
|------|--------|----------|
| [Command Buffer](commandbuffer.md) | `gnm_commandbuffer.h` | `GnmCommandBuffer` (0x40) |
| [Draw Command Buffer](drawcommandbuffer.md) | `gnm_drawcommandbuffer.h` | 63 `sceGnmDrawCmd*` functions |

### Runtime

| Page | Header | Contents |
|------|--------|----------|
| [Driver Runtime](driver.md) | `gnmdriver.h` | 265 `sceGnm*` functions (207+ public + stubs) |
| [Surface Computation](gpuaddr.md) | `gpuaddr.h` | 19 `sceGpa*` functions |
| [Platform](platform.md) | `platform.h` | GPU mode, buffer label |
| [Helpers](helpers.md) | `gnm_helpers.h` | VideoOut, direct memory, validation |

### Utilities

| Page | Header | Contents |
|------|--------|----------|
| [String Utilities](strings.md) | `gnm_strings.h` | 22 enum-to-string converters |
| [Compatibility Layer](compat.md) | `compat/freegnm.h` | `gnm*` → `sceGnm*` aliases |

---

## API Statistics

| Category | Count |
|----------|-------|
| Enums | 63 |
| Structs | 45 |
| Functions (total, including inline) | 536 |
| `sceGnm*` runtime functions | 207+ |
| `sceGnmDrawCmd*` functions | 63 |
| `sceGpa*` functions | 19 |
| `#define` constants | 37 |
