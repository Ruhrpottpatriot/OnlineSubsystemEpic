#include "EpicOnlineServicesLoginTask.h"

#include "Online.h"
#include "OnlineSubsystemEpic.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSubsystemEpicTypes.h"
#include "Utilities.h"
#include "Private/Utilities.h"


IOnlineIdentityPtr UEpicOnlineServicesIdentityTask::GetIdentityInterface()
{
	auto world = GetWorld();
	IOnlineSubsystem* subsystem = Online::GetSubsystem(world, EPIC_SUBSYSTEM);
	if (subsystem == nullptr) {
		return nullptr;
	}
	auto interface = subsystem->GetIdentityInterface();
	if (!interface.IsValid()) {
		return nullptr;
	}
	return interface;
}

UEpicOnlineServicesLoginTask::UEpicOnlineServicesLoginTask() : LocalUserNum(0)
{
}

void UEpicOnlineServicesLoginTask::OnLoginCompleteDelegate(int32 localUserNum, bool bWasSuccessful, const FUniqueNetId& userId, const FString& errorString)
{
	if (!bWasSuccessful) {
		OnLoginFailure.Broadcast();
	}
	else {
		auto interface = this->GetIdentityInterface();
		auto playerId = interface->GetUniquePlayerId(0);
		auto PlayerNickName = interface->GetPlayerNickname(0);
		
		OnLoginSuccess.Broadcast(playerId->ToString(), PlayerNickName);
	}
	this->EndTask();
} 

void UEpicOnlineServicesLoginTask::EndTask()
{
	auto identityInterface = this->GetIdentityInterface();
	if (identityInterface != nullptr) {
		// TODO: Look at the OculusIdentityCallbackProxy.cpp example
		identityInterface->ClearOnLoginCompleteDelegate_Handle(0, DelegateHandle);
	}
}

void UEpicOnlineServicesLoginTask::Activate()
{
	IOnlineIdentityPtr identityInterface = this->GetIdentityInterface();

	bool result = false;

	if (identityInterface != nullptr) {
		result = identityInterface->Login(LocalUserNum, Credentials);
	}

	if (!result) {
		this->OnLoginFailure.Broadcast();
		this->EndTask();
	}
}

void UEpicOnlineServicesLoginTask::setDelegate(IOnlineIdentityPtr identityInterface)
{
	this->DelegateHandle = identityInterface->AddOnLoginCompleteDelegate_Handle(
		LocalUserNum,
		FOnLoginCompleteDelegate::CreateUObject(this, &UEpicOnlineServicesLoginTask::OnLoginCompleteDelegate)
	);
}

UEpicOnlineServicesLoginTask* UEpicOnlineServicesLoginTask::TryLogin(ELoginType loginType, FString id, FString token, int32 localUserNum)
{
	auto task = NewObject<UEpicOnlineServicesLoginTask>();

	IOnlineIdentityPtr identityInterface = task->GetIdentityInterface();
	if (identityInterface == nullptr) {
		task->OnLoginFailure.Broadcast();
		task->EndTask();
		return nullptr;
	}

	task->setDelegate(identityInterface);

	task->Credentials.Type = FUtils::GetEnumValueAsString<ELoginType>("ELoginType", loginType);

	switch (loginType)
	{
	case ELoginType::AccountPortal:
	case ELoginType::DeviceCode:
	case ELoginType::PersistentAuth:
		task->Credentials.Id = NULL;
		task->Credentials.Token = NULL;
		break;
	case ELoginType::ExchangeCode:
		task->Credentials.Token = NULL;
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