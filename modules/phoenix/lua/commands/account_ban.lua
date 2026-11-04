-----------------------------------
-- func: banaccount <player> (days) (reason)
-- desc: Bans the player's PlayOnline account from this world and logs out its characters here.
--       No days (or 0): until !unbanaccount.
-----------------------------------
-- func: unbanaccount <player>
-- desc: Lifts the ban on the player's PlayOnline account.
-----------------------------------
-- The ban is checked by the lobby API when a character is created or enters the world, so it holds
-- across logins. Characters online on another zone group (cluster) are not logged out from here.
-----------------------------------
-- luacheck: globals BanAccount UnbanAccount GetAccountCharacters
-----------------------------------

local function error(player, msg, usage)
    player:printToPlayer(msg)
    player:printToPlayer(usage)
end

---@type TCommand
local banAccount = {}

banAccount.cmdprops =
{
    permission = 3,
    parameters = 'sis'
}

banAccount.onTrigger = function(player, target, days, reason)
    local usage = '!banaccount <player> (days) (reason)'
    if target == nil then
        error(player, 'You must name a player.', usage)
        return
    end

    days = days or 0
    if days < 0 then
        error(player, 'Days cannot be negative.', usage)
        return
    end

    if not BanAccount(target, days, reason or '', player:getName()) then
        error(player, string.format('No live character \'%s\' with a PlayOnline account.', target), usage)
        return
    end

    local loggedOut = {}
    for _, character in ipairs(GetAccountCharacters(target)) do
        local online = GetPlayerByName(character.name)
        if online ~= nil then
            table.insert(loggedOut, character.name)
            online:leaveGame()
        end
    end

    local duration = days == 0 and 'until lifted' or string.format('for %i days', days)
    player:printToPlayer(string.format('Banned %s\'s PlayOnline account %s.', target, duration))
    if #loggedOut > 0 then
        player:printToPlayer(string.format('Logged out: %s', table.concat(loggedOut, ', ')))
    end
end

---@type TCommand
local unbanAccount = {}

unbanAccount.cmdprops =
{
    permission = 3,
    parameters = 's'
}

unbanAccount.onTrigger = function(player, target)
    local usage = '!unbanaccount <player>'
    if target == nil then
        error(player, 'You must name a player.', usage)
        return
    end

    if not UnbanAccount(target) then
        error(player, string.format('%s\'s PlayOnline account is not banned.', target), usage)
        return
    end

    player:printToPlayer(string.format('Lifted the ban on %s\'s PlayOnline account.', target))
end

xi.module.registerCommand('banaccount', banAccount)
xi.module.registerCommand('unbanaccount', unbanAccount)
