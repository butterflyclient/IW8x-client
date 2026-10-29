-- XMake build for the MW2019 1.20 Replay XInput proxy.
-- Run from this directory:
--   xmake f -m release -a x64 -y && xmake

set_project("mw120rproxy")
set_xmakever("2.8.0")

set_allowedplats("windows")
set_allowedarchs("x64")
set_allowedmodes("debug", "release")
set_defaultmode("release")
set_languages("cxx20", "c17")

option("custom_content")
    set_default(false)
    set_showmenu(true)
    set_description("Build converted frontends, Rebirth and night map ports")
option_end()

option("island_port_probe")
    set_default(false)
    set_showmenu(true)
    set_description("Build the isolated Island serialization probe; runtime semantics are incomplete")
option_end()

if has_config("island_port_probe") then
    add_defines("IW8X_ISLAND_PORT_PROBE")
end

if not has_config("custom_content") then
    add_defines("REPLAY_DISABLE_CUSTOM_CONTENT")
end

local MODE_DEBUG = is_mode("debug")
local CFG = MODE_DEBUG and "Debug" or "Release"

-- The Island Release probe can exhaust C: if test targets continue writing
-- objects there while the main proxy DLL uses the external build root.
-- This is opt-in, preserving every ordinary local/CI output path.
local function island_test_output_dirs(test_name, old_targetdir, old_objectdir)
    local stage = os.getenv("IW8X_BUILD_STAGE_ROOT")
    if stage and stage ~= "" then
        set_targetdir(path.join(stage, "xmake-out", "tests", CFG))
        set_objectdir(path.join(stage, "xmake-obj", "tests", CFG, test_name))
    else
        set_targetdir(old_targetdir)
        set_objectdir(old_objectdir)
    end
end

-- Optional integration/fixture tools share the same external-volume policy,
-- with a distinct subfolder for loadable test plugins. Never change their
-- default layout unless the external stage environment is explicitly set.
local function island_extra_output_dirs(name, relative_targetdir, old_targetdir, old_objectdir)
    local stage = os.getenv("IW8X_BUILD_STAGE_ROOT")
    if stage and stage ~= "" then
        set_targetdir(path.join(stage, "xmake-out", relative_targetdir))
        set_objectdir(path.join(stage, "xmake-obj", "tests", CFG, name))
    else
        set_targetdir(old_targetdir)
        set_objectdir(old_objectdir)
    end
end

rule("replay.embedded_assets")
    before_build(function (target)
        import("core.project.depend")
        local inventory = path.join(os.projectdir(), "data/embedded_assets.inc")
        local inputs = {inventory}
        for asset in io.readfile(inventory):gmatch('EMBED_ASSET%(%d+,%s*"([^"]+)"') do
            table.insert(inputs, path.join(os.projectdir(), asset))
        end
        local object = target:objectfile("embedded_assets.rc")
        -- XMake's RC scanner tracks headers/icons/bitmaps, not RCDATA payloads.
        -- Force this resource object to rebuild when Lua or binary assets change.
        depend.on_changed(function () os.tryrm(object) end, {
            dependfile = target:dependfile(object .. ".inputs"),
            files = inputs, changed = target:is_rebuilt()
        })
    end)
rule_end()

rule("replay.backend_assets")
    before_build(function (target)
        import("core.project.depend")
        local object = target:objectfile("backend_assets.rc")
        depend.on_changed(function () os.tryrm(object) end, {
            dependfile = target:dependfile(object .. ".inputs"),
            files = {"backend_assets.rc", "data/playlists/unified_large_playlist_tu19.aggr.json",
                     "data/playlists/unified_large_playlist_tu27.aggr.json"},
            changed = target:is_rebuilt()
        })
    end)
rule_end()

