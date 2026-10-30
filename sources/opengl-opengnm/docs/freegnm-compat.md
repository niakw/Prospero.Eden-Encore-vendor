# freegnm Compatibility

opengnm provides a source-level compatibility layer for projects that used the
old **freegnm** `gnm*` wrapper API. This allows existing freegnm consumers to
switch to opengnm with minimal code changes.

---

## How It Works

The compatibility layer is implemented entirely through preprocessor `#define`
aliases in `<compat/freegnm.h>`. When included, it rewrites `gnm*` function
names to their `sceGnm*` equivalents at compile time.

**No second ABI is exported.** Linked objects still call the Sony SDK-style
`sceGnm*` / `sceGpa*` symbols. The compatibility is source-only.

---

## Usage

### Option 1: Include the compat header

```c
#include <compat/freegnm.h>

// Now gnm* names work:
GnmTexture tex;
gnmTexSetWidth(&tex, 1920);
gnmTexSetHeight(&tex, 1080);
```

### Option 2: Switch include path to opengnm

Keep core includes such as `<gnm/drawcommandbuffer.h>`, `<gnm/platform.h>`,
and `<gnm/gpuaddr/gpuaddr.h>`. The `gnm/` forwarding headers in opengnm
include `<compat/freegnm.h>` automatically.

---

## Aliased Functions

The compatibility header maps the following categories of `gnm*` names to
`sceGnm*`:

### Error & Platform

| freegnm | opengnm |
|---------|---------|
| `gnmStrError` | `sceGnmStrError` |
| `gnmSetMessageHandler` | `sceGnmSetMessageHandler` |
| `gnmWriteMsg` | `sceGnmWriteMsg` |
| `gnmWriteMsgf` | `sceGnmWriteMsgf` |
| `gnmGpuMode` | `sceGnmGpuMode` |
| `gnmPlatInit` | `sceGnmPlatInit` |
| `gnmPlatGetBufferLabelAddress` | `sceGnmPlatGetBufferLabelAddress` |

### GpuAddr (sceGpa*)

| freegnm | opengnm |
|---------|---------|
| `gpaStrError` | `sceGpaStrError` |
| `gpaComputeSurfaceInfo` | `sceGpaComputeSurfaceInfo` |
| `gpaComputeHtileInfo` | `sceGpaComputeHtileInfo` |
| `gpaComputeCmaskInfo` | `sceGpaComputeCmaskInfo` |
| `gpaComputeFmaskInfo` | `sceGpaComputeFmaskInfo` |
| `gpaComputeSurfaceTileMode` | `sceGpaComputeSurfaceTileMode` |
| `gpaInitSurfaceContext` | `sceGpaInitSurfaceContext` |
| `gpaComputeSurfaceCoord` | `sceGpaComputeSurfaceCoord` |
| `gpaComputeSurfaceSizeOffset` | `sceGpaComputeSurfaceSizeOffset` |
| `gpaFindOptimalSurface` | `sceGpaFindOptimalSurface` |
| `gpaGetTileInfo` | `sceGpaGetTileInfo` |
| `gpaComputeBaseSwizzle` | `sceGpaComputeBaseSwizzle` |
| `gpaGetDecompressedSize` | `sceGpaGetDecompressedSize` |
| `gpaDecompressTexture` | `sceGpaDecompressTexture` |
| `gpaTpInit` | `sceGpaTpInit` |
| `gpaTileSurface` | `sceGpaTileSurface` |
| `gpaTileSurfaceRegion` | `sceGpaTileSurfaceRegion` |
| `gpaTileTextureIndexed` | `sceGpaTileTextureIndexed` |
| `gpaTileTextureAll` | `sceGpaTileTextureAll` |

### Command Buffers

| freegnm | opengnm |
|---------|---------|
| `gnmCmdInit` | `sceGnmCmdInit` |
| `gnmCmdReset` | `sceGnmCmdReset` |
| `gnmCmdAllocInside` | `sceGnmCmdAllocInside` |

### Data Formats

