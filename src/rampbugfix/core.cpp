/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2024 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Adapted from Interesting-exe/CS2Fixes-RampbugFix, src/rbf-detours.cpp.
 * Its README credits zer0: https://gist.github.com/zer0k-z/2eb0c230c8f2c62b5c46d36353cf8d8d
 */

#include "core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rampbugfix
{
namespace
{
constexpr float kEpsilon = std::numeric_limits<float>::epsilon();

bool UsableVector(const Vec3 &v)
{
	return v.IsFinite() && std::isfinite(v.Dot(v));
}

bool UsablePlane(const Vec3 &normal)
{
	// Real trace normals are unit length. Reject malformed normals instead of
	// normalizing them into plausible geometry or allowing clipping to add speed.
	return UsableVector(normal) && std::fabs(normal.x) <= 1.0f && std::fabs(normal.y) <= 1.0f
		   && std::fabs(normal.z) <= 1.0f && std::fabs(normal.Dot(normal) - 1.0f) <= 0.01f;
}

bool ValidContext(const MovementContext &context)
{
	return context.valid && context.movementId != 0 && std::isfinite(context.frameTime) && context.frameTime > 0.0f
		   && UsableVector(context.origin) && UsableVector(context.velocity) && UsableVector(context.bounds.mins)
		   && UsableVector(context.bounds.maxs) && context.bounds.mins.x < context.bounds.maxs.x
		   && context.bounds.mins.y < context.bounds.maxs.y && context.bounds.mins.z < context.bounds.maxs.z;
}

bool ActiveContext(const State &state, const MovementContext &context)
{
	return ValidContext(context) && state.processingMovement && state.movementId == context.movementId;
}

bool TraceDataValid(const Trace &trace, const Vec3 &requestedStart)
{
	return UsableVector(trace.start) && UsableVector(trace.end) && UsableVector(trace.normal)
		   && trace.start == requestedStart && std::isfinite(trace.fraction) && trace.fraction >= 0.0f && trace.fraction <= 1.0f
		   && std::fabs(trace.normal.x) <= 1.0f && std::fabs(trace.normal.y) <= 1.0f && std::fabs(trace.normal.z) <= 1.0f
		   && (trace.normal.Dot(trace.normal) <= kEpsilon * kEpsilon || UsablePlane(trace.normal));
}

bool TraceMatchesSweep(const Trace &trace, const Vec3 &start, const Vec3 &end)
{
	if (!TraceDataValid(trace, start) || !UsableVector(end - start))
		return false;
	const Vec3 expected = start + (end - start) * trace.fraction;
	const Vec3 error = trace.end - expected;
	// Reject mismatched/stale or fabricated endpoints. Allow normal float error
	// in an engine adapter, but not an unrelated destination with fraction == 1.
	return UsableVector(error) && error.Length() <= 0.001f + (end - start).Length() * 0.00001f;
}

bool ReadTrace(TraceProvider &provider, const Vec3 &start, const Vec3 &end, const Bounds &bounds, Trace &trace, bool &failed)
{
	trace = {};
	if (!UsableVector(start) || !UsableVector(end) || !provider.TraceHull(start, end, bounds, trace) || !TraceMatchesSweep(trace, start, end))
	{
		failed = true;
		return false;
	}
	return true;
}

bool ValidMovementTrace(const Trace &trace, const Bounds &bounds, TraceProvider &provider, bool &failed)
{
	if (trace.startSolid || trace.allSolid || (trace.fraction < 1.0f && !UsablePlane(trace.normal)))
		return false;

	Trace stuck;
	if (!ReadTrace(provider, trace.end, trace.end, bounds, stuck, failed) || stuck.startSolid || stuck.allSolid
		|| stuck.fraction < 1.0f - kEpsilon)
		return false;
	if (!ReadTrace(provider, trace.end, trace.start, bounds, stuck, failed) || stuck.startSolid || stuck.allSolid)
		return false;
	// Deliberately do not reject backward-only fractional hits: legacy disabled
	// that test after the Call to Arms update. Numerical validity is still required.
	return true;
}

Vec3 Cross(const Vec3 &a, const Vec3 &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

bool TryPierce(const Vec3 &start, const Vec3 &end, const Bounds &bounds, TraceProvider &provider,
			  Vec3 &lastPlane, Trace &movement, bool &failed)
{
	const float distance = (end - start).Length();
	if (!std::isfinite(distance) || distance <= kEpsilon)
		return false;
	constexpr float offsets[] = {0.0f, -1.0f, 1.0f};
	for (unsigned i = 0; i < 3; ++i)
	{
		for (unsigned j = 0; j < 3; ++j)
		{
			for (unsigned k = 0; k < 3; ++k)
			{
				Vec3 direction = lastPlane;
				if (i != 0 || j != 0 || k != 0)
				{
					direction = {offsets[i], offsets[j], offsets[k]};
					if (lastPlane.Dot(direction) <= 0.0f)
						continue;
					Trace test;
					if (!ReadTrace(provider, start + direction * kRampPierceDistance, start, bounds, test, failed))
						return false;
					if (!ValidMovementTrace(test, bounds, provider, failed))
					{
						if (failed)
							return false;
						continue;
					}
				}

				Trace pierce;
				bool goodTrace = false;
				bool hitNewPlane = false;
				bool haveCandidate = false;
				// Ten exact candidate indices retain the intended 0.1..1.0 range;
				// repeated floating addition accidentally omitted 1.0 in legacy.
				for (unsigned step = 1; step <= 10; ++step)
				{
					const Vec3 offset = direction * (kRampPierceDistance * (static_cast<float>(step) * 0.1f));
					if (!ReadTrace(provider, start + offset, end + offset, bounds, pierce, failed))
						return false;
					if (!ValidMovementTrace(pierce, bounds, provider, failed))
					{
						if (failed)
							return false;
						// An invalid last sample must not reuse the preceding sample's
						// hitNewPlane flag (an uninitialized/stale legacy path).
						haveCandidate = false;
						hitNewPlane = false;
						continue;
					}
					haveCandidate = true;
					const bool validPlane = pierce.fraction < 1.0f && pierce.fraction > 0.1f
										&& pierce.normal.Dot(lastPlane) >= kRampBugThreshold;
					hitNewPlane = movement.normal.Dot(pierce.normal) < kNewRampThreshold
								  && lastPlane.Dot(pierce.normal) > kNewRampThreshold;
					goodTrace = std::fabs(pierce.fraction - 1.0f) < kEpsilon || validPlane;
					if (goodTrace)
						break;
				}

				if (haveCandidate && (goodTrace || hitNewPlane))
				{
					Trace back;
					if (!ReadTrace(provider, pierce.end, end, bounds, back, failed))
						return false;
					// Legacy used this result unchecked. Never propose an origin in
					// solid or use missing/malformed geometry from the connector.
					if (!ValidMovementTrace(back, bounds, provider, failed))
					{
						if (failed)
							return false;
						continue;
					}
					movement = pierce;
					movement.start = start;
					movement.fraction = std::clamp((pierce.end - pierce.start).Length() / distance, 0.0f, 1.0f);
					movement.end = back.end;
					movement.normal = UsablePlane(pierce.normal) ? pierce.normal : back.normal;
					if (!TraceDataValid(movement, start) || (movement.fraction < 1.0f && !UsablePlane(movement.normal)))
						return false;
					if (UsablePlane(movement.normal))
						lastPlane = movement.normal;
					return true;
				}
			}
		}
	}
	return false;
}
} // namespace

Vec3 Vec3::operator+(const Vec3 &other) const { return {x + other.x, y + other.y, z + other.z}; }
Vec3 Vec3::operator-(const Vec3 &other) const { return {x - other.x, y - other.y, z - other.z}; }
Vec3 Vec3::operator*(float scale) const { return {x * scale, y * scale, z * scale}; }
bool Vec3::operator==(const Vec3 &other) const { return x == other.x && y == other.y && z == other.z; }
float Vec3::Dot(const Vec3 &other) const { return x * other.x + y * other.y + z * other.z; }
float Vec3::Length() const { return std::sqrt(Dot(*this)); }
bool Vec3::IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }

void State::Reset() { *this = {}; }

bool BeginMovement(State &state, const MovementContext &context)
{
	if (!ValidContext(context) || state.processingMovement)
	{
		state.Reset();
		return false;
	}
	if (!UsablePlane(state.lastValidPlane))
		state.lastValidPlane = {};
	state.processingMovement = true;
	state.didTryPlayerMove = false;
	state.movementId = context.movementId;
	state.prediction = {};
	return true;
}

void EndMovement(State &state, std::uint64_t movementId)
{
	if (!state.processingMovement || state.movementId != movementId)
	{
		state.Reset();
		return;
	}
	if (!state.didTryPlayerMove)
		state.lastValidPlane = {};
	state.processingMovement = false;
	state.movementId = 0;
	state.prediction = {};
}

Vec3 ClipVelocity(const Vec3 &velocity, const Vec3 &normal)
{
	if (!UsableVector(velocity) || !UsablePlane(normal))
		return {};
	Vec3 result = velocity - normal * velocity.Dot(normal);
	const float adjust = result.Dot(normal);
	if (adjust < 0.0f)
	{
		// In the original MIN(adjust, -1 / 128), integer division evaluates to
		// zero. Preserve that effective behavior, not a new -1.0f/128 impulse.
		result = result - normal * adjust;
	}
	return UsableVector(result) ? result : Vec3{};
}

bool PredictTryPlayerMove(State &state, const MovementContext &context, TraceProvider &provider,
						  const Vec3 *firstDest, const Trace *firstTrace)
{
	state.prediction = {};
	if (!ActiveContext(state, context))
		return false;
	state.didTryPlayerMove = true;
	Vec3 start = context.origin;
	Vec3 velocity = context.velocity;
	if (velocity.Length() <= kEpsilon)
		return false;
	const Vec3 primalVelocity = velocity;
	Vec3 lastPlane = UsablePlane(state.lastValidPlane) ? state.lastValidPlane : Vec3{};
	float timeLeft = context.frameTime;
	float allFraction = 0.0f;
	std::array<Vec3, kMaxBumps + 1> planes{};
	unsigned numPlanes = 0;
	Trace movement;
	bool haveTrace = false;
	bool pierced = false;
	bool potentiallyStuck = false;
	bool failed = false;

	for (unsigned bump = 0; bump < kMaxBumps; ++bump)
	{
		const Vec3 end = start + velocity * timeLeft;
		if (!UsableVector(end))
			return false;
		if (end == start || velocity.Length() <= kEpsilon || timeLeft <= 0.0f)
			break;
		const bool cached = bump == 0 && firstDest && firstTrace && *firstDest == end && TraceMatchesSweep(*firstTrace, start, end);
		if (cached)
			movement = *firstTrace;
		else if (!ReadTrace(provider, start, end, context.bounds, movement, failed))
			return false;
		haveTrace = true;
		const bool valid = ValidMovementTrace(movement, context.bounds, provider, failed);
		if (failed)
			return false;
		if (!cached)
		{
			if (valid && movement.fraction == 1.0f)
				break;
			if (UsablePlane(lastPlane) && (!valid || movement.normal.Dot(lastPlane) < kRampBugThreshold
										 || (potentiallyStuck && movement.fraction == 0.0f)))
			{
				const bool rescued = TryPierce(start, end, context.bounds, provider, lastPlane, movement, failed);
				if (failed)
					return false;
				if (!valid && !rescued)
					return false;
				pierced = pierced || rescued;
			}
			else if (!valid)
				return false;
			if (UsablePlane(movement.normal) && movement.normal.Length() > 0.99f)
				lastPlane = movement.normal;
			potentiallyStuck = movement.fraction == 0.0f;
		}
		else if (!valid)
			return false;

		if (movement.fraction * velocity.Length() > 0.03125f)
		{
			allFraction += movement.fraction;
			start = movement.end;
			numPlanes = 0;
		}
		if (allFraction == 1.0f || movement.fraction == 1.0f)
			break;
		// Retain the legacy full-frame subtraction, but never integrate backward
		// when accumulated fractions consume more than the available frame time.
		timeLeft = std::max(0.0f, timeLeft - context.frameTime * movement.fraction);
		if (numPlanes >= planes.size() || !UsablePlane(movement.normal))
			return false;
		planes[numPlanes++] = movement.normal;
		if (numPlanes == 1 && context.walking && !context.onGround)
			velocity = ClipVelocity(velocity, planes[0]);
		else
		{
			unsigned i = 0;
			for (; i < numPlanes; ++i)
			{
				velocity = ClipVelocity(velocity, planes[i]);
				unsigned j = 0;
				for (; j < numPlanes; ++j)
				{
					if (j != i && velocity.Dot(planes[j]) < 0.0f)
						break;
				}
				if (j == numPlanes)
					break;
			}
			if (i == numPlanes)
			{
				const float speed2D = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
				if (numPlanes != 2 || (movement.normal.z >= 0.7f && speed2D < 1.0f))
				{
					velocity = {};
					break;
				}
				Vec3 direction = Cross(planes[0], planes[1]);
				const float length = direction.Length();
				if (!std::isfinite(length) || length <= kEpsilon)
				{
					velocity = {};
					break;
				}
				direction = direction * (1.0f / length);
				velocity = direction * direction.Dot(velocity);
				if (velocity.Dot(primalVelocity) <= 0.0f)
				{
					velocity = {};
					break;
				}
			}
		}
		if (!UsableVector(velocity))
			return false;
	}
	if (!haveTrace || !UsableVector(movement.end) || !UsableVector(velocity))
		return false;
	state.lastValidPlane = lastPlane;
	state.prediction = {true, pierced, context.movementId, movement.end, velocity};
	return true;
}

Correction FinishTryPlayerMove(State &state, const MovementContext &actual)
{
	const Prediction prediction = state.prediction;
	state.prediction = {};
	if (!ActiveContext(state, actual) || !prediction.valid || !prediction.pierced || prediction.movementId != actual.movementId
		|| !UsableVector(prediction.origin) || !UsableVector(prediction.velocity))
		return {};
	const float predictedSpeed = prediction.velocity.Length();
	const float actualSpeed = actual.velocity.Length();
	// A stopped prediction has no direction to compare and cannot justify a
	// velocity restoration. A genuinely stopped engine result can still be fixed.
	if (predictedSpeed <= kEpsilon)
		return {};
	const bool changedDirection = actualSpeed <= kEpsilon
								  || (prediction.velocity * (1.0f / predictedSpeed)).Dot(actual.velocity * (1.0f / actualSpeed))
										 < kRampBugThreshold;
	const bool lostSpeed = predictedSpeed > 50.0f && actualSpeed / predictedSpeed < kVelocityRatioThreshold;
	if (!changedDirection && !lostSpeed)
		return {};
	return {true, true, prediction.origin, prediction.velocity};
}

Correction CategorizePosition(const State &state, const MovementContext &context, TraceProvider &provider, bool stayOnGround)
{
	if (!ActiveContext(state, context) || stayOnGround || !UsablePlane(state.lastValidPlane)
		|| state.lastValidPlane.z > 0.7f || context.velocity.z > -64.0f)
		return {};
	Vec3 down = context.origin - Vec3{0.0f, 0.0f, kGroundTraceDistance};
	Trace ground;
	bool failed = false;
	if (!ReadTrace(provider, context.origin, down, context.bounds, ground, failed)
		|| !ValidMovementTrace(ground, context.bounds, provider, failed) || failed || ground.fraction >= 0.95f
		|| ground.normal.z <= 0.7f || state.lastValidPlane.Dot(ground.normal) >= kRampBugThreshold)
		return {};
	const Vec3 origin = context.origin + state.lastValidPlane * kGroundNudgeDistance;
	down = origin - Vec3{0.0f, 0.0f, kGroundTraceDistance};
	if (!ReadTrace(provider, origin, down, context.bounds, ground, failed)
		|| !ValidMovementTrace(ground, context.bounds, provider, failed) || failed)
		return {};
	if (ground.fraction == 1.0f || state.lastValidPlane.Dot(ground.normal) >= kRampBugThreshold)
		return {true, false, origin, context.velocity};
	return {};
}
} // namespace rampbugfix
