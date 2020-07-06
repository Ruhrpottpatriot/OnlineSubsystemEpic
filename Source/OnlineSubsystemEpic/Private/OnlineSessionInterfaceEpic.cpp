#include "OnlineSessionInterfaceEpic.h"
#include "Misc/ScopeLock.h"
#include "OnlineSubsystemEpicTypes.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "eos_sessions.h"
#include "SocketSubsystem.h"
#include "Utilities.h"
#include "eos_auth.h"

// ---------------------------------------------
// FOnlineSessionInfoEpic definitions
//
// These defintions are from OnlineSubsystemEpicTypes.h
// and are located here to keep the types header only
// ---------------------------------------------

FOnlineSessionInfoEpic::FOnlineSessionInfoEpic()
	: HostAddr(nullptr)
	, SessionId(new FUniqueNetIdEpic("INVALID"))
{
}


// ---------------------------------------------
// Implementation file only structs
// These structs carry additional informations to the callbacks
// ---------------------------------------------

/**
 * This structure is needed, since the callback to start as session doesn't include the session's name,
 * which is needed to call the completion delegates
 */
typedef struct SessionStateChangeAdditionalData
{
	FOnlineSessionEpic* OnlineSessionPtr;
	FName SessionName;
} SessionStateChangeAdditionalData;

/**
 * Needed, since we need a way to pass the old settings to the callback,
 * in case the backend refuses the update
 */
typedef struct FUpdateSessionAdditionalData
{
	FOnlineSessionEpic* OnlineSessionPtr;
	FOnlineSessionSettings OldSessionSettings;
} FUpdateSessionAdditionalData;

/**
 * To find the search handle in the callback,
 * the key needs to be passed along.
 */
typedef struct FFindSessionsAdditionalData {
	FOnlineSessionEpic* OnlineSessionPtr;
	double SearchStartTime;
} FFindSessionsAdditionalData;

typedef struct FJoinSessionAdditionalData
{
	FOnlineSessionEpic* OnlineSessionPtr;
	FName SessionName;
} FJoinSessionAdditionalData;

typedef struct FFindFriendSessionAdditionalData
{
	FOnlineSessionEpic* OnlineSessionPtr;
	double SearchCreationTime;
	FUniqueNetId const& SearchingUserId;
} FFindFriendSessionAdditionalData;

typedef struct FRegisterPlayersAdditionalData
{
	FOnlineSessionEpic* OnlineSessionPtr;
	FName SessionName;
	TArray<TSharedRef<const FUniqueNetId>> RegisteredPlayers;
} FRegisterPlayersAdditionalData;


// ---------------------------------------------
// Free functions/Utility functions.
// 
// Most free functions should be moved to be private class functions
// ---------------------------------------------

/**
 * Creates EOS Attribute data from UE4s variant data
 * @param attributeName - The name for the data.
 * @param variantData - The variable data itself
 * @outAttributeData - The EOS AttributeData populated with the variant data
 * @error - The error message if the creation was unsuccessful
 * @returns - True if the creation was successful, false otherwise
*/
EOS_Sessions_AttributeData FOnlineSessionEpic::CreateEOSAttributeData(FString const attributeName, FVariantData const variantData, FString& error)
{
	EOS_Sessions_AttributeData outAttributeData;
	outAttributeData.ApiVersion = EOS_SESSIONS_SESSIONATTRIBUTEDATA_API_LATEST;
	outAttributeData.Key = TCHAR_TO_UTF8(*attributeName);

	bool success = false;
	if (variantData.GetType() == EOnlineKeyValuePairDataType::Json
		|| variantData.GetType() == EOnlineKeyValuePairDataType::UInt32
		|| variantData.GetType() == EOnlineKeyValuePairDataType::UInt64
		|| variantData.GetType() == EOnlineKeyValuePairDataType::Blob)
	{
		error = FString::Printf(TEXT("Data of type \"%s\" not supported."), EOnlineKeyValuePairDataType::ToString(variantData.GetType()));
		success = false;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::Empty)
	{
		// Ignore the data
		success = true;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::Bool)
	{
		bool data;
		variantData.GetValue(data);
		outAttributeData.Value.AsBool = data;
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_BOOLEAN;
		success = true;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::Float)
	{
		float data;
		variantData.GetValue(data);
		outAttributeData.Value.AsDouble = data;
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_DOUBLE;
		success = true;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::Double)
	{
		double data;
		variantData.GetValue(data);
		outAttributeData.Value.AsDouble = data;
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_DOUBLE;
		success = true;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::Int64)
	{
		int64 iData;
		variantData.GetValue(iData);
		outAttributeData.Value.AsInt64 = iData;
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_INT64;
		success = true;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::Int32)
	{
		int32 iData;
		variantData.GetValue(iData);
		outAttributeData.Value.AsInt64 = iData;
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_INT64;
		success = true;
	}
	else if (variantData.GetType() == EOnlineKeyValuePairDataType::String)
	{
		FString sData;
		variantData.GetValue(sData);
		outAttributeData.Value.AsUtf8 = TCHAR_TO_UTF8(*sData);
		outAttributeData.ValueType = EOS_ESessionAttributeType::EOS_AT_STRING;
		success = true;
	}
	else
	{
		error = TEXT("Type mismatch for session setting attribute.");
		success = false;
	}

	if (success)
	{
		return outAttributeData;
	}
	return EOS_Sessions_AttributeData();
}

//EOS_SessionModification_AddAttributeOptions FOnlineSessionEpic::CreateCustomAttrHandle(FString attributeName, FVariantData data, EOS_ESessionAttributeAdvertisementType advertisementType)
//{
//	FString error;
//	EOS_Sessions_AttributeData attributeData = CreateEOSAttributeData(attributeName, data, error);
//	if (error.IsEmpty())
//	{
//		EOS_SessionModification_AddAttributeOptions attrOpts = {
//			EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
//			&attributeData,
//			advertisementType
//		};
//		return attrOpts;
//	}
//	else
//	{
//		UE_LOG_ONLINE_SESSION(Warning, TEXT("%s"), *error);
//		return EOS_SessionModification_AddAttributeOptions();
//	}
//}

/** Get a resolved connection string from a session info */
bool FOnlineSessionEpic::GetConnectStringFromSessionInfo(TSharedPtr<FOnlineSessionInfoEpic>& SessionInfo, FString& ConnectInfo, int32 PortOverride)
{
	bool bSuccess = false;
	if (SessionInfo.IsValid())
	{
		if (SessionInfo->HostAddr.IsValid() && SessionInfo->HostAddr->IsValid())
		{
			if (PortOverride != 0)
			{
				ConnectInfo = FString::Printf(TEXT("%s:%d"), *SessionInfo->HostAddr->ToString(false), PortOverride);
			}
			else
			{
				ConnectInfo = FString::Printf(TEXT("%s"), *SessionInfo->HostAddr->ToString(true));
			}

			bSuccess = true;
		}
	}

	return bSuccess;
}

/** Takes the session search handle and populates it the session query settings */
void FOnlineSessionEpic::UpdateSessionSearchParameters(TSharedRef<FOnlineSessionSearch> const& sessionSearchPtr, EOS_HSessionSearch const eosSessionSearch, FString& error)
{
	FOnlineSearchSettings SearchSettings = sessionSearchPtr->QuerySettings;
	for (auto param : SearchSettings.SearchParams)
	{
		EOS_EOnlineComparisonOp compOp = EOS_EOnlineComparisonOp::EOS_CO_ANYOF;
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
		EOS_Sessions_AttributeData attributeData = CreateEOSAttributeData(param.Key.ToString(), param.Value.Data, error);
		if (error.IsEmpty())
		{
			EOS_SessionSearch_SetParameterOptions eosParam = {
				EOS_SESSIONSEARCH_SETPARAMETER_API_LATEST,
				&attributeData,
				compOp
			};

			// Pass the parameter to the session search
			EOS_SessionSearch_SetParameter(eosSessionSearch, &eosParam);
		}
		else
		{
			return;
		}
	}
}

