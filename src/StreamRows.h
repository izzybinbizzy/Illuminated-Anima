// StreamRows - a row of small lights along every bolt, flame spray and cone a plugin claims, and one light where a cone's cloud is
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// Each plugin carries its own copy of this file and StreamRows.cpp, kept byte for byte the master's in Tools\Shared Plugin Code\ by
// PC Runner\shared code sync.py. Illuminated's row (its 2026-10-20 checklist 140 fallback: one stretched light cannot be drawn inside
// our mods, so a row of lights spread over the projectile's real length every frame, BeamRow.h) moved here unchanged so another of our
// plugins draws its beams the same way. One plugin draws a given beam (Owner): the carriers answer through an export, in a fixed rank.
//
// CREDIT: how a light is made and registered follows ReLight by Truman (github.com/TrumanGIT/ReLight), GPL-3.0-or-later, used with his
// permission: one master light cloned for every use, its size carried in the radius' z, handed to the shadow scene node.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace StreamRows
{
	// one projectile base's row: the color, fade and reach its own light would give right under the beam's middle (BeamRow::Lay scales
	// every light to it); a bolt runs along its model, a flame spray or cone along its aim
	struct Recipe
	{
		RE::NiColor              color{ 1.0f, 1.0f, 1.0f };
		float                    fade{ 1.0f };
		float                    reach{ 300.0f };
		bool                     beam{ true };
		bool                     spray{ false };        // the host's own kind (Illuminated: a spray its Spray Lights setting owns)
		const RE::TESObjectLIGH* colorFrom{ nullptr };  // a light record recolored on every refresh: the row wears its color
		bool                     cloud{ false };        // one light on the path, held where the cloud is after the cone ends
	};

	// what the carrying plugin answers - plain function pointers, read on the main thread
	struct Host
	{
		const char* rowName{ "StreamRow" };                               // the row lights' node name
		const char* cloudName{ "StreamCloud" };                           // a cloud light's node name
		bool (*rowOn)(const Recipe&){ nullptr };                          // the host's switches for this row
		bool (*cloudOn)(RE::FormID){ nullptr };                           // the host's switches for a cone's cloud light
		float (*brightness)(){ nullptr };                                 // the host's Brightness x its row strength
		float (*reach)(){ nullptr };                                      // the host's Reach, 1 = 100 %
		bool (*flicker)(){ nullptr };                                     // the rows shimmer
		bool (*shaped)(){ nullptr };                                      // they shimmer in their spell's shape (Pulse.h)
		std::optional<RE::NiColor> (*spellColor)(RE::FormID){ nullptr };  // a color the player chose for the spell, or none
		int (*lighting)(){ nullptr };                                     // 0 Community Shaders, 1 ENB, 2 Vanilla (Plugin::Lighting's values)
		bool (*inverseSquare)(){ nullptr };                               // the lights are drawn inverse square
		bool (*lightLimitFix)(){ nullptr };                               // Light Limit Fix reads every light's words
		bool (*oldCarrier)(const char* a_module, RE::FormID){ nullptr };  // an older plugin with no export still draws this beam
	};

	void                           Install(const Host& a_host);                           // data load, once, before Claim: the Load3D / Release3D hooks on bolts, flames and cones
	std::size_t                    Claim(std::unordered_map<RE::FormID, Recipe> a_rows);  // this plugin's rows by projectile base record
	void                           Recolor(RE::FormID a_base, const Recipe& a_recipe);    // a settings refresh changed one row's numbers
	void                           Refresh();                                             // the host's switches moved: a row switched off goes and is parked, one switched on is hung
	bool                           Awake();                                               // something to move or let go: the host's player update calls Frame only while this is true
	void                           Frame();                                               // main thread
	void                           DropAll();                                             // kPreLoadGame / kNewGame
	[[nodiscard]] bool             Claims(RE::FormID a_base);                             // this plugin claimed it
	[[nodiscard]] std::string_view Owner(RE::FormID a_base);                              // the module that draws it: the first carrier in the rank that claims it
	[[nodiscard]] bool             IsRowLight(const RE::NiLight* a_light);

	struct Counts
	{
		std::size_t rows{ 0 }, lights{ 0 }, parked{ 0 }, clouds{ 0 };
	};
	[[nodiscard]] Counts Stats();
}
