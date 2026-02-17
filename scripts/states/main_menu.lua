-- scripts/states/main_menu.lua

local selected_mode = "classic"
local screen = "main" -- main | settings | video | audio | controls | gameplay | accessibility
local fullscreen = false
local started = false
hide_world = true

local COLOR_SELECTED = { 1.0, 0.92, 0.10 }
local COLOR_NORMAL = { 1.0, 1.0, 1.0 }
local COLOR_MUTED = { 0.72, 0.78, 0.86 }

local resolutions = {
    { id = "res_1280_720", label = "1280x720", w = 1280, h = 720 },
    { id = "res_1600_900", label = "1600x900", w = 1600, h = 900 },
    { id = "res_1920_1080", label = "1920x1080", w = 1920, h = 1080 }
}
local selected_res_index = 1

local video_cfg = {
    vsync = true,
    fps_caps = { 30, 60, 120, 0 },
    fps_index = 2,
    ui_scales = { 75, 100, 125, 150 },
    ui_scale_index = 2,
    quality = { "Low", "Medium", "High", "Ultra" },
    quality_index = 3,
    renderer_backends = { "auto", "opengl", "vulkan", "d3d12" },
    renderer_index = 1,
    require_discrete_gpu = false
}

local audio_cfg = {
    master = 80,
    music = 70,
    sfx = 80,
    voice = 80,
    mute = false
}

local controls_cfg = {
    layouts = { "Classic", "Modern" },
    layout_index = 1,
    invert_y = false,
    rumble = true
}

local gameplay_cfg = {
    cam_shake = true,
    hit_stop = true,
    auto_pause = false,
    tutorial = true
}

local access_cfg = {
    text_scales = { 100, 125, 150, 175 },
    text_scale_index = 1,
    high_contrast = false,
    color_filters = { "Off", "Protanopia", "Deuteranopia", "Tritanopia" },
    color_filter_index = 1,
    reduce_motion = false,
    captions = true
}

local function selected_resolution()
    return resolutions[selected_res_index]
end

local function bool_text(v)
    if v then return "On" end
    return "Off"
end

local function renderer_label(v)
    if v == "auto" then return "Auto" end
    if v == "opengl" then return "OpenGL" end
    if v == "vulkan" then return "Vulkan" end
    if v == "d3d12" then return "D3D12" end
    return tostring(v)
end

local function cycle_index(tbl, idx, dir)
    local n = #tbl
    if n <= 0 then return 1 end
    local next_idx = idx + dir
    if next_idx > n then next_idx = 1 end
    if next_idx < 1 then next_idx = n end
    return next_idx
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

    local pref = get_renderer_backend_pref()
    if pref then
        for i, backend in ipairs(video_cfg.renderer_backends) do
            if backend == pref then
                video_cfg.renderer_index = i
                break
            end
        end
    end
    video_cfg.require_discrete_gpu = get_require_discrete_gpu_pref() == true
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

local function add_entry(entries, id, label, opts)
    local row = { id = id, label = label }
    if opts then
        for k, v in pairs(opts) do
            row[k] = v
        end
    end
    table.insert(entries, row)
end

local function heading(entries, label, y_frac)
    add_entry(entries, "h_" .. tostring(label), label, {
        enabled = false,
        color = COLOR_MUTED,
        x_frac = 0.5,
        y_frac = y_frac,
        anchor = "center"
    })
end

local function segmented(entries, id, label, x_frac, y_frac, selected)
    add_entry(entries, id, label, {
        x_frac = x_frac,
        y_frac = y_frac,
        anchor = "center",
        bold = selected,
        underline = selected,
        color = selected and COLOR_SELECTED or COLOR_NORMAL
    })
end

local function action(entries, id, label, y_frac, bold)
    add_entry(entries, id, label, {
        x_frac = 0.5,
        y_frac = y_frac,
        anchor = "center",
        bold = bold == true,
        color = COLOR_NORMAL
    })
end

local function info(entries, label, y_frac)
    add_entry(entries, "info_" .. tostring(y_frac), label, {
        enabled = false,
        x_frac = 0.5,
        y_frac = y_frac,
        anchor = "center",
        color = COLOR_MUTED
    })
