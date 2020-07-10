#pragma once
#pragma once

#include "Interfaces/OnlinePresenceInterface.h"
#include "OnlineSubsystemEpic.h"
#include "eos_sdk.h"
#include "OnlineSubsystemEpicTypes.h"

class FOnlinePresenceEpic
	: public IOnlinePresence
{
private:
	FOnlineSubsystemEpic const* subsystem;

	EOS_HPresence presenceHandle;

	//We want to keep track of presence notifications for every new person we add to friend or meet in sessions
	TMap<FUniqueNetIdEpic, EOS_NotificationId> PresenceNotifications;

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

	/*
	 * Custom function made to have a way to unsubscribe to presence updates if one chooses to do so
	 *
	 * @param TagetUserId removing presence query for this person
	 */
	virtual void RemovePresenceQuery(const FUniqueNetId& TargetUserId);
};

typedef TSharedPtr<class FOnlinePresenceEpic, ESPMode::ThreadSafe> FOnlinePresenceEpicPtr;