void FOnlineSessionEpic::CreateSessionModificationHandle(FOnlineSessionSettings const& NewSessionSettings, EOS_HSessionModification& ModificationHandle, FString& Error)
{
	// Note on GoTo usage:
	// Goto was used here to remove duplicate calls to Printf
	// and make error handling easier in general.
	// Initializations need to be put in their own scope

	EOS_EResult eosResult = EOS_EResult::EOS_Success;
	FString setting;
	FVariantData data;
	FString error;

	// NumPublicConnections
	{
		setting = TEXT("NumPublicConnections");
		data.SetValue(NewSessionSettings.NumPublicConnections);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
		{
			goto handleError;
		}
	}

	// NumPrivateConnections
	{
		setting = TEXT("NumPrivateConnections");
		data.SetValue(NewSessionSettings.NumPrivateConnections);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
		{
			goto handleError;
		}
	}

	// bUsesPresence
	{
		setting = TEXT("bUsesPresence");
		data.SetValue(NewSessionSettings.bShouldAdvertise);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
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
		data.SetValue(NewSessionSettings.bIsLANMatch);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
		{
			goto handleError;
		}
	}

	// bIsDedicated
	{
		setting = TEXT("bIsDedicated");
		data.SetValue(NewSessionSettings.bIsDedicated);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
		{
			goto handleError;
		}
	}

	// bUsesStats
	{
		setting = TEXT("bUsesStats");
		data.SetValue(NewSessionSettings.bUsesStats);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
		{
			goto handleError;
		}
	}

	// bAllowInvites
	{
		setting = TEXT("bAllowInvites");
		data.SetValue(NewSessionSettings.bAllowInvites);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
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
		data.SetValue(NewSessionSettings.bAntiCheatProtected);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
		{
			goto handleError;
		}
	}

	// BuildUniqueId
	{
		setting = TEXT("BuildUniqueId");
		data.SetValue(NewSessionSettings.BuildUniqueId);

		EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, data, Error);
		if (Error.IsEmpty())
		{
			EOS_SessionModification_AddAttributeOptions attrOpts = {
			   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
			   &attrData,
			   EOS_ESessionAttributeAdvertisementType::EOS_SAAT_Advertise
			};

			eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
			if (eosResult != EOS_EResult::EOS_Success)
			{
				goto handleError;
			}
		}
		else
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

			EOS_Sessions_AttributeData attrData = CreateEOSAttributeData(setting, s.Value.Data, Error);
			if (Error.IsEmpty())
			{
				EOS_SessionModification_AddAttributeOptions attrOpts = {
				   EOS_SESSIONMODIFICATION_ADDATTRIBUTE_API_LATEST,
				   &attrData,
				   advertisementType
				};

				eosResult = EOS_SessionModification_AddAttribute(ModificationHandle, &attrOpts);
				if (eosResult != EOS_EResult::EOS_Success)
				{
					goto handleError;
				}
			}
			else
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
	Error = FString::Printf(TEXT("Cannot update setting: %s - Error Code: %s"), *setting, UTF8_TO_TCHAR(EOS_EResult_ToString(eosResult)));
	ModificationHandle = EOS_HSessionModification();
}

FNamedOnlineSession FOnlineSessionEpic::ActiveSessionToNamedSession(EOS_ActiveSession_Info const* ActiveSession, bool IsHosting)
{
	FString sessionName = UTF8_TO_TCHAR(ActiveSession->SessionName);

	// Create a new local user if from a PUID
	TSharedRef<FUniqueNetId> localUserId = MakeShared<FUniqueNetIdEpic>(UTF8_TO_TCHAR(ActiveSession->LocalUserId));

	FOnlineSession session = SessionDetailsToSessionOnlineSession(ActiveSession->SessionDetails);

	FNamedOnlineSession namedSession(FName(*sessionName), session);
	namedSession.LocalOwnerId = localUserId;
	namedSession.bHosting = IsHosting;

	switch (ActiveSession->State)
	{
	case EOS_EOnlineSessionState::EOS_OSS_NoSession:
		namedSession.SessionState = EOnlineSessionState::NoSession;
		break;
	case EOS_EOnlineSessionState::EOS_OSS_Creating:
		namedSession.SessionState = EOnlineSessionState::Creating;
		break;
	case EOS_EOnlineSessionState::EOS_OSS_Pending:
		namedSession.SessionState = EOnlineSessionState::Pending;
		break;
	case EOS_EOnlineSessionState::EOS_OSS_Starting:
		namedSession.SessionState = EOnlineSessionState::Starting;
		break;
	case EOS_EOnlineSessionState::EOS_OSS_InProgress:
		namedSession.SessionState = EOnlineSessionState::InProgress;
		break;
	case EOS_EOnlineSessionState::EOS_OSS_Ending:
		namedSession.SessionState = EOnlineSessionState::Ending;
		break;
	case EOS_EOnlineSessionState::EOS_OSS_Ended:
		namedSession.SessionState = EOnlineSessionState::Ended;
		break;
	case EOS_EOnlineSessionState::EOS_OSS_Destroying:
		namedSession.SessionState = EOnlineSessionState::Destroying;
		break;
	default:
		checkNoEntry();
		break;
	}

	return namedSession;
}

FOnlineSession FOnlineSessionEpic::SessionDetailsToSessionOnlineSession(EOS_SessionDetails_Info const* SessionDetails)
{
	EOS_SessionDetails_Settings const* eosSessionSettings = SessionDetails->Settings;

	// Copy over the session settings
	FOnlineSessionSettings sessionSettings;
	sessionSettings.bAllowInvites = eosSessionSettings->bInvitesAllowed == EOS_TRUE ? true : false;
	sessionSettings.bAllowJoinInProgress = eosSessionSettings->bAllowJoinInProgress == EOS_TRUE ? true : false;
	sessionSettings.NumPublicConnections = eosSessionSettings->NumPublicConnections;

	// Unique identifier of this build for compatibility
	sessionSettings.BuildUniqueId = GetBuildUniqueId();

	FOnlineSession session(sessionSettings);
	session.NumOpenPublicConnections = SessionDetails->NumOpenPublicConnections;

	// Setup the host session info
	TSharedPtr<FOnlineSessionInfoEpic> sessionInfo = MakeShared<FOnlineSessionInfoEpic>();
	sessionInfo->HostAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

	FString hostAddrStr = UTF8_TO_TCHAR(SessionDetails->HostAddress);
	FString ip, port;
	ip.Split(":", &ip, &port);

	bool isValid;
	sessionInfo->HostAddr->SetIp(*ip, isValid);
	sessionInfo->HostAddr->SetPort(FCString::Atoi(*port));

	sessionInfo->SessionId = MakeShared<FUniqueNetIdEpic>(UTF8_TO_TCHAR(SessionDetails->SessionId));

	session.SessionInfo = sessionInfo;

	return session;
}


// ---------------------------------------------
// EOS method callbacks
// ---------------------------------------------

void FOnlineSessionEpic::OnEOSCreateSessionComplete(const EOS_Sessions_UpdateSessionCallbackInfo* Data)
{
	FName sessionName = FName(Data->SessionName);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult ResultCode = Data->ResultCode;
	/** Context that was passed into EOS_Sessions_UpdateSession */
	FOnlineSessionEpic* thisPtr = (FOnlineSessionEpic*)Data->ClientData;

	if (ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Update Session failed. Error Code: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(ResultCode)));
		thisPtr->RemoveNamedSession(sessionName);
		thisPtr->TriggerOnCreateSessionCompleteDelegates(sessionName, false);
		return;
	}

	FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName);
	if (!session)
	{
		UE_LOG_ONLINE_SESSION(Fatal, TEXT("CreateSession complete callback called, but session \"%s\" not found."), *sessionName.ToString());
		thisPtr->TriggerOnCreateSessionCompleteDelegates(sessionName, false);
		return;
	}

	// Get the session handle for a given session
	EOS_HActiveSession activeSessionHandle = {};
	EOS_Sessions_CopyActiveSessionHandleOptions copyActiveSessionHandleOptions = {
		EOS_SESSIONS_COPYACTIVESESSIONHANDLE_API_LATEST,
		Data->SessionName
	};
	EOS_Sessions_CopyActiveSessionHandle(thisPtr->sessionsHandle, &copyActiveSessionHandleOptions, &activeSessionHandle);

	// Get information about the active session
	EOS_ActiveSession_Info* activeSessionInfo = new EOS_ActiveSession_Info();
	EOS_ActiveSession_CopyInfoOptions activeSessionCopyInfoOptions = {
		EOS_ACTIVESESSION_COPYINFO_API_LATEST
	};
	EOS_ActiveSession_CopyInfo(activeSessionHandle, &activeSessionCopyInfoOptions, &activeSessionInfo);

	// Overwrite the locally created session with the latest remote details just to be sure.
	FNamedOnlineSession nSession = thisPtr->ActiveSessionToNamedSession(activeSessionInfo, true);
	session = &nSession;

	// Release the active session info memory 
	EOS_ActiveSession_Info_Release(activeSessionInfo);

	// Release the active session handle memory
	EOS_ActiveSession_Release(activeSessionHandle);

	// Mark the session as created, but pending to start
	session->SessionState = EOnlineSessionState::Pending;
	UE_LOG_ONLINE_SESSION(Display, TEXT("Created session: %s"), *sessionName.ToString());
	thisPtr->TriggerOnCreateSessionCompleteDelegates(sessionName, true);
}

