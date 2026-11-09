// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace rampbugfix
{
struct HookPairResult
{
	bool accepted = false;
	bool rolledBack = false;
};

template <typename FirstHook, typename SecondHook>
HookPairResult RegisterHookPair(FirstHook &first, void *firstAddress, SecondHook &second, void *secondAddress)
{
	const bool firstAccepted = first.Register(firstAddress);
	const bool secondAccepted = firstAccepted && second.Register(secondAddress);
	if (firstAccepted && secondAccepted)
		return {true, false};
	const bool firstRemoved = first.RemoveAll();
	const bool secondRemoved = second.RemoveAll();
	return {false, firstRemoved && secondRemoved};
}

class AdapterLifecycle
{
public:
	enum class Phase
	{
		Unavailable,
		Observing,
		Active,
		Stopping
	};

	void Reset() { *this = {}; }
	void RegistrationAccepted()
	{
		m_phase = Phase::Observing;
		ResetObservations();
	}
	void RegistrationFailed()
	{
		m_phase = Phase::Unavailable;
		ResetObservations();
	}
	void ObserveTryPlayerMove(bool enabled)
	{
		if (m_phase == Phase::Observing && enabled)
			m_seenTryPlayerMove = true;
	}
	void ObserveCategorizePosition(bool enabled)
	{
		if (m_phase == Phase::Observing && enabled)
			m_seenCategorizePosition = true;
	}
	bool OnFrame(bool enabled, bool movementIdle)
	{
		if (m_phase == Phase::Stopping || m_phase == Phase::Unavailable)
			return false;
		if (!enabled)
		{
			m_phase = Phase::Observing;
			ResetObservations();
			return false;
		}
		if (m_phase == Phase::Observing && movementIdle && m_seenTryPlayerMove && m_seenCategorizePosition)
		{
			m_phase = Phase::Active;
			return true;
		}
		return false;
	}
	void ToggleChanged()
	{
		if (m_phase == Phase::Observing || m_phase == Phase::Active)
			m_phase = Phase::Observing;
		ResetObservations();
	}
	void Stop()
	{
		m_phase = Phase::Stopping;
		ResetObservations();
	}
	bool CanCorrect(bool enabled) const { return m_phase == Phase::Active && enabled; }
	bool IsPrepared() const { return m_phase == Phase::Observing || m_phase == Phase::Active; }
	bool IsStopping() const { return m_phase == Phase::Stopping; }

private:
	void ResetObservations()
	{
		m_seenTryPlayerMove = false;
		m_seenCategorizePosition = false;
	}

	Phase m_phase = Phase::Unavailable;
	bool m_seenTryPlayerMove = false;
	bool m_seenCategorizePosition = false;
};
} // namespace rampbugfix
