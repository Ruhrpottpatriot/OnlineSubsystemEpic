#include "OnlineSessionInterfaceEpic.h"
#include "Misc/ScopeLock.h"
#include "OnlineSubsystemEpicTypes.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "eos_sessions.h"
#include "Utilities.h"

FNamedOnlineSession* FOnlineSessionEpic::GetNamedSession(FName SessionName)
{
	FScopeLock ScopeLock(&SessionLock);
	for (int32 SearchIndex = 0; SearchIndex < Sessions.Num(); SearchIndex++)
	{
		if (Sessions[SearchIndex].SessionName == SessionName)
		{
			return &Sessions[SearchIndex];
		}
	}
	return nullptr;
}

void FOnlineSessionEpic::RemoveNamedSession(FName SessionName)
{
	FScopeLock ScopeLock(&SessionLock);
	for (int32 SearchIndex = 0; SearchIndex < Sessions.Num(); SearchIndex++)
	{
		if (Sessions[SearchIndex].SessionName == SessionName)
		{
			Sessions.RemoveAtSwap(SearchIndex);
			return;
		}
	}
}

EOnlineSessionState::Type FOnlineSessionEpic::GetSessionState(FName SessionName) const
{
	FScopeLock ScopeLock(&SessionLock);
	for (int32 SearchIndex = 0; SearchIndex < Sessions.Num(); SearchIndex++)
	{
		if (Sessions[SearchIndex].SessionName == SessionName)
		{
			return Sessions[SearchIndex].SessionState;
		}
	}

	return EOnlineSessionState::NoSession;
}

bool FOnlineSessionEpic::HasPresenceSession()
{
	FScopeLock ScopeLock(&SessionLock);
	for (int32 SearchIndex = 0; SearchIndex < Sessions.Num(); SearchIndex++)
	{
		if (Sessions[SearchIndex].SessionSettings.bUsesPresence)
		{
			return true;
		}
	}

	return false;
}

bool FOnlineSessionEpic::IsSessionJoinable(const FNamedOnlineSession& Session) const
{
	return false;
}

bool FOnlineSessionEpic::IsHost(const FNamedOnlineSession& Session) const
{
	return false;
}

FOnlineSessionEpic::FOnlineSessionEpic(FOnlineSubsystemEpic* InSubsystem)
	: Subsystem(InSubsystem)
{
	// Get the sessions handle
	EOS_HPlatform hPlatform = this->Subsystem->PlatformHandle;
	check(hPlatform);
	EOS_HSessions hSessions = EOS_Platform_GetSessionsInterface(hPlatform);
	check(hSessions);
	this->sessionsHandle = hSessions;
}

void FOnlineSessionEpic::Tick(float DeltaTime)
{
	// ToDo: Iterate through all session searches and cancel them if timeout has been reached
}

void FOnlineSessionEpic::CheckPendingSessionInvite()
{

}

void FOnlineSessionEpic::RegisterLocalPlayers(class FNamedOnlineSession* Session)
{
}

TSharedPtr<const FUniqueNetId> FOnlineSessionEpic::CreateSessionIdFromString(const FString& SessionIdStr)
{
	return MakeShared<FUniqueNetIdEpic>(SessionIdStr);
}

/**
 * Creates EOS Attribute data from UE4s variant data
 * @param attributeName - The name for the data.
 * @param variantData - The variable data itself
 * @outAttributeData - The EOS AttributeData populated with the variant data
 * @error - The error message if the creation was unsuccessful
 * @returns - True if the creation was successful, false otherwise
*/
bool CreateEOSAttributeData(FString const attributeName, FVariantData const variantData, EOS_Sessions_AttributeData outAttributeData, FString& error)
{
	outAttributeData.ApiVersion = EOS_SESSIONS_SESSIONATTRIBUTEDATA_API_LATEST;
	outAttributeData.Key = TCHAR_TO_UTF8(*attributeName);

	if (variantData.GetType() == EOnlineKeyValuePairDataType::Bool)
	{
		bool bData;
		variantData.GetValue(bData);
		outAttributeData.Value.AsBool = bData;
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_BOOLEAN;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::Double)
	{
		double dData;
		variantData.GetValue(dData);
		outAttributeData.Value.AsDouble = dData;
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_DOUBLE;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::Int64)
	{
		int64 iData;
		variantData.GetValue(iData);
		outAttributeData.Value.AsDouble = iData;
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_INT64;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::String)
	{
		FString sData;
		variantData.GetValue(sData);
		outAttributeData.Value.AsUtf8 = TCHAR_TO_UTF8(*sData);
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_STRING;
	}
	else
	{
		error = TEXT("Type mismatch for session setting attribute.");
		return false;
	}
	return true;
}

/**
 * Creates the AddAttributeOptions struct
 */
EOS_SessionModification_AddAttributeOptions CreateCustomAttrHandle(FString attributeName, FVariantData data, EOS_ESessionAttributeAdvertisementType advertisementType)
{
	EOS_Sessions_AttributeData attributeData = {};
	FString error;
	if (CreateEOSAttributeData(attributeName, data, attributeData, error))
	{
		EOS_SessionModification_AddAttributeOptions attrOpts = {
		EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
		&attributeData,
		advertisementType
		};
		return attrOpts;
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("%s"), *error);
		return EOS_SessionModification_AddAttributeOptions();
	}
}

