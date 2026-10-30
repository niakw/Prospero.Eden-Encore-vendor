# Compatibility Layer (freegnm)

API reference for the source-level compatibility layer that maps old `freegnm` `gnm*` / `gpa*` wrapper names to the Sony SDK-style `sceGnm*` / `sceGpa*` ABI that opengnm implements.

Header: `compat/freegnm.h`

---

## Overview

This header provides **source-level compatibility** for projects that were written against the old `freegnm` library's `gnm*` wrapper naming convention. It uses preprocessor `#define` aliases to rewrite calls at compile time — no second ABI is exported, and no additional symbols are introduced into the binary.

```c
#include <gnm.h>
```

The compatibility header includes `<gnm.h>` (the main opengnm umbrella header) and then defines macros that map the old names to the canonical `sceGnm*` / `sceGpa*` functions.

### How It Works

- Each `#define` maps an old `freegnm` name to the corresponding `sceGnm*` or `sceGpa*` symbol.
- The preprocessor performs a textual substitution, so calling `gnmCmdInit(...)` is compiled as `sceGnmCmdInit(...)`.
- **No second ABI is exported** — the binary only contains the `sceGnm*` symbols. The `gnm*` names exist only in source.

---

## Error / Platform

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
| `gnmStrError` | `sceGnmStrError` |
| `gnmSetMessageHandler` | `sceGnmSetMessageHandler` |
| `gnmWriteMsg` | `sceGnmWriteMsg` |
| `gnmWriteMsgf` | `sceGnmWriteMsgf` |
| `gnmGpuMode` | `sceGnmGpuMode` |
| `gnmPlatInit` | `sceGnmPlatInit` |
| `gnmPlatGetBufferLabelAddress` | `sceGnmPlatGetBufferLabelAddress` |

---

## GPU Address (gpuaddr)

| `freegnm` Name | opengnm (`sceGpa*`) Name |
|----------------|--------------------------|
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

---

## Command Buffers

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
| `gnmCmdInit` | `sceGnmCmdInit` |
| `gnmCmdReset` | `sceGnmCmdReset` |
| `gnmCmdAllocInside` | `sceGnmCmdAllocInside` |

---

## Data Formats

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
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

---

## Buffers & Samplers

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
| `gnmBufGetBaseAddress` | `sceGnmBufGetBaseAddress` |
| `gnmBufSetBaseAddress` | `sceGnmBufSetBaseAddress` |
| `gnmBufGetFormat` | `sceGnmBufGetFormat` |
| `gnmBufSetFormat` | `sceGnmBufSetFormat` |
| `gnmBufSetMemoryType` | `sceGnmBufSetMemoryType` |
| `gnmCreateConstBuffer` | `sceGnmCreateConstBuffer` |
| `gnmCreateVertexBuffer` | `sceGnmCreateVertexBuffer` |
| `gnmSampGetAnisotropyRatio` | `sceGnmSampGetAnisotropyRatio` |

---

## Textures

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
| `gnmCreateTexture` | `sceGnmCreateTexture` |
| `gnmTexGetBaseAddress` | `sceGnmTexGetBaseAddress` |
| `gnmTexSetBaseAddress` | `sceGnmTexSetBaseAddress` |
| `gnmTexGetFormat` | `sceGnmTexGetFormat` |
| `gnmTexSetFormat` | `sceGnmTexSetFormat` |
| `gnmTexGetWidth` | `sceGnmTexGetWidth` |
| `gnmTexSetWidth` | `sceGnmTexSetWidth` |
| `gnmTexGetHeight` | `sceGnmTexGetHeight` |
| `gnmTexSetHeight` | `sceGnmTexSetHeight` |
| `gnmTexGetDepth` | `sceGnmTexGetDepth` |
| `gnmTexSetDepth` | `sceGnmTexSetDepth` |
| `gnmTexGetPitch` | `sceGnmTexGetPitch` |
| `gnmTexSetPitch` | `sceGnmTexSetPitch` |
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

---

