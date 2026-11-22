/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023-2026 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hud_manager.h"
#include "map_countdown.h"
#include "../cs2fixes.h"
#include "../ctimer.h"
#include "entity/ccsplayercontroller.h"
#include "../recipientfilters.h"
#include "engine2/igameeventsystem.h"
#include "entity/cgamerules.h"
#include "gameevents.pb.h"
#include "networksystem/inetworkmessages.h"
#include <array>

CConVar<bool> g_cvarFixHudFlashing("cs2f_fix_hud_flashing", FCVAR_NONE, "Whether to fix hud flashing using a workaround, this BREAKS warmup so pick one or the other", false);
CConVar<bool> g_cvarDisableHudOutsideRound("cs2f_disable_hud_outside_round", FCVAR_NONE, "Whether to disable hud messages that would flash when a round is not ongoing, since flashing fix cannot run then", false);
CConVar<int> g_cvarHudDurationLeeway("cs2f_hud_duration_leeway", FCVAR_NONE, "Extra seconds duration to leave hud messages visible (without priority), reduces transition flashes between different priority messages", 2);

static std::vector<std::shared_ptr<CHudMessage>> g_vecHudMessages;
static std::array<ozlin::map_countdown::PopupGuard, ABSOLUTE_PLAYER_LIMIT> g_countdownPopupGuards;

void ResetMapCountdownHudFrames()
{
	g_countdownPopupGuards = {};
}

bool ShouldDisplayForPlayer(ZEPlayerHandle hPlayer, EHudPriority ePriority)
{
	for (std::shared_ptr<CHudMessage> pHudMessage : g_vecHudMessages)
	{
		// Require higher priority to block, so if priority matches most recent message takes precedence
		if (pHudMessage->HasRecipient(hPlayer) && pHudMessage->GetPriority() > ePriority)
			return false;
	}

	return true;
}

void CreateHudMessage(std::shared_ptr<CHudMessage> pHudMessage, bool bMapCountdownFrame = false)
{
	if (!g_pGameRules || (bMapCountdownFrame && (!g_gameEventManager || !g_gameEventSystem || !g_pNetworkMessages)))
		return;

	if (g_cvarDisableHudOutsideRound.Get() && !g_pGameRules->m_bGameRestart())
		return;

	CRecipientFilter filter;

	bool bHasRecipient = false;
	std::vector<ZEPlayerHandle> priorityRecipients;
	for (ZEPlayerHandle hPlayer : pHudMessage->GetRecipients())
	{
		auto* pPlayer = hPlayer.Get();
		if (!pPlayer || !ShouldDisplayForPlayer(hPlayer, pHudMessage->GetPriority()))
			continue;
		const auto slot = pPlayer->GetPlayerSlot();
		if (!slot.IsValid())
			continue;
		if (bMapCountdownFrame && g_countdownPopupGuards[slot.Get()].Blocks(hPlayer.GetIndex(), g_flUniversalTime))
			continue;

		filter.AddRecipient(slot);
		bHasRecipient = true;
		if (!bMapCountdownFrame && pHudMessage->GetPriority() > EHudPriority::MapCountdown)
			priorityRecipients.push_back(hPlayer);
	}

	if (bMapCountdownFrame && !bHasRecipient)
		return;

	static IGameEvent* pEvent = nullptr;

	if (!pEvent)
		pEvent = g_gameEventManager->CreateEvent("show_survival_respawn_status");

	if (!pEvent)
		return;

	const int iVisibleDuration = pHudMessage->GetDuration() + (bMapCountdownFrame ? 0 : g_cvarHudDurationLeeway.Get());
	pEvent->SetString("loc_token", pHudMessage->GetMessage());
	pEvent->SetInt("duration", iVisibleDuration);
	pEvent->SetInt("userid", -1);

	NetMessageInfo_t* pMsg = g_pNetworkMessages->FindNetworkMessageById(GE_Source1LegacyGameEvent);

	if (!pMsg)
		return;

	auto* pMessage = pMsg->AllocateMessage();
	if (!pMessage)
		return;
	CNetMessagePB<CMsgSource1LegacyGameEvent>* data = pMessage->ToPB<CMsgSource1LegacyGameEvent>();

	// A low-priority one-frame countdown never reserves the HUD or delays other messages.
	if (!bMapCountdownFrame)
	{
		for (auto handle : priorityRecipients)
			g_countdownPopupGuards[handle.GetPlayerSlot()].Reserve(handle.GetIndex(), g_flUniversalTime, iVisibleDuration);
		g_vecHudMessages.push_back(pHudMessage);
		CTimer::Create(pHudMessage->GetDuration(), TIMERFLAG_NONE, [pHudMessage]() {
			g_vecHudMessages.erase(std::remove(g_vecHudMessages.begin(), g_vecHudMessages.end(), pHudMessage), g_vecHudMessages.end());
			return -1.0f;
		});
	}

	g_gameEventManager->SerializeEvent(pEvent, data);
	g_gameEventSystem->PostEventAbstract(-1, false, &filter, pMsg, data, 0);
	delete data;
}

