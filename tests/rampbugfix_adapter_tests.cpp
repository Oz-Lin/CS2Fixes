/**
 * CS2Fixes - Copyright (C) 2023-2024 Source2ZE
 * GPL version 3.0; see LICENSE. No warranty of merchantability or fitness.
 * Deterministic tests for the RampBugFix adapter lifecycle. These do not load
 * CS2, Metamod or KHook and do not validate runtime detours.
 */

#include "rampbugfix/lifecycle.h"

#include <iostream>
#include <stdexcept>

using rampbugfix::AdapterLifecycle;
using rampbugfix::RegisterHookPair;

namespace
{
void Require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void TestNeedsBothCallbacksAndFrameBoundary()
{
	AdapterLifecycle lifecycle;
	lifecycle.RegistrationAccepted();
	lifecycle.ObserveTryPlayerMove(true);
	Require(!lifecycle.OnFrame(true, true), "one callback armed corrections");
	lifecycle.ObserveCategorizePosition(true);
	Require(!lifecycle.OnFrame(true, false), "active movement armed corrections");
	Require(lifecycle.OnFrame(true, true), "safe frame failed to arm corrections");
	Require(lifecycle.CanCorrect(true), "active lifecycle rejected corrections");
}

void TestDisabledObservationsCannotArm()
{
	AdapterLifecycle lifecycle;
	lifecycle.RegistrationAccepted();
	lifecycle.ObserveTryPlayerMove(false);
	lifecycle.ObserveCategorizePosition(false);
	Require(!lifecycle.OnFrame(true, true), "disabled observations armed corrections");
}

void TestToggleRequiresFreshObservations()
{
	AdapterLifecycle lifecycle;
	lifecycle.RegistrationAccepted();
	lifecycle.ObserveTryPlayerMove(true);
	lifecycle.ObserveCategorizePosition(true);
	Require(lifecycle.OnFrame(true, true), "initial activation failed");
	lifecycle.ToggleChanged();
	Require(!lifecycle.CanCorrect(true), "toggle retained active corrections");
	Require(!lifecycle.OnFrame(true, true), "toggle reused stale observations");
}

void TestRegistrationFailureAndStopFailClosed()
{
	AdapterLifecycle lifecycle;
	lifecycle.RegistrationFailed();
	lifecycle.ObserveTryPlayerMove(true);
	lifecycle.ObserveCategorizePosition(true);
	Require(!lifecycle.OnFrame(true, true), "failed registration armed corrections");
	lifecycle.RegistrationAccepted();
	lifecycle.Stop();
	Require(!lifecycle.CanCorrect(true), "stopping lifecycle allowed corrections");
	Require(!lifecycle.OnFrame(true, true), "stopping lifecycle rearmed");
}

struct FakeHook
{
	bool accepts = false;
	bool removalSucceeds = true;
	int registerCalls = 0;
	int removeCalls = 0;

	bool Register(void *)
	{
		++registerCalls;
		return accepts;
	}
	bool RemoveAll()
	{
		++removeCalls;
		return removalSucceeds;
	}
};

void TestHookPairTransaction()
{
	int firstAddress = 0;
	int secondAddress = 0;
	FakeHook first{true}; // Represents an accepted hook, including valid ID zero.
	FakeHook second{false};
	auto result = RegisterHookPair(first, &firstAddress, second, &secondAddress);
	Require(!result.accepted && result.rolledBack, "partial registration did not roll back");
	Require(first.registerCalls == 1 && second.registerCalls == 1, "registration order changed");
	Require(first.removeCalls == 1 && second.removeCalls == 1, "both hooks were not retired");

	first = FakeHook{false, false};
	second = FakeHook{true, true};
	result = RegisterHookPair(first, &firstAddress, second, &secondAddress);
	Require(!result.accepted && !result.rolledBack, "incomplete rollback was reported complete");
	Require(second.registerCalls == 0, "second hook registered after first rejection");
	Require(first.removeCalls == 1 && second.removeCalls == 1, "rollback short-circuited a removal");

	first = FakeHook{true};
	second = FakeHook{true};
	result = RegisterHookPair(first, &firstAddress, second, &secondAddress);
	Require(result.accepted && !result.rolledBack, "accepted pair reported failure");
	Require(first.removeCalls == 0 && second.removeCalls == 0, "accepted hooks were removed");
}
} // namespace

int main()
{
	try
	{
		TestNeedsBothCallbacksAndFrameBoundary();
		TestDisabledObservationsCannotArm();
		TestToggleRequiresFreshObservations();
		TestRegistrationFailureAndStopFailClosed();
		TestHookPairTransaction();
		std::cout << "RampBugFix adapter lifecycle tests passed (5 groups).\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL: " << error.what() << '\n';
		return 1;
	}
}
