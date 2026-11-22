-----------------------------------
-- func: talktest
-- desc: PS2 soft-lock test: the datagram carrying your next Talk to an NPC is ignored, as if lost on the way.
-----------------------------------
---@type TCommand
local commandObj = {}

commandObj.cmdprops =
{
    permission = 1,
    parameters = 'i'
}

commandObj.onTrigger = function(player, on)
    if on == nil then
        on = player:getLocalVar('TALK_TEST_NO_REPLY') == 0 and 1 or 0
    end

    player:setLocalVar('TALK_TEST_NO_REPLY', on ~= 0 and 1 or 0)
    if on ~= 0 then
        player:printToPlayer('Talk test on: your next Talk is lost on the way to the server.')
    else
        player:printToPlayer('Talk test off.')
    end
end

return commandObj