-- The Island bundle's generated file list is #included from night_files.cpp.
-- XMake's native dependency scan can miss changes to this nested generated
-- include, leaving a DLL with stale hashes even after a successful link.
-- Recompile the owner object and fail the build if its embedded map hash
-- does not match the generated manifest.
rule("replay.island_manifest")
    before_build(function (target)
        import("core.project.depend")
        local source = "src/diagnostics/night/night_files.cpp"
        local object = target:objectfile(source)
        local inventory = path.join(os.projectdir(), "src/diagnostics/island/island_probe_files.inc")
        depend.on_changed(function () os.tryrm(object) end, {
            dependfile = target:dependfile(object .. ".island_inventory"),
            files = {inventory},
            changed = target:is_rebuilt()
        })
    end)
    after_build(function (target)
        local inventory = path.join(os.projectdir(), "src/diagnostics/island/island_probe_files.inc")
        local text = assert(io.readfile(inventory), "Missing generated Island manifest")
        local sha = text:match('"mp_sm_island_1%.ff",%s*%d+ull,%s*"([0-9a-fA-F]+)"')
        assert(sha and #sha == 64, "Island fastfile entry missing/invalid in manifest")
        -- io.readfile() performs text decoding; for a PE image use raw bytes.
        local stream = assert(io.open(target:targetfile(), "rb"), "Cannot inspect built proxy DLL")
        local binary = assert(stream:read("*a"), "Cannot read built proxy DLL")
        stream:close()
        assert(binary:find(sha:lower(), 1, true),
            "Stale compiled Island manifest: xmake build --rebuild mw120rproxy is required")
    end)
rule_end()

target("discord_rpc120")
    set_kind("static")
    set_default(false)
    set_runtimes(MODE_DEBUG and "MTd" or "MT")
    add_defines("DISCORD_WINDOWS", "NOMINMAX")
    add_includedirs("thirdparty/discord-rpc/include", {public = true})
    add_includedirs("thirdparty/rapidjson/include")
    add_files("thirdparty/discord-rpc/src/discord_rpc.cpp", "thirdparty/discord-rpc/src/rpc_connection.cpp",
              "thirdparty/discord-rpc/src/serialization.cpp", "thirdparty/discord-rpc/src/connection_win.cpp",
              "thirdparty/discord-rpc/src/discord_register_win.cpp")
    add_syslinks("advapi32", "psapi", {public = true})
target_end()

target("magma_backend120")
    set_kind("binary")
    set_default(false)
    add_rules("replay.embedded_assets", "replay.backend_assets")
    set_basename("MagmaBackend120")
    set_targetdir(path.join("xmake-out", "x64", CFG, "backend"))
    set_objectdir(path.join(".xmake", "obj", "backend", CFG))
    set_runtimes(MODE_DEBUG and "MTd" or "MT")
    add_defines("MAGMA_BACKEND_STANDALONE", "UNICODE", "_UNICODE", "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs(".", "src", "src/backend", "src/common", "src/engine", "src/auth", "src/auth/profile", "src/dedicated", "src/network",
                    "src/network/backend", "src/network/profile", "src/network/social", "src/network/commerce")
    add_files("src/backend/*.cpp", "src/network/match_http.cpp", "src/network/profile/profile_backend.cpp", "src/network/profile/settings_backend.cpp",
              "src/network/profile/progression_backend.cpp", "src/network/backend/tournament_backend.cpp", "src/network/commerce/store_backend.cpp",
              "src/common/embedded_assets.cpp", "embedded_assets.rc", "backend_assets.rc")
    add_cxxflags("/utf-8", "/bigobj", {tools = "cl"})
    add_syslinks("kernel32", "user32", "gdi32", "comctl32", "shell32", "bcrypt", "ws2_32", "winhttp")
    add_ldflags("/SUBSYSTEM:WINDOWS", {force = true})
target_end()

for _, variant in ipairs({"mw120rproxy", "mw120rproxy_radmin_host", "mw120rproxy_radmin_client", "iw8x_client"}) do
target(variant)
    add_deps("discord_rpc120")
    local client_only = variant == "iw8x_client"
    local flavor = client_only and "client" or (variant == "mw120rproxy" and "" or
                   (variant == "mw120rproxy_radmin_host" and "radmin-host" or "radmin-client"))
    if variant == "mw120rproxy" and has_config("island_port_probe") then
        flavor = "island-probe"
        -- Do not link dedicated runtime hooks into standalone gsc_core_tests.
        add_defines("IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE")
        add_defines("IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE")
    end
    if flavor ~= "" then
        set_default(false)
        if flavor ~= "island-probe" then
            add_defines(client_only and "IW8X_CLIENT_ONLY" or
                        (flavor == "radmin-client" and "REPLAY_REMOTE_CLIENT" or "REPLAY_RADMIN_HOST"))
        end
    end
    add_rules("replay.embedded_assets")
    if variant == "mw120rproxy" and has_config("island_port_probe") then
        add_rules("replay.island_manifest")
    end
    set_kind("shared")
    set_basename("XInput9_1_0")
    set_prefixname("")

    -- Keep XMake products apart from the existing Visual Studio x64 directory.
    -- C: may be nearly full during large original-fastfile analysis. Opt in
    -- to an external disk for the large proxy DLL intermediates/results,
    -- without disturbing existing C: build products or installed artifacts.
    local external_stage = os.getenv("IW8X_BUILD_STAGE_ROOT")
    if external_stage and external_stage ~= "" then
        local variant_dir = flavor == "" and "default" or flavor
        set_targetdir(path.join(external_stage, "xmake-out", "x64", CFG, variant_dir))
        set_objectdir(path.join(external_stage, "xmake-obj", CFG, variant_dir))
    else
        set_targetdir(path.join(os.projectdir(), "xmake-out", "x64", CFG, flavor))
        set_objectdir(path.join(os.projectdir(), ".xmake", "obj", CFG, flavor))
    end

    add_defines("WIN32", "_WINDOWS", "_USRDLL", "NOMINMAX", "WIN32_LEAN_AND_MEAN",
                "_CRT_SECURE_NO_WARNINGS")
    add_syslinks("kernel32", "user32", "bcrypt", "ws2_32", "iphlpapi", "winhttp")
    add_cxflags("/utf-8", "/permissive-", "/sdl", "/W3", "/bigobj", {tools = "cl"})
    add_shflags("/DEF:XInput9_1_0.def", "/SUBSYSTEM:WINDOWS", {force = true})
    add_includedirs(
        ".",
        "src",
        "src/common",
        "src/engine",
        "src/bootstrap",
        "src/auth", "src/auth/profile",
        "src/client", "src/client/lua",
        "src/dedicated", "src/dedicated/assets", "src/dedicated/bots",
        "src/dedicated/headless",
        "src/network",
        "src/diagnostics",
        "src/proxy",
        "src/client/frontend", "src/client/weapons", "src/network/backend", "src/network/profile", "src/network/social", "src/network/commerce", "src/diagnostics/night", "src/diagnostics/escape", "src/diagnostics/weapons", "src/gsc/compiled/compat")

    if MODE_DEBUG then
        set_runtimes("MTd")
        add_defines("_DEBUG")
        set_symbols("debug")
    else
        set_runtimes("MT")
        add_defines("NDEBUG")
        add_cxflags("/Gy", "/Oi", {tools = "cl"})
        add_shflags("/OPT:REF", "/OPT:ICF", "/Brepro", "/emittoolversioninfo:no", {force = true})
        set_symbols("debug")
        -- An explicitly opted-in diagnostic build can avoid generating a
        -- large linker PDB when the machine is low on disk space. Normal
        -- release and CI builds keep the existing debug-symbol behavior.
        if os.getenv("IW8X_LINK_WITHOUT_PDB") == "1" then
            add_shflags("/DEBUG:NONE", {force = true})
        end
    end

    -- Minimal LAN/offline runtime: XInput forwarding, auth, fence dvars,
    -- console logging and the hook bootstrap they require.
    add_files("src/common/*.cpp")
    add_files("src/engine/*.cpp")
    add_files("src/bootstrap/*.cpp", "src/tlsdll/*.cpp", "src/client/loader/*.cpp", "src/client/component/*.cpp", "src/common/utils/*.cpp")
    add_files("src/gsc/*.cpp", "src/gsc/internal/*.cpp", "src/gsc/compiled/*.cpp", "src/gsc/compiled/*.asm")
    add_files("src/plugin/loader.cpp", "src/plugin/manifest.cpp", "src/common/commands/command.cpp")
    add_files("src/auth/*.cpp", "src/auth/profile/*.cpp")
    add_files("src/client/*.cpp", "src/client/lua/*.cpp", "src/client/console/*.cpp")
    if not client_only then
        add_files("src/dedicated/*.cpp", "src/dedicated/runtime/*.cpp", "src/dedicated/network/*.cpp", "src/dedicated/assets/*.cpp", "src/dedicated/assets/bap/*.cpp")
        add_files("src/dedicated/bots/*.cpp|bot_nav_gpu.cpp|bot_nav_compute.cpp")
        -- The optional navigation backend scans real graph snapshots on the server
        -- thread. Compile these measured hot loops with optimization; keep unrelated
        -- reverse-engineered hooks on their existing build settings.
        if MODE_DEBUG then
            add_files("src/dedicated/bots/bot_nav_gpu.cpp", "src/dedicated/bots/bot_nav_compute.cpp")
        else
            add_files("src/dedicated/bots/bot_nav_gpu.cpp", "src/dedicated/bots/bot_nav_compute.cpp",
                      {cxflags = {"/O2", "/fp:precise"}})
        end
        add_files("src/dedicated/headless/*.cpp")
    end
    add_files("src/network/*.cpp", "src/network/services/*.cpp")
    add_files("src/diagnostics/*.cpp")
    if has_config("island_port_probe") then
        -- Preserve native Island file/asset handling, but do not compile the
        -- experimental DX12 SetupPass/ExecuteIndirect hooks into the proxy.
        add_files("src/diagnostics/island/*.cpp|island_depth_quickfix.cpp|island_render_formats.cpp|island_renderer_adapter.cpp")
    end
    add_files("src/proxy/*.cpp")
    add_files("src/client/frontend/*.cpp|frontend_animtree128.cpp|frontend_materials.cpp|frontend_render128.cpp",
              "src/client/weapons/*.cpp",
              "src/network/social/*.cpp", "src/network/social/runtime/*.cpp", "src/network/commerce/*.cpp",
              "src/diagnostics/weapons/*.cpp")
    if client_only then
        add_files("src/network/backend/*.cpp|backend_agent.cpp")
    else
        add_files("src/network/backend/*.cpp", "src/network/profile/*.cpp")
    end
    add_files("src/diagnostics/night/*.cpp|night_geometry.cpp|night_light_bits.cpp|night_frame_lights.cpp",
              "src/diagnostics/escape/*.cpp")
    if client_only then
        add_files("src/dedicated/assets/bap/collision.cpp")
    end
    if has_config("custom_content") then
        add_files("src/client/frontend/frontend_animtree128.cpp", "src/client/frontend/frontend_materials.cpp",
                  "src/client/frontend/frontend_render128.cpp", "src/diagnostics/night/night_geometry.cpp",
                  "src/diagnostics/night/night_light_bits.cpp", "src/diagnostics/night/night_frame_lights.cpp",
                  "src/gsc/compiled/compat/*.cpp")
    end
    add_files("embedded_assets.rc")
    add_files("thirdparty/minhook/src/*.c", "thirdparty/minhook/src/hde/hde64.c")

target_end()
end

target("island_br_dvar_policy_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("island-br-dvar-policy-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "island-br-dvar-policy-tests"))
    set_runtimes("MT")
    add_includedirs("src")
    add_files("src/auth/tests/island_br_dvar_policy.cpp")
    add_cxflags("/permissive-", "/W3", {tools = "cl"})