void FOnlineSessionEpic::CreateSessionModificationHandle(FOnlineSessionSettings const& NewSessionSettings, EOS_HSessionModification& ModificationHandle, FString& Error)
{
	// Note on GoTo usage:
	// Goto was used here to remove duplicate calls to Printf
	// and make error handling easier in general.
	// Initializations need to be put in their own scope

	EOS_EResult eosResult;
	FString setting;

	// NumPublicConnections
	{
		setting = TEXT("NumPublicConnections");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.NumPublicConnections);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// NumPrivateConnections
	{
		setting = TEXT("NumPublicConnections");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.NumPrivateConnections);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// bUsesPresence
	{
		setting = TEXT("bUsesPresence");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.bShouldAdvertise);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// bAllowJoinInProgress
	{
		EOS_SessionModification_SetJoinInProgressAllowedOptions joinInProgresOpts = {
		EOS_SESSIONMODIFICATION_SETJOININPROGRESSALLOWED_API_LATEST,
		NewSessionSettings.bAllowJoinInProgress
		};
		eosResult = EOS_SessionModification_SetJoinInProgressAllowed(ModificationHandle, &joinInProgresOpts);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			setting = TEXT("JoinInProgress");
			goto handleError;
		}
	}

	// bIsLANMatch
	{
		setting = TEXT("bIsLANMatch");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.bIsLANMatch);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// bIsDedicated
	{
		setting = TEXT("bIsDedicated");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.bIsDedicated);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// bUsesStats
	{
		setting = TEXT("bUsesStats");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.bUsesStats);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// bAllowInvites
	{
		setting = TEXT("bAllowInvites");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.bAllowInvites);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// bShouldAdvertise || bAllowJoinViaPresence || bAllowJoinViaPresenceFriendsOnly
	// More restrictive from left to right. More restrictive takes precedence.
	{
		EOS_EOnlineSessionPermissionLevel permissionLevel = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_InviteOnly;
		if (NewSessionSettings.bShouldAdvertise)
		{
			if (NewSessionSettings.bAllowJoinViaPresence)
			{
				permissionLevel = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_PublicAdvertised;
			}
			if (NewSessionSettings.bAllowJoinViaPresenceFriendsOnly)
			{
				permissionLevel = permissionLevel = EOS_EOnlineSessionPermissionLevel::EOS_OSPF_JoinViaPresence;
			}
		}
		EOS_SessionModification_SetPermissionLevelOptions permissionOpts{
			EOS_SESSIONMODIFICATION_SETPERMISSIONLEVEL_API_LATEST,
			permissionLevel
		};
		eosResult = EOS_SessionModification_SetPermissionLevel(ModificationHandle, &permissionOpts);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			setting = TEXT("bShouldAdvertise || bAllowJoinViaPresence || bAllowJoinViaPresenceFriendsOnly");
			goto handleError;
		}
	}

	// bAntiCheatProtected
	{
		setting = TEXT("bAntiCheatProtected");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.bAntiCheatProtected);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// BuildUniqueId
	{
		setting = TEXT("BuildUniqueId");
		FVariantData data = FVariantData();
		data.SetValue(NewSessionSettings.BuildUniqueId);
		EOS_SessionModification_AddAttributeOptions attributeOptions =
			CreateCustomAttrHandle(
				setting
				, data
				, EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			);

		eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			goto handleError;
		}
	}

	// FSessionSettings[]
	{
		for (auto s : NewSessionSettings.Settings)
		{
			setting = s.Key.ToString();
			EOS_ESessionAttributeAdvertisementType advertisementType;
			if (s.Value.AdvertisementType == EOnlineDataAdvertisementType::DontAdvertise)
			{
				advertisementType = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_DontAdvertise;
			}
			else
			{
				advertisementType = EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise;
			}

			EOS_SessionModification_AddAttributeOptions attributeOptions =
				CreateCustomAttrHandle(
					setting
					, s.Value.Data
					, advertisementType
				);

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attributeOptions);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
	}

	// Set the total players, which are (public + private) connections
	{
		EOS_SessionModification_SetMaxPlayersOptions playerOpts = {};
		playerOpts.ApiVersion = EOS_SESSIONMODIFICATION_SETMAXPLAYERS_API_LATEST;
		playerOpts.MaxPlayers = NewSessionSettings.NumPrivateConnections + NewSessionSettings.NumPublicConnections;

		eosResult = EOS_SessionModification_SetMaxPlayers(ModificationHandle, &playerOpts);
		if (eosResult != EOS_EResult::EOS_Success)
		{
			setting = TEXT("MaxPlayers");
			goto handleError;
		}
	}
	return;

handleError:
	// If there's an error, assign it to Error and default the ModificationHandle
	Error = FString::Printf(TEXT("Cannot update setting: %s - Error Code: %s"), *setting, *UTF8_TO_TCHAR(EOS_EResult_ToString(eosResult)));
	ModificationHandle = EOS_HSessionModification();
}

bool FOnlineSessionEpic::CreateSession(int32 HostingPlayerNum, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	check(identityPtr);

	TSharedPtr<const FUniqueNetId> netId = identityPtr->GetUniquePlayerId(HostingPlayerNum);
	return this->CreateSession(*netId, SessionName, NewSessionSettings);
}

