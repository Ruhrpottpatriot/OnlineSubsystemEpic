//Copyright (c) MikhailSorokin
#include "OnlineFriendInterfaceEpic.h"
#include "CoreMinimal.h"

#include "eos_friends.h"
#include "eos_friends_types.h"
#include "OnlineSessionInterfaceEpic.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineUserInterfaceEpic.h"
#include "Utilities.h"
#include "Engine/Engine.h"
#include "OnlinePresenceEpic.h"

typedef struct FLocalUserData
{
	FOnlineFriendInterfaceEpic* OnlineFriendPtr;
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
	GetAccountData(USER_ATTR_REALNAME, Result);
	return Result;
}

FString FOnlineFriendEpic::GetDisplayName(const FString& Platform) const
{
	//TODO - Not sure what to do platform here
	FString Result;
	GetAccountData(USER_ATTR_DISPLAYNAME, Result);
	return Result;
}

bool FOnlineFriendEpic::GetUserAttribute(const FString& AttrName, FString& OutAttrValue) const
{
	return GetAccountData(AttrName, OutAttrValue);
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

const FOnlineUserPresence& FOnlineFriendEpic::GetPresence() const
{
	return Presence;
}

//TODO - Rename to FOnlineFriendsInterfaceEpic instead of FOnlineFriendInterfaceEpic
//-------------------------------
// FOnlineFriendsInterfaceEpic
//-------------------------------

FOnlineFriendInterfaceEpic::FOnlineFriendInterfaceEpic(FOnlineSubsystemEpic* InSubsystem) :
	Subsystem(InSubsystem)
{
	check(Subsystem);
	this->friendsHandle = EOS_Platform_GetFriendsInterface(Subsystem->PlatformHandle);
}

bool FOnlineFriendInterfaceEpic::GetFriendsList(int32 InLocalUserNum, const FString& ListName,
	TArray<TSharedRef<FOnlineFriend>>& OutFriends)
{
	bool bResult = false;
	if (InLocalUserNum < MAX_LOCAL_PLAYERS && Subsystem != nullptr) {
		FEpicFriendsList* FriendsList = FriendsLists.Find(InLocalUserNum);
		if (FriendsList != nullptr) {
			for (int FriendInd = 0; FriendInd < FriendsList->Friends.Num(); FriendInd++) {
				OutFriends.Add(FriendsList->Friends[FriendInd]);
			}
			bResult = true;
		}
	}
	return bResult;
}

bool FOnlineFriendInterfaceEpic::ReadFriendsList(int32 InLocalUserNum, const FString& ListName,
	const FOnReadFriendsListComplete& Delegate)
{
	UE_LOG_ONLINE_FRIEND(Log, TEXT("%s: calling friend query."), __FUNCTIONW__);

	if (!this->Subsystem || !this->Subsystem->GetIdentityInterface()) {
		UE_LOG_ONLINE_FRIEND(Display, TEXT("Subsystem not set up or identity interface could not be used!"));

		return false;
	}
	
	uint32 result = ONLINE_SUCCESS;
	
	EOS_Friends_QueryFriendsOptions Options;
	Options.ApiVersion = EOS_FRIENDS_QUERYFRIENDS_API_LATEST;

	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	TSharedPtr<const FUniqueNetIdEpic> userId = StaticCastSharedPtr<const FUniqueNetIdEpic>(identityPtr->GetUniquePlayerId(InLocalUserNum));

	if (userId && userId.IsValid())
	{
		UE_LOG_ONLINE_FRIEND(Log, TEXT("%s: In User Id %s"), __FUNCTIONW__, *userId->ToDebugString());

		OnQueryUserInfoCompleteDelegate = FOnQueryUserInfoCompleteDelegate::CreateRaw(this, &FOnlineFriendInterfaceEpic::HandleQueryUserInfoComplete);
		OnQueryUserInfoCompleteDelegateHandle = this->Subsystem->GetUserInterface()->AddOnQueryUserInfoCompleteDelegate_Handle(InLocalUserNum, OnQueryUserInfoCompleteDelegate);

		Options.LocalUserId = userId->ToEpicAccountId();

		FLocalUserData* UserData = new FLocalUserData
		{
			this,
			friendsHandle,
			InLocalUserNum
		};

		ReadFriendsListDelegate = Delegate;
		EOS_Friends_QueryFriends(this->friendsHandle, &Options, UserData, &FOnlineFriendInterfaceEpic::OnEASQueryFriendsComplete);
	}
	else
	{
		UE_CLOG_ONLINE_USER(!userId || !userId.IsValid(), Display, TEXT("%s user ID is not valid!"), __FUNCTIONW__);
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
}

void FOnlineFriendInterfaceEpic::HandleQueryUserInfoComplete(int32 InLocalUserNum, bool bWasSuccessful, const TArray< TSharedRef<const FUniqueNetId> >& Ids, const FString& Error)
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, "Got into this callback");
	UE_LOG(LogTemp, Log, TEXT("Query user callback result: %d with number of ids: %d"), bWasSuccessful, Ids.Num());

	IOnlineUserPtr UserInterfacePtr = this->Subsystem->GetUserInterface();

	if (bWasSuccessful) {
		for (int32 UserIdx = 0; UserIdx < Ids.Num(); UserIdx++)
		{
			TSharedRef<FOnlineFriendEpic> CurrentFriend = FriendsLists[InLocalUserNum].Friends[UserIdx];
			CurrentFriend->AccountData[USER_ATTR_DISPLAYNAME] = UserInterfacePtr->GetUserInfo(InLocalUserNum, Ids[UserIdx].Get())->GetDisplayName();
			//CurrentFriend->AccountData[USER_ATTR_REALNAME] = UserInterfacePtr->GetUserInfo(InLocalUserNum, Ids[UserIdx].Get())->GetDisplayName();
			//CurrentFriend->AccountData[USER_ATTR_PREFERRED_DISPLAYNAME] = UserInterfacePtr->GetUserInfo(InLocalUserNum, Ids[UserIdx].Get())->GetDisplayName();
			//CurrentFriend->AccountData[USER_ATTR_ALIAS] = UserInterfacePtr->GetUserInfo(InLocalUserNum, Ids[UserIdx].Get())->GetDisplayName();

			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "Name is: " + CurrentFriend->AccountData[USER_ATTR_DISPLAYNAME]);
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
	this->Subsystem->GetUserInterface()->ClearOnQueryUserInfoCompleteDelegate_Handle(0, OnQueryUserInfoCompleteDelegateHandle);
}

void FOnlineFriendInterfaceEpic::Tick(float DeltaTime)
{
}

TSharedPtr<FOnlineFriend> FOnlineFriendInterfaceEpic::GetFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	TSharedPtr<FOnlineFriend> Result;
	if (InLocalUserNum < MAX_LOCAL_PLAYERS && Subsystem != NULL)
	{
		FEpicFriendsList* FriendsList = FriendsLists.Find(InLocalUserNum);
		if (FriendsList != NULL)
		{
			for (int32 FriendIdx = 0; FriendIdx < FriendsList->Friends.Num(); FriendIdx++)
			{
				if (*FriendsList->Friends[FriendIdx]->GetUserId() == FriendId)
				{
					Result = FriendsList->Friends[FriendIdx];
					break;
				}
			}
		}
	}
	return Result;
}

