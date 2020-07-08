#include "OnlineSubsystemEpic.h"
#include "OnlineIdentityInterfaceEpic.h"
#include "OnlineSessionInterfaceEpic.h"
#include "OnlineUserInterfaceEpic.h"
#include "OnlineFriendInterfaceEpic.h"
#include "Utilities.h"
#include <string>

IOnlineSessionPtr FOnlineSubsystemEpic::GetSessionInterface() const
{
	return this->SessionInterface;
}

IOnlineFriendsPtr FOnlineSubsystemEpic::GetFriendsInterface() const
{
	return this->FriendsInterface;
}

IOnlinePartyPtr FOnlineSubsystemEpic::GetPartyInterface() const
{
	return nullptr;
}

IOnlineGroupsPtr FOnlineSubsystemEpic::GetGroupsInterface() const
{
	return nullptr;
}

IOnlineSharedCloudPtr FOnlineSubsystemEpic::GetSharedCloudInterface() const
{
	return nullptr;
}

IOnlineUserCloudPtr FOnlineSubsystemEpic::GetUserCloudInterface() const
{
	return nullptr;
}

IOnlineEntitlementsPtr FOnlineSubsystemEpic::GetEntitlementsInterface() const
{
	return nullptr;
}

IOnlineLeaderboardsPtr FOnlineSubsystemEpic::GetLeaderboardsInterface() const
{
	return nullptr;
}

IOnlineVoicePtr FOnlineSubsystemEpic::GetVoiceInterface() const
{
	return nullptr;
}

IOnlineExternalUIPtr FOnlineSubsystemEpic::GetExternalUIInterface() const
{
	return nullptr;
}

IOnlineTimePtr FOnlineSubsystemEpic::GetTimeInterface() const
{
	return nullptr;
}

IOnlineIdentityPtr FOnlineSubsystemEpic::GetIdentityInterface() const
{
	return this->IdentityInterface;
}

IOnlineTitleFilePtr FOnlineSubsystemEpic::GetTitleFileInterface() const
{
	return nullptr;
}

IOnlineStorePtr FOnlineSubsystemEpic::GetStoreInterface() const
{
	return nullptr;
}

IOnlineStoreV2Ptr FOnlineSubsystemEpic::GetStoreV2Interface() const
{
	return nullptr;
}

IOnlinePurchasePtr FOnlineSubsystemEpic::GetPurchaseInterface() const
{
	return nullptr;
}

IOnlineEventsPtr FOnlineSubsystemEpic::GetEventsInterface() const
{
	return nullptr;
}

IOnlineAchievementsPtr FOnlineSubsystemEpic::GetAchievementsInterface() const
{
	return nullptr;
}

IOnlineSharingPtr FOnlineSubsystemEpic::GetSharingInterface() const
{
	return nullptr;
}

IOnlineUserPtr FOnlineSubsystemEpic::GetUserInterface() const
{
	return this->UserInterface;
}

IOnlineMessagePtr FOnlineSubsystemEpic::GetMessageInterface() const
{
	return nullptr;
}

IOnlinePresencePtr FOnlineSubsystemEpic::GetPresenceInterface() const
{
	return this->PresenceInterface;
}

IOnlineChatPtr FOnlineSubsystemEpic::GetChatInterface() const
{
	return nullptr;
}

IOnlineStatsPtr FOnlineSubsystemEpic::GetStatsInterface() const
{
	return nullptr;
}

IOnlineTurnBasedPtr FOnlineSubsystemEpic::GetTurnBasedInterface() const
{
	return nullptr;
}

IOnlineTournamentPtr FOnlineSubsystemEpic::GetTournamentInterface() const
{
	return nullptr;
}