void FOnlineSessionEpic::OnEOSStartSessionComplete(const EOS_Sessions_StartSessionCallbackInfo* Data)
{
	// Context that was passed into EOS_Sessions_UpdateSession
	// Copy the session ptr and session name, then free
	SessionStateChangeAdditionalData* context = (SessionStateChangeAdditionalData*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	FName sessionName = context->SessionName;
	delete(context);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult result = Data->ResultCode;
	if (result != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't start session. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
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

void FOnlineSessionEpic::OnEOSUpdateSessionComplete(const EOS_Sessions_UpdateSessionCallbackInfo* Data)
{
	FName sessionName = FName(Data->SessionName);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult ResultCode = Data->ResultCode;
	/** Context that was passed into EOS_Sessions_UpdateSession */
	FUpdateSessionAdditionalData* context = (FUpdateSessionAdditionalData*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	FOnlineSessionSettings oldSettings = context->OldSessionSettings;

	if (ResultCode != EOS_EResult::EOS_Success)
	{
		FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName);
		if (session)
		{
			// Revert local only changes
			session->SessionSettings = oldSettings;
		}
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Failed to update session - Error Code: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(ResultCode)));
		thisPtr->TriggerOnCreateSessionCompleteDelegates(sessionName, false);
		return;
	}

	UE_LOG_ONLINE_SESSION(Display, TEXT("Updated session: %s"), *sessionName.ToString());

	// Cleanup the additional resources.
	delete(context);
}

void FOnlineSessionEpic::OnEOSEndSessionComplete(const EOS_Sessions_EndSessionCallbackInfo* Data)
{
	// Context that was passed into EOS_Sessions_UpdateSession
	// Copy the session ptr and session name, then free
	SessionStateChangeAdditionalData* context = (SessionStateChangeAdditionalData*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	FName sessionName = context->SessionName;
	delete(context);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult result = Data->ResultCode;
	if (result != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't end session. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
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
		// Improvement: Maybe just copy the data from the backend?
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Session \"%s\" changed on backend, but local session not found."), *sessionName.ToString());
	}
}

void FOnlineSessionEpic::OnEOSDestroySessionComplete(const EOS_Sessions_DestroySessionCallbackInfo* Data)
{
	// Context that was passed into EOS_Sessions_UpdateSession
	// Copy the session ptr and session name, then free
	SessionStateChangeAdditionalData* context = (SessionStateChangeAdditionalData*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	FName sessionName = context->SessionName;
	delete(context);

	/** Result code for the operation. EOS_Success is returned for a successful operation, otherwise one of the error codes is returned. See eos_common.h */
	EOS_EResult result = Data->ResultCode;
	if (result != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't destroy session. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
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
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Session \"%s\" changed on backend, but local session not found."), *sessionName.ToString());
	}
}

void FOnlineSessionEpic::OnEOSFindSessionComplete(const EOS_SessionSearch_FindCallbackInfo* Data)
{
	// Context that was passed into EOS_Sessions_UpdateSession
	// Copy the session ptr and start time, then free
	FFindSessionsAdditionalData* context = (FFindSessionsAdditionalData*)Data->ClientData;
	FOnlineSessionEpic* thisPtr = context->OnlineSessionPtr;
	double searchStartTime = context->SearchStartTime;
	delete(context);

	FString error;

	TTuple<TSharedPtr<EOS_HSessionSearch>, TSharedRef<FOnlineSessionSearch>>* currentSearch = thisPtr->SessionSearches.Find(searchStartTime);
	if (currentSearch)
	{
		EOS_EResult eosResult = Data->ResultCode;
		if (eosResult == EOS_EResult::EOS_Success)
		{
			TSharedRef<FOnlineSessionSearch> searchRef = currentSearch->Value;
			TSharedPtr<EOS_HSessionSearch> searchHandle = currentSearch->Key;
			checkf(searchHandle, TEXT("%s called, but the EOS session search handle is invalid"), *FString(__FUNCTION__));

			// Get how many results we got
			EOS_SessionSearch_GetSearchResultCountOptions searchResultCountOptions = {
				EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST
			};
			uint32 resultCount = EOS_SessionSearch_GetSearchResultCount(*searchHandle, &searchResultCountOptions);

			if (resultCount > 0)
			{
				for (uint32 i = 0; i < resultCount; ++i)
				{
					EOS_SessionSearch_CopySearchResultByIndexOptions copySearchResultsByIndex = {
						EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST,
						i
					};
					EOS_HSessionDetails sessionDetailsHandle;
					eosResult = EOS_SessionSearch_CopySearchResultByIndex(*searchHandle, &copySearchResultsByIndex, &sessionDetailsHandle);
					if (eosResult == EOS_EResult::EOS_Success)
					{
						// Allocate space for the session infos
						EOS_SessionDetails_Info* eosSessionInfo = new EOS_SessionDetails_Info();

						// Copy the session details
						EOS_SessionDetails_CopyInfoOptions copyInfoOptions = {
							EOS_SESSIONDETAILS_COPYINFO_API_LATEST
						};
						eosResult = EOS_SessionDetails_CopyInfo(sessionDetailsHandle, &copyInfoOptions, &eosSessionInfo);
						if (eosResult == EOS_EResult::EOS_Success)
						{
							FOnlineSession session = thisPtr->SessionDetailsToSessionOnlineSession(eosSessionInfo);

							// Create a new search result.
							// Ping is set to -1, as we have no way of retrieving it for now
							FOnlineSessionSearchResult searchResult;
							searchResult.PingInMs = -1;
							searchResult.Session = session;

							// Add the session to the list of search results.
							searchRef->SearchResults.Add(searchResult);
						}
						else
						{
							error = FString::Printf(TEXT("[EOS SDK] Couldn't copy session info.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
						}

						// Release the prevously allocated memory for the session info;
						EOS_SessionDetails_Info_Release(eosSessionInfo);
					}
					else
					{
						error = FString::Printf(TEXT("[EOS SDK] Couldn't get session details handle.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
					}
				}
			}
			else
			{
				UE_LOG_ONLINE_SESSION(Display, TEXT("No sessions found"));
			}

			EOS_SessionSearch_Release(*searchHandle);
			searchRef->SearchState = EOnlineAsyncTaskState::Done;
		}
		else
		{
			error = FString::Printf(TEXT("[EOS SDK] Couldn't find session. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		}
	}
	else
	{
		error = TEXT("Session search completed, but session not in session search list!");
	}

	UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("Error in %s\r\n    Message: %s"), *FString(__FUNCTION__), *error);
	UE_CLOG_ONLINE_SESSION(error.IsEmpty(), Display, TEXT("Finished session search with start time %f"), searchStartTime);
	thisPtr->TriggerOnFindSessionsCompleteDelegates(error.IsEmpty());
}

void FOnlineSessionEpic::OnEOSJoinSessionComplete(const EOS_Sessions_JoinSessionCallbackInfo* Data)
{
	FJoinSessionAdditionalData* additionalData = (FJoinSessionAdditionalData*)Data->ClientData;
	checkf(additionalData, TEXT("OnEOSJoinSessionComplete delegate called, but no client data available"));

	FOnlineSessionEpic* thisPtr = additionalData->OnlineSessionPtr;
	checkf(thisPtr, TEXT("OnEOSJoinSessionComplete: additional data \"this\" missing"));

	FName sessionName = additionalData->SessionName;

	delete(additionalData);

	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		thisPtr->RemoveNamedSession(sessionName);

		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't find session.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		thisPtr->TriggerOnJoinSessionCompleteDelegates(sessionName, EOnJoinSessionCompleteResult::UnknownError);
		return;
	}

	FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName);
	if (!session)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Tried joining session \"%s\", but session wasn't found."), *sessionName.ToString());
		thisPtr->TriggerOnJoinSessionCompleteDelegates(sessionName, EOnJoinSessionCompleteResult::SessionDoesNotExist);
		return;
	}

	thisPtr->TriggerOnJoinSessionCompleteDelegates(sessionName, EOnJoinSessionCompleteResult::Success);
}

void FOnlineSessionEpic::OnEOSFindFriendSessionComplete(const EOS_SessionSearch_FindCallbackInfo* Data)
{
	FFindFriendSessionAdditionalData* additionalData = (FFindFriendSessionAdditionalData*)Data->ClientData;
	checkf(additionalData, TEXT("%s called, but no ClientData recieved."), *FString(__FUNCTION__));

	FOnlineSessionEpic* thisPtr = additionalData->OnlineSessionPtr;
	checkf(thisPtr, TEXT("%s called, but \"this\" missing from ClientData"), *FString(__FUNCTION__));

	double searchCreationTime = additionalData->SearchCreationTime;

	FUniqueNetId const& searchingUserId = additionalData->SearchingUserId;

	// Free the previously allocated memory
	delete(additionalData);

	FString error;
	TArray<FOnlineSessionSearchResult> searchResults;

	EOS_EResult eosResult = Data->ResultCode;
	if (eosResult == EOS_EResult::EOS_Success)
	{
		// Retrieve the EOS session search handle and the local session search, into which we're going to write the results.
		TSharedPtr<EOS_HSessionSearch> sessionSearchHandle = thisPtr->SessionSearches.Find(searchCreationTime)->Key;
		checkf(sessionSearchHandle, TEXT("%s called, but the EOS session search handle is invalid"), *FString(__FUNCTION__));

		TSharedRef<FOnlineSessionSearch> localSessionSearch = thisPtr->SessionSearches.Find(searchCreationTime)->Value;

		// Get how many results we got
		EOS_SessionSearch_GetSearchResultCountOptions searchResultCountOptions = {
			EOS_SESSIONSEARCH_GETSEARCHRESULTCOUNT_API_LATEST
		};
		uint32 resultCount = EOS_SessionSearch_GetSearchResultCount(*sessionSearchHandle, &searchResultCountOptions);
		if (resultCount > 0)
		{
			for (uint32 i = 0; i < resultCount; ++i)
			{
				EOS_SessionSearch_CopySearchResultByIndexOptions copySearchResultsByIndex = {
					EOS_SESSIONSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST,
					i
				};
				EOS_HSessionDetails sessionDetailsHandle;
				eosResult = EOS_SessionSearch_CopySearchResultByIndex(*sessionSearchHandle, &copySearchResultsByIndex, &sessionDetailsHandle);
				if (eosResult == EOS_EResult::EOS_Success)
				{
					// Allocate space for the session infos
					EOS_SessionDetails_Info* eosSessionInfo = (EOS_SessionDetails_Info*)malloc(sizeof(EOS_SessionDetails_Info));

					// Copy the session details
					EOS_SessionDetails_CopyInfoOptions copyInfoOptions = {
						EOS_SESSIONDETAILS_COPYINFO_API_LATEST
					};
					eosResult = EOS_SessionDetails_CopyInfo(sessionDetailsHandle, &copyInfoOptions, &eosSessionInfo);
					if (eosResult == EOS_EResult::EOS_Success)
					{
						FOnlineSession session = thisPtr->SessionDetailsToSessionOnlineSession(eosSessionInfo);

						// Create a new search result.
						// Ping is set to -1, as we have no way of retrieving it for now
						FOnlineSessionSearchResult searchResult;
						searchResult.PingInMs = -1;
						searchResult.Session = session;

						// Add the session to the list of search results.
						searchResults.Add(searchResult);
					}
					else
					{
						error = FString::Printf(TEXT("[EOS SDK] Couldn't copy session info.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
					}

					// Release the prevously allocated memory for the session info;
					EOS_SessionDetails_Info_Release(eosSessionInfo);
				}
				else
				{
					error = FString::Printf(TEXT("[EOS SDK] Couldn't get session details handle.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
				}
			}

			localSessionSearch->SearchResults = searchResults;
		}
		else
		{
			UE_LOG_ONLINE_SESSION(Display, TEXT("No friend sessions found.\r\n    LocalPlayerId: %s"), *searchingUserId.ToString());
		}
	}
	else
	{
		error = FString::Printf(TEXT("[EOS SDK] Couldn't find session.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
	}

	// Report an error if there was any
	UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("%s"), *error);

	// Get the local index of the user that started the search
	IOnlineIdentityPtr identityPtr = thisPtr->Subsystem->GetIdentityInterface();
	FPlatformUserId userIdx = identityPtr->GetPlatformUserIdFromUniqueNetId(searchingUserId);

	// Trigger delegates
	thisPtr->TriggerOnFindFriendSessionCompleteDelegates(userIdx, error.IsEmpty(), searchResults);
}

void FOnlineSessionEpic::OnEOSSendSessionInviteToFriendsComplete(const EOS_Sessions_SendInviteCallbackInfo* Data)
{
	FOnlineSessionEpic* thisPtr = (FOnlineSessionEpic*)Data->ClientData;
	check(thisPtr);

	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Session invite(s) couldn't be sent to remote.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Display, TEXT("[EOS SDK] Session invite(s) sent to remote."));
	}
}

void FOnlineSessionEpic::OnEOSRegisterPlayersComplete(const EOS_Sessions_RegisterPlayersCallbackInfo* Data)
{
	FRegisterPlayersAdditionalData* additionalData = (FRegisterPlayersAdditionalData*)Data->ClientData;
	checkf(additionalData, TEXT("OnEOSJoinSessionComplete delegate called, but no client data available"));

	FOnlineSessionEpic* thisPtr = additionalData->OnlineSessionPtr;
	checkf(thisPtr, TEXT("OnEOSJoinSessionComplete: additional data \"this\" missing"));

	FName sessionName = additionalData->SessionName;

	TArray<TSharedRef<const FUniqueNetId>> const registeredPlayers = additionalData->RegisteredPlayers;

	delete(additionalData);

	FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName);
	if (!session)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("RegisterPlayers callback called, but session not found.\r\n    %s"), *sessionName.ToString());
		thisPtr->TriggerOnRegisterPlayersCompleteDelegates(sessionName, TArray<TSharedRef<const FUniqueNetId>>(), false);
		return;
	}

	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		for (int32 i = 0; i < registeredPlayers.Num(); ++i)
		{
			session->RegisteredPlayers.RemoveAtSwap(i);

			// update number of open connections
			if (session->NumOpenPublicConnections < session->SessionSettings.NumPublicConnections)
			{
				session->NumOpenPublicConnections += 1;
			}
			else if (session->NumOpenPrivateConnections < session->SessionSettings.NumPrivateConnections)
			{
				session->NumOpenPrivateConnections += 1;
			}
		}

		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't find session.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		thisPtr->TriggerOnRegisterPlayersCompleteDelegates(sessionName, TArray<TSharedRef<const FUniqueNetId>>(), false);
		return;
	}

	thisPtr->TriggerOnRegisterPlayersCompleteDelegates(sessionName, registeredPlayers, true);
}

void FOnlineSessionEpic::OnEOSUnRegisterPlayersComplete(const EOS_Sessions_UnregisterPlayersCallbackInfo* Data)
{
	FRegisterPlayersAdditionalData* additionalData = (FRegisterPlayersAdditionalData*)Data->ClientData;
	checkf(additionalData, TEXT("OnEOSJoinSessionComplete delegate called, but no client data available"));

	FOnlineSessionEpic* thisPtr = additionalData->OnlineSessionPtr;
	checkf(thisPtr, TEXT("OnEOSJoinSessionComplete: additional data \"this\" missing"));

	FName sessionName = additionalData->SessionName;

	TArray<TSharedRef<const FUniqueNetId>> const players = additionalData->RegisteredPlayers;

	delete(additionalData);

	FNamedOnlineSession* session = thisPtr->GetNamedSession(sessionName);
	if (!session)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("RegisterPlayers callback called, but session not found.\r\n    %s"), *sessionName.ToString());
		thisPtr->TriggerOnRegisterPlayersCompleteDelegates(sessionName, TArray<TSharedRef<const FUniqueNetId>>(), false);
		return;
	}

	if (Data->ResultCode != EOS_EResult::EOS_Success)
	{
		for (int32 i = 0; i < players.Num(); ++i)
		{
			session->RegisteredPlayers.Add(players[i]);

			// update number of open connections
			if (session->NumOpenPublicConnections > 0)
			{
				session->NumOpenPublicConnections -= 1;
			}
			else if (session->NumOpenPrivateConnections > 0)
			{
				session->NumOpenPrivateConnections -= 1;
			}
		}

		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Couldn't find session.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode)));
		thisPtr->TriggerOnUnregisterPlayersCompleteDelegates(sessionName, TArray<TSharedRef<const FUniqueNetId>>(), false);
		return;
	}

	thisPtr->TriggerOnUnregisterPlayersCompleteDelegates(sessionName, players, true);
}