## Render Targets

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
| `gnmCreateRenderTarget` | `sceGnmCreateRenderTarget` |
| `gnmRtGetFormat` | `sceGnmRtGetFormat` |
| `gnmRtGetBaseAddr` | `sceGnmRtGetBaseAddr` |
| `gnmRtSetBaseAddr` | `sceGnmRtSetBaseAddr` |
| `gnmRtGetPitch` | `sceGnmRtGetPitch` |
| `gnmRtGetSliceSize` | `sceGnmRtGetSliceSize` |
| `gnmRtGetNumSlices` | `sceGnmRtGetNumSlices` |
| `gnmRtGetNumSamples` | `sceGnmRtGetNumSamples` |
| `gnmRtGetNumFragments` | `sceGnmRtGetNumFragments` |
| `gnmRtBuildInfo` | `sceGnmRtBuildInfo` |
| `gnmRtCalcByteSize` | `sceGnmRtCalcByteSize` |

---

## Depth Render Targets

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
| `gnmCreateDepthRenderTarget` | `sceGnmCreateDepthRenderTarget` |
| `gnmDrtCalcByteSize` | `sceGnmDrtCalcByteSize` |
| `gnmDrtCalcStencilByteOffset` | `sceGnmDrtCalcStencilByteOffset` |
| `gnmDrtGetNumFragments` | `sceGnmDrtGetNumFragments` |
| `gnmDrtSetNumFragments` | `sceGnmDrtSetNumFragments` |
| `gnmDrtSetTileMode` | `sceGnmDrtSetTileMode` |
| `gnmDrtGetZReadAddress` | `sceGnmDrtGetZReadAddress` |
| `gnmDrtSetZReadAddress` | `sceGnmDrtSetZReadAddress` |
| `gnmDrtGetStencilReadAddress` | `sceGnmDrtGetStencilReadAddress` |
| `gnmDrtSetStencilReadAddress` | `sceGnmDrtSetStencilReadAddress` |
| `gnmDrtGetZWriteAddress` | `sceGnmDrtGetZWriteAddress` |
| `gnmDrtSetZWriteAddress` | `sceGnmDrtSetZWriteAddress` |
| `gnmDrtGetStencilWriteAddress` | `sceGnmDrtGetStencilWriteAddress` |
| `gnmDrtSetStencilWriteAddress` | `sceGnmDrtSetStencilWriteAddress` |
| `gnmDrtGetSliceSize` | `sceGnmDrtGetSliceSize` |
| `gnmDrtSetSliceSize` | `sceGnmDrtSetSliceSize` |
| `gnmDrtGetPaddedWidth` | `sceGnmDrtGetPaddedWidth` |
| `gnmDrtSetPaddedWidth` | `sceGnmDrtSetPaddedWidth` |
| `gnmDrtGetPaddedHeight` | `sceGnmDrtGetPaddedHeight` |
| `gnmDrtSetPaddedHeight` | `sceGnmDrtSetPaddedHeight` |
| `gnmDrtGetNumSlices` | `sceGnmDrtGetNumSlices` |
| `gnmDrtGetHtileAddress` | `sceGnmDrtGetHtileAddress` |
| `gnmDrtSetHtileAddress` | `sceGnmDrtSetHtileAddress` |
| `gnmDrtGetMinGpuMode` | `sceGnmDrtGetMinGpuMode` |
| `gnmDrtGetWidth` | `sceGnmDrtGetWidth` |
| `gnmDrtSetWidth` | `sceGnmDrtSetWidth` |
| `gnmDrtGetHeight` | `sceGnmDrtGetHeight` |
| `gnmDrtSetHeight` | `sceGnmDrtSetHeight` |

---

