# Platform

API reference for the GNM platform initialization and GPU mode query functions.

Header: `platform.h`

---

## Functions

### sceGnmGpuMode

Returns the current GPU mode (base PS4 or NEO/Pro).

```c
GnmGpuMode PS4_SYSV_ABI sceGnmGpuMode(void);
```

**Returns:** `GnmGpuMode` — the active GPU mode.

The `PS4_SYSV_ABI` macro ensures the function uses the PS4 System V calling convention.

---

## Types

### GnmPlatParams

Parameters passed to `sceGnmPlatInit` to configure the platform layer.

```c
typedef struct {
    GnmGpuMode gpumode;
    int32_t (*getbufferlabeladdress)(
        int32_t videohandle, uint64_t* outaddr
    );
} GnmPlatParams;
```

| Field | Type | Description |
|-------|------|-------------|
| `gpumode` | `GnmGpuMode` | The target GPU mode (base or NEO) |
| `getbufferlabeladdress` | `int32_t (*)(int32_t, uint64_t*)` | Function pointer to retrieve a buffer label address for a video handle |

---

## Functions (continued)

### sceGnmPlatInit

Initializes the GNM platform layer with the supplied parameters.

```c
void sceGnmPlatInit(GnmPlatParams* params);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `params` | `GnmPlatParams*` | Platform configuration parameters |

---

### sceGnmPlatGetBufferLabelAddress

Retrieves the GPU-visible buffer label address for a given video handle. This is the function that `GnmPlatParams.getbufferlabeladdress` points to.

```c
int32_t PS4_SYSV_ABI sceGnmPlatGetBufferLabelAddress(
    int32_t videohandle, uint64_t* outaddr
);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `videohandle` | `int32_t` | The video handle to query |
| `outaddr` | `uint64_t*` | Output pointer that receives the buffer label GPU address |

**Returns:** `int32_t` — zero on success, negative on error.

---

## See Also

- [Command Buffer](commandbuffer.md)
- [Draw Command Buffer](drawcommandbuffer.md)
- [Compatibility Layer](compat.md)
