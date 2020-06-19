#include "OnlinePrensenceEpic.h"
#include "eos_presence.h"
#include "OnlineSubsystemEpicTypes.h"


// ---------------------------------------------
// Implementation file only structs
// These structs carry additional informations to the callbacks
// ---------------------------------------------
typedef struct FSetPresenceAdditionalData
{
	FOnlinePresenceEpic const* This;
	FUniqueNetIdEpic const& EpicNetId;
	FOnlinePresenceEpic::FOnPresenceTaskCompleteDelegate const& Delegate;
} FSetPresenceAdditionalData;


void FOnlinePresenceEpic::EOS_OnPresenceChanged(EOS_Presence_PresenceChangedCallbackInfo const* data)
{
	unimplemented();
	// this->TriggerOnPresenceReceivedDelegates
}

// -----------------------------
// EOS Callbacks
// -----------------------------
void FOnlinePresenceEpic::EOS_SetPresenceComplete(EOS_Presence_SetPresenceCallbackInfo const* data)
{
	FSetPresenceAdditionalData* additionalData = static_cast<FSetPresenceAdditionalData*>(data->ClientData);

	if (data->ResultCode == EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_PRESENCE(Display, TEXT("[EOS SDK] Sucessfully updated presence for user \"%s\""), *FUniqueNetIdEpic::EpicAccountIdToString(data->LocalUserId));
		additionalData->Delegate.ExecuteIfBound(additionalData->EpicNetId, true);
	}
	else
	{
		UE_LOG_ONLINE_PRESENCE(Warning, TEXT("[EOS SDK] Couldn't update presence information. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(data->ResultCode)));
		additionalData->Delegate.ExecuteIfBound(additionalData->EpicNetId, false);
	}

	// Release the additional data memory
	delete(additionalData);
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

	EOS_Presence_AddNotifyOnPresenceChangedOptions onPresenceChangedOptions = {
		EOS_PRESENCE_ADDNOTIFYONPRESENCECHANGED_API_LATEST
	};
	this->OnPresenceChangedHandle = EOS_Presence_AddNotifyOnPresenceChanged(this->presenceHandle, &onPresenceChangedOptions, this, &FOnlinePresenceEpic::EOS_OnPresenceChanged);
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

		EOS_EResult eosResult = EOS_Presence_CreatePresenceModification(this->presenceHandle, &createPresenceModOptions, &modHandle);
		if (eosResult == EOS_EResult::EOS_Success)
		{
			// Set the new status
			EOS_PresenceModification_SetStatusOptions setStatusOptions = {
				EOS_PRESENCE_SETSTATUS_API_LATEST,
				this->UEPresenceStateToEOSPresenceState(Status.State)
			};
			eosResult = EOS_PresenceModification_SetStatus(modHandle, &setStatusOptions);
			if (eosResult == EOS_EResult::EOS_Success)
			{
				// Set the raw status message
				EOS_PresenceModification_SetRawRichTextOptions setRawMessageOptions = {
					EOS_PRESENCE_SETRAWRICHTEXT_API_LATEST,
					TCHAR_TO_UTF8(*Status.StatusStr)
				};
				eosResult = EOS_PresenceModification_SetRawRichText(modHandle, &setRawMessageOptions);
				if (eosResult == EOS_EResult::EOS_Success)
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
					eosResult = EOS_PresenceModification_SetData(modHandle, &setDataOpts);
					if (eosResult == EOS_EResult::EOS_Success)
					{
						// Finally update the presence itself.
						EOS_Presence_SetPresenceOptions setPresenceOptions = {
							EOS_PRESENCE_SETPRESENCE_API_LATEST,
							epicNetId.ToEpicAccountId(),
							modHandle
						};
						FSetPresenceAdditionalData* additionalData = new FSetPresenceAdditionalData {
							this,
							epicNetId,
							Delegate
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

void FOnlinePresenceEpic::QueryPresence(const FUniqueNetId& User, const FOnPresenceTaskCompleteDelegate& Delegate)
{
	unimplemented();
}

EOnlineCachedResult::Type FOnlinePresenceEpic::GetCachedPresence(const FUniqueNetId& User, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	unimplemented();
	return EOnlineCachedResult::NotFound;
}

EOnlineCachedResult::Type FOnlinePresenceEpic::GetCachedPresenceForApp(const FUniqueNetId& LocalUserId, const FUniqueNetId& User, const FString& AppId, TSharedPtr<FOnlineUserPresence>& OutPresence)
{
	unimplemented();
	return EOnlineCachedResult::NotFound;
}
