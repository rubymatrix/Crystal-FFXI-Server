/************************************************************************
 * Account Ban
 *
 * Backs modules/phoenix/lua/commands/account_ban.lua: !banaccount and
 * !unbanaccount. A ban is on the PlayOnline account (chars.pol_id) and lives
 * in pol_bans; the world's lobby API refuses to create or admit a character
 * of a banned account.
 *
 * Lua:
 * - BanAccount(charName, days, reason, bannedBy) - bans charName's account, 0 days until lifted; false if
 *                                                  there is no live character of that name with a POL ID
 * - UnbanAccount(charName)                       - lifts the ban on charName's account; false if there was none
 ************************************************************************/

#include "common/database.h"
#include "common/lua.h"
#include "common/vana_time.h"
#include "map/utils/moduleutils.h"

namespace
{

constexpr uint32 kSecondsPerDay = 24 * 60 * 60;

auto banAccount(const std::string& charName, const uint32 days, const std::string& reason, const std::string& bannedBy) -> bool
{
    const auto rset = db::preparedStmt("SELECT pol_id FROM chars WHERE charname = ? AND accid <> 0 AND pol_id <> 0 LIMIT 1", charName);
    if (!rset || !rset->next())
    {
        return false;
    }

    const auto polId = rset->get<uint64>("pol_id");
    const auto until = days == 0 ? 0 : earth_time::timestamp() + days * kSecondsPerDay;
    const auto saved = db::preparedStmt("INSERT INTO pol_bans (pol_id, banned_until, reason, banned_by) VALUES (?, ?, ?, ?) "
                                        "ON DUPLICATE KEY UPDATE banned_until = ?, reason = ?, banned_by = ?, created = CURRENT_TIMESTAMP",
                                        polId,
                                        until,
                                        reason.substr(0, 128),
                                        bannedBy,
                                        until,
                                        reason.substr(0, 128),
                                        bannedBy);
    if (!saved)
    {
        return false;
    }

    ShowInfoFmt("account_ban: {} banned POL ID {} (via {}) {}: {}", bannedBy, polId, charName, days == 0 ? "until lifted" : fmt::format("for {} days", days), reason);
    return true;
}

auto unbanAccount(const std::string& charName) -> bool
{
    const auto rset = db::preparedStmt("DELETE b FROM pol_bans b "
                                       "INNER JOIN chars c ON c.pol_id = b.pol_id "
                                       "WHERE c.charname = ? AND c.accid <> 0",
                                       charName);
    return rset && rset->rowsAffected() > 0;
}

} // namespace

class AccountBanModule : public CPPModule
{
    void OnInit() override
    {
        TracyZoneScoped;

        ::lua.set_function("BanAccount", banAccount);
        ::lua.set_function("UnbanAccount", unbanAccount);
    }
};

REGISTER_CPP_MODULE(AccountBanModule);