void FOnlineSessionEpic::OnEOSSessionInviteReceived(const EOS_Sessions_SessionInviteReceivedCallbackInfo* Data)
{
	FOnlineSessionEpic* thisPtr = (FOnlineSessionEpic*)Data->ClientData;
	checkf(thisPtr, TEXT("%s called. But \"this\" is missing"), *FString(__FUNCTION__));

	// User that received the invite
	TSharedRef<FUniqueNetId const> localUserId = MakeShared<FUniqueNetIdEpic>(UTF8_TO_TCHAR(Data->LocalUserId));

	// User that sent the invite
	TSharedRef<FUniqueNetId const> fromUserId = MakeShared<FUniqueNetIdEpic>(UTF8_TO_TCHAR(Data->TargetUserId));

	EOS_Sessions_CopySessionHandleByInviteIdOptions copySessionHandleByInviteIdOptions = {
		EOS_SESSIONS_COPYSESSIONHANDLEBYINVITEID_API_LATEST,
		Data->InviteId
	};

	EOS_HSessionDetails sessionDetailsHandle = {};
	EOS_EResult eosResult = EOS_Sessions_CopySessionHandleByInviteId(thisPtr->sessionsHandle, &copySessionHandleByInviteIdOptions, &sessionDetailsHandle);
	if (eosResult == EOS_EResult::EOS_Success)
	{
		// Allocate space for the session infos
		EOS_SessionDetails_Info* eosSessionInfo = (EOS_SessionDetails_Info*)malloc(sizeof(EOS_SessionDetails_Info));

		// Copy the session details
		EOS_SessionDetails_CopyInfoOptions copyInfoOptions = {
			EOS_SESSIONDETAILS_COPYINFO_API_LATEST
		};
		eosResult = EOS_SessionDetails_CopyInfo(sessionDetailsHandle, &copyInfoOptions, &eosSessionInfo);
		if (eosResult == EOS_EResult::EOS_Success)
		{
			FOnlineSession session = thisPtr->SessionDetailsToSessionOnlineSession(eosSessionInfo);

			// Create a new search result.
			// Ping is set to -1, as we have no way of retrieving it for now
			FOnlineSessionSearchResult searchResult;
			searchResult.PingInMs = -1;
			searchResult.Session = session;

			thisPtr->TriggerOnSessionInviteReceivedDelegates(*localUserId, *fromUserId, FString(), searchResult);
		}
		else
		{
			UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Error copying session details"));
		}

		EOS_SessionDetails_Info_Release(eosSessionInfo);
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Error copying session handle by invite.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(eosResult)));
	}

	EOS_SessionDetails_Release(sessionDetailsHandle);
}

