-----------------------------------
-- func: release
-- desc: Releases the player from current events. Anyone can release themselves; naming another player needs GM level 1.
-----------------------------------
---@type TCommand
local commandObj = {}

commandObj.cmdprops =
{
    permission = 0,
    parameters = 's'
}

local function error(player, msg)
    player:printToPlayer(msg)
    player:printToPlayer('!release (name)')
end

commandObj.onTrigger = function(player, name)
    local target
    if name == nil then
        target = player
    else
        if player:getGMLevel() < 1 then
            player:printToPlayer('You can only release yourself: !release')
            return
        end

        target = GetPlayerByName(name)
        if target == nil then
            error(player, string.format('Player named "%s" not found!', name))
            return
        end
    end

    target:release()
end

return commandObj
