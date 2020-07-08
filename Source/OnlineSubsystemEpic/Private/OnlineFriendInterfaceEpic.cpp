#include "OnlineFriendInterfaceEpic.h"
#include "CoreMinimal.h"

#include "eos_friends.h"
#include "eos_friends_types.h"
#include "OnlineSessionInterfaceEpic.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineUserInterfaceEpic.h"
#include "Utilities.h"
#include "Engine/Engine.h"

// FOnlineFriendEpic
FOnlineFriendEpic::FOnlineFriendEpic(const EOS_EpicAccountId& InUserId)
	: FriendStatus(EFriendStatus::NotFriends)
{
	TSharedRef<FUniqueNetIdEpic> epicNetId = MakeShared<FUniqueNetIdEpic>("");
	epicNetId->SetEpicAccountId(InUserId);

	UserId = epicNetId;
}

TSharedRef<const FUniqueNetId> FOnlineFriendEpic::GetUserId() const
{
	return UserId;
}

FString FOnlineFriendEpic::GetRealName() const
{
	FString Result;
	GetAccountData(TEXT("nickname"), Result);
	return Result;
}

//TODO - Is there a difference based on the platform? Have to figure this out
FString FOnlineFriendEpic::GetDisplayName(const FString& Platform) const
{
	FString Result;
	GetAccountData(TEXT("nickname"), Result);
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
	Subsystem(InSubsystem), LocalUserNum(0)
{
	check(Subsystem);
	this->friendsHandle = EOS_Platform_GetFriendsInterface(Subsystem->PlatformHandle);
}

bool FOnlineFriendInterfaceEpic::GetFriendsList(int32 InLocalUserNum, const FString& ListName,
	TArray<TSharedRef<FOnlineFriend>>& OutFriends)
{
	bool bResult = false;
	if (LocalUserNum < MAX_LOCAL_PLAYERS && Subsystem != nullptr) {
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
	
	LocalUserNum = InLocalUserNum;
	uint32 result = ONLINE_SUCCESS;
	
	EOS_Friends_QueryFriendsOptions Options;
	Options.ApiVersion = EOS_FRIENDS_QUERYFRIENDS_API_LATEST;

	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	TSharedPtr<const FUniqueNetIdEpic> userId = StaticCastSharedPtr<const FUniqueNetIdEpic>(identityPtr->GetUniquePlayerId(LocalUserNum));

	if (userId && userId.IsValid())
	{
		UE_LOG_ONLINE_FRIEND(Log, TEXT("%s: In User Id %s"), __FUNCTIONW__, *userId->ToDebugString());

		OnQueryUserInfoCompleteDelegate = FOnQueryUserInfoCompleteDelegate::CreateRaw(this, &FOnlineFriendInterfaceEpic::HandleQueryUserInfoComplete);
		OnQueryUserInfoCompleteDelegateHandle = this->Subsystem->GetUserInterface()->AddOnQueryUserInfoCompleteDelegate_Handle(InLocalUserNum, OnQueryUserInfoCompleteDelegate);

		Options.LocalUserId = userId->ToEpicAccountId();
		EOS_Friends_QueryFriends(this->friendsHandle, &Options, this, &FOnlineFriendInterfaceEpic::OnEOSQueryFriendsComplete);
	}
	else
	{
		UE_CLOG_ONLINE_USER(!userId || !userId.IsValid(), Display, TEXT("%s user ID is not valid!"), __FUNCTIONW__);
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
}

void FOnlineFriendInterfaceEpic::HandleQueryUserInfoComplete(int32 InLocalUserNum, bool bWasSuccessful, const TArray< TSharedRef<const FUniqueNetId> >& Ids, const FString& Test)
{
	GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, "Got into this callback");
	
	IOnlineUserPtr UserInterfacePtr = this->Subsystem->GetUserInterface();

	if (bWasSuccessful) {
		for (int32 UserIdx = 0; UserIdx < Ids.Num(); UserIdx++)
		{
			TSharedRef<FOnlineFriendEpic> CurrentFriend = FriendsLists[InLocalUserNum].Friends[UserIdx];
			CurrentFriend->AccountData[TEXT("nickname")] = UserInterfacePtr->GetUserInfo(InLocalUserNum, Ids[UserIdx].Get())->GetDisplayName();

			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, "Name is: " + CurrentFriend->AccountData[TEXT("nickname")]);
		}
	}

	// Clear delegates for the various async calls
	this->Subsystem->GetUserInterface()->ClearOnQueryUserInfoCompleteDelegate_Handle(0, OnQueryUserInfoCompleteDelegateHandle);
}

TSharedPtr<FOnlineFriend> FOnlineFriendInterfaceEpic::GetFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	TSharedPtr<FOnlineFriend> Result;
	if (LocalUserNum < MAX_LOCAL_PLAYERS && Subsystem != NULL)
	{
		FEpicFriendsList* FriendsList = FriendsLists.Find(LocalUserNum);
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

void FOnlineFriendInterfaceEpic::Tick(float DeltaTime)
{
	// ToDo: Iterate through all session searches and cancel them if timeout has been reached
}

// ---------------------------------------------
// EOS SDK Callback functions
// ---------------------------------------------

void FOnlineFriendInterfaceEpic::OnEOSQueryFriendsComplete(EOS_Friends_QueryFriendsCallbackInfo const* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{

		// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
		FOnlineFriendInterfaceEpic* ThisPtr = (FOnlineFriendInterfaceEpic*)(Data->ClientData);
		check(ThisPtr);
		
		EOS_Friends_GetFriendsCountOptions Options;
		Options.ApiVersion = EOS_FRIENDS_GETFRIENDSCOUNT_API_LATEST;
		Options.LocalUserId = Data->LocalUserId;
		
		int32_t FriendsCount = EOS_Friends_GetFriendsCount(ThisPtr->GetFriendsHandle(), &Options);

		FOnlineFriendInterfaceEpic::FEpicFriendsList& FriendsList = ThisPtr->FriendsLists.FindOrAdd(ThisPtr->LocalUserNum);
		//Pre-Size array for minimum re-allocs
		FriendsList.Friends.Empty(FriendsCount);

		
		EOS_Friends_GetFriendAtIndexOptions IndexOptions;
		IndexOptions.ApiVersion = EOS_FRIENDS_GETFRIENDATINDEX_API_LATEST;
		IndexOptions.LocalUserId = Data->LocalUserId;

		TArray < TSharedRef<const FUniqueNetId>> UserIds;
		for (int32_t FriendIdx = 0; FriendIdx < FriendsCount; ++FriendIdx)
		{
			IndexOptions.Index = FriendIdx;

			EOS_EpicAccountId FriendUserId = EOS_Friends_GetFriendAtIndex(ThisPtr->GetFriendsHandle(), &IndexOptions);

			if (EOS_EpicAccountId_IsValid(FriendUserId))
			{
				EOS_Friends_GetStatusOptions StatusOptions;
				StatusOptions.ApiVersion = EOS_FRIENDS_GETSTATUS_API_LATEST;
				StatusOptions.LocalUserId = Data->LocalUserId;
				StatusOptions.TargetUserId = FriendUserId;

				EOS_EFriendsStatus FriendStatus = EOS_Friends_GetStatus(ThisPtr->GetFriendsHandle(), &StatusOptions);
				
				// Add to list
				TSharedRef<FOnlineFriendEpic> Friend(new FOnlineFriendEpic(FriendUserId));

				FriendsList.Friends.Add(Friend);
				//NOTE: We only get the actual info from the account IDs
				Friend->AccountData.Add(TEXT("nickname"), "Pending...");

				UserIds.Add(Friend->UserId);
				Friend->FriendStatus = (EFriendStatus)FriendStatus;

			}
		}

		//Query the array of friends
		ThisPtr->Subsystem->GetUserInterface()->QueryUserInfo(ThisPtr->LocalUserNum, UserIds);
		//TODO - Do a Finalize call where I can actually set the names of the friends
	}
	else {
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s asynchronous call was not successful!"), __FUNCTIONW__);
	}
}

bool FOnlineFriendInterfaceEpic::DeleteFriendsList(int32 InLocalUserNum, const FString& ListName,
	const FOnDeleteFriendsListComplete& Delegate)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the Epic subsystems!"), __FUNCTIONW__);
	return false;
}

bool FOnlineFriendInterfaceEpic::SendInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
	const FOnSendInviteComplete& Delegate)
{
	EOS_Friends_SendInviteOptions Options;
	Options.ApiVersion = EOS_FRIENDS_SENDINVITE_API_LATEST;

	TSharedPtr<FUniqueNetId const> sourceUser = Subsystem->GetIdentityInterface()->GetUniquePlayerId(LocalUserNum);
	Options.LocalUserId = FUniqueNetIdEpic::EpicAccountIDFromString(sourceUser->ToString());
	FOnlineFriend* Friend = GetFriend(LocalUserNum, FriendId, ListName).Get();
	Options.TargetUserId = FUniqueNetIdEpic::EpicAccountIDFromString(Friend->GetDisplayName());

	EOS_Friends_SendInvite(this->friendsHandle, &Options, this, SendInviteCallback);
	TriggerOnOutgoingInviteSentDelegates(LocalUserNum);
	Delegate.ExecuteIfBound(LocalUserNum, true, FriendId, ListName, TEXT(""));

	return true;
}