void FOnlineSessionEpic::OnEOSSessionInviteAccepted(const EOS_Sessions_SessionInviteAcceptedCallbackInfo* Data)
{
	FOnlineSessionEpic* thisPtr = (FOnlineSessionEpic*)Data->ClientData;
	checkf(thisPtr, TEXT("%s called. But \"this\" is missing"), *FString(__FUNCTION__));

	// User that received the invite
	TSharedRef<FUniqueNetId const> localUserId = MakeShared<FUniqueNetIdEpic>(UTF8_TO_TCHAR(Data->LocalUserId));

	// User that sent the invite
	TSharedRef<FUniqueNetId const> fromUserId = MakeShared<FUniqueNetIdEpic>(UTF8_TO_TCHAR(Data->TargetUserId));


	EOS_HSessionDetails sessionDetailsHandle = {};
	EOS_Sessions_CopySessionHandleByInviteIdOptions copySessionHandleByInviteIdOptions = {
		EOS_SESSIONS_COPYSESSIONHANDLEBYINVITEID_API_LATEST,
		Data->InviteId
	};
	EOS_EResult eosResult = EOS_Sessions_CopySessionHandleByInviteId(thisPtr->sessionsHandle, &copySessionHandleByInviteIdOptions, &sessionDetailsHandle);
	if (eosResult == EOS_EResult::EOS_Success)
	{
		// Allocate space for the session infos
		EOS_SessionDetails_Info* eosSessionInfo = (EOS_SessionDetails_Info*)malloc(sizeof(EOS_SessionDetails_Info));

		// Copy the session details
		EOS_SessionDetails_CopyInfoOptions copyInfoOptions = {
			EOS_SESSIONDETAILS_COPYINFO_API_LATEST
		};
		eosResult = EOS_SessionDetails_CopyInfo(sessionDetailsHandle, &copyInfoOptions, &eosSessionInfo);
		if (eosResult == EOS_EResult::EOS_Success)
		{
			FOnlineSession session = thisPtr->SessionDetailsToSessionOnlineSession(eosSessionInfo);

			// Create a new search result.
			// Ping is set to -1, as we have no way of retrieving it for now
			FOnlineSessionSearchResult searchResult;
			searchResult.PingInMs = -1;
			searchResult.Session = session;

			// ToDo: Get the actual controller number
			thisPtr->TriggerOnSessionUserInviteAcceptedDelegates(true, 0, localUserId, searchResult);
		}
		else
		{
			UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Error copying session details"));
		}

		EOS_SessionDetails_Info_Release(eosSessionInfo);
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("[EOS SDK] Error copying session handle by invite.\r\n    Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(eosResult)));
	}

	EOS_SessionDetails_Release(sessionDetailsHandle);

	// ToDo: Get the actual controller number
	thisPtr->TriggerOnSessionUserInviteAcceptedDelegates(false, 0, localUserId, FOnlineSessionSearchResult());
}


// ---------------------------------------------
// IOnlineSession implementations
// ---------------------------------------------

FOnlineSessionEpic::FOnlineSessionEpic(FOnlineSubsystemEpic* InSubsystem)
	: Subsystem(InSubsystem)
{
	// Get the sessions handle
	EOS_HPlatform hPlatform = this->Subsystem->PlatformHandle;
	check(hPlatform);
	EOS_HSessions hSessions = EOS_Platform_GetSessionsInterface(hPlatform);
	check(hSessions);
	this->sessionsHandle = hSessions;

	// Register the callback for a session invite received
	EOS_Sessions_AddNotifySessionInviteReceivedOptions notifySessionInviteReceivedOptions = {
		EOS_SESSIONS_ADDNOTIFYSESSIONINVITERECEIVED_API_LATEST
	};
	this->sessionInviteRecivedCallbackHandle = EOS_Sessions_AddNotifySessionInviteReceived(hSessions, &notifySessionInviteReceivedOptions, this, &FOnlineSessionEpic::OnEOSSessionInviteReceived);

	// Register the callback for a session invite accepted
	EOS_Sessions_AddNotifySessionInviteAcceptedOptions nofitySessionInviteAcceptedOptions = {
		EOS_SESSIONS_ADDNOTIFYSESSIONINVITEACCEPTED_API_LATEST
	};
	this->sessionInviteAcceptedCallbackHandle = EOS_Sessions_AddNotifySessionInviteAccepted(hSessions, &nofitySessionInviteAcceptedOptions, this, &FOnlineSessionEpic::OnEOSSessionInviteAccepted);
}

FOnlineSessionEpic::~FOnlineSessionEpic()
{
	EOS_Sessions_RemoveNotifySessionInviteReceived(this->sessionsHandle, this->sessionInviteRecivedCallbackHandle);
	EOS_Sessions_RemoveNotifySessionInviteAccepted(this->sessionsHandle, this->sessionInviteAcceptedCallbackHandle);
}

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

void FOnlineSessionEpic::Tick(float DeltaTime)
{
	// ToDo: Iterate through all session searches and cancel them if timeout has been reached
}

TSharedPtr<const FUniqueNetId> FOnlineSessionEpic::CreateSessionIdFromString(const FString& SessionIdStr)
{
	// This is a deliberate choice as a session has nothing to do with a user's PUID or EAID
	return MakeShared<FUniqueNetIdString>(SessionIdStr);
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
			checkf(session, TEXT("Failed to create new named session"));

			session->SessionState = EOnlineSessionState::Creating;
			session->NumOpenPrivateConnections = NewSessionSettings.NumPrivateConnections;
			session->NumOpenPublicConnections = NewSessionSettings.NumPublicConnections;


			session->HostingPlayerNum = INDEX_NONE; // HostingPlayernNum is going to be deprecated. Don't use it here
			session->LocalOwnerId = HostingPlayerId.AsShared();
			session->bHosting = true; // A person creating a session is always hosting

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

			result = ONLINE_IO_PENDING;

			// Interface with EOS
			FString bucketId;
			if (!NewSessionSettings.Get(TEXT("BucketId"), bucketId))
			{
				UE_LOG_ONLINE_SESSION(Log, TEXT("No BucketId specified. Using default of \"0\""));
				bucketId = TEXT("0");
			}

			EOS_Sessions_CreateSessionModificationOptions createSessionOptions = {
				EOS_SESSIONS_CREATESESSIONMODIFICATION_API_LATEST,
				TCHAR_TO_UTF8(*SessionName.ToString()),
				TCHAR_TO_UTF8(*bucketId),
				static_cast<uint32_t>(NewSessionSettings.NumPublicConnections),
				((FUniqueNetIdEpic)HostingPlayerId).ToProdcutUserId(),
				session->SessionSettings.bUsesPresence
			};

			// Create a new - local - session handle
			EOS_HSessionModification modificationHandle = nullptr;
			EOS_EResult eosResult = EOS_Sessions_CreateSessionModification(this->sessionsHandle, &createSessionOptions, &modificationHandle);
			if (eosResult == EOS_EResult::EOS_Success)
			{
				this->CreateSessionModificationHandle(NewSessionSettings, modificationHandle, err);
				if (err.IsEmpty())
				{
					// Modify the local session with the modified session options
					EOS_Sessions_UpdateSessionModificationOptions sessionModificationOptions = {
						EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_LATEST,
						TCHAR_TO_UTF8(*SessionName.ToString())
					};
					eosResult = EOS_Sessions_UpdateSessionModification(this->sessionsHandle, &sessionModificationOptions, &modificationHandle);
					if (eosResult == EOS_EResult::EOS_Success)
					{
						// Update the remote session
						EOS_Sessions_UpdateSessionOptions updateSessionOptions = {
							EOS_SESSIONS_UPDATESESSION_API_LATEST,
							modificationHandle
						};
						EOS_Sessions_UpdateSession(this->sessionsHandle, &updateSessionOptions, this, &FOnlineSessionEpic::OnEOSCreateSessionComplete);

						result = ONLINE_IO_PENDING;
					}
					else
					{
						char const* resultStr = EOS_EResult_ToString(eosResult);
						err = FString::Printf(TEXT("[EOS SDK] Error modifying session options - Error Code: %s"), UTF8_TO_TCHAR(resultStr));

						// We failed in creating a new session, so we need to clean up the one we created
						this->RemoveNamedSession(SessionName);

						result = ONLINE_FAIL;
					}
				}
			}
			else
			{
				char const* resultStr = EOS_EResult_ToString(eosResult);
				err = FString::Printf(TEXT("[EOS SDK] Error creating session - Error Code: %s"), UTF8_TO_TCHAR(resultStr));

				// We failed in creating a new session, so we need to clean up the one we created
				this->RemoveNamedSession(SessionName);

				result = ONLINE_FAIL;
			}

			// No matter the update result, release the memory for the SessionModification handle
			EOS_SessionModification_Release(modificationHandle);
		}
	}

	if (result != ONLINE_IO_PENDING)
	{
		if (result == ONLINE_FAIL)
		{
			UE_LOG_ONLINE_SESSION(Warning, TEXT("%s"), *err);
		}
		TriggerOnCreateSessionCompleteDelegates(SessionName, (result == ONLINE_SUCCESS) ? true : false);
	}

	return result == ONLINE_IO_PENDING || result == ONLINE_SUCCESS;
}

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
			SessionStateChangeAdditionalData* additionalInfo = new SessionStateChangeAdditionalData{
				this,
				SessionName
			};
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

