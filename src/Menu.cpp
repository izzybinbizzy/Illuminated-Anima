// Illuminated - Creature Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The SKSE Menu Framework page - every setting this mod has (settings live in the SKSE menu only). One page: what was found,
// the lights' switch, the kinds, the one switch for elements that should not glow, the lights of their own switch, Brightness, Reach, the three ways a
// light moves, a checkbox per creature, and Other Mods' Lights - one switch per other creature lighting mod that is installed.
// A change is saved and applied on the game's main thread when it is let go.

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX  // the build defines it too (xmake.lua)
#endif
#include "Plugin.h"

// SKSE Menu Framework's own header (theirs, MIT): its warnings are not ours, and ours are errors (xmake.lua) - under MSVC and
// under clang-cl, which checks every file too
#pragma warning(push, 0)
#if defined(__clang__)
#	pragma clang diagnostic push
#	pragma clang diagnostic ignored "-Weverything"
#endif
#include "SKSEMenuFramework.h"
#if defined(__clang__)
#	pragma clang diagnostic pop
#endif
#pragma warning(pop)
#include "MenuStyle.h"
#include "Translation.h"

// a line Translation.json carries though it is not written inside T( ) here (Soul Gem Hub's mark, read by translation file.py)
#define TR_MARK(x) x

namespace Plugin
{
	namespace
	{
		using namespace ImGuiMCP;
		using MenuStyle::Header;
		using Translation::T;
		namespace Icon = MenuStyle::Icon;

		// the menu draws on the RENDER thread: the save and the lights follow in ONE task on the main thread
		void Changed()
		{
			static std::atomic<bool> queued{ false };
			if (queued.exchange(true)) {
				return;
			}
			if (!Later("menu change", []() {
					queued = false;
					SaveSettings();
					ApplyNow();
				})) {
				queued = false;
				SKSE::log::warn("menu: the change could not reach the main thread - it is kept for this session but not saved");
			}
		}

		// a checkbox: true when ticked or unticked now (the caller writes the setting, then Changed() saves and applies it)
		[[nodiscard]] bool Switch(const char* a_label, bool& a_value, const char* a_tip)
		{
			const bool moved = Checkbox(a_label, &a_value);
			MenuStyle::Tip(a_tip);
			return moved;
		}

		// while a slider moves: the lights follow at once, at most one task a frame; the file is saved when it is let go
		void AppliedLive()
		{
			static std::atomic<bool> queued{ false };
			if (!queued.exchange(true) && !Later("menu slider", []() {
					queued = false;
					ApplyNow(false);
				})) {
				queued = false;
			}
		}

		// a percent slider in steps of 5: live while dragged, saved when let go
		void Percent(const char* a_label, int Anima::Settings::* a_field, int a_min, int a_max, const char* a_tip)
		{
			const int current = SettingsNow().*a_field;
			int       v = current;
			SetNextItemWidth(260.0f);
			if (SliderInt(a_label, &v, a_min, a_max, "%d%%")) {
				v = Anima::SnapPercent(v, a_min, a_max);
				// a drag that snaps back to the stored step changes nothing, so the lights are not dropped and picked again for it
				if (v != current) {
					UpdateSettings([&](Anima::Settings& a_s) { a_s.*a_field = v; });
					AppliedLive();
				}
			}
			if (IsItemDeactivatedAfterEdit()) {
				Changed();
			}
			MenuStyle::Tip(a_tip);
		}

		// a switch bound to one settings field: read now, written under the settings lock
		void Field(const char* a_label, bool Anima::Settings::* a_field, const char* a_tip)
		{
			bool v = SettingsNow().*a_field;
			if (Switch(a_label, v, a_tip)) {
				UpdateSettings([&](Anima::Settings& a_s) { a_s.*a_field = v; });
				Changed();
			}
		}

