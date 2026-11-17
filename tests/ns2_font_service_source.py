#!/usr/bin/env python3
from pathlib import Path
R=Path(__file__).resolve().parents[1]
root=R/"encore-active/ns2-runtime/font_service/core/hle/service/ns"
cpp=(root/"platform_service_manager.cpp").read_text()
hdr=(root/"platform_service_manager.h").read_text()
merge=(root/"font_merge.cpp").read_text()
merge_h=(root/"font_merge.h").read_text()

for token in (
    '{0, D<&IPlatformServiceManager::RequestLoad>, "RequestLoad"}',
    '{1, D<&IPlatformServiceManager::GetLoadState>, "GetLoadState"}',
    '{2, D<&IPlatformServiceManager::GetSize>, "GetSize"}',
    '{3, D<&IPlatformServiceManager::GetSharedMemoryAddressOffset>, "GetSharedMemoryAddressOffset"}',
    '{4, D<&IPlatformServiceManager::GetSharedMemoryNativeHandle>, "GetSharedMemoryNativeHandle"}',
    '{5, D<&IPlatformServiceManager::GetSharedFontInOrderOfPriority>, "GetSharedFontInOrderOfPriority"}',
):
    assert token in cpp, token

for token in (
    '{2000, &IPlatformServiceManager::GetBuiltInFontSharedMemory',
    '{2010, &IPlatformServiceManager::GetBuiltInFontData',
    '{2020, &IPlatformServiceManager::GetBuiltInFontDataSet',
    '{2100, &IPlatformServiceManager::RequestLoadNxFont',
    'class INx2FontService final',
    'Kernel::KTransferMemory',
    'builtin_font_memory_size',
    'Common::AlignUp(builtin_offset + size, 0x1000)',
    'Kernel::Svc::MemoryPermission::Read',
    'ReplyFontDataSet(ctx, impl->builtin_font_regions)',
):
    assert token in cpp, token
for token in (
    'void RequestLoadNxFont(HLERequestContext& ctx);',
    'void GetBuiltInFontSharedMemory(HLERequestContext& ctx);',
    'void GetBuiltInFontData(HLERequestContext& ctx);',
    'void GetBuiltInFontDataSet(HLERequestContext& ctx);',
):
    assert token in hdr, token

assert 'BUTTON_GLYPHS' in cpp
assert 'MergeGlyphs(' in cpp
assert 'MergeGlyphs(' in merge and 'MergeGlyphs(' in merge_h
for forbidden in ('0400C3F00006E000','040074A01BF12000','sceKernelAllocate','512_MiB'):
    assert forbidden not in cpp, forbidden
assert "501ed81eb024e1d1b069baf5397caef507a33c7a" in cpp
print("NS2_FONT_SERVICE_SOURCE_PASS cmds=2000,2010,2020,2100 ns1_0_6=preserved merge_glyphs=1 upstream=501ed81e ps5=unverified")
