# OnlineSubsystemEpic
An unofficial integration of the Epic Online Systems SDK with Unreal Engine 4's OnlineSubsystem

## Installation
1. Clone the repository into your Pojects _Plugins_ directory.
2. Download the EOS C++ SDK from the Developer Portal
3. Drop the _Bin_, _Include_ and _Lib_ from inside the SDK folder into _Source\ThirdParty\OnlineSubsystemEpicLibrary_

## Usage
This plugin is used like any other OnlineSubsystem Plugin already existing. This means, that most of the time you won't need to directly interface with the system directly, but can let the engine classes handle the calls.
If you need to directly access the OnlineSubsystem you should get the static pointer to it via `IOnlineSubsystem::Get()` and go on from there.

## Limitations
Currently only the `IOnlineIdentity` interface is implemented and inside said interface only _Developer_ login is working. MFA and PinGrantCode are also not supported currently.
If you want more feature you have to wait, or submit a pull request ;)