// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineSubsystemEpicModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "OnlineSubsystemModule.h"
#include "OnlineSubsystemEpic.h"
#include <ThirdParty\OnlineSubsystemEpicLibrary\Include\eos_init.h>
#include <ThirdParty\OnlineSubsystemEpicLibrary\Include\eos_logging.h>


void EOS_CALL EOSSDKLoggingCallback(const EOS_LogMessage* InMsg)
{
	if (InMsg->Level != EOS_ELogLevel::EOS_LOG_Off)
	{
		FString logMsg = FString::Printf(TEXT("[EOS SDK] %s: %s"), ANSI_TO_TCHAR(InMsg->Category), ANSI_TO_TCHAR(InMsg->Message));

		if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Fatal)
		{
			UE_LOG_ONLINE(Fatal, TEXT("%s"), *logMsg);
		}

		if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Error)
		{
			UE_LOG_ONLINE(Error, TEXT("%s"), *logMsg);
		}

		if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Warning)
		{
			UE_LOG_ONLINE(Warning, TEXT("%s"), *logMsg);
		}

		if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Info)
		{
			UE_LOG_ONLINE(Display, TEXT("%s"), *logMsg);
		}

		if (InMsg->Level == EOS_ELogLevel::EOS_LOG_Verbose)
		{
			UE_LOG_ONLINE(Verbose, TEXT("%s"), *logMsg);
		}

		if (InMsg->Level == EOS_ELogLevel::EOS_LOG_VeryVerbose)
		{
			UE_LOG_ONLINE(VeryVerbose, TEXT("%s"), *logMsg);
		}
	}
}

/**
 * Class responsible for creating instance(s) of the subsystem
 */
class FOnlineFactoryEpic : public IOnlineFactory
{
public:

	FOnlineFactoryEpic() = default;
	virtual ~FOnlineFactoryEpic() = default;

	virtual IOnlineSubsystemPtr CreateSubsystem(FName InstanceName)
	{
		FOnlineSubsystemEpicPtr OnlineSub = MakeShared<FOnlineSubsystemEpic, ESPMode::ThreadSafe>(InstanceName);
		if (OnlineSub->IsEnabled())
		{
			if (!OnlineSub->Init())
			{
				UE_LOG_ONLINE(Warning, TEXT("OnlineSubsystemEpic failed to initialize!"));
				OnlineSub->Shutdown();
				OnlineSub = nullptr;
			}
		}
		else
		{
			UE_LOG_ONLINE(Warning, TEXT("OnlineSubsystemEpic is disabled!"));
			OnlineSub->Shutdown();
			OnlineSub = nullptr;
		}

		return OnlineSub;
	}
};


// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
void FOnlineSubsystemEpicModule::StartupModule()
{
	// Get the base directory of this plugin
	FString BaseDir = IPluginManager::Get().FindPlugin("OnlineSubsystemEpic")->GetBaseDir();

	// Add on the relative location of the third party dll and load it
	FString LibraryPath;
#if PLATFORM_WINDOWS
#if PLATFORM_32BITS
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Source/ThirdParty/OnlineSubsystemEpicLibrary/x86/EOSSDK-Win32-Shipping.dll"));
#else
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Source/ThirdParty/OnlineSubsystemEpicLibrary/x64/EOSSDK-Win64-Shipping.dll"));
#endif
#elif PLATFORM_MAC
	LibraryPath = FPaths::Combine(*BaseDir, TEXT("Source/ThirdParty/OnlineSubsystemEpicLibrary/Mac/libEOSSDK-Mac-Shipping.dylib"));
#endif // PLATFORM_WINDOWS

	EpicOnlineServiceSDKLibraryHandle = !LibraryPath.IsEmpty() ? FPlatformProcess::GetDllHandle(*LibraryPath) : nullptr;
	checkf(EpicOnlineServiceSDKLibraryHandle, TEXT("Failed to load Epic online service library"));

	OnlineFactory = new FOnlineFactoryEpic();

	FOnlineSubsystemModule& subsystem = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	subsystem.RegisterPlatformService(EPIC_SUBSYSTEM, OnlineFactory);

	// --------------------------------------------------
	// Initialize the EOS SDK. This must only happen once!

	//Read project name and version from config files
	FString projectName;
	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("ProjectName"),
		projectName,
		GGameIni
	);
	FString projectVersion;
	GConfig->GetString(
		TEXT("/Script/EngineSettings.GeneralProjectSettings"),
		TEXT("ProjectVersion"),
		projectVersion,
		GGameIni
	);

	EOS_InitializeOptions initOpts;
	initOpts.ApiVersion = EOS_INITIALIZE_API_LATEST;
	initOpts.AllocateMemoryFunction = nullptr;
	initOpts.ReallocateMemoryFunction = nullptr;
	initOpts.ReleaseMemoryFunction = nullptr;
	initOpts.ProductName = TCHAR_TO_ANSI(*projectName);
	initOpts.ProductVersion = TCHAR_TO_ANSI(*projectVersion);
	initOpts.Reserved = nullptr;
	initOpts.SystemInitializeOptions = nullptr;

	// Initialize the SDK and only proceed if the init was successful
	EOS_EResult initResult = EOS_Initialize(&initOpts);
	checkf(initResult == EOS_EResult::EOS_Success, TEXT("Failed to initialize the EpicOnlineService SDK. Error: %s"), *ANSI_TO_TCHAR(EOS_EResult_ToString(initResult)));

	// Register logging
	UE_LOG_ONLINE(Display, TEXT("[EOS SDK] Initialized. Setting Logging Callback ..."));
	EOS_EResult SetLogCallbackResult = EOS_Logging_SetCallback(&EOSSDKLoggingCallback);
	if (SetLogCallbackResult != EOS_EResult::EOS_Success)
	{
		UE_LOG_ONLINE(Warning, TEXT("[EOS SDK] Set Logging Callback Failed!"));
	}
	else
	{
		UE_LOG_ONLINE(Display, TEXT("[EOS SDK] Logging Callback Set"));
		EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_Verbose);
	}

}


// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading, we call this function before unloading the module.
void FOnlineSubsystemEpicModule::ShutdownModule()
{
	FOnlineSubsystemModule& subsystem = FModuleManager::GetModuleChecked<FOnlineSubsystemModule>("OnlineSubsystem");
	subsystem.UnregisterPlatformService(EPIC_SUBSYSTEM);

	delete OnlineFactory;
	OnlineFactory = nullptr;

	// Free the dll handle
	FPlatformProcess::FreeDllHandle(EpicOnlineServiceSDKLibraryHandle);
	EpicOnlineServiceSDKLibraryHandle = nullptr;
}

IMPLEMENT_MODULE(FOnlineSubsystemEpicModule, OnlineSubsystemEpic)