bool FOnlineSessionEpic::UpdateSession(FName SessionName, FOnlineSessionSettings& UpdatedSessionSettings, bool bShouldRefreshOnlineData)
{
	FString err;
	uint32 result = ONLINE_FAIL;

	if (FNamedOnlineSession* session = this->GetNamedSession(SessionName))
	{
		// Make a copy of the old settings
		FOnlineSessionSettings oldSettings = FOnlineSessionSettings();

		// Update the local session with the new settings 
		session->SessionSettings = UpdatedSessionSettings;

		// Only do work if the online data should be refreshed
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

					FUpdateSessionAdditionalData* additionalInfo = new FUpdateSessionAdditionalData{
						this,
						oldSettings
					};

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
			SessionStateChangeAdditionalData* additionalInfo = new SessionStateChangeAdditionalData{
				this,
				SessionName
			};
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

			SessionStateChangeAdditionalData* additionalInfo = new SessionStateChangeAdditionalData{
				this,
				SessionName
			};
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

bool FOnlineSessionEpic::IsPlayerInSession(FName SessionName, const FUniqueNetId& UniqueId)
{
	// Improvement: Maybe call SDK backend?
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
bool FOnlineSessionEpic::FindSessions(const FUniqueNetId& SearchingPlayerId, const TSharedRef<FOnlineSessionSearch>& SearchSettings)
{
	FString error;
	uint32 result = ONLINE_FAIL;
	SearchSettings->SearchState = EOnlineAsyncTaskState::NotStarted;

	FUniqueNetIdEpic const epicNetId = static_cast<FUniqueNetIdEpic>(SearchingPlayerId);
	if (epicNetId.IsEpicAccountIdValid())
	{
		if (SearchSettings->bIsLanQuery)
		{
			error = TEXT("LAN matches are not supported.");
		}
		else
		{
			EOS_Sessions_CreateSessionSearchOptions sessionSearchOpts = {
				EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST,
				static_cast<uint32_t>(SearchSettings->MaxSearchResults)
			};

			// Handle where the session search is stored
			EOS_HSessionSearch sessionSearchHandle = {};
			EOS_EResult eosResult = EOS_Sessions_CreateSessionSearch(this->sessionsHandle, &sessionSearchOpts, &sessionSearchHandle);
			if (eosResult == EOS_EResult::EOS_Success)
			{
				UpdateSessionSearchParameters(SearchSettings, sessionSearchHandle, error);
				if (error.IsEmpty()) // Only proceeded if there was no error
				{
					// Locally store the session search so it can be accessed later
					double searchCreationTime = FDateTime::UtcNow().ToUnixTimestamp();

					// Mark the search as in progress
					SearchSettings->SearchState = EOnlineAsyncTaskState::InProgress;

					EOS_SessionSearch_FindOptions findOptions = {
						EOS_SESSIONSEARCH_FIND_API_LATEST,
						epicNetId.ToProdcutUserId()
					};
					FFindSessionsAdditionalData* additionalData = new FFindSessionsAdditionalData{
						this,
						searchCreationTime
					};
					EOS_SessionSearch_Find(sessionSearchHandle, &findOptions, additionalData, &FOnlineSessionEpic::OnEOSFindSessionComplete);

					// Convert the raw session search ptr into a shared one
					TSharedRef<EOS_HSessionSearch> sessionSearchRef = MakeShareable(&sessionSearchHandle);

					// Create pointer to a local, default session search object so the user can later access it
					TSharedRef<FOnlineSessionSearch> sessionSearch = MakeShared<FOnlineSessionSearch>();

					// Mark the session as in progress
					sessionSearch->SearchState = EOnlineAsyncTaskState::InProgress;

					// Store the EOS session search handle and the local session search object
					TTuple<TSharedPtr<EOS_HSessionSearch>, TSharedRef<FOnlineSessionSearch>> value(sessionSearchRef, sessionSearch);
					this->SessionSearches.Add(searchCreationTime, value);

					// Mark the operation as pending
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
		error = TEXT("SearchingPlayerId is not a valid EpicAccountId");
	}

	if (result != ONLINE_IO_PENDING)
	{
		UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("%s"), *error);
		SearchSettings->SearchState = EOnlineAsyncTaskState::Failed;
		TriggerOnFindSessionsCompleteDelegates(false);
	}

	return result == ONLINE_IO_PENDING || result == ONLINE_SUCCESS;
}

bool FOnlineSessionEpic::FindSessionById(const FUniqueNetId& SearchingUserId, const FUniqueNetId& SessionId, const FUniqueNetId& FriendId, const FOnSingleSessionResultCompleteDelegate& CompletionDelegate)
{
	// Currently there is no way to get a named session (and therefore the joinability)
	// of the current session searches or by using EOS_SessionSearch_SetSessionId.
	// A way to make this work could be to assume that searched sessions are `GameSession`s only (bad)
	// Or to only use the locally registered named sessions (also bad)
	UE_LOG_ONLINE_SESSION(Warning, TEXT("FindSessionById not supported by the EOS SDK(yet)."));
	return false;

	//FString error;
	//uint32 result = ONLINE_FAIL;
	//FOnlineSessionSearchResult searchResult;

	//IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	//check(identityPtr);

	//// FPlatformUserId is a typedefed int32, which we use as local index
	//FPlatformUserId localID = identityPtr->GetPlatformUserIdFromUniqueNetId(SearchingUserId);

	//if (FriendId.IsValid())
	//{
	//	error = TEXT("[EOS SDK] Searching for friend id is not supported.");
	//}
	//else
	//{
	//	result = ONLINE_IO_PENDING;
	//	for (auto session : this->CurrentSessionSearches)
	//	{
	//		if (session.Value->SearchState != EOnlineAsyncTaskState::Done)
	//		{
	//			// Only consider completed requests
	//			continue;
	//		}

	//		TArray<FOnlineSessionSearchResult> searchResults = session.Value->SearchResults;
	//		FOnlineSessionSearchResult* searchResultPtr = searchResults.FindByPredicate([&SessionId](FOnlineSessionSearchResult sr)
	//			{
	//				return FUniqueNetIdEpic(sr.GetSessionIdStr()) == SessionId;
	//			});

	//		if (searchResultPtr)
	//		{
	//			result = ONLINE_SUCCESS;
	//			break;
	//		}
	//	}

	//	if (result != ONLINE_SUCCESS)
	//	{
	//		result = ONLINE_FAIL;
	//		error = FString::Printf(TEXT("No session by id found.\r\n    SessionId: %s"), *SessionId.ToString());
	//	}
	//}

	//this->GetNamedSession(searchResult.Session.ses)

	//this->IsSessionJoinable()


	//bool 
	//if (result != ONLINE_IO_PENDING)
	//{
	//	UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("%s"), *error);
	//	CompletionDelegate.ExecuteIfBound(localID, false, FOnlineSessionSearchResult());
	//}


	//CompletionDelegate.ExecuteIfBound(localID, false, FOnlineSessionSearchResult());
	//return result == ONLINE_IO_PENDING || result == ONLINE_SUCCESS;
}

bool FOnlineSessionEpic::CancelFindSessions()
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("CancelFindSession not supported."));
	return false;
}

bool FOnlineSessionEpic::PingSearchResults(const FOnlineSessionSearchResult& SearchResult)
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("PingSearchResults not implemented."));
	return false;
}

bool FOnlineSessionEpic::JoinSession(int32 PlayerNum, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	check(identityPtr);

	TSharedPtr<const FUniqueNetId> netId = identityPtr->GetUniquePlayerId(PlayerNum);
	return this->JoinSession(*netId, SessionName, DesiredSession);
}
bool FOnlineSessionEpic::JoinSession(const FUniqueNetId& PlayerId, FName SessionName, const FOnlineSessionSearchResult& DesiredSession)
{
	FString error;
	uint32 result = ONLINE_FAIL;

	if (this->IsPlayerInSession(SessionName, PlayerId))
	{
		error = FString::Printf(TEXT("Already in session.\r\n    SessionName: %s"), *SessionName.ToString());
	}
	else
	{
		FNamedOnlineSession* namedSession = this->GetNamedSession(SessionName);
		if (!namedSession) // This should be the norm
		{
			namedSession = this->AddNamedSession(SessionName, DesiredSession.Session);
		}

		// Get the session form the search results
		FOnlineSessionSearchResult* searchResultPtr = nullptr;
		for (auto sessionT : this->SessionSearches)
		{
			TSharedRef<FOnlineSessionSearch> session = sessionT.Value.Value;

			// Only consider completed searches
			if (session->SearchState == EOnlineAsyncTaskState::Done)
			{
				// Check if the desired session is in the search results
				searchResultPtr = session->SearchResults.FindByPredicate([&DesiredSession](FOnlineSessionSearchResult sr)
					{
						return FUniqueNetIdEpic(sr.GetSessionIdStr()) == FUniqueNetIdEpic(DesiredSession.GetSessionIdStr());
					});

				if (searchResultPtr)
				{
					// Remove the search result, as it is no longer needed
					this->SessionSearches.Remove(sessionT.Key);
					break;
				}
			}

			if (searchResultPtr)
			{
				// Joining players are the local owner but never the host
				namedSession->HostingPlayerNum = INDEX_NONE; // HostingPlayernNum is going to be deprecated. Don't use it here
				namedSession->LocalOwnerId = PlayerId.AsShared();
				namedSession->bHosting = false;

				IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
				if (identityPtr.IsValid())
				{
					namedSession->OwningUserName = identityPtr->GetPlayerNickname(PlayerId);
				}
				else
				{
					namedSession->OwningUserName = FString(TEXT("EPIC User"));
				}

				namedSession->SessionSettings.BuildUniqueId = GetBuildUniqueId();

				// Register the current player as local player in the session. No need for a callback
				this->RegisterLocalPlayer(PlayerId, SessionName, nullptr);

				// Push the join to backend
				EOS_Sessions_JoinSessionOptions joinSessionOpts = {
							EOS_SESSIONS_JOINSESSION_API_LATEST,
							TCHAR_TO_UTF8(*SessionName.ToString())
				};
				FJoinSessionAdditionalData* additionalData = new FJoinSessionAdditionalData{
					this,
					SessionName
				};
				EOS_Sessions_JoinSession(this->sessionsHandle, &joinSessionOpts, additionalData, &FOnlineSessionEpic::OnEOSJoinSessionComplete);

				result = ONLINE_IO_PENDING;
			}
			else
			{
				error = FString::Printf(TEXT("No sesssion to join found.\r\n Session: %s"), *SessionName.ToString());
			}
		}
	}

	if (result != ONLINE_IO_PENDING)
	{
		UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("Error in %\r\n Message: %s"), *FString(__FUNCTION__), *error);
		TriggerOnJoinSessionCompleteDelegates(SessionName, result == ONLINE_SUCCESS ? EOnJoinSessionCompleteResult::Success : EOnJoinSessionCompleteResult::UnknownError);
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
}

bool FOnlineSessionEpic::FindFriendSession(int32 LocalUserNum, const FUniqueNetId& Friend)
{
	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	check(identityPtr);

	TSharedPtr<const FUniqueNetId> netId = identityPtr->GetUniquePlayerId(LocalUserNum);
	return this->FindFriendSession(*netId, Friend);
}
bool FOnlineSessionEpic::FindFriendSession(const FUniqueNetId& LocalUserId, const FUniqueNetId& Friend)
{
	unimplemented();

	FString error;
	uint32 result = ONLINE_FAIL;

	FUniqueNetIdEpic const epicNetId = (FUniqueNetIdEpic)LocalUserId;
	if (epicNetId.IsEpicAccountIdValid())
	{
		if (Friend.IsValid())
		{
			// Create session search handle
			EOS_HSessionSearch sessionSearchHandle;
			EOS_Sessions_CreateSessionSearchOptions sessionSearchOptions = {
				EOS_SESSIONS_CREATESESSIONSEARCH_API_LATEST,
				1
			};
			EOS_Sessions_CreateSessionSearch(this->sessionsHandle, &sessionSearchOptions, &sessionSearchHandle);

			// To later access everything, the sessions creation time in UTC is used
			double searchCreationTime = FDateTime::UtcNow().ToUnixTimestamp();

			// Set the session search to only search for a user
			EOS_SessionSearch_SetTargetUserIdOptions targetUserIdOptions = {
				EOS_SESSIONSEARCH_SETTARGETUSERID_API_LATEST,
				epicNetId.ToProdcutUserId()
			};
			EOS_SessionSearch_SetTargetUserId(sessionSearchHandle, &targetUserIdOptions);

			// Create the session search itself and execute it
			EOS_SessionSearch_FindOptions findOptions = {
				EOS_SESSIONSEARCH_FIND_API_LATEST,
				NULL
			};
			FFindFriendSessionAdditionalData additionalData = {
				this,
				searchCreationTime,
				LocalUserId
			};
			EOS_SessionSearch_Find(sessionSearchHandle, &findOptions, &additionalData, &FOnlineSessionEpic::OnEOSFindFriendSessionComplete);

			// Convert the raw session search ptr into a shared one
			TSharedRef<EOS_HSessionSearch> sessionSearchRef = MakeShareable(&sessionSearchHandle);

			// Create pointer to a local, default session search object so the user can later access it
			TSharedRef<FOnlineSessionSearch> sessionSearch = MakeShared<FOnlineSessionSearch>();

			// Mark the session as in progress
			sessionSearch->SearchState = EOnlineAsyncTaskState::InProgress;

			// Store the EOS session search handle and the local session search object
			TTuple<TSharedPtr<EOS_HSessionSearch>, TSharedRef<FOnlineSessionSearch>> value(sessionSearchRef, sessionSearch);
			this->SessionSearches.Add(searchCreationTime, value);

			// Mark the operation as pending
			result = ONLINE_IO_PENDING;
		}
		else
		{
			error = FString::Printf(TEXT("Invalid friend id passed to %s"), *FString(__FUNCTION__));
		}
	}
	else
	{
		error = FString::Printf(TEXT("Invalid user id passed to %s"), *FString(__FUNCTION__));
	}

	if (result != ONLINE_IO_PENDING)
	{
		IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
		FPlatformUserId userIdx = identityPtr->GetPlatformUserIdFromUniqueNetId(LocalUserId);

		UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("Error during FindFriendSession:\r\n    Error: %s"), *error);
		this->TriggerOnFindFriendSessionCompleteDelegates(userIdx, false, TArray<FOnlineSessionSearchResult>());
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
}
bool FOnlineSessionEpic::FindFriendSession(const FUniqueNetId& LocalUserId, const TArray<TSharedRef<const FUniqueNetId>>& FriendList)
{
	UE_LOG_ONLINE_SESSION(Warning, TEXT("FindFriendSession with an array of friends is not supported. Repeatedly call the singular version instead."));
	return false;
}

bool FOnlineSessionEpic::SendSessionInviteToFriend(int32 LocalUserNum, FName SessionName, const FUniqueNetId& Friend)
{
	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	check(identityPtr);

	TSharedPtr<const FUniqueNetId> netId = identityPtr->GetUniquePlayerId(LocalUserNum);
	return this->SendSessionInviteToFriend(*netId, SessionName, Friend);
}
bool FOnlineSessionEpic::SendSessionInviteToFriend(const FUniqueNetId& LocalUserId, FName SessionName, const FUniqueNetId& Friend)
{
	TArray<TSharedRef<FUniqueNetId const>> Friends;
	Friends.Emplace(Friend.AsShared());
	return this->SendSessionInviteToFriends(LocalUserId, SessionName, Friends);
}
bool FOnlineSessionEpic::SendSessionInviteToFriends(int32 LocalUserNum, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends)
{
	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	check(identityPtr);

	TSharedPtr<const FUniqueNetId> netId = identityPtr->GetUniquePlayerId(LocalUserNum);
	return this->SendSessionInviteToFriends(*netId, SessionName, Friends);
}

bool FOnlineSessionEpic::SendSessionInviteToFriends(const FUniqueNetId& LocalUserId, FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Friends)
{
	FString error;
	uint32 result = ONLINE_FAIL;

	FUniqueNetIdEpic const epicNetId = (FUniqueNetIdEpic)LocalUserId;
	if (epicNetId.IsEpicAccountIdValid())
	{
		if (this->IsPlayerInSession(SessionName, LocalUserId))
		{
			for (int32 i = 0; i < Friends.Num(); ++i)
			{
				TSharedRef<FUniqueNetId const> friendNetId = Friends[i];
				TSharedRef<FUniqueNetIdEpic const> friendEpicNetId = StaticCastSharedRef<FUniqueNetIdEpic const>(friendNetId);

				EOS_Sessions_SendInviteOptions sendInviteOptions = {
					EOS_SESSIONS_SENDINVITE_API_LATEST,
					TCHAR_TO_UTF8(*SessionName.ToString()),
					epicNetId.ToProdcutUserId(),
					friendEpicNetId->ToProdcutUserId()
				};

				EOS_Sessions_SendInvite(this->sessionsHandle, &sendInviteOptions, this, &FOnlineSessionEpic::OnEOSSendSessionInviteToFriendsComplete);
			}

			result = ONLINE_IO_PENDING;
		}
		else
		{
			error = FString::Printf(TEXT("Player not in session.\r\n    Player: %s\r\n Session: %s"), *LocalUserId.ToString(), *SessionName.ToString());
		}
	}
	else
	{
		error = TEXT("Invalid local user id");
	}

	if (result != ONLINE_IO_PENDING)
	{
		UE_CLOG_ONLINE(!error.IsEmpty(), Warning, TEXT("Error occoured during %s.\r\n    Message: %s"), *FString(__FUNCTION__), *error);
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
}

bool FOnlineSessionEpic::GetResolvedConnectString(FName SessionName, FString& ConnectInfo, FName PortType)
{
	bool bSuccess = false;

	// Find the session
	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session != nullptr)
	{
		TSharedPtr<FOnlineSessionInfoEpic> SessionInfo = StaticCastSharedPtr<FOnlineSessionInfoEpic>(Session->SessionInfo);
		if (PortType == NAME_BeaconPort)
		{
			int32 BeaconListenPort = GetBeaconPortFromSessionSettings(Session->SessionSettings);
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo, BeaconListenPort);
		}
		else if (PortType == NAME_GamePort)
		{
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo);
		}

		if (!bSuccess)
		{
			UE_LOG_ONLINE_SESSION(Warning, TEXT("Invalid session info for session %s in %s"), *SessionName.ToString(), *FString(__FUNCTION__));
		}
	}
	else
	{
		UE_LOG_ONLINE_SESSION(Warning,
			TEXT("Unknown session name (%s) specified to %s"),
			*SessionName.ToString(), *FString(__FUNCTION__));
	}

	return bSuccess;
}
bool FOnlineSessionEpic::GetResolvedConnectString(const FOnlineSessionSearchResult& SearchResult, FName PortType, FString& ConnectInfo)
{
	bool bSuccess = false;
	if (SearchResult.Session.SessionInfo.IsValid())
	{
		TSharedPtr<FOnlineSessionInfoEpic> SessionInfo = StaticCastSharedPtr<FOnlineSessionInfoEpic>(SearchResult.Session.SessionInfo);

		if (PortType == NAME_BeaconPort)
		{
			int32 BeaconListenPort = GetBeaconPortFromSessionSettings(SearchResult.Session.SessionSettings);
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo, BeaconListenPort);

		}
		else if (PortType == NAME_GamePort)
		{
			bSuccess = GetConnectStringFromSessionInfo(SessionInfo, ConnectInfo);
		}
	}

	UE_CLOG_ONLINE_SESSION(!bSuccess || ConnectInfo.IsEmpty(), Warning, TEXT("Invalid session info in search result to %s"), *FString(__FUNCTION__));

	return bSuccess;
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
	FString error;
	uint32 result = ONLINE_FAIL;

	FNamedOnlineSession* Session = GetNamedSession(SessionName);
	if (Session)
	{
		TArray<TSharedRef<const FUniqueNetId>> successfullyRegisteredPlayers;
		EOS_ProductUserId* userIdArr = (EOS_ProductUserId*)malloc(Players.Num() * sizeof(EOS_ProductUserId));
		for (int32 i = 0; i < Players.Num(); ++i)
		{
			TSharedRef<FUniqueNetId const> playerId = Players[i];
			FUniqueNetIdMatcher PlayerMatch(*playerId);
			if (Session->RegisteredPlayers.IndexOfByPredicate(PlayerMatch) == INDEX_NONE)
			{
				Session->RegisteredPlayers.Add(playerId);
				successfullyRegisteredPlayers.Add(playerId);

				TSharedRef<FUniqueNetIdEpic const> epicNetId = StaticCastSharedRef<FUniqueNetIdEpic const>(playerId);
				userIdArr[i] = epicNetId->ToProdcutUserId();

				// update number of open connections
				if (Session->NumOpenPublicConnections > 0)
				{
					Session->NumOpenPublicConnections--;
				}
				else if (Session->NumOpenPrivateConnections > 0)
				{
					Session->NumOpenPrivateConnections--;
				}
			}
			else
			{
				UE_LOG_ONLINE_SESSION(Log, TEXT("Player already registered in session.\r\n    Player: %s\r\n    Session: %s"), *playerId->ToDebugString(), *SessionName.ToString());
			}
		}

		EOS_Sessions_RegisterPlayersOptions registerPlayerOpts = {
			EOS_SESSIONS_REGISTERPLAYERS_API_LATEST,
			TCHAR_TO_UTF8(*SessionName.ToString()),
			userIdArr,
			static_cast<uint32_t>(Players.Num())
		};

		FRegisterPlayersAdditionalData* additionalData = new FRegisterPlayersAdditionalData();
		additionalData->OnlineSessionPtr = this;
		additionalData->SessionName = SessionName;
		additionalData->RegisteredPlayers = successfullyRegisteredPlayers;

		EOS_Sessions_RegisterPlayers(this->sessionsHandle, &registerPlayerOpts, additionalData, &FOnlineSessionEpic::OnEOSRegisterPlayersComplete);

		result = ONLINE_IO_PENDING;
	}
	else
	{
		error = FString::Printf(TEXT("Session not found.\r\n    Session Name: %s"), *SessionName.ToString());
	}

	if (result != ONLINE_IO_PENDING)
	{
		UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("%s"), *error);
		TriggerOnRegisterPlayersCompleteDelegates(SessionName, TArray<TSharedRef<const FUniqueNetId>>(), result == ONLINE_SUCCESS);
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
}

