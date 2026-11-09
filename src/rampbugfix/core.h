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
 * Engine-independent adaptation of Interesting-exe/CS2Fixes-RampbugFix's
 * src/rbf-detours.cpp, with the legacy ramp fix credited in its README to zer0.
 * This is not an implementation of the newer CS2KZ ramp algorithm.
 */

#pragma once

#include <cstdint>

namespace rampbugfix
{
constexpr unsigned kMaxBumps = 4;
constexpr float kRampPierceDistance = 0.75f;
constexpr float kRampBugThreshold = 0.99f;
constexpr float kVelocityRatioThreshold = 0.95f;
constexpr float kNewRampThreshold = 0.95f;
constexpr float kGroundTraceDistance = 2.0f;
constexpr float kGroundNudgeDistance = 0.0625f;

struct Vec3
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	Vec3 operator+(const Vec3 &other) const;
	Vec3 operator-(const Vec3 &other) const;
	Vec3 operator*(float scale) const;
	bool operator==(const Vec3 &other) const;
	float Dot(const Vec3 &other) const;
	float Length() const;
	bool IsFinite() const;
};

struct Bounds
{
	Vec3 mins{-16.0f, -16.0f, 0.0f};
	Vec3 maxs{16.0f, 16.0f, 72.0f};
};

struct Trace
{
	Vec3 start;
	Vec3 end;
	Vec3 normal;
	float fraction = 1.0f;
	bool startSolid = false;
	bool allSolid = false;
};

class TraceProvider
{
public:
	virtual ~TraceProvider() = default;
	// Return false for unavailable/failed traces. Output must describe this exact
	// sweep (start matches exactly, end agrees with fraction within float error).
	// No engine object, filter, ABI, pointer or memory offset lives here.
	virtual bool TraceHull(const Vec3 &start, const Vec3 &end, const Bounds &bounds, Trace &out) = 0;
};

struct MovementContext
{
	// The adapter must explicitly establish a valid movement context. Supply a
	// nonzero, unique ID per ProcessMovement invocation, not just per server tick.
	bool valid = false;
	std::uint64_t movementId = 0;
	float frameTime = 0.0f;
	Vec3 origin;
	Vec3 velocity;
	Bounds bounds;
	bool walking = true;
	bool onGround = false;
};

struct Prediction
{
	bool valid = false;
	bool pierced = false;
	std::uint64_t movementId = 0;
	Vec3 origin;
	Vec3 velocity;
};

struct State
{
	Vec3 lastValidPlane;
	bool processingMovement = false;
	bool didTryPlayerMove = false;
	std::uint64_t movementId = 0;
	Prediction prediction;

	// Call for spawn, death, disconnect, map change, teleport, disable/unload and
	// any loss of trustworthy movement context. Never store engine pointers here.
	void Reset();
};

struct Correction
{
	// A proposal only. The adapter owns applying it to authoritative move data.
	bool apply = false;
	bool changeVelocity = false;
	Vec3 origin;
	Vec3 velocity;
};

bool BeginMovement(State &state, const MovementContext &context);
void EndMovement(State &state, std::uint64_t movementId);

// Clears any prior prediction first. A partial firstDest/firstTrace pair is
// ignored safely. Cached traces are used only for a matching first sweep.
bool PredictTryPlayerMove(State &state, const MovementContext &context, TraceProvider &provider,
						  const Vec3 *firstDest = nullptr, const Trace *firstTrace = nullptr);

// Consumes the prediction even on rejection. Actual movement is supplied in
// context; a prediction alone never reports that movement has been changed.
Correction FinishTryPlayerMove(State &state, const MovementContext &actual);

// Origin-only nudge before the engine's CategorizePosition. Requires an active
// movement scope and valid traces; leaves velocity and prediction untouched.
Correction CategorizePosition(const State &state, const MovementContext &context, TraceProvider &provider, bool stayOnGround);

// Exposed for deterministic algorithm tests. Returns zero for malformed input.
// Preserves the effective legacy '-1 / 128' integer-zero adjustment.
Vec3 ClipVelocity(const Vec3 &velocity, const Vec3 &normal);
} // namespace rampbugfix
