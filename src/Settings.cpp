// Illuminated Anima - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// IlluminatedAnima\Settings.ini, written by the menu (settings live in the SKSE menu only): the live table (AnimaMath.h's
// Settings, read and written by the game-free reader and writer the off-game tests run) under one lock.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/IlluminatedAnima/Settings.ini";

		std::mutex      gLock;
		Anima::Settings gS;  // under gLock

		// clamped in the menu's steps of 5, whoever set it - the menu or the file; under gLock
		void Clamp(Anima::Settings& a_s)
		{
			a_s.brightness = std::clamp((std::clamp(a_s.brightness, Anima::kBrightnessMin, Anima::kBrightnessMax) + 2) / 5 * 5, Anima::kBrightnessMin, Anima::kBrightnessMax);
			a_s.reach = std::clamp((std::clamp(a_s.reach, Anima::kReachMin, Anima::kReachMax) + 2) / 5 * 5, Anima::kReachMin, Anima::kReachMax);
		}
	}

	void LoadSettings()
	{
		std::ifstream   in(kPath);
		Anima::Settings read;
		const auto      keys = Anima::ReadSettings(in, read);
		{
			std::scoped_lock l{ gLock };
			gS = read;
			Clamp(gS);
		}
		const auto  s = SettingsNow();
		std::string kinds;
		for (std::size_t i = 0; i < Anima::kKinds; ++i) {
			kinds += std::format("{}{} {}", i ? ", " : "", Anima::kKindNames[i], s.kinds[i] ? "on" : "off");
		}
		SKSE::log::info("settings: lights {}, {}, natural elements {}, brightness {}%, reach {}%, flicker {}, death fade {}, cast surge {} ({} key(s) read)",
			s.on ? "on" : "off", kinds, s.natural ? "on" : "off", s.brightness, s.reach, s.flicker ? "on" : "off", s.deathFade ? "on" : "off",
			s.surge ? "on" : "off", keys);
	}

	// written beside the file and moved over it: a crash or a full disk mid-write leaves the old file, never half of one
	bool SaveSettings()
	{
		static std::mutex  writing;
		std::scoped_lock   w{ writing };
		std::ostringstream out;
		{
			std::scoped_lock l{ gLock };
			Anima::WriteSettings(gS, out);
		}
		const std::string tmp = std::string(kPath) + ".tmp";
		std::error_code   ec;
		std::filesystem::create_directories(std::filesystem::path(kPath).parent_path(), ec);
		{
			std::ofstream file(tmp, std::ios::trunc);
			file << out.str();
			file.close();
			if (!file) {
				SKSE::log::warn("settings: {} could not be written", kPath);
				std::filesystem::remove(tmp, ec);
				return false;
			}
		}
		if (std::filesystem::rename(tmp, kPath, ec); ec) {
			SKSE::log::warn("settings: {} could not be replaced ({})", kPath, ec.message());
			std::filesystem::remove(tmp, ec);
			return false;
		}
		return true;
	}

	Anima::Settings SettingsNow()
	{
		std::scoped_lock l{ gLock };
		return gS;
	}

	void UpdateSettings(const std::function<void(Anima::Settings&)>& a_change)
	{
		std::scoped_lock l{ gLock };
		a_change(gS);
		Clamp(gS);
	}
}
