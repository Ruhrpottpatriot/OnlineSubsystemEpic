#include "OnlineUserInterfaceEpic.h"
#include "OnlineSubsystemEpicTypes.h"
#include "OnlineSubsystemEpic.h"
#include "Utilities.h"
#include "eos_userinfo.h"
#include "Interfaces/OnlineIdentityInterface.h"

// -------------------------------------------- -
// Implementation file only structs
// These structs carry additional informations to the callbacks
// ---------------------------------------------

/** Stores information about an external id mapping */
typedef struct FExternalIdMapping
{
	TSharedRef<FUniqueNetId const> UserId;
	FString DisplayName;
	FString ExternalId;
	FString AccountType;
} FExternalIdMapping;

/**
 * This structure is needed, since the callback to start as session doesn't include the session's name,
 * which is needed to call the completion delegates
 */
typedef struct FQueryUserInfoAdditionalData {
	FOnlineUserEpic* OnlineUserPtr;
	int32 LocalUserId;
	double StartTime;
} FQueryUserInfoAdditionalData;

typedef struct FQueryUserIdMappingAdditionalInfo
{
	FOnlineUserEpic* OnlineUserPtr;
	IOnlineUser::FOnQueryUserMappingComplete const& CompletionDelegate;
} FQueryUserIdMappingAdditionalInfo;

typedef struct FQueryExternalIdMappingsAdditionalData {
	FOnlineUserEpic* OnlineUserPtr;
	double StartTime;
	const IOnlineUser::FOnQueryExternalIdMappingsComplete& Delegate;
} FQueryExternalIdMappingsAdditionalData;


// ---------------------------------------------
// Free functions/Utility functions.
// 
// Most free functions should be moved to be private class functions
// ---------------------------------------------

FString FOnlineUserEpic::ConcatErrorString(TArray<FString> ErrorStrings)
{
	if (ErrorStrings.Num() == 0)
	{
		return TEXT("");
	}

	FString concatError = FString::Join(ErrorStrings, TEXT(";"));
	return concatError;
}

void FOnlineUserEpic::HandleQueryUserIdMappingsCallback(FOnlineUserEpic* thisPtr, EOS_EResult result, EOS_EpicAccountId eosLocalUserId, EOS_EpicAccountId eosTargetUserId, double startTime, FOnQueryExternalIdMappingsComplete const& completionDelegate)
{
	unimplemented();

	//FString error;

	//// The local user id is ALWAYS needed, ans we want to fail quickly if it is not present
	//TSharedPtr<FUniqueNetId const> localUserId = MakeShared<FUniqueNetIdEpic>(FIdentityUtilities::EpicAccountIDToString(eosLocalUserId));
	//checkf(localUserId, TEXT("%s returned invalid local user id"), *FString(__FUNCTION__));

	//// Since the SDK caches the user info by itself, we only need to check if there were any errors.
	//if (result != EOS_EResult::EOS_Success)
	//{
	//	error = FString::Printf(TEXT("[EOS SDK] Server returned an error. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
	//}
	//else
	//{
	//	TSharedRef<FUniqueNetId> targetUserId = MakeShared<FUniqueNetIdEpic>(FIdentityUtilities::EpicAccountIDToString(eosTargetUserId));
	//	if (targetUserId->IsValid())
	//	{
	//		thisPtr->queriedUserIds.Add(targetUserId);
	//	}
	//	else
	//	{
	//		error = TEXT("TargetUserId is invalid.");
	//	}
	//}

	//// Since the EOS SDK only allows a single user query, we have to make sure the delegate only fires when all user queries are done
	//// For this, we retrieve the query by its start time, and check how many queries are complete. If the amount of completed queries
	//// is equal to the number of total queries the error message will be created and the completion delegate triggered

	//// Lock the following section to make sure the amount of completed queries doesn't change mid way.
	//FScopeLock ScopeLock(&thisPtr->ExternalIdMappingsQueriesLock);

	//// Auto used below to increase readability
	//auto query = thisPtr->ExternalIdMappingsQueries.Find(startTime);

	//FExternalIdQueryOptions const& queryOptions = query->Get<0>();
	//TArray<FString> userIds = query->Get<1>();
	//TArray<bool> completedQueries = query->Get<2>();
	//TArray<FString> errors = query->Get<3>();

	//checkf(userIds.Num() == errors.Num() && errors.Num() == completedQueries.Num(), TEXT("Amount(UserIds, completedQueries, errors) mismatch."));

	//// Count the number of completed queries
	//int32 doneQueries = 0;
	//for (int32 i = 0; i < errors.Num(); ++i)
	//{
	//	if (completedQueries[i])
	//	{
	//		// Change the error message so that the end user knows at which sub-query index the error occurred.
	//		errors[i] = FString::Printf(TEXT("SubQueryId: %d, Message: %s"), i, *errors[i]);
	//		doneQueries += 1;
	//	}
	//}

	//ScopeLock.Unlock();

	//// If all queries are done, log the result of the function
	//if (doneQueries == userIds.Num())
	//{
	//	FString completeErrorString = thisPtr->ConcatErrorString(errors);

	//	if (completeErrorString.IsEmpty())
	//	{
	//		UE_LOG_ONLINE_USER(Display, TEXT("Query user info successful."));
	//		completionDelegate.ExecuteIfBound(true, *localUserId, queryOptions, userIds, FString());
	//	}
	//	else
	//	{
	//		UE_LOG_ONLINE_USER(Warning, TEXT("Query user info failed:\r\n%s"), *error);
	//		completionDelegate.ExecuteIfBound(false, *localUserId, queryOptions, userIds, completeErrorString);
	//	}
	//}
}