| freegnm | opengnm |
|---------|---------|
| `gnmDfInitFromFmask` | `sceGnmDfInitFromFmask` |
| `gnmDfInitFromZ` | `sceGnmDfInitFromZ` |
| `gnmDfInitFromStencil` | `sceGnmDfInitFromStencil` |
| `gnmDfGetTexelsPerElement` | `sceGnmDfGetTexelsPerElement` |
| `gnmDfGetNumComponents` | `sceGnmDfGetNumComponents` |
| `gnmDfGetBitsPerElement` | `sceGnmDfGetBitsPerElement` |
| `gnmDfGetTotalBitsPerElement` | `sceGnmDfGetTotalBitsPerElement` |
| `gnmDfGetBytesPerElement` | `sceGnmDfGetBytesPerElement` |
| `gnmDfGetTotalBytesPerElement` | `sceGnmDfGetTotalBytesPerElement` |
| `gnmDfIsBlockCompressed` | `sceGnmDfIsBlockCompressed` |
| `gnmDfGetRtChannelType` | `sceGnmDfGetRtChannelType` |
| `gnmDfGetRtChannelOrder` | `sceGnmDfGetRtChannelOrder` |
| `gnmDfGetZFormat` | `sceGnmDfGetZFormat` |
| `gnmDfGetStencilFormat` | `sceGnmDfGetStencilFormat` |
| `gnmDfGetTexelsPerElementWide` | `sceGnmDfGetTexelsPerElementWide` |
| `gnmDfGetTexelsPerElementTall` | `sceGnmDfGetTexelsPerElementTall` |

### Buffer

| freegnm | opengnm |
|---------|---------|
| `gnmBufGetBaseAddress` | `sceGnmBufGetBaseAddress` |
| `gnmBufSetBaseAddress` | `sceGnmBufSetBaseAddress` |
| `gnmBufGetFormat` | `sceGnmBufGetFormat` |
| `gnmBufSetFormat` | `sceGnmBufSetFormat` |
| `gnmBufSetMemoryType` | `sceGnmBufSetMemoryType` |
| `gnmCreateConstBuffer` | `sceGnmCreateConstBuffer` |
| `gnmCreateVertexBuffer` | `sceGnmCreateVertexBuffer` |

### Texture

| freegnm | opengnm |
|---------|---------|
| `gnmCreateTexture` | `sceGnmCreateTexture` |
| `gnmTexGetBaseAddress` | `sceGnmTexGetBaseAddress` |
| `gnmTexSetBaseAddress` | `sceGnmTexSetBaseAddress` |
| `gnmTexGetFormat` / `gnmTexSetFormat` | `sceGnmTexGetFormat` / `sceGnmTexSetFormat` |
| `gnmTexGetWidth` / `gnmTexSetWidth` | `sceGnmTexGetWidth` / `sceGnmTexSetWidth` |
| `gnmTexGetHeight` / `gnmTexSetHeight` | `sceGnmTexGetHeight` / `sceGnmTexSetHeight` |
| `gnmTexGetDepth` / `gnmTexSetDepth` | `sceGnmTexGetDepth` / `sceGnmTexSetDepth` |
| `gnmTexGetPitch` / `gnmTexSetPitch` | `sceGnmTexGetPitch` / `sceGnmTexSetPitch` |
| `gnmTexGetBaseMipLevel` | `sceGnmTexGetBaseMipLevel` |
| `gnmTexGetLastMipLevel` | `sceGnmTexGetLastMipLevel` |
| `gnmTexGetNumMips` | `sceGnmTexGetNumMips` |
| `gnmTexGetNumFaces` | `sceGnmTexGetNumFaces` |
| `gnmTexGetTotalArraySlices` | `sceGnmTexGetTotalArraySlices` |
| `gnmTexGetNumArraySlices` | `sceGnmTexGetNumArraySlices` |
| `gnmTexGetNumFragments` | `sceGnmTexGetNumFragments` |
| `gnmTexSetMemoryType` | `sceGnmTexSetMemoryType` |
| `gnmTexBuildInfo` | `sceGnmTexBuildInfo` |
| `gnmTexCalcByteSize` | `sceGnmTexCalcByteSize` |

### Render Target

