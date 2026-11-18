#!/usr/bin/env python3
"""FC27 P1 research: reproduce redundant dynamic vertex-attribute pipeline keys.

Upstream-first witness for Lichii #408. The witness itself does not mutate runtime.
It proves the selected Encore raw Vulkan state still includes types for every
non-constant attribute register, while Shader::Info already records exactly
which generic attributes the vertex shader reads.
"""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
PIPE_PATH = ROOT / "encore-active/eden-vulkan/vk_pipeline_cache_cost.cpp"
HEADER_PATH = ROOT / "encore-active/eden-vulkan/include/video_core/renderer_vulkan/vk_pipeline_cache.h"
PIPE = PIPE_PATH.read_text()
FIXED = (ROOT / "encore-active/eden-vulkan/fixed_pipeline_state.cpp").read_text()
INFO = (ROOT / "encore-active/eden-shader/src/shader_recompiler/shader_info.h").read_text()
COLLECT = (
    ROOT / "encore-active/eden-shader/src/shader_recompiler/ir_opt/collect_shader_info_pass.cpp"
).read_text()


def git_blob_bytes(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode() + bytes([0]) + data).hexdigest()

# Exact successor receipt: remove ONLY this candidate's reviewed additions and
# recover the prior selected owner byte-for-byte. This lets older focused gates
# admit the successor without weakening their historical receipts.
assert git_blob_bytes(PIPE_PATH.read_bytes()) == "22e8403bb7b2722bae65742097b8cb18ef160f03"
predecessor = PIPE
for include in ("#include <map>\n", "#include <optional>\n"):
    assert predecessor.count(include) == 1
    predecessor = predecessor.replace(include, "", 1)

registry_begin = predecessor.index("#ifdef PS5_NATIVE\n// Lichii #408:")
registry_end = predecessor.index("#endif\n} // Anonymous namespace", registry_begin) + len("#endif\n")
predecessor = predecessor[:registry_begin] + predecessor[registry_end:]

refresh = predecessor.index("    graphics_key.state.Refresh(*maxwell3d, dynamic_features);")
restore_begin = predecessor.rfind("#ifdef PS5_NATIVE\n    if (native) {", 0, refresh)
assert restore_begin >= 0
restore_end = predecessor.index("#endif\n", restore_begin) + len("#endif\n")
predecessor = predecessor[:restore_begin] + predecessor[restore_end:]

refresh = predecessor.index("    graphics_key.state.Refresh(*maxwell3d, dynamic_features);")
mask_begin = predecessor.index("#ifdef PS5_NATIVE\n    if (!native && dynamic_features.has_dynamic_vertex_input)", refresh)
mask_end = predecessor.index("#endif\n", mask_begin) + len("#endif\n")
predecessor = predecessor[:mask_begin] + predecessor[mask_end:]

record_delta = """#ifdef PS5_NATIVE
        EdenRecordVertexAttributeMask(
            {key.unique_hashes[0], key.unique_hashes[1]}, programs[1].info);
#endif
"""
assert predecessor.count(record_delta) == 1
predecessor = predecessor.replace(record_delta, "", 1)
assert git_blob_bytes(predecessor.encode()) == "e3910d11f33376097a90858118ba276f7e02cb70"

header = HEADER_PATH.read_text()
assert git_blob_bytes(HEADER_PATH.read_bytes()) == "0502146a2ba56694ef2469e2949ad353d855c291"
header_delta = """#ifdef PS5_NATIVE
    // Dynamic vertex-input types before the shader-read mask. Refresh only
    // rewrites these when VertexInput is dirty, so restore this raw value
    // before the next shader key is evaluated.
    u64 unmasked_vertex_attribute_types{};
    bool unmasked_vertex_attribute_types_valid{};
#endif
"""
assert header.count(header_delta) == 1
header_predecessor = header.replace(header_delta, "", 1)
assert git_blob_bytes(header_predecessor.encode()) == "343383c7b5ed55f181cee9d840e6b59fae0409d0"

# Exact selected-source negative control: dynamic vertex input currently keys
# every non-constant attribute type, independently of what VertexB reads.
for needle in (
    "attribute_types = 0;",
    "for (size_t i = 0; i < Maxwell::NumVertexAttributes; ++i)",
    "attribute_types |= static_cast<u64>(type & mask) << (i * 2);",
):
    assert needle in FIXED, needle

# Encore already publishes the special all-attributes-unused case, but not a
# per-attribute mask. This is the gap that Lichii #408 addresses.
for needle in (
    "void EdenRecordAttributeless(EdenVertexIdentity vertex_hash, const Shader::Info& info)",
    "info.loads.Generic(index)",
    "EdenAttributeless({graphics_key.unique_hashes[0], graphics_key.unique_hashes[1]})",
):
    assert needle in PIPE, needle
# The runtime candidate must consume the published per-shader mask only on
# PS5 NS1/Maxwell and restore the raw union before every subsequent Refresh.
for needle in (
    "u64 EdenVertexAttributeReadMask(const Shader::Info& info) noexcept",
    "void EdenRecordVertexAttributeMask(EdenVertexIdentity vertex_hash, const Shader::Info& info)",
    "std::optional<u64> EdenKnownVertexAttributeMask(EdenVertexIdentity vertex_hash)",
    "if (native) {",
    "unmasked_vertex_attribute_types_valid = false;",
    "graphics_key.state.attribute_types = unmasked_vertex_attribute_types;",
    "unmasked_vertex_attribute_types = graphics_key.state.attribute_types;",
    "graphics_key.unique_hashes[0] == 0 && graphics_key.unique_hashes[1] != 0",
    "graphics_key.state.attribute_types &= *mask;",
    "it->second = ~u64{0};",
    "constexpr size_t EdenMaxVertexAttributeMasks = 8192;",
    "eden_vertex_attribute_masks.size() >= EdenMaxVertexAttributeMasks",
):
    assert needle in PIPE, needle

