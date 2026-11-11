// Illuminated - Creature Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Which lighting the game draws with - Illuminated's Lighting.cpp (Soul Gem Hub's copy of it),
// looked at once, at data load, never picked by the player:
//   Community Shaders   CommunityShaders.dll loaded (Bottled Shaders and Open Shaders keep the name). Inverse square only where a shader
//                       draws it (the shared `LightKit::Isl::DrawnByShaders`: its add-on, or Light Limit Fix naming it): there a light's
//                       reach comes from its fade and its cutoff; an older one without either draws every light linear, its radius
//                       straight from the light, and Light Limit Fix still reads its first words, so a plain light carries its own
//                       linear words there
//   ENB                 } the game's own lighting: a light reaches its radius, drawn plain
//   Vanilla             }
// Leftover ENB settings never make it ENB while Community Shaders runs (RELight 2.1's report, Illuminated's rule), and ENB's settings
// files alone do not make it ENB: an ENB preset left in the game folder with no ENB running is Vanilla lighting. Only when the
// loaded modules cannot be listed do the settings files decide (and the log says so).

#include "Plugin.h"

#include <Windows.h>
#include <TlHelp32.h>

namespace Plugin
{
	namespace
	{
		constexpr const char* kEnbFiles[] = { "enbseries.ini", "enblocal.ini" };

		std::atomic<Lighting> gFound{ Lighting::kShaders };
		std::atomic<bool>     gLightLimitFix{ false };  // CommunityShaders.dll loaded: Light Limit Fix reads every light's first words
		std::atomic<bool>     gInverseSquare{ false };  // ... and a shader of it draws inverse square

		[[nodiscard]] bool EnbFilesLeft()
		{
			std::error_code ec;
			const auto      root = std::filesystem::current_path(ec);  // the non-throwing form
			return !ec && std::ranges::any_of(kEnbFiles, [&](const char* a_file) { return std::filesystem::exists(root / a_file, ec); });
		}

		[[nodiscard]] bool CommunityShadersLoaded() { return REX::W32::GetModuleHandleA("CommunityShaders.dll") != nullptr; }

		// an ENB that is running: a loaded module exports its SDK - d3d11.dll as usual, or any other name an injector gave it;
		// nullopt when the module list could not be read, so the caller falls back to ENB's settings files
		[[nodiscard]] std::optional<bool> EnbRunning()
		{
			for (const char* dll : { "d3d11.dll", "dxgi.dll", "d3d9.dll" }) {
				if (const auto handle = REX::W32::GetModuleHandleA(dll); handle && REX::W32::GetProcAddress(handle, "ENBGetSDKVersion")) {
					return true;
				}
			}
			// a module loading at that moment makes the snapshot fail with ERROR_BAD_LENGTH; Windows asks for a retry
			auto snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
			for (int tries = 0; snap == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_BAD_LENGTH && tries < 8; ++tries) {
				snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
			}
			if (snap == INVALID_HANDLE_VALUE) {
				return std::nullopt;
			}
			MODULEENTRY32W entry{};
			entry.dwSize = sizeof(entry);
			if (!::Module32FirstW(snap, &entry)) {
				::CloseHandle(snap);
				return std::nullopt;
			}
			bool found = false;
			do {
				// a counted reference: a module unloaded after the snapshot cannot be read through a stale handle
				HMODULE module = nullptr;
				if (::GetModuleHandleExW(0, entry.szModule, &module)) {
					found = ::GetProcAddress(module, "ENBGetSDKVersion") != nullptr;
					::FreeLibrary(module);
				}
			} while (!found && ::Module32NextW(snap, &entry));
			::CloseHandle(snap);
			return found;
		}
	}

	void ReadLighting()
	{
		const auto drawn = LightKit::Isl::DrawnByShaders();
		const bool shaderFile = drawn.addOn;
		const bool inLlf = drawn.inLightLimitFix;
		const bool csLoaded = CommunityShadersLoaded();
		gLightLimitFix = csLoaded;
		gInverseSquare = csLoaded && drawn.Any();
		const bool enbFiles = EnbFilesLeft();
		const auto running = EnbRunning();
		const bool enbRunning = running.value_or(enbFiles);
		if (!running) {
			SKSE::log::warn("lighting: the loaded modules could not be listed - ENB judged by its settings files");
		}
		gFound = csLoaded ? Lighting::kShaders : enbRunning ? Lighting::kEnb :
		                                                      Lighting::kVanilla;
		// the shader files alone are a leftover when no Community Shaders runs (a hand removal), as ENB's settings files are: only logged
		SKSE::log::info(
			"lighting: {} (detected - CommunityShaders.dll {}, inverse square shader {}, in Light Limit Fix {}, ENB running {}, ENB "
			"settings in the game folder {}{}); lights drawn {}",
			LightingName(gFound), csLoaded ? "loaded" : "not loaded", shaderFile ? "installed" : "not installed", inLlf ? "yes" : "no",
			enbRunning ? "yes" : "no", enbFiles ? "yes" : "no", enbFiles && !enbRunning && !csLoaded ? " - left over, not used" : "",
			InverseSquare() ? "inverse square" : "plain");
	}

	Lighting LightingFound()
	{
		return gFound.load();
	}

	const char* LightingName(Lighting a_lighting)
	{
		switch (a_lighting) {
		case Lighting::kEnb:
			return "ENB";
		case Lighting::kVanilla:
			return "Vanilla";
		default:
			return "Community Shaders";
		}
	}

	bool InverseSquare()
	{
		return gInverseSquare.load();
	}

	bool LightLimitFix()
	{
		return gLightLimitFix.load();
	}
}
