#!/usr/bin/env python3
"""Exact-source guard for the NS2 NVK default M2MF subchannel backport.

Upstream evidence:
  Eglantine/Lichii bf1de8f40130 — Switch 2 pushbuffer/semaphore fixes.
This gate proves the Encore-owned override differs from the pinned Eden
snapshot only by binding NVK subchannel 2 to the already-existing
KeplerMemory owner. It does not claim PS5 runtime compatibility.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
pristine = (root / "sources/eden/src/video_core/control/channel_state.cpp").read_text()
active = (root / "encore-active/eden/src/video_core/control/channel_state.cpp").read_text()

old_decl = """constexpr u32 NvkComputeSubchannel = 1;
constexpr u32 Nvk2DSubchannel = 3;"""
new_decl = """constexpr u32 NvkComputeSubchannel = 1;
// Eglantine bf1de8f40130: NVN2 uses subchannel 2 as M2MF/inline-to-memory
// before an explicit SET_OBJECT. KeplerMemory owns the matching layout.
constexpr u32 NvkM2mfSubchannel = 2;
constexpr u32 Nvk2DSubchannel = 3;"""
old_bind = """    // Subchannel 2 is M2MF there; Eden does not expose a 0x9039 engine yet.
    dma_pusher.BindSubchannel(&payload.fermi_2d, Nvk2DSubchannel, Engines::EngineTypes::Fermi2D);"""
new_bind = """    // NVK/NVN2 may use M2MF/inline-to-memory on subchannel 2 without an
    // explicit bind. KeplerMemory has the compatible LINE_LENGTH_IN /
    // OFFSET_OUT / LAUNCH_DMA / LOAD_INLINE_DATA layout used by upstream.
    dma_pusher.BindSubchannel(&payload.kepler_memory, NvkM2mfSubchannel,
                              Engines::EngineTypes::KeplerMemory);
    dma_pusher.BindSubchannel(&payload.fermi_2d, Nvk2DSubchannel, Engines::EngineTypes::Fermi2D);"""

assert pristine.count(old_decl) == 1
assert pristine.count(old_bind) == 1
expected = pristine.replace(old_decl, new_decl).replace(old_bind, new_bind)
assert active == expected, "maintained channel_state has changes beyond the reviewed M2MF default binding"
assert "payload.kepler_memory, NvkM2mfSubchannel" in active
assert "Engines::EngineTypes::KeplerMemory" in active
assert "constexpr u32 NvkM2mfSubchannel = 2;" in active
print("NS2_NVK_M2MF_DEFAULT_SUBCHANNEL_PASS subchannel=2 owner=KeplerMemory upstream=bf1de8f ps5=unverified")
