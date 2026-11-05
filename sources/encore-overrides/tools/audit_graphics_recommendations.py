#!/usr/bin/env python3
"""Audit per-game image-quality recommendations without rewriting baselines.

A newly available shader does not prove that it is faster, safer or better
for a particular game on PS5. Manual overrides always win.
"""
import collections
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
EXPECTED={"minimum","recommended","high","ultra"}
SPATIAL={"bilinear","fsr","bicubic","nearest","lanczos","sgsr","sgsr_edge"}
AA={"none","fxaa","smaa","fxaa_quality"}
VULKAN_ONLY_SPATIAL={"sgsr","sgsr_edge"}
VULKAN_ONLY_AA={"fxaa_quality"}

def audit():
    stats=collections.Counter()
    filters=collections.Counter()
    antialias=collections.Counter()
    curated=[]
    seen=set()
    paths=sorted((ROOT/"games").glob("*.json"))
    for path in paths:
        data=json.loads(path.read_text(encoding="utf-8"))
        title=data["title_id"]
        assert path.stem==title and len(title)==16 and int(title,16),path
        assert title not in seen, f"duplicate Title ID {title}"
        seen.add(title)
        status=data.get("status","unknown")
        stats[status]+=1
        if status!="auto-baseline":
            curated.append((title,data.get("name",""),status))
        assert set(data.get("profiles",{}))==EXPECTED,(path,"profiles")
        for tier,entry in data["profiles"].items():
            settings=entry["settings"]
            filt=settings.get("upscaling_filter")
            aa=settings.get("anti_aliasing")
            renderer=settings.get("renderer")
            assert renderer in ("vulkan","opengl"),(title,tier,renderer)
            assert filt in SPATIAL and aa in AA,(title,tier,filt,aa)
            if renderer=="opengl":
                assert filt not in VULKAN_ONLY_SPATIAL,(title,tier,filt)
                assert aa not in VULKAN_ONLY_AA,(title,tier,aa)
            if status=="auto-baseline":
                if tier=="recommended":
                    assert filt=="fsr", (title,tier,"old Bilinear baseline must not regress")
                # Generated profiles without game measurements cannot opt
                # all titles into previously untested shaders.
                assert filt not in {"sgsr","sgsr_edge"},(title,tier,filt)
                assert aa!="fxaa_quality",(title,tier,aa)
            filters[(tier,filt)]+=1
            antialias[(tier,aa)]+=1
    print("GRAPHICS_RECOMMENDATIONS_AUDIT_PASS")
    print("games",len(paths),"statuses",dict(stats))
    print("non-auto-baseline",curated[:15])
    for tier in ("minimum","recommended","high","ultra"):
        print(tier,"upscale",dict(sorted((k[1],v) for k,v in filters.items() if k[0]==tier)),
              "AA",dict(sorted((k[1],v) for k,v in antialias.items() if k[0]==tier)))
    return stats,curated

if __name__=="__main__":
    audit()
