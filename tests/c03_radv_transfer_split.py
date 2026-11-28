#!/usr/bin/env python3
"""C03 fail-closed source provenance for the seven RADV split sites.

Checks maintained compiler units by exact reconstruction against the current baseline sources.
Source-only, not native compilation, GPU execution, or PS5 qualification.
"""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
ACTIVE = ROOT / "encore-active/ps5-mesa"
ORIG = ROOT / "sources/ps5-mesa"

SITES = {
    "src/amd/vulkan/meta/radv_meta_buffer.c": (
        "radv_CmdFillBuffer(",
        "radv_CmdCopyBuffer2(",
        "radv_CmdUpdateBuffer(",
    ),
    "src/amd/vulkan/meta/radv_meta_copy.c": (
        "radv_CmdCopyBufferToImage2(",
        "radv_CmdCopyImageToBuffer2(",
        "radv_CmdCopyImage2(",
    ),
}
PROTO = ("/* C03: shared command-stream split, implemented in radv_cmd_buffer.c. */\n"
         "void radv_split_cmd_stream_if_due(struct radv_cmd_buffer *cmd_buffer);\n")
TRANSFER = "   radv_split_cmd_stream_if_due(cmd_buffer); /* Encore C03: transfer-only streams */\n"
BARRIER = "   radv_split_cmd_stream_if_due(cmd_buffer); /* Encore C03: split barrier-only streams */\n"
SIGNATURE = "void\nradv_split_cmd_stream_if_due(struct radv_cmd_buffer *cmd_buffer)\n"
FORWARD = "void radv_split_cmd_stream_if_due(struct radv_cmd_buffer *cmd_buffer);" + chr(10) * 2

# Exact maintained driver immediately before C03. It already includes the
# independently reviewed R397/CodeQL fixes; C03 must reverse to this blob,
# not to pristine Mesa.
PRE_C03_DRIVER_GIT_BLOB = "9dd12c19a1a277a2753432e1411a449fdf923c4a"


def verify_meta(rel: str, sites: tuple[str, ...]) -> None:
    live = (ACTIVE / rel).read_text()
    old = (ORIG / rel).read_text()
    assert live.count(PROTO) == 1, rel
    assert live.count(TRANSFER) == len(sites), rel
    for site in sites:
        start = live.index(site)
        body = live.index("VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);", start)
        call = live.index(TRANSFER, body)
        assert call - body < 150, (rel, site, "split not immediately after command buffer binding")
    recovered = live.replace(chr(10) + PROTO, "").replace(TRANSFER, "")
    assert recovered == old, (rel, "unapproved full-unit drift")
    print("C03_META_SOURCE_PASS", rel, "split_sites=" + str(len(sites)))


def verify_driver() -> None:
    rel = "src/amd/vulkan/radv_cmd_buffer.c"
    live = (ACTIVE / rel).read_text()
    old = (ORIG / rel).read_text()
    assert live.count(SIGNATURE) == 1
    assert live.count(FORWARD) == 1
    assert live.count(BARRIER) == 1
    assert live.count("radv_split_cmd_stream_if_due(cmd_buffer);") == 4
    header = "radv_CmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo)\n"
    assert header in live
    body = live.index(header)
    command = live.index("VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);", body)
    call = live.index(BARRIER, command)
    assert call - command < 150
    recovered = live.replace(FORWARD, "", 1).replace(
        SIGNATURE, "static " + SIGNATURE, 1).replace(BARRIER, "", 1)
    data = recovered.encode()
    predecessor = hashlib.sha1(
        b"blob " + str(len(data)).encode() + b"\0" + data
    ).hexdigest()
    assert predecessor == PRE_C03_DRIVER_GIT_BLOB, (
        "maintained RADV code drifted beyond C03", predecessor)
    assert recovered != old, "pre-C03 maintained driver unexpectedly equals pristine Mesa"
    print("C03_DRIVER_SOURCE_PASS barrier_sites=1 existing_dispatch_sites=3 predecessor_exact=1")


if __name__ == "__main__":
    verify_driver()
    for path, expected_sites in SITES.items():
        verify_meta(path, expected_sites)
    print("C03_RADV_SOURCE_REPRODUCTION_PASS transfer_sites=6 barrier_sites=1 originals_immutable=1")
