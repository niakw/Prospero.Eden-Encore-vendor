# Control Registers

API reference for GPU control register structs in the opengnm PS4 GNM library. These structs configure blend, depth/stencil, primitive setup, and viewport transform state.

Header: `gnm_controls.h`

---

## GnmBlendControl

Controls per-target blending configuration for color and alpha channels.

```c
typedef struct {
    bool blendenabled;
    GnmCombFunc colorfunc;
    GnmBlendOp colorsrcmult;
    GnmBlendOp colordstmult;
    GnmCombFunc alphafunc;
    GnmBlendOp alphasrcmult;
    GnmBlendOp alphadstmult;
    bool separatealphaenable;
} GnmBlendControl;
```

| Field | Type | Description |
|-------|------|-------------|
| `blendenabled` | `bool` | Enable blending for this target |
| `colorfunc` | `GnmCombFunc` | Color blend combination function |
| `colorsrcmult` | `GnmBlendOp` | Color source blend multiplier |
| `colordstmult` | `GnmBlendOp` | Color destination blend multiplier |
| `alphafunc` | `GnmCombFunc` | Alpha blend combination function |
| `alphasrcmult` | `GnmBlendOp` | Alpha source blend multiplier |
| `alphadstmult` | `GnmBlendOp` | Alpha destination blend multiplier |
| `separatealphaenable` | `bool` | Enable separate alpha blending |

---

## GnmDbRenderControl

Controls the DB (Depth Block) render state, including clears, HTile, and copy operations.

```c
typedef struct {
    bool depthclearenable;
    bool stencilclearenable;
    bool htileresummarizeenable;
    bool depthwritebackpol;
    bool stencilwritebackpol;
    bool forcedepthdecompress;
    bool copycentroidenable;
    uint8_t copysampleindex;
    bool copydepthtocolorenable;
    bool copystenciltocolorenable;
} GnmDbRenderControl;
```

| Field | Type | Description |
|-------|------|-------------|
| `depthclearenable` | `bool` | Enable depth clear |
| `stencilclearenable` | `bool` | Enable stencil clear |
| `htileresummarizeenable` | `bool` | Enable HTile resummarize |
| `depthwritebackpol` | `bool` | Depth write-back policy |
| `stencilwritebackpol` | `bool` | Stencil write-back policy |
| `forcedepthdecompress` | `bool` | Force depth decompression |
| `copycentroidenable` | `bool` | Enable copy centroid |
| `copysampleindex` | `uint8_t` | Copy sample index |
| `copydepthtocolorenable` | `bool` | Enable depth-to-color copy |
| `copystenciltocolorenable` | `bool` | Enable stencil-to-color copy |

---

## GnmDepthStencilControl

Controls depth and stencil testing state.

```c
typedef struct {
    bool zwrite;
    GnmDepthCompare zfunc;
    GnmDepthCompare stencilfunc;
    GnmDepthCompare stencilbackfunc;
    bool separatestencilenable;
    bool depthenable;
    bool stencilenable;
    bool depthboundsenable;
} GnmDepthStencilControl;
```

| Field | Type | Description |
|-------|------|-------------|
| `zwrite` | `bool` | Enable depth writes |
| `zfunc` | `GnmDepthCompare` | Depth comparison function |
| `stencilfunc` | `GnmDepthCompare` | Front-face stencil comparison function |
| `stencilbackfunc` | `GnmDepthCompare` | Back-face stencil comparison function |
| `separatestencilenable` | `bool` | Enable separate front/back stencil |
| `depthenable` | `bool` | Enable depth testing |
| `stencilenable` | `bool` | Enable stencil testing |
| `depthboundsenable` | `bool` | Enable depth bounds testing |

---

## GnmPrimitiveSetup

Controls primitive setup state including culling, fill mode, and provoking vertex.

```c
typedef struct {
    GnmCullMode cullmode;
    GnmFaceOrientation frontface;
    GnmFillMode frontmode;
    GnmFillMode backmode;
    bool frontoffsetmode;
    bool backoffsetmode;
    bool vertexwindowoffsetenable;
    GnmProvokingVertex provokemode;
    bool perspectivecorrectiondisable;
} GnmPrimitiveSetup;
```

| Field | Type | Description |
|-------|------|-------------|
| `cullmode` | `GnmCullMode` | Culling mode (none/front/back/both) |
| `frontface` | `GnmFaceOrientation` | Front-face winding orientation |
| `frontmode` | `GnmFillMode` | Front-face fill mode |
| `backmode` | `GnmFillMode` | Back-face fill mode |
| `frontoffsetmode` | `bool` | Front-face offset mode |
| `backoffsetmode` | `bool` | Back-face offset mode |
| `vertexwindowoffsetenable` | `bool` | Enable vertex window offset |
| `provokemode` | `GnmProvokingVertex` | Provoking vertex mode |
| `perspectivecorrectiondisable` | `bool` | Disable perspective correction |

---

## GnmViewportTransformControl

Controls which components of the viewport transform are applied.

```c
typedef struct {
    bool scalex;
    bool offsetx;
    bool scaley;
    bool offsety;
    bool scalez;
    bool offsetz;
    bool perspectivedividexy;
    bool perspectivedividez;
    bool invertw;
} GnmViewportTransformControl;
```

| Field | Type | Description |
|-------|------|-------------|
| `scalex` | `bool` | Apply X scale |
| `offsetx` | `bool` | Apply X offset |
| `scaley` | `bool` | Apply Y scale |
| `offsety` | `bool` | Apply Y offset |
| `scalez` | `bool` | Apply Z scale |
| `offsetz` | `bool` | Apply Z offset |
| `perspectivedividexy` | `bool` | Apply perspective divide on X/Y |
| `perspectivedividez` | `bool` | Apply perspective divide on Z |
| `invertw` | `bool` | Invert W coordinate |

---

## See Also

- [Render Target](rendertarget.md)
- [Depth Render Target](depthrendertarget.md)
- [Shader](shader.md)
- [Shader Binary](shaderbinary.md)