bool FOnlineFriendInterfaceEpic::IsFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	FOnlineFriend* Friend = GetFriend(InLocalUserNum, FriendId, ListName).Get();
	FOnlineFriendEpic EpicFriend = FOnlineFriendEpic(FUniqueNetIdEpic::EpicAccountIDFromString(FriendId.ToString()));
	return (EpicFriend.FriendStatus != EFriendStatus::NotFriends);
}

bool FOnlineFriendInterfaceEpic::QueryRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace)
{
	return false;
}

bool FOnlineFriendInterfaceEpic::GetRecentPlayers(const FUniqueNetId& UserId, const FString& Namespace,
	TArray<TSharedRef<FOnlineRecentPlayer>>& OutRecentPlayers)
{
	return false;
}

void FOnlineFriendInterfaceEpic::DumpRecentPlayers() const
{
	
}

bool FOnlineFriendInterfaceEpic::BlockPlayer(int32 InLocalUserNum, const FUniqueNetId& PlayerId)
{
	return false;
}

bool FOnlineFriendInterfaceEpic::UnblockPlayer(int32 InLocalUserNum, const FUniqueNetId& PlayerId)
{
	return false;
}

bool FOnlineFriendInterfaceEpic::QueryBlockedPlayers(const FUniqueNetId& UserId)
{
	return false;
}

bool FOnlineFriendInterfaceEpic::GetBlockedPlayers(const FUniqueNetId& UserId,
	TArray<TSharedRef<FOnlineBlockedPlayer>>& OutBlockedPlayers)
{
	return false;
}

void FOnlineFriendInterfaceEpic::DumpBlockedPlayers() const
{
}


// ---------------------------------------------
// EOS SDK Callback functions
// ---------------------------------------------

