// Illuminated Anima - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// DevBench, when it is in the load order (it is optional; nothing here runs without it):
//   inspect kind=anima   the lighting found, the settings, the creatures files read, the other creature lighting mods found and
//                        the creatures each lights instead of ours, every creature lit near the player now (its lights' color,
//                        fade, radius, death and cast state), the lit abilities applied that the game's event announced, and
//                        the cost of the task that acts on what was noted
//   anima.control        action = set (setting + value: one menu setting changed the way the menu does - saved, then applied
//                        on the main thread; the reply is the state after it) | rescan (light the creatures around the player again)

#include "Plugin.h"

#include "DevBenchGlue.h"

namespace Plugin
{
	namespace
	{
		using DevBenchGlue::json;

		constexpr const char* kKey = "anima";

		constexpr const char* kDescriptor =
			R"json({"description":"Illuminated Anima - the lighting found (automatic), the settings, the creatures files read, the other creature lighting mods installed and the creatures each one lights instead of ours (ours steps aside there), every creature lit around the player now with each light's color, fade, radius and its death / cast state, how many abilities that carry a light the game's apply event announced, and the cost in microseconds and the call count of the task that acts on what was noted. Read only.","inputSchema":{"type":"object","properties":{}},"readOnly":true})json";

		constexpr const char* kTool =
			R"json({"description":"Illuminated Anima (lights creatures): change its settings the way its menu does, or light the creatures around the player again. action=set changes one setting (saved, then applied on the main thread) and replies with the state after it: lights (0/1), glow / spell / burst / eyes (0/1, the four kinds), natural (0/1, ice, wind, water and stone creatures), brightness (10-300, percent), reach (25-300, percent), flicker / deathFade / castSurge (0/1), OnTopEmissive / OnTopElementals / OnTopDarenii / OnTopLuminous / OnTopEnhanced / OnTopIlluminatedAtronachs / OnTopJSEyes / OnTopOther (0/1, our light on that mod's creatures too, on top of its own; OnTopOther = every other mod that lights one of our creatures); menuPage (a test, never saved: 1 = the settings page in a window of its own for a picture, 0 = closed). action=rescan drops every light and lights the creatures around the player again at once. Read state with inspect kind=anima.","inputSchema":{"type":"object","properties":{"action":{"type":"string","enum":["set","rescan"]},"setting":{"type":"string","enum":["lights","glow","spell","burst","eyes","natural","brightness","reach","flicker","deathFade","castSurge","OnTopEmissive","OnTopElementals","OnTopDarenii","OnTopLuminous","OnTopEnhanced","OnTopIlluminatedAtronachs","OnTopJSEyes","OnTopOther","menuPage"]},"value":{"type":"number"}},"required":["action"]}})json";

		json SettingsJson(const Anima::Settings& a_s)
		{
			json j{ { "lights", a_s.on }, { "natural", a_s.natural }, { "brightness", a_s.brightness }, { "reach", a_s.reach },
				{ "flicker", a_s.flicker }, { "deathFade", a_s.deathFade }, { "castSurge", a_s.surge } };
			for (std::size_t i = 0; i < Anima::kKinds; ++i) {
				j[std::string(Anima::kKindNames[i])] = a_s.kinds[i];
			}
			for (std::size_t i = 0; i < Anima::kOthers; ++i) {
				j[std::string(Anima::kOtherMods[i].key)] = a_s.onTop[i];
			}
			return j;
		}

		json OthersJson()
		{
			json       out = json::array();
			const auto found = OthersFound();
			for (std::size_t i = 0; i < Anima::kOthers; ++i) {
				if (found[i]) {
					out.push_back({ { "mod", std::string(Anima::kOtherMods[i].name) }, { "replaces", ReplacedBy(i) } });
				}
			}
			return out;
		}

		json CostJson()
		{
			json out = json::array();
			for (const auto& slot : LightKit::Cost::Slots()) {
				if (const char* name = slot.name.load(std::memory_order_relaxed)) {
					out.push_back({ { "pass", name }, { "usAvg", slot.usAvg.load(std::memory_order_relaxed) }, { "usMax", slot.usMax.load(std::memory_order_relaxed) }, { "calls", slot.calls.load(std::memory_order_relaxed) } });
				}
			}
			return out;
		}

		// everything, read on the main thread (the lights are the main thread's)
		json StateNow()
		{
			auto now = DevBenchGlue::OnMainThread([]() {
				return json{ { "lighting", LightingName(LightingFound()) }, { "inverseSquare", InverseSquare() }, { "settings", SettingsJson(SettingsNow()) },
					{ "creatureFiles", FilesRead() }, { "creatureRows", CreatureRowCount() }, { "spellRecordsLit", SpellRecordCount() },
					{ "otherMods", OthersJson() }, { "lit", LitReport() },
					{ "loadEvents", { { "litAbilities", LitAbilitiesNoted() }, { "hungAgain", LightsHungAgain() } } }, { "cost", CostJson() } };
			});
			return now ? *now : DevBenchGlue::NotRunYet();
		}

		void Handler(void*, const char*, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				DevBenchGlue::Reply(a_sink, a_write, StateNow());
			} catch (...) {
				try {  // the refusal is JSON too and may throw (low memory) - never out of a noexcept function
					DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("the Illuminated Anima report could not be made"));
				} catch (...) {
				}
			}
		}

