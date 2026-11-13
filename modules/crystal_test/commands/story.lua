-----------------------------------
-- func: story
-- desc: Steps a character through the nation, Rise of the Zilart, Chains of Promathia and Treasures of
--       Aht Urhgan storylines for testing. It reads the mission scripts' own sections to find what
--       advances the current mission, gives the items and key items that step needs, and teleports
--       the character to the NPC, mob or battlefield entrance. Fights are not won for you.
--
--   !story                      this help and the storyline currently selected
--   !story lines                every storyline with its current mission
--   !story start <line> [mission]  select a storyline; with a mission (1-1, 2_3, 05) jump to it
--   !story show [line]          list what advances the current mission, without changing anything
--   !story next [line] [n]      give what step n needs (default 1) and go there
--
-- Lines: sandoria, bastok, windurst (or nation), rotz, cop, toau.
-----------------------------------
require('modules/module_utils')
require('scripts/globals/interaction/interaction_util')
-----------------------------------
---@type TCommand
local commandObj = {}

commandObj.cmdprops =
{
    permission = 1,
    parameters = 'sss',
}

local kLineVar   = 'CrystalTest_StoryLine'
local kBackslash = string.char(92)
local kMaxShown  = 6

-- Storylines up to Treasures of Aht Urhgan. dir is the folder under scripts/missions.
-- clang-format off
local kLines =
{
    sandoria = { log = xi.mission.log_id.SANDORIA, dir = 'sandoria', area = 'sandoria', nation = xi.nation.SANDORIA, title = "San d'Oria"              },
    bastok   = { log = xi.mission.log_id.BASTOK,   dir = 'bastok',   area = 'bastok',   nation = xi.nation.BASTOK,   title = 'Bastok'                  },
    windurst = { log = xi.mission.log_id.WINDURST, dir = 'windurst', area = 'windurst', nation = xi.nation.WINDURST, title = 'Windurst'                },
    rotz     = { log = xi.mission.log_id.ZILART,   dir = 'rotz',     area = 'zilart',                                title = 'Rise of the Zilart'      },
    cop      = { log = xi.mission.log_id.COP,      dir = 'cop',      area = 'cop',                                   title = 'Chains of Promathia'     },
    toau     = { log = xi.mission.log_id.TOAU,     dir = 'toau',     area = 'toau',                                  title = 'Treasures of Aht Urhgan' },
}
-- clang-format on

local kLineOrder = { 'sandoria', 'bastok', 'windurst', 'rotz', 'cop', 'toau' }

-- Zone-wide keys in a mission section. Every other key is an entity name.
local kZoneHandlers =
{
    onZoneIn           = true,
    onZoneOut          = true,
    afterZoneIn        = true,
    onEventFinish      = true,
    onEventUpdate      = true,
    onTriggerAreaEnter = true,
    onTriggerAreaLeave = true,
}
local kNationLines = { [xi.nation.SANDORIA] = 'sandoria', [xi.nation.BASTOK] = 'bastok', [xi.nation.WINDURST] = 'windurst' }

-- How much a candidate step is likely to advance the mission. Higher goes first.
local kScore =
{
    zoneIn = 960,
    trade  = 950,
    talk   = 900, -- a function handler that starts an event
    fight  = 850,
    kill   = 800,
    area   = 700,
}

-----------------------------------
-- Output
-----------------------------------

local function say(player, fmt, ...)
    player:printToPlayer(string.format(fmt, ...), xi.msg.channel.SYSTEM_3)
end

local function reverseLookup(tbl)
    local out = {}
    for name, value in pairs(tbl or {}) do
        if type(value) == 'number' then
            out[value] = name
        end
    end

    return out
end

local itemNames    = nil
local keyItemNames = nil

local function itemName(id)
    itemNames = itemNames or reverseLookup(xi.item)
    return itemNames[id] or tostring(id)
end

local function keyItemName(id)
    keyItemNames = keyItemNames or reverseLookup(xi.keyItem)
    return keyItemNames[id] or tostring(id)
end

local function zoneName(zoneId)
    local zone = GetZone(zoneId)
    return zone and zone:getName() or tostring(zoneId)
end

-----------------------------------
-- Storyline data, read from the mission scripts
-----------------------------------

local lineCache = {}

-- Sort labels like 1_1, 2_10 and 05 in story order.
local function labelKey(label)
    local major, minor = label:match('^(%d+)_(%d+)$')
    if major then
        return tonumber(major) * 1000 + tonumber(minor)
    end

    return (tonumber(label) or 0) * 1000
