-- scripts/states/main_menu.lua

local selected_mode = "classic"
local screen = "main" -- main | starting | settings | video | graphics | advanced | advanced_restart_confirm | audio | controls | gameplay | accessibility
local fullscreen = false
local started = false
hide_world = true
local start_pending = false
local start_pending_mode = "classic"

local COLOR_SELECTED = { 1.0, 0.92, 0.10 }
local COLOR_NORMAL = { 1.0, 1.0, 1.0 }
local COLOR_MUTED = { 0.72, 0.78, 0.86 }

local current_video_size_label = "Unknown"

local video_cfg = {
    vsync = false,
    fps_caps = { 30, 60, 120, 144, 240, 0 },
    fps_cap = 0,
    ui_scales = { 75, 100, 125, 150 },
    ui_scale_index = 2,
    quality = { "Low", "Medium", "High", "Ultra" },
    quality_index = 3,
    renderer_backends = {},
    renderer_index = 1,
    require_discrete_gpu = false,
    character_inking = false,
    gpu_adapters = { "auto" },
    gpu_adapter_index = 1
}
local renderer_backend_catalog = { "opengl", "vulkan", "d3d12" }

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

local function backend_pref_matches_active(pref, active)
    return pref == active
end

local function gpu_adapter_label(v)
    if v == nil or v == "" or v == "auto" then
        return "Auto (System Default)"
    end
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

