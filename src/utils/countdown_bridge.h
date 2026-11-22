// CS2Fixes (GPL-3.0). OZLIN compatibility bridge, one legacy CSS sink.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string_view>

namespace ozlin::countdown_bridge
{
constexpr std::size_t kCommandBytes = 256;
constexpr std::size_t kMaxTextBytes = 220;

// The engine's command parser is not a structured argument transport. Forward
// only unchanged map text that fits one quoted argument without any escaping.
inline bool Format(char (&out)[kCommandBytes], std::uint32_t seconds, std::string_view text)
{
	out[0] = '\0';
	if (seconds <= 4 || seconds > 86400 || text.empty() || text.size() > kMaxTextBytes ||
		text.find_first_not_of(' ') == std::string_view::npos)
		return false;
	for (const unsigned char c : text)
	{
		if (c < 32 || c == 127 || c == '"' || c == '\\' || c == ';')
			return false;
	}
	const int len = std::snprintf(out, sizeof(out), "css_countdown_text %u \"%.*s\"",
								  static_cast<unsigned>(seconds), static_cast<int>(text.size()), text.data());
	if (len <= 0 || static_cast<std::size_t>(len) >= sizeof(out))
	{
		out[0] = '\0';
		return false;
	}
	return true;
}
}