bool FOnlineSessionEpic::CreateSession(const FUniqueNetId& HostingPlayerId, FName SessionName, const FOnlineSessionSettings& NewSessionSettings)
{
	FString err;
	uint32 result = ONLINE_FAIL;
	if (!HostingPlayerId.IsValid())
	{
		err = TEXT("HostingPlayerId invalid!");
	}
	else
	{
		FNamedOnlineSession* session = this->GetNamedSession(SessionName);
		if (session)
		{
			err = FString::Printf(TEXT("Can't create session with name \"%s\". Session already exists"), *SessionName.ToString());
		}
		else
		{
			// Create and store session locally
			session = this->AddNamedSession(SessionName, NewSessionSettings);
			check(session);

			session->SessionState = EOnlineSessionState::Creating;
			session->NumOpenPrivateConnections = NewSessionSettings.NumPrivateConnections;
			session->NumOpenPublicConnections = NewSessionSettings.NumPublicConnections;

			session->HostingPlayerNum = INDEX_NONE; // HostingPlayernNum is going to be deprecated. Don't use it here
			session->LocalOwnerId = MakeShareable(&HostingPlayerId);

			IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
			if (identityPtr.IsValid())
			{
				session->OwningUserName = identityPtr->GetPlayerNickname(HostingPlayerId);
			}
			else
			{
				session->OwningUserName = FString(TEXT("EPIC User"));
			}

			session->SessionSettings.BuildUniqueId = GetBuildUniqueId();

			// Register the current player as local player in the session
			FOnRegisterLocalPlayerCompleteDelegate registerLocalPlayerCompleteDelegate = FOnRegisterLocalPlayerCompleteDelegate::CreateRaw(this, &FOnlineSessionEpic::OnRegisterLocalPlayerComplete);
			this->RegisterLocalPlayer(HostingPlayerId, SessionName, registerLocalPlayerCompleteDelegate);



			// Interface with EOS
			EOS_Sessions_CreateSessionModificationOptions opts = {};
			opts.ApiVersion = EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST;
			opts.SessionName = TCHAR_TO_UTF8(*SessionName.ToString());
			opts.bPresenceEnabled = NewSessionSettings.bUsesPresence;
			opts.BucketId = 0; // ToDo: Get bucket id from session settings
			opts.MaxPlayers = NewSessionSettings.NumPublicConnections;

			// Create a new - local - session handle
			EOS_HSessionModification sessionModificationHandle = {};
			EOS_EResult eosResult = EOS_Sessions_CreateSessionModification(this->sessionsHandle, &opts, &sessionModificationHandle);
			if (eosResult == EOS_EResult::EOS_Success)
			{
				this->CreateSessionModificationHandle(NewSessionSettings, sessionModificationHandle, err);

				// Modify the local session with the modified session options
				EOS_Sessions_UpdateSessionModificationOptions sessionModificationOptions =
				{
					EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_LATEST,
					TCHAR_TO_UTF8(*SessionName.ToString())
				};
				eosResult = EOS_Sessions_UpdateSessionModification(this->sessionsHandle, &sessionModificationOptions, &sessionModificationHandle);
				if (eosResult == EOS_EResult::EOS_Success)
				{
					// Update the remote session
					EOS_Sessions_UpdateSessionOptions updateSessionOptions = {};
					updateSessionOptions.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
					updateSessionOptions.SessionModificationHandle = sessionModificationHandle;

					EOS_Sessions_UpdateSession(this->sessionsHandle, &updateSessionOptions, this, &FOnlineSessionEpic::OnEOSCreateSessionComplete);
					result = ONLINE_IO_PENDING;
				}
				else
				{
					char const* resultStr = EOS_EResult_ToString(eosResult);
					err = FString::Printf(TEXT("[EOS SDK] Error modifying session options - Error Code: %s"), resultStr);
				}
			}
			else
			{
				char const* resultStr = EOS_EResult_ToString(eosResult);
				err = FString::Printf(TEXT("[EOS SDK] Error creating session - Error Code: %s"), resultStr);
			}

			// No matter the update result, release the memory for the SessionModification handle
			EOS_SessionModification_Release(sessionModificationHandle);
		}
	}

	if (result != ONLINE_IO_PENDING)
	{
		if (!err.IsEmpty())
		{
			UE_LOG_ONLINE_SESSION(Warning, TEXT("%s"), *err);
		}
		TriggerOnCreateSessionCompleteDelegates(SessionName, (result == ONLINE_SUCCESS) ? true : false);
	}

	return result == ONLINE_IO_PENDING || result == ONLINE_SUCCESS;
}
void FOnlineSessionEpic::OnEOSCreateSessionComplete(const EOS_Sessions_UpdateSessionCallbackInfo* Data)
{
	FName sessionName = FName(Data->SessionName);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult ResultCode = Data->ResultCode;
	/** Context that was passed into EOS_Sessions_UpdateSession */
	FOnlineSessionEpic* thisPtr = (FOnlineSessionEpic*)Data->ClientData;

	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Update Session failed. Error Code: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(ResultCode)));
		thisPtr->RemoveNamedSession(sessionName);
		thisPtr->TriggerOnCreateSessionCompleteDelegates(sessionName, false);
		return;
	}

	FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName);
	if (!session)
	{
		UE_LOG_ONLINE_SESSION(Fatal, TEXT("CreateSession complete callback called, but session \"%s\" not found."), *sessionName.ToString());
		return;
	}

	// Mark the session as created, but pending to start
	session->SessionState = EOnlineSessionState::Pending;
	UE_LOG_ONLINE_SESSION(Display, TEXT("Created session: %s"), *sessionName.ToString());
	thisPtr->TriggerOnCreateSessionCompleteDelegates(sessionName, true);
}

/**
 * This structure is needed, since the callback to start as session doesn't include the session's name,
 * which is needed to call the completion delegates */