void FOnlineFriendInterfaceEpic::OnEASQueryFriendsComplete(EOS_Friends_QueryFriendsCallbackInfo const* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
	FLocalUserData* ThisPtr = (FLocalUserData*)(Data->ClientData);
	check(ThisPtr);

	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{

		FOnlineFriendInterfaceEpic* FriendInterfaceEpic = ThisPtr->OnlineFriendPtr;
		
		EOS_Friends_GetFriendsCountOptions Options;
		Options.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
		Options.LocalUserId = Data->LocalUserId;
		
		int32_t FriendsCount = EOS_Friends_GetFriendsCount(ThisPtr->FriendsHandle, &Options);

		UE_LOG_ONLINE_FRIEND(Log, TEXT("%s number of friends is: %d"), __FUNCTIONW__, FriendsCount);

		
		FOnlineFriendInterfaceEpic::FEpicFriendsList& FriendsList = FriendInterfaceEpic->FriendsLists.FindOrAdd(ThisPtr->LocalPlayerNum);
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
				//NOTE: We only get the actual info from the account IDs
				Friend->AccountData.Add(USER_ATTR_DISPLAYNAME, "Pending...");

				UserIds.Add(Friend->UserId);
				Friend->FriendStatus = (EFriendStatus)FriendStatus;

				IOnlinePresence::FOnPresenceTaskCompleteDelegate DelegateHandle = IOnlinePresence::FOnPresenceTaskCompleteDelegate::CreateRaw(ThisPtr->OnlineFriendPtr, &FOnlineFriendInterfaceEpic::OnFriendQueryPresenceComplete);
				ThisPtr->OnlineFriendPtr->DelayedPresenceDelegates.Add(FUniqueNetIdEpic(Friend->UserId.Get()), MakeShared<const IOnlinePresence::FOnPresenceTaskCompleteDelegate>(DelegateHandle));
				ThisPtr->OnlineFriendPtr->Subsystem->GetPresenceInterface()->QueryPresence(Friend->UserId.Get(), DelegateHandle);
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

void FOnlineFriendInterfaceEpic::OnFriendQueryPresenceComplete(const class FUniqueNetId& UserId, const bool bWasSuccessful)
{
	TSharedPtr<FOnlineFriend> OnlineAccount = this->GetFriend(0, FUniqueNetIdEpic(UserId), "Default");

	if (bWasSuccessful) {
		IOnlinePresencePtr FriendPresencePtr = this->Subsystem->GetPresenceInterface();
		TSharedPtr<FOnlineUserPresence> UserPresence;
		FriendPresencePtr->GetCachedPresence(UserId, UserPresence);
		FOnlineFriendEpic* OnlineFriendEpic = static_cast<FOnlineFriendEpic*>(OnlineAccount.Get());
		//It doesn't matter when presence is set whether before or after query of name
		OnlineFriendEpic->SetPresence(*UserPresence.Get());
	}
	else
	{
		UE_LOG_ONLINE_FRIEND(Error, TEXT("could not set presence for this account"));
		//UE_LOG_ONLINE_FRIEND(Error, TEXT("%s could not set presence for account: %s"), __FUNCTIONW__, *OnlineAccount->GetUserId()->ToDebugString());
	}

	const FUniqueNetIdEpic& EpicId = (const FUniqueNetIdEpic&)(UserId);

	// Clear delegates for the presence async call
	DelayedPresenceDelegates.Remove(EpicId);
}

bool FOnlineFriendInterfaceEpic::DeleteFriendsList(int32 InLocalUserNum, const FString& ListName,
	const FOnDeleteFriendsListComplete& Delegate)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the Epic subsystems!"), __FUNCTIONW__);
	return false;
}

/* ============================================= INVITE INFORMATION ===================================== */
bool FOnlineFriendInterfaceEpic::SendInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
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
			friendsHandle,
			InLocalUserNum
	};
	
	EOS_Friends_SendInvite(this->friendsHandle, &Options, InformationData, SendInviteCallback);

	return true;
}

bool FOnlineFriendInterfaceEpic::RejectInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
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

bool FOnlineFriendInterfaceEpic::AcceptInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
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


void FOnlineFriendInterfaceEpic::SetFriendAlias(int32 InLocalUserNum, const FUniqueNetId& FriendId,
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

bool FOnlineFriendInterfaceEpic::DeleteFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the Epic subsystem!"), __FUNCTIONW__);
	return false;
}


void FOnlineFriendInterfaceEpic::SendInviteCallback(const EOS_Friends_SendInviteCallbackInfo* Data)
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


void FOnlineFriendInterfaceEpic::AcceptInviteCallback(const EOS_Friends_AcceptInviteCallbackInfo* Data)
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



void FOnlineFriendInterfaceEpic::RejectInviteCallback(const EOS_Friends_RejectInviteCallbackInfo* Data)
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
