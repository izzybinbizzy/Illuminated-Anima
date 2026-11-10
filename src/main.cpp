// Illuminated - Creature Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see the
// GNU General Public License in LICENSE.txt for details.
//
// Creatures glow in their own light - atronachs, wisps, spriggans, ice wraiths, ash spawn, chaurus, familiars and the creatures
// of the mods its creatures files name - with lights made in memory, on Community Shaders (with or without Light Placer), ENB and
// Vanilla, the lighting found by itself. No plugin file, nothing in the save; the settings live in its SKSE Menu Framework page.
// Where another creature lighting mod's config lights a creature, ours steps aside and never touches its light; the menu warns
// which mod replaces ours on which creatures, and each mod's switch puts our light back on top of its own.
//
// CREDITS: a light is made and registered the way ReLight by Truman does it (github.com/TrumanGIT/ReLight, GPL-3.0-or-later,
// used with his permission): a master NiPointLight cloned for every use and handed to the shadow scene node. Light spots and
// relative colors after iLluminous Elementals (RogueUnicorn00); Darenii's creatures after Darenii's Spells - Creatures - CS
// Lights Addon (ShreditTES, with permission). The Character::Load3D hook follows Light Placer's (powerof3, GPL-3.0, used with
// permission); the game's light dimmer was read off CommonLibSSE-NG #477 and Baked Goods by ItzIvy05 (MIT / GPL-3.0).
//
// THE FILES
//   main.cpp            this file - the order of the data-load work, and the SKSE messages
//   Plugin.h            what the files share; PCH.h what every file includes
//   AnimaMath.h         game-free: the creatures files, the settings file, the light over time - tested off the game
//   Creatures.cpp       the creatures files, the races and body models they name, what the other creature lighting mods light
//   CreatureLights.cpp  the lights: made when a creature's 3D loads, dropped when it unloads; their flicker, death fade and cast surge
//                       are keys the game's own dimmer controllers play - no pass of ours runs while they do
//   SpellLights.cpp     the spells creatures use and their explosions, lit through the game's own record lights
//   Lighting.cpp        Community Shaders, ENB or Vanilla (Illuminated's automatic detection)
//   Settings.cpp        the settings file, read at load and saved after each change
//   Menu.cpp            the SKSE Menu Framework page        DevBench.cpp  DevBench's inspect and control (optional)
//   LightKit.h, LightMath.h, Pulse.h, MenuStyle.h, Translation.h, RuntimeGuard.h, DevBenchGlue.h, SKSEMenuFramework.h,
//                       DevBenchAPI.*: shared word for word with our other plugins (master: Tools\Shared Plugin Code)
//   FloorLightFix.*, FloorLightMath.h, FloorLightChecks.h
//                       the Floor Light Fix, shared the same way: a light the game takes out of the scene stops lighting the floor
//   tests\testanima.cpp the off-game tests of AnimaMath.h and FloorLightMath.h

#include "FloorLightFix.h"
#include "Plugin.h"
#include "RuntimeGuard.h"
#include "Translation.h"

using namespace Plugin;

namespace
{
	void LoadEverything()
	{
		const auto started = std::chrono::steady_clock::now();
		ReadLighting();
		ReadCreatures();
		ReadSpellRecords();
		ApplySpellRecords(SettingsNow());
		InstallLights();  // before the menu's steps: a throw in them leaves the creatures lit
		SKSE::log::info("{}", Translation::Load("Data/SKSE/Plugins/IlluminatedCreatureLightingNG/Translation.json"));
		RegisterMenu();
		SKSE::log::info("ready in {:.1f} ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
	}

	// an exception never leaves into the game: one thrown by the load work is logged, and the steps before it keep what they did
	void OnDataLoaded()
	{
		try {
			LoadEverything();
		} catch (const std::exception& e) {
			SKSE::log::critical("the data-load work stopped part way: {} - the steps before it keep their work, the rest is not done this session",
				e.what());
		}
	}

	void Handle(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			LoadSettings();  // before DevBench can change one and save the rest
			OfferToDevBench();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			DropLights();  // the creatures they hang on are the old game's
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			LookAround();  // the creatures loaded with the save sent no load event of their own
			break;
		default:
			break;
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (a_msg) {
			Guarded("SKSE message", [a_msg]() { Handle(a_msg); });
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// the trampoline holds the Floor Light Fix's two call hooks, the only ones of this plugin that need it
	SKSE::InitInfo info{};
	info.trampoline = true;
	info.trampolineSize = FloorLightFix::kTrampolineBytes;
	SKSE::Init(a_skse, info);
	// the wrong game: loaded but idle - a false here makes SKSE add its own "exit game?" box over the guard's
	if (!RuntimeGuard::RightGame("Illuminated - Creature Lighting NG", "Illuminated - Creature Lighting NG", "Illuminated - Creature Lighting NG - VR")) {
		return true;
	}
	// installed only by the first of our mods that carries it; every later copy finds its sites taken and stands down
	FloorLightFix::Install();
	// the start line is the only work here that can throw (low memory): it never leaves the plugin
	try {
		const auto* decl = SKSE::PluginDeclaration::GetSingleton();
		const auto  ver = decl ? std::format("{}.{}", decl->GetVersion().major(), decl->GetVersion().minor()) : std::string("?");
		SKSE::log::info("Illuminated - Creature Lighting NG {} - creatures glow in their own light, on any lighting", ver);
	} catch (...) {
	}
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