typedef struct SessionStateChangeAdditionalInfo
{
	FOnlineSessionEpic* OnlineSessionPtr;
	FName SessionName;
} SessionStateChangeAdditionalInfo;
bool FOnlineSessionEpic::StartSession(FName SessionName)
{
	FString error;
	uint32 resultCode = ONLINE_FAIL;
	if (FNamedOnlineSession* session = this->GetNamedSession(SessionName))
	{
		EOnlineSessionState::Type sessionState = this->GetSessionState(SessionName);
		if (sessionState == EOnlineSessionState::Pending || sessionState == EOnlineSessionState::Ending)
		{
			sessionState = EOnlineSessionState::Starting;

			EOS_Sessions_StartSessionOptions startSessionOpts = {
				EOS_SESSIONS_STARTSESSION_API_LATEST,
				TCHAR_TO_UTF8(*SessionName.ToString())
			};

			// Allocate struct for additional information, 
			//as the callback doesn't expose the session that was started
			SessionStateChangeAdditionalInfo* additionalInfo = new SessionStateChangeAdditionalInfo();
			additionalInfo->OnlineSessionPtr = this;
			additionalInfo->SessionName = SessionName;

			EOS_Sessions_StartSession(this->sessionsHandle, &startSessionOpts, additionalInfo, &FOnlineSessionEpic::OnEOSStartSessionComplete);
			resultCode = ONLINE_IO_PENDING;
		}
		else
		{
			error = FString::Printf(TEXT("Cannot start session \"%s\". Session not Pending or Ending."), *SessionName.ToString());
		}
	}
	else
	{
		error = FString::Printf(TEXT("Cannot find session \"%s\""), *SessionName.ToString());
	}

	if (resultCode != ONLINE_IO_PENDING)
	{
		if (!error.IsEmpty())
		{
			UE_LOG_ONLINE_SESSION(Warning, TEXT("%s"), *error);
		}
		TriggerOnCreateSessionCompleteDelegates(SessionName, (resultCode == ONLINE_SUCCESS) ? true : false);
	}

	return resultCode == ONLINE_IO_PENDING || resultCode == ONLINE_SUCCESS;
}
void FOnlineSessionEpic::OnEOSStartSessionComplete(const EOS_Sessions_StartSessionCallbackInfo* Data)
{
	// Context that was passed into EOS_Sessions_UpdateSession
	// Copy the session ptr and session name, then free
	SessionStateChangeAdditionalInfo* context = (SessionStateChangeAdditionalInfo*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	FName sessionName = context->SessionName;
	delete(context);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult result = Data->ResultCode;
	if (result != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't start session. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
		thisPtr->TriggerOnStartSessionCompleteDelegates(sessionName, false);
		return;
	}

	if (FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName))
	{
		session->SessionState = EOnlineSessionState::InProgress;
		thisPtr->TriggerOnStartSessionCompleteDelegates(sessionName, true);
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Fatal, TEXT("Session \"%s\" changed on backend, but local session not found."), *sessionName.ToString());
	}
}

/** Needed, since we need a way to pass the old settings to the callback, in case the backend refuses the update */
typedef struct FUpdateSessionAdditionalInfo
{
	FOnlineSessionEpic* OnlineSessionPtr;
	FOnlineSessionSettings* OldSessionSettings;
} FUpdateSessionAdditionalInfo;
bool FOnlineSessionEpic::UpdateSession(FName SessionName, FOnlineSessionSettings& UpdatedSessionSettings, bool bShouldRefreshOnlineData)
{
	FString err;
	uint32 result = ONLINE_FAIL;

	if (FNamedOnlineSession* session = this->GetNamedSession(SessionName))
	{
		// Create the additional struct info here
		FOnlineSessionSettings oldSettings = FOnlineSessionSettings(session->SessionSettings);
		FUpdateSessionAdditionalInfo* additionalInfo = new FUpdateSessionAdditionalInfo();
		additionalInfo->OnlineSessionPtr = this;
		additionalInfo->OldSessionSettings = &oldSettings;

		session->SessionSettings = UpdatedSessionSettings;

		if (bShouldRefreshOnlineData)
		{
			EOS_HSessionModification sessionModificationHandle = {};
			this->CreateSessionModificationHandle(UpdatedSessionSettings, sessionModificationHandle, err);
			if (err.IsEmpty())
			{
				// Modify the local session with the modified session options
				EOS_Sessions_UpdateSessionModificationOptions sessionModificationOptions =
				{
					EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_LATEST,
					TCHAR_TO_UTF8(*SessionName.ToString())
				};
				EOS_EResult eosResult = EOS_Sessions_UpdateSessionModification(this->sessionsHandle, &sessionModificationOptions, &sessionModificationHandle);
				if (eosResult == EOS_EResult::EOS_Success)
				{
					// Update the remote session
					EOS_Sessions_UpdateSessionOptions updateSessionOptions = {};
					updateSessionOptions.ApiVersion = EOS_SESSIONS_UPDATESESSION_API_LATEST;
					updateSessionOptions.SessionModificationHandle = sessionModificationHandle;

					EOS_Sessions_UpdateSession(this->sessionsHandle, &updateSessionOptions, additionalInfo, &FOnlineSessionEpic::OnEOSCreateSessionComplete);
					result = ONLINE_IO_PENDING;

					EOS_SessionModification_Release(sessionModificationHandle);
				}
				else
				{
					char const* resultStr = EOS_EResult_ToString(eosResult);
					err = FString::Printf(TEXT("[EOS SDK] Error modifying session options - Error Code: %s"), resultStr);
				}
			}
			else
			{
				err = FString::Printf(TEXT("[EOS SDK] Error creating session modification - Error Code: %s"), *err);
			}
		}
		else
		{
			result = ONLINE_SUCCESS;
		}
	}
	else
	{
		err = FString::Printf(TEXT("Couldn't find sessiion \"%s\" to update."), *SessionName.ToString());
	}

	if (result != ONLINE_IO_PENDING)
	{
		if (!err.IsEmpty())
		{
			if (!err.IsEmpty())
			{
				UE_LOG_ONLINE_SESSION(Warning, TEXT("%s"), *err);
			}
		}
		TriggerOnCreateSessionCompleteDelegates(SessionName, (result == ONLINE_SUCCESS) ? true : false);
	}
	return result == ONLINE_IO_PENDING || result == ONLINE_SUCCESS;
}
void FOnlineSessionEpic::OnEOSUpdateSessionComplete(const EOS_Sessions_UpdateSessionCallbackInfo* Data)
{
	FName sessionName = FName(Data->SessionName);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult ResultCode = Data->ResultCode;
	/** Context that was passed into EOS_Sessions_UpdateSession */
	FUpdateSessionAdditionalInfo* context = (FUpdateSessionAdditionalInfo*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	FOnlineSessionSettings* oldSettings = context->OldSessionSettings;

	if (ResultCode != EOS_EResult::EOS_Success)
	{
		FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName);
		checkf(session, TEXT("Failed updating an online session \"%s\". Session not present anymore"), *sessionName.ToString());

		session->SessionSettings = *oldSettings;

		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Failed to update session - Error Code: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(ResultCode)));
		thisPtr->TriggerOnCreateSessionCompleteDelegates(sessionName, false);
		return;
	}

	UE_LOG_ONLINE_SESSION(Display, TEXT("Updated session: %s"), *sessionName.ToString());

	// Cleanup the additional resources.
	delete(context);
	delete(oldSettings);
}

