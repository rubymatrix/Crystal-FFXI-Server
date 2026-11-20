-----------------------------------
-- Zoning at the end of a cutscene is not a death
-----------------------------------

describe('Zoning at the end of a cutscene', function()
    ---@type CClientEntityPair
    local player

    before_each(function()
        -- Make sure a death in these tests would cost EXP, so the EXP check can't pass by accident
        xi.test.world:setSetting('map.EXP_RETAIN', 0)
        xi.test.world:setSetting('map.EXP_LOSS_RATE', 1)
        xi.test.world:setSetting('map.EXP_LOSS_LEVEL', 5)

        player = xi.test.world:spawnPlayer({ zone = xi.zone.NORTHERN_SAN_DORIA, level = 50 })
        player:setNation(xi.nation.SANDORIA)
        player:setRank(3)
        player:setCharVar('expLost', 0)
    end)

    it('does not show the player as dead while they zone into the Chateau', function()
        -- The gate cutscene moves the player into Chateau d'Oraguille when it ends
        player:startEvent(569)
        player.events:finish(569)

        assert(player:isPendingZone(), 'precondition: finishing the gate cutscene should start a zone change')
        player.assert.no:hasAnimation(xi.animation.DEATH)
    end)

    it('does not cost EXP to zone into the Chateau', function()
        player:startEvent(569)
        player.events:expect({ eventId = 569 })
        xi.test.world:tick()

        assert(player:getZoneID() == xi.zone.CHATEAU_DORAGUILLE, 'the player should have zoned into the Chateau')
        assert(player:getHP() > 0, 'the player should arrive with HP')
        player.assert.no:hasAnimation(xi.animation.DEATH)
        assert(player:getCharVar('expLost') == 0, 'zoning should not cost EXP')
    end)
end)
