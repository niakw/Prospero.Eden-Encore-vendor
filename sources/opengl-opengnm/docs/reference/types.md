# Types & Enums

Core type definitions, enumerations, and inline structs shared across the opengnm GNM library. This header (`gnm_types.h`) defines all fundamental GPU enums (array modes, blend ops, formats, tile modes, primitive types, etc.), alignment constants, resource limits, and the indirect-draw argument structs used by the command buffer API.

---

## Defines & Constants

### Alignment Constants

| Constant | Value | Description |
|---|---|---|
| `GNM_INDIRECT_BUFFER_MAX_BYTESIZE` | `0x3ffffc` | Maximum byte size of an indirect command buffer |
| `GNM_ALIGNMENT_BUFFER_BYTES` | `4` | Minimum alignment for buffer base addresses |
| `GNM_ALIGNMENT_SHADER_BYTES` | `256` | Minimum alignment for shader code |
| `GNM_ALIGNMENT_FETCHSHADER_BYTES` | `4` | Minimum alignment for fetch shader code |

### Resource Limits

| Constant | Value | Description |
|---|---|---|
| `GNM_NUM_SHADER_STAGES` | `8` | Number of shader stages in the pipeline |
| `GNM_MAX_TSHARP_USERDATA_SLOTS` | `8` | Max user-data slots for T# (texture) resources |
| `GNM_MAX_SSHARP_USERDATA_SLOTS` | `12` | Max user-data slots for S# (sampler) resources |
| `GNM_MAX_VSHARP_USERDATA_SLOTS` | `12` | Max user-data slots for V# (buffer) resources |
| `GNM_MAX_POINTER_USERDATA_SLOTS` | `14` | Max user-data slots for pointer resources |
| `GNM_MAX_RENDERTARGETS` | `8` | Maximum simultaneous render targets (MRT) |
| `GNM_MAX_VIEWPORTS` | `16` | Maximum simultaneous viewports |

### ABI & Compatibility Macros

| Macro | Description |
|---|---|
| `PS4_SYSV_ABI` | PS4 System V calling convention attribute (no-op on x86_64; active only when `OPENGNM_REQUIRE_ABI` is defined) |
| `OPENGNM_EXTERN_C_BEGIN` | Expands to `extern "C" {` in C++, empty in C |
| `OPENGNM_EXTERN_C_END` | Expands to `}` in C++, empty in C |

---

## Enumerations

### GnmAcquireTargetFlags

Bitmask flags specifying which render targets to acquire before a draw or dispatch.

| Constant | Value |
|---|---|
| `GNM_ACQUIRE_TARGET_CB0` | `0x00000040` |
| `GNM_ACQUIRE_TARGET_CB1` | `0x00000080` |
| `GNM_ACQUIRE_TARGET_CB2` | `0x00000100` |
| `GNM_ACQUIRE_TARGET_CB3` | `0x00000200` |
| `GNM_ACQUIRE_TARGET_CB4` | `0x00000400` |
| `GNM_ACQUIRE_TARGET_CB5` | `0x00000800` |
| `GNM_ACQUIRE_TARGET_CB6` | `0x00001000` |
| `GNM_ACQUIRE_TARGET_CB7` | `0x00002000` |
| `GNM_ACQUIRE_TARGET_DB` | `0x00004000` |

---

### GnmArrayMode

Surface memory layout / tiling array mode.

| Constant | Value |
|---|---|
| `GNM_ARRAY_LINEAR_GENERAL` | `0x0` |
| `GNM_ARRAY_LINEAR_ALIGNED` | `0x1` |
| `GNM_ARRAY_1D_TILED_THIN1` | `0x2` |
| `GNM_ARRAY_1D_TILED_THICK` | `0x3` |
| `GNM_ARRAY_2D_TILED_THIN1` | `0x4` |
| `GNM_ARRAY_PRT_TILED_THIN1` | `0x5` |
| `GNM_ARRAY_PRT_2D_TILED_THIN1` | `0x6` |
| `GNM_ARRAY_2D_TILED_THICK` | `0x7` |
| `GNM_ARRAY_2D_TILED_XTHICK` | `0x8` |
| `GNM_ARRAY_PRT_TILED_THICK` | `0x9` |
| `GNM_ARRAY_PRT_2D_TILED_THICK` | `0xa` |
| `GNM_ARRAY_PRT_3D_TILED_THIN1` | `0xb` |
| `GNM_ARRAY_3D_TILED_THIN1` | `0xc` |
| `GNM_ARRAY_3D_TILED_THICK` | `0xd` |
| `GNM_ARRAY_3D_TILED_XTHICK` | `0xe` |
| `GNM_ARRAY_PRT_3D_TILED_THICK` | `0xf` |

---

### GnmBankWidth

Surface bank width (in pipes) for tiling configuration.

| Constant | Value |
|---|---|
| `GNM_SURF_BANK_WIDTH_1` | `0x0` |
| `GNM_SURF_BANK_WIDTH_2` | `0x1` |
| `GNM_SURF_BANK_WIDTH_4` | `0x2` |
| `GNM_SURF_BANK_WIDTH_8` | `0x3` |