void SendHudMessage(ZEPlayer* pPlayer, int iDuration, EHudPriority ePriority, const char* pszMessage, ...)
{
	va_list args;
	va_start(args, pszMessage);

	char buf[1024];
	V_vsnprintf(buf, sizeof(buf), pszMessage, args);

	va_end(args);

	std::shared_ptr<CHudMessage> pHudMessage = std::make_shared<CHudMessage>(buf, iDuration, ePriority);

	pHudMessage->AddRecipient(pPlayer->GetHandle());
	CreateHudMessage(pHudMessage);
}

void SendHudMessageAll(int iDuration, EHudPriority ePriority, const char* pszMessage, ...)
{
	if (!GetGlobals())
		return;

	va_list args;
	va_start(args, pszMessage);

	char buf[1024];
	V_vsnprintf(buf, sizeof(buf), pszMessage, args);

	va_end(args);

	std::shared_ptr<CHudMessage> pHudMessage = std::make_shared<CHudMessage>(buf, iDuration, ePriority);

	for (int i = 0; i < GetGlobals()->maxClients; i++)
	{
		ZEPlayer* pPlayer = g_playerManager->GetPlayer(i);

		if (pPlayer)
			pHudMessage->AddRecipient(pPlayer->GetHandle());
	}

	CreateHudMessage(pHudMessage);
}

void SendMapCountdownHudFrame(const std::vector<ZEPlayerHandle>& recipients, const char* pszHtml)
{
	if (!GetGlobals() || !g_playerManager || !pszHtml || !*pszHtml)
		return;

	auto pHudMessage = std::make_shared<CHudMessage>(pszHtml, ozlin::map_countdown::kHudFrameSeconds, EHudPriority::MapCountdown);
	for (auto handle : recipients)
	{
		auto* pPlayer = handle.Get(); // Serial check: never retarget to a re-used slot.
		if (!pPlayer || pPlayer->IsFakeClient())
			continue;
		auto* pController = CCSPlayerController::FromSlot(pPlayer->GetPlayerSlot());
		if (pController && pController->IsConnected() && !pController->IsBot() && !pController->m_bIsHLTV())
			pHudMessage->AddRecipient(handle);
	}
	CreateHudMessage(pHudMessage, true);
}

void StartFlashingFixTimer()
{
	// Timer that fakes m_bGameRestart enabled, to fix flashing with show_survival_respawn_status
	CTimer::Create(0.5f, TIMERFLAG_MAP, []() {
		if (!g_cvarFixHudFlashing.Get() || !g_pGameRules)
			return 0.5f;

		// Faking m_bGameRestart as true close to a round ending causes UI to falsely show game is restarting
		if (g_pGameRules->m_flRestartRoundTime.Get().GetTime() == 0.0f)
			g_pGameRules->m_bGameRestart = true;
		else
			g_pGameRules->m_bGameRestart = false;

		return 0.5f;
	});
}

std::string EscapeHTMLSpecialCharacters(std::string strMsg)
{
	// Always replace & first, as it is used in html escaped characters (so dont want to replace inside an escape)
	for (size_t iPos = 0; (iPos = strMsg.find('&', iPos)) != std::string::npos; iPos += 5)
		strMsg.replace(iPos, 1, "&amp;");

	std::unordered_map<std::string, std::string> mapReplacements{
		{"<",  "&lt;"	},
		{">",  "&gt;"	},
		{"\"", "&quot;"},
		{"\'", "&apos;"}
	};

	for (const auto& [strBadChar, strEscapedChar] : mapReplacements)
		for (size_t iPos = 0; (iPos = strMsg.find(strBadChar, iPos)) != std::string::npos; iPos += strEscapedChar.length())
			strMsg.replace(iPos, strBadChar.length(), strEscapedChar);

	return strMsg;
}