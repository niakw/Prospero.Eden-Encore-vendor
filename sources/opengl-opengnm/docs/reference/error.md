# Error Handling

Error codes, message severity levels, and the message handler infrastructure for the opengnm GNM library. The `GnmError` enum includes both Sony SDK error codes (returned by the firmware driver) and opengnm-internal error codes. The message system allows the library to report warnings and errors through a user-installable handler.

---

## GnmError Enum

All error codes returned by GNM functions. `ORBIS_GNM_ERROR_*` values match the Sony SDK and are returned by the firmware driver. `GNM_ERROR_*` values are opengnm-internal.

### Success

| Constant | Value | Description |
|---|---|---|
| `GNM_ERROR_OK` | `0x0` | No error — operation succeeded |

### Sony SDK Submission Errors

Errors returned by the firmware driver when command buffer submission fails.

| Constant | Value | Description |
|---|---|---|
| `ORBIS_GNM_ERROR_SUBMISSION_FAILED_INVALID_ARGUMENT` | `0x80d11000` | Submission failed due to an invalid argument |
| `ORBIS_GNM_ERROR_SUBMISSION_NOT_ENOUGH_RESOURCES` | `0x80d11001` | Submission failed due to insufficient resources |
| `ORBIS_GNM_ERROR_SUBMISSION_AND_FLIP_FAILED_INVALID_COMMAND_BUFFER` | `0x80d11080` | Submit-and-flip failed due to an invalid command buffer |
| `ORBIS_GNM_ERROR_SUBMISSION_AND_FLIP_FAILED_INVALID_QUEUE_FULL` | `0x80d11081` | Submit-and-flip failed because the queue is full |
| `ORBIS_GNM_ERROR_SUBMISSION_AND_FLIP_FAILED_REQUEST_FAILED` | `0x80d11082` | Submit-and-flip request failed |
| `ORBIS_GNM_ERROR_SUBMISSION_FAILED_INTERNAL_ERROR` | `0x80d110ff` | Submission failed due to an internal driver error |

### Sony SDK Validation Warnings

Non-fatal validation warnings issued when validation is enabled.

| Constant | Value | Description |
|---|---|---|
| `ORBIS_GNM_ERROR_VALIDATION_WARNING` | `0x80d12000` | Generic validation warning |
| `ORBIS_GNM_ERROR_VALIDATION_WARNING_RWBUFFER_ROTYPE` | `0x80d12001` | Warning: RW buffer with read-only type |
| `ORBIS_GNM_ERROR_VALIDATION_WARNING_BLENDING` | `0x80d12002` | Warning: blending configuration issue |
| `ORBIS_GNM_ERROR_VALIDATION_WARNING_DRAW_CU_MASK` | `0x80d12003` | Warning: draw CU mask issue |
| `ORBIS_GNM_ERROR_VALIDATION_WARNING_MRT_SETUP` | `0x80d12004` | Warning: MRT (multiple render target) setup issue |
| `ORBIS_GNM_ERROR_VALIDATION_WARNING_DEPTH_RT_SETUP` | `0x80d12005` | Warning: depth render target setup issue |
| `ORBIS_GNM_ERROR_VALIDATION_WARNING_PS_AND_MRT_FORMAT` | `0x80d1200f` | Warning: pixel shader and MRT format mismatch |

### Sony SDK Validation Errors

Fatal validation errors issued when validation is enabled.