---

### GnmBankHeight

Surface bank height (in pipes) for tiling configuration.

| Constant | Value |
|---|---|
| `GNM_SURF_BANK_HEIGHT_1` | `0x0` |
| `GNM_SURF_BANK_HEIGHT_2` | `0x1` |
| `GNM_SURF_BANK_HEIGHT_4` | `0x2` |
| `GNM_SURF_BANK_HEIGHT_8` | `0x3` |

---

### GnmBlendOp

Blend factor operands for color/alpha blending operations.

| Constant | Value |
|---|---|
| `GNM_BLEND_ZERO` | `0x0` |
| `GNM_BLEND_ONE` | `0x1` |
| `GNM_BLEND_SRC_COLOR` | `0x2` |
| `GNM_BLEND_ONE_MINUS_SRC_COLOR` | `0x3` |
| `GNM_BLEND_SRC_ALPHA` | `0x4` |
| `GNM_BLEND_ONE_MINUS_SRC_ALPHA` | `0x5` |
| `GNM_BLEND_DEST_ALPHA` | `0x6` |
| `GNM_BLEND_ONE_MINUS_DEST_ALPHA` | `0x7` |
| `GNM_BLEND_DEST_COLOR` | `0x8` |
| `GNM_BLEND_ONE_MINUS_DEST_COLOR` | `0x9` |
| `GNM_BLEND_SRC_ALPHA_SATURATE` | `0xa` |
| `GNM_BLEND_CONSTANT_COLOR` | `0xd` |
| `GNM_BLEND_ONE_MINUS_CONSTANT_COLOR` | `0xe` |
| `GNM_BLEND_SRC1_COLOR` | `0xf` |
| `GNM_BLEND_INVERSE_SRC1_COLOR` | `0x10` |
| `GNM_BLEND_SRC1_ALPHA` | `0x11` |
| `GNM_BLEND_INVERSE_SRC1_ALPHA` | `0x12` |
| `GNM_BLEND_CONSTANT_ALPHA` | `0x13` |
| `GNM_BLEND_ONE_MINUS_CONSTANT_ALPHA` | `0x14` |

---

### GnmBorderColor

Border color handling mode for texture sampling outside [0,1] coordinates.

| Constant | Value |
|---|---|
| `GNM_BORDER_COLOR_TRANS_BLACK` | `0x0` |
| `GNM_BORDER_COLOR_OPAQUE_BLACK` | `0x1` |
| `GNM_BORDER_COLOR_OPAQUE_WHITE` | `0x2` |
| `GNM_BORDER_COLOR_FROM_TABLE` | `0x3` |

---

### GnmBufferFormat

