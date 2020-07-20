//Copyright (c) MikhailSorokin

#pragma once

#include "CoreMinimal.h"
#include "eos_friends.h"
#include "eos_friends_types.h"
#include "OnlineIdentityInterfaceEpic.h"
#include "Interfaces/OnlineFriendsInterface.h"
#include "OnlineSubsystemEpic/Private/OnlineSubsystemEpicTypes.h"
#include "Interfaces/OnlineUserInterface.h"
#include "OnlinePresenceInterface.h"

class FOnlineSubsystemEpic;

/*
 * Info associated with an online friend on the Epic Game Store (EGS).
 *
 * NOTE: these functions only support accounts on EGS or those
 * linked to an EGS account with a different supported identity provider.
 */
class FOnlineFriendEpic : public FOnlineFriend
{

public:
	/**
	 * Init/default constructor that must take in an Epic Account Id
	 */
	FOnlineFriendEpic(const EOS_EpicAccountId& InUserId = EOS_EpicAccountId());

	FOnlineFriendEpic(const FUniqueNetId& NetId);

	/** Virtual destructor to keep resharper happy */
	virtual ~FOnlineFriendEpic() {};

	// FOnlineUser
	virtual TSharedRef<const FUniqueNetId> GetUserId() const override;
	virtual FString GetRealName() const override;
	virtual FString GetDisplayName(const FString& Platform = FString()) const override;

	/**
	 * Get account data attribute
	 *
	 * @param Key account data entry key
	 * @param OutVal [out] value that was found
	 *
	 * @return true if entry was found
	 */
	virtual bool GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const override;

	// FOnlineFriend

	virtual EInviteStatus::Type GetInviteStatus() const override;
	virtual const FOnlineUserPresence& GetPresence() const override;

	// Custom functions
	EFriendStatus GetFriendStatus() const;

	virtual void FORCEINLINE SetFriendStatus(EFriendStatus InFriendStatus) { FriendStatus = InFriendStatus; }
	virtual void FORCEINLINE SetPresence(FOnlineUserPresence InPresence) { Presence = InPresence; }

	bool SetUserLocalAttribute(const FString& AttrName, const FString& InAttrValue) override;

protected:
	/** User Id represented as a FUniqueNetId */
	TSharedRef<const FUniqueNetId> UserId;
	
	/** Any additional account data associated with the friend */
	TMap<FString, FString> AccountData;
	
	/** Friend status that can be used for blueprints */
	EFriendStatus FriendStatus;
	
	/* Full presence information including platform type, application, status, rich text. */
	FOnlineUserPresence Presence;
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
	TMap<FUniqueNetIdEpic, IOnlinePresence::FOnPresenceTaskCompleteDelegate> DelayedPresenceDelegates;

protected:
	EOS_HFriends friendsHandle;
	FOnReadFriendsListComplete ReadFriendsListDelegate;

	/** List of friends */
	struct FEpicFriendsList
	{
		TArray<TSharedRef<FOnlineFriendEpic> > Friends;
	};
	
	/**Mmap of local user idx to friends */
	TMap<int32, FEpicFriendsList> FriendsLists;

	/** Delegate to use for querying user info list */
	FOnQueryUserInfoCompleteDelegate OnQueryUserInfoCompleteDelegate;
	/** OnQueryUserInfoComplete delegate handle */
	FDelegateHandle OnQueryUserInfoCompleteDelegateHandle;

public:
	/*
	 * A call that keeps a cached friends list available at the ready.
	 * In order for this to be populated, the friends list needs to
	 * be queried at least once, which ReadFriendsList handles.
	 *
	 * @param LocalUserNum the local user whose friends info we want to see
	 * @param ListName a filter we can use to sort out different types of presence.
	 * I.E. - Online players vs offline, away, etc.
	 * @param OutFriends returns an array of FOnlineFriends that have data shared with FOnlineFriendEpic
	 *
	 * @return true if any friends were found, false if not
	 */
	bool GetFriendsList(int32 LocalUserNum, const FString& ListName,
		TArray<TSharedRef<FOnlineFriend>>& OutFriends) override;