void FOnlineFriendInterfaceEpic::SendInviteCallback(const EOS_Friends_SendInviteCallbackInfo* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	//Data->ClientData
	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
		FOnlineFriendInterfaceEpic* ThisPtr = (FOnlineFriendInterfaceEpic*)(Data->ClientData);
		check(ThisPtr);

		//TODO - Complete
		/*if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			if (FriendsHandle->OnFriendInviteSent.IsBound()) {
				FriendsHandle->OnFriendInviteSent.Broadcast();
			}
		}
		else
		{
			FString DisplayName = UEOSManager::GetEOSManager()->GetUserInfo()->GetDisplayName(Data->TargetUserId);
			FString CallbackError = FString::Printf(TEXT("[EOS SDK | Plugin] Error %s when sending invite to %s"), *UEOSCommon::EOSResultToString(Data->ResultCode), *DisplayName);
			//NOTE: Leave it to blueprint to log the warning
			if (EOSFriends->OnFriendActionError.IsBound()) {
				EOSFriends->OnFriendActionError.Broadcast(CallbackError);
			}
		}*/
	}
	else {
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s asynchronous call was not successful!"), __FUNCTIONW__);
	}
}


bool FOnlineFriendInterfaceEpic::RejectInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	EOS_Friends_RejectInviteOptions Options;
	Options.ApiVersion = EOS_FRIENDS_REJECTINVITE_API_LATEST;

	TSharedPtr<FUniqueNetId const> sourceUser = Subsystem->GetIdentityInterface()->GetUniquePlayerId(LocalUserNum);
	Options.LocalUserId = FUniqueNetIdEpic::EpicAccountIDFromString(sourceUser->ToString());
	FOnlineFriend* Friend = GetFriend(LocalUserNum, FriendId, ListName).Get();
	Options.TargetUserId = FUniqueNetIdEpic::EpicAccountIDFromString(Friend->GetDisplayName());

	EOS_Friends_RejectInvite(this->friendsHandle, &Options, this, RejectInviteCallback);
	TriggerOnRejectInviteCompleteDelegates(LocalUserNum, true, FriendId, ListName, "");

	return false;
}