		// the one setting a_key names, as a change made under the settings lock; empty for an unknown key
		std::function<void(Anima::Settings&)> ChangeOf(const std::string& a_key, int a_v)
		{
			const bool                                                                on = a_v != 0;
			const std::array<std::pair<std::string_view, bool Anima::Settings::*>, 5> switches{ { { "lights", &Anima::Settings::on },
				{ "natural", &Anima::Settings::natural }, { "flicker", &Anima::Settings::flicker }, { "deathFade", &Anima::Settings::deathFade },
				{ "castSurge", &Anima::Settings::surge } } };
			for (const auto& [name, field] : switches) {
				if (a_key == name) {
					return [field, on](Anima::Settings& a_s) { a_s.*field = on; };
				}
			}
			if (a_key == "brightness") {
				return [a_v](Anima::Settings& a_s) { a_s.brightness = a_v; };
			}
			if (a_key == "reach") {
				return [a_v](Anima::Settings& a_s) { a_s.reach = a_v; };
			}
			for (std::size_t i = 0; i < Anima::kKinds; ++i) {
				if (a_key == Anima::kKindNames[i]) {
					return [i, on](Anima::Settings& a_s) { a_s.kinds[i] = on; };
				}
			}
			for (std::size_t i = 0; i < Anima::kOthers; ++i) {
				if (a_key == Anima::kOtherMods[i].key) {
					return [i, on](Anima::Settings& a_s) { a_s.onTop[i] = on; };
				}
			}
			return {};
		}

		json Act(const json& a_args)
		{
			const auto action = DevBenchGlue::Text(a_args, "action");
			if (action == "rescan") {
				auto now = DevBenchGlue::OnMainThread([]() {
					ApplyNow();
					return json{ { "ok", true }, { "lit", LitReport() } };
				});
				return now ? *now : DevBenchGlue::NotRunYet();
			}
			if (action != "set") {
				return DevBenchGlue::Refusal("unknown action '" + action + "'", json::array({ "set", "rescan" }));
			}
			const auto key = DevBenchGlue::Text(a_args, "setting");
			const auto value = DevBenchGlue::Number(a_args, "value");
			if (!value) {
				return DevBenchGlue::Refusal("set needs a number value");
			}
			const int v = static_cast<int>(std::clamp(*value, -100000.0, 100000.0));
			if (key == "menuPage") {
				// the menu framework draws that window on its own thread: the flag is set from the main thread, never this listener
				const bool open = v > 0;
				auto       shown = DevBenchGlue::OnMainThread([open]() {
					ShowTestPage(open);
					return json{ { "ok", true }, { "setting", "menuPage" }, { "saved", false }, { "menuPage", open } };
				});
				return shown ? *shown : DevBenchGlue::NotRunYet();
			}
			const auto change = ChangeOf(key, v);
			if (!change) {
				return DevBenchGlue::Refusal("unknown setting '" + key + "'");
			}
			UpdateSettings(change);
			const bool saved = SaveSettings();
			auto       now = DevBenchGlue::OnMainThread([]() {
				ApplyNow();
				return SettingsJson(SettingsNow());
			});
			if (now && DevBenchGlue::Refused(*now)) {
				// nothing was queued, or the change failed on the main thread: the refusal says which; the live table holds the change
				auto j = *now;
				j["setting"] = key;
				j["saved"] = saved;
				return j;
			}
			if (!now) {
				// the live table holds the change already and the task is queued: only this answer did not wait for it
				return json{ { "ok", true }, { "setting", key }, { "saved", saved }, { "applied", "when the game runs its queued task" },
					{ "settings", SettingsJson(SettingsNow()) } };
			}
			return json{ { "ok", true }, { "setting", key }, { "saved", saved }, { "settings", *now } };
		}

		void Control(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				DevBenchGlue::Reply(a_sink, a_write, Act(DevBenchGlue::Args(a_args)));
			} catch (...) {
				try {
					DevBenchGlue::Reply(a_sink, a_write, DevBenchGlue::Refusal("Illuminated Anima could not handle that"));
				} catch (...) {
				}
			}
		}
	}

	void OfferToDevBench()
	{
		auto* devbench = DevBenchAPI::GetDevBenchInterface001();
		if (!devbench) {
			SKSE::log::info("[DEVBENCH] DevBench is not in this load order");
			return;
		}
		const auto build = devbench->GetBuildNumber();
		if (build < DevBenchGlue::kNeedsBuild) {
			SKSE::log::warn("[DEVBENCH] DevBench build {} is older than {} and cannot take an inspect extension", build, DevBenchGlue::kNeedsBuild);
			return;
		}
		const bool fresh = devbench->RegisterToolExtension("inspect", kKey, kDescriptor, Handler, nullptr);
		devbench->RegisterTool("anima.control", kTool, Control, nullptr);
		SKSE::log::info("[DEVBENCH] offered to DevBench build {}: inspect kind={} ({}), the tool anima.control", build, kKey,
			fresh ? "a new entry" : "replacing one already registered");
	}
}
