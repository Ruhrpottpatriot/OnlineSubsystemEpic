//Copyright (c) MikhailSorokin

#pragma once

#include "CoreMinimal.h"
#include "eos_friends.h"
#include "OnlineIdentityInterfaceEpic.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "OnlineSubsystemEpic/Private/OnlineSubsystemEpicTypes.h"
#include "Interfaces/OnlineUserInterface.h"
#include "OnlinePresenceInterface.h"

class FOnlineSubsystemEpic;

/* Info associated with an online friend on Epic Online Services.
 * NOTE: EOS can support storing friends across different platforms.
 */
class FOnlineFriendEpic : public FOnlineFriend
{

	// FOnlineUser

public:
	virtual TSharedRef<const FUniqueNetId> GetUserId() const override;
	virtual FString GetRealName() const override;
	virtual FString GetDisplayName(const FString& Platform = FString()) const override;
	virtual bool GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const override;

	// FOnlineFriend

	virtual EInviteStatus::Type GetInviteStatus() const override;
	virtual const FOnlineUserPresence& GetPresence() const override;

	// Custom function
	EFriendStatus GetFriendStatus() const;
	
	/**
	 * Init/default constructor
	 */
	FOnlineFriendEpic(const EOS_EpicAccountId& InUserId = EOS_EpicAccountId());

	/** Virtual destructor to keep clang happy */
	virtual ~FOnlineFriendEpic() {};
	
	/**
	 * Get account data attribute
	 *
	 * @param Key account data entry key
	 * @param OutVal [out] value that was found
	 *
	 * @return true if entry was found
	 */
	inline bool GetAccountData(const FString& Key, FString& OutVal) const
	{
		const FString* FoundVal = AccountData.Find(Key);
		if (FoundVal != NULL)
		{
			OutVal = *FoundVal;
			return true;
		}
		return false;
	}

	/** User Id represented as a FUniqueNetId */
	TSharedRef<const FUniqueNetId> UserId;
	/** Any addition account data associated with the friend */
	TMap<FString, FString> AccountData;

	/** Friend status that can be used for blueprints */
	EFriendStatus FriendStatus;
	
	/* Full presence information including platform type, application, status, rich text. */
	FOnlineUserPresence Presence;

	virtual void FORCEINLINE SetPresence(FOnlineUserPresence InPresence) { Presence = InPresence; }
};

class FOnlineFriendInterfaceEpic : public IOnlineFriends
{

public:
	FOnlineFriendInterfaceEpic() : Subsystem(nullptr),
	                               friendsHandle(nullptr)
	{
	}

	/**
	 * Initializes the various interfaces
	 *
	 * @param InSubsystem the subsystem that owns this object
	 */
	FOnlineFriendInterfaceEpic(FOnlineSubsystemEpic* InSubsystem);

	virtual ~FOnlineFriendInterfaceEpic() {};

	/** The subsystem that owns the instance */
	FOnlineSubsystemEpic* Subsystem;
	/** Delegates for non-friend users that were called with QueryPresence */
	TMap<FUniqueNetIdEpic, TSharedRef<const IOnlinePresence::FOnPresenceTaskCompleteDelegate>> DelayedPresenceDelegates;


protected:
	EOS_HFriends friendsHandle;
	FOnReadFriendsListComplete ReadFriendsListDelegate;

	/** List of friends */
	struct FEpicFriendsList
	{
		TArray<TSharedRef<FOnlineFriendEpic> > Friends;
	};
	/** map of local user idx to friends */
	TMap<int32, FEpicFriendsList> FriendsLists;

	/** Delegate to use for querying user info list */
	FOnQueryUserInfoCompleteDelegate OnQueryUserInfoCompleteDelegate;
	/** OnQueryUserInfoComplete delegate handle */
	FDelegateHandle OnQueryUserInfoCompleteDelegateHandle;

public:
	bool GetFriendsList(int32 InLocalUserNum, const FString& ListName,
		TArray<TSharedRef<FOnlineFriend>>& OutFriends) override;

	bool ReadFriendsList(int32 InLocalUserNum, const FString& ListName,
		const FOnReadFriendsListComplete& Delegate) override;
	void HandleQueryUserInfoComplete(int32 InLocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& Ids, const FString& Error);

public:

	virtual void Tick(float DeltaTime);

	EOS_HFriends FORCEINLINE GetFriendsHandle() { return friendsHandle;  }

	bool DeleteFriendsList(int32 InLocalUserNum, const FString& ListName, 
		const FOnDeleteFriendsListComplete& Delegate) override;

	/* =============== Invite functions for friends ================== */
	bool SendInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, 
		const FOnSendInviteComplete& Delegate) override;
	bool AcceptInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
	                 const FOnAcceptInviteComplete& Delegate) override;
	bool RejectInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;

	//Friend modifiable functions
	void SetFriendAlias(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FString& Alias,
	                    const FOnSetFriendAliasComplete& Delegate) override;
	bool DeleteFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	
	TSharedPtr<FOnlineFriend> GetFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	
	bool IsFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	bool QueryRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace) override;
	bool GetRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace,
		TArray<TSharedRef<FOnlineRecentPlayer>>& OutRecentPlayers) override;
	void DumpRecentPlayers() const override;
	bool BlockPlayer(int32 InLocalUserNum, const FUniqueNetId& PlayerId) override;
	bool UnblockPlayer(int32 InLocalUserNum, const FUniqueNetId& PlayerId) override;
	bool QueryBlockedPlayers(const FUniqueNetId& UserId) override;
	bool GetBlockedPlayers(const FUniqueNetId& UserId,
		TArray<TSharedRef<FOnlineBlockedPlayer>>& OutBlockedPlayers) override;
	void DumpBlockedPlayers() const override;


private:
	static void OnEASQueryFriendsComplete(EOS_Friends_QueryFriendsCallbackInfo const* Data);
	void OnFriendQueryPresenceComplete(const FUniqueNetId& UserId, bool bWasSuccessful);
	static void SendInviteCallback(const EOS_Friends_SendInviteCallbackInfo* Data);
	static void RejectInviteCallback(const EOS_Friends_RejectInviteCallbackInfo* Data);
	static void AcceptInviteCallback(const EOS_Friends_AcceptInviteCallbackInfo* Data);

};

typedef TSharedPtr<class FOnlineFriendEpic, ESPMode::ThreadSafe> FOnlineFriendsSteamPtr;
