#include "EpicOnlineServicesLoginTask.h"

#include "Online.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSubsystemEpicTypes.h"
#include "Utilities.h"


IOnlineIdentityPtr UEpicOnlineServicesIdentityTask::GetIdentityInterface()
{
	UWorld* world = GetWorld();
	IOnlineSubsystem* subsystem = Online::GetSubsystem(world, EPIC_SUBSYSTEM);
	if (subsystem == nullptr) {
		return nullptr;
	}
	IOnlineIdentityPtr interface = subsystem->GetIdentityInterface();
	if (!interface.IsValid()) {
		return nullptr;
	}
	return interface;
}

// ------------------------------------
// UEpicOnlineServicesIdentityTask - Login via EOS Auth
// ------------------------------------
UEpicOnlineServicesLoginTask::UEpicOnlineServicesLoginTask()
	: LocalUserNum(0)
{
}

void UEpicOnlineServicesLoginTask::OnLoginCompleteDelegate(int32 localUserNum, bool bWasSuccessful, const FUniqueNetId& userId, const FString& errorString)
{
	FString error = errorString;
	if (bWasSuccessful)
	{
		this->OnLoginSuccess.Broadcast();
	}
	else
	{
		this->OnLoginFailure.Broadcast(errorString);
	}

	this->EndTask();
} 

void UEpicOnlineServicesLoginTask::EndTask()
{
	if (IOnlineIdentityPtr identityInterface = this->GetIdentityInterface()) 
	{
		identityInterface->ClearOnLoginCompleteDelegate_Handle(this->LocalUserNum, DelegateHandle);
	}
}

void UEpicOnlineServicesLoginTask::Activate()
{
	if (IOnlineIdentityPtr identityInterface = this->GetIdentityInterface())
	{
		if (identityInterface->Login(LocalUserNum, Credentials))
		{
			// Everything went as planned, return
			return;
		}
	}

	// Something went wrong, abort
	this->OnLoginFailure.Broadcast(TEXT("Failed starting login task"));
	this->EndTask();
}

UEpicOnlineServicesLoginTask* UEpicOnlineServicesLoginTask::TryLogin(ELoginType loginType, FString id, FString token, int32 localUserNum)
{
	UEpicOnlineServicesLoginTask* task = NewObject<UEpicOnlineServicesLoginTask>();

	IOnlineIdentityPtr identityInterface = task->GetIdentityInterface();
	if (identityInterface == nullptr) 
	{
		task->OnLoginFailure.Broadcast(TEXT("Failed retrieving Identity Interface"));
		task->EndTask();
		return nullptr;
	}

	auto loginCompleteDelegate = FOnLoginCompleteDelegate::CreateUObject(task, &UEpicOnlineServicesLoginTask::OnLoginCompleteDelegate);
	task->DelegateHandle = identityInterface->AddOnLoginCompleteDelegate_Handle(task->LocalUserNum, loginCompleteDelegate);

	task->Credentials.Type = FString::Printf(TEXT("EAS:%s"), *FUtils::GetEnumValueAsString<ELoginType>("ELoginType", loginType));

	switch (loginType)
	{
	case ELoginType::AccountPortal:
	case ELoginType::DeviceCode:
	case ELoginType::PersistentAuth:
		task->Credentials.Id = TEXT("");
		task->Credentials.Token = TEXT("");
		break;
	case ELoginType::ExchangeCode:
		task->Credentials.Token = TEXT("");
		task->Credentials.Id = id;
	case ELoginType::Password:
	case ELoginType::Developer:
		task->Credentials.Id = id;
		task->Credentials.Token = token;
		break;
	}

	return task;
}

UEpicOnlineServicesLoginTask* UEpicOnlineServicesLoginTask::TryAutoLogin(int32 localUserNum)
{
	return UEpicOnlineServicesLoginTask::TryLogin(ELoginType::PersistentAuth, FString(), FString(), localUserNum);
}

