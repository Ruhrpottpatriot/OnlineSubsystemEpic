#pragma once

#include "CoreMinimal.h"
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
	static EOS_EpicAccountId EpicAccountIDFromString(const FString AccountString);
};

class FUtils
{
public:
	/**
	 * Returns full path to system temporary directory
	 */
	static const char* GetTempDirectory();
};