FString ExternalAccountTypeToString(EOS_EExternalAccountType externalAccountType)
{
	switch (externalAccountType)
	{
	case EOS_EExternalAccountType::EOS_EAT_EPIC:
		return TEXT("epic");
	case EOS_EExternalAccountType::EOS_EAT_STEAM:
		return TEXT("steam");
	case EOS_EExternalAccountType::EOS_EAT_PSN:
		return TEXT("psn");
	case EOS_EExternalAccountType::EOS_EAT_XBL:
		return TEXT("xbl");
	case EOS_EExternalAccountType::EOS_EAT_DISCORD:
		return TEXT("discord");
	case EOS_EExternalAccountType::EOS_EAT_GOG:
		return TEXT("gog");
	case EOS_EExternalAccountType::EOS_EAT_NINTENDO:
		return TEXT("nintendo");
	case EOS_EExternalAccountType::EOS_EAT_UPLAY:
		return TEXT("uplay");
	case EOS_EExternalAccountType::EOS_EAT_OPENID:
		return TEXT("openid");
	case EOS_EExternalAccountType::EOS_EAT_APPLE:
		return TEXT("apple");
	}
	return TEXT("unknown");
}

bool ExternalAccountTypeFromString(FString externalAccountTypeString, EOS_EExternalAccountType& outExternalAccountType)
{
	bool success = true;
	if (externalAccountTypeString.ToLower() == TEXT("epic"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_EPIC;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("steam"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_STEAM;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("psn"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_PSN;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("xbl"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_XBL;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("discord"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_DISCORD;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("gog"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_GOG;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("nintendo"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_NINTENDO;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("uplay"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_UPLAY;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("openid"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_OPENID;
	}
	else if (externalAccountTypeString.ToLower() == TEXT("apple"))
	{
		outExternalAccountType = EOS_EExternalAccountType::EOS_EAT_APPLE;
	}
	else
	{
		success = false;
	}
	return success;
}

/** checks if the mapping maps to the external id per query options */
bool FilterByPredicate(FExternalIdMapping mapping, FString externalId, FExternalIdQueryOptions QueryOptions, TSharedPtr<FUniqueNetId const> outId)
{
	if (mapping.AccountType == QueryOptions.AuthType)
	{
		bool sameDisplayName = QueryOptions.bLookupByDisplayName && mapping.DisplayName == externalId;
		bool sameId = !QueryOptions.bLookupByDisplayName && mapping.ExternalId == externalId;
		if (sameDisplayName || sameId)
		{
			outId = mapping.UserId;
			return true;
		}
	}
	outId = nullptr;
	return false;
}

// ---------------------------------------------
// EOS SDK Callback functions
// ---------------------------------------------

void FOnlineUserEpic::OnEOSQueryUserInfoComplete(EOS_UserInfo_QueryUserInfoCallbackInfo const* Data)
{
	FString error;
	EOS_EResult result = Data->ResultCode;

	FQueryUserInfoAdditionalData* additionalData = (FQueryUserInfoAdditionalData*)Data->ClientData;
	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;
	checkf(thisPtr, TEXT("%s called, but \"this\" is missing."), *FString(__FUNCTION__));

	// The local user id is ALWAYS needed, ans we want to fail quickly if it is not present
	TSharedPtr<FUniqueNetId const> netId = thisPtr->Subsystem->IdentityInterface->GetUniquePlayerId(additionalData->LocalUserId);
	TSharedPtr<FUniqueNetIdEpic> localUserId = MakeShared<FUniqueNetIdEpic>(*netId);
	localUserId->SetEpicAccountId(Data->LocalUserId);
	checkf(localUserId, TEXT("%s returned invalid local user id"), *FString(__FUNCTION__));

	// Since the SDK caches the user info by itself, we only need to check if there were any errors.
	if (result != EOS_EResult::EOS_Success)
	{
		error = FString::Printf(TEXT("[EOS SDK] Server returned an error. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
	}
	else
	{
		TSharedRef<FUniqueNetId const> targetUserId = MakeShared<FUniqueNetIdEpic>();
		if (!targetUserId->IsValid())
		{
			error = TEXT("Callback returned invalid target user id.");
		}
		else
		{
			thisPtr->queriedUserIds.Add(targetUserId);
		}
	}

	// Since the EOS SDK only allows a single user query, we have to make sure the delegate only fires when all user queries are done
	// For this, we retrieve the query by its start time, and check how many queries are complete. If the amount of completed queries
	// is equal to the number of total queries the error message will be created and the completion delegate triggered

	// Lock the following section to make sure the amount of completed queries doesn't change mid way.
	FScopeLock ScopeLock(&thisPtr->UserQueryLock);

	// Auto used below to increase readability
	auto query = thisPtr->UserQueries.Find(additionalData->StartTime);

	TArray<TSharedRef<FUniqueNetId const>> userIds = query->Get<0>();
	TArray<bool> completedQueries = query->Get<1>();
	TArray<FString> errors = query->Get<2>();

	checkf(userIds.Num() == errors.Num() && errors.Num() == completedQueries.Num(), TEXT("Amount(UserIds, completedQueries, errors) mismatch."));

	// Count the number of completed queries
	int32 doneQueries = 0;
	for (int32 i = 0; i < errors.Num(); ++i)
	{
		if (completedQueries[i])
		{
			// Change the error message so that the end user knows at which sub-query index the error occurred.
			errors[i] = FString::Printf(TEXT("SubQueryId: %d, Message: %s"), i, *errors[i]);
			doneQueries += 1;
		}
	}

	ScopeLock.Unlock();

	// If all queries are done, log the result of the function
	if (doneQueries == userIds.Num())
	{
		FString completeErrorString = thisPtr->ConcatErrorString(errors);
		UE_CLOG_ONLINE_USER(completeErrorString.IsEmpty(), Display, TEXT("Query user info successful."));
		UE_CLOG_ONLINE_USER(!completeErrorString.IsEmpty(), Warning, TEXT("Query user info failed:\r\n%s"), *error);

		IOnlineIdentityPtr identityPtr = thisPtr->Subsystem->GetIdentityInterface();
		FPlatformUserId localUserIndex = identityPtr->GetPlatformUserIdFromUniqueNetId(*localUserId);

		thisPtr->TriggerOnQueryUserInfoCompleteDelegates(localUserIndex, error.IsEmpty(), userIds, completeErrorString);
	}

	// Release the memory from additionalUserData
	delete(additionalData);
}

void FOnlineUserEpic::OnEOSQueryUserInfoByDisplayNameComplete(EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo const* Data)
{
	FQueryUserIdMappingAdditionalInfo* additionalData = (FQueryUserIdMappingAdditionalInfo*)Data->ClientData;
	check(additionalData);

	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;

	FUniqueNetIdEpic localUserId;
	FUniqueNetIdEpic targetUserID;
	FString targetUserDisplayName(UTF8_TO_TCHAR(Data->DisplayName));

	FString error;
	if (localUserId.IsValid())
	{
		if (targetUserID.IsValid())
		{
			additionalData->CompletionDelegate.ExecuteIfBound(true, localUserId, targetUserDisplayName, targetUserID, TEXT(""));
			return;
		}
		else
		{
			error = TEXT("Target user id invalid");
		}
	}
	else
	{
		error = TEXT("Local user id invalid");
	}

	additionalData->CompletionDelegate.ExecuteIfBound(false, FUniqueNetIdEpic(), FString(), FUniqueNetIdEpic(), error);

	// Release additional memory
	delete(additionalData);
}

void FOnlineUserEpic::OnEOSQueryExternalIdMappingsByDisplayNameComplete(EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo const* Data)
{
	FQueryExternalIdMappingsAdditionalData* additionalData = (FQueryExternalIdMappingsAdditionalData*)Data->ClientData;
	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;
	checkf(thisPtr, TEXT("%s called, but \"this\" is missing."), *FString(__FUNCTION__));

	thisPtr->HandleQueryUserIdMappingsCallback(additionalData->OnlineUserPtr, Data->ResultCode, Data->LocalUserId, Data->TargetUserId, additionalData->StartTime, additionalData->Delegate);

	// Release the additional memory
	delete(additionalData);
}

void FOnlineUserEpic::OnEOSQueryExternalIdMappingsByIdComplete(EOS_UserInfo_QueryUserInfoCallbackInfo const* Data)
{
	FQueryExternalIdMappingsAdditionalData* additionalData = (FQueryExternalIdMappingsAdditionalData*)Data->ClientData;
	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;
	checkf(thisPtr, TEXT("%s called, but \"this\" is missing."), *FString(__FUNCTION__));

	thisPtr->HandleQueryUserIdMappingsCallback(thisPtr, Data->ResultCode, Data->LocalUserId, Data->TargetUserId, additionalData->StartTime, additionalData->Delegate);

	// Release the additional memory
	delete(additionalData);
}

// ---------------------------------------------
// IOnlineUser implementations
// ---------------------------------------------

FOnlineUserEpic::FOnlineUserEpic(FOnlineSubsystemEpic* InSubsystem)
	: Subsystem(InSubsystem)
{
	this->userInfoHandle = EOS_Platform_GetUserInfoInterface(InSubsystem->PlatformHandle);
}

void FOnlineUserEpic::Tick(float DeltaTime)
{
}

bool FOnlineUserEpic::QueryUserInfo(int32 LocalUserNum, const TArray<TSharedRef<const FUniqueNetId> >& UserIds)
{
	FString error;
	uint32 result = ONLINE_FAIL;

	if (UserIds.Num())
	{
		if (0 <= LocalUserNum && LocalUserNum < MAX_LOCAL_PLAYERS)
		{
			// Get the id of the local user specified by the index
			IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
			TSharedPtr<FUniqueNetIdEpic const> localUserId = StaticCastSharedPtr<FUniqueNetIdEpic const>(identityPtr->GetUniquePlayerId(LocalUserNum));

			if (localUserId.IsValid() && localUserId->IsEpicAccountIdValid())
			{
				double startTime = FDateTime::UtcNow().ToUnixTimestamp();

				// Store the query inside the queries map beforehand
				// Without this it might be possible that the callback gets an inconsistent array
				TArray<bool> states;
				states.Init(false, UserIds.Num());

				TArray<FString> errors;
				errors.Init(FString(), UserIds.Num());

				TTuple<TArray<TSharedRef<FUniqueNetId const>>, TArray<bool>, TArray<FString>> queries = MakeTuple(UserIds, states, errors);
				this->UserQueries.Add(FDateTime::UtcNow().ToUnixTimestamp(), queries);


				// Start the actual queries
				for (auto id : UserIds)
				{
					// Only do work, if the id is a valid EAID
					TSharedRef<FUniqueNetIdEpic const> targetUserId = StaticCastSharedRef<FUniqueNetIdEpic const>(id);
					if (targetUserId->IsEpicAccountIdValid())
					{
						EOS_UserInfo_QueryUserInfoOptions queryUserInfoOptions = {
						   EOS_USERINFO_QUERYUSERINFO_API_LATEST,
						   localUserId->ToEpicAccountId(),
						   targetUserId->ToEpicAccountId()
						};
						FQueryUserInfoAdditionalData* additionalData = new FQueryUserInfoAdditionalData{
							this,
							LocalUserNum,
							startTime
						};
						EOS_UserInfo_QueryUserInfo(this->userInfoHandle, &queryUserInfoOptions, additionalData, &FOnlineUserEpic::OnEOSQueryUserInfoComplete);

						result = ONLINE_IO_PENDING;
					}
				}
			}
			else
			{
				error = FString::Printf(TEXT("Cannot retrieve user id for local user.\r\n    User index: %d"), LocalUserNum);
			}
		}
		else
		{
			error = FString::Printf(TEXT("Invalid local user index.\r\n    User index: %d"), LocalUserNum);
		}
	}
	else
	{
		// Doing nothing always succeeds
		result = ONLINE_SUCCESS;
	}

	if (result != ONLINE_IO_PENDING)
	{
		UE_CLOG_ONLINE_USER(result == ONLINE_FAIL, Warning, TEXT("Error in %s\r\n%s"), *FString(__FUNCTION__), *error);
		this->TriggerOnQueryUserInfoCompleteDelegates(LocalUserNum, result != ONLINE_FAIL, UserIds, error);
	}

	return result == ONLINE_SUCCESS || result == ONLINE_IO_PENDING;
}

bool FOnlineUserEpic::GetAllUserInfo(int32 LocalUserNum, TArray< TSharedRef<class FOnlineUser> >& OutUsers)
{
	FString error;
	bool success = false;

	if (0 <= LocalUserNum && LocalUserNum < MAX_LOCAL_PLAYERS)
	{
		IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
		TSharedPtr<FUniqueNetIdEpic const> localUserId = StaticCastSharedPtr<FUniqueNetIdEpic const>(identityPtr->GetUniquePlayerId(LocalUserNum));

		if (localUserId.IsValid() && localUserId->IsEpicAccountIdValid())
		{
			// Empty the output array
			OutUsers.Empty();

			// Declare here so the user information can be reused
			EOS_UserInfo* userInfo = nullptr;
			for (auto id : this->queriedUserIds)
			{
				// Only do work, if the id is a valid EAID
				TSharedRef<FUniqueNetIdEpic const> targetUserId = StaticCastSharedRef<FUniqueNetIdEpic const>(id);
				if (targetUserId->IsEpicAccountIdValid())
				{
					EOS_UserInfo_CopyUserInfoOptions copyUserInfoOptions = {
					   EOS_USERINFO_COPYUSERINFO_API_LATEST,
					   localUserId->ToEpicAccountId(),
					   targetUserId->ToEpicAccountId()
					};
					EOS_EResult result = EOS_UserInfo_CopyUserInfo(this->userInfoHandle, &copyUserInfoOptions, &userInfo);
					if (result == EOS_EResult::EOS_Success)
					{
						// Make sure the data is copied
						FString country = UTF8_TO_TCHAR(userInfo->Country);
						FString displayName = UTF8_TO_TCHAR(userInfo->DisplayName);
						FString preferredLanguage = UTF8_TO_TCHAR(userInfo->PreferredLanguage);
						FString nickname = UTF8_TO_TCHAR(userInfo->Nickname);

						TSharedRef<FUserOnlineAccount> localUser = MakeShared<FUserOnlineAccountEpic>(id);
						localUser->SetUserAttribute(USER_ATTR_COUNTRY, country);
						localUser->SetUserAttribute(USER_ATTR_DISPLAYNAME, displayName);
						localUser->SetUserAttribute(USER_ATTR_PREFERRED_LANGUAGE, preferredLanguage);
						localUser->SetUserAttribute(USER_ATTR_PREFERRED_DISPLAYNAME, nickname);

						OutUsers.Add(localUser);
					}
					else
					{
						UE_LOG_ONLINE_USER(Display, TEXT("[EOS SDK] Couldn't get cached user data. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
					}
				}
				else
				{
					UE_LOG_ONLINE_USER(Display, TEXT("User id is not a valid EpicAccountId.\r\n    Id: %s"), *id->ToDebugString())
				}
			}
			if (OutUsers.Num() < this->queriedUserIds.Num())
			{
				error = TEXT("One or more errors while retrieving cached data.");
			}
			else
			{
				success = true;
			}

			// Release the user info memory
			EOS_UserInfo_Release(userInfo);
		}
		else
		{
			error = FString::Printf(TEXT("User id for user \"%d\" is not a valid EpicAccountId."), LocalUserNum);
		}
	}
	else
	{
		error = FString::Printf(TEXT("Invalid local user index: %d"), LocalUserNum);
	}

	UE_CLOG_ONLINE_USER(!success, Warning, TEXT("Error in %s\r\n%s"), *FString(__FUNCTION__), *error);

	return success;
}

TSharedPtr<FOnlineUser> FOnlineUserEpic::GetUserInfo(int32 LocalUserNum, const class FUniqueNetId& UserId)
{
	FString error;
	TSharedPtr<FUserOnlineAccount> localUser = nullptr;

	if (0 <= LocalUserNum && LocalUserNum < MAX_LOCAL_PLAYERS)
	{
		IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
		TSharedPtr<FUniqueNetIdEpic const> localUserId = StaticCastSharedPtr<FUniqueNetIdEpic const>(identityPtr->GetUniquePlayerId(LocalUserNum));

		if (localUserId.IsValid() && localUserId->IsEpicAccountIdValid())
		{
			FUniqueNetIdEpic const epicUserId = (FUniqueNetIdEpic)UserId;

			if (epicUserId.IsEpicAccountIdValid())
			{
				EOS_UserInfo* userInfo = nullptr;

				EOS_UserInfo_CopyUserInfoOptions copyUserInfoOptions = {
				   EOS_USERINFO_COPYUSERINFO_API_LATEST,
				   localUserId->ToEpicAccountId(),
				   epicUserId.ToEpicAccountId()
				};
				EOS_EResult result = EOS_UserInfo_CopyUserInfo(this->userInfoHandle, &copyUserInfoOptions, &userInfo);

				if (result == EOS_EResult::EOS_Success)
				{
					// Make sure the data is copied
					FString country = UTF8_TO_TCHAR(userInfo->Country);
					FString displayName = UTF8_TO_TCHAR(userInfo->DisplayName);
					FString preferredLanguage = UTF8_TO_TCHAR(userInfo->PreferredLanguage);
					FString nickname = UTF8_TO_TCHAR(userInfo->Nickname);

					localUser = MakeShared<FUserOnlineAccountEpic>(UserId.AsShared());
					localUser->SetUserAttribute(USER_ATTR_COUNTRY, country);
					localUser->SetUserAttribute(USER_ATTR_DISPLAYNAME, displayName);
					localUser->SetUserAttribute(USER_ATTR_PREFERRED_LANGUAGE, preferredLanguage);
					localUser->SetUserLocalAttribute(USER_ATTR_PREFERRED_DISPLAYNAME, nickname);
				}
				else
				{
					error = FString::Printf(TEXT("[EOS SDK] Couldn't get cached user data. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
				}

				// Release the user info memory
				EOS_UserInfo_Release(userInfo);
			}
			else
			{
				error = TEXT("Target user id is not a valid EpicAccountId");
			}
		}
		else
		{
			error = TEXT("Local user id is not a valid EpicAccountId");
		}
	}
	else
	{
		error = FString::Printf(TEXT("\"%d\" is not a valid local account index."), LocalUserNum);
	}

	UE_CLOG_ONLINE_USER(!localUser, Warning, TEXT("Error during %s. Message:\r\n%s"), *FString(__FUNCTION__),
		*error);

	return localUser;
}

bool FOnlineUserEpic::QueryUserIdMapping(const FUniqueNetId& UserId, const FString& DisplayNameOrEmail, const FOnQueryUserMappingComplete& Delegate)
{
	FString error;

	FUniqueNetIdEpic const epicNetId = (FUniqueNetIdEpic)UserId;
	if (epicNetId.IsEpicAccountIdValid())
	{
		FQueryUserIdMappingAdditionalInfo* additionalInfo = new FQueryUserIdMappingAdditionalInfo{
			this,
			Delegate
		};

		EOS_UserInfo_QueryUserInfoByDisplayNameOptions queryUserByDisplayNameOptions = {
			EOS_USERINFO_QUERYUSERINFOBYDISPLAYNAME_API_LATEST,
			epicNetId.ToEpicAccountId(),
			TCHAR_TO_UTF8(*DisplayNameOrEmail),
		};
		EOS_UserInfo_QueryUserInfoByDisplayName(this->userInfoHandle, &queryUserByDisplayNameOptions, additionalInfo, &FOnlineUserEpic::OnEOSQueryUserInfoByDisplayNameComplete);
	}
	else
	{
		UE_LOG_ONLINE_USER(Warning, TEXT("Id for querying user is invalid"));
	}

	return false;
}

bool FOnlineUserEpic::QueryExternalIdMappings(const FUniqueNetId& UserId, const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, const FOnQueryExternalIdMappingsComplete& Delegate)
{
	FString error;
	bool success = false;

	if (ExternalIds.Num())
	{
		FUniqueNetIdEpic const epicNetId = (FUniqueNetIdEpic)UserId;
		if (epicNetId.IsValid())
		{
			double startTime = FDateTime::UtcNow().ToUnixTimestamp();
			FQueryExternalIdMappingsAdditionalData* additionalData = new FQueryExternalIdMappingsAdditionalData{
				this,
				startTime,
				Delegate
			};

			// Store the query inside the queries map beforehand
			// Without this it might be possible that the callback gets an inconsistent array
			TArray<bool> states;
			states.Init(false, ExternalIds.Num());

			TArray<FString> errors;
			errors.Init(FString(), ExternalIds.Num());

			TTuple<FExternalIdQueryOptions, TArray<FString>, TArray<bool>, TArray<FString>> queries = MakeTuple(QueryOptions, ExternalIds, states, errors);
			this->ExternalIdMappingsQueries.Add(FDateTime::UtcNow().ToUnixTimestamp(), queries);

			for (auto id : ExternalIds)
			{
				EOS_EpicAccountId eaid = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*id));
				if (EOS_EpicAccountId_IsValid(eaid))
				{
					if (QueryOptions.bLookupByDisplayName)
					{
						EOS_UserInfo_QueryUserInfoByDisplayNameOptions queryByDisplaynameOptions = {
							EOS_USERINFO_QUERYUSERINFOBYDISPLAYNAME_API_LATEST,
							epicNetId.ToEpicAccountId(),
							TCHAR_TO_UTF8(*id)
						};
						EOS_UserInfo_QueryUserInfoByDisplayName(this->userInfoHandle, &queryByDisplaynameOptions, additionalData, &FOnlineUserEpic::OnEOSQueryExternalIdMappingsByDisplayNameComplete);
					}
					else
					{
						EOS_UserInfo_QueryUserInfoOptions queryByIdOtios = {
							EOS_USERINFO_QUERYUSERINFO_API_LATEST,
							epicNetId.ToEpicAccountId(),
							eaid
						};
						EOS_UserInfo_QueryUserInfo(this->userInfoHandle, &queryByIdOtios, additionalData, &FOnlineUserEpic::OnEOSQueryExternalIdMappingsByIdComplete);
					}
				}
				else
				{
					UE_LOG_ONLINE_USER(Display, TEXT("[EOS SDK] \"%s\" is not a vald epic user id"), *id);
				}
			}
			success = true;

		}
		else
		{
			error = TEXT("Local user id is not a valid EpicAccountId");
		}
	}
	else
	{
		UE_LOG_ONLINE_USER(Display, TEXT("No external user ids to query."));
		success = true; // Doing nothing will always succeed.
	}

	UE_CLOG_ONLINE_USER(!success, Warning, TEXT("%s encountered an error. Message\r\n    %s"), *FString(__FUNCTION__), *error);

	return success;
}

void FOnlineUserEpic::GetExternalIdMappings(const FExternalIdQueryOptions& QueryOptions, const TArray<FString>& ExternalIds, TArray<TSharedPtr<const FUniqueNetId>>& OutIds)
{
	if (ExternalIds.Num() > 0)
	{
		OutIds.Empty(ExternalIds.Num());

		for (int32 i = 0; i < ExternalIds.Num(); ++i)
		{
			FString externalId = ExternalIds[i];

			TSharedPtr<FUniqueNetId const> id;
			for (auto m : this->externalIdMappings)
			{
				if (FilterByPredicate(m, externalId, QueryOptions, id))
				{
					break;
				}
			}

			OutIds[i] = id;
		}
	}
	else
	{
		UE_LOG_ONLINE_USER(Warning, TEXT("No external ids to retrieve."));
	}
}

TSharedPtr<const FUniqueNetId> FOnlineUserEpic::GetExternalIdMapping(const FExternalIdQueryOptions& QueryOptions, const FString& ExternalId)
{
	for (auto ex : externalIdMappings)
	{
		if (ex.AccountType == QueryOptions.AuthType.ToLower())
		{
			if (QueryOptions.bLookupByDisplayName)
			{
				if (ex.DisplayName == ExternalId)
				{
					return ex.UserId;
				}
			}
			else
			{
				if (ex.ExternalId == ExternalId)
				{
					return ex.UserId;
				}
			}
		}
	}

	return nullptr;
}
