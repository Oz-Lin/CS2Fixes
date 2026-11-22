#include "utils/map_countdown.h"

#include <cassert>
#include <iostream>
#include <limits>

using namespace ozlin::map_countdown;

int main()
{
	assert(ParseRoute("legacy") == Route::Legacy);
	assert(ParseRoute("native") == Route::Native);
	for (const auto mode : {"none", "", "Legacy", "native legacy", "unknown"})
		assert(ParseRoute(mode) == Route::None);

	assert(SelectAudience(false, -1, false) == Audience::Broadcast);
	assert(SelectAudience(false, 1, true) == Audience::Mask);
	assert(SelectAudience(false, 64, true) == Audience::Mask);
	assert(SelectAudience(true, -1, false) == Audience::Unknown);
	assert(SelectAudience(false, -1, true) == Audience::Unknown);
	assert(SelectAudience(false, 0, true) == Audience::Unknown);
	assert(SelectAudience(false, 1, false) == Audience::Unknown);

	State timer;
	assert(!timer.Start(10.0, 15, "before observed round"));
	timer.OnRoundStart();
	assert(!timer.Start(10.0, 4, "too short"));
	assert(!timer.Start(10.0, 86401, "too long"));
	assert(!timer.Start(std::numeric_limits<double>::quiet_NaN(), 15, "invalid clock"));
	assert(!timer.Start(10.0, 15, ""));
	assert(!timer.Start(10.0, 15, "   "));
	assert(timer.Start(10.0, 15, "守住门口・ゲート <A> & \"B\" 'C'"));
	assert(timer.Frame(10.0) ==
		   "<span color='#FFD700'>守住门口・ゲート &lt;A&gt; &amp; &quot;B&quot; &#39;C&#39;</span><br>"
		   "<span color='#FFFFFF'>[15 s]</span>");
	assert(timer.Frame(10.25).empty());
	assert(!timer.Start(10.3, 15, "守住门口・ゲート <A> & \"B\" 'C'")); // does not extend deadline
	assert(timer.Frame(11.0).find("[14 s]") != std::string::npos);
	assert(timer.Frame(10.9).empty()); // no higher count from a backward clock
	assert(timer.Frame(24.0).find("[1 s]") != std::string::npos); // no packet flood after hitch
	assert(timer.Frame(25.0).empty());
	assert(!timer.IsRunning(25.0));
	assert(timer.Start(25.1, 15, "守住门口・ゲート <A> & \"B\" 'C'")); // legitimate repeat after expiry
	assert(timer.Start(25.2, 5, "replacement; % \\"));
	assert(timer.Frame(25.2).find("replacement; % \\") != std::string::npos);
	timer.Stop(); // config changed while a round is active
	assert(timer.Frame(26.2).empty());
	assert(timer.Start(26.2, 5, "allowed after stop"));
	timer.OnRoundEnd();
	assert(timer.Frame(26.3).empty());
	assert(!timer.Start(26.3, 5, "closed round"));
	timer.OnRoundStart();
	assert(timer.Start(30.1, 5, "fresh round"));
	assert(timer.Frame(30.1).find("[5 s]") != std::string::npos);
	assert(timer.Frame(30.0).empty());
	timer.OnRoundStart(); // another start cannot inherit a previous timer
	assert(timer.Frame(31.0).empty());

	assert(EscapeText(std::string(220, 'a')).size() == 220);
	assert(EscapeText(std::string(221, 'a')).empty());
	assert(EscapeText(std::string(120, '"')).size() == 720);
	assert(EscapeText(std::string(121, '"')).empty());
	for (const auto invalid : {std::string_view("A\0B", 3), std::string_view("\x7f"), std::string_view("A\nB"),
							  std::string_view("\xc0\xaf"), std::string_view("\xe3\x81"), std::string_view("\xed\xa0\x80"),
							  std::string_view("\xf4\x90\x80\x80"), std::string_view("\xc2\x85"), std::string_view("\xe2\x80\xae")})
		assert(EscapeText(invalid).empty());
	assert(!EscapeText("👾 Ω 歐林 Защита العربية").empty());
	PopupGuard popup;
	popup.Reserve(19, 50.0, 12); // 10 s hsay + 2 s leeway
	assert(popup.Blocks(19, 61.99));
	popup.Reserve(19, 52.0, 1); // a brief later frame must not shorten the existing popup
	assert(popup.Blocks(19, 61.99));
	assert(!popup.Blocks(19, 62.0));
	assert(!popup.Blocks(20, 52.0)); // player slot reused
	popup.Reserve(20, 53.0, 1);
	assert(!popup.Blocks(19, 53.5));
	assert(popup.Blocks(20, 53.5));
	assert(!popup.Blocks(20, 54.0));
	assert(kHudFrameSeconds == 1);
	std::cout << "Native CountDown policy tests passed\n";
}