restore = PIPE.index("graphics_key.state.attribute_types = unmasked_vertex_attribute_types;")
refresh = PIPE.index("graphics_key.state.Refresh(*maxwell3d, dynamic_features);")
capture = PIPE.index("unmasked_vertex_attribute_types = graphics_key.state.attribute_types;")
masked = PIPE.index("graphics_key.state.attribute_types &= *mask;")
assert restore < refresh < capture < masked
assert PIPE.index("EdenRecordVertexAttributeMask(") < PIPE.rindex("EdenRecordVertexAttributeMask(")

GRAPHICS = (ROOT / "encore-active/eden-vulkan/vk_graphics_pipeline_cost.cpp").read_text()
assert "stage_infos[0].loads.Generic(index)" in GRAPHICS

# The shader compiler already owns the information needed for a safe mask.
assert "VaryingState loads;" in INFO
assert "bool loads_indexed_attributes{};" in INFO
for needle in (
    "info.loads.mask[static_cast<size_t>(inst.Arg(0).Attribute())] = true;",
    "header.vtg.InputGeneric(index)",
    "info.loads.Set(attribute + i, mask[i]);",
):
    assert needle in COLLECT, needle

MASK64 = (1 << 64) - 1

def raw_attribute_types(types, constants):
    """Model selected FixedPipelineState::Refresh: two bits per attribute."""
    assert len(types) == 32 and len(constants) == 32
    out = 0
    for i, value in enumerate(types):
        out |= (value & (0 if constants[i] else 3)) << (i * 2)
    return out & MASK64

def vertex_attribute_mask(input_generic):
    """Lichii #408 policy: keep both type bits if any component is read."""
    assert len(input_generic) == 32
    out = 0
    for i, components in enumerate(input_generic):
        if any(components):
            out |= 3 << (i * 2)
    return out & MASK64

def keyed_attribute_types(attribute_types, unique_hashes, attribute_masks):
    """Conservative upstream rule: VertexA disables masking."""
    assert len(unique_hashes) == len(attribute_masks) == 6
    if unique_hashes[0]:
        return attribute_types
    result = attribute_types
    for stage in range(1, len(unique_hashes)):
        if unique_hashes[stage]:
            result &= attribute_masks[stage]
    return result & MASK64

# Attribute 0, 3 and 31 are read; all others are irrelevant to this vertex program.
inputs = [[False] * 4 for _ in range(32)]
inputs[0][0] = True
inputs[3][3] = True
inputs[31][1] = inputs[31][2] = True
mask = vertex_attribute_mask(inputs)
expected_mask = (3 << 0) | (3 << 6) | (3 << 62)
assert mask == expected_mask

constants = [False] * 32
types_a = [1] * 32
types_b = types_a.copy()
types_b[12] = 3  # unread register changes
raw_a = raw_attribute_types(types_a, constants)
raw_b = raw_attribute_types(types_b, constants)
assert raw_a != raw_b, "negative control must reproduce current redundant key split"

hashes = [0, 0xBEEF, 0, 0, 0, 0xF00D]
stage_masks = [0, mask, MASK64, MASK64, MASK64, MASK64]
masked_a = keyed_attribute_types(raw_a, hashes, stage_masks)
masked_b = keyed_attribute_types(raw_b, hashes, stage_masks)
assert masked_a == masked_b, "unread attribute change should collapse to one key"

# Counterexample: a type change on a shader-read attribute must remain distinct.
types_used = types_a.copy()
types_used[3] = 2
raw_used = raw_attribute_types(types_used, constants)
assert keyed_attribute_types(raw_a, hashes, stage_masks) != keyed_attribute_types(
    raw_used, hashes, stage_masks
)

# VertexA+VertexB merging remains conservative: do not mask the key.
dual_hashes = [0xA, 0xB, 0, 0, 0, 0xF]
assert keyed_attribute_types(raw_a, dual_hashes, stage_masks) == raw_a
assert keyed_attribute_types(raw_b, dual_hashes, stage_masks) == raw_b

# Fully-read programs retain the exact old key; no-read programs reduce to
# zero, matching Encore's existing attributeless optimization.
all_inputs = [[True] * 4 for _ in range(32)]
all_mask = vertex_attribute_mask(all_inputs)
assert all_mask == MASK64
all_stage_masks = [0, all_mask, MASK64, MASK64, MASK64, MASK64]
assert keyed_attribute_types(raw_a, hashes, all_stage_masks) == raw_a
none_stage_masks = [0, 0, MASK64, MASK64, MASK64, MASK64]
assert keyed_attribute_types(raw_a, hashes, none_stage_masks) == 0

print(
    "FC27_VERTEX_ATTRIBUTE_KEY_NEGATIVE_CONTROL_PASS "
    "current_unused_register_splits=1"
)
print(
    "FC27_VERTEX_ATTRIBUTE_MASK_CANDIDATE_PASS "
    "unread_collapses=1 read_type_preserved=1 vertexA_conservative=1 "
    "raw_restore_order=1 ns2_excluded=1 collision_widens=1 registry_bounded=8192 "
    "shader_info_source=selected runtime_candidate=1 gameplay_unverified=1"
)
