//Copyright (c) MikhailSorokin
#include "OnlineFriendInterfaceEpic.h"
#include "CoreMinimal.h"


#include "OnlineSessionInterfaceEpic.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineUserInterfaceEpic.h"
#include "OnlinePresenceEpic.h"
#include "Engine/Engine.h"
#include "Utilities.h"


typedef struct FLocalUserData
{
	FOnlineFriendsInterfaceEpic* OnlineFriendPtr;
	FOnlineUserEpic* OnlineUserPtr;
	EOS_HFriends FriendsHandle;
	int32 LocalPlayerNum;
} FLocalUserData;

// FOnlineFriendEpic
FOnlineFriendEpic::FOnlineFriendEpic(const EOS_EpicAccountId& InUserId)
	: UserId(new FUniqueNetIdEpic(InUserId)),
	FriendStatus(EFriendStatus::NotFriends)
{
}

TSharedRef<const FUniqueNetId> FOnlineFriendEpic::GetUserId() const
{
	return UserId;
}

FString FOnlineFriendEpic::GetRealName() const
{
	FString Result;
	GetUserAttribute(USER_ATTR_REALNAME, Result);
	return Result;
}

FString FOnlineFriendEpic::GetDisplayName(const FString& Platform /*= FString()*/) const
{
	FString Result;
	GetUserAttribute(USER_ATTR_DISPLAYNAME, Result);
	return Result;
}

bool FOnlineFriendEpic::GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const
{
	const FString* FoundVal = AccountData.Find(AttrName);
	if (FoundVal != NULL)
	{
		OutAttrValue = *FoundVal;
		return true;
	}
	return false;
}

EInviteStatus::Type FOnlineFriendEpic::GetInviteStatus() const
{
	switch (FriendStatus)
	{
		case EFriendStatus::Friends:
			return EInviteStatus::Accepted;
		case EFriendStatus::InviteSent:
			return EInviteStatus::PendingOutbound;
		case EFriendStatus::InviteReceived:
			return EInviteStatus::PendingInbound;
		case EFriendStatus::NotFriends:
			return EInviteStatus::Blocked;
		default:
			return EInviteStatus::Unknown;
	}
}

EFriendStatus FOnlineFriendEpic::GetFriendStatus() const
{
	return FriendStatus;
}

bool FOnlineFriendEpic::SetUserLocalAttribute(const FString& AttrName, const FString& InAttrValue)
{
	if (!AccountData.Contains(AttrName)) {
		AccountData.Add(AttrName);
	}

	AccountData[AttrName] = InAttrValue;
	
	return true;
}

const FOnlineUserPresence& FOnlineFriendEpic::GetPresence() const
{
	return Presence;
}

//-------------------------------
// FOnlineFriendsInterfaceEpic
//-------------------------------

FOnlineFriendsInterfaceEpic::FOnlineFriendsInterfaceEpic(FOnlineSubsystemEpic* InSubsystem) :
	Subsystem(InSubsystem)
{
	check(Subsystem);
	this->friendsHandle = EOS_Platform_GetFriendsInterface(Subsystem->PlatformHandle);
}

bool FOnlineFriendsInterfaceEpic::GetFriendsList(int32 LocalUserNum, const FString& ListName,
	TArray<TSharedRef<FOnlineFriend>>& OutFriends)
{
	bool bResult = false;
	if (LocalUserNum < MAX_LOCAL_PLAYERS && Subsystem != nullptr) {
		FEpicFriendsList* FriendsList = FriendsLists.Find(LocalUserNum);
		if (FriendsList != nullptr) {
			for (int FriendInd = 0; FriendInd < FriendsList->Friends.Num(); FriendInd++) {
				OutFriends.Add(FriendsList->Friends[FriendInd]);
			}
			bResult = true;
		}
	}

	UE_LOG_ONLINE_FRIEND(Log, TEXT("Number of friends is: %d"), OutFriends.Num());
	return bResult;
}