target_end()

target("island_original_spawn_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("island-original-spawn-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "island-original-spawn-tests"))
    set_runtimes("MT")
    add_includedirs("src")
    add_files("src/dedicated/runtime/tests/island_original_prematch_spawn.cpp")
    add_cxflags("/permissive-", "/W3", {tools = "cl"})
target_end()

target("island_plunder_loadout_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("island-plunder-loadout-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "island-plunder-loadout-tests"))
    set_runtimes("MT")
    add_includedirs("src")
    add_files("src/dedicated/runtime/tests/island_plunder_loadout_policy.cpp")
    add_cxflags("/permissive-", "/W3", {tools = "cl"})
target_end()

target("island_native_loot_bounds_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("island-native-loot-bounds-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "island-native-loot-bounds-tests"))
    set_runtimes("MT")
    add_includedirs("src")
    add_files("src/dedicated/runtime/tests/island_seed_bounds.cpp")
    add_cxflags("/permissive-", "/W3", {tools = "cl"})
target_end()

target("custom_fastfile_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("custom-fastfile-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "custom-fastfile-tests"))
    set_runtimes("MT")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "REPLAY_DISABLE_CUSTOM_CONTENT", "IW8X_CLIENT_ONLY", "IW8X_ISLAND_PORT_PROBE")
    add_includedirs(".", "src", "src/common", "src/engine", "src/dedicated/assets", "src/diagnostics", "src/client/frontend")
    add_files("src/client/loader/tests/custom_fastfiles.cpp", "src/client/loader/custom_fastfiles.cpp",
              "src/diagnostics/weapons/weapon_files.cpp", "src/engine/inline_hook.cpp",
              "thirdparty/minhook/src/*.c", "thirdparty/minhook/src/hde/hde64.c")
    add_syslinks("kernel32", "bcrypt")
    add_cxflags("/utf-8", "/W3", "/bigobj", {tools = "cl"})
