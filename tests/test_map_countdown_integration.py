"""Source-contract tests; do not substitute for SteamRT3 or real-client smoke."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class NativeCountdownWiring(unittest.TestCase):
    def test_default_and_exclusive_route(self):
        cfg = read("cfg/cs2fixes.cfg")
        hooks = read("src/virtual_hooks.cpp")
        self.assertRegex(cfg, r'(?m)^cs2f_trigger_timer_enable\s+0\s')
        self.assertRegex(cfg, r'(?m)^cs2f_trigger_timer_hud_route\s+"legacy"\s')
        switch = hooks.split("// Choose one mutually exclusive HUD sink.", 1)[1].split("V_snprintf(buf", 1)[0]
        legacy = switch.split("Route::Legacy:", 1)[1].split("Route::Native:", 1)[0]
        native = switch.split("Route::Native:", 1)[1].split("Route::None:", 1)[0]
        neither = switch.split("Route::None:", 1)[1]
        self.assertEqual(legacy.count("ServerCommand(countdownCommand)"), 1)
        self.assertNotIn("StartNativeMapCountdown", legacy)
        self.assertIn("break;", legacy)
        self.assertEqual(native.count("StartNativeMapCountdown"), 1)
        self.assertNotIn("ServerCommand", native)
        self.assertIn("break;", native)
        self.assertNotRegex(neither, r"ServerCommand|StartNativeMapCountdown")

    def test_lifecycle_stops_and_input_contract(self):
        hooks = read("src/virtual_hooks.cpp")
        events = read("src/events.cpp")
        lifecycle = read("src/cs2fixes.cpp")
        self.assertIn('msg->playerindex() != -1 || V_strncmp(msg->text().c_str(), "Console: ", 9)', hooks)
        self.assertIn("CTimer::Create(0.0f, TIMERFLAG_MAP | TIMERFLAG_ROUND, [recipients = std::move(recipients)]", hooks)
        self.assertIn("g_cvarCountdownHudRoute.Get().String()) != ozlin::map_countdown::Route::Native", hooks)
        self.assertIn("if (audience == ozlin::map_countdown::Audience::Unknown)", hooks)
        self.assertIn("static_assert(ABSOLUTE_PLAYER_LIMIT <= std::numeric_limits<uint64>::digits)", hooks)
        for event, action in (("round_prestart", "End"), ("round_start", "Start"), ("round_end", "End")):
            self.assertIn(f"GAME_EVENT_F({event})\n{{\n\tMapCountdown_OnRound{action}();", events)
        self.assertEqual(lifecycle.count("MapCountdown_OnRoundEnd();\n\tResetMapCountdownHudFrames();"), 3)

    def test_native_never_takes_priority_or_allocates_empty(self):
        code = read("src/utils/hud_manager.cpp")
        priority = read("src/utils/hud_manager.h")
        create = code.split("void CreateHudMessage(", 1)[1].split("void SendHudMessage(", 1)[0]
        values = [int(re.search(rf"\b{name} = (\d+)", priority)[1])
                  for name in ("MapCountdown", "InfectionCountdown", "AdminHSay")]
        self.assertLess(values[0], values[1])
        self.assertLess(values[0], values[2])
        self.assertLess(create.index("if (bMapCountdownFrame && !bHasRecipient)"), create.index("CreateEvent("))
        self.assertLess(create.index("if (bMapCountdownFrame && !bHasRecipient)"), create.index("AllocateMessage("))
        self.assertRegex(create, r"if \(!bMapCountdownFrame\)\s+\{[^}]*g_vecHudMessages\.push_back")
        self.assertIn("g_countdownPopupGuards[slot.Get()].Blocks", create)
        self.assertIn("bMapCountdownFrame ? 0 : g_cvarHudDurationLeeway.Get()", create)


if __name__ == "__main__":
    unittest.main(verbosity=2)