bool FOnlineFriendsInterfaceEpic::ReadFriendsList(int32 LocalUserNum, const FString& ListName,
	const FOnReadFriendsListComplete& Delegate)
{
	UE_LOG_ONLINE_FRIEND(Log, TEXT("%s: calling friend query."), __FUNCTIONW__);

	if (!this->Subsystem || !this->Subsystem->GetIdentityInterface()) {
		UE_LOG_ONLINE_FRIEND(Display, TEXT("Subsystem not set up or identity interface could not be used!"));

		return false;
	}
	
	uint32 result = ONLINE_FAIL;
	
	EOS_Friends_QueryFriendsOptions Options;
	Options.ApiVersion = EOS_FRIENDS_QUERYFRIENDS_API_LATEST;

	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	TSharedPtr<const FUniqueNetIdEpic> userId = StaticCastSharedPtr<const FUniqueNetIdEpic>(identityPtr->GetUniquePlayerId(LocalUserNum));

	if (userId && userId.IsValid() && userId->IsEpicAccountIdValid())
	{
		UE_LOG_ONLINE_FRIEND(Log, TEXT("%s: In User Id %s"), __FUNCTIONW__, *userId->ToDebugString());

		OnQueryUserInfoCompleteDelegate = FOnQueryUserInfoCompleteDelegate::CreateRaw(this, &FOnlineFriendsInterfaceEpic::OnQueryFriendsComplete);
		OnQueryUserInfoCompleteDelegateHandle = this->Subsystem->GetUserInterface()->AddOnQueryUserInfoCompleteDelegate_Handle(LocalUserNum, OnQueryUserInfoCompleteDelegate);

		Options.LocalUserId = userId->ToEpicAccountId();

		//If the epic account is not valid, get a mapping of their product ID
		FLocalUserData* UserData = new FLocalUserData
		{
			this,
			(FOnlineUserEpic*)Subsystem->GetUserInterface().Get(),
			friendsHandle,
			LocalUserNum,
		};

		ReadFriendsListDelegate = Delegate;
		EOS_Friends_QueryFriends(this->friendsHandle, &Options, UserData, &FOnlineFriendsInterfaceEpic::OnEASQueryFriendsComplete);
		result = ONLINE_IO_PENDING;
	}
	else
	{
		UE_CLOG_ONLINE_USER(!userId || !userId.IsValid() || !userId->IsProductUserIdValid(), Display, TEXT("%s user ID is not valid! Check to see if your product ID is valid."), __FUNCTIONW__);
		result = ONLINE_FAIL;
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
}

void FOnlineFriendsInterfaceEpic::OnQueryFriendsComplete(int32 InLocalUserNum, bool bWasSuccessful, const TArray< TSharedRef<const FUniqueNetId> >& Ids, const FString& Error)
{
	UE_LOG(LogTemp, Log, TEXT("Query user callback result: %d with number of ids: %d"), bWasSuccessful, Ids.Num());

	IOnlineUserPtr UserInterfacePtr = this->Subsystem->GetUserInterface();

	if (bWasSuccessful) {
		for (int32 UserIdx = 0; UserIdx < Ids.Num(); UserIdx++)
		{
			if (FriendsLists.Contains(InLocalUserNum)) {
				TSharedRef<FOnlineFriendEpic> CurrentFriend = FriendsLists[InLocalUserNum].Friends[UserIdx];
				TSharedPtr<FOnlineUser> OnlineUser = UserInterfacePtr->GetUserInfo(InLocalUserNum, Ids[UserIdx].Get());

				CurrentFriend->SetUserLocalAttribute(USER_ATTR_DISPLAYNAME, OnlineUser->GetDisplayName());
				CurrentFriend->SetUserLocalAttribute(USER_ATTR_REALNAME, OnlineUser->GetRealName());

				FString PreferredDisplayName;
				OnlineUser->GetUserAttribute(USER_ATTR_PREFERRED_DISPLAYNAME, PreferredDisplayName);
				CurrentFriend->SetUserLocalAttribute(USER_ATTR_PREFERRED_DISPLAYNAME, PreferredDisplayName);
				FString AliasName;
				OnlineUser->GetUserAttribute(USER_ATTR_ALIAS, AliasName);
				CurrentFriend->SetUserLocalAttribute(USER_ATTR_ALIAS, AliasName);

				FString DisplayName;
				CurrentFriend->GetUserAttribute(USER_ATTR_DISPLAYNAME, DisplayName);
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "Name is: " + CurrentFriend->GetDisplayName()
					+ ", with nickname of: " + AliasName);
			}
			else
			{
				UE_LOG_ONLINE_FRIEND(Error, TEXT("Friendslist is not active yet for local user: %d"), InLocalUserNum);
			}
		}
	}

	//It is ok if we don't execute this with presence status, that can be updated immediately after
	if (ReadFriendsListDelegate.IsBound() && bWasSuccessful) {
		ReadFriendsListDelegate.ExecuteIfBound(InLocalUserNum, true, "Default", "");
	}
	else
	{
		ReadFriendsListDelegate.ExecuteIfBound(InLocalUserNum, false, "Default", FString::Printf(TEXT("Could perform a query, but could not isolate out delegates.")));
	}


	// Clear delegates for the various async calls
	this->Subsystem->GetUserInterface()->ClearOnQueryUserInfoCompleteDelegate_Handle(InLocalUserNum, OnQueryUserInfoCompleteDelegateHandle);
}