end

local function build_main_entries(entries)
    heading(entries, "Main Menu", 0.22)

    if not started then
        heading(entries, "Choose Mode", 0.30)
        segmented(entries, "mode_classic", "CLASSIC", 0.40, 0.36, selected_mode == "classic")
        segmented(entries, "mode_adventure", "ADVENTURE", 0.60, 0.36, selected_mode == "adventure")
        action(entries, "start_game", "Start", 0.50, true)
    else
        action(entries, "start_game", "Resume", 0.42, true)
        heading(entries, "Start New Run", 0.52)
        segmented(entries, "new_game_classic", "New Classic", 0.40, 0.58, false)
        segmented(entries, "new_game_adventure", "New Adventure", 0.60, 0.58, false)
        info(entries, "Current run mode is locked until new run.", 0.64)
    end

    action(entries, "open_settings", "Settings", 0.72, false)
    action(entries, "quit_game", "Quit", 0.80, false)
end

local function build_settings_entries(entries)
    heading(entries, "Settings", 0.22)
    info(entries, "Most options are placeholders for tuning.", 0.27)

    segmented(entries, "open_video", "Display", 0.35, 0.38, false)
    segmented(entries, "open_audio", "Audio", 0.50, 0.38, false)
    segmented(entries, "open_controls", "Controls", 0.65, 0.38, false)

    segmented(entries, "open_gameplay", "Gameplay", 0.42, 0.50, false)
    segmented(entries, "open_accessibility", "Accessibility", 0.58, 0.50, false)

    action(entries, "settings_back_to_main", "Back", 0.72, false)
end

local function build_video_entries(entries)
    heading(entries, "Display", 0.22)

    segmented(entries, "display_windowed", "Windowed", 0.42, 0.32, not fullscreen)
    segmented(entries, "display_fullscreen", "Fullscreen", 0.58, 0.32, fullscreen)

    heading(entries, "Resolution", 0.40)
    segmented(entries, "res_1280_720", "1280x720", 0.32, 0.46, selected_res_index == 1)
    segmented(entries, "res_1600_900", "1600x900", 0.50, 0.46, selected_res_index == 2)
    segmented(entries, "res_1920_1080", "1920x1080", 0.68, 0.46, selected_res_index == 3)

    local backend = video_cfg.renderer_backends[video_cfg.renderer_index]
    local active_backend = get_active_renderer_backend()
    local active_gpu = get_active_gpu_renderer()
    local gpu_class = is_active_gpu_discrete() and "Discrete" or "Integrated"

    action(entries, "video_renderer_backend",
        "Render API: " .. renderer_label(backend) .. " (applies next launch)", 0.54, false)
    action(entries, "video_require_discrete",
        "Require Discrete GPU: " .. bool_text(video_cfg.require_discrete_gpu) .. " (applies next launch)", 0.60, false)
    info(entries, "Active API: " .. renderer_label(active_backend), 0.66)
    info(entries, "Active GPU: " .. tostring(active_gpu) .. " (" .. gpu_class .. ")", 0.70)

    local fps = video_cfg.fps_caps[video_cfg.fps_index]
    local fps_label = "FPS Cap: "
    if fps == 0 then fps_label = fps_label .. "Uncapped (placeholder)"
    else fps_label = fps_label .. tostring(fps) .. " (placeholder)" end

    action(entries, "video_vsync", "VSync: " .. bool_text(video_cfg.vsync) .. " (placeholder)", 0.76, false)
    action(entries, "video_fps", fps_label, 0.80, false)
    action(entries, "video_ui_scale", "UI Scale: " .. tostring(video_cfg.ui_scales[video_cfg.ui_scale_index]) .. "% (placeholder)", 0.84, false)
    action(entries, "video_quality", "Quality: " .. video_cfg.quality[video_cfg.quality_index] .. " (placeholder)", 0.88, false)

    action(entries, "settings_back", "Back", 0.94, false)
end

