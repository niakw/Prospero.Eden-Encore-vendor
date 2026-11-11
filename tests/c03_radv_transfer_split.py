#!/usr/bin/env python3
"""C03 fail-closed source provenance for the seven RADV split sites.

Checks complete maintained compiler units against original Git blobs.
Source-only, not native compilation, GPU execution, or PS5 qualification.
"""
from pathlib import Path
import hashlib

ROOT = Path(__file__).resolve().parents[1]
ACTIVE = ROOT / "encore-active/ps5-mesa"
ORIG = ROOT / "sources/ps5-mesa"

PREIMAGES = {
    "src/amd/vulkan/radv_cmd_buffer.c": "9dd12c19a1a277a2753432e1411a449fdf923c4a",
    "src/amd/vulkan/meta/radv_meta_buffer.c": "559272a4c99014a1cfb6c62aa7d81b2301729fd7",
    "src/amd/vulkan/meta/radv_meta_copy.c": "b48ad04b1637898c5e2b95e438e2e9ae9ac522a1",
}
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


def blob(text: str) -> str:
    data = text.encode()
    return hashlib.sha1(b"blob " + str(len(data)).encode() + bytes([0]) + data).hexdigest()


def verify_meta(rel: str, sites: tuple[str, ...]) -> None:
    live = (ACTIVE / rel).read_text()
    old = (ORIG / rel).read_text()
    assert blob(old) == PREIMAGES[rel], rel
    assert live.count(PROTO) == 1, rel
    assert live.count(TRANSFER) == len(sites), rel
    for site in sites:
        start = live.index(site)
        body = live.index("VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);", start)
        call = live.index(TRANSFER, body)
        assert call - body < 150, (rel, site, "split not immediately after command buffer binding")
    recovered = live.replace(chr(10) + PROTO, "").replace(TRANSFER, "")
    assert recovered == old, (rel, "unapproved full-unit drift")
    assert blob(recovered) == PREIMAGES[rel]
    print("C03_META_SOURCE_PASS", rel, "split_sites=" + str(len(sites)))


def verify_driver() -> None:
    rel = "src/amd/vulkan/radv_cmd_buffer.c"
    live = (ACTIVE / rel).read_text()
    assert live.count(SIGNATURE) == 1
    assert live.count(BARRIER) == 1
    assert live.count("radv_split_cmd_stream_if_due(cmd_buffer);") == 4
    header = "radv_CmdPipelineBarrier2(VkCommandBuffer commandBuffer, const VkDependencyInfo *pDependencyInfo)\n"
    assert header in live
    body = live.index(header)
    command = live.index("VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);", body)
    call = live.index(BARRIER, command)
    assert call - command < 150
    recovered = live.replace(SIGNATURE, "static " + SIGNATURE, 1).replace(BARRIER, "", 1)
    assert blob(recovered) == PREIMAGES[rel], "maintained RADV code drifted beyond C03"
    print("C03_DRIVER_SOURCE_PASS barrier_sites=1 existing_dispatch_sites=3")


if __name__ == "__main__":
    verify_driver()
    for path, expected_sites in SITES.items():
        verify_meta(path, expected_sites)
    print("C03_RADV_SOURCE_REPRODUCTION_PASS transfer_sites=6 barrier_sites=1 originals_immutable=1")