TSharedPtr<FOnlineFriend> FOnlineFriendsInterfaceEpic::GetFriend(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	TSharedPtr<FOnlineFriend> Result = nullptr;
	if (LocalUserNum < MAX_LOCAL_PLAYERS && Subsystem != NULL)
	{
		FEpicFriendsList* FriendsList = FriendsLists.Find(LocalUserNum);
		if (FriendsList != NULL)
		{
			for (int32 FriendIdx = 0; FriendIdx < FriendsList->Friends.Num(); FriendIdx++)
			{
				// HACK: The user id would not return true in cases after the initial query - something to do with how reference
				// equality is passed by value or something? IDK
				if (FriendsList->Friends[FriendIdx]->GetUserId()->ToString() == FriendId.ToString())
				{
					Result = FriendsList->Friends[FriendIdx];
					break;
				}
			}
		}
	}
	return Result;
}

bool FOnlineFriendsInterfaceEpic::IsFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	if (InLocalUserNum < MAX_LOCAL_PLAYERS && FriendId.IsValid()) {
		TSharedPtr<FOnlineFriendEpic> EpicFriend = StaticCastSharedPtr<FOnlineFriendEpic>(GetFriend(InLocalUserNum, FriendId, ListName));
		if (EpicFriend) {
			return (EpicFriend->GetFriendStatus() != EFriendStatus::NotFriends);
		}
	}
	return false;
}

// ---------------------------------------------
// EOS SDK Callback functions
// ---------------------------------------------

