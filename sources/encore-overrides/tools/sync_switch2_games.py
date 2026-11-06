#!/usr/bin/env python3
"""Incremental Nintendo Switch 2 game profile sync from Switchbrew's game list.

NS2 native game application IDs use the 0400... namespace. Switch 2 Editions
of NS1 titles often reuse 0100... IDs and must NOT overwrite NS1 profiles.
No game-specific FPS/performance is inferred from a public title listing.
New files ONLY; preserves curated and user changes. Standard library only.
"""
from __future__ import annotations

import argparse
import copy
import html
from html.parser import HTMLParser
import json
import re
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
GAMES_DIR = ROOT / "games"
MANIFEST = ROOT / "manifest.json"
PROFILES = ("minimum", "recommended", "high", "ultra")
SOURCE_URL = "https://www.switchbrew.org/wiki/Switch_2:_Title_list/Games"
NS2_ID = re.compile(r"^040[0-9A-F]{13}$")
EDITION_ID = re.compile(r"^010[0-9A-F]{13}$")

class WikiTables(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.tables: list[list[list[str]]] = []
        self.in_table = 0
        self.in_row = False
        self.in_cell = False
        self.buffer: list[str] = []
        self.row: list[str] = []
    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag == "table":
            if self.in_table == 0:
                self.tables.append([])
            self.in_table += 1
        elif self.in_table and tag == "tr":
            self.row, self.in_row = [], True
        elif self.in_row and tag in ("td", "th"):
            self.buffer, self.in_cell = [], True
        elif self.in_cell and tag == "br":
            self.buffer.append(" ")
    def handle_data(self, text: str) -> None:
        if self.in_cell:
            self.buffer.append(text)
    def handle_endtag(self, tag: str) -> None:
        if self.in_cell and tag in ("td", "th"):
            self.row.append(" ".join("".join(self.buffer).split()))
            self.in_cell = False
        elif self.in_row and tag == "tr":
            if self.row and self.tables:
                self.tables[-1].append(self.row)
            self.in_row = False
        elif self.in_table and tag == "table":
            self.in_table -= 1

def parse_switchbrew_games(source: str) -> tuple[dict[str, dict[str, Any]], dict[str,str]]:
    parsed=WikiTables()
    parsed.feed(source)
    games: dict[str,dict[str,Any]] = {}
    editions: dict[str,str] = {}
    conflicted: set[str] = set()
    for table in parsed.tables:
        for row in table:
            if len(row) < 8: continue
            tid=row[0].strip().upper()
            name=html.unescape(row[1].strip())
            kind=row[-1].strip().casefold()
            if not name or len(name)>200 or "game" not in kind:
                continue
            if NS2_ID.fullmatch(tid) and tid.endswith("000"):
                item={
                    "name":name,
                    "regions":sorted({x for x in row[2].split() if x in {
                        "CHN","EUR","JPN","KOR","USA","AUS"}}),
                    "versions_text":row[5][:200],
                }
                if tid in conflicted:
                    continue
                if tid in games and games[tid]["name"]!=name:
                    # Wiki may repeat a shared application ID with different
                    # regional names. Never pick a random game's profile.
                    games.pop(tid)
                    conflicted.add(tid)
                    continue
                games[tid]=item
            elif EDITION_ID.fullmatch(tid) and tid.endswith("000") and (
                "switch" in name.casefold() and "2 edition" in name.casefold()
            ):
                editions[tid]=name
    if not games or "040074A01BF12000" not in games or (
        "0400C3F00006E000" not in games
    ):
        raise ValueError("Switchbrew NS2 games list looks incomplete; refusing sync")
    return games, editions

def fetch_source() -> str:
    req=urllib.request.Request(
        SOURCE_URL, headers={"User-Agent":"Encore-Overrides-NS2-Sync/1.0 (+https://github.com/niakw/encore-overrides)",
                             "Accept":"text/html"})
    with urllib.request.urlopen(req,timeout=25) as response:
        data=response.read(2_000_001)
    if len(data)>2_000_000:
        raise ValueError("NS2 catalog unexpectedly large")
    return data.decode("utf-8","replace")

def load_profiles() -> dict[str,dict[str,Any]]:
    profiles={}
    for tier in PROFILES:
        relative=f"general/ns2/{tier}.json"
        data=json.loads((ROOT/relative).read_text(encoding="utf-8"))
        assert data["id"]==tier and data["source_platform"]=="Nintendo Switch 2"
        profiles[tier]={
            "name":data["name"],"profile_source":relative,
            "settings":copy.deepcopy(data["settings"]),
        }
    return profiles

def build_game(tid:str,meta:dict[str,Any],profiles:dict[str,dict[str,Any]]) -> dict[str,Any]:
    return {
        "schema_version":1, "title_id":tid, "name":meta["name"],
        "source_console":"Nintendo Switch 2",
        "target_hardware":"PS5 standard",
        "status":"auto-baseline", "auto_apply":True,
        "default_profile":"recommended", "profiles":copy.deepcopy(profiles),
        "technical_data":{
            "catalog":{"source":"Nintendo Switch Brew Switch 2 Title list/Games",
                       "url":SOURCE_URL,"regions":meta["regions"],
                       "versions_text":meta["versions_text"]},
            "source_game":{"docked_resolution":None,"handheld_resolution":None,
                           "target_fps":None,"confidence":"unknown",
                           "rule":"Do not infer render resolution or FPS from title catalog."},
            "switch_2":{"platform":"Nintendo Switch 2",
                        "game_namespace":"0400","gpu_family":"Ampere-class"},
            "ps5_standard":{"platform":"PS5","gpu_family":"RDNA2-class"},
            "encore":{"profile_model":list(PROFILES),
                      "profile_origin":"NS2 conservative generic baseline",
                      "manual_user_override_wins":True},
        },
        "generated":{"generator":"tools/sync_switch2_games.py",
                     "policy":"new-title-only; no edits to existing profiles"}
    }

def sync(*, write:bool, limit:int|None, source_text:str|None=None) -> int:
    found,editions=parse_switchbrew_games(source_text if source_text is not None
                                          else fetch_source())
    profiles=load_profiles()
    manifest=json.loads(MANIFEST.read_text(encoding="utf-8"))
    entries=manifest.setdefault("entries",[])
    known={str(e.get("title_id","")).upper() for e in entries}
    new=[tid for tid in sorted(found) if not (GAMES_DIR/f"{tid}.json").exists()]
    if limit is not None:new=new[:limit]
    print(f"Switch2 catalog={len(found)} NS2 editions={len(editions)} "
          f"new={len(new)} dry_run={not write}")
    if not write:
        for tid in new[:25]: print(tid,found[tid]["name"])
        return len(new)
    created=[]
    GAMES_DIR.mkdir(exist_ok=True,parents=True)
    for tid in new:
        dst=GAMES_DIR/f"{tid}.json"
        game=build_game(tid,found[tid],profiles)
        # Exclusive-create: never overwrite a curated, auto, or manual profile.
        try:
            with dst.open("x",encoding="utf-8") as out:
                json.dump(game,out,ensure_ascii=False,indent=2)
                out.write("\n")
        except FileExistsError:
            continue
        created.append(tid)
        if tid not in known:
            entries.append({"title_id":tid,"name":game["name"],
                            "path":f"games/{tid}.json",
                            "status":"auto-baseline","auto_apply":True,
                            "default_profile":"recommended",
                            "profiles":list(PROFILES)})
            known.add(tid)
    if created:
        entries.sort(key=lambda item:str(item.get("title_id","")))
        manifest["database_revision"]=int(manifest.get("database_revision",0))+1
        MANIFEST.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+"\n",
                            encoding="utf-8")
    # Editions are metadata only: 0100 belongs to the NS1 title namespace.
    # They cannot be auto-applied without knowing the precise guest variant.
    edition_file=ROOT/"metadata"/"ns2_editions.json"
    if write and editions:
        edition_file.parent.mkdir(exist_ok=True)
        edition_file.write_text(json.dumps({
            "schema":1,"source":SOURCE_URL,"auto_apply":False,
            "reason":"0100 title IDs may also identify NS1 games; never overwrite.",
            "editions":[{"title_id":tid,"name":name}
                        for tid,name in sorted(editions.items())]
        },ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("created",len(created))
    return len(created)

def main()->int:
    ap=argparse.ArgumentParser()
    ap.add_argument("--write",action="store_true")
    ap.add_argument("--limit",type=int,default=None)
    ap.add_argument("--source-html",type=Path,default=None,
                    help="Offline checked catalog fixture, no network")
    args=ap.parse_args()
    if args.limit is not None and args.limit < 1:
        ap.error("limit must be positive")
    source=args.source_html.read_text() if args.source_html else None
    sync(write=args.write,limit=args.limit,source_text=source)
    return 0

if __name__=="__main__":
    raise SystemExit(main())