	/*
	 * Queries the current list of friends. This may be useful for
	 * updates in adding or removing friends as well.
	 *
	 * @param LocalUserNum the local user whose friends info we want to see
	 * @param ListName a filter we can use to sort out different types of presence.
	 * I.E. - Online players vs offline, away, etc.
	 * @param Delegate a callback that will inform us of when a friends query has been complete.
	 *
	 * @return true if we could go through with a query, does NOT guarantee we get all the
	 * right information such as presence or name! Returns false if query abruptly failed
	 */
	bool ReadFriendsList(int32 LocalUserNum, const FString& ListName,
		const FOnReadFriendsListComplete& Delegate) override;

	/*
	 * A call that gets a specific friend from the FriendsList.
	 *
	 * @param LocalUserNum the local user whose friends info we want to see
	 * @param FriendId assumed to have an epic account id stored in the GetBytes() method
	 * @param ListName a filter we can use to sort out different types of presence.
	 * I.E. - Online players vs offline, away, etc.
	 *
	 * @return the shared pointer equivalent of a specific friend, null if FriendId improperly formatted
	 */
	TSharedPtr<FOnlineFriend> GetFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;

	/*
	 * Checks if the friend id that is passed in is indeed a friend of the specified local user.
	 *
	 * @param LocalUserNum the local user whose friends info we want to see
	 * @param FriendId assumed to have an epic account id stored in the GetBytes() method
	 * @param ListName a filter we can use to sort out different types of presence.
	 * I.E. - Online players vs offline, away, etc.
	 *
	 * @return true if FriendId can match to a friend in the FriendsList, false otherwise
	 */
	bool IsFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	
protected:
	/*
	 * The callbacks associated with the delegate in ReadFriendsList and QueryPresence, respectively.
	 */
	void OnQueryFriendsComplete(int32 InLocalUserNum, bool bWasSuccessful, const TArray<TSharedRef<const FUniqueNetId>>& Ids, const FString& Error);
	void OnFriendQueryPresenceComplete(const FUniqueNetId& UserId, bool bWasSuccessful);
	
	// ---------------------------------------------
	// EOS SDK Callback functions
	// ---------------------------------------------
	static void OnEASQueryFriendsComplete(EOS_Friends_QueryFriendsCallbackInfo const* Data);
	static void SendInviteCallback(const EOS_Friends_SendInviteCallbackInfo* Data);
	static void RejectInviteCallback(const EOS_Friends_RejectInviteCallbackInfo* Data);
	static void AcceptInviteCallback(const EOS_Friends_AcceptInviteCallbackInfo* Data);
	
public:
	//Functions for support later
	
	/* =============== Invite functions for friends ================== */
	bool SendInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, 
		const FOnSendInviteComplete& Delegate) override;
	bool AcceptInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
	                 const FOnAcceptInviteComplete& Delegate) override;
	bool RejectInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;

	/* ================== Functions for potential support later ====== */
	bool QueryRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace) override;
	bool GetRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace,
		TArray<TSharedRef<FOnlineRecentPlayer>>& OutRecentPlayers) override;
	void DumpRecentPlayers() const override;

	/* ============ Modifiable friend setter functions =============== */
	void SetFriendAlias(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FString& Alias,
	                    const FOnSetFriendAliasComplete& Delegate) override;

	/* ============ Currently unsupported functions ================== */
	bool DeleteFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName) override;
	bool DeleteFriendsList(int32 InLocalUserNum, const FString& ListName,
		const FOnDeleteFriendsListComplete& Delegate) override;

	bool BlockPlayer(int32 InLocalUserNum, const FUniqueNetId& PlayerId) override;
	bool UnblockPlayer(int32 InLocalUserNum, const FUniqueNetId& PlayerId) override;
	bool QueryBlockedPlayers(const FUniqueNetId& UserId) override;
	bool GetBlockedPlayers(const FUniqueNetId& UserId,
		TArray<TSharedRef<FOnlineBlockedPlayer>>& OutBlockedPlayers) override;
	void DumpBlockedPlayers() const override;

public:
	/* ========================= CUSTOM Functions ========================= */
	EOS_HFriends FORCEINLINE GetFriendsHandle() { return friendsHandle; }

#if ENGINE_MINOR_VERSION >= 25
	void DeleteFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
		const FOnDeleteFriendAliasComplete& Delegate) override;
#endif
};

using FOnlineFriendInterfacePtr = TSharedPtr<FOnlineFriendInterfaceEpic, ESPMode::ThreadSafe>;
