#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real JXR decoder/link and security-backport smoke tests, host only."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
JXR = ROOT / "sources/jxrlib"
ADAPTER = ROOT / "encore-active/ps5-jxr"
checks = {
    "jxrgluelib/JXRMeta.c": (
        "Call(uCount > ((U32)-1) - 2 ? WMP_errFail : WMP_errSuccess);",
        "Call(PKAlloc((void **) &pbData, (size_t)uCount + 2))",
    ),
    "image/decode/strdec.c": (
        "if(szMS[7] != 0)",
        "strstr((char *) szMS,",
        "if(pII->bdBitDepth < BD_1 || pII->bdBitDepth >= BDB_MAX)",
        "cbChannels[pII->bdBitDepth]",
        "if (pSC->p1MBbuffer[0] == NULL)",
        "memset(pSC->p1MBbuffer[0],",
    ),
    "image/decode/segdec.c": (
        "if (iMaxRun < 0 || iMaxRun > 14)",
        "gSignificantRunBin[iMaxRun]",
    ),
    "jxrgluelib/JXRGlueJxr.c": (
        "FailIf(uValue >= O_MAX, WMP_errUnsupportedFormat);",
        "pID->WMP.fOrientationFromContainer = TRUE;",
        "FailIf((szSig[0] != 'I' || szSig[1] != 'I'), WMP_errUnsupportedFormat);",
    ),
}
for path, anchors in checks.items():
    source = (JXR / path).read_text()
    for anchor in anchors:
        assert anchor in source, (path, anchor)
    for i in range(0, len(anchors) - 1, 2):
        assert source.index(anchors[i]) < source.index(anchors[i + 1]), (path, anchors[i])
assert 'szSig != strstr(szSig, "II")' not in (JXR / "jxrgluelib/JXRGlueJxr.c").read_text()

harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "JXRGlue.h"
#include "jxr_decoder.h"
#include "strcodec.h"
#include <webp/decode.h>
extern ERR ReadBinaryData(struct WMPStream *, const U32, const U32, U8 **);

static void rejected_inputs(void)
{
    U8 *data = NULL;
    /* Overflow boundary must be rejected before stream use or allocation. */
    assert(ReadBinaryData(NULL, (U32)-1, 0, &data) != WMP_errSuccess);
    assert(data == NULL);
    assert(!encore_jxr_to_webp("missing.jxr", "out.webp"));
    assert(!encore_jxr_to_webp("short.jxr", "out.webp"));
    assert(!encore_jxr_to_webp("not-an-image.jxr", "out.webp"));
    /* Exercise the real factory failure path independently of adapter prechecks. */
    PKCodecFactory *codec = NULL;
    PKImageDecode *decoder = NULL;
    assert(PKCreateCodecFactory(&codec, WMP_SDK_VERSION) == WMP_errSuccess);
    for (int i = 0; i < 16; ++i) {
        assert(codec->CreateDecoderFromFile("not-an-image.jxr", &decoder) != WMP_errSuccess);
        assert(decoder == NULL);
        assert(codec->CreateDecoderFromFile("missing.jxr", &decoder) != WMP_errSuccess);
        assert(decoder == NULL);
    }
    assert(codec->Release(&codec) == WMP_errSuccess);
    assert(!encore_jxr_to_webp("oversized.jxr", "out.webp"));
    assert(!encore_jxr_to_webp("link.jxr", "out.webp"));
    assert(access("out.webp", F_OK) != 0);
}

static void bit_width_boundaries(void)
{
    U16 slots[4] = {0xaaaa, 0xbbbb, 0xcccc, 0xdddd};
    BitIOInfo io = {0};
    io.pbCurrent = (U8 *)slots;
    io.iMask = -1;
    io.uiAccumulator = 0xdeadbeefu;
    assert(peekBit16(&io, 0) == 0);
    putBit16(&io, 0xffffffffu, 0);
    assert(io.uiAccumulator == 0xdeadbeefu && io.cBitsUsed == 0);
    assert(io.pbCurrent == (U8 *)slots && slots[0] == 0xaaaa);
    putBit16(&io, 0xffff1234u, 16);
    assert((io.uiAccumulator & 0xffffu) == 0x1234u);
    assert(io.cBitsUsed == 0 && io.pbCurrent == (U8 *)(slots + 1));
}