void FOnlineFriendsInterfaceEpic::OnEASQueryFriendsComplete(EOS_Friends_QueryFriendsCallbackInfo const* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
	FLocalUserData* ThisPtr = (FLocalUserData*)(Data->ClientData);
	check(ThisPtr);

	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{

		FOnlineFriendsInterfaceEpic* FriendInterfaceEpic = ThisPtr->OnlineFriendPtr;
		
		EOS_Friends_GetFriendsCountOptions Options;
		Options.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
		Options.LocalUserId = Data->LocalUserId;
		
		int32_t FriendsCount = EOS_Friends_GetFriendsCount(ThisPtr->FriendsHandle, &Options);

		UE_LOG_ONLINE_FRIEND(Log, TEXT("%s number of friends is: %d"), __FUNCTIONW__, FriendsCount);

		FOnlineFriendsInterfaceEpic::FEpicFriendsList& FriendsList = FriendInterfaceEpic->FriendsLists.FindOrAdd(ThisPtr->LocalPlayerNum);
		//Pre-Size array for minimum re-allocs
		FriendsList.Friends.Empty(FriendsCount);

		EOS_Friends_GetFriendAtIndexOptions IndexOptions;
		IndexOptions.ApiVersion = EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST;
		IndexOptions.LocalUserId = Data->LocalUserId;

		TArray < TSharedRef<const FUniqueNetId>> UserIds;
		for (int32_t FriendIdx = 0; FriendIdx < FriendsCount; ++FriendIdx)
		{
			IndexOptions.Index = FriendIdx;

			EOS_EpicAccountId FriendUserId = EOS_Friends_GetFriendAtIndex(ThisPtr->FriendsHandle, &IndexOptions);

			if (EOS_EpicAccountId_IsValid(FriendUserId))
			{
				EOS_Friends_GetStatusOptions StatusOptions;
				StatusOptions.ApiVersion = EOS_FRIENDS_GETSTATUS_API_LATEST;
				StatusOptions.LocalUserId = Data->LocalUserId;
				StatusOptions.TargetUserId = FriendUserId;

				EOS_EFriendsStatus FriendStatus = EOS_Friends_GetStatus(ThisPtr->FriendsHandle, &StatusOptions);

				// Add to list
				TSharedRef<FOnlineFriendEpic> Friend(new FOnlineFriendEpic(FriendUserId));
				
				FriendsList.Friends.Add(Friend);
				//NOTE: We get the actual queried user info from the QueryUserInfo callback below
				Friend->SetUserLocalAttribute(USER_ATTR_REALNAME, "Pending...");
				Friend->SetUserLocalAttribute(USER_ATTR_DISPLAYNAME, "Pending...");
				Friend->SetUserLocalAttribute(USER_ATTR_PREFERRED_DISPLAYNAME, "Pending...");
				Friend->SetUserLocalAttribute(USER_ATTR_ALIAS, "Pending...");
				
				UserIds.Add(Friend->GetUserId());
				Friend->SetFriendStatus((EFriendStatus)FriendStatus);

				/** TODO - Kind of weird how QueryPresence doesn't have a similar multi-user query like UserInfo...
				 * there is a TODO in the OnlinePresenceInterface class that the original designers wanted to do it,
				 * so it is possible but that would require an engine build.
				 * Individual queries will have to do for now
				 */
				IOnlinePresence::FOnPresenceTaskCompleteDelegate DelegateHandle = IOnlinePresence::FOnPresenceTaskCompleteDelegate::CreateRaw(ThisPtr->OnlineFriendPtr, &FOnlineFriendsInterfaceEpic::OnFriendQueryPresenceComplete);
				ThisPtr->OnlineFriendPtr->DelayedPresenceDelegates.Add(FUniqueNetIdEpic(Friend->GetUserId().Get()), DelegateHandle);
				ThisPtr->OnlineFriendPtr->Subsystem->GetPresenceInterface()->QueryPresence(FUniqueNetIdEpic(Friend->GetUserId().Get()), DelegateHandle);
			}
		}

		//Query the array of friends
		ThisPtr->OnlineFriendPtr->Subsystem->GetUserInterface()->QueryUserInfo(ThisPtr->LocalPlayerNum, UserIds);

	}
	else {
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s asynchronous call was not successful!"), __FUNCTIONW__);
	}

	delete ThisPtr;
}

void FOnlineFriendsInterfaceEpic::OnFriendQueryPresenceComplete(const class FUniqueNetId& UserId, const bool bWasSuccessful)
{
	if (UserId.IsValid()) {		
		TSharedPtr<FOnlineFriendEpic> EpicFriendPtr = StaticCastSharedPtr<FOnlineFriendEpic>(GetFriend(0, FUniqueNetIdEpic(UserId), "Default"));
		
		if (bWasSuccessful && EpicFriendPtr) {
			IOnlinePresencePtr FriendPresencePtr = this->Subsystem->GetPresenceInterface();
			TSharedPtr<FOnlineUserPresence> UserPresence;
			FriendPresencePtr->GetCachedPresence(UserId, UserPresence);

			UE_LOG_ONLINE_FRIEND(Log, TEXT("%s setting presence for user: %s"), *FString(__FUNCTION__), *UserId.ToString());

			//It doesn't matter when presence is set whether before or after query of name
			EpicFriendPtr->SetPresence(*UserPresence.Get());
			FriendPresencePtr->TriggerOnPresenceReceivedDelegates(*EpicFriendPtr->GetUserId(), UserPresence.ToSharedRef());
		}
		else
		{
			UE_LOG_ONLINE_FRIEND(Error, TEXT("%s could not set presence for user: %s"), *FString(__FUNCTION__), *UserId.ToString());
		}
	}
	else
	{
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s userId is not valid"), *FString(__FUNCTION__), *UserId.ToString());
	}

	const FUniqueNetIdEpic& EpicId = (const FUniqueNetIdEpic&)(UserId);

	// Clear delegates for the presence async call
	DelayedPresenceDelegates.Remove(EpicId);
}

