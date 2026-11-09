// SPDX-License-Identifier: GPL-3.0-only
#pragma once

class CCSPlayer_MovementServices;

namespace rampbugfix
{
// Optional module: a missing/unknown ABI profile never fails CS2Fixes startup.
void Initialize();
// Returns false while KHook still owns callbacks. Do not unload the DLL then.
bool Shutdown();
void OnFrame();
void ResetAll();
void ResetPlayer(int slot);

// Called by the EXISTING ProcessMovement hook, never a second detour.
void EnterMovement(CCSPlayer_MovementServices* services, void* move);
void PrepareMovement(CCSPlayer_MovementServices* services, void* move);
void LeaveMovement(CCSPlayer_MovementServices* services, void* move);
} // namespace rampbugfix
