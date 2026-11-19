local M={}

function M.readiness(dependency, adapter)
    if dependency.state=='checking' then return 'checking' end
    if dependency.state=='probe-failed' then return 'probe-failed' end
    if dependency.installed~=true then return 'dolby-missing' end
    if not adapter then return 'adapter-missing' end
    if dependency.display_hdr_active~=false or
        (dependency.display_color_mode~='sdr' and dependency.display_color_mode~='wcg') then
        return 'not-sdr'
    end
    local profiles=dependency.renderer_profiles or {}
    for _,profile in ipairs(profiles) do
        if profile=='dvhe.08' then return 'ready' end
    end
    return 'profile-missing'
end

function M.source(params, profile, active)
    if not active or profile~=0 or params.colormatrix~='bt.2020-ncl' then return nil end
    local gamma=params['gamma-original'] or params.gamma
    if gamma=='pq' then return 'hdr10' end
    if gamma=='hlg' then return 'hlg' end
end

function M.route(requested, readiness, source, windows_wanted, windows_available, blocked)
    if blocked then return nil end
    if requested and readiness=='ready' and source then return 'bridge' end
    if windows_wanted and windows_available then return 'windows' end
end

return M
