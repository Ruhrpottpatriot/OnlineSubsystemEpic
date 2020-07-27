#include "OnlineUserInterfaceEpic.h"
#include "OnlineSubsystemEpicTypes.h"
#include "OnlineSubsystemEpic.h"
#include "Utilities.h"
#include "eos_userinfo.h"
#include "eos_auth.h"
#include "OnlineIdentityInterfaceEpic.h"
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
};

/**
 * This structure is needed, since the callback to start as session doesn't include the session's name,
 * which is needed to call the completion delegates
 */
typedef struct FQueryUserInfoAdditionalData {
	FOnlineUserEpic* OnlineUserPtr;
	int32 LocalUserId;
	double StartTime;
	//Count the query id index we are currently on
	int32 CurrentQueryUserIndex;
} FQueryUserInfoAdditionalData;

typedef struct FQueryUserIdMappingAdditionalInfo
{
	FOnlineUserEpic* OnlineUserPtr;
	FUniqueNetIdEpic const& LocalUserId;
	IOnlineUser::FOnQueryUserMappingComplete const& CompletionDelegate;
} FQueryUserIdMappingAdditionalInfo;

typedef struct FQueryExternalIdMappingsAdditionalData {
	FOnlineUserEpic* OnlineUserPtr;
	double StartTime;
	int32 SubQueryIndex;
	const IOnlineUser::FOnQueryExternalIdMappingsComplete& Delegate;
	TSharedRef<FUniqueNetIdEpic const> QueryUserId;
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
	FQueryUserInfoAdditionalData* additionalData = (FQueryUserInfoAdditionalData*)Data->ClientData;
	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;
	checkf(thisPtr, TEXT("%s called, but \"this\" is missing."), *FString(__FUNCTION__));


	FString error;
	EOS_EResult result = Data->ResultCode;
	if (result != EOS_EResult::EOS_Success)
	{
		error = FString::Printf(TEXT("[EOS SDK] Server returned an error. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
	}
	else
	{
		TSharedPtr<FUniqueNetId const> netId = thisPtr->Subsystem->IdentityInterface->GetUniquePlayerId(additionalData->LocalUserId);
		TSharedPtr<FUniqueNetIdEpic const> epicNetId = StaticCastSharedPtr<FUniqueNetIdEpic const>(netId);
		if (!epicNetId)
		{
			error = FString::Printf(TEXT("Could not find user for index %d"), additionalData->LocalUserId);
		}
		else
		{
			if (epicNetId->ToEpicAccountId() != Data->LocalUserId)
			{
				error = TEXT("User id for local user index and callback local user id mismatch");
			}
			else
			{
				// Add the target user id to the list of queried users
				thisPtr->queriedUserIdsCache.Add(Data->TargetUserId);
			}
		}
	}

	// Since the EOS SDK only allows a single user query, we have to make sure the delegate only fires when all user queries are done
	// For this, we retrieve the query by its start time, and check how many queries are complete. If the amount of completed queries
	// is equal to the number of total queries the error message will be created and the completion delegate triggered

	// Lock the following section to make sure the amount of completed queries doesn't change mid way.
	thisPtr->UserQueryLock.Lock();

	// Auto used below to increase readability
	auto query = thisPtr->userQueries.Find(additionalData->StartTime);
	
	TArray<TSharedRef<FUniqueNetId const>> userIds = query->Get<0>();
	TArray<bool> completedQueries = query->Get<1>();
	TArray<FString> errors = query->Get<2>();

	int32 CurrentIndex = additionalData->CurrentQueryUserIndex;
	//We need to update the tuple here - otherwise we never complete the query! - Mike
	if (result != EOS_EResult::EOS_Success)
	{
		// Change the error message so that the end user knows at which sub-query index the error occurred.
		errors[CurrentIndex] = FString::Printf(TEXT("SubQueryId: %d, Message: %s"), CurrentIndex, *error);
	}
	
	//Regardless if there is an error or not for this index, we have completed a query
	//we will simply move on to the next index
	completedQueries[CurrentIndex] = true;
	checkf(userIds.Num() == errors.Num() && errors.Num() == completedQueries.Num(), TEXT("Amount(UserIds, completedQueries, errors) mismatch."));
	thisPtr->userQueries[additionalData->StartTime] = MakeTuple(userIds, completedQueries, errors);
	
	// Count the number of completed queries
	int32 doneQueries = 0;
	for (int32 i = 0; i < completedQueries.Num(); ++i)
	{
		//We are done if all queries have been completed in some form
		if (completedQueries[i])
		{
			doneQueries += 1;
		}
	}

	thisPtr->UserQueryLock.Unlock();

	// If all queries are done, log the result of the function
	if (doneQueries == userIds.Num())
	{
		FString completeErrorString = thisPtr->ConcatErrorString(errors);
		UE_CLOG_ONLINE_USER(completeErrorString.IsEmpty(), Display, TEXT("Query user info successful."));
		UE_CLOG_ONLINE_USER(!completeErrorString.IsEmpty(), Warning, TEXT("Query user info failed:\r\n%s"), *error);

		//queries don't respect order, so remove the index on what the current query is set at
		int32 IndexToRemove = thisPtr->TimeToIndexMap[additionalData->StartTime];
		thisPtr->queriedUserIdsCache.RemoveAt(IndexToRemove);
		thisPtr->TimeToIndexMap.Remove(IndexToRemove);

		IOnlineIdentityPtr identityPtr = thisPtr->Subsystem->GetIdentityInterface();
		thisPtr->TriggerOnQueryUserInfoCompleteDelegates(additionalData->LocalUserId, error.IsEmpty(), userIds, completeErrorString);
	}

	// Release the memory from additionalUserData
	delete(additionalData);
}

void FOnlineUserEpic::OnEOSQueryUserInfoByDisplayNameComplete(EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo const* Data)
{
	FQueryUserIdMappingAdditionalInfo* additionalData = (FQueryUserIdMappingAdditionalInfo*)Data->ClientData;
	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;
	EOS_HConnect connectHandle = EOS_Platform_GetConnectInterface(thisPtr->Subsystem->PlatformHandle);

	FString error;
	if (additionalData->LocalUserId.IsProductUserIdValid())
	{
		EOS_Connect_GetExternalAccountMappingsOptions getExternalAccountMappingsOptions = {
				EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST,
				additionalData->LocalUserId.ToProductUserId(),
				EOS_EExternalAccountType::EOS_EAT_EPIC,
				TCHAR_TO_UTF8(*FUniqueNetIdEpic::EpicAccountIdToString(Data->TargetUserId))
		};
		EOS_ProductUserId targetPUID = EOS_Connect_GetExternalAccountMapping(connectHandle, &getExternalAccountMappingsOptions);

		FUniqueNetIdEpic targetUserID(targetPUID, Data->TargetUserId);
		FString targetUserDisplayName(UTF8_TO_TCHAR(Data->DisplayName));

		if (targetUserID.IsValid())
		{
			additionalData->CompletionDelegate.ExecuteIfBound(true, additionalData->LocalUserId, targetUserDisplayName, targetUserID, TEXT(""));
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
	EOS_HConnect connectHandle = EOS_Platform_GetConnectInterface(thisPtr->Subsystem->PlatformHandle);
	
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
			EOS_Connect_GetExternalAccountMappingsOptions getExternalAccountMappingsOptions = {
				EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST,
				additionalData->QueryUserId->ToProductUserId(),
				EOS_EExternalAccountType::EOS_EAT_EPIC,
				TCHAR_TO_UTF8(*FUniqueNetIdEpic::EpicAccountIdToString(Data->TargetUserId))
			};
			EOS_ProductUserId targetPUID = EOS_Connect_GetExternalAccountMapping(connectHandle, &getExternalAccountMappingsOptions);

			TSharedRef<FUniqueNetId const> targetUserId = MakeShared<FUniqueNetIdEpic>(targetPUID, Data->TargetUserId);
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
				FExternalIdMapping newMapping{
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
		checkf(EOS_EpicAccountId_IsValid(Data->LocalUserId), TEXT("%s returned invalid local user id"), *FString(__FUNCTION__));

		EOS_Connect_GetExternalAccountMappingsOptions getExternalAccountMappingsOptions = {
			EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST,
			additionalData->QueryUserId->ToProductUserId(),
			EOS_EExternalAccountType::EOS_EAT_EPIC,
			TCHAR_TO_UTF8(*FUniqueNetIdEpic::EpicAccountIdToString(Data->LocalUserId))
		};
		EOS_ProductUserId localPUID = EOS_Connect_GetExternalAccountMapping(connectHandle, &getExternalAccountMappingsOptions);
		FUniqueNetIdEpic localUserNetId(localPUID, Data->LocalUserId);

		FString completeErrorString = thisPtr->ConcatErrorString(errors);

		if (completeErrorString.IsEmpty())
		{
			UE_LOG_ONLINE_USER(Display, TEXT("Query user info successful."));
			additionalData->Delegate.ExecuteIfBound(true, localUserNetId, queryOptions, userIds, FString());
		}
		else
		{
			UE_LOG_ONLINE_USER(Warning, TEXT("Query user info failed:\r\n%s"), *error);
			additionalData->Delegate.ExecuteIfBound(false, localUserNetId, queryOptions, userIds, completeErrorString);
		}
	}

	// Release the additionalData memory
	delete(additionalData);
}

void FOnlineUserEpic::OnEOSQueryExternalIdMappingsByIdComplete(EOS_UserInfo_QueryUserInfoCallbackInfo const* Data)
{
	FQueryExternalIdMappingsAdditionalData* additionalData = (FQueryExternalIdMappingsAdditionalData*)Data->ClientData;
	FOnlineUserEpic* thisPtr = additionalData->OnlineUserPtr;
	EOS_HConnect connectHandle = EOS_Platform_GetConnectInterface(thisPtr->Subsystem->PlatformHandle);
	
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
					TSharedRef<FUniqueNetId const> targetUserId = MakeShared<FUniqueNetIdEpic>(FUniqueNetIdEpic::EpicAccountIDFromString(UTF8_TO_TCHAR(Data->TargetUserId)));
					FString externalAccountType = queryOptions.AuthType;

					FExternalIdMapping* mapping = thisPtr->externalIdMappings.FindByPredicate([&externalAccountType, &targetUserId](FExternalIdMapping mapping)
						{
							return mapping.AccountType == externalAccountType && mapping.UserId == targetUserId;
						});

					// If there already exists a mapping for the target user, update it with new information
					if (mapping)
					{
						mapping->AccountType = FUtils::ExternalAccountTypeToString(externalUserInfoHandle->AccountType);
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
							FUtils::ExternalAccountTypeToString(externalUserInfoHandle->AccountType),
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
		checkf(EOS_EpicAccountId_IsValid(Data->LocalUserId), TEXT("%s returned invalid local user id"), *FString(__FUNCTION__));

		EOS_Connect_GetExternalAccountMappingsOptions getExternalAccountMappingsOptions = {
			EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST,
			additionalData->QueryUserId->ToProductUserId(),
			EOS_EExternalAccountType::EOS_EAT_EPIC,
			TCHAR_TO_UTF8(*FUniqueNetIdEpic::EpicAccountIdToString(Data->LocalUserId))
		};
		EOS_ProductUserId localPUID = EOS_Connect_GetExternalAccountMapping(connectHandle, &getExternalAccountMappingsOptions);
		FUniqueNetIdEpic localUserNetId(localPUID, Data->LocalUserId);

		FString completeErrorString = thisPtr->ConcatErrorString(errors);

		if (completeErrorString.IsEmpty())
		{
			UE_LOG_ONLINE_USER(Display, TEXT("Query user info successful."));
			additionalData->Delegate.ExecuteIfBound(true, localUserNetId, queryOptions, userIds, FString());
		}
		else
		{
			UE_LOG_ONLINE_USER(Warning, TEXT("Query user info failed:\r\n%s"), *error);
			additionalData->Delegate.ExecuteIfBound(false, localUserNetId, queryOptions, userIds, completeErrorString);
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
				int64 CurrentTimeStamp = FDateTime::UtcNow().ToUnixTimestamp();
				this->userQueries.Add(CurrentTimeStamp, queries);
				//Add only the current time stamp to the index
				if (!this->TimeToIndexMap.Contains(CurrentTimeStamp)) {
					this->TimeToIndexMap.Add(CurrentTimeStamp, this->userQueries.Num() - 1);
				}
					
				// Start the actual queries
				for (int32 i = 0; i < UserIds.Num(); i++)
				{
					// Only do work, if the id is a valid EAID
					TSharedRef<FUniqueNetIdEpic const> targetUserId = StaticCastSharedRef<FUniqueNetIdEpic const>(UserIds[i]);
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
							startTime,
							i
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
		UE_CLOG_ONLINE_USER(result == ONLINE_FAIL, Warning, TEXT("Error in %s. Message:\r\n%s"), *FString(__FUNCTION__), *error);
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
		TSharedPtr<FUniqueNetIdEpic const> localUserNetId = StaticCastSharedPtr<FUniqueNetIdEpic const>(identityPtr->GetUniquePlayerId(LocalUserNum));

		if (localUserNetId.IsValid() && localUserNetId->IsEpicAccountIdValid())
		{
			// Empty the output array
			OutUsers.Empty();

			// Declare here so the user information can be reused
			EOS_UserInfo* userInfo = nullptr;
			for (auto eaid : this->queriedUserIdsCache)
			{
				// Only do work, if the id is a valid EAID
				if (EOS_EpicAccountId_IsValid(eaid))
				{
					EOS_UserInfo_CopyUserInfoOptions copyUserInfoOptions = {
					   EOS_USERINFO_COPYUSERINFO_API_LATEST,
					   localUserNetId->ToEpicAccountId(),
					   eaid
					};
					EOS_EResult result = EOS_UserInfo_CopyUserInfo(this->userInfoHandle, &copyUserInfoOptions, &userInfo);
					if (result == EOS_EResult::EOS_Success)
					{
						EOS_HConnect connectHandle = EOS_Platform_GetConnectInterface(this->Subsystem->PlatformHandle);
						EOS_Connect_GetExternalAccountMappingsOptions getExternalAccountMappingsOptions = {
							EOS_CONNECT_GETEXTERNALACCOUNTMAPPINGS_API_LATEST,
							localUserNetId->ToProductUserId(),
							EOS_EExternalAccountType::EOS_EAT_EPIC,
							TCHAR_TO_UTF8(*FUniqueNetIdEpic::EpicAccountIdToString(eaid))
						};
						EOS_ProductUserId puid = EOS_Connect_GetExternalAccountMapping(connectHandle, &getExternalAccountMappingsOptions);

						// Make sure the data is copied
						FString country = UTF8_TO_TCHAR(userInfo->Country);
						FString displayName = UTF8_TO_TCHAR(userInfo->DisplayName);
						FString preferredLanguage = UTF8_TO_TCHAR(userInfo->PreferredLanguage);
						FString nickname = UTF8_TO_TCHAR(userInfo->Nickname);

						TSharedRef<FUniqueNetIdEpic const> epicNetId = MakeShared<FUniqueNetIdEpic>(puid, eaid);

						TSharedRef<FUserOnlineAccount> localUser = MakeShared<FUserOnlineAccountEpic>(epicNetId);
						localUser->SetUserAttribute(USER_ATTR_COUNTRY, country);
						localUser->SetUserAttribute(USER_ATTR_REALNAME, displayName);
						localUser->SetUserAttribute(USER_ATTR_DISPLAYNAME, displayName);
						localUser->SetUserAttribute(USER_ATTR_PREFERRED_LANGUAGE, preferredLanguage);
						localUser->SetUserAttribute(USER_ATTR_PREFERRED_DISPLAYNAME, nickname);
						localUser->SetUserAttribute(USER_ATTR_ALIAS, nickname);

						OutUsers.Add(localUser);
					}
					else
					{
						UE_LOG_ONLINE_USER(Display, TEXT("[EOS SDK] Couldn't get cached user data. Error: %s"), UTF8_TO_TCHAR(EOS_EResult_ToString(result)));
					}
				}
				else
				{
					UE_LOG_ONLINE_USER(Display, TEXT("\"%s\" is not a valid EpicAccountId."), *FUniqueNetIdEpic::EpicAccountIdToString(eaid));
				}
			}
			if (OutUsers.Num() < this->queriedUserIdsCache.Num())
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
			FUniqueNetIdEpic const epicUserId = static_cast<FUniqueNetIdEpic const>(UserId);

			UE_LOG_ONLINE_USER(Log, TEXT("%hs: Local ID : %s"), __FUNCTION__, *localUserId->ToDebugString());
			UE_LOG_ONLINE_USER(Log, TEXT("%hs: Target ID: %s"), __FUNCTION__, *epicUserId.ToDebugString());
			
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
					FString nickname = FString(UTF8_TO_TCHAR(userInfo->Nickname));

					localUser = MakeShared<FUserOnlineAccountEpic>(UserId.AsShared());
					localUser->SetUserAttribute(USER_ATTR_COUNTRY, country);
					localUser->SetUserAttribute(USER_ATTR_DISPLAYNAME, displayName);
					localUser->SetUserAttribute(USER_ATTR_PREFERRED_LANGUAGE, preferredLanguage);
					localUser->SetUserAttribute(USER_ATTR_PREFERRED_DISPLAYNAME, nickname);
					//Alias is needed in Friends interface, usually nickname is null anyways in EOS
					localUser->SetUserAttribute(USER_ATTR_ALIAS, nickname);

					//Good to log so that users can see the difference between display name and nickname
					FString DebugDisplayName;
					localUser->GetUserAttribute(USER_ATTR_DISPLAYNAME, DebugDisplayName);
					FString DebugNickname;
					localUser->GetUserAttribute(USER_ATTR_ALIAS, DebugNickname);
					
					UE_LOG_ONLINE_USER(Log, TEXT("%s: User name is: %s with nickname of: %s"), *FString(__FUNCTION__), *DebugDisplayName, *DebugNickname);
				}
				else
				{
					FString ResultString = UTF8_TO_TCHAR(EOS_EResult_ToString(result));
					error = FString::Printf(TEXT("[EOS SDK] Couldn't get cached user data. Error: %s"), *ResultString);
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

	IOnlineIdentityPtr identityPtr = this->Subsystem->GetIdentityInterface();
	FPlatformUserId localUserNum = identityPtr->GetPlatformUserIdFromUniqueNetId(UserId);

	FUniqueNetIdEpic const epicNetId = static_cast<FUniqueNetIdEpic const>(UserId);
	if (epicNetId.IsEpicAccountIdValid())
	{
		FQueryUserIdMappingAdditionalInfo* additionalInfo = new FQueryUserIdMappingAdditionalInfo{
			this,
			epicNetId,
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
		if (epicNetId.IsEpicAccountIdValid())
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
						epicNetId.ToEpicAccountId(),
						TCHAR_TO_UTF8(*id)
					};
					EOS_UserInfo_QueryUserInfoByDisplayName(this->userInfoHandle, &queryByDisplaynameOptions, additionalData, &FOnlineUserEpic::OnEOSQueryExternalIdMappingsByDisplayNameComplete);

					success = true;
				}
				else
				{
					EOS_EpicAccountId eaid = EOS_EpicAccountId_FromString(TCHAR_TO_UTF8(*id));
					if (EOS_EpicAccountId_IsValid(eaid))
					{
						EOS_UserInfo_QueryUserInfoOptions queryByIdOtios = {
							EOS_USERINFO_QUERYUSERINFO_API_LATEST,
							epicNetId.ToEpicAccountId(),
							//eaid
						};
						EOS_UserInfo_QueryUserInfo(this->userInfoHandle, &queryByIdOtios, additionalData, &FOnlineUserEpic::OnEOSQueryExternalIdMappingsByIdComplete);

						success = true;
					}
					else
					{
						UE_LOG_ONLINE_USER(Display, TEXT("[EOS SDK] \"%s\" is not a vald epic user id"), *id);
					}
				}
			}
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
		UE_LOG_ONLINE_USER(Display, TEXT("Amount of external ids to retrieve is zero."));
	}
}

TSharedPtr<const FUniqueNetId> FOnlineUserEpic::GetExternalIdMapping(const FExternalIdQueryOptions& QueryOptions, const FString& ExternalId)
{
	TArray<FString> ids = { ExternalId };
	TArray<TSharedPtr<FUniqueNetId const>> outIds;
	GetExternalIdMappings(QueryOptions, ids, outIds);

	if (outIds.Num() < 1)
	{
		return nullptr;
	}
	if (outIds.Num() > 1)
	{
		UE_LOG_ONLINE_USER(Warning, TEXT("More than one id for the given external id \"%s\"."), *ExternalId);
		return nullptr;
	}
	return outIds[0];
}
