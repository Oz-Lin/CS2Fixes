// SPDX-License-Identifier: GPL-3.0-only
// Optional engine adapter for the legacy RampBugFix algorithm. See
// docs/rampbugfix-integration.md for provenance, exact ABI profiles and limits.
#include "module.h"
#include "core.h"
#include "lifecycle.h"
#include "../addresses.h"
#include "../cs2fixes.h"
#include "entity/ccsplayercontroller.h"
#include "entity/ccsplayerpawn.h"
#include "entity/services.h"
#include "gametrace.h"
#include "tier0/convar.h"
#include "checked_hook.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace rampbugfix
{
namespace
{
struct Profile
{
    std::uint64_t fileSize;
    std::uint64_t fileHash;
    const char* tryPlayerMove;
    const char* categorizePosition;
    const char* traceBounds;
    const char* initFilter;
    const char* hullMins;
    const char* hullMaxs;
    const char* movementMask;
};

// Hashes identify an audited build, not a security boundary. Never broaden a
// profile on the strength of a matching signature alone.
#ifdef _WIN32
constexpr Profile kProfiles[]{ {
    34672792, UINT64_C(0x64e05e7cdc591254),
    "48 8B C4 4C 89 48 ?? 4C 89 40 ?? 48 89 50 ?? 48 89 48 ?? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 0F 29 70",
    "40 55 56 57 41 54 41 55 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 48 8B F9",
    "48 8B C4 4C 89 40 ?? 55 53 57 41 55 41 56 48 8D A8 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 49 8B 79 10 48 8B DA 4D 8B F1 4C 8B E9 49 8B D0",
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 0F B6 41 39 33 FF C7 41 34 FF FF 00 0F 24 C9 66 C7 41 37 0F 03",
    "40 53 48 83 EC 20 48 83 79 38 00 48 8B D9 75 05 E8 ?? ?? ?? ?? 48 8B 4B 38 E8 ?? ?? ?? ?? 84 C0 74 0F E8 ?? ?? ?? ?? 48 83 C0 48",
    "40 53 48 83 EC 20 48 83 79 38 00 48 8B D9 75 05 E8 ?? ?? ?? ?? 48 8B 4B 38 E8 ?? ?? ?? ?? 84 C0 74 0F E8 ?? ?? ?? ?? 48 83 C0 54",
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 83 79 38 00 48 8B F9 75 05 E8 ?? ?? ?? ?? 48 8B 4F 38 48 8D 77 38 48 85 C9 74 16 48 8B 01 FF 90 20 0C 00 00"
} };
#else
// Static audits of the exact Linux binaries prove all seven targets and the
// layouts below. Identity and unique-signature checks still fail closed.
constexpr Profile kProfiles[]{ {
    41807736, UINT64_C(0xde50ca3604b5443f),
    "48 B8 ?? ?? ?? ?? ?? ?? ?? ?? 55 66 0F EF C0 48 89 E5 41 57 41 56 49 89 F6",
    "48 B8 ?? ?? ?? ?? ?? ?? ?? ?? 55 66 0F EF C0 48 89 E5 41 57 41 56 41 55 41 89 D5",
    "55 48 89 E5 41 57 49 89 FF 41 56 49 89 D6 41 55 41 54 49 89 F4 53 48 89 CB 48 81 EC ?? ?? ?? ??",
    "48 B8 ?? ?? ?? ?? ?? ?? ?? ?? 55 66 0F EF C0 48 89 E5 41 55 41 54 49 89 F4 53 48 89 FB 48 83 EC ?? 48 89 47 30 B8 00 49 00 00 66 89 47 38 48 8D 05 ?? ?? ?? ??",
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 48 8B 7F 38 48 85 FF 74 4B E8 ?? ?? ?? ?? 84 C0 75 32 80 BB 08 04 00 00 00 75 19 E8 ?? ?? ?? ?? 48 8B 5D F8 C9 48 83 C0 0C C3",
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 48 8B 7F 38 48 85 FF 74 4B E8 ?? ?? ?? ?? 84 C0 75 32 80 BB 08 04 00 00 00 75 19 E8 ?? ?? ?? ?? 48 8B 5D F8 C9 48 83 C0 18 C3",
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 48 8B 7F 38 48 85 FF 74 43 48 8B 07 48 8D 15 ?? ?? ?? ?? 48 8B 80 28 0C 00 00 48 39 D0 75 55"
}, {
    41850680, UINT64_C(0x96c2563722232d09),
    "48 B8 ?? ?? ?? ?? ?? ?? ?? ?? 55 66 0F EF C0 48 89 E5 41 57 41 56 49 89 F6",
    "48 B8 ?? ?? ?? ?? ?? ?? ?? ?? 55 66 0F EF C0 48 89 E5 41 57 41 56 41 55 41 89 D5",
    "55 48 89 E5 41 57 49 89 FF 41 56 49 89 D6 41 55 41 54 49 89 F4 53 48 89 CB 48 81 EC ?? ?? ?? ??",
    "48 B8 ?? ?? ?? ?? ?? ?? ?? ?? 55 66 0F EF C0 48 89 E5 41 55 41 54 49 89 F4 53 48 89 FB 48 83 EC ?? 48 89 47 30 B8 00 49 00 00 66 89 47 38 48 8D 05 ?? ?? ?? ??",
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 48 8B 7F 38 48 85 FF 74 4B E8 ?? ?? ?? ?? 84 C0 75 32 80 BB 08 04 00 00 00 75 19 E8 ?? ?? ?? ?? 48 8B 5D F8 C9 48 83 C0 0C C3",
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 48 8B 7F 38 48 85 FF 74 4B E8 ?? ?? ?? ?? 84 C0 75 32 80 BB 08 04 00 00 00 75 19 E8 ?? ?? ?? ?? 48 8B 5D F8 C9 48 83 C0 18 C3",
    "55 48 89 E5 53 48 89 FB 48 83 EC 08 48 8B 7F 38 48 85 FF 74 43 48 8B 07 48 8D 15 ?? ?? ?? ?? 48 8B 80 28 0C 00 00 48 39 D0 75 55"
} };
#endif

static_assert(sizeof(Vector) == 12 && sizeof(Vec3) == 12);
static_assert(sizeof(trace_t) == 192);
static_assert(alignof(trace_t) == 16);
static_assert(offsetof(trace_t, m_vStartPos) == 0x78);
static_assert(offsetof(trace_t, m_vEndPos) == 0x84);
static_assert(offsetof(trace_t, m_vHitNormal) == 0x90);
static_assert(offsetof(trace_t, m_flFraction) == 0xAC);
static_assert(offsetof(trace_t, m_bStartInSolid) == 0xBB);
#ifdef _WIN32
static_assert(sizeof(CTraceFilter) == 72);
#else
static_assert(sizeof(CTraceFilter) == 64);
static_assert(alignof(CTraceFilter) == 8);
#endif

using InitFilterFn = void (*)(CTraceFilter*, CCSPlayerPawn*, uint64, int32_t);
using TraceBoundsFn = void (*)(const Vector*, const Vector*, const Vector*, CTraceFilter*, trace_t*);
using HullFn = const Vector* (*)(CCSPlayer_MovementServices*);
using MaskFn = uint64 (*)(CCSPlayer_MovementServices*);
InitFilterFn s_initFilter = nullptr;
TraceBoundsFn s_traceBounds = nullptr;
HullFn s_hullMins = nullptr;
HullFn s_hullMaxs = nullptr;
MaskFn s_movementMask = nullptr;

std::thread::id s_gameThread;
AdapterLifecycle s_lifecycle;
std::atomic<bool> s_togglePending{false};
bool s_toggleWarning = false;
std::uint64_t s_nextMovement = 0;

struct PlayerState
{
    State core;
    CHandle<CCSPlayerPawn> pawn;
    Vec3 lastEnd;
    bool haveLastEnd = false;
};
std::array<PlayerState, MAX_PLAYERS> s_players{};

struct Scope
{
    CCSPlayer_MovementServices* services = nullptr;
    void* move = nullptr;
    int slot = -1;
    std::uint64_t id = 0;
    Vec3 pawnOriginBeforeTry;
    bool predicting = false;
};
std::array<Scope, 8> s_scopes{};
unsigned s_depth = 0;
unsigned s_overflow = 0;

bool OnGameThread() { return std::this_thread::get_id() == s_gameThread; }

void ApplyToggleChange()
{
    s_togglePending = false;
    s_lifecycle.ToggleChanged();
    ResetAll();
}

void EnableChanged(CConVar<bool>*, CSplitScreenSlot, const bool*, const bool*)
{
    // A cvar callback may come from outside the game thread. Enabled() treats a
    // pending transition as off until the game thread invalidates observations.
    s_togglePending = true;
    if (OnGameThread())
        ApplyToggleChange();
}

CConVar<bool> s_enable("cs2f_rampbugfix_enable", FCVAR_NONE,
    "Experimental legacy ramp fix; default off. Requires an audited ABI and enable in cs2fixes.cfg before plugin load. Runtime 0 stops corrections; unload removes callbacks.",
    false, EnableChanged);

bool Enabled() { return !s_togglePending && s_lifecycle.CanCorrect(s_enable.Get()); }

Vec3 ToCore(const Vector& v) { return {v.x, v.y, v.z}; }
Vector ToEngine(const Vec3& v) { return Vector(v.x, v.y, v.z); }

bool ReadPawnOrigin(const CHandle<CCSPlayerPawn>& handle, Vec3& origin)
{
    auto* pawn = handle.Get();
    if (!pawn)
        return false;
    CBodyComponent* body = pawn->m_CBodyComponent.Get();
    if (!body)
        return false;
    CGameSceneNode* node = body->m_pSceneNode.Get();
    if (!node)
        return false;
    origin = ToCore(node->m_vecAbsOrigin.Get());
    return origin.IsFinite();
}

Vec3 ReadMoveVector(void* move, size_t offset)
{
    Vec3 result;
    std::memcpy(&result, static_cast<unsigned char*>(move) + offset, sizeof(result));
    return result;
}

void WriteMoveVector(void* move, size_t offset, const Vec3& value)
{
    std::memcpy(static_cast<unsigned char*>(move) + offset, &value, sizeof(value));
}

Trace ToCore(const trace_t& trace)
{
    return {ToCore(trace.m_vStartPos), ToCore(trace.m_vEndPos), ToCore(trace.m_vHitNormal),
        trace.m_flFraction, trace.m_bStartInSolid, false};
}

Scope* CurrentScope(CCSPlayer_MovementServices* services, void* move)
{
    if (!OnGameThread() || !Enabled() || s_overflow || !s_depth)
        return nullptr;
    auto& scope = s_scopes[s_depth - 1];
    if (scope.services != services || scope.move != move || scope.slot < 0 || !scope.id)
        return nullptr;
    auto& player = s_players[scope.slot];
    if (!player.core.processingMovement || player.core.movementId != scope.id)
        return nullptr;
    auto* pawn = player.pawn.Get();
    if (!pawn || pawn != services->GetPawn() || !pawn->IsAlive() || pawn->m_nActualMoveType() != MOVETYPE_WALK)
    {
        player.core.Reset();
        return nullptr;
    }
    return &scope;
}

MovementContext Context(const Scope& scope)
{
    MovementContext context;
    auto* pawn = s_players[scope.slot].pawn.Get();
    auto* globals = GetGlobals();
    if (!pawn || !globals || !s_hullMins || !s_hullMaxs)
        return context;
    const Vector* mins = s_hullMins(scope.services);
    if (!mins)
        return context;
    context.bounds.mins = ToCore(*mins);
    const Vector* maxs = s_hullMaxs(scope.services);
    if (!maxs)
        return context;
    context.bounds.maxs = ToCore(*maxs);
    context.valid = true;
    context.movementId = scope.id;
    context.frameTime = globals->frametime;
    // These two offsets are independently proven in each allowed binary. Do
    // not copy the old CMoveData class (its origin member is at the WRONG 0xB8).
    context.origin = ReadMoveVector(scope.move, 0xC8);
    context.velocity = ReadMoveVector(scope.move, 0x38);
    context.walking = pawn->m_nActualMoveType() == MOVETYPE_WALK;
    context.onGround = (pawn->m_fFlags() & FL_ONGROUND) != 0;
    return context;
}

class EngineTraceProvider final : public TraceProvider
{
public:
    explicit EngineTraceProvider(Scope& scope) : m_scope(scope), m_id(scope.id) {}

    bool TraceHull(const Vec3& start, const Vec3& end, const Bounds& bounds, Trace& result) override
    {
        if (m_scope.id != m_id || !CurrentScope(m_scope.services, m_scope.move))
            return false;
        auto* pawn = s_players[m_scope.slot].pawn.Get();
        if (!pawn || !s_initFilter || !s_traceBounds || !s_movementMask)
            return false;
        Vector nativeStart = ToEngine(start);
        Vector nativeEnd = ToEngine(end);
        Vector nativeBounds[2]{ToEngine(bounds.mins), ToEngine(bounds.maxs)};
        alignas(16) std::array<unsigned char, sizeof(CTraceFilter)> filterStorage{};
        auto* filter = reinterpret_cast<CTraceFilter*>(filterStorage.data());
        s_initFilter(filter, pawn, s_movementMask(m_scope.services), COLLISION_GROUP_PLAYER_MOVEMENT);
        trace_t trace;
        // This is the verified UNCACHED five-argument helper. A null/fabricated
        // cache passed to the six-argument helper would crash the current game.
        s_traceBounds(&nativeStart, &nativeEnd, nativeBounds, filter, &trace);
        if (m_scope.id != m_id || !CurrentScope(m_scope.services, m_scope.move))
            return false;
        result = ToCore(trace);
        return true;
    }

private:
    Scope& m_scope;
    std::uint64_t m_id;
};

void Apply(const Scope& scope, const Correction& correction)
{
    if (!correction.apply || !correction.origin.IsFinite() ||
        (correction.changeVelocity && !correction.velocity.IsFinite()))
        return;
    WriteMoveVector(scope.move, 0xC8, correction.origin);
    if (correction.changeVelocity)
        WriteMoveVector(scope.move, 0x38, correction.velocity);
}

KHook::Return<void> TryPre(CCSPlayer_MovementServices* services, void* move, Vector* firstDest, trace_t* firstTrace, bool* isSurfing)
{
    // isSurfing is deliberately untouched. KHook forwards ALL five arguments
    // to the engine and subsequent callbacks, including the nullable output.
    (void)isSurfing;
    if (!OnGameThread())
        return {KHook::Action::Ignore};
    s_lifecycle.ObserveTryPlayerMove(s_enable.Get());
    auto* scope = CurrentScope(services, move);
    if (!scope)
        return {KHook::Action::Ignore};
    auto& player = s_players[scope->slot];
    if (scope->predicting)
    {
        player.core.Reset(); // Recursive/foreign recall: fail closed.
        return {KHook::Action::Ignore};
    }
    scope->predicting = true;
    if (!ReadPawnOrigin(player.pawn, scope->pawnOriginBeforeTry))
    {
        player.core.Reset();
        scope->predicting = false;
        return {KHook::Action::Ignore};
    }
    EngineTraceProvider provider(*scope);
    Vec3 destination;
    Trace trace;
    const bool havePair = firstDest && firstTrace;
    if (havePair)
    {
        destination = ToCore(*firstDest);
        trace = ToCore(*firstTrace);
    }
    PredictTryPlayerMove(player.core, Context(*scope), provider,
        havePair ? &destination : nullptr, havePair ? &trace : nullptr);
    return {KHook::Action::Ignore};
}

KHook::Return<void> TryPost(CCSPlayer_MovementServices* services, void* move, Vector*, trace_t*, bool*)
{
    auto* scope = CurrentScope(services, move);
    if (!scope)
        return {KHook::Action::Ignore};
    auto& player = s_players[scope->slot];
    Vec3 pawnOriginAfterTry;
    if (!ReadPawnOrigin(player.pawn, pawnOriginAfterTry))
    {
        player.core.Reset();
        scope->predicting = false;
        return {KHook::Action::Ignore};
    }
    if (!scope->predicting || KHook::WasOriginalFunctionSkipped() ||
        !(pawnOriginAfterTry == scope->pawnOriginBeforeTry))
    {
        player.core.Reset(); // Includes teleports performed during this call.
        scope->predicting = false;
        return {KHook::Action::Ignore};
    }
    scope->predicting = false;
    Apply(*scope, FinishTryPlayerMove(player.core, Context(*scope)));
    return {KHook::Action::Ignore};
}

KHook::Return<void> CategorizePre(CCSPlayer_MovementServices* services, void* move, bool stayOnGround)
{
    if (!OnGameThread())
        return {KHook::Action::Ignore};
    s_lifecycle.ObserveCategorizePosition(s_enable.Get());
    auto* scope = CurrentScope(services, move);
    if (scope && !scope->predicting)
    {
        EngineTraceProvider provider(*scope);
        auto correction = CategorizePosition(s_players[scope->slot].core, Context(*scope), provider, stayOnGround);
        if (CurrentScope(services, move))
            Apply(*scope, correction);
    }
    return {KHook::Action::Ignore};
}

CheckedMember<void*, Vector*, trace_t*, bool*> s_tryHook(TryPre, TryPost);
CheckedMember<void*, bool> s_categorizeHook(CategorizePre, nullptr);

// Bounded scanner: reject missing AND ambiguous targets. Do not chase a
// foreign detour, overwrite a trampoline, or borrow a signature from another
// platform. Raw-prologue checks intentionally refuse already patched entries.
void* FindUnique(const char* signature)
{
    if (!signature || !modules::server)
        return nullptr;
    auto* section = modules::server->GetSection(".text");
    if (!section)
        return nullptr;
    std::vector<int> pattern;
    const char* cursor = signature;
    while (*cursor)
    {
        if (*cursor == ' ') { ++cursor; continue; }
        if (*cursor == '?')
        {
            pattern.push_back(-1);
            while (*cursor == '?') ++cursor;
        }
        else
        {
            unsigned value;
            if (std::sscanf(cursor, "%2x", &value) != 1 || !cursor[1])
                return nullptr;
            pattern.push_back(static_cast<int>(value));
            cursor += 2;
        }
    }
    if (pattern.empty() || pattern.size() > section->m_iSize)
        return nullptr;
    auto* bytes = static_cast<unsigned char*>(section->m_pBase);
    void* found = nullptr;
    for (size_t i = 0; i <= section->m_iSize - pattern.size(); ++i)
    {
        size_t j = 0;
        while (j < pattern.size() && (pattern[j] < 0 || bytes[i + j] == pattern[j])) ++j;
        if (j != pattern.size()) continue;
        if (found) return nullptr;
        found = bytes + i;
    }
    return found;
}

const Profile* MatchBinary()
{
    char path[MAX_PATH];
    V_snprintf(path, sizeof(path), "%s%s%sserver%s", Plat_GetGameDirectory(), GAMEBIN, MODULE_PREFIX, MODULE_EXT);
    FILE* file = std::fopen(path, "rb");
    if (!file) return nullptr;
    std::uint64_t size = 0;
    std::uint64_t hash = UINT64_C(14695981039346656037);
    std::array<unsigned char, 65536> buffer;
    size_t count;
    while ((count = std::fread(buffer.data(), 1, buffer.size(), file)) != 0)
    {
        size += count;
        for (size_t i = 0; i < count; ++i)
        {
            hash ^= buffer[i];
            hash *= UINT64_C(1099511628211);
        }
    }
    const bool readOk = !std::ferror(file);
    std::fclose(file);
    if (!readOk) return nullptr;
    for (const auto& profile : kProfiles)
    {
        if (profile.fileSize && profile.movementMask && size == profile.fileSize && hash == profile.fileHash)
            return &profile;
    }
    return nullptr;
}
} // namespace

void ResetPlayer(int slot)
{
    if (slot < 0 || static_cast<size_t>(slot) >= s_players.size())
        return;
    s_players[slot] = PlayerState{};
}

void ResetAll()
{
    for (auto& player : s_players) player = PlayerState{};
    // Do not destroy the balanced enter/leave stack while a cvar is changed
    // from an engine callback. Invalidate its IDs instead.
    for (auto& scope : s_scopes) scope.id = 0;
}

void Initialize()
{
    s_gameThread = std::this_thread::get_id();
    s_lifecycle.Reset();
    s_togglePending = false;
    s_toggleWarning = false;
    ResetAll();
    if (!s_enable.Get())
    {
        Message("RampBugFix disabled (default); no optional hooks registered.\n");
        return;
    }
    const Profile* profile = MatchBinary();
    if (!profile)
    {
        Message("RampBugFix disabled: server binary has no complete audited ABI profile. CS2Fixes continues.\n");
        return;
    }
    if (!profile->categorizePosition)
    {
        Message("RampBugFix disabled: audited profile lacks a unique CategorizePosition target. CS2Fixes continues.\n");
        return;
    }
    void* tryAddress = FindUnique(profile->tryPlayerMove);
    void* categorizeAddress = FindUnique(profile->categorizePosition);
    s_traceBounds = reinterpret_cast<TraceBoundsFn>(FindUnique(profile->traceBounds));
    s_initFilter = reinterpret_cast<InitFilterFn>(FindUnique(profile->initFilter));
    s_hullMins = reinterpret_cast<HullFn>(FindUnique(profile->hullMins));
    s_hullMaxs = reinterpret_cast<HullFn>(FindUnique(profile->hullMaxs));
    s_movementMask = reinterpret_cast<MaskFn>(FindUnique(profile->movementMask));
    if (!tryAddress || !categorizeAddress || !s_traceBounds || !s_initFilter || !s_hullMins || !s_hullMaxs || !s_movementMask)
    {
        Message("RampBugFix disabled: missing, ambiguous or already patched target; no hooks registered.\n");
        return;
    }
    const HookPairResult registration = RegisterHookPair(s_tryHook, tryAddress, s_categorizeHook, categorizeAddress);
    if (!registration.accepted)
    {
        s_lifecycle.RegistrationFailed();
        Message(registration.rolledBack
            ? "RampBugFix disabled: hook registration failed; partial registration rolled back.\n"
            : "RampBugFix disabled: hook registration failed and synchronous rollback was incomplete.\n");
        return;
    }
    s_lifecycle.RegistrationAccepted();
    Message("RampBugFix registered, pending both live callbacks; corrections remain off until the next safe frame.\n");
}

bool Shutdown()
{
    s_lifecycle.Stop();
    ResetAll();
    // Synchronous removal waits for the detour lock. A normal unload requested
    // from inside movement must be retried after that callback unwinds.
    if (s_depth || s_overflow)
        return false;
    const bool tryRemoved = s_tryHook.RemoveAll();
    const bool categorizeRemoved = s_categorizeHook.RemoveAll();
    return tryRemoved && categorizeRemoved;
}

void OnFrame()
{
    if (!OnGameThread() || s_lifecycle.IsStopping())
        return;
    if (s_togglePending)
        ApplyToggleChange();
    if (!s_enable.Get())
    {
        s_lifecycle.OnFrame(false, s_depth == 0);
        return;
    }
    if (!s_lifecycle.IsPrepared())
    {
        if (!s_toggleWarning)
        {
            Message("RampBugFix unavailable: enable in cs2fixes.cfg before load, with a supported unpatched binary.\n");
            s_toggleWarning = true;
        }
        return;
    }
    if (s_lifecycle.OnFrame(true, s_depth == 0))
    {
        ResetAll();
        Message("RampBugFix corrections enabled (experimental); runtime cs2f_rampbugfix_enable 0 stops corrections.\n");
    }
}

void EnterMovement(CCSPlayer_MovementServices* services, void* move)
{
    if (!OnGameThread() || !Enabled()) return;
    if (s_depth == s_scopes.size() || s_overflow)
    {
        ++s_overflow;
        ResetAll();
        return;
    }
    s_scopes[s_depth++] = Scope{services, move};
}

void PrepareMovement(CCSPlayer_MovementServices* services, void* move)
{
    if (!OnGameThread() || !Enabled() || !services || !move || !s_depth || s_overflow)
        return;
    auto& scope = s_scopes[s_depth - 1];
    if (scope.services != services || scope.move != move)
        return;
    auto* pawn = services->GetPawn();
    if (!pawn || !pawn->IsAlive() || pawn->m_nActualMoveType() != MOVETYPE_WALK)
        return;
    auto* controller = pawn->GetOriginalController();
    if (!controller || !controller->IsConnected()) return;
    const int slot = controller->GetPlayerSlot();
    if (slot < 0 || static_cast<size_t>(slot) >= s_players.size()) return;
    auto& player = s_players[slot];
    const Vec3 origin = ReadMoveVector(move, 0xC8);
    // No plane may carry through an out-of-band origin change, pawn replacement
    // or nested processing of the same player. Every prediction is also scoped.
    if (player.pawn.Get() != pawn || player.core.processingMovement ||
        (player.haveLastEnd && !(origin == player.lastEnd)))
        player = PlayerState{};
    player.pawn = pawn->GetHandle();
    scope.slot = slot;
    if (++s_nextMovement == 0) ++s_nextMovement;
    scope.id = s_nextMovement;
    if (!BeginMovement(player.core, Context(scope))) scope.id = 0;
}

void LeaveMovement(CCSPlayer_MovementServices* services, void* move)
{
    if (!OnGameThread()) return;
    if (s_overflow) { --s_overflow; return; }
    if (!s_depth) return;
    auto& scope = s_scopes[s_depth - 1];
    if (scope.services != services || scope.move != move)
    {
        ResetAll();
        s_depth = 0;
        return;
    }
    if (scope.slot >= 0 && scope.id)
    {
        auto& player = s_players[scope.slot];
        if (player.core.movementId == scope.id)
        {
            player.lastEnd = ReadMoveVector(move, 0xC8);
            player.haveLastEnd = player.lastEnd.IsFinite();
            EndMovement(player.core, scope.id);
        }
    }
    scope = Scope{};
    --s_depth;
}
} // namespace rampbugfix