/* ============================================= INVITE INFORMATION ===================================== */
bool FOnlineFriendsInterfaceEpic::SendInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
	const FOnSendInviteComplete& Delegate)
{
	EOS_Friends_SendInviteOptions Options;
	Options.ApiVersion = EOS_FRIENDS_SENDINVITE_API_LATEST;

	TSharedPtr<FUniqueNetId const> sourceUser = Subsystem->GetIdentityInterface()->GetUniquePlayerId(InLocalUserNum);
	Options.LocalUserId = FUniqueNetIdEpic::EpicAccountIDFromString(sourceUser->ToString());
	FOnlineFriend* Friend = GetFriend(InLocalUserNum, FriendId, ListName).Get();
	Options.TargetUserId = FUniqueNetIdEpic::EpicAccountIDFromString(Friend->GetDisplayName());

	FLocalUserData* InformationData = new FLocalUserData{
		this,
			(FOnlineUserEpic*)Subsystem->GetUserInterface().Get(),
			friendsHandle,
			InLocalUserNum
	};
	
	EOS_Friends_SendInvite(this->friendsHandle, &Options, InformationData, SendInviteCallback);

	return true;
}

bool FOnlineFriendsInterfaceEpic::RejectInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	EOS_Friends_RejectInviteOptions Options;
	Options.ApiVersion = EOS_FRIENDS_REJECTINVITE_API_LATEST;

	TSharedPtr<FUniqueNetId const> sourceUser = Subsystem->GetIdentityInterface()->GetUniquePlayerId(InLocalUserNum);
	Options.LocalUserId = FUniqueNetIdEpic::EpicAccountIDFromString(sourceUser->ToString());
	FOnlineFriend* Friend = GetFriend(InLocalUserNum, FriendId, ListName).Get();
	Options.TargetUserId = FUniqueNetIdEpic::EpicAccountIDFromString(Friend->GetDisplayName());

	EOS_Friends_RejectInvite(this->friendsHandle, &Options, this, RejectInviteCallback);

	return false;
}

bool FOnlineFriendsInterfaceEpic::AcceptInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
	const FOnAcceptInviteComplete& Delegate)
{
	EOS_Friends_AcceptInviteOptions Options;
	Options.ApiVersion = EOS_FRIENDS_ACCEPTINVITE_API_LATEST;

	TSharedPtr<FUniqueNetId const> sourceUser = Subsystem->GetIdentityInterface()->GetUniquePlayerId(InLocalUserNum);
	Options.LocalUserId = FUniqueNetIdEpic::EpicAccountIDFromString(sourceUser->ToString());
	FOnlineFriend* Friend = GetFriend(InLocalUserNum, FriendId, ListName).Get();
	Options.TargetUserId = FUniqueNetIdEpic::EpicAccountIDFromString(Friend->GetDisplayName());

	EOS_Friends_AcceptInvite(this->friendsHandle, &Options, this, AcceptInviteCallback);
	TriggerOnOutgoingInviteSentDelegates(InLocalUserNum);

	return false;
}


void FOnlineFriendsInterfaceEpic::SetFriendAlias(int32 InLocalUserNum, const FUniqueNetId& FriendId,
	const FString& ListName, const FString& Alias, const FOnSetFriendAliasComplete& Delegate)
{

	FString ErrorMessage = "";
	TSharedPtr<FOnlineFriend> FriendPtr = GetFriend(InLocalUserNum, FriendId, ListName);
	bool bCouldSetAttribute = false;
	if (FriendPtr.Get()) {
		if (!FriendPtr->SetUserLocalAttribute(USER_ATTR_PREFERRED_DISPLAYNAME, Alias))
		{
			ErrorMessage = FString::Printf(TEXT("%s: the local attribute does not exist."), __FUNCTIONW__);
		}
	}

	if (!bCouldSetAttribute)
	{
		ErrorMessage = FString::Printf(TEXT("%s: you are requesting a friend who doesn't exist or isn't queried."), __FUNCTIONW__);
	}

	//Empty error string indicates that there is a success
	Delegate.ExecuteIfBound(InLocalUserNum, FriendId, Alias, FOnlineError(*ErrorMessage));
}

