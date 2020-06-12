#pragma once

#include "CoreMinimal.h"
#include "UObject/CoreOnline.h"
#include "eos_sdk.h"

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