bool FOnlineSessionEpic::EndSession(FName SessionName)
{
	FString error;
	uint32 resultCode = ONLINE_FAIL;

	if (FNamedOnlineSession* session = this->GetNamedSession(SessionName))
	{
		EOnlineSessionState::Type sessionState = this->GetSessionState(SessionName);
		if (sessionState == EOnlineSessionState::InProgress)
		{
			session->SessionState = EOnlineSessionState::Ending;

			EOS_Sessions_EndSessionOptions endSessionOptions = {
				EOS_SESSIONS_ENDSESSION_API_LATEST,
				TCHAR_TO_UTF8(*SessionName.ToString())
			};

			SessionStateChangeAdditionalInfo* additionalInfo = new SessionStateChangeAdditionalInfo();
			additionalInfo->OnlineSessionPtr = this;
			additionalInfo->SessionName = SessionName;

			EOS_Sessions_EndSession(this->sessionsHandle, &endSessionOptions, additionalInfo, &FOnlineSessionEpic::OnEOSEndSessionComplete);
			resultCode = ONLINE_IO_PENDING;
		}
		else
		{
			error = FString::Printf(TEXT("Cannot end session \"%s\". Session not running."), *SessionName.ToString());
		}
	}
	else
	{
		error = FString::Printf(TEXT("Cannot find session \"%s\""), *SessionName.ToString());
	}

	if (resultCode != ONLINE_IO_PENDING)
	{
		if (!error.IsEmpty())
		{
			if (!error.IsEmpty())
			{
				UE_LOG_ONLINE_SESSION(Warning, TEXT("%s"), *error);
			}
		}
		TriggerOnCreateSessionCompleteDelegates(SessionName, (resultCode == ONLINE_SUCCESS) ? true : false);
	}
	return resultCode == ONLINE_IO_PENDING || resultCode == ONLINE_SUCCESS;
}
void FOnlineSessionEpic::OnEOSEndSessionComplete(const EOS_Sessions_EndSessionCallbackInfo* Data)
{
	// Context that was passed into EOS_Sessions_UpdateSession
	// Copy the session ptr and session name, then free
	SessionStateChangeAdditionalInfo* context = (SessionStateChangeAdditionalInfo*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	FName sessionName = context->SessionName;
	delete(context);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult result = Data->ResultCode;
	if (result != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't end session. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
		thisPtr->TriggerOnEndSessionCompleteDelegates(sessionName, false);
		return;
	}

	if (FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName))
	{
		session->SessionState = EOnlineSessionState::Ended;
		thisPtr->TriggerOnEndSessionCompleteDelegates(sessionName, true);
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Fatal, TEXT("Session \"%s\" changed on backend, but local session not found."), *sessionName.ToString());
	}
}

bool FOnlineSessionEpic::DestroySession(FName SessionName, const FOnDestroySessionCompleteDelegate& CompletionDelegate)
{
	FString error;
	uint32 resultCode = ONLINE_FAIL;

	if (FNamedOnlineSession* session = this->GetNamedSession(SessionName))
	{
		EOnlineSessionState::Type sessionState = this->GetSessionState(SessionName);
		if (sessionState == EOnlineSessionState::Destroying)
		{
			session->SessionState = EOnlineSessionState::Destroying;

			SessionStateChangeAdditionalInfo* additionalInfo = new SessionStateChangeAdditionalInfo();
			additionalInfo->OnlineSessionPtr = this;
			additionalInfo->SessionName = SessionName;

			EOS_Sessions_DestroySessionOptions destroySessionOpts = {
				EOS_SESSIONS_DESTROYSESSION_API_LATEST,
				TCHAR_TO_UTF8(*SessionName.ToString())
			};

			EOS_Sessions_DestroySession(this->sessionsHandle, &destroySessionOpts, additionalInfo, &FOnlineSessionEpic::OnEOSDestroySessionComplete);
			resultCode = ONLINE_IO_PENDING;
		}
		else
		{
			error = FString::Printf(TEXT("Cannot destroy session \"%s\". Session is already being destroyed."), *SessionName.ToString());
		}
	}
	else
	{
		error = FString::Printf(TEXT("Cannot find session \"%s\""), *SessionName.ToString());
	}

	if (resultCode != ONLINE_IO_PENDING)
	{
		if (!error.IsEmpty())
		{
			if (!error.IsEmpty())
			{
				UE_LOG_ONLINE_SESSION(Warning, TEXT("%s"), *error);
			}
		}
		TriggerOnCreateSessionCompleteDelegates(SessionName, (resultCode == ONLINE_SUCCESS) ? true : false);
	}
	return resultCode == ONLINE_IO_PENDING || resultCode == ONLINE_SUCCESS;
}
void FOnlineSessionEpic::OnEOSDestroySessionComplete(const EOS_Sessions_DestroySessionCallbackInfo* Data)
{
	// Context that was passed into EOS_Sessions_UpdateSession
	// Copy the session ptr and session name, then free
	SessionStateChangeAdditionalInfo* context = (SessionStateChangeAdditionalInfo*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	FName sessionName = context->SessionName;
	delete(context);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult result = Data->ResultCode;
	if (result != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't destroy session. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
		thisPtr->TriggerOnDestroySessionCompleteDelegates(sessionName, false);
		return;
	}

	if (FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName))
	{
		thisPtr->RemoveNamedSession(sessionName);
		thisPtr->TriggerOnDestroySessionCompleteDelegates(sessionName, true);
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Fatal, TEXT("Session \"%s\" changed on backend, but local session not found."), *sessionName.ToString());
	}
}

