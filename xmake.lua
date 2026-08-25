set_optimize("smallest")
add_cxflags("/Gw", "/Gy")
add_ldflags("/DEBUG:FULL", "/OPT:REF", "/OPT:ICF", { force = true })

set_project("EnhancedConsoleSSE")
set_version("0.1.0")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

option("devlog")
    set_default(false)
    set_showmenu(true)
option_end()

includes("../CommonLibSSE-NG")

if is_mode("releasedbg") and not has_config("devlog") then
    local no_log_include = path.absolute("compat/commonlibsse-ng/include")

    target("commonlibsse-ng")
        remove_files("../CommonLibSSE-NG/src/SKSE/Logger.cpp")
        add_files("compat/commonlibsse-ng/src/SKSE/Logger.cpp")
        add_includedirs("compat/commonlibsse-ng/include", {
            public = true,
            before = true
        })
        after_load(function (target)
            local includedirs = { no_log_include }
            for _, includedir in ipairs(target:get("includedirs") or {}) do
                if path.absolute(includedir) ~= no_log_include then
                    table.insert(includedirs, includedir)
                end
            end
            target:set("includedirs", includedirs)
        end)
    target_end()
end

target("EnhancedConsoleSSE")
    set_kind("shared")
    set_arch("x64")
    add_deps("commonlibsse-ng")
    if is_mode("releasedbg") and not has_config("devlog") then
        add_includedirs("compat/commonlibsse-ng/include", { before = true })
    end
    add_files("src/**.cpp")
    add_files("src/version.rc")
    add_files("src/vendor/miniz.c")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

target("EnhancedConsoleSSETests")
    set_kind("binary")
    set_default(false)
    add_undefines("NDEBUG")
    add_files("tests/test_core.cpp", "src/Core.cpp")
    add_includedirs("src")

target("EnhancedConsoleSSEScannerTests")
    set_kind("binary")
    set_default(false)
    add_undefines("NDEBUG")
    add_files("tests/test_espscan.cpp", "src/EspScan.cpp", "src/vendor/miniz.c")
    add_includedirs("src")
