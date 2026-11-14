#!/usr/bin/env python3
"""Selected JPEG XR -> bounded WebP adapter static/provenance host gate.

Real native C syntax is compiled by the paired CI using JXR+WebP headers.
This static contract is not a pixel decode or console HDR-quality check.
"""
from pathlib import Path
import hashlib
import re

ROOT=Path(__file__).resolve().parents[1]
CODEC=ROOT/"sources/jxrlib"
SOURCE=ROOT/"encore-active/ps5-jxr/jxr_decoder.c"
HEADER=ROOT/"encore-active/ps5-jxr/jxr_decoder.h"

def git_blob(path):
    raw=path.read_bytes()
    return hashlib.sha1(b"blob "+str(len(raw)).encode()+bytes([0])+raw).hexdigest()

entries=[p for p in CODEC.rglob("*") if p.is_file()]
assert len(entries)==42,("Incomplete original JXR upstream codec snapshot",len(entries))
assert (CODEC/"LICENSE").is_file()
assert "BSD" in (CODEC/"LICENSE").read_text()
assert git_blob(SOURCE)=="e83e13a3a6e66b7e63fb9510215c1dff2d42dd14"
assert git_blob(HEADER)=="d8bb62a86fa1226375ae09e70f39c58511a667f9"
src=SOURCE.read_text()
hdr=HEADER.read_text()
assert "bool encore_jxr_to_webp(const char *src, const char *dst)" in src
assert "bool encore_jxr_to_webp(const char *src, const char *dst);" in hdr
for code in (
    "lstat(src, &st) != 0", "S_ISREG(st.st_mode)", "st.st_size < 32",
    "st.st_size > 48 * 1024 * 1024",
    "decoder->GetSize(decoder, &width, &height) != WMP_errSuccess",
    "width <= 0 || height <= 0 || width > 3840 || height > 2160",
    "count > SIZE_MAX / (size_t)fi->bytes",
    "width > 1920 || height > 1080",
    "WebPEncodeRGB(output, target_w, target_h,",
    "length > (12u * 1024u * 1024u)", "WebPGetInfo(compressed, length",
    "O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW", "0600",
    "fclose(out) != 0", "if (!ok) remove(dst);",
):
    assert code in src, ("Selected-only JXR bound or hardening missing",code)
assert src.index("lstat(src, &st)") < src.index("PKCreateCodecFactory")
assert src.index("decoder->GetSize") < src.index("pixels = malloc")
assert src.index("WebPGetInfo") < src.index("open(dst")
assert "printf(\"%s\"" not in src
assert re.search(r"\b(O_TRUNC|opendir|readdir|scandir)\b",src) is None
print("JXR_SELECTED_SOURCE_CONTRACT_HOST_PASS files=42 bounds=48MiB "
      "dimensions=3840x2160 output=1920x1080 webp=12MiB "
      "symlinks=blocked exclusive_temp=1 decode_per_selection=1 "
      "native_ps5_and_pixel_output=unverified")
