// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <khook.hpp>

namespace rampbugfix
{
// Configure() acknowledges registration, not completed installation. KHook may
// queue even a nominally synchronous request. The module remains inert until
// both callbacks have actually run. Configure(nullptr) is NOT an uninstall
// API, so shutdown uses KHook's synchronous removal path, as its own Member
// destructor does, before plugin-owned managers are released.
template <typename... Args>
class CheckedMember : public KHook::Member<CCSPlayer_MovementServices, void, Args...>
{
    using Base = KHook::Member<CCSPlayer_MovementServices, void, Args...>;

public:
    using Base::Base;

    bool Register(void* address)
    {
        if (!address || HasRegistrations())
            return false;
        Base::Configure(address);
        return HasRegistrations();
    }

    bool HasRegistrations()
    {
        std::lock_guard<std::mutex> guard(this->_hooks_stored);
        return !this->_hook_ids.empty();
    }

    bool RemoveAll()
    {
        std::unordered_set<KHook::HookID_t> ids;
        {
            std::lock_guard<std::mutex> guard(this->_hooks_stored);
            ids = this->_hook_ids;
        }
        for (auto id : ids)
            KHook::RemoveHook(id, false);
        return !HasRegistrations();
    }
};
} // namespace rampbugfix
