-- Throwaway Hyprland (0.57+, Lua config) for testing hypr-window-anim in a nested window.
-- SUPER binds only: the host owns ALT, and host binds win over the nested session.

hl.monitor({ output = "", mode = "1600x900@60", position = "0x0", scale = 1 })

hl.config({
    debug = {
        disable_logs = false,
    },

    general = {
        layout      = "dwindle",
        gaps_in     = 4,
        gaps_out    = 8,
        border_size = 2,
        col = {
            active_border = { colors = { "rgba(9933ffee)", "rgba(ff9966ee)" }, angle = 45 },
        },
    },

    decoration = {
        rounding = 10,
        shadow = {
            enabled = true,
            range   = 6,
        },
    },

    animations = {
        enabled = true,
    },

    misc = {
        disable_hyprland_logo   = true,
        force_default_wallpaper = 0,
        background_color        = "rgb(1e1e2e)",
    },

    input = {
        kb_layout = "us",
    },
})

hl.curve("easeOutQuint", { type = "bezier", points = { {0.23, 1}, {0.32, 1} } })
hl.curve("linear",       { type = "bezier", points = { {0, 0},    {1, 1}    } })

hl.animation({ leaf = "windows",    enabled = true, speed = 4.79, bezier = "easeOutQuint" })
hl.animation({ leaf = "windowsOut", enabled = true, speed = 1.49, bezier = "linear", style = "popin 87%" })
hl.animation({ leaf = "fadeOut",    enabled = true, speed = 1.46, bezier = "default" })

hl.on("hyprland.start", function()
    hl.exec_cmd("foot")
end)

local function bind(keys, dispatcher)
    hl.bind("SUPER + " .. keys, dispatcher)
end

-- several ways to get a terminal; inside it any app can be started too
bind("Return",   hl.dsp.exec_cmd("foot"))
bind("KP_Enter", hl.dsp.exec_cmd("foot"))
bind("T",        hl.dsp.exec_cmd("foot"))
bind("Q",        hl.dsp.exec_cmd("foot"))
bind("C",        hl.dsp.window.close())
bind("SHIFT + E", hl.dsp.exit())
bind("V",        hl.dsp.window.float({ action = "toggle" }))
-- plugin functions vanish on unload; a wrapper keeps the bind safe
bind("P",        function() if hl.plugin.hwanim then hl.plugin.hwanim.preview() end end)
bind("left",     hl.dsp.focus({ direction = "left" }))
bind("right",    hl.dsp.focus({ direction = "right" }))
bind("up",       hl.dsp.focus({ direction = "up" }))
bind("down",     hl.dsp.focus({ direction = "down" }))