local function build_audio_entries(entries)
    heading(entries, "Audio", 0.22)
    action(entries, "audio_master", "Master: " .. tostring(audio_cfg.master) .. "% (placeholder)", 0.36, false)
    action(entries, "audio_music", "Music: " .. tostring(audio_cfg.music) .. "% (placeholder)", 0.44, false)
    action(entries, "audio_sfx", "SFX: " .. tostring(audio_cfg.sfx) .. "% (placeholder)", 0.52, false)
    action(entries, "audio_voice", "Voice: " .. tostring(audio_cfg.voice) .. "% (placeholder)", 0.60, false)
    action(entries, "audio_mute", "Mute All: " .. bool_text(audio_cfg.mute) .. " (placeholder)", 0.68, false)
    action(entries, "settings_back", "Back", 0.84, false)
end

local function build_controls_entries(entries)
    heading(entries, "Controls", 0.22)
    action(entries, "controls_layout", "Layout: " .. controls_cfg.layouts[controls_cfg.layout_index] .. " (placeholder)", 0.38, false)
    action(entries, "controls_invert_y", "Invert Y: " .. bool_text(controls_cfg.invert_y) .. " (placeholder)", 0.48, false)
    action(entries, "controls_rumble", "Rumble: " .. bool_text(controls_cfg.rumble) .. " (placeholder)", 0.58, false)
    action(entries, "settings_back", "Back", 0.84, false)
end

local function build_gameplay_entries(entries)
    heading(entries, "Gameplay", 0.22)
    action(entries, "gameplay_cam_shake", "Camera Shake: " .. bool_text(gameplay_cfg.cam_shake) .. " (placeholder)", 0.38, false)
    action(entries, "gameplay_hit_stop", "Hit Stop: " .. bool_text(gameplay_cfg.hit_stop) .. " (placeholder)", 0.48, false)
    action(entries, "gameplay_auto_pause", "Auto Pause: " .. bool_text(gameplay_cfg.auto_pause) .. " (placeholder)", 0.58, false)
    action(entries, "gameplay_tutorial", "Tutorial Tips: " .. bool_text(gameplay_cfg.tutorial) .. " (placeholder)", 0.68, false)
    action(entries, "settings_back", "Back", 0.84, false)
end

local function build_access_entries(entries)
    heading(entries, "Accessibility", 0.22)
    action(entries, "access_text_scale",
        "Text Scale: " .. tostring(access_cfg.text_scales[access_cfg.text_scale_index]) .. "% (placeholder)", 0.34, false)
    action(entries, "access_high_contrast",
        "High Contrast UI: " .. bool_text(access_cfg.high_contrast) .. " (placeholder)", 0.44, false)
    action(entries, "access_color_filter",
        "Color Filter: " .. access_cfg.color_filters[access_cfg.color_filter_index] .. " (placeholder)", 0.54, false)
    action(entries, "access_reduce_motion",
        "Reduce Motion: " .. bool_text(access_cfg.reduce_motion) .. " (placeholder)", 0.64, false)
    action(entries, "access_captions",
        "Captions: " .. bool_text(access_cfg.captions) .. " (placeholder)", 0.74, false)
    action(entries, "settings_back", "Back", 0.84, false)
end

function on_enter()
    started = get_has_started_game()
    screen = "main"
    sync_from_engine()
end

function get_message()
    if screen == "main" then return "Pokemon Autochess" end
    if screen == "settings" then return "Settings" end
    if screen == "video" then return "Display" end
    if screen == "audio" then return "Audio" end
    if screen == "controls" then return "Controls" end
    if screen == "gameplay" then return "Gameplay" end
    if screen == "accessibility" then return "Accessibility" end
    return "Menu"
end

function get_text_menu_entries()
    local entries = {}
    if screen == "main" then
        build_main_entries(entries)
    elseif screen == "settings" then
        build_settings_entries(entries)
    elseif screen == "video" then
        build_video_entries(entries)
    elseif screen == "audio" then
        build_audio_entries(entries)
    elseif screen == "controls" then
        build_controls_entries(entries)
    elseif screen == "gameplay" then
        build_gameplay_entries(entries)
    elseif screen == "accessibility" then
        build_access_entries(entries)
    end
    return entries
end