bool FOnlineSessionEpic::IsPlayerInSession(FName SessionName, const FUniqueNetId& UniqueId)
{
	if (FNamedOnlineSession* session = this->GetNamedSession(SessionName))
	{
		return session->RegisteredPlayers.ContainsByPredicate([&UniqueId](TSharedRef<const FUniqueNetId> id) {
			return *id == UniqueId;
			});
	}

	UE_LOG_ONLINE_SESSION(Warning, TEXT("No session with name \"%s\" found"), *SessionName.ToString());
	return false;
}

bool FOnlineSessionEpic::StartMatchmaking(const TArray< TSharedRef<const FUniqueNetId> >& LocalPlayers, FName SessionName, const FOnlineSessionSettings& NewSessionSettings, TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("StartMatchmaking not supported by EOS."));
	return false;
}

bool FOnlineSessionEpic::CancelMatchmaking(int32 SearchingPlayerNum, FName SessionName)
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("CancelMatchmaking not supported by EOS."));
	return false;
}

bool FOnlineSessionEpic::CancelMatchmaking(const FUniqueNetId& SearchingPlayerId, FName SessionName)
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("CancelMatchmaking not supported by EOS."));
	return false;
}

bool FOnlineSessionEpic::FindSessions(int32 SearchingPlayerNum, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	check(identityPtr);

	TSharedPtr<const FUniqueNetId> netId = identityPtr->GetUniquePlayerId(SearchingPlayerNum);
	return this->FindSessions(*netId, SearchSettings);
}

typedef struct FFindSessionsAdditionalInfo {
	FOnlineSessionEpic* OnlineSessionPtr;
	double SearchStartTime;
} FFindSessionsAdditionalInfo;
/** Takes the session search handle and populates it the session query settings */
void UpdateSessionSearchParameters(TSharedRef<FOnlineSessionSearch> const& sessionSearchPtr, EOS_HSessionSearch const eosSessionSearch, FString& error)
{
	FOnlineSearchSettings SearchSettings = sessionSearchPtr->QuerySettings;
	for (auto param : SearchSettings.SearchParams)
	{
		EOS_EOnlineComparisonOp compOp;
		switch (param.Value.ComparisonOp)
		{
		case EOnlineComparisonOp::Equals:
			compOp = EOS_EOnlineComparisonOp::EOS_CO_EQUAL;
			break;
		case EOnlineComparisonOp::NotEquals:
			compOp = EOS_EOnlineComparisonOp::EOS_CO_NOTEQUAL;
			break;
		case EOnlineComparisonOp::GreaterThan:
		{
			if (!param.Value.Data.IsNumeric())
			{
				error = FString::Printf(TEXT("%s is not a valid comparison op for non numeric types."), *EOnlineComparisonOp::ToString(EOnlineComparisonOp::GreaterThan));
			}
			else
			{
				compOp = EOS_EOnlineComparisonOp::EOS_CO_GREATERTHAN;
			}
			break;
		}
		case EOnlineComparisonOp::GreaterThanEquals:
		{
			if (!param.Value.Data.IsNumeric())
			{
				error = FString::Printf(TEXT("%s is not a valid comparison op for non numeric types."), *EOnlineComparisonOp::ToString(EOnlineComparisonOp::GreaterThanEquals));
			}
			else
			{
				compOp = EOS_EOnlineComparisonOp::EOS_CO_GREATERTHANOREQUAL;
			}
			break;
		}
		case EOnlineComparisonOp::LessThan:
		{
			if (!param.Value.Data.IsNumeric())
			{
				error = FString::Printf(TEXT("%s is not a valid comparison op for non numeric types."), *EOnlineComparisonOp::ToString(EOnlineComparisonOp::LessThan));
			}
			else
			{
				compOp = EOS_EOnlineComparisonOp::EOS_CO_LESSTHAN;
			}
			break;
		}
		case EOnlineComparisonOp::LessThanEquals:
		{
			if (!param.Value.Data.IsNumeric())
			{
				error = FString::Printf(TEXT("%s is not a valid comparison op for non numeric types."), *EOnlineComparisonOp::ToString(EOnlineComparisonOp::LessThanEquals));
			}
			else
			{
				compOp = EOS_EOnlineComparisonOp::EOS_CO_LESSTHANOREQUAL;
			}
			break;
		}
		case EOnlineComparisonOp::Near:
		{
			if (!param.Value.Data.IsNumeric())
			{
				error = FString::Printf(TEXT("%s is not a valid comparison op for non numeric types."), *EOnlineComparisonOp::ToString(EOnlineComparisonOp::Near));
			}
			else
			{
				compOp = EOS_EOnlineComparisonOp::EOS_CO_DISTANCE;
			}
			break;
		}
		case EOnlineComparisonOp::In:
		{
			if (param.Value.Data.GetType() != EOnlineKeyValuePairDataType::String)
			{
				error = FString::Printf(TEXT("%s is not a valid comparison op for non string types."), *EOnlineComparisonOp::ToString(EOnlineComparisonOp::In));
			}
			else
			{
				compOp = EOS_EOnlineComparisonOp::EOS_CO_ANYOF;
			}
			break;
		}
		case EOnlineComparisonOp::NotIn:
		{
			if (param.Value.Data.GetType() != EOnlineKeyValuePairDataType::String)
			{
				error = FString::Printf(TEXT("%s is not a valid comparison op for non string types."), *EOnlineComparisonOp::ToString(EOnlineComparisonOp::NotIn));
			}
			else
			{
				compOp = EOS_EOnlineComparisonOp::EOS_CO_NOTANYOF;
			}
			break;
		}
		default:
			checkNoEntry();
			break;
		}

		if (!error.IsEmpty())
		{
			// ToDo: Maybe return nullptr here?
			return;
		}

		// Create the attribute data struct
		EOS_Sessions_AttributeData attributeData = {};
		if (CreateEOSAttributeData(param.Key.ToString(), param.Value.Data, attributeData, error))
		{
			EOS_SessionSearch_SetParameterOptions eosParam;
			eosParam.ApiVersion = EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST;
			eosParam.ComparisonOp = compOp;
			eosParam.Parameter = &attributeData;

			// Pass the parameter to the session search
			EOS_SessionSearch_SetParameter(eosSessionSearch, &eosParam);
		}
		else
		{
			// ToDo: Maybe return nullptr here?
			return;
		}
	}
}
bool FOnlineSessionEpic::FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	FString error;
	uint32 result = ONLINE_FAIL;
	SearchSettings->SearchState = EOnlineAsyncTaskState::NotStarted;

	if (SearchingPlayerId.IsValid())
	{
		if (SearchSettings->bIsLanQuery)
		{
			error = TEXT("LAN matches are not supported.");
		}
		else
		{
			EOS_Sessions_CreateSessionSearchOptions sessionSearchOpts = {
				EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST,
				SearchSettings->MaxSearchResults
			};

			// Handle where the session search is stored
			EOS_HSessionSearch hSessionSearch = {};
			EOS_EResult eosResult = EOS_Sessions_CreateSessionSearch(this->sessionsHandle, &sessionSearchOpts, &hSessionSearch);
			if (eosResult == EOS_EResult::EOS_Success)
			{
				UpdateSessionSearchParameters(SearchSettings, hSessionSearch, error);
				if (error.IsEmpty()) // Only proceeed if there was no error
				{
					EOS_SessionSearch_FindOptions findOpts = {};
					findOpts.ApiVersion = EOS_SESSIONSEARCH_FIND_API_LATEST;
					findOpts.LocalUserId = FIdentityUtilities::ProductUserIDFromString(SearchingPlayerId.ToString());

					// Mark the search as in progress
					SearchSettings->SearchState = EOnlineAsyncTaskState::InProgress;

					// Locally store the session search so it can be accessed later
					double utcNow = FDateTime::UtcNow().ToUnixTimestamp();
					this->CurrentSessionSearches.Add(utcNow, SearchSettings);

					FFindSessionsAdditionalInfo* additionalInfo = new FFindSessionsAdditionalInfo();
					additionalInfo->OnlineSessionPtr = this;
					additionalInfo->SearchStartTime = utcNow;

					EOS_SessionSearch_Find(hSessionSearch, &findOpts, additionalInfo, &FOnlineSessionEpic::OnEOSFindSessionComplete);
					result = ONLINE_IO_PENDING;
				}
			}
			else
			{
				error = FString::Printf(TEXT("[EOS SDK] Couldn't create sessionsearch. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(eosResult)));
			}
		}
	}
	else
	{
		error = TEXT("Invalid SearchingPlayerId");
	}

	if (result != ONLINE_IO_PENDING)
	{
		UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("%s"), *error);
		SearchSettings->SearchState = EOnlineAsyncTaskState::Failed;
		TriggerOnFindSessionsCompleteDelegates(false);
	}

	return result == ONLINE_IO_PENDING || result == ONLINE_SUCCESS;
}
void FOnlineSessionEpic::OnEOSFindSessionComplete(const EOS_SessionSearch_FindCallbackInfo* Data)
{
	// Context that was passed into EOS_Sessions_UpdateSession
	// Copy the session ptr and start time, then free
	FFindSessionsAdditionalInfo* context = (FFindSessionsAdditionalInfo*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	double searchStartTime = context->SearchStartTime;
	delete(context);

	TSharedRef<FOnlineSessionSearch>* searchRefPtr = thisPtr->CurrentSessionSearches.Find(searchStartTime);
	if (!searchRefPtr)
	{
		UE_LOG_ONLINE_SESSION(Fatal, TEXT("Session search completed, but session not in session search list!"));
		return;
	}

	TSharedRef<FOnlineSessionSearch> searchRef = *searchRefPtr;
	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		searchRef->SearchState = EOnlineAsyncTaskState::Failed;
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't find session. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		thisPtr->TriggerOnFindSessionsCompleteDelegates(false);
		return;
	}

	searchRef->SearchState = EOnlineAsyncTaskState::Done;
	UE_LOG_ONLINE_SESSION(Display, TEXT("Finished session search with start time %f"), searchStartTime);
	thisPtr->TriggerOnFindSessionsCompleteDelegates(true);
}

