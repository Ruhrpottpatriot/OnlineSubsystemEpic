#include "OnlinePresenceEpic.h"
#include "eos_presence.h"
#include "OnlineSubsystemEpicTypes.h"
#include "eos_connect.h"
#include "eos_userinfo.h"
#include "eos_sessions.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Interfaces/OnlineSessionInterface.h"

// ---------------------------------------------
// Implementation file only structs
// These structs carry additional informations to the callbacks
// ---------------------------------------------
typedef struct FPresenceAdditionalData
{
	FOnlinePresenceEpic const* This;
	const FUniqueNetId& EpicNetId;
	TSharedRef<const IOnlinePresence::FOnPresenceTaskCompleteDelegate> Delegate;
} FSetPresenceAdditionalData;

typedef struct FQueryExternalMappingForPresenceAdditionalInformation
{
	FOnlinePresenceEpic* PresencePtr;
	EOS_EpicAccountId TargetId;
	TSharedPtr<FUniqueNetId const> LocalUser;
} FQueryExternalMappingForPresenceAdditionalInformation;

// -----------------------------
// EOS Callbacks
// -----------------------------
void FOnlinePresenceEpic::EOS_SetPresenceComplete(EOS_Presence_SetPresenceCallbackInfo const* data)
{
	FPresenceAdditionalData* additionalData = static_cast<FPresenceAdditionalData*>(data->ClientData);

	if (data->ResultCode == EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_PRESENCE(Display, TEXT("[EOS SDK] Sucessfully updated presence for user \"%s\""), *FUniqueNetIdEpic::EpicAccountIdToString(data->LocalUserId));
		additionalData->Delegate->ExecuteIfBound(additionalData->EpicNetId, true);
	}
	else
	{
		UE_LOG_ONLINE_PRESENCE(Warning, TEXT("[EOS SDK] Couldn't update presence information. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(data->ResultCode)));
		additionalData->Delegate->ExecuteIfBound(additionalData->EpicNetId, false);
	}

	// Release the additional data memory
	delete(additionalData);
}

void FOnlinePresenceEpic::EOS_QueryPresenceComplete(EOS_Presence_QueryPresenceCallbackInfo const* data)
{
	FPresenceAdditionalData* additionalData = static_cast<FPresenceAdditionalData*>(data->ClientData);

	bool success = data->ResultCode == EOS_EResult::EOS_Success;

	UE_CLOG_ONLINE_PRESENCE(success, Display, TEXT("[EOS SDK] Sucessfully queried presence for user: %s"), *FUniqueNetIdEpic(data->TargetUserId).ToString());
	UE_CLOG_ONLINE_PRESENCE(!success, Warning, TEXT("[EOS SDK] QueryPresence encountered an error: %s"), *FString(__FUNCTION__));

	//Right now, only the FriendsInterface listens for this event - but it can be done anywhere
	additionalData->Delegate->ExecuteIfBound(FUniqueNetIdEpic(data->TargetUserId), success);

	delete additionalData;
}

void FOnlinePresenceEpic::EOS_OnPresenceChanged(EOS_Presence_PresenceChangedCallbackInfo const* data)
{
	FOnlinePresenceEpic* THIS = static_cast<FOnlinePresenceEpic*>(data->ClientData);

	// After receiving a valid EAID, we can get the cached presence for it
	FUniqueNetIdEpic targetEpicNetId = FUniqueNetIdEpic(data->PresenceUserId);
	TSharedPtr<FOnlineUserPresence> targetPresence;
	// Sometimes presence may change before we are finished with our query, so don't do anything until query is confirmed to be valid
	if (EOnlineCachedResult::NotFound != THIS->GetCachedPresence(targetEpicNetId, targetPresence))
		THIS->TriggerOnPresenceReceivedDelegates(targetEpicNetId, targetPresence.ToSharedRef());
}

void FOnlinePresenceEpic::EOS_QueryExternalAccountMappingsForPresenceComplete(EOS_Connect_QueryExternalAccountMappingsCallbackInfo const* data)
{
	auto additionalData = static_cast<FQueryExternalMappingForPresenceAdditionalInformation*>(data->ClientData);
	FOnlinePresenceEpic* THIS = additionalData->PresencePtr;

	if (data->ResultCode == EOS_EResult::EOS_Success)
	{
		EOS_HConnect connectHandle = EOS_Platform_GetConnectInterface(THIS->subsystem->PlatformHandle);
		EOS_Connect_GetExternalAccountMappingsOptions getExternalMappingOpts = {
				EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST,
				data->LocalUserId,
				EOS_EExternalAccountType::EOS_EAT_EPIC,
				TCHAR_TO_UTF8(*FUniqueNetIdEpic::EpicAccountIdToString(additionalData->TargetId))
		};
		EOS_ProductUserId targetPUID = EOS_Connect_GetExternalAccountMapping(connectHandle, &getExternalMappingOpts);
		if (EOS_ProductUserId_IsValid(targetPUID))
		{
			FUniqueNetIdEpic targetEpicNetId(targetPUID, additionalData->TargetId);

			TSharedPtr<FOnlineUserPresence> targetPresence;
			EOnlineCachedResult::Type cacheResult = THIS->GetCachedPresence(targetEpicNetId, targetPresence);
			if (cacheResult == EOnlineCachedResult::Success)
			{
				THIS->TriggerOnPresenceReceivedDelegates(*additionalData->LocalUser, targetPresence.ToSharedRef());
			}
			else
			{
				// If the user, that got his presence updated is not in the cache, we need to query them
				// Usually this shouldn't happen, but we never know.
				// Using a lambda here makes the code more readable
				auto completeFunc = [THIS](const class FUniqueNetId& UserId, const bool bWasSuccessful)
				{
					TSharedPtr<FOnlineUserPresence> queriedPresence;
					EOnlineCachedResult::Type cacheResult = THIS->GetCachedPresence(UserId, queriedPresence);
					if (cacheResult == EOnlineCachedResult::Success)
					{
						THIS->TriggerOnPresenceReceivedDelegates(UserId, queriedPresence.ToSharedRef());
					}
					else
					{
						UE_LOG_ONLINE_PRESENCE(Warning, TEXT("Recieved presence update, but couldn't retrive user presence information."));
					}
				};
				THIS->QueryPresence(targetEpicNetId, FOnPresenceTaskCompleteDelegate::CreateLambda(completeFunc));
			}
		}
		else
		{
			// We already queried once, doing it again (possibly ad infinitum) won't yield anything
			UE_LOG_ONLINE_PRESENCE(Warning, TEXT("Tried querying account info for presence, but account couldn't be found."));
		}
	}
	else
	{
		UE_LOG_ONLINE_PRESENCE(Warning, TEXT("Couldn't query external account mapping for presence information"));
	}

	delete additionalData;
}


//-------------------------------
// Utility Methods
//-------------------------------
EOnlinePresenceState::Type FOnlinePresenceEpic::EOSPresenceStateToUEPresenceState(EOS_Presence_EStatus status) const
{
	switch (status)
	{
	case EOS_Presence_EStatus::EOS_PS_Offline:
		return EOnlinePresenceState::Offline;
	case  EOS_Presence_EStatus::EOS_PS_Online:
		return EOnlinePresenceState::Online;
	case EOS_Presence_EStatus::EOS_PS_Away:
		return EOnlinePresenceState::Away;
	case EOS_Presence_EStatus::EOS_PS_ExtendedAway:
		return EOnlinePresenceState::ExtendedAway;
	case EOS_Presence_EStatus::EOS_PS_DoNotDisturb:
		return EOnlinePresenceState::DoNotDisturb;
	}
	// Default to Offline / generally unavailable
	return EOnlinePresenceState::Offline;
}

EOS_Presence_EStatus FOnlinePresenceEpic::UEPresenceStateToEOSPresenceState(EOnlinePresenceState::Type status) const
{
	switch (status)
	{
	default:
		break;
	case EOnlinePresenceState::Online:
		return EOS_Presence_EStatus::EOS_PS_Online;
	case EOnlinePresenceState::Offline:
		return EOS_Presence_EStatus::EOS_PS_Offline;
	case EOnlinePresenceState::Away:
		return EOS_Presence_EStatus::EOS_PS_Away;
	case EOnlinePresenceState::ExtendedAway:
		return EOS_Presence_EStatus::EOS_PS_ExtendedAway;
	case EOnlinePresenceState::DoNotDisturb:
		return EOS_Presence_EStatus::EOS_PS_DoNotDisturb;
	}
	// Default to Offline / generally unavailable
	return EOS_Presence_EStatus::EOS_PS_Offline;
}


//-------------------------------
// FOnlineIdentityInterfaceEpic
//-------------------------------
FOnlinePresenceEpic::FOnlinePresenceEpic(FOnlineSubsystemEpic const* InSubsystem)
	: subsystem(InSubsystem)
{
	this->presenceHandle = EOS_Platform_GetPresenceInterface(this->subsystem->PlatformHandle);
	PresenceNotifications = TMap<EOS_EpicAccountId, EOS_NotificationId>();
}


void FOnlinePresenceEpic::QueryPresence(const FUniqueNetId& User, const FOnPresenceTaskCompleteDelegate& Delegate)
{

	EOS_Presence_AddNotifyOnPresenceChangedOptions onPresenceChangedOptions = {
		EOS_PRESENCE_ADDNOTIFYONPRESENCECHANGED_API_LATEST
	};

	FUniqueNetIdEpic const& epicUser = FUniqueNetIdEpic(User);

	if (!PresenceNotifications.Contains(epicUser.ToEpicAccountId())) {
		EOS_NotificationId PresenceNotificationHandle = EOS_Presence_AddNotifyOnPresenceChanged(this->presenceHandle, &onPresenceChangedOptions, this, &FOnlinePresenceEpic::EOS_OnPresenceChanged);

		//We add the presence status in a map for every target user that we meet through this function
		if (!PresenceNotificationHandle)
		{
			UE_LOG_ONLINE_PRESENCE(Warning, TEXT("%s: could not subscribe to presence updates."), *FString(__FUNCTION__));
			return;
		}
		else
		{
			/*
			 * Add a subscription to a presence event when a query needs to be done.
			 * NOTE: Only add this subscription if one hasn't done it already.
			 */
			PresenceNotifications.Add(epicUser.ToEpicAccountId(), PresenceNotificationHandle);
		}

		// Loop through all local players to get any active local players to query as well
		TArray<TSharedPtr<FUserOnlineAccount>> UserAccountArray = subsystem->GetIdentityInterface()->GetAllUserAccounts();

		for (TSharedPtr<FUserOnlineAccount> UserAccount : UserAccountArray) {
			TSharedRef<FUniqueNetId const> localUser = UserAccount->GetUserId();

			if (epicUser.IsEpicAccountIdValid())
			{
				EOS_Presence_QueryPresenceOptions queryPresenceOptions = {
					EOS_PRESENCE_QUERYPRESENCE_API_LATEST,
					FUniqueNetIdEpic(localUser.Get()).ToEpicAccountId(),
					epicUser.ToEpicAccountId()
				};
				FPresenceAdditionalData* additionalData = new FPresenceAdditionalData{
					this,
					epicUser,
					MakeShared<const IOnlinePresence::FOnPresenceTaskCompleteDelegate>(Delegate)
				};

				EOS_Presence_QueryPresence(this->presenceHandle, &queryPresenceOptions, additionalData, &FOnlinePresenceEpic::EOS_QueryPresenceComplete);
			}
			else
			{
				UE_LOG_ONLINE_PRESENCE(Warning, TEXT("%s: UserId doesn't contain a valid epic account id."), *FString(__FUNCTION__));
			}
		}
	}
	else
	{
		// We should have this user cached, no need to query anymore - broadcast the information from cache
		Delegate.ExecuteIfBound(User, true);
		TSharedPtr<FOnlineUserPresence> Presence;
		GetCachedPresence(User, Presence);
		TriggerOnPresenceReceivedDelegates(User, Presence.ToSharedRef());
	}
}

EOnlineCachedResult::Type FOnlinePresenceEpic::GetCachedPresence(const FUniqueNetId& User, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	IOnlineIdentityPtr identityPtr = this->subsystem->GetIdentityInterface();
	
	EOnlineCachedResult::Type result = EOnlineCachedResult::NotFound;
	FString error;

	TSharedPtr<const FUniqueNetId> LocalUserId = identityPtr->GetUniquePlayerId(0);
	FUniqueNetIdEpic EpicLocalUserId = FUniqueNetIdEpic(*LocalUserId.Get());
	FUniqueNetIdEpic const& TargetUserId = static_cast<FUniqueNetIdEpic const>(User);
	if (PresenceNotifications.Contains(TargetUserId.ToEpicAccountId()) && TargetUserId.IsEpicAccountIdValid())
	{

		UE_LOG_ONLINE_PRESENCE(Log, TEXT("%s getting cached presence for user: %s"), *FString(__FUNCTION__), *TargetUserId.ToString());
		// We need to put in our local id and our target id for presence queries here
		EOS_Presence_Info* presenceInfo = nullptr;
		EOS_Presence_CopyPresenceOptions copyPresenceOptions = {
			EOS_PRESENCE_COPYPRESENCE_API_LATEST,
			EpicLocalUserId.ToEpicAccountId(),
			TargetUserId.ToEpicAccountId()
		};
		EOS_EResult EOS_EResult = EOS_Presence_CopyPresence(this->presenceHandle, &copyPresenceOptions, &presenceInfo);
		if (EOS_EResult == EOS_EResult::EOS_Success)
		{
			// Add remaining presence fields to a users presence status, which include additional information 
			FOnlineUserPresenceStatus presenceStatus;
			for (int32 i = 0; i < presenceInfo->RecordsCount; ++i)
			{
				EOS_Presence_DataRecord const record = presenceInfo->Records[i];
				presenceStatus.Properties.Add(UTF8_TO_TCHAR(record.Key), UTF8_TO_TCHAR(record.Value));
			}

			// ToDo: Check if there's a better way to do this
			// This seems solid, I do something similar in Friends Interface - Mike
			presenceStatus.Properties.Add(TEXT("ProductName"), UTF8_TO_TCHAR(presenceInfo->ProductName));
			presenceStatus.Properties.Add(TEXT("ProductVersion"), UTF8_TO_TCHAR(presenceInfo->ProductVersion));
			presenceStatus.Properties.Add(TEXT("Platform"), UTF8_TO_TCHAR(presenceInfo->Platform));
			presenceStatus.Properties.Add(TEXT("ProductVersion"), UTF8_TO_TCHAR(presenceInfo->ProductVersion));
			presenceStatus.StatusStr = UTF8_TO_TCHAR(presenceInfo->RichText);
			presenceStatus.State = EOSPresenceStateToUEPresenceState(presenceInfo->Status);

			FString appId = this->subsystem->GetAppId();

			FString projectId;
			FString projectVersion;
			appId.Split(TEXT("::"), &projectId, &projectVersion);

			// If the game the user is in is the same as this, the user is playing the same game
			OutPresence = MakeShared< FOnlineUserPresence>();

			// If the product id is not empty, we assume that the user is playing a game
			OutPresence->bIsPlaying = presenceInfo->ProductId[0] != '\0';

			OutPresence->bIsPlayingThisGame = projectId.Equals(UTF8_TO_TCHAR(presenceInfo->ProductId), ESearchCase::IgnoreCase);

			// A general check if the user is online, more details in the Presence.State field
			OutPresence->bIsOnline = presenceInfo->Status > EOS_Presence_EStatus::EOS_PS_Offline;

			// Todo: For now this OSS doesn't support voice at all.
			OutPresence->bHasVoiceSupport = false;

			// Create the presence object
			OutPresence->Status = presenceStatus;

			int32 joinInfoLen = EOS_PRESENCEMODIFICATION_JOININFO_MAX_LENGTH;
			char* joinInfo = nullptr;
			EOS_Presence_GetJoinInfoOptions getJoinInfoOptions = {
				EOS_PRESENCE_GETJOININFO_API_LATEST,
				EpicLocalUserId.ToEpicAccountId(),
				TargetUserId.ToEpicAccountId()
			};
			EOS_EResult = EOS_Presence_GetJoinInfo(this->presenceHandle, &getJoinInfoOptions, joinInfo, &joinInfoLen);
			if (EOS_EResult == EOS_EResult::EOS_Success)
			{
				TSharedPtr<FUserOnlineAccount> userAcc = identityPtr->GetUserAccount(TargetUserId);

				// Get the last time the querying user was online.
#if ENGINE_MINOR_VERSION >= 25
				// Get the last time the querying user was online.
				FString lastOnlineString;
				userAcc->GetUserAttribute(USER_ATTR_LAST_LOGIN_TIME, lastOnlineString);
				OutPresence->LastOnline = FDateTime::FromUnixTimestamp(FCString::Atoi64(*lastOnlineString));
#endif

				IOnlineSessionPtr sessionPtr = this->subsystem->GetSessionInterface();

				// Get the session id
				TSharedPtr<FUniqueNetId const> sessionId = sessionPtr->CreateSessionIdFromString(UTF8_TO_TCHAR(joinInfo));
				OutPresence->SessionId = sessionId;

				// A session is joinable, when the player is in a presence session, they are is playing this game
				// and the game version is the the same as this game
				OutPresence->bIsJoinable = sessionPtr->HasPresenceSession()
					&& OutPresence->bIsPlayingThisGame
					&& projectVersion.Equals(UTF8_TO_TCHAR(presenceInfo->ProductVersion), ESearchCase::IgnoreCase);

				result = EOnlineCachedResult::Success;
			}
			else
			{
				error = TEXT("[EOS SDK] Couldn't get join info.");
				//this may occur if we are just querying using friends, still return success
				result = EOnlineCachedResult::Success;
			}
		}
		else
		{
			error = FString::Printf(TEXT("[EOS SDK] Error while retrieving cached presence information. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(EOS_EResult)));
		}

		EOS_Presence_Info_Release(presenceInfo);

		UE_CLOG_ONLINE_PRESENCE(result != EOnlineCachedResult::Success, Warning, TEXT("%s: Message: %s"), *FString(__FUNCTION__), *error);
	}
		
	return result;
}

EOnlineCachedResult::Type FOnlinePresenceEpic::GetCachedPresenceForApp(const FUniqueNetId& LocalUserId, const FUniqueNetId& User, const FString& AppId, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	UE_LOG_ONLINE_PRESENCE(Warning, TEXT("Getting presence for a user and app is not supported."));
	return EOnlineCachedResult::NotFound;
}


void FOnlinePresenceEpic::SetPresence(const FUniqueNetId& User, const FOnlineUserPresenceStatus& Status, const FOnPresenceTaskCompleteDelegate& Delegate)
{
	FString error;

	FUniqueNetIdEpic const epicNetId = static_cast<FUniqueNetIdEpic const>(User);
	if (epicNetId.IsEpicAccountIdValid())
	{
		EOS_HPresenceModification modHandle = nullptr;
		EOS_Presence_CreatePresenceModificationOptions createPresenceModOptions = {
			EOS_PRESENCE_CREATEPRESENCEMODIFICATION_API_LATEST,
			epicNetId.ToEpicAccountId()
		};

		EOS_EResult EOS_EResult = EOS_Presence_CreatePresenceModification(this->presenceHandle, &createPresenceModOptions, &modHandle);
		if (EOS_EResult == EOS_EResult::EOS_Success)
		{
			// Set the new status
			EOS_PresenceModification_SetStatusOptions setStatusOptions = {
				EOS_PRESENCE_SETSTATUS_API_LATEST,
				this->UEPresenceStateToEOSPresenceState(Status.State)
			};
			EOS_EResult = EOS_PresenceModification_SetStatus(modHandle, &setStatusOptions);
			if (EOS_EResult == EOS_EResult::EOS_Success)
			{
				// Set the raw status message
				EOS_PresenceModification_SetRawRichTextOptions setRawMessageOptions = {
					EOS_PRESENCE_SETRAWRICHTEXT_API_LATEST,
					TCHAR_TO_UTF8(*Status.StatusStr)
				};
				EOS_EResult = EOS_PresenceModification_SetRawRichText(modHandle, &setRawMessageOptions);
				if (EOS_EResult == EOS_EResult::EOS_Success)
				{
					// Set all additional presence properties.
					int32 recordCount = Status.Properties.Num();
					EOS_Presence_DataRecord* recordArr = new EOS_Presence_DataRecord[recordCount];

					// Create the records
					int32 currentIdx = 0;
					for (auto prop : Status.Properties)
					{
						EOS_Presence_DataRecord dataRecord = {
							EOS_PRESENCE_DATARECORD_API_LATEST,
							TCHAR_TO_UTF8(*prop.Key),
							TCHAR_TO_UTF8(*prop.Value.ToString())
						};
						recordArr[currentIdx] = dataRecord;
						currentIdx += 1;
					}

					EOS_PresenceModification_SetDataOptions setDataOpts = {
						EOS_PRESENCE_SETDATA_API_LATEST,
						recordCount,
						recordArr
					};
					EOS_EResult = EOS_PresenceModification_SetData(modHandle, &setDataOpts);
					if (EOS_EResult == EOS_EResult::EOS_Success)
					{
						// Finally update the presence itself.
						EOS_Presence_SetPresenceOptions setPresenceOptions = {
							EOS_PRESENCE_SETPRESENCE_API_LATEST,
							epicNetId.ToEpicAccountId(),
							modHandle
						};
						FPresenceAdditionalData* additionalData = new FPresenceAdditionalData{
							this,
							epicNetId,
							MakeShared<const IOnlinePresence::FOnPresenceTaskCompleteDelegate>(Delegate)
						};
						EOS_Presence_SetPresence(this->presenceHandle, &setPresenceOptions, additionalData, &FOnlinePresenceEpic::EOS_SetPresenceComplete);
					}
					else
					{
						error = TEXT("[EOS SDK] Couldn't update additional data");
					}
				}
				else
				{
					error = TEXT("[EOS SDK] Couldn't update presence text.");
				}
			}
			else
			{
				error = TEXT("[EOS SDK] Couldn't update presence status.");
			}
		}
		else
		{
			error = TEXT("[EOS SDK] Couldn't created presence modification handle");
		}
	}
	else
	{
		error = TEXT("User does not a valid EpicAccountId");
	}

	if (!error.IsEmpty())
	{
		UE_LOG_ONLINE_PRESENCE(Warning, TEXT("%s encounted an error. Message: %s"), *FString(__FUNCTION__), *error);
	}
}

void FOnlinePresenceEpic::RemoveAllPresenceQueries()
{
	for (auto& NotificationQuery : PresenceNotifications)
	{
		EOS_Presence_RemoveNotifyOnPresenceChanged(presenceHandle, NotificationQuery.Value);
	}

	PresenceNotifications.Empty();
}

void FOnlinePresenceEpic::RemovePresenceQuery(const FUniqueNetId& TargetUserId)
{
	if (PresenceNotifications.Contains(FUniqueNetIdEpic(TargetUserId).ToEpicAccountId())) {
		EOS_Presence_RemoveNotifyOnPresenceChanged(presenceHandle, PresenceNotifications[FUniqueNetIdEpic(TargetUserId).ToEpicAccountId()]);
		PresenceNotifications.Remove(FUniqueNetIdEpic(TargetUserId).ToEpicAccountId());
	}
}
