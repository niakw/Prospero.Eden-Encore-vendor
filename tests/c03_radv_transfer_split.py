#!/usr/bin/env python3
"""C03 fail-closed source provenance for the seven RADV split sites.

Checks maintained compiler units by exact reconstruction against the current baseline sources.
Source-only, not native compilation, GPU execution, or PS5 qualification.
"""
from pathlib import Path

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

# Independent reviewed Mesa R397 fix carried by the same canonical TU.
# Reverse it explicitly after removing C03 so this oracle still fails on any
# unapproved third drift instead of treating the whole maintained file as C03.
BIN_HEIGHT_OLD = "extent.height = MAX2(extent.width, pdev->info.gfx_level >= GFX12 ? 128 : 64);"
BIN_HEIGHT_NEW = "extent.height = MAX2(extent.height, pdev->info.gfx_level >= GFX12 ? 128 : 64);"


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
    recovered = live.replace(FORWARD, "", 1).replace(SIGNATURE, "static " + SIGNATURE, 1).replace(BARRIER, "", 1)
    assert live.count(BIN_HEIGHT_NEW) == 1 and BIN_HEIGHT_OLD not in live
    assert old.count(BIN_HEIGHT_OLD) == 1 and BIN_HEIGHT_NEW not in old
    recovered = recovered.replace(BIN_HEIGHT_NEW, BIN_HEIGHT_OLD, 1)
    assert recovered == old, "maintained RADV code drifted beyond C03 + reviewed R397"
    print("C03_DRIVER_SOURCE_PASS barrier_sites=1 existing_dispatch_sites=3 r397_bin_height=1")


if __name__ == "__main__":
    verify_driver()
    for path, expected_sites in SITES.items():
        verify_meta(path, expected_sites)
    print("C03_RADV_SOURCE_REPRODUCTION_PASS transfer_sites=6 barrier_sites=1 originals_immutable=1")