local function cycle_value(tbl, current, dir)
    for i, value in ipairs(tbl) do
        if value == current then
            return tbl[cycle_index(tbl, i, dir)]
        end
    end
    if dir >= 0 then
        return tbl[1]
    end
    return tbl[#tbl]
end

local function cycle_percent(value)
    local next_value = (value or 0) + 10
    if next_value > 100 then
        next_value = 0
    end
    return next_value
end

local function clamp_percent(value)
    local n = tonumber(value) or 0
    n = math.floor(n + 0.5)
    if n < 0 then n = 0 end
    if n > 100 then n = 100 end
    return n
end

local function get_bool_pref(fn_name, fallback)
    local fn = _G[fn_name]
    if type(fn) ~= "function" then
        return fallback == true
    end
    local ok, value = pcall(fn)
    if not ok then
        return fallback == true
    end
    return value == true
end

local function set_bool_pref(fn_name, value)
    local fn = _G[fn_name]
    if type(fn) ~= "function" then
        return false
    end
    local ok, result = pcall(fn, value == true)
    if not ok then
        return false
    end
    return result == true
end

local function is_renderer_backend_implemented(backend)
    local fn = _G["is_renderer_backend_implemented"]
    if type(fn) ~= "function" then
        return true
    end
    local ok, result = pcall(fn, backend)
    if not ok then
        return true
    end
    return result == true
end

local function get_int_pref(fn_name, fallback)
    local fn = _G[fn_name]
    if type(fn) ~= "function" then
        return fallback
    end
    local ok, value = pcall(fn)
    if not ok or type(value) ~= "number" then
        return fallback
    end
    return math.floor(value)
end

local function set_int_pref(fn_name, value)
    local fn = _G[fn_name]
    if type(fn) ~= "function" then
        return false
    end
    local ok, result = pcall(fn, math.floor(value or 0))
    if not ok then
        return false
    end
    return result == true
end

local function sync_renderer_backend_options(preferred_backend)
    local filtered = {}
    for _, backend in ipairs(renderer_backend_catalog) do
        if is_renderer_backend_implemented(backend) then
            table.insert(filtered, backend)
        end
    end
    if #filtered == 0 then
        filtered = { "opengl" }
    end

    video_cfg.renderer_backends = filtered
    video_cfg.renderer_index = 1
    if preferred_backend then
        for i, backend in ipairs(video_cfg.renderer_backends) do
            if backend == preferred_backend then
                video_cfg.renderer_index = i
                break
            end
        end
    end
end

local function sync_gpu_adapter_options()
    video_cfg.gpu_adapters = { "auto" }
    local adapters = get_gpu_adapters()
    if adapters then
        for _, name in ipairs(adapters) do
            if type(name) == "string" and name ~= "" then
                table.insert(video_cfg.gpu_adapters, name)
            end
        end
    end

    video_cfg.gpu_adapter_index = 1
    local pref = get_preferred_gpu_adapter_pref()
    if pref and pref ~= "" then
        for i, name in ipairs(video_cfg.gpu_adapters) do
            if name == pref then
                video_cfg.gpu_adapter_index = i
                break
            end
        end
    end
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
        current_video_size_label = string.format("%dx%d", vm.width or 0, vm.height or 0)
    else
        current_video_size_label = "Unknown"
    end

    local pref = get_renderer_backend_pref()
    sync_renderer_backend_options(pref)
    video_cfg.vsync = get_bool_pref("get_vsync_pref", video_cfg.vsync)
    video_cfg.fps_cap = get_int_pref("get_fps_cap_pref", video_cfg.fps_cap)
    video_cfg.require_discrete_gpu = get_bool_pref("get_require_discrete_gpu_pref", video_cfg.require_discrete_gpu)
    video_cfg.character_inking = get_bool_pref("get_character_inking_pref", video_cfg.character_inking)
    audio_cfg.master = get_int_pref("get_audio_master_volume_pref", audio_cfg.master)
    audio_cfg.music = get_int_pref("get_audio_music_volume_pref", audio_cfg.music)
    audio_cfg.sfx = get_int_pref("get_audio_sfx_volume_pref", audio_cfg.sfx)
    audio_cfg.voice = get_int_pref("get_audio_voice_volume_pref", audio_cfg.voice)
    audio_cfg.mute = get_bool_pref("get_audio_mute_pref", audio_cfg.mute)
    sync_gpu_adapter_options()
end

local function apply_video_mode(width, height, want_fullscreen)
    local ok = set_video_mode(width, height, want_fullscreen)
    if ok then
        sync_from_engine()
        emit("Menu", string.format("Video set: %s %s",
            fullscreen and "Fullscreen" or "Windowed",
            current_video_size_label))
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

local function slider_action(entries, id, label, y_frac, slider_value, slider_opts)
    local opts = {
        x_frac = 0.5,
        y_frac = y_frac,
        anchor = "center",
        color = COLOR_NORMAL,
        slider = {
            min = slider_opts and slider_opts.min or 0,
            max = slider_opts and slider_opts.max or 100,
            value = slider_value,
            step = slider_opts and slider_opts.step or 10,
            width_frac = slider_opts and slider_opts.width_frac or 0.22,
            value_label = slider_opts and slider_opts.value_label or (tostring(clamp_percent(slider_value)) .. "%")
        }
    }
    add_entry(entries, id, label, opts)
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

local function build_starting_entries(entries)
    heading(entries, "Starting New Run", 0.28)
    info(entries, "Preparing starter selection...", 0.42)
    info(entries, "Please wait...", 0.50)
end

local function build_settings_entries(entries)
    heading(entries, "Settings", 0.22)
    info(entries, "Display handles screen behavior. Renderer and GPU tweaks live in Advanced.", 0.28)
    action(entries, "open_video", "Display", 0.38, false)
    action(entries, "open_graphics", "Graphics", 0.46, false)
    action(entries, "open_audio", "Audio", 0.54, false)
    action(entries, "open_controls", "Controls", 0.62, false)
    action(entries, "open_gameplay", "Gameplay", 0.70, false)
    action(entries, "open_accessibility", "Accessibility", 0.78, false)
    action(entries, "open_advanced", "Advanced", 0.86, false)
    action(entries, "settings_back_to_main", "Back", 0.94, false)
end

local function build_video_entries(entries)
    heading(entries, "Display", 0.22)

    segmented(entries, "display_windowed", "Windowed", 0.42, 0.32, not fullscreen)
    segmented(entries, "display_fullscreen", "Fullscreen", 0.58, 0.32, fullscreen)
    if fullscreen then
        info(entries, "Current fullscreen size: " .. current_video_size_label, 0.40)
        info(entries, "Windowed mode restores your saved local window size.", 0.44)
    else
        info(entries, "Current window size: " .. current_video_size_label, 0.40)
        info(entries, "Drag the window edges to resize. Size saves automatically.", 0.44)
    end

    local fps = video_cfg.fps_cap
    local fps_label = "FPS Cap: "
    if fps == 0 then
        fps_label = fps_label .. "Uncapped"
    else
        fps_label = fps_label .. tostring(fps)
    end

    action(entries, "video_vsync", "VSync: " .. bool_text(video_cfg.vsync), 0.58, false)
    action(entries, "video_fps", fps_label, 0.66, false)
    info(entries, "Graphics quality and style options live in Graphics.", 0.76)
    info(entries, "Renderer and GPU selection live in Advanced.", 0.80)
    action(entries, "settings_back", "Back", 0.92, false)
end

local function build_graphics_entries(entries)
    heading(entries, "Graphics", 0.22)
    action(entries, "graphics_quality", "Quality: " .. video_cfg.quality[video_cfg.quality_index] .. " (placeholder)", 0.40, false)
    action(entries, "graphics_character_inking", "Character Inking: " .. bool_text(video_cfg.character_inking), 0.50, false)
    info(entries, "Window mode, VSync, and frame pacing live in Display.", 0.64)
    action(entries, "settings_back", "Back", 0.84, false)
end

local function build_advanced_entries(entries)
    heading(entries, "Advanced", 0.22)

    local backend = video_cfg.renderer_backends[video_cfg.renderer_index]
    local active_backend = get_active_renderer_backend()
    local active_gpu = get_active_gpu_renderer()
    local gpu_class = is_active_gpu_discrete() and "Discrete" or "Integrated"
    local adapter_pref = video_cfg.gpu_adapters[video_cfg.gpu_adapter_index]

    action(entries, "advanced_renderer_backend",
        "Render API Pref: " .. renderer_label(backend) .. " (next launch)", 0.38, false)
    action(entries, "advanced_require_discrete",
        "Require Discrete GPU: " .. bool_text(video_cfg.require_discrete_gpu) .. " (next launch)", 0.46, false)
    action(entries, "advanced_gpu_adapter",
        "Preferred GPU: " .. gpu_adapter_label(adapter_pref) .. " (next launch)", 0.54, false)
    info(entries, "Current API: " .. renderer_label(active_backend), 0.62)
    info(entries, "Active GPU: " .. tostring(active_gpu) .. " (" .. gpu_class .. ")", 0.66)
    if not backend_pref_matches_active(backend, active_backend) then
        info(entries, "Current API differs from preference (env override or fallback).", 0.70)
    end
    info(entries, "Use this screen only for backend/GPU troubleshooting.", 0.76)
    action(entries, "advanced_apply_restart", "Apply + Restart", 0.86, true)
    action(entries, "settings_back", "Back", 0.94, false)
end

local function build_advanced_restart_confirm_entries(entries)
    heading(entries, "Apply + Restart", 0.22)
    info(entries, "Renderer/GPU changes need a full restart to take effect.", 0.34)
    if started then
        info(entries, "Warning: restarting now will drop your current run progress.", 0.40)
    else
        info(entries, "You will return to Advanced settings after restart.", 0.40)
    end
    action(entries, "advanced_restart_confirm_yes", "Restart Now", 0.56, true)
    action(entries, "advanced_restart_confirm_no", "Cancel", 0.64, false)
end

local function build_audio_entries(entries)
    heading(entries, "Audio", 0.22)
    slider_action(entries, "audio_master", "Master", 0.36, audio_cfg.master, { step = 10, value_label = tostring(audio_cfg.master) .. "%" })
    slider_action(entries, "audio_music", "Music", 0.44, audio_cfg.music, { step = 10, value_label = tostring(audio_cfg.music) .. "%" })
    slider_action(entries, "audio_sfx", "SFX", 0.52, audio_cfg.sfx, { step = 10, value_label = tostring(audio_cfg.sfx) .. "%" })
    slider_action(entries, "audio_voice", "Voice", 0.60, audio_cfg.voice, { step = 10, value_label = tostring(audio_cfg.voice) .. "%" })
    action(entries, "audio_mute", "Mute All: " .. bool_text(audio_cfg.mute), 0.68, false)
    info(entries, "Click the slider bar for exact values, or press the row key to step by 10%.", 0.76)
    info(entries, "Audio preferences save now. Game audio content is still forthcoming.", 0.80)
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
    action(entries, "access_ui_scale",
        "UI Scale: " .. tostring(video_cfg.ui_scales[video_cfg.ui_scale_index]) .. "% (placeholder)", 0.30, false)
    action(entries, "access_text_scale",
        "Text Scale: " .. tostring(access_cfg.text_scales[access_cfg.text_scale_index]) .. "% (placeholder)", 0.40, false)
    action(entries, "access_high_contrast",
        "High Contrast UI: " .. bool_text(access_cfg.high_contrast) .. " (placeholder)", 0.50, false)
    action(entries, "access_color_filter",
        "Color Filter: " .. access_cfg.color_filters[access_cfg.color_filter_index] .. " (placeholder)", 0.60, false)
    action(entries, "access_reduce_motion",
        "Reduce Motion: " .. bool_text(access_cfg.reduce_motion) .. " (placeholder)", 0.70, false)
    action(entries, "access_captions",
        "Captions: " .. bool_text(access_cfg.captions) .. " (placeholder)", 0.80, false)
    action(entries, "settings_back", "Back", 0.90, false)
end

function on_enter()
    started = get_has_started_game()
    screen = "main"
    local boot_screen = nil
    if consume_boot_menu_screen then
        boot_screen = consume_boot_menu_screen()
    end
    if boot_screen == "settings" or
       boot_screen == "video" or
       boot_screen == "graphics" or
       boot_screen == "advanced" or
       boot_screen == "audio" or
       boot_screen == "controls" or
       boot_screen == "gameplay" or
       boot_screen == "accessibility" then
        screen = boot_screen
    end
    sync_from_engine()
end

function get_message()
    if screen == "main" then return "Pokemon Autochess" end
    if screen == "starting" then return "Starting" end
    if screen == "settings" then return "Settings" end
    if screen == "video" then return "Display" end
    if screen == "graphics" then return "Graphics" end
    if screen == "advanced" then return "Advanced" end
    if screen == "advanced_restart_confirm" then return "Apply Advanced Changes" end
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
    elseif screen == "starting" then
        build_starting_entries(entries)
    elseif screen == "settings" then
        build_settings_entries(entries)
    elseif screen == "video" then
        build_video_entries(entries)
    elseif screen == "graphics" then
        build_graphics_entries(entries)
    elseif screen == "advanced" then
        build_advanced_entries(entries)
    elseif screen == "advanced_restart_confirm" then
        build_advanced_restart_confirm_entries(entries)
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
        emit("FlowTrace", "menu_click entry=new_game_classic mode=classic")
        start_new_game("classic")
        return true
    end
    if entry_id == "new_game_adventure" then
        emit("FlowTrace", "menu_click entry=new_game_adventure mode=adventure")
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
            emit("FlowTrace", "menu_click entry=start_game mode=" .. tostring(selected_mode))
            start_pending = true
            start_pending_mode = selected_mode
            screen = "starting"
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
    if entry_id == "open_graphics" then screen = "graphics"; return true end
    if entry_id == "open_audio" then screen = "audio"; return true end
    if entry_id == "open_controls" then screen = "controls"; return true end
    if entry_id == "open_gameplay" then screen = "gameplay"; return true end
    if entry_id == "open_accessibility" then screen = "accessibility"; return true end
    if entry_id == "open_advanced" then screen = "advanced"; return true end
    if entry_id == "settings_back_to_main" then screen = "main"; return true end
    return false
end

local function handle_video_click(entry_id)
    if entry_id == "display_windowed" then
        apply_video_mode(0, 0, false)
        return true
    end
    if entry_id == "display_fullscreen" then
        apply_video_mode(0, 0, true)
        return true
    end

    if entry_id == "video_vsync" then
        video_cfg.vsync = not video_cfg.vsync
        local ok = set_bool_pref("set_vsync_pref", video_cfg.vsync)
        if ok then
            emit("Menu", "VSync: " .. bool_text(video_cfg.vsync))
        else
            emit("Menu", "Failed to save VSync preference")
            video_cfg.vsync = get_bool_pref("get_vsync_pref", video_cfg.vsync)
        end
        return true
    end
    if entry_id == "video_fps" then
        local next_cap = cycle_value(video_cfg.fps_caps, video_cfg.fps_cap, 1)
        local ok = set_int_pref("set_fps_cap_pref", next_cap)
        if ok then
            video_cfg.fps_cap = next_cap
            if next_cap == 0 then
                emit("Menu", "FPS cap: Uncapped")
            else
                emit("Menu", "FPS cap: " .. tostring(next_cap))
            end
        else
            emit("Menu", "Failed to save FPS cap preference")
            video_cfg.fps_cap = get_int_pref("get_fps_cap_pref", video_cfg.fps_cap)
        end
        return true
    end
    if entry_id == "settings_back" then
        screen = "settings"
        return true
    end
    return false
end

local function handle_graphics_click(entry_id)
    if entry_id == "graphics_character_inking" then
        video_cfg.character_inking = not video_cfg.character_inking
        local ok = set_bool_pref("set_character_inking_pref", video_cfg.character_inking)
        if ok then
            emit("Menu", "Character inking: " .. bool_text(video_cfg.character_inking))
        else
            emit("Menu", "Failed to save character inking preference")
        end
        return true
    end
    if entry_id == "graphics_quality" then
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

local function handle_advanced_click(entry_id)
    if entry_id == "advanced_renderer_backend" then
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
    if entry_id == "advanced_require_discrete" then
        video_cfg.require_discrete_gpu = not video_cfg.require_discrete_gpu
        local ok = set_require_discrete_gpu_pref(video_cfg.require_discrete_gpu)
        if ok then
            emit("Menu", "Require Discrete GPU: " .. bool_text(video_cfg.require_discrete_gpu) .. " (restart required)")
        else
            emit("Menu", "Failed to save discrete GPU preference")
        end
        return true
    end
    if entry_id == "advanced_gpu_adapter" then
        video_cfg.gpu_adapter_index = cycle_index(video_cfg.gpu_adapters, video_cfg.gpu_adapter_index, 1)
        local selected = video_cfg.gpu_adapters[video_cfg.gpu_adapter_index]
        local requested = selected
        if requested == "auto" then requested = "" end
        local ok = set_preferred_gpu_adapter_pref(requested)
        if ok then
            emit("Menu", "Preferred GPU saved: " .. gpu_adapter_label(selected) .. " (restart required)")
        else
            emit("Menu", "Failed to save preferred GPU")
        end
        return true
    end
    if entry_id == "settings_back" then
        screen = "settings"
        return true
    end
    if entry_id == "advanced_apply_restart" then
        if started then
            screen = "advanced_restart_confirm"
            return true
        end
        if request_restart_to_menu and request_restart_to_menu("advanced") then
            emit("Menu", "Applying advanced graphics settings and restarting...")
        else
            emit("Menu", "Failed to restart game")
        end
        return true
    end
    return false
end

local function handle_advanced_restart_confirm_click(entry_id)
    if entry_id == "advanced_restart_confirm_yes" then
        if request_restart_to_menu and request_restart_to_menu("advanced") then
            emit("Menu", "Applying advanced graphics settings and restarting...")
        else
            emit("Menu", "Failed to restart game")
        end
        return true
    end
    if entry_id == "advanced_restart_confirm_no" then
        screen = "advanced"
        return true
    end
    return false
end

local function resolve_audio_slider_value(current_value, slider_value)
    if type(slider_value) == "number" then
        return clamp_percent(slider_value)
    end
    return cycle_percent(current_value)
end

local function handle_audio_click(entry_id, slider_value)
    if entry_id == "audio_master" then
        local next_value = resolve_audio_slider_value(audio_cfg.master, slider_value)
        local ok = set_int_pref("set_audio_master_volume_pref", next_value)
        if ok then
            audio_cfg.master = next_value
            emit("Menu", "Master volume: " .. tostring(audio_cfg.master) .. "%")
        else
            emit("Menu", "Failed to save master volume")
        end
        return true
    end
    if entry_id == "audio_music" then
        local next_value = resolve_audio_slider_value(audio_cfg.music, slider_value)
        local ok = set_int_pref("set_audio_music_volume_pref", next_value)
        if ok then
            audio_cfg.music = next_value
            emit("Menu", "Music volume: " .. tostring(audio_cfg.music) .. "%")
        else
            emit("Menu", "Failed to save music volume")
        end
        return true
    end
    if entry_id == "audio_sfx" then
        local next_value = resolve_audio_slider_value(audio_cfg.sfx, slider_value)
        local ok = set_int_pref("set_audio_sfx_volume_pref", next_value)
        if ok then
            audio_cfg.sfx = next_value
            emit("Menu", "SFX volume: " .. tostring(audio_cfg.sfx) .. "%")
        else
            emit("Menu", "Failed to save SFX volume")
        end
        return true
    end
    if entry_id == "audio_voice" then
        local next_value = resolve_audio_slider_value(audio_cfg.voice, slider_value)
        local ok = set_int_pref("set_audio_voice_volume_pref", next_value)
        if ok then
            audio_cfg.voice = next_value
            emit("Menu", "Voice volume: " .. tostring(audio_cfg.voice) .. "%")
        else
            emit("Menu", "Failed to save voice volume")
        end
        return true
    end
    if entry_id == "audio_mute" then
        local next_value = not audio_cfg.mute
        local ok = set_bool_pref("set_audio_mute_pref", next_value)
        if ok then
            audio_cfg.mute = next_value
            emit("Menu", "Mute All: " .. bool_text(audio_cfg.mute))
        else
            emit("Menu", "Failed to save mute preference")
        end
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
    if entry_id == "access_ui_scale" then
        video_cfg.ui_scale_index = cycle_index(video_cfg.ui_scales, video_cfg.ui_scale_index, 1)
        emit("Menu", "UI scale changed (placeholder)")
        return true
    end
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

function on_text_menu_click(entry_id, slider_value)
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
    if screen == "graphics" then
        handle_graphics_click(entry_id)
        return
    end
    if screen == "advanced" then
        handle_advanced_click(entry_id)
        return
    end
    if screen == "advanced_restart_confirm" then
        handle_advanced_restart_confirm_click(entry_id)
        return
    end
    if screen == "audio" then
        handle_audio_click(entry_id, slider_value)
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
    if screen == "starting" and start_pending then
        start_pending = false
        emit("FlowTrace", "menu_dispatch_start mode=" .. tostring(start_pending_mode))
        start_new_game(start_pending_mode)
    end
end

function on_text_menu_back()
    if screen == "advanced_restart_confirm" then
        screen = "advanced"
        return true
    end
    if screen == "video" or
       screen == "graphics" or
       screen == "advanced" or
       screen == "audio" or
       screen == "controls" or
       screen == "gameplay" or
       screen == "accessibility" then
        screen = "settings"
        return true
    end
    if screen == "settings" then
        screen = "main"
        return true
    end
    return false
end