## Shader Helpers

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
| `gnmVsRegsSetAddress` | `sceGnmVsRegsSetAddress` |
| `gnmPsRegsSetAddress` | `sceGnmPsRegsSetAddress` |
| `gnmCsRegsSetAddress` | `sceGnmCsRegsSetAddress` |
| `gnmGsRegsSetAddress` | `sceGnmGsRegsSetAddress` |
| `gnmEsRegsSetAddress` | `sceGnmEsRegsSetAddress` |
| `gnmHsRegsSetAddress` | `sceGnmHsRegsSetAddress` |
| `gnmLsRegsSetAddress` | `sceGnmLsRegsSetAddress` |
| `gnmFetchShaderCalcSize` | `sceGnmFetchShaderCalcSize` |
| `gnmCreateFetchShader` | `sceGnmCreateFetchShader` |
| `gnmVsRegsSetFetchShaderModifier` | `sceGnmVsRegsSetFetchShaderModifier` |
| `gnmShaderCommonCodeSize` | `sceGnmShaderCommonCodeSize` |
| `gnmShaderInputUsageTypeSize` | `sceGnmShaderInputUsageTypeSize` |
| `gnmShfCommonData` | `sceGnmShfCommonData` |
| `gnmVsShaderInputUsageSlotTable` | `sceGnmVsShaderInputUsageSlotTable` |
| `gnmVsShaderInputSemanticTable` | `sceGnmVsShaderInputSemanticTable` |
| `gnmVsShaderExportSemanticTable` | `sceGnmVsShaderExportSemanticTable` |
| `gnmVsShaderCalcSize` | `sceGnmVsShaderCalcSize` |
| `gnmVsShaderCodePtr` | `sceGnmVsShaderCodePtr` |
| `gnmPsShaderInputUsageSlotTable` | `sceGnmPsShaderInputUsageSlotTable` |
| `gnmPsShaderInputSemanticTable` | `sceGnmPsShaderInputSemanticTable` |
| `gnmPsShaderCalcSize` | `sceGnmPsShaderCalcSize` |
| `gnmPsShaderCodePtr` | `sceGnmPsShaderCodePtr` |

---

