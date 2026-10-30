# Draw Command Buffer

API reference for the GNM draw command buffer — the high-level interface for recording draw calls, state setup, shader binding, resource binding, synchronization, and queries into a `GnmCommandBuffer`.

Header: `gnm_drawcommandbuffer.h`

---

## Types

### GnmSetViewportInfo

Describes viewport parameters including depth range and scale/offset for the X, Y, and Z axes.

```c
typedef struct {
    float dmin;
    float dmax;
    float scale[3];
    float offset[3];
} GnmSetViewportInfo;
```

| Field | Type | Description |
|-------|------|-------------|
| `dmin` | `float` | Minimum depth value |
| `dmax` | `float` | Maximum depth value |
| `scale[3]` | `float[3]` | Viewport scale for X, Y, Z |
| `offset[3]` | `float[3]` | Viewport offset for X, Y, Z |

---

## Init

### sceGnmDrawCmdInitDefaultHardwareState

Initializes the command buffer with the GPU's default hardware state.

```c
void sceGnmDrawCmdInitDefaultHardwareState(GnmCommandBuffer* cmd);
```

---

## Draw Commands

### sceGnmDrawCmdDrawIndex

Draws using an explicit index buffer.

```c
void sceGnmDrawCmdDrawIndex(
    GnmCommandBuffer* cmd, uint32_t indexcount, const void* indexaddr
);
```

### sceGnmDrawCmdDrawIndex2

Draws using an explicit index buffer with a draw modifier.

```c
void sceGnmDrawCmdDrawIndex2(
    GnmCommandBuffer* cmd, uint32_t indexcount, const void* indexaddr,
    GnmDrawModifier modifier
);
```

### sceGnmDrawCmdDrawIndexAuto

Draws using an auto-generated sequential index sequence (no index buffer needed).

```c
void sceGnmDrawCmdDrawIndexAuto(GnmCommandBuffer* cmd, uint32_t indexcount);
```

### sceGnmDrawCmdDrawIndexAuto2

Draws using an auto-generated sequential index sequence with a draw modifier.

```c
void sceGnmDrawCmdDrawIndexAuto2(
    GnmCommandBuffer* cmd, uint32_t indexcount, GnmDrawModifier modifier
);
```

### sceGnmDrawCmdDrawIndexIndirect

Draws indirectly using an index buffer, with parameters fetched from GPU memory.

```c
void sceGnmDrawCmdDrawIndexIndirect(
    GnmCommandBuffer* cmd, uint32_t dataoffset, GnmShaderStage stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr
);
```

### sceGnmDrawCmdDrawIndexIndirect2

Draws indirectly using an index buffer with a draw modifier.

```c
void sceGnmDrawCmdDrawIndexIndirect2(
    GnmCommandBuffer* cmd, uint32_t dataoffset, GnmShaderStage stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr, GnmDrawModifier mod
);
```

### sceGnmDrawCmdDrawIndirect

Draws indirectly without an index buffer, with parameters fetched from GPU memory.

```c
void sceGnmDrawCmdDrawIndirect(
    GnmCommandBuffer* cmd, uint32_t dataoffset, GnmShaderStage stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr
);
```

### sceGnmDrawCmdDrawIndirect2

Draws indirectly without an index buffer, with a draw modifier.

```c
void sceGnmDrawCmdDrawIndirect2(
    GnmCommandBuffer* cmd, uint32_t dataoffset, GnmShaderStage stage,
    uint8_t vertexoffusgpr, uint8_t instanceoffusgpr, GnmDrawModifier mod
);
```

### sceGnmDrawCmdDrawIndexIndirectMulti

Draws multiple indirect indexed draws from GPU memory.

```c
void sceGnmDrawCmdDrawIndexIndirectMulti(
    GnmCommandBuffer* cmd, uint32_t dataoffset, uint32_t maxcount,
    GnmShaderStage stage, uint8_t vertexoffusgpr, uint8_t instanceoffusgpr
);
```

### sceGnmDrawCmdDrawIndirectMulti

Draws multiple indirect non-indexed draws from GPU memory.

```c
void sceGnmDrawCmdDrawIndirectMulti(
    GnmCommandBuffer* cmd, uint32_t dataoffset, uint32_t maxcount,
    GnmShaderStage stage, uint8_t vertexoffusgpr, uint8_t instanceoffusgpr
);
```

### sceGnmDrawCmdDrawIndexIndirectCountMulti

Draws multiple indirect indexed draws with a GPU-visible count.

```c
void sceGnmDrawCmdDrawIndexIndirectCountMulti(
    GnmCommandBuffer* cmd, uint32_t dataoffset, uint32_t maxcount,
    uint64_t countaddr, GnmShaderStage stage, uint8_t vertexoffusgpr,
    uint8_t instanceoffusgpr
);
```