| Constant | Value | Description |
|---|---|---|
| `ORBIS_GNM_ERROR_VALIDATION_ERROR` | `0x80d13000` | Generic validation error |
| `ORBIS_GNM_ERROR_VALIDATION_VSHARP` | `0x80d13001` | Invalid V# (buffer resource) |
| `ORBIS_GNM_ERROR_VALIDATION_TSHARP` | `0x80d13002` | Invalid T# (texture resource) |
| `ORBIS_GNM_ERROR_VALIDATION_RESOURCE` | `0x80d13003` | Invalid resource |
| `ORBIS_GNM_ERROR_VALIDATION_TABLE_MEMORY` | `0x80d13004` | Table memory validation error |
| `ORBIS_GNM_ERROR_VALIDATION_WRITE_EVENT_OP` | `0x80d13005` | Invalid write event operation |
| `ORBIS_GNM_ERROR_VALIDATION_INDEX_BUFFER` | `0x80d13006` | Invalid index buffer |
| `ORBIS_GNM_ERROR_VALIDATION_TESS_FACTOR_BUFFER` | `0x80d13007` | Invalid tessellation factor buffer |
| `ORBIS_GNM_ERROR_VALIDATION_SCRATCH_RING` | `0x80d13008` | Invalid scratch ring |
| `ORBIS_GNM_ERROR_VALIDATION_PRIMITIVE_TYPE` | `0x80d13009` | Invalid primitive type |
| `ORBIS_GNM_ERROR_VALIDATION_INDEX_SIZE` | `0x80d1300a` | Invalid index size |
| `ORBIS_GNM_ERROR_VALIDATION_INLINE_DRAW_SIZE` | `0x80d1300b` | Invalid inline draw size |
| `ORBIS_GNM_ERROR_VALIDATION_NUM_INPUT_PATCHES` | `0x80d1300c` | Invalid number of input patches |
| `ORBIS_GNM_ERROR_VALIDATION_GS_MODE` | `0x80d1300d` | Invalid geometry shader mode |
| `ORBIS_GNM_ERROR_VALIDATION_SHADER_ADDRESS` | `0x80d1300e` | Invalid shader address |
| `ORBIS_GNM_ERROR_VALIDATION_BORDER_COLOR_TABLE` | `0x80d1300f` | Invalid border color table |
| `ORBIS_GNM_ERROR_VALIDATION_SSHARP` | `0x80d13010` | Invalid S# (sampler resource) |
| `ORBIS_GNM_ERROR_VALIDATION_DISPATCH_DRAW` | `0x80d13011` | Invalid dispatch draw |
| `ORBIS_GNM_ERROR_VALIDATION_ACTIVE_SHADER_STAGE` | `0x80d13012` | Invalid active shader stage |
| `ORBIS_GNM_ERROR_VALIDATION_DCB` | `0x80d13013` | Invalid draw command buffer (DCB) |
| `ORBIS_GNM_ERROR_VALIDATION_MISMATCH_SHADER_STAGE` | `0x80d13014` | Mismatched shader stage |
| `ORBIS_GNM_ERROR_VALIDATION_MRT_SETUP` | `0x80d13015` | MRT setup validation error |
| `ORBIS_GNM_ERROR_VALIDATION_BAD_OP_CODE` | `0x80d13016` | Bad opcode in command buffer |
| `ORBIS_GNM_ERROR_VALIDATION_DEPTH_RT_SETUP` | `0x80d13017` | Depth render target setup validation error |
| `ORBIS_GNM_ERROR_VALIDATION_NUM_INSTANCES` | `0x80d13018` | Invalid number of instances |
| `ORBIS_GNM_ERROR_VALIDATION_SRT` | `0x80d13019` | Invalid SRT (shader resource table) |
| `ORBIS_GNM_ERROR_VALIDATION_INVALID_ARGUMENT` | `0x80d13ffd` | Validation: invalid argument |
| `ORBIS_GNM_ERROR_VALIDATION_FAILED_INTERNAL_ERROR` | `0x80d13ffe` | Validation: internal error |
| `ORBIS_GNM_ERROR_VALIDATION_NOT_ENABLED` | `0x80d13fff` | Validation is not enabled |

### Sony SDK Capture Errors

Errors related to GPU command capture (Razor integration).

| Constant | Value | Description |
|---|---|---|
| `ORBIS_GNM_ERROR_CAPTURE_FILE_IO` | `0x80d15000` | Capture file I/O error |
| `ORBIS_GNM_ERROR_CAPTURE_RAZOR_NOT_LOADED` | `0x80d15001` | Razor is not loaded |
| `ORBIS_GNM_ERROR_CAPTURE_NOTHING_TO_CAPTURE` | `0x80d15002` | Nothing to capture |
| `ORBIS_GNM_ERROR_CAPTURE_FAILED_INTERNAL` | `0x80d1500f` | Capture failed due to internal error |

### Sony SDK Generic Failure

| Constant | Value | Description |
|---|---|---|
| `ORBIS_GNM_ERROR_FAILURE` | `0x8eee00ff` | Generic failure — returned by all stub functions on retail firmware (SDMA, debugger, profiler, resource registration, etc.). Same value as `GNM_ERROR_INTERNAL_FAILURE`. |