| freegnm | opengnm |
|---------|---------|
| `gnmCreateRenderTarget` | `sceGnmCreateRenderTarget` |
| `gnmRtGetFormat` | `sceGnmRtGetFormat` |
| `gnmRtGetBaseAddr` / `gnmRtSetBaseAddr` | `sceGnmRtGetBaseAddr` / `sceGnmRtSetBaseAddr` |
| `gnmRtGetPitch` | `sceGnmRtGetPitch` |
| `gnmRtGetSliceSize` | `sceGnmRtGetSliceSize` |
| `gnmRtGetNumSlices` | `sceGnmRtGetNumSlices` |
| `gnmRtGetNumSamples` | `sceGnmRtGetNumSamples` |
| `gnmRtGetNumFragments` | `sceGnmRtGetNumFragments` |
| `gnmRtBuildInfo` | `sceGnmRtBuildInfo` |
| `gnmRtCalcByteSize` | `sceGnmRtCalcByteSize` |

### Depth Render Target

All `gnmDrt*` functions map to `sceGnmDrt*` — including `CalcByteSize`,
`CalcStencilByteOffset`, `Get/SetNumFragments`, `SetTileMode`,
`Get/SetZReadAddress`, `Get/SetStencilReadAddress`, `Get/SetZWriteAddress`,
`Get/SetStencilWriteAddress`, `Get/SetSliceSize`, `Get/SetPaddedWidth`,
`Get/SetPaddedHeight`, `GetNumSlices`, `Get/SetHtileAddress`,
`GetMinGpuMode`, `Get/SetWidth`, `Get/SetHeight`.

### Shader Helpers

All `gnmVsRegs*`, `gnmPsRegs*`, `gnmCsRegs*`, `gnmGsRegs*`, `gnmEsRegs*`,
`gnmHsRegs*`, `gnmLsRegs*` functions map to their `sceGnm*` equivalents.
Fetch shader functions (`gnmFetchShaderCalcSize`, `gnmCreateFetchShader`,
`gnmVsRegsSetFetchShaderModifier`) and shader binary accessors
(`gnmShaderCommonCodeSize`, `gnmShfCommonData`, `gnmVsShader*`,
`gnmPsShader*`) are also aliased.

### Draw Command Buffer

All ~50 `gnmDrawCmd*` functions map to `sceGnmDrawCmd*` — including draw
commands, state setup, shader set, dispatch, resource binding, control
registers, sync/events, stream-out, and occlusion queries.

### Driver Wrappers

Firmware/driver wrappers that had exact freegnm equivalents:

| freegnm | opengnm |
|---------|---------|
| `gnmDriverDrawInitDefaultHardwareState350` | `sceGnmDriverDrawInitDefaultHardwareState350` |
| `gnmDriverDrawIndex` | `sceGnmDriverDrawIndex` |
| `gnmDriverDrawIndexAuto` | `sceGnmDriverDrawIndexAuto` |
| `gnmDriverDrawIndexIndirect` | `sceGnmDriverDrawIndexIndirect` |
| `gnmDriverDrawIndirect` | `sceGnmDriverDrawIndirect` |
| `gnmDriverDrawIndexIndirectMulti` | `sceGnmDriverDrawIndexIndirectMulti` |
| `gnmDriverDrawIndirectMulti` | `sceGnmDriverDrawIndirectMulti` |
| `gnmDriverDrawIndexIndirectCountMulti` | `sceGnmDriverDrawIndexIndirectCountMulti` |
| `gnmDriverSetVsShader` | `sceGnmDriverSetVsShader` |
| `gnmDriverSetPsShader` | `sceGnmDriverSetPsShader` |
| `gnmDriverSetPsShader350` | `sceGnmDriverSetPsShader350` |
| `gnmDriverSetEmbeddedVsShader` | `sceGnmDriverSetEmbeddedVsShader` |
| `gnmDriverSetEmbeddedPsShader` | `sceGnmDriverSetEmbeddedPsShader` |
| `gnmDriverInsertWaitFlipDone` | `sceGnmDriverInsertWaitFlipDone` |

---

## Migration Notes

- Consumers that include old tooling-only headers such as `gnm/pssl/*` or
  `gnm/gnf/*` still need migration to the split tool libraries.
- Eden and `freegnm-examples` currently consume the older `gnm*` wrapper API
  from freegnm; the first adapter layer now covers one-to-one core headers and
  wrapper names.
- `freegnm-examples/triangle`, `freegnm-examples/eden-composite-blit`,
  `freegnm-examples/eden-composite-dma`, and the C++ wrapper target
  `freegnm-examples/eden-triangle-wrapper` now link with `USE_OPENGNM=1`.