bool FOnlineSessionEpic::UnregisterPlayer(FName SessionName, const FUniqueNetId& PlayerId)
{
	TArray<TSharedRef<const FUniqueNetId>> players;
	players.Add(MakeShared<FUniqueNetIdEpic>(PlayerId));
	return UnregisterPlayers(SessionName, players);
}
bool FOnlineSessionEpic::UnregisterPlayers(FName SessionName, const TArray< TSharedRef<const FUniqueNetId> >& Players)
{
	FString error;
	uint32 result = ONLINE_FAIL;

	FNamedOnlineSession* session = GetNamedSession(SessionName);
	if (session)
	{
		TArray<TSharedRef<const FUniqueNetId>> successfullyRegisteredPlayers;
		EOS_ProductUserId* productUserIdArr = (EOS_ProductUserId*)malloc(Players.Num() * sizeof(EOS_ProductUserId));

		for (int32 i = 0; i < Players.Num(); ++i)
		{
			TSharedRef<const FUniqueNetId> const playerId = Players[i];

			FUniqueNetIdMatcher PlayerMatch(*playerId);
			if (session->RegisteredPlayers.IndexOfByPredicate(PlayerMatch) != INDEX_NONE)
			{
				session->RegisteredPlayers.RemoveAtSwap(i);
				successfullyRegisteredPlayers.Add(playerId);

				// update number of open connections
				if (session->NumOpenPublicConnections < session->SessionSettings.NumPublicConnections)
				{
					session->NumOpenPublicConnections += 1;
				}
				else if (session->NumOpenPrivateConnections < session->SessionSettings.NumPrivateConnections)
				{
					session->NumOpenPrivateConnections += 1;
				}

				TSharedRef<FUniqueNetIdEpic const> epicNetId = StaticCastSharedRef<FUniqueNetIdEpic const>(playerId);
				productUserIdArr[i] = epicNetId->ToProdcutUserId();
			}
			else
			{
				UE_LOG_ONLINE_SESSION(Log, TEXT("Player not in session.\r\n    Player: %s\r\n    Session: %s"), *playerId->ToDebugString(), *SessionName.ToString());
			}
		}

		EOS_Sessions_RegisterPlayersOptions registerPlayerOpts = {
			EOS_SESSIONS_REGISTERPLAYERS_API_LATEST,
			TCHAR_TO_UTF8(*SessionName.ToString()),
			productUserIdArr,
			static_cast<uint32_t>(Players.Num())
		};

		FRegisterPlayersAdditionalData* additionalData = new FRegisterPlayersAdditionalData();
		additionalData->OnlineSessionPtr = this;
		additionalData->SessionName = SessionName;
		additionalData->RegisteredPlayers = successfullyRegisteredPlayers;

		EOS_Sessions_RegisterPlayers(this->sessionsHandle, &registerPlayerOpts, additionalData, &FOnlineSessionEpic::OnEOSRegisterPlayersComplete);
		result = ONLINE_IO_PENDING;
	}
	else
	{
		error = FString::Printf(TEXT("Session not found.\r\n    Session Name: %s"), *SessionName.ToString());
	}

	if (result != ONLINE_IO_PENDING)
	{
		UE_CLOG_ONLINE_SESSION(!error.IsEmpty(), Warning, TEXT("%s"), *error);
		TriggerOnUnregisterPlayersCompleteDelegates(SessionName, TArray<TSharedRef<const FUniqueNetId>>(), result == ONLINE_SUCCESS);
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
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

	if (this->IsPlayerInSession(SessionName, PlayerId))
	{
		Delegate.ExecuteIfBound(PlayerId, EOnJoinSessionCompleteResult::AlreadyInSession);
		return;
	}

	session->RegisteredPlayers.Add(PlayerId.AsShared());

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
void FOnlineSessionEpic::UnregisterLocalPlayer(const FUniqueNetId& PlayerId, FName SessionName, const FOnUnregisterLocalPlayerCompleteDelegate& Delegate)
{
	FNamedOnlineSession* session = this->GetNamedSession(SessionName);
	if (!session)
	{
		UE_LOG_ONLINE_SESSION(Warning, TEXT("Tried registering local player in session, but session doesn't exist."));
		Delegate.ExecuteIfBound(PlayerId, false);
		return;
	}

	session->RegisteredPlayers.RemoveSingle(PlayerId.AsShared());

	// update number of open connections
	if (session->NumOpenPublicConnections < session->SessionSettings.NumPublicConnections)
	{
		session->NumOpenPublicConnections += 1;
	}
	else if (session->NumOpenPrivateConnections < session->SessionSettings.NumPrivateConnections)
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
	FScopeLock ScopeLock(&SessionLock);

	for (int32 SessionIdx = 0; SessionIdx < Sessions.Num(); SessionIdx++)
	{
		DumpNamedSession(&Sessions[SessionIdx]);
	}
}
