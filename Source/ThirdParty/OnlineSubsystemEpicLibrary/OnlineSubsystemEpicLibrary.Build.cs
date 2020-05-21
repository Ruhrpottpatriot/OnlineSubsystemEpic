// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class OnlineSubsystemEpicLibrary : ModuleRules
{
    public OnlineSubsystemEpicLibrary(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
                
        // Add header files to include path
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));

        // Handle libraries
        if(Target.Platform == UnrealTargetPlatform.Win32)
        {
            // Add the import library
            PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "EOSSDK-Win32-Shipping.lib"));

            // Delay-load the DLL, so we can load it from the right place first
            PublicDelayLoadDLLs.Add("EOSSDK-Win32-Shipping.dll");

            // Ensure that the DLL is staged along with the executable
            var s = "$(PluginDir)/Binaries/ThirdParty/OnlineSubsystemEpicLibrary/Bin/EOSSDK-Win32-Shipping.dll";
            RuntimeDependencies.Add(s);
        }
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "Lib", "EOSSDK-Win64-Shipping.lib"));
            PublicDelayLoadDLLs.Add("EOSSDK-Win64-Shipping.dll");
            RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/OnlineSubsystemEpicLibrary/Bin/EOSSDK-Win64-Shipping.dll");
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
            PublicDelayLoadDLLs.Add(Path.Combine(ModuleDirectory, "Bin", "libEOSSDK-Linux-Shipping.so"));
            RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/OnlineSubsystemEpicLibrary/Bin/libEOSSDK-Linux-Shipping.so");

        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicDelayLoadDLLs.Add(Path.Combine(ModuleDirectory, "Bin", "libEOSSDK-Mac-Shipping.dylib"));
            RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/OnlineSubsystemEpicLibrary/Bin/libEOSSDK-Mac-Shipping.dylib");
        }
    }
}