target_end()

target("bot_compute_bench")
    set_kind("binary")
    set_default(false)
    island_extra_output_dirs("bot-compute", path.join("tools", CFG),
                             path.join(os.projectdir(), "xmake-out", "tools"),
                             path.join(os.projectdir(), ".xmake", "obj", "bot-compute"))
    set_runtimes("MT")
    set_optimize("fastest")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_includedirs("thirdparty")
    add_files("tools/bot_compute_bench.cpp")
    add_syslinks("kernel32", "d3d12", "dxgi", "d3dcompiler")
    add_cxflags("/permissive-", "/W3", "/fp:precise", {tools = "cl"})

target("headless_software_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("software-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "software-tests"))
    set_runtimes("MT")
    set_optimize("fastest")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_includedirs("src", "src/common", "src/engine", "src/dedicated", "src/dedicated/assets", "src/dedicated/bots")
    add_files("src/dedicated/headless/tests/software.cpp", "src/dedicated/headless/software.cpp",
              "src/dedicated/headless/d3d.cpp", "src/dedicated/bots/bot_nav_compute.cpp")
    add_syslinks("kernel32")
    add_cxflags("/permissive-", "/W3", {tools = "cl"})

target("gsc_core_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("gsc-core-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "gsc-core-tests"))
    set_runtimes("MT")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs(".", "src")
    add_files("src/gsc/*.cpp", "src/gsc/tests/api.cpp", "src/gsc/tests/sdk_c.c",
              "src/plugin/loader.cpp",
              "src/plugin/manifest.cpp", "src/common/commands/command.cpp")
    add_syslinks("kernel32")
    add_deps("gsc_core_native_fixture", "gsc_core_failing_fixture")
    add_cxflags("/permissive-", "/W3", "/bigobj", {tools = "cl"})