bool FOnlineSessionEpic::FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegate)
{
	return false;
}

bool FOnlineSessionEpic::CancelFindSessions()
{
	return false;

}

bool FOnlineSessionEpic::PingSearchResults(const FOnlineSessionSearchResult& SearchResult)
{
	return false;

}

bool FOnlineSessionEpic::JoinSession(int32 PlayerNum, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	return false;

}

bool FOnlineSessionEpic::JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	return false;

}

bool FOnlineSessionEpic::FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend)
{
	return false;

}

bool FOnlineSessionEpic::FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend)
{
	return false;

}

bool FOnlineSessionEpic::FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& FriendList)
{
	return false;

}

bool FOnlineSessionEpic::SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend)
{
	return false;

}

bool FOnlineSessionEpic::SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend)
{
	return false;

}

bool FOnlineSessionEpic::SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends)
{
	return false;

}

bool FOnlineSessionEpic::SendSessionInviteToFriends(const FUniqueNetId& LocalUserId, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends)
{
	return false;

}

bool FOnlineSessionEpic::GetResolvedConnectString(FName SessionName, FString& ConnectInfo, FName PortType)
{
	return false;
}

bool FOnlineSessionEpic::GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo)
{
	return false;
}

FOnlineSessionSettings* FOnlineSessionEpic::GetSessionSettings(FName SessionName)
{
	if (FNamedOnlineSession* session = this->GetNamedSession(SessionName))
	{
		return &session->SessionSettings;
	}

	return nullptr;
}

bool FOnlineSessionEpic::RegisterPlayer(FName SessionName, const FUniqueNetId& PlayerId, bool bWasInvited)
{
	TArray<TSharedRef<const FUniqueNetId>> players;
	players.Add(MakeShared<FUniqueNetIdEpic>(PlayerId));
	return RegisterPlayers(SessionName, players);
}

