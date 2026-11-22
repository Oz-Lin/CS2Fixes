# OZLIN native map CountDown (opt-in)

This route renders the existing CS2Fixes map Console timer without a SW2/CSS receiver. It does not implement `css_say` or re-render player chat. A provider is selected once per eligible message; failed native rendering does not call the legacy command and vice versa. The original annotated chat message still goes out once.

| Setting | Meaning |
|---|---|
| `cs2f_trigger_timer_enable 0` (default) | No countdown parsing or HUD dispatch. |
| Timer enabled, `cs2f_trigger_timer_hud_route "legacy"` (default route) | Exactly the previous single external `css_countdown_text` path. Only select if one verified external CountDown receiver owns the name. |
| Timer enabled, route `"native"` | CS2Fixes sends one native HUD series; never dispatches the external command. |
| Timer enabled, route `"none"` or an invalid route | Original chat annotation only; no HUD dispatch. |

Use a per-instance opt-in after the defaults load, not a shared config change. Do not switch producers mid-round: disabling a route stops new native frames, but an already displayed frame takes its remaining one-second duration to clear and an external renderer may have its own lifetime.

### Native constraints

- The existing parser still recognizes bare numeric or English-form seconds/minutes, >4 seconds and ending before the round clock. For example, `守住门口 15 seconds` preserves the full Chinese text; `守住门口 15秒` alone is not promised to trigger. 1–4 seconds do not acquire a special HUD. Native preserves valid supplied UTF-8 rather than translating it.
- Native requires the engine primary game thread, a non-warmup active round observed through `round_start`, and either the exact SDK broadcast sentinel or a known mask including all connected humans. A targeted/local-only/unknown input does not start a timer. It retains only original connected human slot+serial handles, so disconnected players, replacement slot occupants, bots, HLTV and late joiners do not receive the retained text.
- A new valid, different input replaces the active timer. Identical text+duration during an active timer does not extend it. Durations count down from a game-time deadline, at most once/second, no catch-up packet burst, no 0 frame. New round, round end/prestart, level init/shutdown and successful unload cancel. An already emitted frame naturally expires; no blank/clear event is sent that could erase another HUD producer.
- Presentation: gold escaped map text and white numeric seconds in the first-party `show_survival_respawn_status` HTML event. Lowest internal priority, no priority reservation and one-second frame without global leeway. Existing infection and admin `hsay` retain their priority and full requested display durations (including existing leeway). Other plugins' client panels may share the same UI surface outside this arbitration; no claims about their co-existence without tests. No global flashing workaround or warmup/EntWatch change.
- Source/text/channel guards fail closed. If the actual engine routes a map trigger as a targeted/secondary-thread notification, do not remove gates on production; inspect the runtime result first.

### SteamRT3 pilot acceptance only after scoped authorization

1. Build the exact candidate in the repo's supported SteamRT3 Docker/CI path. Verify candidate SHA, package checksum and Metamod/load status. Confirm existing SW2/CSS ownership; preserve EntWatch/RampBugFix and normal game config.
2. First with defaults off verify load and existing HUD. Then use per-instance opt-in and a real 28056 game client on a copied/test configuration: known map trigger `Hold for 15 seconds` and UTF-8 text with an English-form duration. Verify one dynamic gold+white HUD, one original chat annotation, no `css_countdown_text` dispatch, no second presenter, no player chat/short/targeted activation. Console input is not a substitute for the real map-output path.
3. Smoke existing infection, `hsay`, other HUD and client typography. Verify round end, map transition, rejoin/replaced slot, restart and disabling the feature do not leak or replay a countdown. Check that one-second screen expiry works cleanly without changing the global HUD settings; stop if it flickers or interferes. Keep the legacy or disabled config and known binary for rollback.
