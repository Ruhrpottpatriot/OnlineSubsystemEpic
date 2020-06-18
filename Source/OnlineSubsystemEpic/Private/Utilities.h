#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "eos_sdk.h"

namespace FUtils
{
	/**
	 * Returns full path to system temporary directory
	 */
	const char* GetTempDirectory();

	template<typename TEnum>
	FORCEINLINE FString GetEnumValueAsString(const FString& Name, TEnum Value) {
		const UEnum* enumPtr = FindObject<UEnum>(ANY_PACKAGE, *Name, true);
		if (!enumPtr) return FString("Invalid");
		return enumPtr->GetNameByValue((int64)Value).ToString();
	}

	template <typename EnumType>
	FORCEINLINE EnumType GetEnumValueFromString(const FString& EnumName, const FString& String) {
		UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, *EnumName, true);
		if (!Enum) {
			return EnumType(0);
		}
		return (EnumType)Enum->GetIndexByName(FName(*String));
	}

	/** 
	  * Converts a string into an EOS_EExternalCredentialType, case insensitive.
	  * @returns - The external credentials type and true if the conversion was successful.
	  */
	TPair<EOS_EExternalCredentialType, bool> ExternalCredentialsTypeFromString(FString const& InputString)
	{
		if (InputString.Equals(TEXT("steam"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_STEAM_APP_TICKET, true);
		}
		else if (InputString.Equals(TEXT("psn"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_PSN_ID_TOKEN, true);
		}
		else if (InputString.Equals(TEXT("xbl"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_XBL_XSTS_TOKEN, true);
		}
		else if (InputString.Equals(TEXT("discord"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_DISCORD_ACCESS_TOKEN, true);
		}
		else if (InputString.Equals(TEXT("gog"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_GOG_SESSION_TICKET, true);
		}
		else if (InputString.Equals(TEXT("nintendo_id"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_NINTENDO_ID_TOKEN, true);
		}
		else if (InputString.Equals(TEXT("nintendo_nsa"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_NINTENDO_NSA_ID_TOKEN, true);
		}
		else if (InputString.Equals(TEXT("uplay"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_UPLAY_ACCESS_TOKEN, true);
		}
		else if (InputString.Equals(TEXT("openid"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_OPENID_ACCESS_TOKEN, true);
		}
		else if (InputString.Equals(TEXT("device"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN, true);
		}
		else if (InputString.Equals(TEXT("apple"), ESearchCase::IgnoreCase))
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_APPLE_ID_TOKEN, true);
		}
		else
		{
			return MakeTuple(EOS_EExternalCredentialType::EOS_ECT_EPIC, false);
		}
	}

	/**
	 * Converts an EOS_EExternalAccountType into a lower case string.
	 * @returns - The string representing the external account enum
	 */
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

		// We covered all enum cases.
		// If we're here we should crash and burn
		checkNoEntry();
		return TEXT("unknown");
	}
};