local function handle_main_click(entry_id)
    if entry_id == "mode_classic" then
        if started then return true end
        selected_mode = "classic"
        set_game_mode(selected_mode)
        emit("Menu", "Mode set to Classic")
        return true
    end
    if entry_id == "mode_adventure" then
        if started then return true end
        selected_mode = "adventure"
        set_game_mode(selected_mode)
        emit("Menu", "Mode set to Adventure")
        return true
    end
    if entry_id == "new_game_classic" then
        start_new_game("classic")
        return true
    end
    if entry_id == "new_game_adventure" then
        start_new_game("adventure")
        return true
    end
    if entry_id == "open_settings" then
        screen = "settings"
        return true
    end
    if entry_id == "start_game" then
        if started then
            pop_state()
        else
            start_new_game(selected_mode)
        end
        return true
    end
    if entry_id == "quit_game" then
        request_quit()
        return true
    end
    return false
end

local function handle_settings_click(entry_id)
    if entry_id == "open_video" then screen = "video"; return true end
    if entry_id == "open_audio" then screen = "audio"; return true end
    if entry_id == "open_controls" then screen = "controls"; return true end
    if entry_id == "open_gameplay" then screen = "gameplay"; return true end
    if entry_id == "open_accessibility" then screen = "accessibility"; return true end
    if entry_id == "settings_back_to_main" then screen = "main"; return true end
    return false
end

local function handle_video_click(entry_id)
    if entry_id == "display_windowed" then
        local r = selected_resolution()
        apply_video_mode(r.w, r.h, false)
        return true
    end
    if entry_id == "display_fullscreen" then
        local r = selected_resolution()
        apply_video_mode(r.w, r.h, true)
        return true
    end

    for i, r in ipairs(resolutions) do
        if entry_id == r.id then
            selected_res_index = i
            apply_video_mode(r.w, r.h, fullscreen)
            return true
        end
    end

    if entry_id == "video_renderer_backend" then
        video_cfg.renderer_index = cycle_index(video_cfg.renderer_backends, video_cfg.renderer_index, 1)
        local backend = video_cfg.renderer_backends[video_cfg.renderer_index]
        local ok = set_renderer_backend_pref(backend)
        if ok then
            emit("Menu", "Render API preference saved: " .. renderer_label(backend) .. " (restart required)")
        else
            emit("Menu", "Failed to save render API preference")
        end
        return true
    end
    if entry_id == "video_require_discrete" then
        video_cfg.require_discrete_gpu = not video_cfg.require_discrete_gpu
        local ok = set_require_discrete_gpu_pref(video_cfg.require_discrete_gpu)
        if ok then
            emit("Menu", "Require Discrete GPU: " .. bool_text(video_cfg.require_discrete_gpu) .. " (restart required)")
        else
            emit("Menu", "Failed to save discrete GPU preference")
        end
        return true
    end

    if entry_id == "video_vsync" then
        video_cfg.vsync = not video_cfg.vsync
        emit("Menu", "VSync changed (placeholder)")
        return true
    end
    if entry_id == "video_fps" then
        video_cfg.fps_index = cycle_index(video_cfg.fps_caps, video_cfg.fps_index, 1)
        emit("Menu", "FPS cap changed (placeholder)")
        return true
    end
    if entry_id == "video_ui_scale" then
        video_cfg.ui_scale_index = cycle_index(video_cfg.ui_scales, video_cfg.ui_scale_index, 1)
        emit("Menu", "UI scale changed (placeholder)")
        return true
    end
    if entry_id == "video_quality" then
        video_cfg.quality_index = cycle_index(video_cfg.quality, video_cfg.quality_index, 1)
        emit("Menu", "Quality changed (placeholder)")
        return true
    end
    if entry_id == "settings_back" then
        screen = "settings"
        return true
    end
    return false
end

