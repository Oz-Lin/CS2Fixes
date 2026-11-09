/**
 * CS2Fixes - Copyright (C) 2023-2024 Source2ZE
 * GPL version 3.0; see LICENSE. No warranty of merchantability or fitness.
 * Regression tests for the engine-independent Interesting-exe/zer0 legacy
 * RampBugFix extraction. These do NOT validate CS2 engine ABI or runtime hooks.
 */

#include "rampbugfix/core.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace rampbugfix;

namespace
{
void Require(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

bool Near(float a, float b, float tolerance = 0.0001f) { return std::fabs(a - b) <= tolerance; }
bool Near(const Vec3 &a, const Vec3 &b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }

Trace Clear(const Vec3 &start, const Vec3 &end) { return {start, end, {}, 1.0f, false, false}; }
Trace Hit(const Vec3 &start, const Vec3 &end, float fraction, const Vec3 &normal)
{
	return {start, start + (end - start) * fraction, normal, fraction, false, false};
}

class Script final : public TraceProvider
{
public:
	struct Step
	{
		Vec3 start;
		Vec3 end;
		Trace result;
		bool available;
	};
	std::vector<Step> steps;
	std::size_t calls = 0;
	Bounds expectedBounds;

	void Add(const Vec3 &start, const Vec3 &end, const Trace &result, bool available = true)
	{
		steps.push_back({start, end, result, available});
	}
	void Validated(const Vec3 &start, const Vec3 &end, const Trace &result)
	{
		Add(start, end, result);
		Add(result.end, result.end, Clear(result.end, result.end));
		Add(result.end, result.start, Clear(result.end, result.start));
	}
	bool TraceHull(const Vec3 &start, const Vec3 &end, const Bounds &bounds, Trace &out) override
	{
		Require(calls < steps.size(), "unexpected trace call");
		const Step &step = steps[calls++];
		Require(Near(start, step.start) && Near(end, step.end), "unexpected trace segment");
		Require(bounds.mins == expectedBounds.mins && bounds.maxs == expectedBounds.maxs, "bounds were not forwarded");
		out = step.result;
		// Round-trip exact input starts as a real adapter should do.
		if (Near(out.start, start))
			out.start = start;
		return step.available;
	}
	void Done() const { Require(calls == steps.size(), "expected trace was not performed"); }
};

class FunctionProvider final : public TraceProvider
{
public:
	std::function<Trace(const Vec3 &, const Vec3 &)> function;
	unsigned calls = 0;
	bool TraceHull(const Vec3 &start, const Vec3 &end, const Bounds &, Trace &out) override
	{
		Require(++calls < 2000, "unbounded trace loop");
		out = function(start, end);
		return true;
	}
};

MovementContext Context()
{
	MovementContext context;
	context.valid = true;
	context.movementId = 1;
	context.frameTime = 0.1f;
	context.velocity = {100.0f, 0.0f, -100.0f};
	return context;
}

void Begin(State &state, const MovementContext &context) { Require(BeginMovement(state, context), "valid movement rejected"); }

void QueueRescue(Script &provider, const MovementContext &context, bool startSolid = false)
{
	const Vec3 end = context.origin + context.velocity * context.frameTime;
	Trace blocked = Hit(context.origin, end, 0.0f, {0.0f, 0.0f, 1.0f});
	blocked.startSolid = startSolid;
	if (startSolid)
		provider.Add(context.origin, end, blocked);
	else
		provider.Validated(context.origin, end, blocked);
	const Vec3 offset = Vec3{0.8f, 0.0f, 0.6f} * (kRampPierceDistance * 0.1f);
	provider.Validated(context.origin + offset, end + offset, Clear(context.origin + offset, end + offset));
	provider.Validated(end + offset, end, Clear(end + offset, end));
}

void PrepareRescue(State &state, MovementContext &context, Script &provider)
{
	state.lastValidPlane = {0.8f, 0.0f, 0.6f};
	Begin(state, context);
	QueueRescue(provider, context);
	Require(PredictTryPlayerMove(state, context, provider), "ramp prediction failed");
	Require(state.prediction.valid && state.prediction.pierced, "ramp was not pierced");
	provider.Done();
}

void NormalMovement()
{
	MovementContext context = Context();
	State state;
	Begin(state, context);
	Script provider;
	const Vec3 end = context.origin + context.velocity * context.frameTime;
	provider.Validated(context.origin, end, Clear(context.origin, end));
	Require(PredictTryPlayerMove(state, context, provider), "normal movement prediction failed");
	Require(!state.prediction.pierced && state.prediction.origin == end, "normal movement marked corrected");
	context.origin = end;
	context.velocity = {}; // Even an engine discrepancy needs a proven pierce.
	Require(!FinishTryPlayerMove(state, context).apply, "normal movement was changed");
	provider.Done();
}

void RampRescue()
{
	MovementContext context = Context();
	State state;
	Script provider;
	PrepareRescue(state, context, provider);
	const Vec3 predictedOrigin = state.prediction.origin;
	const Vec3 predictedVelocity = state.prediction.velocity;
	Require(context.origin == Vec3{} && context.velocity == Vec3{100.0f, 0.0f, -100.0f}, "prediction changed input movement");
	context.velocity = {};
	const Correction correction = FinishTryPlayerMove(state, context);
	Require(correction.apply && correction.changeVelocity, "stopped ramp result was not rescued");
	Require(correction.origin == predictedOrigin && correction.velocity == predictedVelocity, "wrong rescue proposal");
	Require(!FinishTryPlayerMove(state, context).apply, "prediction applied twice");
}

void SharpCorner()
{
	MovementContext context = Context();
	context.velocity = {100.0f, 0.0f, 0.0f};
	State state;
	state.lastValidPlane = {0.8f, 0.0f, 0.6f};
	Begin(state, context);
	FunctionProvider provider;
	provider.function = [](const Vec3 &start, const Vec3 &end) {
		return end.x - start.x > 1.0f ? Hit(start, end, 0.0f, {-1.0f, 0.0f, 0.0f}) : Clear(start, end);
	};
	Require(PredictTryPlayerMove(state, context, provider), "legitimate corner prediction rejected");
	Require(!state.prediction.pierced && state.prediction.velocity == Vec3{}, "corner got an inappropriate boost");
	Require(!FinishTryPlayerMove(state, context).apply, "corner incorrectly overridden");
}

void FourBumpLimit()
{
	MovementContext context = Context();
	State state;
	Begin(state, context);
	FunctionProvider provider;
	unsigned bumps = 0;
	provider.function = [&bumps](const Vec3 &start, const Vec3 &end) {
		if (end.x > start.x)
		{
			++bumps;
			return Hit(start, end, 0.2f, {0.0f, 0.0f, 1.0f});
		}
		return Clear(start, end);
	};
	Require(PredictTryPlayerMove(state, context, provider), "four-bump prediction failed");
	Require(bumps == kMaxBumps && bumps == 4, "legacy four-bump cap changed");
	Require(state.prediction.velocity.Length() <= context.velocity.Length(), "multi-plane clipping added speed");
}

void BadTraces()
{
	const MovementContext context = Context();
	const Vec3 end = context.origin + context.velocity * context.frameTime;
	std::vector<Trace> bad;
	Trace trace = Clear(context.origin, end);
	trace.fraction = std::numeric_limits<float>::quiet_NaN();
	bad.push_back(trace);
	trace = Clear(context.origin, end);
	trace.end.x = std::numeric_limits<float>::infinity();
	bad.push_back(trace);
	trace = Hit(context.origin, end, 0.5f, {1.0f, 1.0f, 0.0f});
	bad.push_back(trace);
	trace = Hit(context.origin, end, 0.5f, {0.5f, 0.0f, 0.0f});
	bad.push_back(trace);
	trace = Clear(context.origin, end);
	trace.fraction = -0.1f;
	bad.push_back(trace);
	trace.fraction = 1.1f;
	bad.push_back(trace);
	trace = Hit(context.origin, end, 0.0f, {});
	bad.push_back(trace);
	trace.startSolid = true;
	bad.push_back(trace);
	trace.startSolid = false;
	trace.allSolid = true;
	bad.push_back(trace);
	trace = Clear(context.origin, end);
	trace.start.x = 99.0f;
	bad.push_back(trace);
	trace = Clear(context.origin, end);
	trace.end.x = 9999.0f;
	bad.push_back(trace);
	for (const Trace &invalid : bad)
	{
		State state;
		Begin(state, context);
		Script provider;
		provider.Add(context.origin, end, invalid);
		Require(!PredictTryPlayerMove(state, context, provider), "malformed trace accepted");
		Require(!state.prediction.valid && !FinishTryPlayerMove(state, context).apply, "bad trace produced a correction");
		provider.Done();
	}
	State state;
	Begin(state, context);
	Script unavailable;
	unavailable.Add(context.origin, end, Clear(context.origin, end), false);
	Require(!PredictTryPlayerMove(state, context, unavailable), "failed provider accepted");
}

void StartSolidRescue()
{
	MovementContext context = Context();
	State state;
	state.lastValidPlane = {0.8f, 0.0f, 0.6f};
	Begin(state, context);
	Script provider;
	QueueRescue(provider, context, true);
	Require(PredictTryPlayerMove(state, context, provider) && state.prediction.pierced, "validated start-solid rescue lost");
	provider.Done();
}

void FullPierceDistance()
{
	const MovementContext context = Context();
	const Vec3 end = context.origin + context.velocity * context.frameTime;
	State state;
	state.lastValidPlane = {0.8f, 0, 0.6f};
	Begin(state, context);
	Script provider;
	provider.Validated(context.origin, end, Hit(context.origin, end, 0, {0, 0, 1}));
	for (unsigned step = 1; step <= 10; ++step)
	{
		const Vec3 offset = state.lastValidPlane * (0.75f * (static_cast<float>(step) * 0.1f));
		const Trace trace = step == 10 ? Clear(context.origin + offset, end + offset)
									  : Hit(context.origin + offset, end + offset, 0, {0, 0, 1});
		provider.Validated(context.origin + offset, end + offset, trace);
	}
	const Vec3 maxOffset = state.lastValidPlane * 0.75f;
	provider.Validated(end + maxOffset, end, Clear(end + maxOffset, end));
	Require(PredictTryPlayerMove(state, context, provider) && state.prediction.pierced, "full 0.75-unit pierce candidate was skipped");
	Require(Near(maxOffset.Length(), 0.75f), "pierce distance changed");
	provider.Done();
}

void NewPlaneAndStaleCandidate()
{
	for (unsigned scenario = 0; scenario < 3; ++scenario)
	{
		MovementContext context = Context();
		context.velocity = {0, 0, -100};
		State state;
		state.lastValidPlane = {0.8f, 0, 0.6f};
		Begin(state, context);
		// Similarity 0.98 is below the 0.99 same-ramp threshold but above the
		// legacy 0.95 new-plane fallback. Similarity 0.94 must not qualify.
		const float angle = scenario == 1 ? 0.35f : 0.2f;
		const Vec3 candidate{0.8f * std::cos(angle) + 0.6f * std::sin(angle), 0,
						  0.6f * std::cos(angle) - 0.8f * std::sin(angle)};
		FunctionProvider provider;
		unsigned sweeps = 0;
		provider.function = [&](const Vec3 &start, const Vec3 &end) {
			if (end.z - start.z < -1.0f)
			{
				++sweeps;
				if (sweeps == 1)
					return Hit(start, end, 0, {0, 0, 1});
				if (scenario == 0 && sweeps > 11)
					return Clear(start, end);
				Trace trace = Hit(start, end, 0.05f, candidate);
				if (scenario == 2 && (sweeps - 1) % 10 == 0)
				{
					trace = Hit(start, end, 0, {});
					trace.startSolid = true;
				}
				return trace;
			}
			return Clear(start, end);
		};
		Require(PredictTryPlayerMove(state, context, provider), "new-plane scenario did not finish");
		Require(state.prediction.pierced == (scenario == 0), "new-plane threshold or stale final-candidate behavior changed");
	}
}

void TraceValidationProbes()
{
	const MovementContext context = Context();
	const Vec3 end = context.origin + context.velocity * context.frameTime;
	for (unsigned scenario = 0; scenario < 3; ++scenario)
	{
		State state;
		Begin(state, context);
		Script provider;
		provider.Validated(context.origin, end, Clear(context.origin, end));
		if (scenario == 0)
		{
			provider.steps.resize(2);
			provider.steps.back().result.startSolid = true;
		}
		if (scenario == 1)
			provider.steps.back().result.startSolid = true;
		if (scenario == 2)
			provider.steps.back().result = Hit(end, context.origin, 0.5f, {1, 0, 0});
		Require(PredictTryPlayerMove(state, context, provider) == (scenario == 2), "legacy validation probe behavior changed");
		Require(!FinishTryPlayerMove(state, context).apply, "validation probe manufactured a correction");
		provider.Done();
	}
}

void InvalidPierceConnector()
{
	MovementContext context = Context();
	State state;
	state.lastValidPlane = {0.8f, 0.0f, 0.6f};
	Begin(state, context);
	Script provider;
	QueueRescue(provider, context);
	// This is the back-to-original-end connector, not the validated sweep.
	provider.steps.resize(7);
	provider.steps.back().available = false;
	Require(!PredictTryPlayerMove(state, context, provider), "failed pierce connector accepted");
	Require(!state.prediction.valid, "partly completed rescue survived failure");
	provider.Done();
}

void StoppedAndInvalidContext()
{
	MovementContext context = Context();
	State state;
	Script provider;
	PrepareRescue(state, context, provider);
	context.velocity = {};
	Script empty;
	Require(!PredictTryPlayerMove(state, context, empty), "stopped movement needs no prediction");
	Require(!state.prediction.valid && !FinishTryPlayerMove(state, context).apply, "stale prediction reused after stop");
	EndMovement(state, context.movementId);
	context.origin = {100000000.0f, 0.0f, 0.0f};
	context.velocity = {0.000001f, 0.0f, 0.0f};
	Begin(state, context);
	Require(!PredictTryPlayerMove(state, context, empty), "zero-length rounded displacement traced");
	EndMovement(state, context.movementId);
	for (unsigned i = 0; i < 7; ++i)
	{
		context = Context();
		switch (i)
		{
		case 0: context.valid = false; break;
		case 1: context.frameTime = 0.0f; break;
		case 2: context.frameTime = -0.1f; break;
		case 3: context.frameTime = std::numeric_limits<float>::quiet_NaN(); break;
		case 4: context.velocity.x = std::numeric_limits<float>::infinity(); break;
		case 5: context.bounds.maxs.x = context.bounds.mins.x; break;
		case 6: context.movementId = 0; break;
		}
		Require(!BeginMovement(state, context), "invalid context accepted");
		Require(!PredictTryPlayerMove(state, context, empty), "invalid context predicted");
	}
	empty.Done();
}

void FirstTraceInputs()
{
	for (unsigned input = 0; input < 5; ++input)
	{
		const MovementContext context = Context();
		const Vec3 end = context.origin + context.velocity * context.frameTime;
		State state;
		Begin(state, context);
		Trace first = Clear(context.origin, end);
		Script provider;
		if (input == 2)
		{
			provider.Add(end, end, Clear(end, end));
			provider.Add(end, context.origin, Clear(end, context.origin));
		}
		else
			provider.Validated(context.origin, end, Clear(context.origin, end));
		if (input == 3)
			first.start.x = 100.0f;
		if (input == 4)
			first.fraction = std::numeric_limits<float>::quiet_NaN();
		Require(PredictTryPlayerMove(state, context, provider, input == 1 ? nullptr : &end, input == 0 ? nullptr : &first),
				"partial/stale cached trace was not handled safely");
		Require(!state.prediction.pierced && !FinishTryPlayerMove(state, context).apply, "cached trace manufactured correction");
		provider.Done();
	}
	const MovementContext context = Context();
	const Vec3 end = context.origin + context.velocity * context.frameTime;
	const Trace cached = Hit(context.origin, end, 0.5f, {0, 0, 1});
	State state;
	state.lastValidPlane = {0.8f, 0, 0.6f};
	Begin(state, context);
	Script provider;
	provider.Add(cached.end, cached.end, Clear(cached.end, cached.end));
	provider.Add(cached.end, cached.start, Clear(cached.end, cached.start));
	const Vec3 nextEnd = cached.end + Vec3{100, 0, 0} * 0.05f;
	provider.Validated(cached.end, nextEnd, Clear(cached.end, nextEnd));
	Require(PredictTryPlayerMove(state, context, provider, &end, &cached), "matching cached collision failed");
	Require(!state.prediction.pierced && state.prediction.velocity == Vec3{100, 0, 0}, "cached first sweep did not preserve legacy clipping path");
	provider.Done();
}

bool Discrepancy(const Vec3 &predicted, const Vec3 &actual)
{
	MovementContext context = Context();
	State state;
	Begin(state, context);
	context.velocity = actual;
	state.prediction = {true, true, context.movementId, {}, predicted};
	return FinishTryPlayerMove(state, context).apply;
}

void VelocityThresholds()
{
	Require(!Discrepancy({100, 0, 0}, {95, 0, 0}), "exact 0.95 speed ratio was overridden");
	Require(Discrepancy({100, 0, 0}, {94.99f, 0, 0}), "sub-0.95 speed ratio was ignored");
	Require(!Discrepancy({50, 0, 0}, {20, 0, 0}), "speed ratio gate at 50 changed");
	Require(Discrepancy({50.01f, 0, 0}, {20, 0, 0}), "speed ratio above 50 was ignored");
	Require(!Discrepancy({100, 0, 0}, {110, 0, 0}), "legitimate acceleration was overridden");
	Require(!Discrepancy({100, 0, 0}, {99.5f, 9.987492f, 0}), "small direction change was overridden");
	Require(Discrepancy({100, 0, 0}, {98.5f, 17.255434f, 0}), "large direction change was ignored");
	Require(Discrepancy({100, 0, 0}, {}), "zero engine velocity was not rescued");
	Require(!Discrepancy({}, {100, 0, 0}), "zero predicted velocity produced a bogus direction");
	Require(!Discrepancy({std::numeric_limits<float>::quiet_NaN(), 0, 0}, {100, 0, 0}), "NaN prediction applied");
	Require(!Discrepancy({100, 0, 0}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}), "NaN actual velocity applied");
}

void NudgeGround()
{
	MovementContext context = Context();
	context.velocity.z = -64.0f;
	context.bounds.maxs.z = 54.0f;
	State state;
	state.lastValidPlane = {0.8f, 0.0f, 0.6f};
	Begin(state, context);
	Script provider;
	provider.expectedBounds = context.bounds;
	const Vec3 down = context.origin - Vec3{0, 0, 2};
	provider.Validated(context.origin, down, Hit(context.origin, down, 0.5f, {0, 0, 1}));
	const Vec3 nudged = context.origin + state.lastValidPlane * 0.0625f;
	const Vec3 nudgedDown = nudged - Vec3{0, 0, 2};
	provider.Validated(nudged, nudgedDown, Clear(nudged, nudgedDown));
	const Correction result = CategorizePosition(state, context, provider, false);
	Require(result.apply && !result.changeVelocity && result.origin == nudged && result.velocity == context.velocity,
			"ground nudge was not an origin-only 1/16-unit proposal");
	Require(context.origin == Vec3{} && !state.prediction.valid, "nudge changed input or fabricated TPM prediction");
	provider.Done();
}

void NudgeRejections()
{
	for (unsigned scenario = 0; scenario < 8; ++scenario)
	{
		MovementContext context = Context();
		State state;
		state.lastValidPlane = {0.8f, 0.0f, 0.6f};
		Begin(state, context);
		Script provider;
		bool stay = false;
		if (scenario == 0) stay = true;
		if (scenario == 1) context.velocity.z = -63.99f;
		if (scenario == 2) state.lastValidPlane = {0, 0, 1};
		if (scenario == 3) state.lastValidPlane = {};
		if (scenario >= 4)
		{
			const Vec3 down = context.origin - Vec3{0, 0, 2};
			const Vec3 normal = scenario == 5 ? Vec3{std::sqrt(0.51f), 0, 0.7f} : Vec3{0, 0, 1};
			const float fraction = scenario == 4 ? 0.95f : 0.5f;
			Trace ground = Hit(context.origin, down, fraction, normal);
			if (scenario == 6)
			{
				ground.startSolid = true;
				provider.Add(context.origin, down, ground);
			}
			else
				provider.Validated(context.origin, down, ground);
			if (scenario == 7)
			{
				const Vec3 nudged = context.origin + state.lastValidPlane * 0.0625f;
				const Vec3 nudgedDown = nudged - Vec3{0, 0, 2};
				Trace blocked = Hit(nudged, nudgedDown, 0.0f, {0, 0, 1});
				blocked.startSolid = true;
				provider.Add(nudged, nudgedDown, blocked);
			}
		}
		Require(!CategorizePosition(state, context, provider, stay).apply, "ineligible ground nudge applied");
		provider.Done();
	}
}

void LifecycleAndStalePredictions()
{
	MovementContext context = Context();
	State state;
	Script provider;
	PrepareRescue(state, context, provider);
	MovementContext otherFrame = context;
	otherFrame.movementId++;
	otherFrame.velocity = {};
	Require(!FinishTryPlayerMove(state, otherFrame).apply && !state.prediction.valid, "wrong movement scope reused prediction");
	EndMovement(state, context.movementId);
	Require(!state.processingMovement && state.lastValidPlane == Vec3{0.8f, 0, 0.6f}, "valid plane did not persist across TPM scope");
	Begin(state, otherFrame);
	EndMovement(state, otherFrame.movementId);
	Require(state.lastValidPlane == Vec3{}, "frame without TPM kept stale plane");
	Begin(state, context);
	state.lastValidPlane = {0.8f, 0, 0.6f};
	state.prediction = {true, true, context.movementId, {1, 2, 3}, {100, 0, 0}};
	state.Reset();
	Require(!state.processingMovement && !state.didTryPlayerMove && !state.prediction.valid && state.movementId == 0
			&& state.lastValidPlane == Vec3{}, "lifecycle reset left state behind");
	Script empty;
	Require(!PredictTryPlayerMove(state, context, empty) && !FinishTryPlayerMove(state, context).apply,
			"inactive movement scope accepted work");
	Require(!CategorizePosition(state, context, empty, false).apply, "inactive scope nudged");
	Begin(state, context);
	Require(!BeginMovement(state, context) && !state.processingMovement, "nested scope reused state");
	Begin(state, context);
	EndMovement(state, context.movementId + 1);
	Require(!state.processingMovement && state.lastValidPlane == Vec3{}, "mismatched end did not reset");
}

void LegacyClipAdjustment()
{
	// Slightly non-exact floating normals can leave a small negative dot after
	// clipping. The legacy integer-zero expression removes only that residual.
	const Vec3 normal{0.8f, 0, 0.6f};
	const Vec3 input{-0.001f, 0, -0.001f};
	Vec3 expected = input - normal * input.Dot(normal);
	const float residual = expected.Dot(normal);
	Require(residual < 0.0f, "clip regression vector did not exercise the legacy residual adjustment");
	if (residual < 0.0f)
		expected = expected - normal * residual;
	const Vec3 clipped = ClipVelocity(input, normal);
	Require(clipped == expected && clipped.Length() < 0.002f, "legacy clip gained a 1/128 impulse");
	Require(ClipVelocity({1, 2, 3}, {}) == Vec3{}, "zero normal not rejected");
	Require(ClipVelocity({1, 2, 3}, {1, 1, 1}) == Vec3{}, "deformed normal not rejected");
}
} // namespace