void FOnlineFriendInterfaceEpic::SetFriendAlias(int32 InLocalUserNum, const FUniqueNetId& FriendId,
	const FString& ListName, const FString& Alias, const FOnSetFriendAliasComplete& Delegate)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported!"), __FUNCTIONW__);
}

bool FOnlineFriendInterfaceEpic::DeleteFriend(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName)
{
	UE_LOG_ONLINE_FRIEND(Error, TEXT("%s call not supported by the Epic subsystems!"), __FUNCTIONW__);
	return false;
}

void FOnlineFriendInterfaceEpic::RejectInviteCallback(const EOS_Friends_RejectInviteCallbackInfo* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	//Data->ClientData
	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
		FOnlineFriendInterfaceEpic* ThisPtr = (FOnlineFriendInterfaceEpic*)(Data->ClientData);
		check(ThisPtr);

		//TODO - Complete
		/*if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			if (FriendsHandle->OnFriendInviteSent.IsBound()) {
				FriendsHandle->OnFriendInviteSent.Broadcast();
			}
		}
		else
		{
			FString DisplayName = UEOSManager::GetEOSManager()->GetUserInfo()->GetDisplayName(Data->TargetUserId);
			FString CallbackError = FString::Printf(TEXT("[EOS SDK | Plugin] Error %s when sending invite to %s"), *UEOSCommon::EOSResultToString(Data->ResultCode), *DisplayName);
			//NOTE: Leave it to blueprint to log the warning
			if (EOSFriends->OnFriendActionError.IsBound()) {
				EOSFriends->OnFriendActionError.Broadcast(CallbackError);
			}
		}*/
	}
	else {
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s asynchronous call was not successful!"), __FUNCTIONW__);
	}
}