### sceGnmDrawCmdDrawIndexOffset

Draws using an index buffer at a specified offset with a draw modifier.

```c
void sceGnmDrawCmdDrawIndexOffset(
    GnmCommandBuffer* cmd, uint32_t indexoffset, uint32_t indexcount,
    GnmDrawModifier modifier
);
```

---

## State Setup

### sceGnmDrawCmdSetDepthClearValue

Sets the depth clear value for the depth render target.

```c
void sceGnmDrawCmdSetDepthClearValue(GnmCommandBuffer* cmd, float clearvalue);
```

### sceGnmDrawCmdSetDepthRenderTarget

Binds a depth render target.

```c
void sceGnmDrawCmdSetDepthRenderTarget(
    GnmCommandBuffer* cmd, const GnmDepthRenderTarget* depthtarget
);
```

### sceGnmDrawCmdSetGuardBands

Sets the guard band clipping region.

```c
void sceGnmDrawCmdSetGuardBands(
    GnmCommandBuffer* cmd, float horzclip, float vertclip, float horzdiscard,
    float vertdiscard
);
```

### sceGnmDrawCmdSetHwScreenOffset

Sets the hardware screen offset.

```c
void sceGnmDrawCmdSetHwScreenOffset(
    GnmCommandBuffer* cmd, uint32_t offsetx, uint32_t offsety
);
```

### sceGnmDrawCmdSetIndexBuffer

Sets the index buffer address.

```c
void sceGnmDrawCmdSetIndexBuffer(GnmCommandBuffer* cmd, const void* buffer);
```

### sceGnmDrawCmdSetIndexCount

Sets the number of indices for the next draw.

```c
void sceGnmDrawCmdSetIndexCount(GnmCommandBuffer* cmd, uint32_t count);
```

### sceGnmDrawCmdSetIndexSize

Sets the index size and cache policy.

```c
void sceGnmDrawCmdSetIndexSize(
    GnmCommandBuffer* cmd, GnmIndexSize indexsize, GnmCachePolicy cachepol
);
```

### sceGnmDrawCmdSetIndirectArgs

Sets the indirect draw arguments structure.

```c
void sceGnmDrawCmdSetIndirectArgs(
    GnmCommandBuffer* cmd, const GnmDrawIndirectArgs* args
);
```

### sceGnmDrawCmdSetIndexedIndirectArgs

Sets the indexed indirect draw arguments structure.

```c
void sceGnmDrawCmdSetIndexedIndirectArgs(
    GnmCommandBuffer* cmd, const GnmDrawIndexedIndirectArgs* args
);
```

### sceGnmDrawCmdSetInstanceStepRate

Sets the instance step rate for instanced rendering.

```c
void sceGnmDrawCmdSetInstanceStepRate(
    GnmCommandBuffer* cmd, uint32_t rate0, uint32_t rate1
);
```

### sceGnmDrawCmdSetNumInstances

Sets the number of instances for the next draw.

```c
void sceGnmDrawCmdSetNumInstances(GnmCommandBuffer* cmd, uint32_t count);
```

### sceGnmDrawCmdSetPrimitiveType

Sets the primitive topology for subsequent draws.

```c
void sceGnmDrawCmdSetPrimitiveType(
    GnmCommandBuffer* cmd, GnmPrimitiveType primType
);
```

### sceGnmDrawCmdSetRenderTarget

Binds a color render target to a specified slot.

```c
void sceGnmDrawCmdSetRenderTarget(
    GnmCommandBuffer* cmd, uint32_t rtslot, const GnmRenderTarget* rt
);
```

### sceGnmDrawCmdSetRenderTargetMask

Sets the render target write mask.

```c
void sceGnmDrawCmdSetRenderTargetMask(GnmCommandBuffer* cmd, uint32_t mask);
```

### sceGnmDrawCmdSetScreenScissor

Sets the screen scissor rectangle.

```c
void sceGnmDrawCmdSetScreenScissor(
    GnmCommandBuffer* cmd, int32_t left, int32_t top, int32_t right,
    int32_t bottom
);
```

### sceGnmDrawCmdSetViewport

Sets viewport parameters for a given viewport ID.

```c
void sceGnmDrawCmdSetViewport(
    GnmCommandBuffer* cmd, uint32_t viewportid, const GnmSetViewportInfo* vpinfo
);
```

---

## Shader Set

### sceGnmDrawCmdSetPsShader

Binds a pixel shader from stage registers.

```c
void sceGnmDrawCmdSetPsShader(
    GnmCommandBuffer* cmd, const GnmPsStageRegisters* regs
);
```

### sceGnmDrawCmdSetEmbeddedPsShader

Binds an embedded (built-in) pixel shader.

```c
void sceGnmDrawCmdSetEmbeddedPsShader(
    GnmCommandBuffer* cmd, GnmEmbeddedPsShader shaderid
);
```

