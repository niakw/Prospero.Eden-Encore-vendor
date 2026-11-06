#!/usr/bin/env python3
"""Offline regression tests for the native Switch 2 metadata/generator."""
import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("ns2sync",ROOT/"tools/sync_switch2_games.py")
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

HTML="""
<html><table class="wikitable">
<tr><th>Base ApplicationId</th><th>Description</th><th>Region</th><th>OS</th>
    <th>Distribution</th><th>Versions</th><th>Notes</th><th>Type</th></tr>
<tr><td>040074A01BF12000</td><td>Donkey Kong Bananza</td><td>EUR USA</td>
    <td></td><td>Digital</td><td>v1.0</td><td></td><td>Game</td></tr>
<tr><td>0400C3F00006E000</td><td>Mario Kart World</td><td>JPN USA</td>
    <td></td><td>Cartridge</td><td></td><td></td><td>Game</td></tr>
<tr><td>040074A01BF12800</td><td>Donkey Kong Update</td><td>USA</td>
    <td></td><td>Digital</td><td>v2</td><td></td><td>Game</td></tr>
<tr><td>01007EF00011E000</td><td>Zelda: Breath of the Wild –
Nintendo Switch 2 Edition</td><td>EUR</td>
    <td></td><td>Digital</td><td></td><td></td><td>Application / Game</td></tr>
<tr><td>04001A0011111000</td><td>Non-game System App</td><td>USA</td>
    <td></td><td>Digital</td><td></td><td></td><td>System</td></tr>
</table></html>
"""

class TestNs2Sync(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        root=Path(self.tmp.name)
        (root/"general/ns2").mkdir(parents=True)
        (root/"games").mkdir()
        for tier in mod.PROFILES:
            src=ROOT/"general/ns2"/(tier+".json")
            dst=root/"general/ns2"/(tier+".json")
            dst.write_bytes(src.read_bytes())
        (root/"manifest.json").write_text(json.dumps({
            "schema_version":1,"database_revision":10,"entries":[]}))
        self.root=root
        self.original=mod.ROOT
        mod.ROOT=root
        mod.GAMES_DIR=root/"games"
        mod.MANIFEST=root/"manifest.json"
        self.addCleanup(self.restore)

    def restore(self):
        mod.ROOT=self.original
        mod.GAMES_DIR=self.original/"games"
        mod.MANIFEST=self.original/"manifest.json"

    def test_whitelist_ns2_and_editions_do_not_overwrite(self):
        game,editions=mod.parse_switchbrew_games(HTML)
        self.assertEqual(set(game),{"040074A01BF12000","0400C3F00006E000"})
        self.assertEqual(editions["01007EF00011E000"].count("Edition"),1)
        self.assertNotIn("040074A01BF12800",game)
        count=mod.sync(write=True,limit=None,source_text=HTML)
        self.assertEqual(count,2)
        files=sorted((self.root/"games").glob("*.json"))
        self.assertEqual(len(files),2)
        data=json.loads(files[0].read_text())
        self.assertEqual(data["source_console"],"Nintendo Switch 2")
        self.assertEqual(data["technical_data"]["source_game"]["target_fps"],None)
        self.assertEqual(data["profiles"]["recommended"]["settings"]["upscaling_filter"],"fsr")
        self.assertTrue(data["technical_data"]["encore"]["manual_user_override_wins"])
        self.assertEqual(len(json.loads((self.root/"manifest.json").read_text())["entries"]),2)
        self.assertEqual(len(json.loads((self.root/"metadata/ns2_editions.json").read_text())["editions"]),1)

    def test_existing_never_overwritten(self):
        path=self.root/"games/040074A01BF12000.json"
        path.write_text('{"curated":true}')
        count=mod.sync(write=True,limit=None,source_text=HTML)
        self.assertEqual(count,1)
        self.assertEqual(path.read_text(),'{"curated":true}')
        self.assertEqual(mod.sync(write=True,limit=None,source_text=HTML),0)

    def test_dry_run_never_changes_files(self):
        before=(self.root/"manifest.json").read_bytes()
        self.assertEqual(mod.sync(write=False,limit=1,source_text=HTML),1)
        self.assertEqual(before,(self.root/"manifest.json").read_bytes())
        self.assertEqual(list((self.root/"games").iterdir()),[])

    def test_incomplete_source_fails_closed(self):
        with self.assertRaises(ValueError):
            mod.sync(write=True,limit=None,source_text="<html></html>")
        self.assertEqual(list((self.root/"games").iterdir()),[])

    def test_conflicting_game_ids_are_skipped(self):
        extra=HTML.replace("</table>",
            "<tr><td>040074A01BF12000</td><td>OTHER TITLE</td>"
            "<td>USA</td><td></td><td>Digital</td><td></td>"
            "<td></td><td>Game</td></tr></table>")
        with self.assertRaises(ValueError):
            mod.sync(write=True,limit=None,source_text=extra)
        self.assertEqual(list((self.root/"games").iterdir()),[])

if __name__=="__main__":
    unittest.main(verbosity=2)
