-- scripts/states/main_menu.lua

local selected_mode = "classic"
local screen = "main" -- "main" | "video"
local fullscreen = false
local started = false
hide_world = true

local resolutions = {
    { id = "res_1280_720", label = "1280x720", w = 1280, h = 720 },
    { id = "res_1600_900", label = "1600x900", w = 1600, h = 900 },
    { id = "res_1920_1080", label = "1920x1080", w = 1920, h = 1080 }
}

local selected_res_index = 1

local function selected_resolution()
    return resolutions[selected_res_index]
end

local function sync_from_engine()
    local gm = get_game_mode()
    if gm == "adventure" then
        selected_mode = "adventure"
    else
        selected_mode = "classic"
    end

    local vm = get_video_mode()
    if vm then
        fullscreen = vm.fullscreen == true
        for i, r in ipairs(resolutions) do
            if r.w == vm.width and r.h == vm.height then
                selected_res_index = i
                break
            end
        end
    end
end

local function mode_label(name, value)
    if selected_mode == value then
        return name .. " [Selected]"
    end
    return name
end

local function resolution_label(index, label)
    if index == selected_res_index then
        return label .. " [Selected]"
    end
    return label
end

local function apply_video_mode(width, height, want_fullscreen)
    local ok = set_video_mode(width, height, want_fullscreen)
    if ok then
        fullscreen = want_fullscreen
        emit("Menu", string.format("Video set: %s %dx%d",
            fullscreen and "Fullscreen" or "Windowed", width, height))
    else
        emit("Menu", "Failed to apply video mode")
    end
end

function on_enter()
    started = false
    screen = "main"
    sync_from_engine()
end

function get_message()
    if screen == "video" then
        local r = selected_resolution()
        return string.format("Video Settings | %s | %dx%d",
            fullscreen and "Fullscreen" or "Windowed",
            r.w, r.h)
    end
    return "Pokemon Autochess"
end

function get_text_menu_entries()
    local entries = {}

    if screen == "video" then
        table.insert(entries, {
            id = "toggle_fullscreen",
            label = fullscreen and "Display: Fullscreen" or "Display: Windowed"
        })
        for i, r in ipairs(resolutions) do
            table.insert(entries, {
                id = r.id,
                label = resolution_label(i, r.label)
            })
        end
        table.insert(entries, { id = "video_back", label = "Back" })
        return entries
    end

    table.insert(entries, { id = "mode_classic", label = mode_label("Classic", "classic") })
    table.insert(entries, { id = "mode_adventure", label = mode_label("Adventure", "adventure") })
    table.insert(entries, { id = "open_video", label = "Video Settings" })
    table.insert(entries, { id = "start_game", label = "Start" })
    return entries
end

function on_text_menu_click(entry_id)
    if screen == "video" then
        if entry_id == "toggle_fullscreen" then
            local r = selected_resolution()
            apply_video_mode(r.w, r.h, not fullscreen)
            return
        end

        for i, r in ipairs(resolutions) do
            if entry_id == r.id then
                selected_res_index = i
                apply_video_mode(r.w, r.h, fullscreen)
                return
            end
        end

        if entry_id == "video_back" then
            screen = "main"
            return
        end
        return
    end

    if entry_id == "mode_classic" then
        selected_mode = "classic"
        set_game_mode(selected_mode)
        emit("Menu", "Mode set to Classic")
        return
    end
    if entry_id == "mode_adventure" then
        selected_mode = "adventure"
        set_game_mode(selected_mode)
        emit("Menu", "Mode set to Adventure")
        return
    end
    if entry_id == "open_video" then
        screen = "video"
        return
    end
    if entry_id == "start_game" then
        if started then return end
        started = true
        push_state("scripts/states/starter.lua")
        return
    end
end

function on_update(dt)
    local _ = dt
end
