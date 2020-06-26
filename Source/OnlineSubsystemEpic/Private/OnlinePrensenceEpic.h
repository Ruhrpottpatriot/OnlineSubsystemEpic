#pragma once
#pragma once

#include "Interfaces/OnlinePresenceInterface.h"
#include "OnlineSubsystemEpic.h"
#include "eos_sdk.h"

class FOnlinePresenceEpic
	: public IOnlinePresence
{
private:
	FOnlineSubsystemEpic const* subsystem;

	EOS_HPresence presenceHandle;

	EOS_NotificationId OnPresenceChangedHandle;

	static void EOS_QueryPresenceComplete(EOS_Presence_QueryPresenceCallbackInfo const* data);
	static void EOS_OnPresenceChanged(EOS_Presence_PresenceChangedCallbackInfo const* data);
	static void EOS_SetPresenceComplete(EOS_Presence_SetPresenceCallbackInfo const* data);
	static void EOS_QueryExternalAccountMappingsForPresenceComplete(EOS_Connect_QueryExternalAccountMappingsCallbackInfo const* data);

	EOnlinePresenceState::Type EOSPresenceStateToUEPresenceState(EOS_Presence_EStatus status) const;

	EOS_Presence_EStatus UEPresenceStateToEOSPresenceState(EOnlinePresenceState::Type status) const;


public:
	FOnlinePresenceEpic(FOnlineSubsystemEpic const* InSubsystem);

	virtual void SetPresence(const FUniqueNetId& User, const FOnlineUserPresenceStatus& Status, const FOnPresenceTaskCompleteDelegate& Delegate = FOnPresenceTaskCompleteDelegate()) override;

	virtual void QueryPresence(const FUniqueNetId& User, const FOnPresenceTaskCompleteDelegate& Delegate = FOnPresenceTaskCompleteDelegate()) override;

	virtual EOnlineCachedResult::Type GetCachedPresence(const FUniqueNetId& User, TSharedPtr<FOnlineUserPresence>& OutPresence) override;

	virtual EOnlineCachedResult::Type GetCachedPresenceForApp(const FUniqueNetId& LocalUserId, const FUniqueNetId& User, const FString& AppId, TSharedPtr<FOnlineUserPresence>& OutPresence) override;
};