		void RenderSettings()
		{
			const MenuStyle::Page page;
			Header(Icon::kBulb, T("Illuminated - Creature Lighting NG"));
			TextWrapped("%s", T("Creatures give off light: atronachs, wisps, spriggans, ice wraiths, ash spawn, chaurus, familiars and more, on "
								"Community Shaders, ENB and Vanilla."));
			MenuStyle::Note(Translation::Fill(T("Lighting found: {}. Creatures lit near you: {}."), LightingName(LightingFound()),
				std::to_string(LitCount()))
					.c_str());
			{
				const auto s = SettingsNow();
				for (std::size_t i = 0; i < Anima::kOthers; ++i) {
					if (!AlsoLitBy(i).empty() && s.theirs[i]) {
						PushStyleColor(ImGuiCol_Text, MenuStyle::kBad);
						TextWrapped("%s", T("Warning: another mod's lights replace mine on some creatures. See Other Mods' Lights below."));
						PopStyleColor();
						break;
					}
				}
			}

			Header(Icon::kBulb, T("Lights"));
			Field(T("Creature lights"), &Anima::Settings::on, T("Turns all of my lights on or off."));
			constexpr std::array<std::array<const char*, 2>, Anima::kKinds> kKinds{ {
				{ TR_MARK("Creature glow"), TR_MARK("Atronachs, wisps, spriggans, ice wraiths and other glowing creatures.") },
				{ TR_MARK("Spells creatures use"), TR_MARK("Breaths, casting glow, swarms and other attacks creatures throw.") },
				{ TR_MARK("Explosions and deaths"), TR_MARK("Exploding spiders, their bombs, and the burst when a creature dies.") },
				{ TR_MARK("Glowing eyes"), TR_MARK("A faint glow in the eyes of the undead and death hounds.") },
				{ TR_MARK("Machines"), TR_MARK("Dwarven spiders, spheres, ballistas and centurions.") },
			} };
			for (std::size_t i = 0; i < Anima::kKinds; ++i) {
				bool v = SettingsNow().kinds[i];
				if (Switch(T(kKinds[i][0]), v, T(kKinds[i][1]))) {
					UpdateSettings([&](Anima::Settings& a_s) { a_s.kinds[i] = v; });
					Changed();
				}
			}
			if (EyesModLoaded()) {
				MenuStyle::Note(T("JS Emissive Eyes handles the eyes while it's installed, so my glowing eyes stay off."));
			}
			Field(T("Ice, wind, water and stone creatures"), &Anima::Settings::natural,
				T("Frost atronachs, ice wraiths and other creatures that wouldn't give off light (off by default)."));
			// each light of its own switch (Anima::kExtras, in that order): on by default, off here for a player who does not want it
			constexpr std::array<std::array<const char*, 2>, Anima::kExtraCount> kExtraText{ {
				{ TR_MARK("Dwarven Centurion steam"), TR_MARK("Lights the steam Dwarven Centurions breathe, and the steam spells that use it.") },
				{ TR_MARK("Ash in the hands"), TR_MARK("Lights the ash glow in the hands while ash spells are cast, like Ash Shell and the ash spawn and ash guardian conjurations.") },
			} };
			for (std::size_t i = 0; i < Anima::kExtraCount; ++i) {
				bool v = SettingsNow().extras[i];
				if (Switch(T(kExtraText[i][0]), v, T(kExtraText[i][1]))) {
					UpdateSettings([&](Anima::Settings& a_s) { a_s.extras[i] = v; });
					Changed();
				}
			}

			Header(Icon::kBulb, T("Strength"));
			Percent(T("Brightness"), &Anima::Settings::brightness, Anima::kBrightnessMin, Anima::kBrightnessMax, T("How bright all creature lights are."));
			Percent(T("Reach"), &Anima::Settings::reach, Anima::kReachMin, Anima::kReachMax, T("How far all creature lights reach."));

			if (const char* owner = PickerOwner()) {
				MenuStyle::Note(Translation::Fill(T("You can pick a color for each creature on {}'s Choose Your Own Color page."), owner).c_str());
			}

			Header(Icon::kBulb, T("Movement"));
			Field(T("Flicker and pulse"), &Anima::Settings::flicker, T("Flame creatures flicker, storm creatures crackle, and wisps and spirits breathe."));
			Field(T("Fade on death"), &Anima::Settings::deathFade, T("A dying creature's light fades over a few seconds, leaving an ember on flame creatures."));
			Field(T("Glow when casting"), &Anima::Settings::surge, T("A creature's light brightens briefly while it casts or breathes."));
			Field(T("Dark until noticed"), &Anima::Settings::noticed, T("A creature stays dark until it notices you, then its light fades up (off by default)."));

			// one dropdown, a sub-dropdown per creatures file, a checkbox beside each creature
			if (const auto& list = CreatureList(); !list.empty()) {
				Header(Icon::kBulb, T("Creatures"));
				if (CollapsingHeader(T("Creature switches"))) {
					MenuStyle::Note(T("Untick a creature to turn off its lights."));
					const auto s = SettingsNow();
					for (const auto& [group, creatures] : list) {
						PushID(group.c_str());
						if (TreeNode(group.c_str())) {
							for (const auto& creature : creatures) {
								bool v = Anima::CreatureOn(s, creature);
								if (Switch(creature.c_str(), v, T("Ticked: this creature is lit."))) {
									UpdateSettings([&](Anima::Settings& a_s) { Anima::SetCreatureOn(a_s, creature, v); });
									Changed();
								}
							}
							TreePop();
						}
						PopID();
					}
				}
			}

			const auto found = OthersFound();
			if (std::ranges::any_of(found, [](bool a_b) { return a_b; })) {
				Header(Icon::kBulb, T("Other Mods' Lights"));
				// a mod's switch on: the warning, its config replaces ours on these creatures. Off: ours replaces its light on them
				const auto s = SettingsNow();
				for (std::size_t i = 0; i < Anima::kOthers; ++i) {
					const auto names = AlsoLitBy(i);
					if (names.empty()) {
						continue;
					}
					std::string list;
					for (std::size_t n = 0; n < names.size(); ++n) {
						list += (n ? ", " : "") + names[n];
					}
					if (s.theirs[i]) {
						PushStyleColor(ImGuiCol_Text, MenuStyle::kBad);
						TextWrapped("%s", Translation::Fill(T("Warning: {} is lighting these creatures instead of me: {}."),
											  Anima::kOtherMods[i].name, list)
											  .c_str());
						PopStyleColor();
					} else {
						MenuStyle::Note(Translation::Fill(T("My lights replace {}'s on these creatures: {}."), Anima::kOtherMods[i].name, list).c_str());
					}
				}
				TextWrapped("%s", T("When another mod lights one of these creatures, my light replaces it. Tick a mod below to give its creatures "
									"back to it, and my lights turn off on them."));
				for (std::size_t i = 0; i < Anima::kOthers; ++i) {
					if (!found[i]) {
						continue;
					}
					bool v = s.theirs[i];
					if (Switch(std::string(Anima::kOtherMods[i].name).c_str(), v, T("Ticked: that mod lights its creatures, and my lights turn off on them."))) {
						UpdateSettings([&](Anima::Settings& a_s) { a_s.theirs[i] = v; });
						Changed();
					}
				}
			}
		}
	}