void FOnlineFriendsInterfaceEpic::SendInviteCallback(const EOS_Friends_SendInviteCallbackInfo* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
	FLocalUserData* InformationData = (FLocalUserData*)(Data->ClientData);
	check(InformationData);

	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		InformationData->OnlineFriendPtr->TriggerOnOutgoingInviteSentDelegates(InformationData->LocalPlayerNum);
	}
	else
	{
		FString DisplayName = FOnlineFriendEpic(Data->TargetUserId).GetDisplayName();
		FString CallbackError = FString::Printf(TEXT("[EOS SDK | Plugin] Error %s when sending invite to %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)), *DisplayName);
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s"), *CallbackError);
	}
}


void FOnlineFriendsInterfaceEpic::AcceptInviteCallback(const EOS_Friends_AcceptInviteCallbackInfo* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
	FLocalUserData* InformationData = (FLocalUserData*)(Data->ClientData);
	check(InformationData);

	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		InformationData->OnlineFriendPtr->TriggerOnInviteAcceptedDelegates(FUniqueNetIdEpic(Data->LocalUserId), FUniqueNetIdEpic(Data->TargetUserId));
	}
	else
	{
		FString DisplayName = FOnlineFriendEpic(Data->TargetUserId).GetDisplayName();
		FString CallbackError = FString::Printf(TEXT("[EOS SDK | Plugin] Error %s when sending invite to %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)), *DisplayName);
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s"), *CallbackError);
	}
}



void FOnlineFriendsInterfaceEpic::RejectInviteCallback(const EOS_Friends_RejectInviteCallbackInfo* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
	FLocalUserData* InformationData = (FLocalUserData*)(Data->ClientData);
	check(InformationData);

	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		//TODO - Pass in ListName and ErrorString
		InformationData->OnlineFriendPtr->TriggerOnRejectInviteCompleteDelegates(InformationData->LocalPlayerNum, true, FUniqueNetIdEpic(Data->TargetUserId), "", "");
	}
	else
	{
		FString DisplayName = FOnlineFriendEpic(Data->TargetUserId).GetDisplayName();
		FString CallbackError = FString::Printf(TEXT("[EOS SDK | Plugin] Error %s when sending invite to %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)), *DisplayName);
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s"), *CallbackError);
	}

	//TODO - Delete the struct data
}



bool FOnlineFriendsInterfaceEpic::QueryRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported at the moment."), *FString(__FUNCTION__));
	return false;
}

bool FOnlineFriendsInterfaceEpic::GetRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace,
	TArray<TSharedRef<FOnlineRecentPlayer>>& OutRecentPlayers)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported at the moment."), *FString(__FUNCTION__));
	return false;
}

void FOnlineFriendsInterfaceEpic::DumpRecentPlayers() const
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported at the moment."), *FString(__FUNCTION__));
}

/* ====================================== Currently unsupported functions ========================= */

bool FOnlineFriendsInterfaceEpic::DeleteFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the EOS SDK!"), *FString(__FUNCTION__));
	return false;
}

bool FOnlineFriendsInterfaceEpic::DeleteFriendsList(int32 InLocalUserNum, const FString& ListName,
	const FOnDeleteFriendsListComplete& Delegate)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the EOS SDK!"), *FString(__FUNCTION__));
	return false;
}

bool FOnlineFriendsInterfaceEpic::BlockPlayer(int32 InLocalUserNum, const FUniqueNetId& PlayerId)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the EOS SDK!"), *FString(__FUNCTION__));
	return false;
}

bool FOnlineFriendsInterfaceEpic::UnblockPlayer(int32 InLocalUserNum, const FUniqueNetId& PlayerId)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the EOS SDK!"), *FString(__FUNCTION__));
	return false;
}

bool FOnlineFriendsInterfaceEpic::QueryBlockedPlayers(const FUniqueNetId& UserId)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the EOS SDK!"), *FString(__FUNCTION__));
	return false;
}

bool FOnlineFriendsInterfaceEpic::GetBlockedPlayers(const FUniqueNetId& UserId,
	TArray<TSharedRef<FOnlineBlockedPlayer>>& OutBlockedPlayers)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the EOS SDK!"), *FString(__FUNCTION__));
	return false;
}

void FOnlineFriendsInterfaceEpic::DumpBlockedPlayers() const
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the EOS SDK!"), *FString(__FUNCTION__));
}

#if ENGINE_MINOR_VERSION >= 25
void FOnlineFriendsInterfaceEpic::DeleteFriendAlias(int32 LocalUserNum, const FUniqueNetId& FriendId,
	const FString& ListName, const FOnDeleteFriendAliasComplete& Delegate)
{
}
#endif