static void valid_roundtrip(void)
{
    enum { W = 16, H = 16 };
    PKFactory *factory = NULL;
    PKImageEncode *encoder = NULL;
    struct WMPStream *stream = NULL;
    CWMIStrCodecParam params = {0};
    U8 pixels[W * H * 3];
    memset(pixels, 128, sizeof(pixels));
    params.cfColorFormat = YUV_444;
    params.bdBitDepth = BD_LONG;
    params.bfBitstreamFormat = FREQUENCY;
    params.bProgressiveMode = TRUE;
    params.olOverlap = OL_ONE;
    params.sbSubband = SB_ALL;
    params.uiDefaultQPIndex = 1;
    params.uiDefaultQPIndexAlpha = 1;
    assert(PKCreateFactory(&factory, PK_SDK_VERSION) == WMP_errSuccess);
    assert(factory->CreateStreamFromFilename(&stream, "valid.jxr", "wb") == WMP_errSuccess);
    assert(PKImageEncode_Create_WMP(&encoder) == WMP_errSuccess);
    assert(encoder->Initialize(encoder, stream, &params, sizeof(params)) == WMP_errSuccess);
    assert(encoder->SetPixelFormat(encoder, GUID_PKPixelFormat24bppRGB) == WMP_errSuccess);
    assert(encoder->SetSize(encoder, W, H) == WMP_errSuccess);
    assert(encoder->SetResolution(encoder, 96.0f, 96.0f) == WMP_errSuccess);
    assert(encoder->WritePixels(encoder, H, pixels, W * 3) == WMP_errSuccess);
    assert(encoder->Release(&encoder) == WMP_errSuccess);
    assert(factory->Release(&factory) == WMP_errSuccess);
    assert(encore_jxr_to_webp("valid.jxr", "valid.webp"));
    FILE *fp = fopen("valid.webp", "rb");
    assert(fp);
    U8 bytes[65536];
    size_t n = fread(bytes, 1, sizeof(bytes), fp);
    assert(fclose(fp) == 0);
    int w = 0, h = 0;
    U8 *rgb = WebPDecodeRGB(bytes, n, &w, &h);
    assert(rgb && w == W && h == H);
    for (int i = 0; i < W * H * 3; ++i)
        assert(rgb[i] >= 112 && rgb[i] <= 144);
    WebPFree(rgb);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "reject") == 0) {
        bit_width_boundaries();
        rejected_inputs();
    }
    else if (strcmp(argv[1], "valid") == 0)
        valid_roundtrip();
    else
        return 2;
    return 0;
}
"""
sources = [p for group in ("image/sys", "image/decode", "image/encode", "jxrgluelib")
           for p in sorted((JXR / group).glob("*.c"))]
assert len(sources) == 22
with tempfile.TemporaryDirectory(prefix="jxr-security-host-") as temp:
    work = Path(temp)
    code = work / "harness.c"
    code.write_text(harness)
    (work / "short.jxr").write_bytes(b"JXR")
    (work / "not-an-image.jxr").write_bytes(b"not an image\n" * 8)
    with (work / "oversized.jxr").open("wb") as stream:
        stream.truncate(48 * 1024 * 1024 + 1)
    (work / "link.jxr").symlink_to("not-an-image.jxr")
    includes = [ADAPTER, JXR, JXR / "common/include", JXR / "image/sys",
                JXR / "jxrgluelib", JXR / "image/decode", JXR / "image/encode"]
    exe = work / "jxr-security"
    command = ["clang", "-std=gnu11", "-O1", "-g", "-msse2",
               "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               "-D__ANSI__", "-DDISABLE_PERF_MEASUREMENT",
               "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
               "-Werror=shift-negative-value", "-Werror=extra-tokens",
               "-include", str(JXR / "jxr_compat.h")]
    command += ["-I" + str(path) for path in includes]
    command += [str(path) for path in sources + [ADAPTER / "jxr_decoder.c", code]]
    command += ["-o", str(exe), "-lwebp", "-lm"]
    subprocess.run(command, check=True, cwd=work, timeout=300)
    subprocess.run([str(exe), "reject"], check=True, cwd=work, timeout=30)
    print("JXR_SECURITY_REJECT_HOST_PASS real_codec=1 overflow_guard=1", flush=True)
    subprocess.run([str(exe), "valid"], check=True, cwd=work, timeout=30)
    print("JXR_VALID_SDR_ROUNDTRIP_HOST_PASS 16x16 pixels=verified", flush=True)