### sceGnmDrawCmdSetVsShader

Binds a vertex shader from stage registers with a shader modifier.

```c
void sceGnmDrawCmdSetVsShader(
    GnmCommandBuffer* cmd, const GnmVsStageRegisters* regs,
    uint32_t shadermodifier
);
```

### sceGnmDrawCmdSetEmbeddedVsShader

Binds an embedded (built-in) vertex shader with a shader modifier.

```c
void sceGnmDrawCmdSetEmbeddedVsShader(
    GnmCommandBuffer* cmd, GnmEmbeddedVsShader shaderid, uint32_t shadermodifier
);
```

### sceGnmDrawCmdSetCsShader

Binds a compute shader from stage registers.

```c
void sceGnmDrawCmdSetCsShader(
    GnmCommandBuffer* cmd, const GnmCsStageRegisters* regs
);
```

### sceGnmDrawCmdSetCsShaderWithModifier

Binds a compute shader from stage registers with a shader modifier.

```c
void sceGnmDrawCmdSetCsShaderWithModifier(
    GnmCommandBuffer* cmd, const GnmCsStageRegisters* regs,
    uint32_t shadermodifier
);
```

### sceGnmDrawCmdSetGsShader

Binds a geometry shader from stage registers.

```c
void sceGnmDrawCmdSetGsShader(
    GnmCommandBuffer* cmd, const GnmGsStageRegisters* regs
);
```

### sceGnmDrawCmdSetEsShader

Binds an export shader from stage registers with a shader modifier.

```c
void sceGnmDrawCmdSetEsShader(
    GnmCommandBuffer* cmd, const GnmEsStageRegisters* regs,
    uint32_t shadermodifier
);
```

### sceGnmDrawCmdSetHsShader

Binds a hull shader from stage registers with an LS/HS config.

```c
void sceGnmDrawCmdSetHsShader(
    GnmCommandBuffer* cmd, const GnmHsStageRegisters* regs,
    uint32_t lshsconfig
);
```

### sceGnmDrawCmdSetLsShader

Binds a local shader from stage registers with a shader modifier.

```c
void sceGnmDrawCmdSetLsShader(
    GnmCommandBuffer* cmd, const GnmLsStageRegisters* regs,
    uint32_t shadermodifier
);
```

---

## Dispatch

### sceGnmDrawCmdDispatchDirect

Dispatches a compute grid with explicit thread counts.

```c
void sceGnmDrawCmdDispatchDirect(
    GnmCommandBuffer* cmd, uint32_t threadsx, uint32_t threadsy,
    uint32_t threadsz, uint32_t flags
);
```

### sceGnmDrawCmdDispatchIndirect

Dispatches a compute grid with parameters fetched from GPU memory.

```c
void sceGnmDrawCmdDispatchIndirect(
    GnmCommandBuffer* cmd, uint32_t dataoffset, uint32_t flags
);
```

---

## PS Input Usage

### sceGnmDrawCmdSetPsInputUsage

Sets the pixel shader input usage tables — vertex export semantics and pixel input semantics.

```c
void sceGnmDrawCmdSetPsInputUsage(
    GnmCommandBuffer* cmd, const GnmVertexExportSemantic* vstable,
    uint32_t numvstableitems, const GnmPixelInputSemantic* pstable,
    uint32_t numpstableitems
);
```

---

## Resource Binding (User Data)

### sceGnmDrawCmdSetVsharpUserData

