#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include <eos_sdk.h>


class FIdentityUtilities
{
public:
	FIdentityUtilities() noexcept(false);
	/**
	* No copying or copy assignment allowed for this class.
	*/
	FIdentityUtilities(FIdentityUtilities const&) = delete;
	FIdentityUtilities& operator=(FIdentityUtilities const&) = delete;

	/**
	* Destructor
	*/
	virtual ~FIdentityUtilities();

	/**
	* Utility to convert account id to a string
	*
	* @param InAccountId - Account id to convert
	*
	* @return String representing account id. Returns string representation of error in case of bad account id.
	*/
	static FString EpicAccountIDToString(EOS_EpicAccountId InAccountId);
	static FString ProductUserIDToString(EOS_ProductUserId InAccountId);

	/**
	* Utility to build epic account id from string
	*/
	static EOS_EpicAccountId EpicAccountIDFromString(FString const AccountString);
	static EOS_ProductUserId ProductUserIDFromString(FString const AccountString);
};

class FUtils
{
public:
	/**
	 * Returns full path to system temporary directory
	 */
	static const char* GetTempDirectory();
};

template<typename TEnum>
static FORCEINLINE FString GetEnumValueAsString(const FString& Name, TEnum Value) {
	const UEnum* enumPtr = FindObject<UEnum>(ANY_PACKAGE, *Name, true);
	if (!enumPtr) return FString("Invalid");
	return enumPtr->GetNameByValue((int64)Value).ToString();
}

template <typename EnumType>
static FORCEINLINE EnumType GetEnumValueFromString(const FString& EnumName, const FString& String) {
	UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, *EnumName, true);
	if (!Enum) {
		return EnumType(0);
	}
	return (EnumType)Enum->GetIndexByName(FName(*String));
}