local function handle_audio_click(entry_id)
    if entry_id == "audio_master" then
        audio_cfg.master = (audio_cfg.master + 10) % 110
        emit("Menu", "Master volume changed (placeholder)")
        return true
    end
    if entry_id == "audio_music" then
        audio_cfg.music = (audio_cfg.music + 10) % 110
        emit("Menu", "Music volume changed (placeholder)")
        return true
    end
    if entry_id == "audio_sfx" then
        audio_cfg.sfx = (audio_cfg.sfx + 10) % 110
        emit("Menu", "SFX volume changed (placeholder)")
        return true
    end
    if entry_id == "audio_voice" then
        audio_cfg.voice = (audio_cfg.voice + 10) % 110
        emit("Menu", "Voice volume changed (placeholder)")
        return true
    end
    if entry_id == "audio_mute" then
        audio_cfg.mute = not audio_cfg.mute
        emit("Menu", "Mute toggled (placeholder)")
        return true
    end
    if entry_id == "settings_back" then
        screen = "settings"
        return true
    end
    return false
end

local function handle_controls_click(entry_id)
    if entry_id == "controls_layout" then
        controls_cfg.layout_index = cycle_index(controls_cfg.layouts, controls_cfg.layout_index, 1)
        emit("Menu", "Control layout changed (placeholder)")
        return true
    end
    if entry_id == "controls_invert_y" then
        controls_cfg.invert_y = not controls_cfg.invert_y
        emit("Menu", "Invert Y toggled (placeholder)")
        return true
    end
    if entry_id == "controls_rumble" then
        controls_cfg.rumble = not controls_cfg.rumble
        emit("Menu", "Rumble toggled (placeholder)")
        return true
    end
    if entry_id == "settings_back" then
        screen = "settings"
        return true
    end
    return false
end

local function handle_gameplay_click(entry_id)
    if entry_id == "gameplay_cam_shake" then gameplay_cfg.cam_shake = not gameplay_cfg.cam_shake; emit("Menu", "Camera shake toggled (placeholder)"); return true end
    if entry_id == "gameplay_hit_stop" then gameplay_cfg.hit_stop = not gameplay_cfg.hit_stop; emit("Menu", "Hit stop toggled (placeholder)"); return true end
    if entry_id == "gameplay_auto_pause" then gameplay_cfg.auto_pause = not gameplay_cfg.auto_pause; emit("Menu", "Auto pause toggled (placeholder)"); return true end
    if entry_id == "gameplay_tutorial" then gameplay_cfg.tutorial = not gameplay_cfg.tutorial; emit("Menu", "Tutorial tips toggled (placeholder)"); return true end
    if entry_id == "settings_back" then screen = "settings"; return true end
    return false
end

local function handle_access_click(entry_id)
    if entry_id == "access_text_scale" then
        access_cfg.text_scale_index = cycle_index(access_cfg.text_scales, access_cfg.text_scale_index, 1)
        emit("Menu", "Text scale changed (placeholder)")
        return true
    end
    if entry_id == "access_high_contrast" then
        access_cfg.high_contrast = not access_cfg.high_contrast
        emit("Menu", "High contrast toggled (placeholder)")
        return true
    end
    if entry_id == "access_color_filter" then
        access_cfg.color_filter_index = cycle_index(access_cfg.color_filters, access_cfg.color_filter_index, 1)
        emit("Menu", "Color filter changed (placeholder)")
        return true
    end
    if entry_id == "access_reduce_motion" then
        access_cfg.reduce_motion = not access_cfg.reduce_motion
        emit("Menu", "Reduce motion toggled (placeholder)")
        return true
    end
    if entry_id == "access_captions" then
        access_cfg.captions = not access_cfg.captions
        emit("Menu", "Captions toggled (placeholder)")
        return true
    end
    if entry_id == "settings_back" then
        screen = "settings"
        return true
    end
    return false
end

function on_text_menu_click(entry_id)
    if screen == "main" then
        handle_main_click(entry_id)
        return
    end
    if screen == "settings" then
        handle_settings_click(entry_id)
        return
    end
    if screen == "video" then
        handle_video_click(entry_id)
        return
    end
    if screen == "audio" then
        handle_audio_click(entry_id)
        return
    end
    if screen == "controls" then
        handle_controls_click(entry_id)
        return
    end
    if screen == "gameplay" then
        handle_gameplay_click(entry_id)
        return
    end
    if screen == "accessibility" then
        handle_access_click(entry_id)
        return
    end
end

function on_update(dt)
    local _ = dt
end
