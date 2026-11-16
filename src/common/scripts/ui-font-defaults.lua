-- ui-font-defaults: migrate an unset Windows UI font without editing user config.
local platform = mp.get_property('platform', '')
if platform == 'windows' or platform == 'win32' then
    local font = mp.get_property('osd-font', '')
    if font == '' or font == 'sans-serif' or font == 'sans' then
        mp.set_property('osd-font', 'Segoe UI')
    end
end
-- Subtitle styles, bundled icon fonts and explicit custom UI fonts are untouched.
