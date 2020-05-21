// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class OnlineSubsystemEpicLibrary : ModuleRules
{
    public OnlineSubsystemEpicLibrary(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Include"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Add the import library
            PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "x64", "EOSSDK-Win64-Shipping.lib"));

            // Delay-load the DLL, so we can load it from the right place first
            PublicDelayLoadDLLs.Add("EOSSDK-Win64-Shipping.dll");

            // Ensure that the DLL is staged along with the executable
            var s = "$(PluginDir)/Binaries/ThirdParty/OnlineSubsystemEpicLibrary/x64/EOSSDK-Win64-Shipping.dll";
            RuntimeDependencies.Add(s);
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicDelayLoadDLLs.Add(Path.Combine(ModuleDirectory, "Mac", "libEOSSDK-Mac-Shipping.dylib"));
            RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/OnlineSubsystemEpicLibrary/Mac/libEOSSDK-Mac-Shipping.dylib");
        }
    }
}