Binds a buffer (V# — vertex buffer resource) to a shader stage's user data slot.

```c
void sceGnmDrawCmdSetVsharpUserData(
    GnmCommandBuffer* cmd, GnmShaderStage stage, uint32_t startuserdataslot,
    const GnmBuffer* buf
);
```

### sceGnmDrawCmdSetTsharpUserData

Binds a texture (T# — texture resource) to a shader stage's user data slot.

```c
void sceGnmDrawCmdSetTsharpUserData(
    GnmCommandBuffer* cmd, GnmShaderStage stage, uint32_t startuserdataslot,
    const GnmTexture* tex
);
```

### sceGnmDrawCmdSetSsharpUserData

Binds a sampler (S# — sampler state) to a shader stage's user data slot.

```c
void sceGnmDrawCmdSetSsharpUserData(
    GnmCommandBuffer* cmd, GnmShaderStage stage, uint32_t startuserdataslot,
    const GnmSampler* sampler
);
```

### sceGnmDrawCmdSetPointerUserData

Binds a raw pointer to a shader stage's user data slot.

```c
void sceGnmDrawCmdSetPointerUserData(
    GnmCommandBuffer* cmd, GnmShaderStage stage, uint32_t startuserdataslot,
    void* ptr
);
```

---

## Control Registers

### sceGnmDrawCmdSetBlendControl

Sets the blend control for a given render target index.

```c
void sceGnmDrawCmdSetBlendControl(
    GnmCommandBuffer* cmd, uint32_t rtindex, const GnmBlendControl* ctrl
);
```

### sceGnmDrawCmdSetBlendColor

Sets the constant blend color.

```c
void sceGnmDrawCmdSetBlendColor(
    GnmCommandBuffer* cmd, float red, float green, float blue, float alpha
);
```

### sceGnmDrawCmdSetDepthStencilControl

Sets the depth/stencil control register.

```c
void sceGnmDrawCmdSetDepthStencilControl(
    GnmCommandBuffer* cmd, const GnmDepthStencilControl* ctrl
);
```

### sceGnmDrawCmdSetDbRenderControl

Sets the depth buffer render control register.

```c
void sceGnmDrawCmdSetDbRenderControl(
    GnmCommandBuffer* cmd, const GnmDbRenderControl* ctrl
);
```

### sceGnmDrawCmdSetPrimitiveSetup

Sets the primitive setup control register.

```c
void sceGnmDrawCmdSetPrimitiveSetup(
    GnmCommandBuffer* cmd, const GnmPrimitiveSetup* ctrl
);
```

### sceGnmDrawCmdSetViewportTransformControl

Sets the viewport transform control register.

```c
void sceGnmDrawCmdSetViewportTransformControl(
    GnmCommandBuffer* cmd, const GnmViewportTransformControl* ctrl
);
```

---

## Sync / Events

### sceGnmDrawCmdEventWriteEop

Writes an end-of-pipe event with data to a GPU-visible address.

```c
void sceGnmDrawCmdEventWriteEop(
    GnmCommandBuffer* cmd, GnmEventType evtype, uint64_t gpuaddr,
    GnmEventDataSel datasel, uint64_t immvalue
);
```

### sceGnmDrawCmdFillMemory

Fills a region of GPU memory with a 32-bit value. Returns `true` on success.

```c
bool sceGnmDrawCmdFillMemory(
    GnmCommandBuffer* cmd, uint64_t gpuaddr, uint32_t sizebytes,
    uint32_t value
);
```

### sceGnmDrawCmdCopyMemory

Copies a region of GPU memory. Returns `true` on success.

```c
bool sceGnmDrawCmdCopyMemory(
    GnmCommandBuffer* cmd, uint64_t dstaddr, uint64_t srcaddr,
    uint32_t sizebytes
);
```

### sceGnmDrawCmdWaitGraphicsWrite

Inserts a wait for graphics pipeline writes to the specified targets.

```c
void sceGnmDrawCmdWaitGraphicsWrite(
    GnmCommandBuffer* cmd, GnmAcquireTargetFlags targets
);
```

### sceGnmDrawCmdWaitMem

Inserts a wait-on-memory operation (register/memory compare).

```c
void sceGnmDrawCmdWaitMem(
    GnmCommandBuffer* cmd, GnmWaitRegMemFunc op, uint64_t gpuaddr,
    uint32_t refval, uint32_t mask
);
```

### sceGnmDrawCmdWaitUntilSafeForRendering

Inserts a wait until the specified display buffer is safe for rendering.

```c
void sceGnmDrawCmdWaitUntilSafeForRendering(
    GnmCommandBuffer* cmd, int32_t videohandle, uint32_t displaybufidx
);
```

---

## Transform Feedback (Stream-Out)

### sceGnmDrawCmdSetStreamOutConfig

Configures transform feedback stream-out.

```c
void sceGnmDrawCmdSetStreamOutConfig(
    GnmCommandBuffer* cmd, uint32_t streamen, uint32_t raststream,
    uint32_t bufferen
);
```

### sceGnmDrawCmdSetStreamOutBuffer

Sets a transform feedback buffer at a given slot.

```c
void sceGnmDrawCmdSetStreamOutBuffer(
    GnmCommandBuffer* cmd, uint32_t slot, uint64_t gpuaddr, uint32_t size,
    uint32_t stride
);
```

---

## Occlusion Queries

### sceGnmDrawCmdResetQuery

Resets an occlusion query at the given GPU address.

```c
void sceGnmDrawCmdResetQuery(
    GnmCommandBuffer* cmd, uint64_t gpuaddr
);
```

### sceGnmDrawCmdBeginQuery

Begins an occlusion query at the given GPU address.

```c
void sceGnmDrawCmdBeginQuery(
    GnmCommandBuffer* cmd, uint64_t gpuaddr
);
```

### sceGnmDrawCmdEndQuery

Ends an occlusion query at the given GPU address.

```c
void sceGnmDrawCmdEndQuery(
    GnmCommandBuffer* cmd, uint64_t gpuaddr
);
```

---

## See Also

- [Command Buffer](commandbuffer.md)
- [Platform](platform.md)
- [Strings](strings.md)
- [Compatibility Layer](compat.md)