	namespace
	{
		// devbench only, never saved: the settings page in a window of its own for a test's picture (DevBench.cpp's menuPage,
		// Dynamic Wards' way)
		std::atomic<bool>                          gTestOpen{ false };
		SKSEMenuFramework::Model::WindowInterface* gTestWindow = nullptr;

		void __stdcall DrawTestWindow()
		{
			SetNextWindowPos(ImVec2(40.0f, 20.0f), ImGuiCond_Always);
			SetNextWindowSize(ImVec2(820.0f, 1040.0f), ImGuiCond_Always);
			if (Begin("Illuminated - Creature Lighting NG (test)", nullptr, ImGuiWindowFlags_NoCollapse) && gTestOpen.load()) {
				LightKit::GuardedDraw<RenderSettings>();
			}
			End();
		}
	}

	void ShowTestPage(bool a_open)
	{
		gTestOpen = a_open;
		if (gTestWindow) {
			gTestWindow->IsOpen = a_open;
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		MenuStyle::gTheme = MenuStyle::MakeTheme(0x6FE3B4);
		SKSEMenuFramework::SetSection(T("Illuminated - Creature Lighting NG"));
		SKSEMenuFramework::AddSectionItem(T("Settings"), LightKit::GuardedDraw<RenderSettings>);
		gTestWindow = SKSEMenuFramework::AddWindow(DrawTestWindow, false);
		SKSE::log::info("settings page added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