bool FOnlineSubsystemEpic::Init()
{
	if (this->IsInit)
	{
		UE_LOG_ONLINE(Warning, TEXT("Tried initializing already initialized subsystem"));
		return false;
	}
	bool hasInvalidParams = false;

	// Get the Developer tool port.
	// If this isn't set, we default to 9999
	if (!GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("DevToolAddress"),
		this->DevToolAddress,
		GEngineIni))
	{
		UE_LOG_ONLINE(Verbose, TEXT("DevToolAddress not set in, defaulting to 127.0.0.1:9999"));
		this->DevToolAddress = TEXT("127.0.0.1:9999");
	}

	// Read project name and version from config files
	FString productId;
	if (!GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("ProductId"),
		productId,
		GEngineIni))
	{
		UE_LOG_ONLINE(Warning, TEXT("Product Id is empty, add your product id from Epic Games DevPortal to the config files."));
		hasInvalidParams = true;
	}

	FString sandboxId;
	if (!GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("SandboxId"),
		sandboxId,
		GEngineIni))
	{
		UE_LOG_ONLINE(Warning, TEXT("Sandbox Id is empty, add your sandbox id from Epic Games DevPortal to the config files."));
		hasInvalidParams = true;
	}

	FString deploymentId;
	if (!GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("DeploymentId"),
		deploymentId,
		GEngineIni))
	{
		UE_LOG_ONLINE(Warning, TEXT("Deployment Id is empty, add your deployment id from Epic Games DevPortal to the config files."));
		hasInvalidParams = true;
	}

	FString clientId;
	GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("ClientCredentialsId"),
		clientId,
		GEngineIni
	);
	FString clientSecret;
	GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("ClientCredentialsSecret"),
		clientSecret,
		GEngineIni
	);
	if (clientId.IsEmpty() || clientSecret.IsEmpty())
	{
		UE_LOG_ONLINE(Warning, TEXT("[EOS SDK] Client credentials are invalid, check clientid and clientsecret!"));
		hasInvalidParams = true;
	}

	FString countryCode;
	char* countryCodeC = nullptr;
	if (GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("CountryCode"),
		countryCode,
		GEngineIni))
	{
		countryCodeC = TCHAR_TO_UTF8(*countryCode);
	}
	else
	{
		countryCodeC = nullptr;
	}

	FString localeCode;
	char* localeCodeC = nullptr;
	if (GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("LocaleCode"),
		localeCode,
		GEngineIni))
	{
		localeCodeC = TCHAR_TO_UTF8(*countryCode);
	}
	else
	{
		localeCodeC = nullptr;
	}

	FString encryptionKey;
	char* encryptionKeyC = nullptr;
	if (GConfig->GetString(
		TEXT("OnlineSubsystemEpic"),
		TEXT("EncryptionKey"),
		encryptionKey,
		GEngineIni))
	{
		if (encryptionKey.Len() != 64)
		{
			UE_LOG_ONLINE(Warning, TEXT("Got encryption key, but its length wasn't 64 characters."));
			hasInvalidParams = true;
		}
		else
		{
			encryptionKeyC = TCHAR_TO_UTF8(*encryptionKey);
		}
	}
	else
	{
		encryptionKeyC = nullptr;
	}
	
	FString cacheDirectory;
	char const* cacheDirectoryC = nullptr;
	if (GConfig->GetString(TEXT("OnlineSubsystemEpic"), TEXT("CacheDirectory"), cacheDirectory, GEngineIni)
		|| cacheDirectory.Len() == 0)
	{
		UE_LOG_ONLINE(Warning, TEXT("Got no cache directory, defaulting to %s"), *cacheDirectory);
		cacheDirectoryC = FUtils::GetTempDirectory();
	}
	else
	{
		cacheDirectoryC = TCHAR_TO_UTF8(*cacheDirectory);
	}

	uint64 platformFlags = 0;
#if UE_EDITOR
	// The platform overlay causes rendering artifacts in the editor,
	// this flag ensures it is not loaded in the editor or PIE
	platformFlags |= EOS_PF_LOADING_IN_EDITOR;
#else
	platformFlags |= 0;