target("gsc_core_native_fixture")
    set_kind("shared")
    set_default(false)
    set_basename("native_example")
    set_prefixname("")
    island_extra_output_dirs("gsc-native-fixture", path.join("tests", CFG, "plugins", "native"),
                             path.join(os.projectdir(), "xmake-out", "tests", "plugins", "native"),
                             path.join(os.projectdir(), ".xmake", "obj", "gsc-native-fixture"))
    set_runtimes("MT")
    add_includedirs("src")
    add_files("examples/native/plugin.cpp")
    after_build(function (target)
        os.cp("examples/native/plugin.json", path.join(target:targetdir(), "plugin.json"))
    end)

target("gsc_core_failing_fixture")
    set_kind("shared")
    set_default(false)
    set_basename("native_failing")
    set_prefixname("")
    island_extra_output_dirs("gsc-failing-fixture", path.join("tests", CFG, "plugins", "failing"),
                             path.join(os.projectdir(), "xmake-out", "tests", "plugins", "failing"),
                             path.join(os.projectdir(), ".xmake", "obj", "gsc-failing-fixture"))
    set_runtimes("MT")
    add_includedirs("src")
    add_files("src/plugin/tests/failing.cpp")
    after_build(function (target)
        os.cp("src/plugin/tests/failing.json", path.join(target:targetdir(), "plugin.json"))
    end)

