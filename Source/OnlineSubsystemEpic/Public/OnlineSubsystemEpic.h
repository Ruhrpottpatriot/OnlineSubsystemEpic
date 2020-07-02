#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemEpicPackage.h"
#include "OnlineSubsystemImpl.h"
#include "eos_sdk.h"


using FOnlineIdentityEpicPtr = TSharedPtr<class FOnlineIdentityInterfaceEpic, ESPMode::ThreadSafe>;
using FOnlineSessionEpicPtr = TSharedPtr<class FOnlineSessionEpic, ESPMode::ThreadSafe>;
using FOnlineUserEpicPtr = TSharedPtr<class FOnlineUserEpic, ESPMode::ThreadSafe>;
using FOnlinePresenceEpicPtr = TSharedPtr<class FOnlinePresenceEpic, ESPMode::ThreadSafe>;

class ONLINESUBSYSTEMEPIC_API FOnlineSubsystemEpic
	: public FOnlineSubsystemImpl
{
public:
	// IOnlineSubsystem

	virtual IOnlineSessionPtr GetSessionInterface() const override;
	virtual IOnlineFriendsPtr GetFriendsInterface() const override;
	virtual IOnlinePartyPtr GetPartyInterface() const override;
	virtual IOnlineGroupsPtr GetGroupsInterface() const override;
	virtual IOnlineSharedCloudPtr GetSharedCloudInterface() const override;
	virtual IOnlineUserCloudPtr GetUserCloudInterface() const override;
	virtual IOnlineEntitlementsPtr GetEntitlementsInterface() const override;
	virtual IOnlineLeaderboardsPtr GetLeaderboardsInterface() const override;
	virtual IOnlineVoicePtr GetVoiceInterface() const override;
	virtual IOnlineExternalUIPtr GetExternalUIInterface() const override;
	virtual IOnlineTimePtr GetTimeInterface() const override;
	virtual IOnlineIdentityPtr GetIdentityInterface() const override;
	virtual IOnlineTitleFilePtr GetTitleFileInterface() const override;
	virtual IOnlineStorePtr GetStoreInterface() const override;
	virtual IOnlineStoreV2Ptr GetStoreV2Interface() const override;
	virtual IOnlinePurchasePtr GetPurchaseInterface() const override;
	virtual IOnlineEventsPtr GetEventsInterface() const override;
	virtual IOnlineAchievementsPtr GetAchievementsInterface() const override;
	virtual IOnlineSharingPtr GetSharingInterface() const override;
	virtual IOnlineUserPtr GetUserInterface() const override;
	virtual IOnlineMessagePtr GetMessageInterface() const override;
	virtual IOnlinePresencePtr GetPresenceInterface() const override;
	virtual IOnlineChatPtr GetChatInterface() const override;
	virtual IOnlineStatsPtr GetStatsInterface() const override;
	virtual IOnlineTurnBasedPtr GetTurnBasedInterface() const override;
	virtual IOnlineTournamentPtr GetTournamentInterface() const override;

	virtual bool Init() override;
	virtual bool Shutdown() override;
	virtual FString GetAppId() const override;
	virtual FText GetOnlineServiceName() const override;

	virtual bool Tick(float DeltaTime) override;

	

PACKAGE_SCOPE:

	/** Only the factory makes instances */
	FOnlineSubsystemEpic() = delete;

	explicit FOnlineSubsystemEpic(FName InInstanceName)
		: FOnlineSubsystemImpl(EPIC_SUBSYSTEM, InInstanceName)
		, IsInit(false)
		, PlatformHandle(nullptr)
		, IdentityInterface(nullptr)
		, PresenceInterface(nullptr)
		, devToolPort(9999)
	{}

	bool IsInit;

	/** Platform handle */
	EOS_HPlatform PlatformHandle;

	/** Interface to the identity registration/auth services */
	FOnlineIdentityEpicPtr IdentityInterface;

	FOnlineSessionEpicPtr SessionInterface;

	FOnlineUserEpicPtr UserInterface;

	FOnlinePresenceEpicPtr PresenceInterface;

	FString DevToolAddress;
};


using FOnlineSubsystemEpicPtr = TSharedPtr<FOnlineSubsystemEpic, ESPMode::ThreadSafe>;