### opengnm Internal Errors

Error codes used internally by the opengnm library.

| Constant | Value | Description |
|---|---|---|
| `GNM_ERROR_INVALID_ARGS` | `0x8eee0001` | Invalid argument(s) passed to function |
| `GNM_ERROR_INTERNAL_FAILURE` | `0x8eee00ff` | Internal failure |
| `GNM_ERROR_CMD_FAILED` | `0xffffffff` | Command failed |
| `GNM_ERROR_INVALID_ALIGNMENT` | `1` | Invalid alignment of a pointer or resource |
| `GNM_ERROR_INVALID_STATE` | `2` | Invalid state for the requested operation |
| `GNM_ERROR_OVERFLOW` | `3` | Buffer or resource overflow |
| `GNM_ERROR_UNDERFLOW` | `4` | Buffer or resource underflow |
| `GNM_ERROR_UNSUPPORTED` | `5` | Operation or feature is unsupported |
| `GNM_ERROR_SEMANTIC_NOT_FOUND` | `6` | Shader semantic not found |
| `GNM_ERROR_ASM_FAILED` | `7` | Shader assembly failed |

---

## GnmMessageSeverity Enum

Severity level for GNM messages reported through the message handler.

| Constant | Value | Description |
|---|---|---|
| `GNM_MSGSEV_WARN` | `0` | Warning — non-fatal issue |
| `GNM_MSGSEV_ERR` | `1` | Error — potentially fatal issue |

---

## GnmMessageHandlerFunc Typedef

Function pointer type for the user-installable message handler callback.

```c
typedef void (*GnmMessageHandlerFunc)(
    GnmMessageSeverity severity,
    const char* message,
    void* userdata
);
```

| Parameter | Type | Description |
|---|---|---|
| `severity` | `GnmMessageSeverity` | Severity of the message (`GNM_MSGSEV_WARN` or `GNM_MSGSEV_ERR`) |
| `message` | `const char*` | Human-readable message string |
| `userdata` | `void*` | User-provided opaque pointer passed through from `sceGnmSetMessageHandler` |

---

## Functions

### sceGnmStrError

Returns a human-readable string describing the given error code.

```c
const char* sceGnmStrError(GnmError err) PS4_SYSV_ABI;
```

| Parameter | Type | Description |
|---|---|---|
| `err` | `GnmError` | The error code to describe |

**Returns:** A pointer to a static string describing the error.

---

### sceGnmSetMessageHandler

Installs (or removes) a message handler callback. When the library needs to report a warning or error, it calls the installed handler. Pass `NULL` to remove the current handler.

```c
void sceGnmSetMessageHandler(GnmMessageHandlerFunc handlerfunc, void* userdata);
```

| Parameter | Type | Description |
|---|---|---|
| `handlerfunc` | `GnmMessageHandlerFunc` | Callback function, or `NULL` to remove the handler |
| `userdata` | `void*` | Opaque pointer passed to the callback as `userdata` |

---

### sceGnmWriteMsg

Reports a message with the given severity through the installed message handler. If no handler is installed, the message is ignored.

```c
void sceGnmWriteMsg(GnmMessageSeverity sev, const char* msg);
```

| Parameter | Type | Description |
|---|---|---|
| `sev` | `GnmMessageSeverity` | Severity of the message |
| `msg` | `const char*` | Message string to report |

---

### sceGnmWriteMsgf

Reports a formatted message with the given severity through the installed message handler. Uses `printf`-style formatting. If no handler is installed, the message is ignored.

```c
void sceGnmWriteMsgf(GnmMessageSeverity sev, const char* fmt, ...);
```

| Parameter | Type | Description |
|---|---|---|
| `sev` | `GnmMessageSeverity` | Severity of the message |
| `fmt` | `const char*` | `printf`-style format string |
| `...` | | Format arguments |

---

## See Also

- [Types & Enums](types.md) — Core type definitions and enumerations
- [Buffer](buffer.md) — Uses `sceGnmWriteMsg`/`sceGnmWriteMsgf` for validation
- [Texture](texture.md) — Returns `GnmError` from creation functions
