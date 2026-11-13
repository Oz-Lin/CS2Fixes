"""Historical source-contract checks for the completed v2.1 sync checkpoint."""

from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
BASE = "687f51acb2b717218f6e1e42710f14cfd58a1960"
FORK = "34aaed13c560204dd4c2131c4c748c032cd28ce5"
SDK = "2283c1f919cb7b75329eb059ef6b064b36c2fdaa"
CHECKPOINT = "85c2ab558a75c61401dcc0def15fa3245ef34ef1"
BRIDGE = '''\t\t// parse to external CS# countdown timer plugin
\t\tV_snprintf(buf, sizeof(buf), "css_countdown_text %d \\"%s\\"", uiTriggerTimerLength, pText + sizeof("Console:"));
\t\tg_pEngineServer2->ServerCommand(buf);

\t\t// now back to the Red Console + Green Say Text
'''


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True, encoding="utf-8")


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def checkpoint_read(path):
    return git("show", CHECKPOINT + ":" + path)


class HistoricalSyncCheckpoint(unittest.TestCase):
    def test_public_release_identity(self):
        self.assertEqual(git("rev-parse", "v2.1^{commit}").strip(), BASE)

    def test_fork_history_retained(self):
        subprocess.run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", FORK, CHECKPOINT], check=True)

    def test_checkpoint_identity(self):
        self.assertEqual(git("rev-parse", CHECKPOINT + "^{commit}").strip(), CHECKPOINT)

    def test_no_unmerged_index(self):
        self.assertEqual(git("ls-files", "--unmerged"), "")

    def test_no_source_conflict_markers(self):
        for path in (ROOT / "src").rglob("*"):
            if path.suffix in {".cpp", ".h"}:
                self.assertIsNone(re.search(r"^(<<<<<<<|=======|>>>>>>>)", path.read_text(encoding="utf-8-sig"), re.M), str(path))

    def test_countdown_bridge_is_exactly_preserved(self):
        self.assertEqual(read("src/virtual_hooks.cpp").count(BRIDGE), 1)
        source = checkpoint_read("src/detours.cpp")
        self.assertEqual(source.count(BRIDGE), 1)
        without_bridge = source.replace(BRIDGE, "").replace('#include "engine2/ivengineserver2.h"\n', "")
        self.assertEqual(without_bridge, git("show", BASE + ":src/detours.cpp"))

    def test_bridge_still_uses_existing_timer_gate(self):
        source = read("src/virtual_hooks.cpp")
        self.assertLess(source.index("if ((uiTriggerTimerLength > 4)"), source.index("css_countdown_text"))
        self.assertIn("g_cvarEnableTriggerTimer.Get() && GetGlobals() && g_pGameRules", source)

    def test_no_stale_clantag_duplicate(self):
        source = read("src/cs2_sdk/entity/ccsplayercontroller.h")
        self.assertEqual(source.count("void SetClanTag("), 1)
        self.assertIn("void ResetClanTag()", source)
        self.assertNotIn("name swap trick", source)
        self.assertNotIn("name swap trick", read("src/playermanager.cpp"))

    def test_upstream_hook_migration_retained(self):
        self.assertNotIn("SH_DECL_HOOK", read("src/cs2fixes.cpp"))
        self.assertIn("KHOOK_VIRTUAL(ApplyGameSettings", read("src/virtual_hooks.cpp"))
        self.assertFalse((ROOT / "src/cdetour.h").exists())

    def test_default_external_features_stay_disabled(self):
        cfg = read("cfg/cs2fixes.cfg")
        for name in ("cs2f_weapons_enable", "cs2f_votemanager_enable"):
            self.assertRegex(cfg, r"(?m)^" + name + r"\s+0\s")

    def test_upstream_files_match_except_bridge(self):
        paths = git("diff", "--name-only", BASE, CHECKPOINT, "--", "src", "cfg", "gamedata", "AMBuilder", "AMBuildScript", "CS2Fixes.vcxproj", "CS2Fixes.vcxproj.filters", "Dockerfile", ".github").splitlines()
        self.assertEqual(paths, ["src/detours.cpp"])

    def test_sdk_pin_and_checkout(self):
        self.assertIn(SDK, git("ls-files", "--stage", "sdk"))
        actual = subprocess.check_output(["git", "-C", str(ROOT / "sdk"), "rev-parse", "HEAD"], text=True).strip()
        self.assertEqual(actual, SDK)

    def test_current_message_abi_and_bridge_location(self):
        source = read("src/virtual_hooks.cpp")
        self.assertIn("NetMessageInfo_t* pEvent", source)
        self.assertNotIn("GetNetMessageInfo()", source)
        self.assertNotIn("INetworkMessageInternal", source)
        self.assertIn('msg->playerindex() != -1', source)
        self.assertNotIn("css_countdown_text", read("src/detours.cpp"))
        self.assertNotIn("Detour_UTIL_SayTextFilter", read("src/detours.cpp"))

    def test_ramp_integration_preserved(self):
        for path in ["src/rampbugfix/core.cpp", "src/rampbugfix/module.h"]:
            self.assertEqual(read(path), git("show", "561f8b7:" + path))
        module = read("src/rampbugfix/module.cpp")
        self.assertIn("0xde50ca3604b5443f", module)
        self.assertIn("0x96c2563722232d09", module)
        self.assertIn("size == profile.fileSize && hash == profile.fileHash", module)
        self.assertIn("rampbugfix::ResetAll();", read("src/virtual_hooks.cpp"))
        self.assertIn("rampbugfix::", read("src/detours.cpp"))

    def test_public_baseline_delta_has_no_whitespace_errors(self):
        subprocess.run(["git", "-C", str(ROOT), "diff", "--check", BASE, CHECKPOINT], check=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