target("gsc_invocation_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("gsc-invocation-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "gsc-invocation-tests"))
    set_runtimes("MT")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs(".", "src")
    add_files("src/gsc/*.cpp", "src/gsc/internal/vm.cpp", "src/gsc/internal/dvars.cpp",
              "src/gsc/internal/entities.cpp", "src/gsc/internal/tables.cpp",
              "src/gsc/internal/player_adapter.cpp", "src/gsc/internal/chat.cpp",
              "src/gsc/tests/invocation.cpp", "src/common/commands/command.cpp", "src/engine/inline_hook.cpp")
    add_files("thirdparty/minhook/src/*.c", "thirdparty/minhook/src/hde/hde64.c")
    add_syslinks("kernel32")
    add_cxflags("/permissive-", "/W3", "/bigobj", {tools = "cl"})

-- Optional compile-only gate for the modules also linked into mw120rproxy.
target("gsc_runtime_port")
    set_kind("static")
    set_default(false)
    island_test_output_dirs("gsc-runtime-port", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "gsc-runtime-port"))
    set_runtimes("MT")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs(".", "src")
    add_files("src/gsc/*.cpp", "src/gsc/internal/*.cpp", "src/gsc/compiled/*.cpp", "src/gsc/compiled/*.asm", "src/plugin/loader.cpp", "src/plugin/manifest.cpp", "src/common/commands/command.cpp")
    add_cxflags("/permissive-", "/W3", "/bigobj", {tools = "cl"})