// ------------------------------------
// UEpicOnlineServicesConnectLoginTask - Login via EOS Connect
// ------------------------------------
UEpicOnlineServicesConnectLoginTask* UEpicOnlineServicesConnectLoginTask::TryLogin(int32 LocalUserNum, EConnectLoginType LoginType, FString Id, FString Token, bool CreateNew)
{
	UEpicOnlineServicesConnectLoginTask* task = NewObject<UEpicOnlineServicesConnectLoginTask>();

	IOnlineIdentityPtr identityInterface = task->GetIdentityInterface();
	if (identityInterface == nullptr) 
	{
		task->OnLoginFailure.Broadcast(TEXT("Failed retrieving Identity Interface"));
		task->EndTask();
		return nullptr;
	}

	// Set the login delegate
	auto loginCompleteDelegate = FOnLoginCompleteDelegate::CreateUObject(task, &UEpicOnlineServicesConnectLoginTask::OnLoginCompleteDelegate);
	task->DelegateHandle = identityInterface->AddOnLoginCompleteDelegate_Handle(LocalUserNum, loginCompleteDelegate);

	// Convert the input argument to credentials
	task->credentials.Id = Id;
	task->credentials.Token = Token;
	task->credentials.Type = FString::Printf(TEXT("CONNECT:%s"), *task->ConnectLoginTypeToString(LoginType));

	// Save if we want to create a new account if we get a continuance token
	task->createNewAccount = CreateNew;

	return task;
}

void UEpicOnlineServicesConnectLoginTask::Activate()
{
	IOnlineIdentityPtr identityInterface = this->GetIdentityInterface();
	if (identityInterface)
	{
		if (identityInterface->Login(this->localUserIdx, this->credentials))
		{
			// Everything went as planned, return
			return;
		}
	}

	// Something went wrong, abort
	this->OnLoginFailure.Broadcast(TEXT("Failed starting login task"));
	this->EndTask();
}

void UEpicOnlineServicesConnectLoginTask::EndTask()
{
	if (IOnlineIdentityPtr identityInterface = this->GetIdentityInterface())
	{
		identityInterface->ClearOnLoginCompleteDelegate_Handle(this->localUserIdx, DelegateHandle);
	}
}

void UEpicOnlineServicesConnectLoginTask::OnLoginCompleteDelegate(int32 localUserNum, bool bWasSuccessful, const FUniqueNetId& userId, const FString& errorString)
{
	FString error = errorString;
	if (bWasSuccessful)
	{
		this->OnLoginSuccess.Broadcast();
	}
	else
	{
		// If the login failed, but we got an FUniqueNetId
		//  we can use continuance token to restart the process
		if (userId.IsValid() && this->createNewAccount)
		{
			UE_LOG_ONLINE_IDENTITY(Display, TEXT("Restarting login flow with continuance token."));

			IOnlineIdentityPtr identityPtr = this->GetIdentityInterface();
			
			FOnlineAccountCredentials contCredentials;
			contCredentials.Type = TEXT("CONNECT:Continuance");
			contCredentials.Id = TEXT("");
			contCredentials.Token = userId.ToString();

			if (identityPtr->Login(localUserNum, contCredentials))
			{
				// Everything went well, return
				return;
			}
			else
			{
				error = TEXT("Failed restarting login flow with continuance token.");
			}
		}
		else
		{
			error = TEXT("User doesn't exist and no new shall be created.");
		}
	}

	OnLoginFailure.Broadcast(error);
	this->EndTask();
}

FString UEpicOnlineServicesConnectLoginTask::ConnectLoginTypeToString(EConnectLoginType LoginType)
{
	switch (LoginType)
	{
	case EConnectLoginType::Steam:
		return TEXT("steam");
		break;
	case EConnectLoginType::PSN:
		return TEXT("psn");
		break;
	case EConnectLoginType::XBL:
		return TEXT("xbl");
		break;
	case EConnectLoginType::GOG:
		return TEXT("gog");
		break;
	case EConnectLoginType::Discord:
		return TEXT("discord");
		break;
	case EConnectLoginType::Nintendo:
		return TEXT("nintendo_id");
		break;
	case EConnectLoginType::NintendoNSA:
		return TEXT("nintendo_nsa");
		break;
	case EConnectLoginType::UPlay:
		return TEXT("uplay");
		break;
	case EConnectLoginType::OpenID:
		return TEXT("openid");
		break;
	case EConnectLoginType::DeviceId:
		return TEXT("device");
		break;
	case EConnectLoginType::Apple:
		return TEXT("apple");
		break;
	default:
		checkNoEntry();
		break;
	}
	
	return TEXT("");
}
