-- Illuminated - Creature Lighting NG - SKSE plugin. GPL-3.0-or-later, see LICENSE.txt.
set_xmakever("3.0.0")
set_project("IlluminatedCreatureLightingNG")
set_version("1.1.0")
set_license("GPL-3.0-or-later")
set_arch("x64")
set_languages("c++23")
-- the DLL carries its own Visual C++ runtime, so an older runtime on a player's PC cannot stop it loading
set_runtimes("MT")
add_rules("mode.releasedbg")
set_defaultmode("releasedbg")

-- GLOW_VR=1 (set by PC Runner\pluginbuild.py) builds for Skyrim VR only; anything else is the SE + AE build
local vr_only = os.getenv("GLOW_VR") == "1"
set_config("skyrim_se", not vr_only)
set_config("skyrim_ae", not vr_only)
set_config("skyrim_vr", vr_only)

-- REPRODUCIBLE BUILDS: the same source gives the same DLL, byte for byte, in whatever folder it is built (as every plugin of
-- ours: /Brepro, /pathmap, /PDBALTPATH - see Dynamic Wards' xmake.lua). MSVC only.
add_cxflags("/Brepro", "/experimental:deterministic", "/pathmap:" .. os.projectdir() .. "=.", { force = true, tools = "cl" })
add_shflags("/Brepro", "/PDBALTPATH:%_PDB%", { force = true, tools = "link" })

includes("lib/commonlibsse-ng")

-- the other creature lighting mods' Light Placer configs are JSON (MIT-licensed library; the version CommonLib pins for its own option)
add_requires("nlohmann_json v3.12.0")
target("IlluminatedCreatureLightingNG", function()
    add_deps("commonlibsse-ng")
    add_packages("nlohmann_json")
    add_defines("NOMINMAX")  -- windows.h min/max macros break std::min/max and numeric_limits::max
    add_rules("commonlibsse-ng.plugin", {
        name = "IlluminatedCreatureLightingNG",
        author = "izzydoingit",
        description = "Illuminated - Creature Lighting NG - creatures glow in their own light, on Community Shaders, ENB and Vanilla",
    })
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
    -- every warning in OUR files is an error (his C++ standard: "/W4 /WX ... fix all warnings")
    set_warnings("allextra", "error")
end)

-- the game-free part (src/AnimaMath.h), tested without the game: xmake build testanima && xmake run testanima
-- (PC Runner\plugin build.py builds and runs it, then again under clang's address + undefined-behavior checkers)
target("testanima", function()
    set_kind("binary")
    set_default(false)
    add_files("tests/testanima.cpp")
    add_includedirs("src")
    set_warnings("allextra", "error")
end)
