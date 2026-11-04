/************************************************************************
 * Account Variables
 *
 * Account-wide variables stored in account_vars, shared by every character
 * on the PlayOnline account (CCharEntity::polId, chars.pol_id).
 * Other Phoenix modules read and write them through here.
 ************************************************************************/

#pragma once

#include "common/cbasetypes.h"

#include <string>

namespace accountvars
{

// An expired variable reads as absent and is removed on the way out
auto fetchAccountVar(uint64 accountId, const std::string& varname) -> int32;

// A value of zero deletes the variable; an expiry already in the past is refused
void persistAccountVar(uint64 accountId, const std::string& varname, int32 value, uint32 expiry);

} // namespace accountvars