## Draw Command Buffer

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
| `gnmDrawCmdInitDefaultHardwareState` | `sceGnmDrawCmdInitDefaultHardwareState` |
| `gnmDrawCmdDrawIndex` | `sceGnmDrawCmdDrawIndex` |
| `gnmDrawCmdDrawIndex2` | `sceGnmDrawCmdDrawIndex2` |
| `gnmDrawCmdDrawIndexAuto` | `sceGnmDrawCmdDrawIndexAuto` |
| `gnmDrawCmdDrawIndexAuto2` | `sceGnmDrawCmdDrawIndexAuto2` |
| `gnmDrawCmdDrawIndexIndirect` | `sceGnmDrawCmdDrawIndexIndirect` |
| `gnmDrawCmdDrawIndexIndirect2` | `sceGnmDrawCmdDrawIndexIndirect2` |
| `gnmDrawCmdDrawIndirect` | `sceGnmDrawCmdDrawIndirect` |
| `gnmDrawCmdDrawIndirect2` | `sceGnmDrawCmdDrawIndirect2` |
| `gnmDrawCmdDrawIndexIndirectMulti` | `sceGnmDrawCmdDrawIndexIndirectMulti` |
| `gnmDrawCmdDrawIndirectMulti` | `sceGnmDrawCmdDrawIndirectMulti` |
| `gnmDrawCmdDrawIndexIndirectCountMulti` | `sceGnmDrawCmdDrawIndexIndirectCountMulti` |
| `gnmDrawCmdSetDepthClearValue` | `sceGnmDrawCmdSetDepthClearValue` |
| `gnmDrawCmdSetDepthRenderTarget` | `sceGnmDrawCmdSetDepthRenderTarget` |
| `gnmDrawCmdSetGuardBands` | `sceGnmDrawCmdSetGuardBands` |
| `gnmDrawCmdSetHwScreenOffset` | `sceGnmDrawCmdSetHwScreenOffset` |
| `gnmDrawCmdSetIndexBuffer` | `sceGnmDrawCmdSetIndexBuffer` |
| `gnmDrawCmdSetIndexCount` | `sceGnmDrawCmdSetIndexCount` |
| `gnmDrawCmdSetIndexSize` | `sceGnmDrawCmdSetIndexSize` |
| `gnmDrawCmdSetIndirectArgs` | `sceGnmDrawCmdSetIndirectArgs` |
| `gnmDrawCmdSetIndexedIndirectArgs` | `sceGnmDrawCmdSetIndexedIndirectArgs` |
| `gnmDrawCmdSetInstanceStepRate` | `sceGnmDrawCmdSetInstanceStepRate` |
| `gnmDrawCmdSetNumInstances` | `sceGnmDrawCmdSetNumInstances` |
| `gnmDrawCmdSetPrimitiveType` | `sceGnmDrawCmdSetPrimitiveType` |
| `gnmDrawCmdSetRenderTarget` | `sceGnmDrawCmdSetRenderTarget` |
| `gnmDrawCmdSetRenderTargetMask` | `sceGnmDrawCmdSetRenderTargetMask` |
| `gnmDrawCmdSetScreenScissor` | `sceGnmDrawCmdSetScreenScissor` |
| `gnmDrawCmdSetViewport` | `sceGnmDrawCmdSetViewport` |
| `gnmDrawCmdSetPsShader` | `sceGnmDrawCmdSetPsShader` |
| `gnmDrawCmdSetEmbeddedPsShader` | `sceGnmDrawCmdSetEmbeddedPsShader` |
| `gnmDrawCmdSetVsShader` | `sceGnmDrawCmdSetVsShader` |
| `gnmDrawCmdSetEmbeddedVsShader` | `sceGnmDrawCmdSetEmbeddedVsShader` |
| `gnmDrawCmdSetCsShader` | `sceGnmDrawCmdSetCsShader` |
| `gnmDrawCmdSetCsShaderWithModifier` | `sceGnmDrawCmdSetCsShaderWithModifier` |
| `gnmDrawCmdSetGsShader` | `sceGnmDrawCmdSetGsShader` |
| `gnmDrawCmdSetEsShader` | `sceGnmDrawCmdSetEsShader` |
| `gnmDrawCmdSetHsShader` | `sceGnmDrawCmdSetHsShader` |
| `gnmDrawCmdSetLsShader` | `sceGnmDrawCmdSetLsShader` |
| `gnmDrawCmdDispatchDirect` | `sceGnmDrawCmdDispatchDirect` |
| `gnmDrawCmdDispatchIndirect` | `sceGnmDrawCmdDispatchIndirect` |
| `gnmDrawCmdDrawIndexOffset` | `sceGnmDrawCmdDrawIndexOffset` |
| `gnmDrawCmdSetPsInputUsage` | `sceGnmDrawCmdSetPsInputUsage` |
| `gnmDrawCmdSetVsharpUserData` | `sceGnmDrawCmdSetVsharpUserData` |
| `gnmDrawCmdSetTsharpUserData` | `sceGnmDrawCmdSetTsharpUserData` |
| `gnmDrawCmdSetSsharpUserData` | `sceGnmDrawCmdSetSsharpUserData` |
| `gnmDrawCmdSetPointerUserData` | `sceGnmDrawCmdSetPointerUserData` |
| `gnmDrawCmdSetBlendControl` | `sceGnmDrawCmdSetBlendControl` |
| `gnmDrawCmdSetDepthStencilControl` | `sceGnmDrawCmdSetDepthStencilControl` |
| `gnmDrawCmdSetDbRenderControl` | `sceGnmDrawCmdSetDbRenderControl` |
| `gnmDrawCmdSetPrimitiveSetup` | `sceGnmDrawCmdSetPrimitiveSetup` |
| `gnmDrawCmdSetViewportTransformControl` | `sceGnmDrawCmdSetViewportTransformControl` |
| `gnmDrawCmdEventWriteEop` | `sceGnmDrawCmdEventWriteEop` |
| `gnmDrawCmdFillMemory` | `sceGnmDrawCmdFillMemory` |
| `gnmDrawCmdCopyMemory` | `sceGnmDrawCmdCopyMemory` |
| `gnmDrawCmdWaitGraphicsWrite` | `sceGnmDrawCmdWaitGraphicsWrite` |
| `gnmDrawCmdWaitMem` | `sceGnmDrawCmdWaitMem` |
| `gnmDrawCmdWaitUntilSafeForRendering` | `sceGnmDrawCmdWaitUntilSafeForRendering` |
| `gnmDrawCmdSetStreamOutConfig` | `sceGnmDrawCmdSetStreamOutConfig` |
| `gnmDrawCmdSetStreamOutBuffer` | `sceGnmDrawCmdSetStreamOutBuffer` |
| `gnmDrawCmdResetQuery` | `sceGnmDrawCmdResetQuery` |
| `gnmDrawCmdBeginQuery` | `sceGnmDrawCmdBeginQuery` |
| `gnmDrawCmdEndQuery` | `sceGnmDrawCmdEndQuery` |

---

## Driver Wrappers

Firmware/driver wrapper functions that had exact `freegnm` equivalents.

| `freegnm` Name | opengnm (`sceGnm*`) Name |
|----------------|--------------------------|
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

## See Also

- [Command Buffer](commandbuffer.md)
- [Draw Command Buffer](drawcommandbuffer.md)
- [Platform](platform.md)
- [Strings](strings.md)