int main()
{
	const std::vector<std::pair<const char *, std::function<void()>>> tests = {
		{"normal movement", NormalMovement}, {"ramp rescue", RampRescue}, {"sharp corner", SharpCorner},
		{"four-bump limit", FourBumpLimit}, {"malformed traces", BadTraces}, {"start-solid rescue", StartSolidRescue},
		{"full 0.75-unit pierce", FullPierceDistance}, {"new-plane and stale candidate", NewPlaneAndStaleCandidate},
		{"unswept/backward validation", TraceValidationProbes},
		{"invalid pierce connector", InvalidPierceConnector}, {"stopped/invalid movement", StoppedAndInvalidContext},
		{"partial/stale first trace", FirstTraceInputs}, {"velocity thresholds", VelocityThresholds},
		{"ground nudge", NudgeGround}, {"ground nudge rejection", NudgeRejections},
		{"lifecycle and stale predictions", LifecycleAndStalePredictions}, {"legacy clip adjustment", LegacyClipAdjustment},
	};
	unsigned failures = 0;
	for (const auto &test : tests)
	{
		try
		{
			test.second();
			std::cout << "PASS " << test.first << '\n';
		}
		catch (const std::exception &error)
		{
			++failures;
			std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
		}
	}
	std::cout << tests.size() - failures << '/' << tests.size() << " algorithm test groups passed (not CS2 runtime verification).\n";
	return failures == 0 ? 0 : 1;
}
