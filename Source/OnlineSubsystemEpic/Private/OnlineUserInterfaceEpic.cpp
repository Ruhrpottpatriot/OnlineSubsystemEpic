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
	// THe local FUniqueNetId the external user info maps to
	TSharedRef<FUniqueNetId const> UserId;

	// The external display name
	FString DisplayName;

	// The external id, can be anything really
	FString ExternalId;

	// The external account type (Steam, XBL, PSN, etc.)
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
	int32 SubQueryIndex;
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
	if (ErrorStrings.Num() == 1)
	{
		return ErrorStrings[0];
	}

	FString concatError = FString::Join(ErrorStrings, TEXT(";"));
	return concatError;
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
	TSharedPtr<FUniqueNetId const> localUserId = MakeShared<FUniqueNetIdEpic>(FIdentityUtilities::EpicAccountIDToString(Data
		->LocalUserId));
	checkf(localUserId, TEXT("%s returned invalid local user id"), *FString(__FUNCTION__));

	// Since the SDK caches the user info by itself, we only need to check if there were any errors.
	if (result != EOS_EResult::EOS_Success)
	{
		error = FString::Printf(TEXT("[EOS SDK] Server returned an error. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
	}
	else
	{
		TSharedRef<FUniqueNetId const> targetUserId = MakeShared<FUniqueNetIdEpic>(FIdentityUtilities::EpicAccountIDToString(Data
			->TargetUserId));
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
	auto query = thisPtr->userQueries.Find(additionalData->StartTime);

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

	FUniqueNetIdEpic localUserId(FIdentityUtilities::EpicAccountIDToString(Data->LocalUserId));
	FUniqueNetIdEpic targetUserID(FIdentityUtilities::EpicAccountIDToString(Data->TargetUserId));
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
	// Make sure the this pointer is always valid. Since we as the caller control this, we can fail fast and hard here
	FQueryExternalIdMappingsAdditionalData* additionalData = (FQueryExternalIdMappingsAdditionalData*)Data->ClientData;
	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;
	checkf(thisPtr, TEXT("%s called, but \"this\" is missing."), *FString(__FUNCTION__));

	// Same as above goes for the local user id
	checkf(EOS_EpicAccountId_IsValid(Data->LocalUserId), TEXT("%s returned invalid local user id"), *FString(__FUNCTION__));
	TSharedPtr<FUniqueNetId const> localUserId = MakeShared<FUniqueNetIdEpic>(FIdentityUtilities::EpicAccountIDToString(Data->LocalUserId));

	// Get the query corresponding query at start time t
	auto query = thisPtr->externalIdMappingsQueries.Find(additionalData->StartTime); // Auto used below to increase readability

	// Get the parts of the tuple
	FExternalIdQueryOptions const& queryOptions = query->Get<0>();
	TArray<FString> userIds = query->Get<1>();
	TArray<bool> completedQueries = query->Get<2>();
	TArray<FString> errors = query->Get<3>();

	// Make sure every array in the tuple has the same number of elements		
	checkf(userIds.Num() == errors.Num() && errors.Num() == completedQueries.Num(), TEXT("Amount(UserIds, CompletedQueries, Errors) mismatch."));


	FString error;
	EOS_EResult result = Data->ResultCode;
	if (result == EOS_EResult::EOS_Success)
	{
		// Get the target user id, shouldn't be null
		if (EOS_EpicAccountId_IsValid(Data->TargetUserId))
		{
			TSharedRef<FUniqueNetId const> targetUserId = MakeShared<FUniqueNetIdEpic>(FIdentityUtilities::EpicAccountIDToString(Data->TargetUserId));
			FString externalAccountType = queryOptions.AuthType;

			FExternalIdMapping* mapping = thisPtr->externalIdMappings.FindByPredicate([&externalAccountType, &targetUserId](FExternalIdMapping mapping)
				{
					return mapping.AccountType == externalAccountType && mapping.UserId == targetUserId;
				});

			// If there already exists a mapping for the target user, update it with new information
			if (mapping)
			{
				mapping->DisplayName = UTF8_TO_TCHAR(Data->DisplayName);
			}
			else
			{
				FExternalIdMapping newMapping {
					targetUserId,
					UTF8_TO_TCHAR(Data->DisplayName),
					externalAccountType
				};
				thisPtr->externalIdMappings.Add(newMapping);
			}
		}
		else
		{
			error = TEXT("[EOS SDK] Target user id invalid");
		}
	}
	else
	{
		error = FString::Printf(TEXT("[EOS SDK] Server returned an error. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));;
	}

	// Put the error into the error array and mark the sub-query as completed
	// No lock needed, as only one callback can ever access the sub-query index
	errors[additionalData->SubQueryIndex] = error;
	completedQueries[additionalData->SubQueryIndex] = true;


	// As the EOS SDK only allows single queries, we have to make sure the delegate only fires when all user queries are done
	// For this, we retrieve the query by its start time, and check how many queries are complete. If the amount of completed queries
	// is equal to the number of total queries the error message will be created and the completion delegate triggered


	// Set the number of queries to finish to the amount of users we had to query
	// This gives us the ability to just check for > 0 later
	int32 queriesYetToFinish = userIds.Num();

	// Lock the following section to make sure the amount of completed queries doesn't change mid way.
	thisPtr->ExternalIdMappingsQueriesLock.Lock();

	// Count the number of completed queries
	for (int32 i = 0; i < userIds.Num(); ++i)
	{
		if (completedQueries[i])
		{
			// Change the error message so that the end user knows at which sub-query index the error occurred.
			if (!errors[i].IsEmpty())
			{
				errors[i] = FString::Printf(TEXT("SubQueryId: %d, Message: %s"), i, *errors[i]);
			}

			// Count down the number of completed queries
			queriesYetToFinish -= 1;
		}
	}

	// Unlock
	thisPtr->ExternalIdMappingsQueriesLock.Unlock();

	// If all queries are done, log the result of the function
	if (queriesYetToFinish == 0)
	{
		FString completeErrorString = thisPtr->ConcatErrorString(errors);

		if (completeErrorString.IsEmpty())
		{
			UE_LOG_ONLINE_USER(Display, TEXT("Query user info successful."));
			additionalData->Delegate.ExecuteIfBound(true, *localUserId, queryOptions, userIds, FString());
		}
		else
		{
			UE_LOG_ONLINE_USER(Warning, TEXT("Query user info failed:\r\n%s"), *error);
			additionalData->Delegate.ExecuteIfBound(false, *localUserId, queryOptions, userIds, completeErrorString);
		}
	}

	// Release the additionalData memory
	delete(additionalData);
}

void FOnlineUserEpic::OnEOSQueryExternalIdMappingsByIdComplete(EOS_UserInfo_QueryUserInfoCallbackInfo const* Data)
{
	// Make sure the this pointer is always valid. Since we as the caller control this, we can fail fast and hard here
	FQueryExternalIdMappingsAdditionalData* additionalData = (FQueryExternalIdMappingsAdditionalData*)Data->ClientData;
	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;
	checkf(thisPtr, TEXT("%s called, but \"this\" is missing."), *FString(__FUNCTION__));

	// Same as above goes for the local user id
	checkf(EOS_EpicAccountId_IsValid(Data->LocalUserId), TEXT("%s returned invalid local user id"), *FString(__FUNCTION__));
	TSharedPtr<FUniqueNetId const> localUserId = MakeShared<FUniqueNetIdEpic>(FIdentityUtilities::EpicAccountIDToString(Data->LocalUserId));

	// Get the query corresponding query at start time t
	auto query = thisPtr->externalIdMappingsQueries.Find(additionalData->StartTime); // Auto used below to increase readability

	// Get the parts of the tuple
	FExternalIdQueryOptions const& queryOptions = query->Get<0>();
	TArray<FString> userIds = query->Get<1>();
	TArray<bool> completedQueries = query->Get<2>();
	TArray<FString> errors = query->Get<3>();

	// Make sure every array in the tuple has the same number of elements		
	checkf(userIds.Num() == errors.Num() && errors.Num() == completedQueries.Num(), TEXT("Amount(UserIds, CompletedQueries, Errors) mismatch."));


	FString error;
	EOS_EResult result = Data->ResultCode;
	if (result == EOS_EResult::EOS_Success)
	{
		// Get the target user id, shouldn't be null
		if (EOS_EpicAccountId_IsValid(Data->TargetUserId))
		{
			EOS_UserInfo_GetExternalUserInfoCountOptions getUserInfoCountOptions = {
								EOS_USERINFO_GETEXTERNALUSERINFOCOUNT_API_LATEST,
								Data->LocalUserId,
								Data->TargetUserId
			};
			uint32 count = EOS_UserInfo_GetExternalUserInfoCount(thisPtr->userInfoHandle, &getUserInfoCountOptions);

			thisPtr->ExternalIdMappingsQueriesLock.Lock();

			EOS_UserInfo_ExternalUserInfo* externalUserInfoHandle = nullptr;
			for (uint32 i = 0; i < count; ++i)
			{
				EOS_UserInfo_CopyExternalUserInfoByIndexOptions copyExternalInfoOptions = {
					EOS_USERINFO_COPYEXTERNALUSERINFOBYINDEX_API_LATEST,
					Data->LocalUserId,
					Data->TargetUserId,
					i
				};
				result = EOS_UserInfo_CopyExternalUserInfoByIndex(thisPtr->userInfoHandle, &copyExternalInfoOptions, &externalUserInfoHandle);
				if (result == EOS_EResult::EOS_Success)
				{
					TSharedRef<FUniqueNetId const> targetUserId = MakeShared<FUniqueNetIdEpic>(FIdentityUtilities::EpicAccountIDToString(Data->TargetUserId));
					FString externalAccountType = queryOptions.AuthType;

					FExternalIdMapping* mapping = thisPtr->externalIdMappings.FindByPredicate([&externalAccountType, &targetUserId](FExternalIdMapping mapping)
						{
							return mapping.AccountType == externalAccountType && mapping.UserId == targetUserId;
						});

					// If there already exists a mapping for the target user, update it with new information
					if (mapping)
					{
						mapping->AccountType = ExternalAccountTypeToString(externalUserInfoHandle->AccountType);
						mapping->DisplayName = UTF8_TO_TCHAR(externalUserInfoHandle->DisplayName);
						mapping->ExternalId = UTF8_TO_TCHAR(externalUserInfoHandle->AccountId);
						mapping->UserId = targetUserId;
					}
					else
					{
						FExternalIdMapping newMapping = {
							targetUserId,
							UTF8_TO_TCHAR(externalUserInfoHandle->DisplayName),
							UTF8_TO_TCHAR(externalUserInfoHandle->AccountId),
							ExternalAccountTypeToString(externalUserInfoHandle->AccountType),
						};
						thisPtr->externalIdMappings.Add(newMapping);
					}
				}
				else
				{
					error = TEXT("[EOS SDK] Couldn't copy external user info.");
				}
			}
			thisPtr->ExternalIdMappingsQueriesLock.Unlock();

			EOS_UserInfo_ExternalUserInfo_Release(externalUserInfoHandle);
		}
		else
		{
			error = TEXT("[EOS SDK] Target user id invalid");
		}
	}
	else
	{
		error = FString::Printf(TEXT("[EOS SDK] Server returned an error. Error: %s"), *UTF8_TO_TCHAR(EOS_EResult_ToString(result)));;
	}

	// Put the error into the error array and mark the sub-query as completed
	// No lock needed, as only one callback can ever access the sub-query index
	errors[additionalData->SubQueryIndex] = error;
	completedQueries[additionalData->SubQueryIndex] = true;


	// As the EOS SDK only allows single queries, we have to make sure the delegate only fires when all user queries are done
	// For this, we retrieve the query by its start time, and check how many queries are complete. If the amount of completed queries
	// is equal to the number of total queries the error message will be created and the completion delegate triggered


	// Set the number of queries to finish to the amount of users we had to query
	// This gives us the ability to just check for > 0 later
	int32 queriesYetToFinish = userIds.Num();

	// Lock the following section to make sure the amount of completed queries doesn't change mid way.
	thisPtr->ExternalIdMappingsQueriesLock.Lock();

	// Count the number of completed queries
	for (int32 i = 0; i < userIds.Num(); ++i)
	{
		if (completedQueries[i])
		{
			// Change the error message so that the end user knows at which sub-query index the error occurred.
			if (!errors[i].IsEmpty())
			{
				errors[i] = FString::Printf(TEXT("SubQueryId: %d, Message: %s"), i, *errors[i]);
			}

			// Count down the number of completed queries
			queriesYetToFinish -= 1;
		}
	}

	// Unlock
	thisPtr->ExternalIdMappingsQueriesLock.Unlock();

	// If all queries are done, log the result of the function
	if (queriesYetToFinish == 0)
	{
		FString completeErrorString = thisPtr->ConcatErrorString(errors);

		if (completeErrorString.IsEmpty())
		{
			UE_LOG_ONLINE_USER(Display, TEXT("Query user info successful."));
			additionalData->Delegate.ExecuteIfBound(true, *localUserId, queryOptions, userIds, FString());
		}
		else
		{
			UE_LOG_ONLINE_USER(Warning, TEXT("Query user info failed:\r\n%s"), *error);
			additionalData->Delegate.ExecuteIfBound(false, *localUserId, queryOptions, userIds, completeErrorString);
		}
	}

	// Release the additionalData memory
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

	if (0 <= LocalUserNum && LocalUserNum < MAX_LOCAL_PLAYERS)
	{
		IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
		TSharedPtr<FUniqueNetId const> userId = identityPtr->GetUniquePlayerId(LocalUserNum);
		if (userId.IsValid() && userId->IsValid())
		{
			double startTime = FDateTime::UtcNow().ToUnixTimestamp();

			// Store the query inside the queries map beforehand
			// Without this it might be possible that the callback gets an inconsistent array
			TArray<bool> states;
			states.Init(false, UserIds.Num());

			TArray<FString> errors;
			errors.Init(FString(), UserIds.Num());

			TTuple<TArray<TSharedRef<FUniqueNetId const>>, TArray<bool>, TArray<FString>> queries = MakeTuple(UserIds, states, errors);
			this->userQueries.Add(FDateTime::UtcNow().ToUnixTimestamp(), queries);

			// Start the actual queries
			for (auto id : UserIds)
			{
				EOS_UserInfo_QueryUserInfoOptions queryUserInfoOptions = {
					EOS_USERINFO_QUERYUSERINFO_API_LATEST,
					FIdentityUtilities::EpicAccountIDFromString(userId->ToString()),
					FIdentityUtilities::EpicAccountIDFromString(id->ToString())
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
		else
		{
			error = FString::Printf(TEXT("Cannot retrieve user id for local user.\r\n    User index: %d"), LocalUserNum);
		}
	}
	else
	{
		error = FString::Printf(TEXT("Invalid local user index.\r\n    User index: %d"), LocalUserNum);
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
		TSharedPtr<FUniqueNetId const> userId = identityPtr->GetUniquePlayerId(LocalUserNum);
		if (userId.IsValid() && userId->IsValid())
		{
			// Empty the output array
			OutUsers.Empty();

			// Allocate the memory for the user information here so it can be reused
			EOS_UserInfo* userInfo = new EOS_UserInfo();
			for (auto id : this->queriedUserIds)
			{
				EOS_UserInfo_CopyUserInfoOptions copyUserInfoOptions = {
				   EOS_USERINFO_COPYUSERINFO_API_LATEST,
				   FIdentityUtilities::EpicAccountIDFromString(userId->ToString()),
				   FIdentityUtilities::EpicAccountIDFromString(id->ToString())
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
			error = FString::Printf(TEXT("Cannot retrieve user id for local user.\r\n    User index: %d"), LocalUserNum);
		}
	}
	else
	{
		error = FString::Printf(TEXT("Invalid local user index.\r\n    User index: %d"), LocalUserNum);
	}

	return success;
}

TSharedPtr<FOnlineUser> FOnlineUserEpic::GetUserInfo(int32 LocalUserNum, const class FUniqueNetId& UserId)
{
	FString error;
	TSharedPtr<FUserOnlineAccount> localUser = nullptr;

	if (0 <= LocalUserNum && LocalUserNum < MAX_LOCAL_PLAYERS)
	{
		IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
		TSharedPtr<FUniqueNetId const> sourceUser = identityPtr->GetUniquePlayerId(LocalUserNum);
		if (sourceUser.IsValid() && sourceUser->IsValid())
		{
			if (UserId.IsValid())
			{
				EOS_UserInfo* userInfo = new EOS_UserInfo();

				EOS_UserInfo_CopyUserInfoOptions copyUserInfoOptions = {
				   EOS_USERINFO_COPYUSERINFO_API_LATEST,
				   FIdentityUtilities::EpicAccountIDFromString(sourceUser->ToString()),
				   FIdentityUtilities::EpicAccountIDFromString(UserId.ToString())
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
				error = TEXT("Invalid target user id.");
			}
		}
		else
		{
			error = TEXT("Invalid local user id");
		}
	}
	else
	{
		error = FString::Printf(TEXT("Invalid local user index.\r\n    User index: %d"), LocalUserNum);
	}

	UE_CLOG_ONLINE_USER(!localUser, Warning, TEXT("Error during %s. Message:\r\n%s"), *FString(__FUNCTION__),
		*error);

	return localUser;
}

bool FOnlineUserEpic::QueryUserIdMapping(const FUniqueNetId& UserId, const FString& DisplayNameOrEmail, const FOnQueryUserMappingComplete& Delegate)
{
	FString error;
	if (UserId.IsValid())
	{
		FQueryUserIdMappingAdditionalInfo* additionalInfo = new FQueryUserIdMappingAdditionalInfo{
			this,
			Delegate
		};

		EOS_UserInfo_QueryUserInfoByDisplayNameOptions queryUserByDisplayNameOptions = {
			EOS_USERINFO_QUERYUSERINFOBYDISPLAYNAME_API_LATEST,
			FIdentityUtilities::EpicAccountIDFromString(UserId.ToString()),
			TCHAR_TO_UTF8(*DisplayNameOrEmail)
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

	if (UserId.IsValid())
	{
		if (ExternalIds.Num() > 0)
		{
			double startTime = FDateTime::UtcNow().ToUnixTimestamp();

			// Store the query inside the queries map beforehand
			// Without this it might be possible that the callback gets an inconsistent array
			TArray<bool> states;
			states.Init(false, ExternalIds.Num());

			TArray<FString> errors;
			errors.Init(FString(), ExternalIds.Num());

			TTuple<FExternalIdQueryOptions, TArray<FString>, TArray<bool>, TArray<FString>> queries = MakeTuple(QueryOptions, ExternalIds, states, errors);
			this->externalIdMappingsQueries.Add(FDateTime::UtcNow().ToUnixTimestamp(), queries);

			for (int32 i = 0; i < ExternalIds.Num(); ++i)
				//for (auto id : ExternalIds)
			{
				FString id = ExternalIds[i];

				FQueryExternalIdMappingsAdditionalData* additionalData = new FQueryExternalIdMappingsAdditionalData{
				this,
				startTime,
				i,
				Delegate
				};

				if (QueryOptions.bLookupByDisplayName)
				{
					EOS_UserInfo_QueryUserInfoByDisplayNameOptions queryByDisplaynameOptions = {
						EOS_USERINFO_QUERYUSERINFOBYDISPLAYNAME_API_LATEST,
						FIdentityUtilities::EpicAccountIDFromString(UserId.ToString()),
						TCHAR_TO_UTF8(*id)
					};
					EOS_UserInfo_QueryUserInfoByDisplayName(this->userInfoHandle, &queryByDisplaynameOptions, additionalData, &FOnlineUserEpic::OnEOSQueryExternalIdMappingsByDisplayNameComplete);
				}
				else
				{
					EOS_UserInfo_QueryUserInfoOptions queryByIdOtios = {
						EOS_USERINFO_QUERYUSERINFO_API_LATEST,
						FIdentityUtilities::EpicAccountIDFromString(UserId.ToString()),
						FIdentityUtilities::EpicAccountIDFromString(id)
					};
					EOS_UserInfo_QueryUserInfo(this->userInfoHandle, &queryByIdOtios, additionalData, &FOnlineUserEpic::OnEOSQueryExternalIdMappingsByIdComplete);
				}
			}
			success = true;
		}
		else
		{
			error = TEXT("No external user ids to query.");
		}
	}
	else
	{
		error = TEXT("Id of querying user is invalid.");
	}

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
