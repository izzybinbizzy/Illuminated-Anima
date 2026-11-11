// Illuminated - Creature Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// What the files share. Each block names the file that defines it.

#pragma once

#include "AnimaMath.h"
#include "LightKit.h"

namespace Plugin
{
	using Anima::Lower;
	using LightKit::Guarded;

	// a_job on the game's main thread at the next chance (SKSE's task interface); false when it could not be queued
	template <class F>
	[[nodiscard]] bool Later(const char* a_where, F&& a_job) noexcept
	{
		try {
			if (auto* tasks = SKSE::GetTaskInterface()) {
				tasks->AddTask([a_where, job = std::forward<F>(a_job)]() { Guarded(a_where, job); });
				return true;
			}
		} catch (...) {
		}
		return false;
	}

	// ------------------------------------------------------------------ Lighting.cpp: which lighting the game draws with
	enum class Lighting : int
	{
		kShaders = 0,  // Community Shaders: inverse square lights
		kEnb = 1,      // an ENB: the game's own lighting, every light drawn plain
		kVanilla = 2,  // the game's own lighting, every light drawn plain
	};
	void                      ReadLighting();   // data load, before any light is made
	[[nodiscard]] Lighting    LightingFound();  // found by itself, never picked by the player
	[[nodiscard]] const char* LightingName(Lighting a_lighting);
	[[nodiscard]] bool        InverseSquare();
	[[nodiscard]] bool        LightLimitFix();

	// ------------------------------------------------------------------ Settings.cpp: IlluminatedCreatureLightingNG\Settings.ini, written by the menu
	void                          LoadSettings();
	bool                          SaveSettings();  // any thread; false when the file could not be written (logged)
	[[nodiscard]] Anima::Settings SettingsNow();
	void                          UpdateSettings(const std::function<void(Anima::Settings&)>& a_change);  // under the lock, clamped

	// ------------------------------------------------------------------ Creatures.cpp: the creature files and who lights what
	void                                         ReadCreatures();  // data load: every creatures file, the races and body models they name, the other mods' claims
	[[nodiscard]] const std::vector<Anima::Row>& Rows();
	// the rows that light this actor (those of a model its body wears or its skin, else its race's), empty for none
	[[nodiscard]] std::vector<std::size_t> RowsOf(RE::Actor* a_actor);
	// any thread once data load is done: a row may name this character (its race, or a skin holding a row's model); two reads
	[[nodiscard]] bool CouldLight(RE::Actor* a_actor);
	// a record named "0x<id>~<plugin>"; nullptr when the plugin is not loaded or the text does not read
	[[nodiscard]] RE::TESForm* FormOfKey(std::string_view a_key);
	// another mod's Light Placer file names a model of this row
	[[nodiscard]] bool RowModelsLitByOthers(std::size_t a_row);
	// main thread: another creature lighting mod lights this actor and its switch gives the creature back to it
	[[nodiscard]] bool GivenToThem(RE::Actor* a_actor, const Anima::Settings& a_s);
	// main thread: another mod's own switch has its look on this row (a game global it names is not 0) and its switch here gives the row to it
	[[nodiscard]] bool RowGivenToThem(std::size_t a_row, const Anima::Settings& a_s);
	// main thread: every other mod's light on this actor put out or given back by the settings; a_root its stored body, nullptr when it has none yet
	void               TheirLights(RE::Actor* a_actor, RE::NiAVObject* a_root, const Anima::Settings& a_s);
	[[nodiscard]] bool OthersLightBodies();  // any thread once data load is done: another mod's Light Placer file names a body or art model
	// any thread once data load is done: does this magic effect belong to an ability that carries a light
	[[nodiscard]] bool IsAbilityLight(RE::FormID a_effect);
	void               NoteWornLight(RE::Actor* a_actor, RE::FormID a_effect);  // main thread: the game's apply event said such an effect reached it
	void               ReadWornLights(RE::Actor* a_actor);                      // main thread: what it wears now, read when it loads
	void               ForgetWornLights();                                      // a game loads, or a new one starts: the marks are the old game's
	// which other mods are installed (the menu offers those) and, for each, the names of our creatures it lights too (the menu's list,
	// empty for none). Any thread: a creature met in play can add a name.
	[[nodiscard]] Anima::Lighters          OthersFound();
	[[nodiscard]] std::vector<std::string> AlsoLitBy(std::size_t a_mod);
	[[nodiscard]] std::size_t              CreatureRowCount();
	[[nodiscard]] std::size_t              FilesRead();

	// ------------------------------------------------------------------ SpellLights.cpp: the spells creatures use, the game's own way
	void                      ReadSpellRecords();                             // data load: the records the spell and burst rows name get a light of ours
	void                      ApplySpellRecords(const Anima::Settings& a_s);  // the settings onto them (data load, the menu)
	[[nodiscard]] std::size_t SpellRecordCount();

	// ------------------------------------------------------------------ CreatureLights.cpp: the lights
	void                         InstallLights();  // data load: a character's two 3D slots and the game's events
	void                         DropLights();     // a game loads, or a new one starts
	void                         LookAround();     // main thread: light every creature already loaded (after a game loads)
	void                         ApplyNow();       // main thread: the settings onto every creature now (the menu)
	[[nodiscard]] std::size_t    LitCount();
	[[nodiscard]] nlohmann::json LitReport();          // main thread: every creature lit now and its lights (DevBench)
	[[nodiscard]] std::uint32_t  LitAbilitiesNoted();  // how many applied abilities that carry a light the game's event announced (DevBench)
	[[nodiscard]] std::uint32_t  LightsHungAgain();    // how many creatures were still lit when a loading screen ended (DevBench)

	// ------------------------------------------------------------------ Menu.cpp / DevBench.cpp
	void RegisterMenu();
	void ShowTestPage(bool a_open);  // devbench only, never saved: the settings page in a window of its own, for a picture
	void OfferToDevBench();
}