for _, test_name in ipairs({"gsc_loader_tests", "gsc_island_loader_tests"}) do
target(test_name)
    set_kind("binary")
    set_default(false)
    island_test_output_dirs(test_name, path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", test_name))
    set_runtimes("MT")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs(".", "src")
    add_files("src/gsc/compiled/tests/loader.cpp", "src/gsc/compiled/link_diagnostics.cpp",
              "src/gsc/compiled/error_trace.asm", "src/engine/inline_hook.cpp")
    add_files("thirdparty/minhook/src/*.c", "thirdparty/minhook/src/hde/hde64.c")
    if test_name == "gsc_island_loader_tests" then
        add_defines("IW8X_ISLAND_PORT_PROBE")
        add_files("src/diagnostics/island/island_loot.cpp")
		add_files("src/gsc/compiled/island_armor_native_abi.cpp", "src/gsc/compiled/island_armor_switch_native_abi.cpp")
		add_files("src/gsc/compiled/island_damage_native_abi.cpp")
        add_files("src/gsc/compiled/island_plunder_validation_native_abi.cpp")
		add_files("src/gsc/compiled/island_plunder_menus_native_abi.cpp")
		add_files("src/gsc/compiled/island_plunder_class_native_abi.cpp")
        add_files("src/gsc/compiled/island_killstreak_native_abi.cpp")
        add_files("src/gsc/compiled/island_vehicle_native_abi.cpp")
    else
        add_undefines("IW8X_ISLAND_PORT_PROBE")
    end
    add_syslinks("kernel32")
    add_cxflags("/permissive-", "/W3", "/bigobj", {tools = "cl"})
target_end()
end

-- Build the existing examples against the Replay SDK into an isolated tree.
-- These targets never discover, install, or execute a plugin in the game.
for _, example in ipairs({"native", "chat", "gsc_gameplay", "player_events", "snipers_only",
                           "restart_on_shutdown", "plugin_api_acceptance", "discord_webhook"}) do
    target("replay_plugin_" .. example)
        set_kind("shared")
        set_default(false)
        local named_example = example == "native" or example == "chat" or
                              example == "gsc_gameplay" or example == "player_events"
        set_basename(named_example and example .. "_example" or example)
        set_prefixname("")
        set_targetdir(path.join(os.projectdir(), "xmake-out", "examples", example))
        set_objectdir(path.join(os.projectdir(), ".xmake", "obj", "replay-plugin-" .. example))
        set_runtimes("MT")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
        add_includedirs("src")
        add_files("examples/" .. example .. "/plugin.cpp")
        if example == "discord_webhook" then add_syslinks("winhttp") end
        after_build(function (target)
            local name = target:name():sub(#"replay_plugin_" + 1)
            os.cp("examples/" .. name .. "/plugin.json", path.join(target:targetdir(), "plugin.json"))
        end)
end

target("loadout_script")
    set_kind("shared")
    set_default(false)
    set_prefixname("")
    set_targetdir(path.join(os.projectdir(), "xmake-out", "plugins", "loadout_script"))
    set_objectdir(path.join(os.projectdir(), ".xmake", "obj", "loadout-script"))
    set_runtimes("MT")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_includedirs("src")
    add_files("plugins/loadout_script/plugin.cpp")
    after_build(function (target)
        os.cp("plugins/loadout_script/plugin.json", path.join(target:targetdir(), "plugin.json"))
    end)

target("replay_plugins")
    set_kind("phony")
    set_default(false)
    add_deps("replay_plugin_native", "replay_plugin_chat", "replay_plugin_gsc_gameplay",
             "replay_plugin_player_events", "replay_plugin_snipers_only", "replay_plugin_restart_on_shutdown",
             "replay_plugin_plugin_api_acceptance", "replay_plugin_discord_webhook")

target("gsc_bootstrap_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("gsc-bootstrap-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "gsc-bootstrap-tests"))
    set_runtimes("MT")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs(".", "src", "src/engine")
    add_files("src/gsc/tests/bootstrap.cpp", "src/common/commands/command.cpp")
    add_syslinks("kernel32")
    add_cxflags("/permissive-", "/W3", {tools = "cl"})

target("gsc_restart_tests")
    set_kind("binary")
    set_default(false)
    island_test_output_dirs("gsc-restart-tests", path.join(os.projectdir(), "xmake-out", "tests"),
                            path.join(os.projectdir(), ".xmake", "obj", "gsc-restart-tests"))
    set_runtimes("MT")
    add_includedirs("src")
    add_files("src/gsc/tests/restart.cpp", "examples/restart_on_shutdown/plugin.cpp")

-- Opt-in probe is compiled only; never copied to an active game plugin folder.
target("gsc_live_probe")
    set_kind("shared")
    set_default(false)
    island_extra_output_dirs("gsc-live-probe", path.join("manual-probe", CFG),
                             path.join(os.projectdir(), "xmake-out", "manual-probe"),
                             path.join(os.projectdir(), ".xmake", "obj", "gsc-live-probe"))
    set_runtimes("MT")
    add_includedirs("src")
    add_files("src/gsc/tests/live_probe.cpp")

target("merge_feature_tests")
    set_kind("binary")
    set_default(false)
    set_runtimes("MT")
    island_test_output_dirs("merge-feature-tests", "xmake-out/tests", ".xmake/obj/merge-feature-tests")
    add_includedirs(".", "src")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_cxxflags("/utf-8", "/bigobj", {tools = "cl"})
    add_files("src/gsc/tests/merge_features.cpp", "src/engine/inline_hook.cpp", "backend_assets.rc")
    add_rules("replay.backend_assets")
    add_files("thirdparty/minhook/src/*.c", "thirdparty/minhook/src/hde/hde64.c")
    add_syslinks("kernel32", "bcrypt")

target("backend_merge_tests")
    set_kind("binary")
    set_default(false)
    set_runtimes("MT")
    island_test_output_dirs("backend-merge-tests", "xmake-out/tests", ".xmake/obj/backend-merge-tests")
    add_defines("MAGMA_BACKEND_STANDALONE", "UNICODE", "_UNICODE", "NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
    add_includedirs(".", "src", "src/backend", "src/common", "src/engine", "src/auth", "src/auth/profile", "src/dedicated", "src/network",
                    "src/network/backend", "src/network/profile", "src/network/social", "src/network/commerce")
    add_files("src/backend/runtime.cpp", "src/backend/services.cpp", "src/backend/playlists.cpp", "src/backend/tests/services.cpp",
              "src/network/match_http.cpp", "src/network/profile/profile_backend.cpp", "src/network/profile/settings_backend.cpp",
              "src/network/profile/progression_backend.cpp", "src/network/backend/tournament_backend.cpp", "src/network/commerce/store_backend.cpp",
              "src/common/embedded_assets.cpp", "embedded_assets.rc", "backend_assets.rc")
    add_rules("replay.embedded_assets", "replay.backend_assets")
    add_cxxflags("/utf-8", "/bigobj", {tools = "cl"})
    add_syslinks("kernel32", "user32", "gdi32", "comctl32", "shell32", "bcrypt", "ws2_32", "winhttp")


target("production_runtime_tests")
    set_kind("binary")
    set_default(false)
    set_runtimes("MT")
    set_optimize("fastest")
    island_test_output_dirs("production-runtime-tests", "xmake-out/tests", ".xmake/obj/production-runtime-tests")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_includedirs(".", "src")
    add_files("src/client/tests/runtime.cpp", "src/client/loader/component_loader.cpp",
              "src/client/component/scheduler.cpp", "src/common/utils/thread_pool.cpp", "src/engine/runtime_signatures.cpp")
    add_syslinks("kernel32", "bcrypt")
    add_cxxflags("/utf-8", "/W3", "/bigobj", {tools = "cl"})
target_end()


target("client_match_capacity_tests")
    set_kind("binary")
    set_default(false)
    set_runtimes("MT")
    island_test_output_dirs("client-match-capacity-tests", "xmake-out/tests", ".xmake/obj/client-match-capacity-tests")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_includedirs(".", "src")
    add_files("src/client/tests/match_client_capacity.cpp", "src/client/match_client_capacity.cpp")
    add_syslinks("kernel32")
    add_cxxflags("/utf-8", "/W3", "/bigobj", {tools = "cl"})
target_end()


target("production_exit_tests")
    set_kind("binary")
    set_default(false)
    set_runtimes("MT")
    island_test_output_dirs("production-exit-tests", "xmake-out/tests", ".xmake/obj/production-exit-tests")
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
    add_includedirs(".", "src")
    add_files("src/client/tests/exit.cpp", "src/client/loader/component_loader.cpp",
              "src/client/component/system.cpp", "src/client/component/scheduler.cpp", "src/common/utils/thread_pool.cpp")
    add_syslinks("kernel32")
    add_cxxflags("/utf-8", "/W3", "/bigobj", {tools = "cl"})
target_end()

-- Optional preserved-source control for identical before/after plugin benchmarks.
local baseline_root = path.absolute("../../backups/production-optimization-20261011")
if os.isdir(path.join(baseline_root, "src")) then
    target("production_gsc_baseline_bench")
        set_kind("binary")
        set_default(false)
        set_targetdir("xmake-out/tests")
        set_objectdir(".xmake/obj/production-gsc-baseline")
        set_runtimes("MT")
        add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "_CRT_SECURE_NO_WARNINGS")
        add_includedirs(baseline_root, path.join(baseline_root, "src"), ".", "src")
        add_files(path.join(baseline_root, "src/gsc/*.cpp"), path.join(baseline_root, "src/gsc/tests/api.cpp"),
                  path.join(baseline_root, "src/gsc/tests/sdk_c.c"), path.join(baseline_root, "src/plugin/loader.cpp"),
                  path.join(baseline_root, "src/plugin/manifest.cpp"), path.join(baseline_root, "src/common/commands/command.cpp"))
        add_syslinks("kernel32")
        add_deps("gsc_core_native_fixture", "gsc_core_failing_fixture")
        add_cxxflags("/permissive-", "/W3", "/bigobj", {tools = "cl"})
    target_end()
end