bool FOnlineFriendInterfaceEpic::AcceptInvite(int32 InLocalUserNum, const FUniqueNetId& FriendId, const FString& ListName,
	const FOnAcceptInviteComplete& Delegate)
{
	EOS_Friends_AcceptInviteOptions Options;
	Options.ApiVersion = EOS_FRIENDS_ACCEPTINVITE_API_LATEST;

	TSharedPtr<FUniqueNetId const> sourceUser = Subsystem->GetIdentityInterface()->GetUniquePlayerId(LocalUserNum);
	Options.LocalUserId = FUniqueNetIdEpic::EpicAccountIDFromString(sourceUser->ToString());
	FOnlineFriend* Friend = GetFriend(LocalUserNum, FriendId, ListName).Get();
	Options.TargetUserId = FUniqueNetIdEpic::EpicAccountIDFromString(Friend->GetDisplayName());

	EOS_Friends_AcceptInvite(this->friendsHandle, &Options, this, AcceptInviteCallback);
	TriggerOnOutgoingInviteSentDelegates(LocalUserNum);

	return false;
}

void FOnlineFriendInterfaceEpic::AcceptInviteCallback(const EOS_Friends_AcceptInviteCallbackInfo* Data)
{
	// Make sure the received data is valid on a low level
	check(Data);

	//Data->ClientData
	if (Data->ResultCode == EOS_EResult::EOS_Success)
	{
		// To raise the friends query complete delegate the interface itself has to be retrieved from the returned data
		FOnlineFriendInterfaceEpic* ThisPtr = (FOnlineFriendInterfaceEpic*)(Data->ClientData);
		check(ThisPtr);

		//TODO - Complete
		/*if (Data->ResultCode == EOS_EResult::EOS_Success)
		{
			if (FriendsHandle->OnFriendInviteSent.IsBound()) {
				FriendsHandle->OnFriendInviteSent.Broadcast();
			}
		}
		else
		{
			FString DisplayName = UEOSManager::GetEOSManager()->GetUserInfo()->GetDisplayName(Data->TargetUserId);
			FString CallbackError = FString::Printf(TEXT("[EOS SDK | Plugin] Error %s when sending invite to %s"), *UEOSCommon::EOSResultToString(Data->ResultCode), *DisplayName);
			//NOTE: Leave it to blueprint to log the warning
			if (EOSFriends->OnFriendActionError.IsBound()) {
				EOSFriends->OnFriendActionError.Broadcast(CallbackError);
			}
		}*/
	}
	else {
		UE_LOG_ONLINE_FRIEND(Error, TEXT("%s asynchronous call was not successful!"), __FUNCTIONW__);
	}
}
