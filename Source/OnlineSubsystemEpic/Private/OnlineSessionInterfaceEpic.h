// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemEpicPackage.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "UObject/CoreOnline.h"
#include "eos_sdk.h"

class FOnlineSubsystemEpic;

/**
 * Interface definition for the online services session services
 * Session services are defined as anything related managing a session
 * and its state within a platform service
 */
class FOnlineSessionEpic
	: public IOnlineSession
{
private:
	/** Hidden on purpose */
	FOnlineSessionEpic()
		: Subsystem(nullptr)
		, sessionsHandle(nullptr)
	{
	}

	// --------
	// Important Handles
	// --------

	/** The subsystem that owns this instance*/
	FOnlineSubsystemEpic* Subsystem;

	/** Convenience handle to EOS sessions */
	EOS_HSessions sessionsHandle;

	/**  Handle to the session invite callback. */
	EOS_NotificationId sessionInviteRecivedCallbackHandle;

	/**  Handle to the session invite callback. */
	EOS_NotificationId sessionInviteAcceptedCallbackHandle;

	// --------
	// EOS Callbacks
	// --------
	static void OnEOSCreateSessionComplete(const EOS_Sessions_UpdateSessionCallbackInfo* Data);
	static void OnEOSStartSessionComplete(const EOS_Sessions_StartSessionCallbackInfo* Data);
	static void OnEOSUpdateSessionComplete(const EOS_Sessions_UpdateSessionCallbackInfo* Data);
	static void OnEOSEndSessionComplete(const EOS_Sessions_EndSessionCallbackInfo* Data);
	static void OnEOSDestroySessionComplete(const EOS_Sessions_DestroySessionCallbackInfo* Data);
	static void OnEOSFindSessionComplete(const EOS_SessionSearch_FindCallbackInfo* Data);
	static void OnEOSJoinSessionComplete(const EOS_Sessions_JoinSessionCallbackInfo* Data);
	static void OnEOSRegisterPlayersComplete(const EOS_Sessions_RegisterPlayersCallbackInfo* Data);
	static void OnEOSUnRegisterPlayersComplete(const EOS_Sessions_UnregisterPlayersCallbackInfo* Data);
	static void OnEOSFindFriendSessionComplete(const EOS_SessionSearch_FindCallbackInfo* Data);
	static void OnEOSSendSessionInviteToFriendsComplete(const EOS_Sessions_SendInviteCallbackInfo* Data);
	static void OnEOSSessionInviteReceived(const EOS_Sessions_SessionInviteReceivedCallbackInfo* Data);
	static void OnEOSSessionInviteAccepted(const EOS_Sessions_SessionInviteAcceptedCallbackInfo* Data);


	// --------
	// Private Utility methods
	// --------
	void UpdateSessionSearchParameters(TSharedRef<FOnlineSessionSearch> const& sessionSearchPtr, EOS_HSessionSearch eosSessionSearch, FString& error);

	bool GetConnectStringFromSessionInfo(TSharedPtr<FOnlineSessionInfoEpic>& SessionInfo, FString& ConnectInfo, int32 PortOverride = 0);

	/**
	 * Creates EOS Attribute data from UE4s variant data
	 * @param attributeName - The name for the data.
	 * @param variantData - The variable data itself
	 * @outAttributeData - The EOS AttributeData populated with the variant data
	 * @error - The error message if the creation was unsuccessful
	 * @returns - True if the creation was successful, false otherwise
	*/
	EOS_Sessions_AttributeData CreateEOSAttributeData(FString const attributeName, FVariantData const variantData, FString& error);
	
	/** Sets the session details from the EOS session details struct*/
	void SetSessionDetails(FOnlineSession* session, EOS_SessionDetails_Info const* SessionDetails);

	/** Converts an EOS active session into a local named session */
	bool UpdateNamedOnlineSession(FNamedOnlineSession* session, EOS_ActiveSession_Info const* ActiveSession, bool IsHosting);

	/** Creates a pointer to an EOS session update struct from the passed session settings */
	void CreateSessionModificationHandle(FOnlineSessionSettings const& NewSessionSettings, EOS_HSessionModification& ModificationHandle, FString& Error);

	/// Convert a String to an Internet address.
	TPair<bool, TSharedPtr<class FInternetAddr>> StringToInternetAddress(FString addressStr);



PACKAGE_SCOPE:

	/** Critical sections for thread safe operation of session lists */
	mutable FCriticalSection SessionLock;

	/** Array of sessions currently available on the local machine. Might not be in sync with remote */
	TArray<FNamedOnlineSession> Sessions;

	/**
	 * Array of session searches.
	 * @Key - The time in UTC the session search was created.
	 * @Value1 - A handle to the EOS session search. If the search isn't running, the handle will be invalid
	 * @Value2 - The local session search settings
	 */
	TMap<double, TTuple<EOS_HSessionSearch, TSharedRef<FOnlineSessionSearch>>> SessionSearches;

	/**
	 * Creates a new instance of the FOnlineSessionEpic class.
	 * @ InSubsystem - The subsystem that owns the instance.
	 */
	FOnlineSessionEpic(FOnlineSubsystemEpic* InSubsystem);

	/** Session tick for various background */
	void Tick(float DeltaTime);

	// IOnlineSession
	class FNamedOnlineSession* AddNamedSession(FName SessionName, const FOnlineSessionSettings& SessionSettings) override
	{
		FScopeLock ScopeLock(&SessionLock);
		return new (Sessions) FNamedOnlineSession(SessionName, SessionSettings);
	}

	class FNamedOnlineSession* AddNamedSession(FName SessionName, const FOnlineSession& Session) override
	{
		FScopeLock ScopeLock(&SessionLock);
		return new (Sessions) FNamedOnlineSession(SessionName, Session);
	}

public:

	virtual ~FOnlineSessionEpic();

	virtual TSharedPtr<const FUniqueNetId> CreateSessionIdFromString(const FString& SessionIdStr) override;
	FNamedOnlineSession* GetNamedSession(FName SessionName) override;
	virtual void RemoveNamedSession(FName SessionName) override;
	virtual EOnlineSessionState::Type GetSessionState(FName SessionName) const override;
	virtual bool HasPresenceSession() override;

	// IOnlineSession
	virtual bool CreateSession(int32 HostingPlayerNum, FName SessionName, const FOnlineSessionSettings& NewSessionSettings) override;
	virtual bool CreateSession(const FUniqueNetId& HostingPlayerId, FName SessionName, const FOnlineSessionSettings& NewSessionSettings) override;
	virtual bool StartSession(FName SessionName) override;
	virtual bool UpdateSession(FName SessionName, FOnlineSessionSettings& UpdatedSessionSettings, bool bShouldRefreshOnlineData = true) override;
	virtual bool EndSession(FName SessionName) override;
	virtual bool DestroySession(FName SessionName, const FOnDestroySessionCompleteDelegate& CompletionDelegate = FOnDestroySessionCompleteDelegate()) override;
	virtual bool IsPlayerInSession(FName SessionName, const FUniqueNetId& UniqueId) override;
	virtual bool StartMatchmaking(const TArray< TSharedRef<const FUniqueNetId> >& LocalPlayers, FName SessionName, const FOnlineSessionSettings& NewSessionSettings, TSharedRef<FOnlineSessionSearch>& SearchSettings) override;
	virtual bool CancelMatchmaking(int32 SearchingPlayerNum, FName SessionName) override;
	virtual bool CancelMatchmaking(const FUniqueNetId& SearchingPlayerId, FName SessionName) override;
	virtual bool FindSessions(int32 SearchingPlayerNum, const TSharedRef<FOnlineSessionSearch>& SearchSettings) override;
	virtual bool FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings) override;
	virtual bool FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegate) override;
	virtual bool CancelFindSessions() override;
	virtual bool PingSearchResults(const FOnlineSessionSearchResult& SearchResult) override;
	virtual bool JoinSession(int32 PlayerNum, FName SessionName, const FOnlineSessionSearchResult& DesiredSession) override;
	virtual bool JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession) override;
	virtual bool FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend) override;
	virtual bool FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend) override;
	virtual bool FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& FriendList) override;
	virtual bool SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend) override;
	virtual bool SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend) override;
	virtual bool SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends) override;
	virtual bool SendSessionInviteToFriends(const FUniqueNetId& LocalUserId, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends) override;
	virtual bool GetResolvedConnectString(FName SessionName, FString& ConnectInfo, FName PortType) override;
	virtual bool GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo) override;
	virtual FOnlineSessionSettings* GetSessionSettings(FName SessionName) override;
	virtual bool RegisterPlayer(FName SessionName, const FUniqueNetId& PlayerId, bool bWasInvited) override;
	virtual bool RegisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players, bool bWasInvited = false) override;
	virtual bool UnregisterPlayer(FName SessionName, const FUniqueNetId& PlayerId) override;
	virtual bool UnregisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players) override;
	virtual void RegisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnRegisterLocalPlayerCompleteDelegate& Delegate) override;
	virtual void UnregisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnUnregisterLocalPlayerCompleteDelegate& Delegate) override;
	virtual int32 GetNumSessions() override;
	virtual void DumpSessionState() override;
};
using FOnlineSessionEpicPtr = TSharedPtr<FOnlineSessionEpic, ESPMode::ThreadSafe>;
