#pragma once

#include "CoreMinimal.h"
#include "eos_friends.h"
#include "OnlineIdentityInterfaceEpic.h"
#include "OnlinePresenceInterface.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "OnlineSubsystemEpic/Private/OnlineSubsystemEpicTypes.h"
#include "Interfaces/OnlineUserInterface.h"

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
	
	//TODO -
	/* Full presence information including platform type, application, status, rich text. */
	/*
		FOnlineUserPresence Presence;*/
	FOnlineUserPresence Presence;
};

class FOnlineFriendInterfaceEpic : public IOnlineFriends
{

public:
	FOnlineFriendInterfaceEpic() : Subsystem(nullptr),
	                               friendsHandle(nullptr), LocalUserNum(0)
	{
	}

	/**
	 * Initializes the various interfaces
	 *
	 * @param InSubsystem the subsystem that owns this object
	 */
	FOnlineFriendInterfaceEpic(FOnlineSubsystemEpic* InSubsystem);

	virtual ~FOnlineFriendInterfaceEpic() {};

protected:
	/** The subsystem that owns the instance */
	FOnlineSubsystemEpic* Subsystem;
	
	EOS_HFriends friendsHandle;

	/** list of friends */
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
	int32 LocalUserNum;

	bool GetFriendsList(int32 InLocalUserNum, const FString& ListName,
		TArray<TSharedRef<FOnlineFriend>>& OutFriends) override;

	bool ReadFriendsList(int32 InLocalUserNum, const FString& ListName,
		const FOnReadFriendsListComplete& Delegate) override;
	void HandleQueryUserInfoComplete(int32 InLocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& Ids, const FString& Test);
	void Tick(float DeltaTime);

public:

	EOS_HFriends FORCEINLINE GetFriendsHandle() { return friendsHandle;  }
	
	bool DeleteFriendsList(int32 InLocalUserNum, const FString& ListName,
	                       const FOnDeleteFriendsListComplete& Delegate) override;

	bool SendInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
		const FOnSendInviteComplete& Delegate) override;

	bool AcceptInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
	                  const FOnAcceptInviteComplete& Delegate) override;

	bool RejectInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;

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
	static void OnEOSQueryFriendsComplete(EOS_Friends_QueryFriendsCallbackInfo const* Data);
	static void SendInviteCallback(const EOS_Friends_SendInviteCallbackInfo* Data);
	static void RejectInviteCallback(const EOS_Friends_RejectInviteCallbackInfo* Data);
	static void AcceptInviteCallback(const EOS_Friends_AcceptInviteCallbackInfo* Data);

};

typedef TSharedPtr<class FOnlineFriendEpic, ESPMode::ThreadSafe> FOnlineFriendsSteamPtr;