bool FOnlineSessionEpic::RegisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players, bool bWasInvited /*= false*/)
{
	return false;
	//bool bSuccess = false;
	//FNamedOnlineSession* Session = GetNamedSession(SessionName);
	//if (!Session)
	//{
	//	UE_LOG_ONLINE_SESSION(Warning, TEXT("No game present to join for session (%s)"), *SessionName.ToString());
	//	TriggerOnRegisterPlayersCompleteDelegates(SessionName, Players, bSuccess);
	//	return false;
	//}

	//
	//
	//if (Session)
	//{
	//	bSuccess = true;

	//	for (int32 PlayerIdx = 0; PlayerIdx < Players.Num(); PlayerIdx++)
	//	{
	//		const TSharedRef<const FUniqueNetId>& PlayerId = Players[PlayerIdx];

	//		FUniqueNetIdMatcher PlayerMatch(*PlayerId);
	//		if (Session->RegisteredPlayers.IndexOfByPredicate(PlayerMatch) == INDEX_NONE)
	//		{
	//			Session->RegisteredPlayers.Add(PlayerId);
	//			RegisterVoice(*PlayerId);

	//			// update number of open connections
	//			if (Session->NumOpenPublicConnections > 0)
	//			{
	//				Session->NumOpenPublicConnections--;
	//			}
	//			else if (Session->NumOpenPrivateConnections > 0)
	//			{
	//				Session->NumOpenPrivateConnections--;
	//			}
	//		}
	//		else
	//		{
	//			RegisterVoice(*PlayerId);
	//			UE_LOG_ONLINE_SESSION(Log, TEXT("Player %s already registered in session %s"), *PlayerId->ToDebugString(), *SessionName.ToString());
	//		}
	//	}
	//}
	//else
	//{
	//}

	//TriggerOnRegisterPlayersCompleteDelegates(SessionName, Players, true);
	//return true;
}

bool FOnlineSessionEpic::UnregisterPlayer(FName SessionName, const FUniqueNetId& PlayerId)
{
	TArray<TSharedRef<const FUniqueNetId>> players;
	players.Add(MakeShared<FUniqueNetIdEpic>(PlayerId));
	return UnregisterPlayers(SessionName, players);
}

bool FOnlineSessionEpic::UnregisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players)
{
	return false;

	//FNamedOnlineSession* Session = GetNamedSession(SessionName);
	//if (!Session)
	//{
	//	UE_LOG_ONLINE_SESSION(Warning, TEXT("No game present to leave for session: %s"), *SessionName.ToString());
	//	TriggerOnUnregisterPlayersCompleteDelegates(SessionName, Players, false);
	//	return false;
	//}

	//for (auto p : Players)
	//{
	//	FUniqueNetIdMatcher PlayerMatch(*p);
	//	int32 RegistrantIndex = Session->RegisteredPlayers.IndexOfByPredicate(PlayerMatch);
	//	if (RegistrantIndex == INDEX_NONE)
	//	{
	//		UE_LOG_ONLINE_SESSION(Warning, TEXT("Player %s is not part of session (%s)"), *PlayerId->ToDebugString(), *SessionName.ToString());
	//		continue;
	//	}

	//	Session->RegisteredPlayers.RemoveAtSwap(RegistrantIndex);

	//	// update number of open connections
	//	if (Session->NumOpenPublicConnections < Session->SessionSettings.NumPublicConnections)
	//	{
	//		Session->NumOpenPublicConnections += 1;
	//	}
	//	else if (Session->NumOpenPrivateConnections < Session->SessionSettings.NumPrivateConnections)
	//	{
	//		Session->NumOpenPrivateConnections += 1;
	//	}
	//}

	//TriggerOnUnregisterPlayersCompleteDelegates(SessionName, Players, true);
}

void FOnlineSessionEpic::RegisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnRegisterLocalPlayerCompleteDelegate& Delegate)
{
	FNamedOnlineSession* session = this->GetNamedSession(SessionName);
	if (!session)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Tried registering local player in session, but session doesn't exist."));
		Delegate.ExecuteIfBound(PlayerId, EOnJoinSessionCompleteResult::SessionDoesNotExist);
		return;
	}

	session->RegisteredPlayers.Add(MakeShareable(&PlayerId));

	// update number of open connections
	if (session->NumOpenPublicConnections > 0)
	{
		session->NumOpenPublicConnections -= 1;
	}
	else if (session->NumOpenPrivateConnections > 0)
	{
		session->NumOpenPrivateConnections -= 1;
	}
	Delegate.ExecuteIfBound(PlayerId, EOnJoinSessionCompleteResult::Success);
}

void FOnlineSessionEpic::OnRegisterLocalPlayerComplete(const FUniqueNetId& PlayerId, EOnJoinSessionCompleteResult::Type Result)
{
}

void FOnlineSessionEpic::UnregisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnUnregisterLocalPlayerCompleteDelegate& Delegate)
{
	FNamedOnlineSession* session = this->GetNamedSession(SessionName);
	if (!session)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Tried registering local player in session, but session doesn't exist."));
		Delegate.ExecuteIfBound(PlayerId, false);
		return;
	}

	session->RegisteredPlayers.RemoveSingle(MakeShareable(&PlayerId));

	// update number of open connections
	if (session->NumOpenPublicConnections > 0)
	{
		session->NumOpenPublicConnections += 1;
	}
	else if (session->NumOpenPrivateConnections > 0)
	{
		session->NumOpenPrivateConnections += 1;
	}
	Delegate.ExecuteIfBound(PlayerId, true);
}

int32 FOnlineSessionEpic::GetNumSessions()
{
	FScopeLock ScopeLock(&SessionLock);
	return this->Sessions.Num();
}

void FOnlineSessionEpic::DumpSessionState()
{
	unimplemented();
}