Data format encoding for buffer (V#) resources — component bit-width layout.

| Constant | Value |
|---|---|
| `GNM_BUF_DATA_FORMAT_INVALID` | `0x0` |
| `GNM_BUF_DATA_FORMAT_8` | `0x1` |
| `GNM_BUF_DATA_FORMAT_16` | `0x2` |
| `GNM_BUF_DATA_FORMAT_8_8` | `0x3` |
| `GNM_BUF_DATA_FORMAT_32` | `0x4` |
| `GNM_BUF_DATA_FORMAT_16_16` | `0x5` |
| `GNM_BUF_DATA_FORMAT_10_11_11` | `0x6` |
| `GNM_BUF_DATA_FORMAT_11_11_10` | `0x7` |
| `GNM_BUF_DATA_FORMAT_10_10_10_2` | `0x8` |
| `GNM_BUF_DATA_FORMAT_2_10_10_10` | `0x9` |
| `GNM_BUF_DATA_FORMAT_8_8_8_8` | `0xa` |
| `GNM_BUF_DATA_FORMAT_32_32` | `0xb` |
| `GNM_BUF_DATA_FORMAT_16_16_16_16` | `0xc` |
| `GNM_BUF_DATA_FORMAT_32_32_32` | `0xd` |
| `GNM_BUF_DATA_FORMAT_32_32_32_32` | `0xe` |
| `GNM_BUF_DATA_FORMAT_RESERVED_15` | `0xf` |

---

### GnmBufNumFormat

Numeric interpretation for buffer (V#) resource data.

| Constant | Value |
|---|---|
| `GNM_BUF_NUM_FORMAT_UNORM` | `0x0` |
| `GNM_BUF_NUM_FORMAT_SNORM` | `0x1` |
| `GNM_BUF_NUM_FORMAT_USCALED` | `0x2` |
| `GNM_BUF_NUM_FORMAT_SSCALED` | `0x3` |
| `GNM_BUF_NUM_FORMAT_UINT` | `0x4` |
| `GNM_BUF_NUM_FORMAT_SINT` | `0x5` |
| `GNM_BUF_NUM_FORMAT_SNORM_OGL` | `0x6` |
| `GNM_BUF_NUM_FORMAT_FLOAT` | `0x7` |

---

### GnmCachePolicy

Cache eviction policy for GPU memory accesses.

| Constant | Value |
|---|---|
| `GNM_POLICY_LRU` | `0x0` |
| `GNM_POLICY_STREAM` | `0x1` |
| `GNM_POLICY_BYPASS` | `0x2` |
| `GNM_POLICY_RESERVED` | `0x3` |

---

### GnmChannel

Channel source selection — maps a texture/buffer channel to a source component or constant.

| Constant | Value |
|---|---|
| `GNM_CHAN_CONSTANT0` | `0x0` |
| `GNM_CHAN_CONSTANT1` | `0x1` |
| `GNM_CHAN_X` | `0x4` |
| `GNM_CHAN_Y` | `0x5` |
| `GNM_CHAN_Z` | `0x6` |
| `GNM_CHAN_W` | `0x7` |

---

### GnmCombFunc

Combine function for blending — how source and destination are combined.

| Constant | Value |
|---|---|
| `GNM_COMB_DST_PLUS_SRC` | `0x0` |
| `GNM_COMB_SRC_MINUS_DST` | `0x1` |
| `GNM_COMB_MIN_DST_SRC` | `0x2` |
| `GNM_COMB_MAX_DST_SRC` | `0x3` |
| `GNM_COMB_DST_MINUS_SRC` | `0x4` |

---

### GnmCullMode

Triangle culling mode for the rasterizer.

| Constant | Value |
|---|---|
| `GNM_CULL_NONE` | `0x0` |
| `GNM_CULL_FRONT` | `0x1` |
| `GNM_CULL_BACK` | `0x2` |
| `GNM_CULL_FRONTBACK` | `0x3` |

---

### GnmDccColorTransform

Delta Color Compression (DCC) color transform mode.

| Constant | Value |
|---|---|
| `GNM_DCC_CT_AUTO` | `0x0` |
| `GNM_DCC_CT_NONE` | `0x1` |
| `GNM_DCC_CT_ABGR` | `0x2` |
| `GNM_DCC_CT_BGRA` | `0x3` |

---

### GnmDccMaxBlockSize

Maximum block size for Delta Color Compression.

| Constant | Value |
|---|---|
| `GNM_DCC_MBS_64` | `0x0` |
| `GNM_DCC_MBS_128` | `0x1` |
| `GNM_DCC_MBS_256` | `0x2` |

---

### GnmDepthCompare

Depth/stencil comparison function for the depth test and sampler compare.

| Constant | Value |
|---|---|
| `GNM_DEPTH_COMPARE_NEVER` | `0x0` |
| `GNM_DEPTH_COMPARE_LESS` | `0x1` |
| `GNM_DEPTH_COMPARE_EQUAL` | `0x2` |
| `GNM_DEPTH_COMPARE_LESSEQUAL` | `0x3` |
| `GNM_DEPTH_COMPARE_GREATER` | `0x4` |
| `GNM_DEPTH_COMPARE_NOTEQUAL` | `0x5` |
| `GNM_DEPTH_COMPARE_GREATEREQUAL` | `0x6` |
| `GNM_DEPTH_COMPARE_ALWAYS` | `0x7` |

---

### GnmEmbeddedVsShader

Identifiers for built-in (embedded) vertex shaders provided by the driver.

| Constant | Value |
|---|---|
| `GNM_EMBEDDED_VSH_FULLSCREEN` | `0` |

---

### GnmEmbeddedPsShader

Identifiers for built-in (embedded) pixel shaders provided by the driver.

| Constant | Value |
|---|---|
| `GNM_EMBEDDED_PSH_DUMMY` | `0` |
| `GNM_EMBEDDED_PSH_DUMMY_RG32` | `1` |

---

### GnmEventType

GPU event types for command buffer event/write operations.

| Constant | Value |
|---|---|
| `GNM_CACHE_FLUSH_TS` | `0x4` |
| `GNM_CACHE_FLUSH_AND_INV_TS_EVENT` | `0x14` |
| `GNM_BOTTOM_OF_PIPE_TS` | `0x28` |
| `GNM_FLUSH_AND_INV_CB_DATA_TS` | `0x2d` |
| `GNM_CS_DONE` | `0x2f` |
| `GNM_PS_DONE` | `0x30` |

---

### GnmEventDataSel

Data selection for event-write operations — what data accompanies the event.

| Constant | Value |
|---|---|
| `GNM_DATA_SEL_DISCARD` | `0x0` |
| `GNM_DATA_SEL_SEND_DATA32` | `0x1` |
| `GNM_DATA_SEL_SEND_DATA64` | `0x2` |
| `GNM_DATA_SEL_SEND_SYS_CLOCK` | `0x3` |
| `GNM_DATA_SEL_SEND_GPU_CLOCK` | `0x4` |

---

### GnmFaceOrientation

Front-face winding order for triangle rasterization.

| Constant | Value |
|---|---|
| `GNM_FACE_CCW` | `0x0` |
| `GNM_FACE_CW` | `0x1` |

---

### GnmFillMode

Rasterization fill mode for polygons.

| Constant | Value |
|---|---|
| `GNM_FILL_POINTS` | `0x0` |
| `GNM_FILL_WIREFRAME` | `0x1` |
| `GNM_FILL_SOLID` | `0x2` |

---

### GnmFilter

Texture magnification/minification filter type.

| Constant | Value |
|---|---|
| `GNM_FILTER_POINT` | `0x0` |
| `GNM_FILTER_BILINEAR` | `0x1` |
| `GNM_FILTER_ANISO_POINT` | `0x2` |
| `GNM_FILTER_ANISO_BILINEAR` | `0x3` |

---

### GnmFilterMode

Filter mode selector for sampler — blend, min, or max.

| Constant | Value |
|---|---|
| `GNM_FILTER_MODE_BLEND` | `0x0` |
| `GNM_FILTER_MODE_MIN` | `0x1` |
| `GNM_FILTER_MODE_MAX` | `0x2` |

---

### GnmGpuMode

GPU variant — base PS4 GPU or NEO (PS4 Pro) GPU.

| Constant | Value |
|---|---|
| `GNM_GPU_BASE` | `0x0` |
| `GNM_GPU_NEO` | `0x1` |

---

### GnmImageFormat

Data format encoding for image/texture (T#) resources — component bit-width layout. Includes standard packed formats, depth/stencil formats, BC compressed formats, and FMASK formats.

| Constant | Value |
|---|---|
| `GNM_IMG_DATA_FORMAT_INVALID` | `0x0` |
| `GNM_IMG_DATA_FORMAT_8` | `0x1` |
| `GNM_IMG_DATA_FORMAT_16` | `0x2` |
| `GNM_IMG_DATA_FORMAT_8_8` | `0x3` |
| `GNM_IMG_DATA_FORMAT_32` | `0x4` |
| `GNM_IMG_DATA_FORMAT_16_16` | `0x5` |
| `GNM_IMG_DATA_FORMAT_10_11_11` | `0x6` |
| `GNM_IMG_DATA_FORMAT_11_11_10` | `0x7` |
| `GNM_IMG_DATA_FORMAT_10_10_10_2` | `0x8` |
| `GNM_IMG_DATA_FORMAT_2_10_10_10` | `0x9` |
| `GNM_IMG_DATA_FORMAT_8_8_8_8` | `0xa` |
| `GNM_IMG_DATA_FORMAT_32_32` | `0xb` |
| `GNM_IMG_DATA_FORMAT_16_16_16_16` | `0xc` |
| `GNM_IMG_DATA_FORMAT_32_32_32` | `0xd` |
| `GNM_IMG_DATA_FORMAT_32_32_32_32` | `0xe` |
| `GNM_IMG_DATA_FORMAT_5_6_5` | `0x10` |
| `GNM_IMG_DATA_FORMAT_1_5_5_5` | `0x11` |
| `GNM_IMG_DATA_FORMAT_5_5_5_1` | `0x12` |
| `GNM_IMG_DATA_FORMAT_4_4_4_4` | `0x13` |
| `GNM_IMG_DATA_FORMAT_8_24` | `0x14` |
| `GNM_IMG_DATA_FORMAT_24_8` | `0x15` |
| `GNM_IMG_DATA_FORMAT_X24_8_32` | `0x16` |
| `GNM_IMG_DATA_FORMAT_GB_GR` | `0x20` |
| `GNM_IMG_DATA_FORMAT_BG_RG` | `0x21` |
| `GNM_IMG_DATA_FORMAT_5_9_9_9` | `0x22` |
| `GNM_IMG_DATA_FORMAT_BC1` | `0x23` |
| `GNM_IMG_DATA_FORMAT_BC2` | `0x24` |
| `GNM_IMG_DATA_FORMAT_BC3` | `0x25` |
| `GNM_IMG_DATA_FORMAT_BC4` | `0x26` |
| `GNM_IMG_DATA_FORMAT_BC5` | `0x27` |
| `GNM_IMG_DATA_FORMAT_BC6` | `0x28` |
| `GNM_IMG_DATA_FORMAT_BC7` | `0x29` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S2_F1` | `0x2c` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S4_F1` | `0x2d` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S8_F1` | `0x2e` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S2_F2` | `0x2f` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S4_F2` | `0x30` |
| `GNM_IMG_DATA_FORMAT_FMASK8_S4_F4` | `0x31` |
| `GNM_IMG_DATA_FORMAT_FMASK16_S16_F1` | `0x32` |
| `GNM_IMG_DATA_FORMAT_FMASK16_S8_F2` | `0x33` |
| `GNM_IMG_DATA_FORMAT_FMASK32_S16_F2` | `0x34` |
| `GNM_IMG_DATA_FORMAT_FMASK32_S8_F4` | `0x35` |
| `GNM_IMG_DATA_FORMAT_FMASK32_S8_F8` | `0x36` |
| `GNM_IMG_DATA_FORMAT_FMASK64_S16_F4` | `0x37` |
| `GNM_IMG_DATA_FORMAT_FMASK64_S16_F8` | `0x38` |
| `GNM_IMG_DATA_FORMAT_4_4` | `0x39` |
| `GNM_IMG_DATA_FORMAT_6_5_5` | `0x3a` |
| `GNM_IMG_DATA_FORMAT_1` | `0x3b` |
| `GNM_IMG_DATA_FORMAT_1_REVERSED` | `0x3c` |
| `GNM_IMG_DATA_FORMAT_32_AS_8` | `0x3d` |
| `GNM_IMG_DATA_FORMAT_32_AS_8_8` | `0x3e` |
| `GNM_IMG_DATA_FORMAT_32_AS_32_32_32_32` | `0x3f` |

---

### GnmImgNumFormat

Numeric interpretation for image/texture (T#) resource data.

| Constant | Value |
|---|---|
| `GNM_IMG_NUM_FORMAT_UNORM` | `0x0` |
| `GNM_IMG_NUM_FORMAT_SNORM` | `0x1` |
| `GNM_IMG_NUM_FORMAT_USCALED` | `0x2` |
| `GNM_IMG_NUM_FORMAT_SSCALED` | `0x3` |
| `GNM_IMG_NUM_FORMAT_UINT` | `0x4` |
| `GNM_IMG_NUM_FORMAT_SINT` | `0x5` |
| `GNM_IMG_NUM_FORMAT_SNORM_OGL` | `0x6` |
| `GNM_IMG_NUM_FORMAT_FLOAT` | `0x7` |
| `GNM_IMG_NUM_FORMAT_SRGB` | `0x9` |
| `GNM_IMG_NUM_FORMAT_UBNORM` | `0xa` |
| `GNM_IMG_NUM_FORMAT_UBNORM_OGL` | `0xb` |
| `GNM_IMG_NUM_FORMAT_UBINT` | `0xc` |
| `GNM_IMG_NUM_FORMAT_UBSCALED` | `0xd` |

---

### GnmIndexSize

Index buffer element size for indexed draws.

| Constant | Value |
|---|---|
| `GNM_INDEX_16` | `0x0` |
| `GNM_INDEX_32` | `0x1` |

---

### GnmMacroTileAspect

Macro-tile aspect ratio for surface tiling.

| Constant | Value |
|---|---|
| `GNM_SURF_MACRO_ASPECT_1` | `0x0` |
| `GNM_SURF_MACRO_ASPECT_2` | `0x1` |
| `GNM_SURF_MACRO_ASPECT_4` | `0x2` |
| `GNM_SURF_MACRO_ASPECT_8` | `0x3` |

---

### GnmMacroTileMode

Macro-tile mode specifying tile dimensions and bank configuration.

| Constant | Value |
|---|---|
| `GNM_MACROTILEMODE_1x4_16` | `0x0` |
| `GNM_MACROTILEMODE_1x2_16` | `0x1` |
| `GNM_MACROTILEMODE_1x1_16` | `0x2` |
| `GNM_MACROTILEMODE_1x1_16_DUP` | `0x3` |
| `GNM_MACROTILEMODE_1x1_8` | `0x4` |
| `GNM_MACROTILEMODE_1x1_4` | `0x5` |
| `GNM_MACROTILEMODE_1x1_2` | `0x6` |
| `GNM_MACROTILEMODE_1x1_2_DUP` | `0x7` |
| `GNM_MACROTILEMODE_1x8_16` | `0x8` |
| `GNM_MACROTILEMODE_1x4_16_DUP` | `0x9` |
| `GNM_MACROTILEMODE_1x2_16_DUP` | `0xa` |
| `GNM_MACROTILEMODE_1x1_16_DUP2` | `0xb` |
| `GNM_MACROTILEMODE_1x1_8_DUP` | `0xc` |
| `GNM_MACROTILEMODE_1x1_4_DUP` | `0xd` |
| `GNM_MACROTILEMODE_1x1_2_DUP2` | `0xe` |
| `GNM_MACROTILEMODE_1x1_2_DUP3` | `0xf` |

---

### GnmMicroTileMode

Micro-tile mode for surface tiling — determines how texels are arranged within a micro-tile.

| Constant | Value |
|---|---|
| `GNM_SURF_DISPLAY_MICRO_TILING` | `0x0` |
| `GNM_SURF_THIN_MICRO_TILING` | `0x1` |
| `GNM_SURF_DEPTH_MICRO_TILING` | `0x2` |
| `GNM_SURF_ROTATED_MICRO_TILING` | `0x3` |
| `GNM_SURF_THICK_MICRO_TILING` | `0x4` |

---

### GnmMipFilter

Mipmap filter mode for texture sampling between mip levels.

| Constant | Value |
|---|---|
| `GNM_MIPFILTER_NONE` | `0x0` |
| `GNM_MIPFILTER_POINT` | `0x1` |
| `GNM_MIPFILTER_LINEAR` | `0x2` |

---

### GnmNumBanks

Number of memory banks for surface tiling configuration.

| Constant | Value |
|---|---|
| `GNM_SURF_2_BANK` | `0x0` |
| `GNM_SURF_4_BANK` | `0x1` |
| `GNM_SURF_8_BANK` | `0x2` |
| `GNM_SURF_16_BANK` | `0x3` |

---

### GnmPipeConfig

Pipe configuration for surface addressing — determines pipe count and tile dimensions.

| Constant | Value |
|---|---|
| `GNM_ADDR_SURF_P2` | `0x0` |
| `GNM_ADDR_SURF_P4_8x16` | `0x4` |
| `GNM_ADDR_SURF_P4_16x16` | `0x5` |
| `GNM_ADDR_SURF_P4_16x32` | `0x6` |
| `GNM_ADDR_SURF_P4_32x32` | `0x7` |
| `GNM_ADDR_SURF_P8_16x16_8x16` | `0x8` |
| `GNM_ADDR_SURF_P8_16x32_8x16` | `0x9` |
| `GNM_ADDR_SURF_P8_32x32_8x16` | `0xa` |
| `GNM_ADDR_SURF_P8_16x32_16x16` | `0xb` |
| `GNM_ADDR_SURF_P8_32x32_16x16` | `0xc` |
| `GNM_ADDR_SURF_P8_32x32_16x32` | `0xd` |
| `GNM_ADDR_SURF_P8_32x64_32x32` | `0xe` |
| `GNM_ADDR_SURF_P16_32x32_8x16` | `0x10` |
| `GNM_ADDR_SURF_P16_32x32_16x16` | `0x11` |

---

### GnmPrimitiveType

Primitive topology for draw calls.

| Constant | Value |
|---|---|
| `GNM_PT_NONE` | `0x0` |
| `GNM_PT_POINTLIST` | `0x1` |
| `GNM_PT_LINELIST` | `0x2` |
| `GNM_PT_LINESTRIP` | `0x3` |
| `GNM_PT_TRILIST` | `0x4` |
| `GNM_PT_TRIFAN` | `0x5` |
| `GNM_PT_TRISTRIP` | `0x6` |
| `GNM_PT_PATCH` | `0x9` |
| `GNM_PT_LINELIST_ADJ` | `0xa` |
| `GNM_PT_LINESTRIP_ADJ` | `0xb` |
| `GNM_PT_TRILIST_ADJ` | `0xc` |
| `GNM_PT_TRIPSTRIP_ADJ` | `0xd` |
| `GNM_PT_TRI_WITH_WFLAGS` | `0x10` |
| `GNM_PT_RECTLIST` | `0x11` |
| `GNM_PT_LINELOOP` | `0x12` |
| `GNM_PT_QUADLIST` | `0x13` |
| `GNM_PT_QUADSTRIP` | `0x14` |
| `GNM_PT_POLYGON` | `0x15` |

---

### GnmProvokingVertex

Provoking vertex convention — which vertex in a primitive is the "last" for flat shading.

| Constant | Value |
|---|---|
| `GNM_PROVOKINGVTX_FIRST` | `0x0` |
| `GNM_PROVOKINGVTX_LAST` | `0x1` |

---

### GnmQuantMode

Quantization mode for fixed-point number representation.

| Constant | Value |
|---|---|
| `GNM_QM_16_8_FIXED_POINT_1_16TH` | `0` |
| `GNM_QM_16_8_FIXED_POINT_1_8TH` | `1` |
| `GNM_QM_16_8_FIXED_POINT_1_4TH` | `2` |
| `GNM_QM_16_8_FIXED_POINT_1_2` | `3` |
| `GNM_QM_16_8_FIXED_POINT_1` | `4` |
| `GNM_QM_16_8_FIXED_POINT_1_256TH` | `5` |
| `GNM_QM_14_10_FIXED_POINT_1_1024TH` | `6` |
| `GNM_QM_12_12_FIXED_POINT_1_4096TH` | `7` |

---

### GnmMemoryType

GPU memory type / caching attributes for resource access.

| Constant | Value |
|---|---|
| `GNM_MEMORY_READONLY` | `0x10` |
| `GNM_MEMORY_PRIVATE` | `0x60` |
| `GNM_MEMORY_CACHE_UNCOHERENT` | `0x6d` |
| `GNM_MEMORY_CACHE_COHERENT` | `0x6e` |
| `GNM_MEMORY_UNCACHED` | `0x6f` |

---

### GnmRoundMode

Rounding mode for floating-point to fixed-point conversion.

| Constant | Value |
|---|---|
| `GNM_RM_TRUNCATE` | `0` |
| `GNM_RM_ROUND` | `1` |
| `GNM_RM_ROUND_TO_EVEN` | `2` |
| `GNM_RM_ROUND_TO_ODD` | `3` |

---

### GnmSampleSplit

Sample split configuration for MSAA surface addressing.

| Constant | Value |
|---|---|
| `GNM_ADDR_SAMPLE_SPLIT_1` | `0x0` |
| `GNM_ADDR_SAMPLE_SPLIT_2` | `0x1` |
| `GNM_ADDR_SAMPLE_SPLIT_4` | `0x2` |
| `GNM_ADDR_SAMPLE_SPLIT_8` | `0x3` |

---

### GnmShaderStage

Shader stage identifiers for the graphics/compute pipeline.

| Constant | Value |
|---|---|
| `GNM_STAGE_CS` | `0x0` |
| `GNM_STAGE_PS` | `0x1` |
| `GNM_STAGE_VS` | `0x2` |
| `GNM_STAGE_GS` | `0x3` |
| `GNM_STAGE_ES` | `0x4` |
| `GNM_STAGE_HS` | `0x5` |
| `GNM_STAGE_LS` | `0x6` |

---

### GnmStencilFormat

Stencil buffer format.

| Constant | Value |
|---|---|
| `GNM_STENCIL_INVALID` | `0x0` |
| `GNM_STENCIL_8` | `0x1` |

---

### GnmSurfaceNumber

Numeric type for render target surface data (subset of image num formats).

| Constant | Value |
|---|---|
| `GNM_NUMBER_UNORM` | `0x0` |
| `GNM_NUMBER_SNORM` | `0x1` |
| `GNM_NUMBER_UINT` | `0x4` |
| `GNM_NUMBER_SINT` | `0x5` |
| `GNM_NUMBER_SRGB` | `0x6` |
| `GNM_NUMBER_FLOAT` | `0x7` |

---

### GnmSurfaceSwap

Channel swap/ordering mode for render target surfaces.

| Constant | Value |
|---|---|
| `GNM_SWAP_STD` | `0x0` |
| `GNM_SWAP_ALT` | `0x1` |
| `GNM_SWAP_STD_REV` | `0x2` |
| `GNM_SWAP_ALT_REV` | `0x3` |

---

### GnmTexClamp

Texture coordinate clamping / addressing mode.

| Constant | Value |
|---|---|
| `GNM_TEX_CLAMP_WRAP` | `0x0` |
| `GNM_TEX_CLAMP_MIRROR` | `0x1` |
| `GNM_TEX_CLAMP_CLAMP_LAST_TEXEL` | `0x2` |
| `GNM_TEX_CLAMP_MIRROR_ONCE_LAST_TEXEL` | `0x3` |
| `GNM_TEX_CLAMP_CLAMP_HALF_BORDER` | `0x4` |
| `GNM_TEX_CLAMP_MIRROR_ONCE_HALF_BORDER` | `0x5` |
| `GNM_TEX_CLAMP_CLAMP_BORDER` | `0x6` |
| `GNM_TEX_CLAMP_MIRROR_ONCE_BORDER` | `0x7` |

---

### GnmTexPerfModulation

Performance modulation mode for texture sampling — controls LOD bias behavior.

| Constant | Value |
|---|---|
| `GNM_TEX_PERFMOD_NONE` | `0x0` |
| `GNM_TEX_PERFMOD_MIN` | `0x1` |
| `GNM_TEX_PERFMOD_DEFAULT` | `0x4` |
| `GNM_TEX_PERFMOD_MAX` | `0x7` |

---

### GnmTextureType

Texture dimensionality and array type.

| Constant | Value |
|---|---|
| `GNM_TEXTURE_1D` | `0x8` |
| `GNM_TEXTURE_2D` | `0x9` |
| `GNM_TEXTURE_3D` | `0xa` |
| `GNM_TEXTURE_CUBEMAP` | `0xb` |
| `GNM_TEXTURE_1D_ARRAY` | `0xc` |
| `GNM_TEXTURE_2D_ARRAY` | `0xd` |
| `GNM_TEXTURE_2D_MSAA` | `0xe` |
| `GNM_TEXTURE_2D_ARRAY_MSAA` | `0xf` |

---

### GnmTileMode

Tile mode index specifying the full tiling configuration for a surface. Covers depth, display, thin, and thick variants with optional PRT (partially resident texture) support.

| Constant | Value |
|---|---|
| `GNM_TM_DEPTH_2D_THIN_64` | `0x0` |
| `GNM_TM_DEPTH_2D_THIN_128` | `0x1` |
| `GNM_TM_DEPTH_2D_THIN_256` | `0x2` |
| `GNM_TM_DEPTH_2D_THIN_512` | `0x3` |
| `GNM_TM_DEPTH_2D_THIN_1K` | `0x4` |
| `GNM_TM_DEPTH_1D_THIN` | `0x5` |
| `GNM_TM_DEPTH_2D_THIN_PRT_256` | `0x6` |
| `GNM_TM_DEPTH_2D_THIN_PRT_1K` | `0x7` |
| `GNM_TM_DISPLAY_LINEAR_ALIGNED` | `0x8` |
| `GNM_TM_DISPLAY_1D_THIN` | `0x9` |
| `GNM_TM_DISPLAY_2D_THIN` | `0xa` |
| `GNM_TM_DISPLAY_THIN_PRT` | `0xb` |
| `GNM_TM_DISPLAY_2D_THIN_PRT` | `0xc` |
| `GNM_TM_THIN_1D_THIN` | `0xd` |
| `GNM_TM_THIN_2D_THIN` | `0xe` |
| `GNM_TM_THIN_3D_THIN` | `0xf` |
| `GNM_TM_THIN_THIN_PRT` | `0x10` |
| `GNM_TM_THIN_2D_THIN_PRT` | `0x11` |
| `GNM_TM_THIN_3D_THIN_PRT` | `0x12` |
| `GNM_TM_THICK_1D_THICK` | `0x13` |
| `GNM_TM_THICK_2D_THICK` | `0x14` |
| `GNM_TM_THICK_3D_THICK` | `0x15` |
| `GNM_TM_THICK_THICK_PRT` | `0x16` |
| `GNM_TM_THICK_2D_THICK_PRT` | `0x17` |
| `GNM_TM_THICK_3D_THICK_PRT` | `0x18` |
| `GNM_TM_THICK_2D_XTHICK` | `0x19` |
| `GNM_TM_THICK_3D_XTHICK` | `0x1a` |
| `GNM_TM_DISPLAY_LINEAR_GENERAL` | `0x1f` |

---

### GnmTileSplit

Tile split size for MSAA surface tiling — how samples are split across memory.

| Constant | Value |
|---|---|
| `GNM_SURF_TILE_SPLIT_64B` | `0x0` |
| `GNM_SURF_TILE_SPLIT_128B` | `0x1` |
| `GNM_SURF_TILE_SPLIT_256B` | `0x2` |
| `GNM_SURF_TILE_SPLIT_512B` | `0x3` |
| `GNM_SURF_TILE_SPLIT_1KB` | `0x4` |
| `GNM_SURF_TILE_SPLIT_2KB` | `0x5` |
| `GNM_SURF_TILE_SPLIT_4KB` | `0x6` |

---

### GnmWaitRegMemFunc

Comparison function for `WAIT_REG_MEM` operations — condition for waiting on a register/memory value.

| Constant | Value |
|---|---|
| `GNM_WAIT_REG_MEM_FUNC_ALWAYS` | `0` |
| `GNM_WAIT_REG_MEM_FUNC_LESS` | `1` |
| `GNM_WAIT_REG_MEM_FUNC_LESS_EQUAL` | `2` |
| `GNM_WAIT_REG_MEM_FUNC_EQUAL` | `3` |
| `GNM_WAIT_REG_MEM_FUNC_NOT_EQUAL` | `4` |
| `GNM_WAIT_REG_MEM_FUNC_GREATER_EQUAL` | `5` |
| `GNM_WAIT_REG_MEM_FUNC_GREATER` | `6` |

---

### GnmZFilter

Z (depth) filter mode for depth texture sampling.

| Constant | Value |
|---|---|
| `GNM_ZFILTER_NONE` | `0x0` |
| `GNM_ZFILTER_POINT` | `0x1` |
| `GNM_ZFILTER_LINEAR` | `0x2` |

---

### GnmZFormat

Depth buffer format.

| Constant | Value |
|---|---|
| `GNM_Z_INVALID` | `0x0` |
| `GNM_Z_16` | `0x1` |
| `GNM_Z_24` | `0x2` |
| `GNM_Z_32_FLOAT` | `0x3` |

---

## Structs

### GnmDrawIndexedIndirectArgs

Arguments for an indexed indirect draw call (GPU-side). Size: `0x14` bytes (20 bytes).

```c
typedef struct {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;
} GnmDrawIndexedIndirectArgs;
```

| Field | Type | Description |
|---|---|---|
| `indexCount` | `uint32_t` | Number of indices to draw |
| `instanceCount` | `uint32_t` | Number of instances to draw |
| `firstIndex` | `uint32_t` | First index in the index buffer |
| `vertexOffset` | `int32_t` | Offset added to each index before fetching vertex |
| `firstInstance` | `uint32_t` | First instance ID |

_Static_assert: `sizeof(GnmDrawIndexedIndirectArgs) == 0x14`

---

### GnmDrawIndirectArgs

Arguments for a non-indexed indirect draw call (GPU-side). Size: `0x10` bytes (16 bytes).

```c
typedef struct {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
} GnmDrawIndirectArgs;
```

| Field | Type | Description |
|---|---|---|
| `vertexCount` | `uint32_t` | Number of vertices to draw |
| `instanceCount` | `uint32_t` | Number of instances to draw |
| `firstVertex` | `uint32_t` | First vertex in the vertex buffer |
| `firstInstance` | `uint32_t` | First instance ID |

_Static_assert: `sizeof(GnmDrawIndirectArgs) == 0x10`

---

### GnmDrawModifier

Draw modifier flags for indirect draws. Size: `0x4` bytes (4 bytes).

```c
typedef struct {
    uint32_t rendertargetsliceoffset : 3;
    uint32_t _unused : 29;
} GnmDrawModifier;
```

| Field | Bits | Type | Description |
|---|---|---|---|
| `rendertargetsliceoffset` | 3 | `uint32_t` | Render target slice offset for the draw |
| `_unused` | 29 | `uint32_t` | Reserved / unused bits |

_Static_assert: `sizeof(GnmDrawModifier) == 0x4`

---

## See Also

- [Error Handling](error.md) — GnmError codes and message handling
- [Data Format](dataformat.md) — GnmDataFormat union and predefined formats
- [Buffer](buffer.md) — GnmBuffer (V#) struct and functions
- [Texture](texture.md) — GnmTexture (T#) struct and functions
- [Sampler](sampler.md) — GnmSampler (S#) struct and functions