#endif

	bool disableOverlay = false;
	if (GConfig->GetBool(TEXT("OnlineSubsystemEpic"), TEXT("DisableOverlay"), disableOverlay, GEngineIni))
	{
		if (disableOverlay)
		{
			platformFlags |= EOS_PF_DISABLE_OVERLAY;
		}
	}

	bool disableSocialOverlay = false;
	if (GConfig->GetBool(TEXT("OnlineSubsystemEpic"), TEXT("DisableSocialOverlay"), disableOverlay, GEngineIni))
	{
		if (disableSocialOverlay)
		{
			platformFlags |= EOS_PF_DISABLE_SOCIAL_OVERLAY;
		}
	}

	// The tick budget is set in the config as a double, not an unsigned 32-bit integer
	// This was done because UE has no way of retrieving an unsigned type from the config
	// and a 64 bit double has the same codomain as an unsigned 32-bit integer.
	double tickBudgetConfig = 0;
	uint32_t tickBudget = 0;
	if (!GConfig->GetDouble(TEXT("OnlineSubsystemEpic"), TEXT("TickBudget"), tickBudgetConfig, GEngineIni))
	{
		UE_LOG_ONLINE(Verbose, TEXT("No tick budget set, defaulting to 0"));
	}
	else
	{
		// Floor the value to the next value. This is a design choice and there is no right or wrong.
		// The reasoning is, that the user wants to use the tick budget at most and "stealing" more time
		// might result in less time for other tasks in the program.
		double d = FMath::FloorToDouble(tickBudgetConfig);

		// Convert to uint32_t by casting
		tickBudget = static_cast<uint32_t>(d);
	}

	if (hasInvalidParams)
	{
		return false;
	}


	// Create platform instance
	EOS_Platform_ClientCredentials clientCredentials = {
		TCHAR_TO_UTF8(*clientId),
		TCHAR_TO_UTF8(*clientSecret)
	};
	EOS_Platform_Options PlatformOptions = {
		EOS_PLATFORM_OPTIONS_API_LATEST,
		nullptr,					// MUST be nulled
		TCHAR_TO_UTF8(*productId),	// Required
		TCHAR_TO_UTF8(*sandboxId),	// Required
		clientCredentials,			// Required
		this->IsServer(),
		encryptionKeyC,
		countryCodeC,
		localeCodeC,
		TCHAR_TO_UTF8(*deploymentId), // Required
		platformFlags,
		cacheDirectoryC,
		tickBudget
	};
	this->PlatformHandle = EOS_Platform_Create(&PlatformOptions);
	if (!this->PlatformHandle)
	{
		UE_LOG_ONLINE(Warning, TEXT("[EOS SDK] Platform Create Failed!"));
		return false;
	}

	this->IdentityInterface = MakeShareable(new FOnlineIdentityInterfaceEpic(this));
	this->SessionInterface = MakeShareable(new FOnlineSessionEpic(this));
	this->UserInterface = MakeShareable(new FOnlineUserEpic(this));
	this->PresenceInterface = MakeShared<FOnlinePresenceEpic, ESPMode::ThreadSafe>(this);
	this->FriendsInterface = MakeShareable(new FOnlineFriendInterfaceEpic(this));
	
	this->IsInit = true;
	return true;
}

bool FOnlineSubsystemEpic::Shutdown()
{
	this->IsInit = false;
	this->PlatformHandle = nullptr;


#define DESTRUCT_INTERFACE(Interface) \
	if (Interface.IsValid()) \
	{ \
		ensure(Interface.IsUnique()); \
		Interface = nullptr; \
	}

	// Destruct the interfaces
	DESTRUCT_INTERFACE(IdentityInterface);
	DESTRUCT_INTERFACE(SessionInterface);
	DESTRUCT_INTERFACE(UserInterface);
	DESTRUCT_INTERFACE(PresenceInterface);
	DESTRUCT_INTERFACE(FriendsInterface);

#undef DESTRUCT_INTERFACE

	return true;
}

FString FOnlineSubsystemEpic::GetAppId() const
{
	// The AppId is a combination of the Projects id and a version in the form of:
	// {ProductId}::{ProductVersion}
	// This guarantees a unique identifier for a given product and version.
	// They however differ from the SDKs product id!

	FString projectId;
	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("ProjectId"),
		projectId,
		GGameIni
	);

	FString projectVersion;
	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("ProjectVersion"),
		projectVersion,
		GGameIni
	);

	return FString::Printf(TEXT("%s::%s"), *projectId, *projectVersion);
}

FText FOnlineSubsystemEpic::GetOnlineServiceName() const
{
	return NSLOCTEXT("OnlineSubsystemEpic", "OnlineServiceName", "Epic");
}

bool FOnlineSubsystemEpic::Tick(float DeltaTime)
{
	FOnlineSubsystemImpl::Tick(DeltaTime);

	if (this->PlatformHandle)
	{
		EOS_Platform_Tick(this->PlatformHandle);
	}

	if (this->SessionInterface)
	{
		this->SessionInterface->Tick(DeltaTime);
	}

	if (this->UserInterface)
	{
		this->UserInterface->Tick(DeltaTime);
	}

	return true;
}