end

-- The mission containers of a storyline, in story order. Each entry is { label, container }.
local function getMissions(lineId)
    if lineCache[lineId] then
        return lineCache[lineId]
    end

    local line     = kLines[lineId]
    local prefix   = 'scripts/missions/' .. line.dir .. '/'
    local missions = {}
    -- A C++ list: index it, ipairs can't walk it
    local paths = GetContainerFilenamesList()
    for i = 1, #paths do
        -- Windows builds use backslashes. Match on a copy, but require the original path:
        -- it is the key the interaction system loaded the container under.
        local path       = paths[i]
        local normalised = path:gsub(kBackslash, '/')
        if normalised:sub(1, #prefix) == prefix then
            local base  = normalised:sub(#prefix + 1):gsub('%.lua$', '')
            local label = base:match('^(%d+_%d+)_') or base:match('^(%d+)_')
            -- require returns the same container the interaction system loaded
            local container = utils.prequire(path)
            if
                label and
                type(container) == 'table' and
                container.missionId and
                container.sections
            then
                table.insert(missions, { label = label, container = container })
            end
        end
    end

    table.sort(missions, function(a, b)
        return labelKey(a.label) < labelKey(b.label)
    end)

    lineCache[lineId] = missions
    return missions
end

local function missionLabel(lineId, missionId)
    for _, entry in ipairs(getMissions(lineId)) do
        if entry.container.missionId == missionId then
            return entry.label
        end
    end
end

local missionNameCache = {}

local function missionName(lineId, missionId)
    local area = kLines[lineId].area
    missionNameCache[area] = missionNameCache[area] or reverseLookup(xi.mission.id[area])
    local name = missionNameCache[area][missionId] or tostring(missionId)
    local label = missionLabel(lineId, missionId)
    return label and string.format('%s %s', label, name) or name
end

local function resolveLine(player, arg)
    if arg == nil or arg == '' then
        local saved = player:getCharVar(kLineVar)
        if saved > 0 then
            return kLineOrder[saved]
        end

        return kNationLines[player:getNation()]
    end

    arg = string.lower(arg)
    if arg == 'nation' then
        return kNationLines[player:getNation()]
    elseif arg == 'zilart' then
        return 'rotz'
    end

    return kLines[arg] and arg or nil
end

local function rememberLine(player, lineId)
    for i, id in ipairs(kLineOrder) do
        if id == lineId then
            player:setCharVar(kLineVar, i)
        end
    end
end

-----------------------------------
-- Probing: run the mission scripts' checks and handlers against a stand-in for the player
-----------------------------------

-- Only read-only methods reach the real character during a dry run.
local function isReadOnly(name)
    return name:match('^get') or name:match('^has') or name:match('^is') or name:match('^can') or name:match('^check')
end

-- A stand-in for the player. It records key items and items the scripts ask about and lacks.
-- opts.assumeHeld answers yes for every key item and item, to find what a section is waiting for.
-- opts.dryRun turns every method that would change the character into a no-op.
local function makeProbe(player, opts)
    local probe = { missingKeyItems = {}, missingItems = {}, events = {} }
    local proxy = {}

    setmetatable(proxy, {
        __index = function(_, name)
            local method = player[name]
            if type(method) ~= 'function' then
                return method
            end

            return function(_, ...)
                local first = select(1, ...)
                if name == 'hasKeyItem' then
                    local has = player:hasKeyItem(...)
                    if not has and first then
                        probe.missingKeyItems[first] = true
                        if opts.assumeHeld then
                            return true
                        end
                    end

                    return has
                elseif name == 'hasItem' then
                    local has = player:hasItem(...)
                    if not has and first then
                        probe.missingItems[first] = true
                        if opts.assumeHeld then
                            return true
                        end
                    end

                    return has
                end

                if opts.dryRun and not isReadOnly(name) then
                    if name == 'startEvent' or name == 'startCutscene' or name == 'startOptionalCutscene' then
                        table.insert(probe.events, first)
                    end

                    return nil
                end

                return method(player, ...)
            end
        end,
    })

    return proxy, probe
end

-- Adds the items a trade check asks for to wanted ({ [itemId] = qty, gil = amount }).
local function recordTradeItems(wanted, items)
    local function add(id, qty)
        if id == 'gil' then
            wanted.gil = (wanted.gil or 0) + qty
        elseif type(id) == 'number' then
            wanted[id] = (wanted[id] or 0) + qty
        end
    end

    if type(items) == 'number' then
        add(items, 1)
    elseif type(items) == 'table' then
        for _, v in pairs(items) do
            if type(v) == 'number' then
                add(v, 1)
            elseif type(v) == 'table' and #v == 2 then
                add(v[1], v[2])
            end
        end
    end
end

-- Runs fn with the npcUtil trade checks recording what they look for and always failing.
local function withTradeRecorder(fn)
    local saved  = { npcUtil.tradeHas, npcUtil.tradeHasExactly, npcUtil.tradeMatches }
    local wanted = {}

    npcUtil.tradeHas = function(trade, items)
        recordTradeItems(wanted, items)
        return false
    end

    npcUtil.tradeHasExactly = npcUtil.tradeHas
    npcUtil.tradeMatches    = npcUtil.tradeHas

    local ok, result = pcall(fn)
    npcUtil.tradeHas, npcUtil.tradeHasExactly, npcUtil.tradeMatches = saved[1], saved[2], saved[3]

    return wanted, ok, result
end

-- An empty trade: no slots, no gil.
local emptyTrade = setmetatable({}, {
    __index = function()
        return function()
            return 0
        end
    end,
})

local function getNpcByName(zoneId, name)
    local zone = GetZone(zoneId)
    if not zone then
        return nil
    end

    for _, entity in pairs(zone:queryEntitiesByName(name) or {}) do
        if entity:getXPos() ~= 0 or entity:getYPos() ~= 0 or entity:getZPos() ~= 0 then
            return entity
        end
    end
end

local function actionPriority(action)
    if type(action) == 'table' then
        return action.priority or Action.Priority.Default
    end

    return 0
end

-- A stand-in for an NPC that cannot change it (no doors opened, no animations), or nil.
local function probeNpc(npc)
    if npc == nil then
        return nil
    end

    return (makeProbe(npc, { dryRun = true }))
end

local kNumber = '%s*,%s*([%-%d%.]+)'
local kCuboid = 'registerCuboidTriggerArea%(%s*(%d+)' .. string.rep(kNumber, 6)
local kCylinder = 'registerCylindricalTriggerArea%(%s*(%d+)' .. string.rep(kNumber, 3)

-- A walkable height at x, z for a cylindrical trigger area, which has none of its own.
local function findGround(zone, x, z)
    for y = -150, 150, 2 do
        if zone:isNavigablePoint({ x = x, y = y, z = z }) then
            return y
        end
    end
end

local areaCache = {} -- zoneId -> { [areaId] = { x, y, z } }

-- Trigger area positions, read from the registrations in the zone's Zone.lua (read only, nothing runs).
local function getTriggerAreas(zoneId)
    if areaCache[zoneId] then
        return areaCache[zoneId]
    end

    local areas = {}
    areaCache[zoneId] = areas
    local zone = GetZone(zoneId)
    local file = zone and io.open(string.format('scripts/zones/%s/Zone.lua', zone:getName()), 'r')
    if not file then
        return areas
    end

    local source = file:read('*a')
    file:close()

    for id, x1, y1, z1, x2, y2, z2 in source:gmatch(kCuboid) do
        areas[tonumber(id)] =
        {
            x = (tonumber(x1) + tonumber(x2)) / 2,
            y = (tonumber(y1) + tonumber(y2)) / 2,
            z = (tonumber(z1) + tonumber(z2)) / 2,
        }
    end

    for id, x, z in source:gmatch(kCylinder) do
        local px, pz = tonumber(x), tonumber(z)
        areas[tonumber(id)] = { x = px, y = findGround(zone, px, pz), z = pz }
    end

    return areas
end

local sourceCache = {} -- reset by analyse(); zoneId -> { [csid] = { step, ... } }

-- What starts each event in a zone when it is not a mission script: an NPC's own onTrigger (gate guards'
-- mission menus) or the zone's onTriggerAreaEnter (the Dangruf Wadi geysers). Found by dry-running them.
-- The server keeps loaded scripts in xi.zones[<zone name>].npcs[<npc name>] and xi.zones[<zone name>].Zone.
local function getEventSources(player, zoneId)
    if sourceCache[zoneId] then
        return sourceCache[zoneId]
    end

    local sources = {}
    sourceCache[zoneId] = sources
    local zone    = GetZone(zoneId)
    local scripts = zone and xi.zones[zone:getName()]
    if type(scripts) ~= 'table' then
        return sources
    end

    local function record(events, step)
        for _, csid in ipairs(events) do
            sources[csid] = sources[csid] or {}
            table.insert(sources[csid], step)
        end
    end

    for npcName, script in pairs(scripts.npcs or {}) do
        if type(script) == 'table' and type(script.onTrigger) == 'function' then
            local proxy, probe = makeProbe(player, { dryRun = true })
            pcall(script.onTrigger, proxy, probeNpc(getNpcByName(zoneId, npcName)))
            record(probe.events, { kind = 'talk', name = npcName })
        end
    end

    local zoneScript = scripts.Zone
    if
        type(zoneScript) == 'table' and
        type(zoneScript.onTriggerAreaEnter) == 'function'
    then
        for areaId, pos in pairs(getTriggerAreas(zoneId)) do
            local triggerArea =
            {
                getTriggerAreaID = function()
                    return areaId
                end,
            }
            local proxy, probe = makeProbe(player, { dryRun = true })
            pcall(zoneScript.onTriggerAreaEnter, proxy, triggerArea)
            record(probe.events, { kind = 'area', area = areaId, pos = pos })
        end
    end

    return sources
end

-- The events a storyline's missions react to when they finish in a zone. A mission split over several files
-- (Bastok 2-3) can react in a sibling file, so every mission of the line counts.
local finishedCache = {} -- lineId:zoneId -> { [csid] = true }

local function getFinishedEvents(lineId, zoneId)
    local cacheKey = string.format('%s:%d', lineId, zoneId)
    if finishedCache[cacheKey] then
        return finishedCache[cacheKey]
    end

    local events = {}
    finishedCache[cacheKey] = events
    for _, entry in ipairs(getMissions(lineId)) do
        for _, section in ipairs(entry.container.sections) do
            local zoneDef = section[zoneId]
            if
                type(zoneDef) == 'table' and
                type(zoneDef.onEventFinish) == 'table'
            then
                for csid, _ in pairs(zoneDef.onEventFinish) do
                    events[csid] = true
                end
            end
        end
    end

    return events
end

-- How much talking to an NPC advances things. A progress event nothing reacts to when it finishes
-- (Baraka's 539 during Bastok 2-3) changes nothing, so it ranks as an ordinary event.
local function talkScore(score, eventId, finished)
    if eventId and not finished[eventId] and score > Action.Priority.Event then
        return Action.Priority.Event
    end

    return score
end

-- Turns one section of a passing check into candidate steps.
local function addSectionCandidates(player, candidates, lineId, entry, section, needs)
    local function add(candidate)
        candidate.mission = entry.container.missionId
        candidate.needs   = needs
        table.insert(candidates, candidate)
    end

    for zoneId, zoneDef in pairs(section) do
        if type(zoneId) == 'number' and type(zoneDef) == 'table' then
            for key, def in pairs(zoneDef) do
                if key == 'onZoneIn' then
                    local fns = type(def) == 'function' and { def } or def
                    -- Only a step if arriving plays a cutscene now. If the dry run fails, it might.
                    local best = 0
                    for _, fn in pairs(fns) do
                        if type(fn) == 'function' then
                            local proxy = makeProbe(player, { dryRun = true })
                            local ok, csid = pcall(fn, proxy, player:getZoneID())
                            if ok and type(csid) == 'number' and csid >= 0 then
                                best = kScore.zoneIn
                            elseif not ok and best == 0 then
                                best = Action.Priority.Event
                            end
                        end
                    end

                    if best > 0 then
                        add({ kind = 'zoneIn', zone = zoneId, score = best })
                    end
                elseif key == 'onTriggerAreaEnter' and type(def) == 'table' then
                    for areaId, _ in pairs(def) do
                        local pos = getTriggerAreas(zoneId)[areaId]
                        add({ kind = 'area', zone = zoneId, area = areaId, pos = pos, score = kScore.area })
                    end
                elseif type(key) == 'string' and not kZoneHandlers[key] then
                    -- An entity name. A bare function or action is its onTrigger.
                    local handlers = def
                    if type(def) == 'function' or (type(def) == 'table' and def.type ~= nil) then
                        handlers = { onTrigger = def }
                    end

                    for handlerName, handler in pairs(handlers) do
                        if handlerName == 'onTrigger' then
                            local finished = getFinishedEvents(lineId, zoneId)
                            local score    = actionPriority(handler)
                            local action   = handler
                            if type(handler) == 'function' then
                                local proxy, probe = makeProbe(player, { dryRun = true })
                                local ok, result = pcall(handler, proxy, probeNpc(getNpcByName(zoneId, key)))
                                action = nil
                                if ok and type(result) == 'table' then
                                    score  = actionPriority(result)
                                    action = result
                                elseif #probe.events > 0 then
                                    score = talkScore(kScore.talk, probe.events[1], finished)
                                end
                            end

                            if type(action) == 'table' and action.type == Action.Type.Event then
                                score = talkScore(score, action.id, finished)
                            end

                            add({ kind = 'talk', zone = zoneId, name = key, score = score })
                        elseif handlerName == 'onTrade' and type(handler) == 'function' then
                            local proxy = makeProbe(player, { dryRun = true })
                            local npc   = probeNpc(getNpcByName(zoneId, key))
                            local wanted = withTradeRecorder(function()
                                return handler(proxy, npc, emptyTrade)
                            end)

                            if next(wanted) then
                                add({ kind = 'trade', zone = zoneId, name = key, items = wanted, score = kScore.trade })
                            end
                        elseif handlerName == 'onMobDeath' then
                            add({ kind = 'kill', zone = zoneId, name = key, score = kScore.kill })
                        end
                    end
                end
            end
        end
    end

    -- An event the section only reacts to when it finishes is started outside the mission script: a gate
    -- guard's mission menu, or walking into a geyser in Dangruf Wadi. Find what starts it in that zone.
    for zoneId, zoneDef in pairs(section) do
        local finishes = type(zoneDef) == 'table' and zoneDef.onEventFinish
        if
            type(zoneId) == 'number' and
            type(finishes) == 'table'
        then
            local sources = getEventSources(player, zoneId)
            for csid, _ in pairs(finishes) do
                for _, step in ipairs(sources[csid] or {}) do
                    local score = step.kind == 'area' and kScore.area or kScore.talk
                    add({ kind = step.kind, zone = zoneId, name = step.name, area = step.area, pos = step.pos, event = csid, score = score })
                end
            end
        end
    end
end

-- Runs every section check of every mission in the storyline.
-- With assumeHeld, keeps only the sections that pass because of a key item or item the character lacks.
local function collectCandidates(player, lineId, assumeHeld)
    local candidates = {}
    for _, entry in ipairs(getMissions(lineId)) do
        local container = entry.container
        for _, section in ipairs(container.sections) do
            local proxy, probe = makeProbe(player, { assumeHeld = assumeHeld })
            local args = container.getCheckArgs and container:getCheckArgs(proxy) or {}
            args[#args + 1] = interactionUtil.makeContainerVarCache(proxy)[container]
            args[#args + 1] = interactionUtil.makeTableCache(function(name)
                return proxy:getVar(name)
            end)

            local passes = true
            if section.check then
                local ok, result = pcall(section.check, proxy, unpack(args))
                passes = ok and result and true or false
            end

            if passes then
                local needs = nil
                if assumeHeld then
                    needs = { keyItems = probe.missingKeyItems, items = probe.missingItems }
                end

                if
                    not assumeHeld or
                    next(probe.missingKeyItems) or
                    next(probe.missingItems)
                then
                    addSectionCandidates(player, candidates, lineId, entry, section, needs)
                end
            end
        end
    end

    return candidates
end

-- A step at a battlefield's entrance, with the key items and trade items it needs to enter.
local function fightStep(player, content, missionId)
    local keyItems = {}
    for _, ki in ipairs(content.requiredKeyItems or {}) do
        local id = type(ki) == 'table' and ki[1] or ki
        if not player:hasKeyItem(id) then
            keyItems[id] = true
        end
    end

    local items = {}
    recordTradeItems(items, content.requiredItems)

    return {
        kind    = 'fight',
        zone    = content.zoneId,
        name    = content.entryNpcs and content.entryNpcs[1],
        mission = missionId,
        items   = next(items) and items or nil,
        needs   = next(keyItems) and { keyItems = keyItems, items = {} } or nil,
        score   = kScore.fight,
    }
end

-- Mission battlefields open for the current mission and status. They come in two forms:
--  * BattlefieldMission, which names its mission and required status;
--  * a plain Battlefield flagged isMission that checks the mission in its own entryRequirement
--    (the rank 2 missions at Balga's Dais, Horlais Peak and Waughroon Shrine). That check is dry-run.
local function collectFights(player, lineId)
    local line    = kLines[lineId]
    local fights  = {}
    local current = player:getCurrentMission(line.log)
    for _, content in pairs(xi.battlefield.contents or {}) do
        if content.isMission and content.mission ~= nil then
            local area = content.missionArea or player:getNation()
            if area == line.log and current == content.mission then
                local statusArea = content.missionStatusArea or player:getNation()
                local status     = player:getMissionStatus(statusArea, content.missionStatus)
                if
                    content.requiredMissionStatus == nil or
                    status == content.requiredMissionStatus
                then
                    table.insert(fights, fightStep(player, content, content.mission))
                end
            end
        elseif
            content.isMission and
            content.entryRequirement ~= Battlefield.entryRequirement
        then
            local proxy = makeProbe(player, { dryRun = true })
            local ok, open = pcall(content.entryRequirement, content, proxy, nil, true)
            if ok and open then
                table.insert(fights, fightStep(player, content, current))
            end
        end
    end

    return fights
end

-- Keeps the first of each kind/zone/NPC. Missions are collected in story order, so a step offered by
-- several missions (every nation mission's offer goes through the same gate guards) keeps the earliest.
local function dedupe(candidates)
    local seen = {}
    local out  = {}
    for _, c in ipairs(candidates) do
        local key = string.format('%s:%d:%s:%s', c.kind, c.zone or 0, c.name or '', tostring(c.area))
        if not seen[key] then
            seen[key] = true
            table.insert(out, c)
        end
    end

    return out
end

local function sortCandidates(candidates)
    table.sort(candidates, function(a, b)
        if a.score ~= b.score then
            return a.score > b.score
        end

        return (a.name or '') < (b.name or '')
    end)
end

-- The steps that advance the storyline now, best first. Messages and flavour lines are left out
-- unless there is nothing else.
local function analyse(player, lineId)
    sourceCache = {}

    local candidates = collectCandidates(player, lineId, false)
    for _, fight in ipairs(collectFights(player, lineId)) do
        table.insert(candidates, fight)
    end

    -- table.sort is not stable: drop duplicates while the list is still in story order
    candidates = dedupe(candidates)
    sortCandidates(candidates)

    -- Every candidate and its score goes to the map log, to see why a step was picked
    print(string.format('!story %s for %s: mission %d status %d, %d candidates',
        lineId, player:getName(), player:getCurrentMission(kLines[lineId].log),
        player:getMissionStatus(kLines[lineId].log), #candidates))
    for i, c in ipairs(candidates) do
        print(string.format('!story   %d. %4d %-6s zone %d %s event %s mission %d', i, c.score, c.kind, c.zone or 0,
            c.name or (c.area and ('area ' .. c.area)) or '-', tostring(c.event), c.mission or -1))
    end

    local useful = {}
    for _, c in ipairs(candidates) do
        if c.score >= Action.Priority.Event then
            table.insert(useful, c)
        end
    end

    if #useful > 0 then
        return useful, false
    end

    -- Nothing advances it as things stand: look for steps blocked only by a missing key item or item.
    local blocked = {}
    for _, c in ipairs(dedupe(collectCandidates(player, lineId, true))) do
        if c.score >= Action.Priority.Event then
            table.insert(blocked, c)
        end
    end

    sortCandidates(blocked)
    if #blocked > 0 then
        return blocked, true
    end

    return candidates, false
end

-----------------------------------
-- Acting on a step
-----------------------------------

local function describe(lineId, c)
    local where = zoneName(c.zone)
    local what
    if c.kind == 'talk' and c.event then
        what = string.format('talk to %s (event %d)', c.name, c.event)
    elseif c.kind == 'talk' then
        what = string.format('talk to %s', c.name)
    elseif c.kind == 'trade' then
        local parts = {}
        for id, qty in pairs(c.items) do
            if id == 'gil' then
                table.insert(parts, string.format('%d gil', qty))
            else
                table.insert(parts, string.format('%s x%d', itemName(id), qty))
            end
        end

        table.sort(parts)
        what = string.format('trade %s to %s', table.concat(parts, ', '), c.name)
    elseif c.kind == 'kill' then
        what = string.format('defeat %s', c.name)
    elseif c.kind == 'fight' then
        what = string.format('battlefield at %s', c.name or '?')
    elseif c.kind == 'zoneIn' then
        what = 'zone in (arrival cutscene)'
    elseif c.kind == 'area' then
        what = string.format('walk into trigger area %d', c.area)
        if c.pos then
            what = string.format('%s at %.0f, %.0f', what, c.pos.x, c.pos.z)
        end
    end

    local text = string.format('%s in %s [%s]', what, where, missionName(lineId, c.mission))
    if c.needs then
        local parts = {}
        for ki, _ in pairs(c.needs.keyItems or {}) do
            table.insert(parts, 'KI ' .. keyItemName(ki))
        end

        for id, _ in pairs(c.needs.items or {}) do
            table.insert(parts, itemName(id))
        end

        if #parts > 0 then
            table.sort(parts)
            text = text .. ' (needs ' .. table.concat(parts, ', ') .. ')'
        end
    end

    return text
end

local function giveItem(player, id, qty)
    if player:getItemCount(id) >= qty then
        return
    end

    if player:getFreeSlotsCount() == 0 then
        say(player, 'No room for %s; free an inventory slot and run it again.', itemName(id))
        return
    end

    player:addItem(id, qty - player:getItemCount(id))
    say(player, 'Gave %s x%d.', itemName(id), qty)
end

local function giveRequirements(player, c)
    if c.needs then
        for ki, _ in pairs(c.needs.keyItems or {}) do
            if not player:hasKeyItem(ki) then
                player:addKeyItem(ki)
                say(player, 'Gave key item %s.', keyItemName(ki))
            end
        end

        for id, _ in pairs(c.needs.items or {}) do
            giveItem(player, id, 1)
        end
    end

    for id, qty in pairs(c.items or {}) do
        if id == 'gil' then
            if player:getGil() < qty then
                player:addGil(qty - player:getGil())
                say(player, 'Gave %d gil.', qty)
            end
        else
            giveItem(player, id, qty)
        end
    end
end

local function goTo(player, c)
    local target = c.name and getNpcByName(c.zone, c.name)
    local zone   = c.zone ~= player:getZoneID() and c.zone or nil

    if target then
        player:timer(500, function(p)
            p:setPos(target:getXPos(), target:getYPos(), target:getZPos(), target:getRotPos(), zone)
        end)
    elseif c.kind == 'area' and c.pos and c.pos.y then
        -- Into the trigger area: it fires on the next movement inside it
        player:timer(500, function(p)
            p:setPos(c.pos.x, c.pos.y, c.pos.z, 0, zone)
        end)
    elseif c.kind == 'zoneIn' and c.zone == player:getZoneID() then
        -- setPos within the zone only moves the character; the arrival cutscene needs a real zone-in
        say(player, 'Zone out of %s and back in for the arrival cutscene.', zoneName(c.zone))
    elseif c.kind == 'zoneIn' or c.kind == 'area' or c.name then
        -- Arrive at the zone's own starting point; the zone script places the character.
        if c.name then
            say(player, 'Could not find %s; going to %s.', c.name, zoneName(c.zone))
        end

        player:timer(500, function(p)
            p:setPos(0, 0, 0, 0, c.zone)
        end)
    end
end

-----------------------------------
-- Subcommands
-----------------------------------

local function showStatus(player, lineId)
    local line    = kLines[lineId]
    local current = player:getCurrentMission(line.log)
    say(player, '%s: %s, status %d.', line.title, missionName(lineId, current), player:getMissionStatus(line.log))
end

local function cmdLines(player)
    for _, lineId in ipairs(kLineOrder) do
        showStatus(player, lineId)
    end
end

local function cmdStart(player, lineArg, missionArg)
    local lineId = resolveLine(player, lineArg)
    if not lineId then
        say(player, 'Unknown storyline. Use sandoria, bastok, windurst, nation, rotz, cop or toau.')
        return
    end

    local line = kLines[lineId]
    rememberLine(player, lineId)

    if line.nation and player:getNation() ~= line.nation then
        player:setNation(line.nation)
        say(player, 'Nation set to %s.', line.title)
    end

    if missionArg and missionArg ~= '' then
        local wanted  = missionArg:gsub('-', '_')
        local entries = getMissions(lineId)
        local index   = nil
        for i, entry in ipairs(entries) do
            if entry.label == wanted or labelKey(entry.label) == labelKey(wanted) then
                index = i
            end
        end

        if not index then
            say(player, 'No %s mission %s.', line.title, missionArg)
            return
        end

        -- completeMission only completes the current mission, so walk the earlier ones in order.
        -- Chains of Promathia only tracks its current mission.
        if line.log ~= xi.mission.log_id.COP then
            for i = 1, index - 1 do
                local id = entries[i].container.missionId
                if not player:hasCompletedMission(line.log, id) then
                    player:addMission(line.log, id)
                    player:completeMission(line.log, id)
                end
            end
        end

        player:addMission(line.log, entries[index].container.missionId)
        player:setMissionStatus(line.log, 0)

        -- Nation missions are grouped by rank: 2-1 needs rank 2.
        local rank = tonumber(entries[index].label:match('^(%d+)_'))
        if line.nation and rank and player:getRank(line.nation) < rank then
            player:setRank(rank)
            say(player, 'Rank set to %d.', rank)
        end
    end

    showStatus(player, lineId)
    say(player, 'Next: !story show, or !story next to go there.')
end

-- Between nation missions the gate guards only list the next one once the rank and rank bar allow it
-- (xi.mission.getMissionMask). Returns what the next mission needs: { mission, rank, rankPoints }, or nil.
local kMaxRankPoints = 4000 -- setRankPoints caps here; covers every mission's crystal requirement

local function nationOfferNeeds(player, lineId)
    local line = kLines[lineId]
    if
        not line.nation or
        player:getCurrentMission(line.log) ~= xi.mission.id.nation.NONE
    then
        return nil
    end

    for _, entry in ipairs(getMissions(lineId)) do
        local id = entry.container.missionId
        if not player:hasCompletedMission(line.log, id) then
            local needs = { mission = id }

            -- Missions are grouped by rank: 2-1 needs rank 2
            local rank = tonumber(entry.label:match('^(%d+)_'))
            if rank and player:getRank(line.nation) < rank then
                needs.rank = rank
            end

            if player:getRankPoints() < kMaxRankPoints then
                needs.rankPoints = kMaxRankPoints
            end

            return (needs.rank or needs.rankPoints) and needs or nil
        end
    end
end

local function applyNationOffer(player, lineId)
    local needs = nationOfferNeeds(player, lineId)
    if not needs then
        return
    end

    if needs.rank then
        player:setRank(needs.rank)
        say(player, 'Rank set to %d.', needs.rank)
    end

    if needs.rankPoints then
        player:setRankPoints(needs.rankPoints)
        say(player, 'Rank points set to %d.', needs.rankPoints)
    end
end

local function cmdShow(player, lineArg)
    local lineId = resolveLine(player, lineArg)
    if not lineId then
        say(player, 'Unknown storyline.')
        return
    end

    showStatus(player, lineId)

    local offer = nationOfferNeeds(player, lineId)
    if offer then
        say(player, 'For %s the gate guards need %s; !story next sets it.',
            missionName(lineId, offer.mission),
            offer.rank and string.format('rank %d and a full rank bar', offer.rank) or 'a full rank bar')
    end

    local steps, blocked = analyse(player, lineId)
    if #steps == 0 then
        say(player, 'Nothing in the mission scripts advances this right now.')
        return
    end

    if blocked then
        say(player, 'Nothing advances it yet; these steps are waiting on a key item or item:')
    end

    for i = 1, math.min(#steps, kMaxShown) do
        say(player, '%d. %s', i, describe(lineId, steps[i]))
    end
end

local function cmdNext(player, lineArg, indexArg)
    -- !story next 2 picks the second step on the current storyline
    if tonumber(lineArg) and not indexArg then
        lineArg, indexArg = nil, lineArg
    end

    local lineId = resolveLine(player, lineArg)
    if not lineId then
        say(player, 'Unknown storyline.')
        return
    end

    rememberLine(player, lineId)
    applyNationOffer(player, lineId)

    local steps = analyse(player, lineId)
    local step  = steps[tonumber(indexArg) or 1]
    if not step then
        say(player, 'Nothing in the mission scripts advances this right now. Try !story show.')
        return
    end

    say(player, '%s', describe(lineId, step))
    giveRequirements(player, step)
    goTo(player, step)
end

local function cmdHelp(player)
    say(player, '!story lines | start <line> [mission] | show [line] | next [line] [n]')
    say(player, 'Lines: sandoria, bastok, windurst, nation, rotz, cop, toau.')
    local lineId = resolveLine(player, nil)
    if lineId then
        showStatus(player, lineId)
    end
end

local function run(player, sub, arg1, arg2)
    if sub == 'lines' then
        cmdLines(player)
    elseif sub == 'start' then
        cmdStart(player, arg1, arg2)
    elseif sub == 'show' then
        cmdShow(player, arg1)
    elseif sub == 'next' then
        cmdNext(player, arg1, arg2)
    else
        cmdHelp(player)
    end
end

commandObj.onTrigger = function(player, sub, arg1, arg2)
    sub = sub and string.lower(sub) or ''

    -- Show a failure in game as well as in the map log, so it is never silent
    local ok, err = pcall(run, player, sub, arg1, arg2)
    if not ok then
        print(string.format('!story %s failed: %s', sub, tostring(err)))
        say(player, '!story failed: %s', tostring(err):sub(1, 120))
    end
end

xi.module.registerCommand('story', commandObj)
