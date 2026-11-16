-- Optional bitmap-margin support in the D native build; old backend stays valid.
-- Text/ASS layout remains in mpv.conf. Positioned signs are not forcibly stripped.
if mp.get_property_native('option-info/image-subs-use-margins') then
    mp.set_property_bool('image-subs-use-margins',true)
end
