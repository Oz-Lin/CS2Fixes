// CS2Fixes (GPL-3.0). Engine-independent state and rendering for one OZLIN map countdown.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace ozlin::map_countdown
{
enum class Route { None, Legacy, Native };
enum class Audience { Unknown, Broadcast, Mask };

inline Audience SelectAudience(bool localOnly, int clientCount, bool hasMask)
{
	if (localOnly)
		return Audience::Unknown;
	if (clientCount == -1 && !hasMask)
		return Audience::Broadcast; // IGameEventSystem::PostEventAbstract broadcast sentinel.
	if (clientCount > 0 && hasMask)
		return Audience::Mask;
	return Audience::Unknown;
}

inline Route ParseRoute(std::string_view value)
{
	if (value == "legacy")
		return Route::Legacy;
	if (value == "native")
		return Route::Native;
	return Route::None;
}

inline constexpr std::size_t kMaxTextBytes = 220;
inline constexpr std::size_t kMaxEscapedBytes = 720;
inline constexpr int kHudFrameSeconds = 1;

inline bool ValidUtf8Text(std::string_view text)
{
	if (text.empty() || text.size() > kMaxTextBytes || text.find_first_not_of(' ') == std::string_view::npos)
		return false;
	for (std::size_t i = 0; i < text.size();)
	{
		const auto lead = static_cast<unsigned char>(text[i++]);
		if (lead < 0x80)
		{
			if (lead < 0x20 || lead == 0x7f)
				return false;
			continue;
		}
		std::size_t more;
		std::uint32_t point;
		std::uint32_t minimum;
		if (lead >= 0xc2 && lead <= 0xdf)
			more = 1, point = lead & 0x1f, minimum = 0x80;
		else if (lead >= 0xe0 && lead <= 0xef)
			more = 2, point = lead & 0xf, minimum = 0x800;
		else if (lead >= 0xf0 && lead <= 0xf4)
			more = 3, point = lead & 0x7, minimum = 0x10000;
		else
			return false;
		if (more > text.size() - i)
			return false;
		for (; more; --more)
		{
			const auto next = static_cast<unsigned char>(text[i++]);
			if ((next & 0xc0) != 0x80)
				return false;
			point = (point << 6) | (next & 0x3f);
		}
		if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff) ||
			(point >= 0x80 && point <= 0x9f) || point == 0x2028 || point == 0x2029 ||
			(point >= 0x202a && point <= 0x202e) || (point >= 0x2066 && point <= 0x2069))
			return false;
	}
	return true;
}

inline std::string EscapeText(std::string_view text)
{
	if (!ValidUtf8Text(text))
		return {};
	std::string escaped;
	for (const char c : text)
	{
		switch (c)
		{
			case '&': escaped += "&amp;"; break;
			case '<': escaped += "&lt;"; break;
			case '>': escaped += "&gt;"; break;
			case '"': escaped += "&quot;"; break;
			case '\'': escaped += "&#39;"; break;
			default: escaped += c; break;
		}
		if (escaped.size() > kMaxEscapedBytes)
			return {};
	}
	return escaped;
}

// Preserve the full visible lifetime of an existing, higher-priority CS2Fixes popup,
// not just the shorter arbitration lifetime of its ordinary HUD message.
class PopupGuard
{
public:
	void Reserve(std::uint32_t identity, double now, int seconds)
	{
		if (!std::isfinite(now) || seconds <= 0)
			return;
		const double until = now + seconds;
		if (identity != m_identity || m_until < until)
		{
			m_identity = identity;
			m_until = until;
		}
	}

	bool Blocks(std::uint32_t identity, double now) const
	{
		return identity == m_identity && now < m_until;
	}

private:
	std::uint32_t m_identity{0};
	double m_until{0.0};
};

class State
{
public:
	void OnRoundStart()
	{
		Stop();
		m_roundActive = true;
	}

	void OnRoundEnd()
	{
		m_roundActive = false;
		Stop();
	}

	void Stop()
	{
		m_seconds = 0;
		m_deadline = 0.0;
		m_lastSeconds = 0;
		m_text.clear();
	}

	bool Start(double now, std::uint32_t seconds, std::string_view text)
	{
		if (!m_roundActive || !std::isfinite(now) || seconds <= 4 || seconds > 86400)
			return false;
		auto escaped = EscapeText(text);
		if (escaped.empty())
			return false;
		// Identical engine output during one active timer cannot restart it.
		if (now < m_deadline && seconds == m_seconds && escaped == m_text)
			return false;
		m_text = std::move(escaped);
		m_seconds = seconds;
		m_deadline = now + seconds;
		m_lastSeconds = 0;
		return true;
	}

	bool IsRunning(double now) const
	{
		return m_roundActive && m_seconds && std::isfinite(now) && now < m_deadline;
	}

	std::string Frame(double now)
	{
		if (!IsRunning(now))
			return {};
		const auto remaining = m_deadline - now;
		// A backward engine clock must not cause larger/overflowing frames.
		if (remaining > m_seconds + 0.000001)
			return {};
		const int seconds = static_cast<int>(std::ceil(remaining - 0.000001));
		if (seconds <= 0 || (m_lastSeconds > 0 && seconds >= m_lastSeconds))
			return {};
		m_lastSeconds = seconds;
		// Use the same span-color syntax as the first-party infection countdown.
		return "<span color='#FFD700'>" + m_text + "</span><br><span color='#FFFFFF'>[" +
			   std::to_string(seconds) + " s]</span>";
	}

private:
	bool m_roundActive{false};
	std::uint32_t m_seconds{0};
	double m_deadline{0.0};
	int m_lastSeconds{0};
	std::string m_text;
};
} // namespace ozlin::map_countdown
