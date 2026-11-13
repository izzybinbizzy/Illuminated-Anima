// Illuminated - Creature Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The game-free part, tested off the game (tests\testanima.cpp): the creature files' rows, the settings file, the strength a
// light is drawn at and the element each creature's light breathes with. Nothing here includes a game header.

#pragma once

#include "LightMath.h"
#include "Pulse.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Anima
{
	using LightKit::Lower;
	using LightKit::SameText;
	using Rgb = std::array<int, 3>;

	// the kinds the menu switches, each optional
	enum class Kind : std::uint8_t
	{
		kGlow,      // the creature's own glow
		kSpell,     // the spells, breaths and attacks a creature uses
		kBurst,     // explosions and deaths
		kEyes,      // glowing eyes
		kMachines,  // the dwarven machines
	};
	inline constexpr std::size_t                          kKinds = 5;
	inline constexpr std::array<std::string_view, kKinds> kKindNames{ "glow", "spell", "burst", "eyes", "machines" };

	// a row of lights along a beam or a breath (stream=): drawn by the shared StreamRows module along its records' projectiles, never
	// through their light slot (each projectile keeps its own light, as in Illuminated)
	enum class Stream : std::uint8_t
	{
		kNone,
		kBolt,   // along its model, to where it hits
		kSpray,  // a flame spray or a cone, along its aim
		kCloud,  // one light where a cone's cloud is
	};
	inline constexpr std::array<std::string_view, 3> kStreamNames{ "bolt", "spray", "cloud" };

	// a creature row's line on Illuminated's or RELight's Choose Your Own Color page; a spell or burst row lights a record, not a creature
	[[nodiscard]] constexpr bool OnThePicker(Kind a_kind) noexcept
	{
		return a_kind == Kind::kGlow || a_kind == Kind::kEyes || a_kind == Kind::kMachines;
	}
	[[nodiscard]] inline std::string PickerKey(std::string_view a_name)
	{
		return "Creature|" + std::string(a_name);
	}

	// JS Emissive Eyes takes over the eyes when it is installed: ours are off on every creature they light. Machines are not eyes
	[[nodiscard]] constexpr bool TakenByEyesMod(Kind a_kind, bool a_eyesMod) noexcept
	{
		return a_eyesMod && a_kind == Kind::kEyes;
	}

	// what a creature's light is made of: it picks the breath of its flicker, and whether it sits under the one switch for
	// elements that should not really glow (wind, ice, water, stone - OFF by default)
	enum class Element : std::uint8_t
	{
		kFire,
		kShock,
		kSpirit,
		kNature,
		kShadow,
		kArcane,
		kPoison,
		kBlood,
		kNecrotic,
		kDwemer,
		kIce,
		kWind,
		kWater,
		kStone,
	};
	inline constexpr std::array<std::string_view, 14> kElementNames{ "fire", "shock", "spirit", "nature", "shadow", "arcane", "poison",
		"blood", "necrotic", "dwemer", "ice", "wind", "water", "stone" };

	[[nodiscard]] constexpr bool Natural(Element a_element) noexcept
	{
		return a_element == Element::kIce || a_element == Element::kWind || a_element == Element::kWater || a_element == Element::kStone;
	}

	// the shared pulse shapes (Pulse.h, Illuminated's and RELight's Realistic look): one per element, never a shape of our own
	[[nodiscard]] constexpr Pulse::Kind PulseOf(Element a_element) noexcept
	{
		switch (a_element) {
		case Element::kFire:
			return Pulse::Kind::kFire;
		case Element::kShock:
			return Pulse::Kind::kShock;
		case Element::kIce:
		case Element::kWater:
			return Pulse::Kind::kFrost;
		case Element::kPoison:
			return Pulse::Kind::kPoison;
		case Element::kBlood:
			return Pulse::Kind::kAbsorbHealth;
		case Element::kShadow:
			return Pulse::Kind::kIllusion;
		case Element::kArcane:
			return Pulse::Kind::kMagelight;
		case Element::kNecrotic:
			return Pulse::Kind::kConjuration;
		case Element::kSpirit:
		case Element::kNature:
			return Pulse::Kind::kHealing;
		default:
			return Pulse::Kind::kNone;
		}
	}

	struct Point
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
	};

	// one creature of a creatures file: which creature (by race editor ID, or by a model its body wears), its light and its look
	struct Row
	{
		std::string                name;
		std::vector<std::string>   races;    // race editor IDs, lower case
		std::vector<std::string>   models;   // body model paths, NormalPath
		std::vector<std::string>   nodes;    // bones the light hangs on, the first found wins (a body replacer's light rides its own bones)
		std::vector<Point>         offsets;  // on such a bone: one light per offset, in the bone's frame; none = one light on the bone itself
		std::vector<Point>         points;   // on a body with none of those bones: one light per point, in the creature's root frame
		Kind                       kind{ Kind::kGlow };
		Element                    element{ Element::kSpirit };
		Rgb                        rgb{ 255, 255, 255 };
		float                      reach{ 256.0f };
		float                      strength{ 1.0f };
		bool                       ember{ false };           // a small light stays on its remains after the death fade
		std::vector<std::string>   records;                  // a spell or burst row: the game records it lights, "0x<id>~<plugin>"
		float                      size{ 2.0f };             // an inverse-square light's size (Community Shaders)
		std::vector<std::string>   skins;                    // skin records, "0x<id>~<plugin>": two looks of one body model told apart by the skin worn
		std::vector<std::string>   shaders;                  // effect shaders, "0x<id>~<plugin>": a see-through creature whose own ability plays one (a ghost)
		std::vector<std::string>   arts;                     // a spell or burst row: art objects it lights by their model (a hit art, an enchantment's art), NormalPath
		std::vector<std::string>   impacts;                  // a burst row: impact records it lights by their model (an impact's splash), NormalPath
		std::optional<std::size_t> extra;                    // a light of its own switch (switch=, kExtras): the menu turns it off apart from its kind
		Stream                     stream{ Stream::kNone };  // stream=: a row of lights along its records' projectiles (StreamRows)
		bool                       moved{ false };           // moved=yes: Illuminated / RELight lit it before; left to an older one still loaded
		std::string                creature;                 // the creature as a player knows it, for its menu switch (creature=, else the row's name)
		std::string                group;                    // its menu group: the creatures file it came from (the caller fills it)
	};

	// the lights a player can turn off on their own, each a switch in the menu, on by default (his 575: "make these toggles like the configs
	// we have for illuminated in case users dont want them"); a row names one with switch=<key>. Menu.cpp holds each one's text
	inline constexpr std::array<std::string_view, 2> kExtras{ "CenturionSteam", "AshHand" };
	inline constexpr std::size_t                     kExtraCount = kExtras.size();

	// a model path as compared everywhere: lower case, backslashes, no leading "meshes\" or "data\meshes\"
	[[nodiscard]] inline std::string NormalPath(std::string_view a_path)
	{
		auto p = Lower(a_path);
		std::ranges::replace(p, '/', '\\');
		for (const std::string_view lead : { "data\\", "meshes\\" }) {
			if (p.starts_with(lead)) {
				p.erase(0, lead.size());
			}
		}
		return p;
	}

	// RE::Light's meshPath key: a part of the model's file name, without its folders and extension, in any case
	[[nodiscard]] inline bool MeshKeyNames(std::string_view a_model, const std::vector<std::string>& a_keys)
	{
		const auto path = NormalPath(a_model);
		const auto slash = path.rfind('\\');
		auto       stem = std::string_view(path).substr(slash == std::string::npos ? 0 : slash + 1);
		stem = stem.substr(0, stem.rfind('.'));
		return !stem.empty() && std::ranges::any_of(a_keys, [&](const std::string& a_key) { return !a_key.empty() && stem.find(Lower(a_key)) != std::string_view::npos; });
	}

	// a line without its blanks at either end, and without a UTF-8 BOM in front
	[[nodiscard]] inline std::string_view Trim(std::string_view a_text) noexcept
	{
		if (a_text.starts_with("\xEF\xBB\xBF")) {
			a_text.remove_prefix(3);
		}
		while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t' || a_text.front() == '\r')) {
			a_text.remove_prefix(1);
		}
		while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) {
			a_text.remove_suffix(1);
		}
		return a_text;
	}

	// the numbers of a comma list, at most a_most of them; nullopt when one does not read as a finite number (a nan would pass a clamp)
	[[nodiscard]] inline std::optional<std::vector<float>> Numbers(std::string_view a_text, std::size_t a_most)
	{
		std::vector<float> out;
		while (!a_text.empty() && out.size() < a_most) {
			const auto comma = a_text.find(',');
			const auto part = Trim(a_text.substr(0, comma));
			float      v = 0.0f;
			const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), v);
			if (ec != std::errc{} || end != part.data() + part.size() || !std::isfinite(v)) {
				return std::nullopt;
			}
			out.push_back(v);
			if (comma == std::string_view::npos) {
				break;
			}
			a_text.remove_prefix(comma + 1);
		}
		return out;
	}

	template <std::size_t N>
	[[nodiscard]] std::optional<std::size_t> IndexOf(const std::array<std::string_view, N>& a_names, std::string_view a_name)
	{
		for (std::size_t i = 0; i < N; ++i) {
			if (SameText(a_names[i], a_name)) {
				return i;
			}
		}
		return std::nullopt;
	}

	namespace Detail
	{
		// one key of a [creature] block onto its row; false when the value did not read (a color: the block is then left out)
		inline bool Apply(Row& a_row, std::string_view a_key, std::string_view a_value)
		{
			const auto key = Lower(a_key);
			if (key == "name") {
				a_row.name = a_value;
			} else if (key == "race") {
				a_row.races.push_back(Lower(a_value));
			} else if (key == "model") {
				a_row.models.push_back(NormalPath(a_value));
			} else if (key == "node") {
				a_row.nodes.emplace_back(a_value);
			} else if (key == "point") {
				if (const auto n = Numbers(a_value, 3); n && n->size() == 3) {
					a_row.points.push_back({ (*n)[0], (*n)[1], (*n)[2] });
				}
			} else if (key == "offset") {
				if (const auto n = Numbers(a_value, 3); n && n->size() == 3) {
					a_row.offsets.push_back({ (*n)[0], (*n)[1], (*n)[2] });
				}
			} else if (key == "kind") {
				if (const auto i = IndexOf(kKindNames, a_value)) {
					a_row.kind = static_cast<Kind>(*i);
				}
			} else if (key == "element") {
				if (const auto i = IndexOf(kElementNames, a_value)) {
					a_row.element = static_cast<Element>(*i);
				}
			} else if (key == "color") {
				const auto n = Numbers(a_value, 3);
				if (!n || n->size() != 3) {
					return false;
				}
				for (std::size_t i = 0; i < 3; ++i) {
					a_row.rgb[i] = static_cast<int>(std::clamp((*n)[i], 0.0f, 255.0f) + 0.5f);  // clamped first: a huge number cast to int is undefined
				}
			} else if (key == "reach") {
				if (const auto n = Numbers(a_value, 1); n && n->size() == 1) {
					a_row.reach = std::clamp((*n)[0], 16.0f, 2048.0f);
				}
			} else if (key == "strength") {
				if (const auto n = Numbers(a_value, 1); n && n->size() == 1) {
					// a burst's flash takes Illuminated's 7.1 and a draugr breath 5.9: the top is above both
					a_row.strength = std::clamp((*n)[0], 0.05f, 12.0f);
				}
			} else if (key == "record") {
				a_row.records.emplace_back(a_value);
			} else if (key == "skin") {
				a_row.skins.emplace_back(a_value);
			} else if (key == "shader") {
				a_row.shaders.emplace_back(a_value);
			} else if (key == "stream") {
				// an unknown kind of row would draw nothing: the row is left out and counted
				const auto i = IndexOf(kStreamNames, a_value);
				if (!i) {
					return false;
				}
				a_row.stream = static_cast<Stream>(*i + 1);
			} else if (key == "creature") {
				a_row.creature = std::string(a_value);
			} else if (key == "moved") {
				// only "yes": a misspelt value would light a moved row beside the older carrier that still lights it
				if (!SameText(a_value, "yes")) {
					return false;
				}
				a_row.moved = true;
			} else if (key == "art") {
				a_row.arts.push_back(NormalPath(a_value));
			} else if (key == "impact") {
				a_row.impacts.push_back(NormalPath(a_value));
			} else if (key == "size") {
				if (const auto n = Numbers(a_value, 1); n && n->size() == 1) {
					a_row.size = std::clamp((*n)[0], 0.5f, 8.0f);
				}
			} else if (key == "ember") {
				a_row.ember = SameText(a_value, "yes") || a_value == "1" || SameText(a_value, "true");
			} else if (key == "switch") {
				// a switch no menu has would leave the row lit with nothing to turn it off: the row is left out and counted instead
				a_row.extra = IndexOf(kExtras, a_value);
				if (!a_row.extra) {
					return false;
				}
			}
			return true;
		}
	}

	// One creatures file: [creature] blocks of key=value lines, '#' or ';' starts a comment. A block that names no creature,
	// or whose color does not read, is left out and counted in a_skipped (the caller logs it); a bad number keeps its default.
	[[nodiscard]] inline std::vector<Row> ReadCreatures(std::istream& a_in, std::size_t& a_skipped)
	{
		std::vector<Row>   rows;
		std::optional<Row> row;
		bool               ok = true;
		const auto         finish = [&]() {
			// a stream row draws only along the projectiles its records name
			if (row && ((row->races.empty() && row->models.empty() && row->records.empty() && row->skins.empty() && row->shaders.empty() && row->arts.empty() && row->impacts.empty()) ||
						   (row->stream != Stream::kNone && row->records.empty()) || !ok)) {
				++a_skipped;
			} else if (row) {
				if (row->points.empty()) {
					row->points.push_back({});
				}
				if (row->creature.empty()) {
					row->creature = row->name;
				}
				rows.push_back(std::move(*row));
			}
			row.reset();
			ok = true;
		};
		std::string line;
		while (std::getline(a_in, line)) {
			const auto text = Trim(std::string_view(line).substr(0, line.find_first_of("#;")));
			if (text.empty()) {
				continue;
			}
			if (text.front() == '[') {
				finish();
				if (SameText(text, "[creature]")) {
					row.emplace();
				}
				continue;
			}
			const auto eq = text.find('=');
			if (row && eq != std::string_view::npos && !Detail::Apply(*row, Trim(text.substr(0, eq)), Trim(text.substr(eq + 1)))) {
				ok = false;
			}
		}
		finish();
		return rows;
	}

	// A creature's rows: those of a model its body wears, else those of its race. A reskin that keeps a vanilla race (the Creation Club's
	// liches on the dragon priest race) wears its own body's light, never its race's beside it or instead of it.
	[[nodiscard]] inline std::vector<std::size_t> RowsFor(const std::vector<std::size_t>& a_body, const std::vector<std::size_t>& a_race)
	{
		return a_body.empty() ? a_race : a_body;
	}

	// A see-through creature's ghost rows (shader=, his 573) join its rows only when none of them already gives it a glow: a ghost of a
	// creature we light keeps its own light, and a ghost of anything else (a sabre cat, a person, a spirit summon) gets the ghost's
	template <class IsGlow>
	[[nodiscard]] std::vector<std::size_t> WithGhostRows(std::vector<std::size_t> a_rows, const std::vector<std::size_t>& a_ghost, IsGlow a_isGlow)
	{
		if (!a_ghost.empty() && std::ranges::none_of(a_rows, a_isGlow)) {
			for (const auto i : a_ghost) {
				if (std::ranges::find(a_rows, i) == a_rows.end()) {
					a_rows.push_back(i);
				}
			}
		}
		return a_rows;
	}

	// where a row's lights sit: on a bone it names (its offsets, or the bone itself), else at its points in the creature's root frame
	[[nodiscard]] inline std::vector<Point> SpotsOn(const Row& a_row, bool a_onBone)
	{
		if (!a_onBone) {
			return a_row.points;
		}
		return a_row.offsets.empty() ? std::vector<Point>{ Point{} } : a_row.offsets;
	}

	// ------------------------------------------------------------------ the settings (written by the menu only)
	// the other creature lighting mods: where one lights a creature, ours is the light the player gets and its light there is put out;
	// each has a switch, off by default, that gives its creatures back to its own light and turns ours off on them
	struct OtherMod
	{
		std::string_view key;     // the settings key of its switch
		std::string_view name;    // as the menu shows it
		std::string_view folder;  // its Light Placer folder (lower case); empty = found by its file names or kOtherFiles
		std::string_view stem;    // its Light Placer file names start with this (lower case); empty = any file in the folder
		std::string_view plugin;  // its plugin file without the extension (lower case), when it lights through an ability; empty = none
	};
	// the last entry takes every other mod that lights one of our creatures (Magic Creatures of Radiance and any mod not named here):
	// a mod we do not know has a switch too, and the menu names its Light Placer folder or its plugin file
	inline constexpr std::array<OtherMod, 7> kOtherMods{ {
		{ "TheirsEmissive", "Emissive series (iamzachhunter)", "", "", "" },
		{ "TheirsElementals", "iLluminous Elementals", "illuminouselementals", "", "" },
		{ "TheirsDarenii", "Darenii's Spells - Creatures - CS Lights Addon", "dareniis creatures", "", "" },
		{ "TheirsLuminous", "Luminous Atronachs (Raxyx7)", "", "", "luminousatronachs" },
		{ "TheirsEnhanced", "Enhanced Atronachs (Raxyx7)", "", "", "enhancedatronachs" },
		{ "TheirsIlluminatedAtronachs", "Illuminated Atronachs (Nehluxhes and Le_Magicien)", "", "", "lm_astronach" },
		{ "TheirsOther", "Other mods", "", "", "" },
	} };
	inline constexpr std::size_t             kOthers = kOtherMods.size();
	inline constexpr std::size_t             kOtherUnnamed = kOthers - 1;
	using Lighters = std::array<bool, kOthers>;

	// A mod whose Light Placer folders have plain names ("Atronachs", "Draugr") is known by the exact pair of folder and file name,
	// so another mod's folder of the same name is not taken for it. The Emissive series: one folder per creature download, its files
	// named after what they light.
	struct OtherFile
	{
		std::size_t      mod{};   // its entry in kOtherMods
		std::string_view folder;  // its first folder under LightPlacer (lower case)
		std::string_view stem;    // its file name without .json (lower case)
	};
	inline constexpr std::size_t kEmissive = 0;
	static_assert(kOtherMods[kEmissive].key == "TheirsEmissive");
	inline constexpr std::array<OtherFile, 30> kOtherFiles{ {
		{ kEmissive, "ash spawns", "ash spawns" },
		{ kEmissive, "atronachs", "flame atronach" },
		{ kEmissive, "atronachs", "frost atronach" },
		{ kEmissive, "atronachs", "storm atronach" },
		{ kEmissive, "chaurus", "chaurus" },
		{ kEmissive, "chaurus", "chaurus egg sac" },
		{ kEmissive, "chaurus", "chaurus hunter" },
		{ kEmissive, "critters", "emissive critters" },
		{ kEmissive, "draugr", "dragon priest" },
		{ kEmissive, "draugr", "draugr" },
		{ kEmissive, "dwarven spiders", "emissive dwarven spiders" },
		{ kEmissive, "dynamo cores", "emissive dynamo cores" },
		{ kEmissive, "ice wraiths", "ice wraith" },
		{ kEmissive, "ice wraiths", "ice wraith death explosion" },
		{ kEmissive, "mounts", "arvak" },
		{ kEmissive, "mounts", "daedric horse" },
		{ kEmissive, "netch", "emissive netch" },
		{ kEmissive, "skeletons", "corrupted shades" },
		{ kEmissive, "skeletons", "cursed spectre" },
		{ kEmissive, "skeletons", "dawnguard skeletons" },
		{ kEmissive, "skeletons", "skeletons" },
		{ kEmissive, "spriggans", "corrupted spriggan" },
		{ kEmissive, "spriggans", "spriggan" },
		{ kEmissive, "spriggans", "spriggan burnt" },
		{ kEmissive, "spriggans", "spriggan earth mother" },
		{ kEmissive, "spriggans", "spriggan matron" },
		{ kEmissive, "wisps", "illusion spell explosion effect" },
		{ kEmissive, "wisps", "wisp" },
		{ kEmissive, "wisps", "wisp core" },
		{ kEmissive, "wisps", "wispmother" },
	} };

	// which other mod a Light Placer file belongs to: its first folder under LightPlacer and its file name, lower case. Our own
	// folders are none; a file of a mod not named here is kOtherUnnamed.
	[[nodiscard]] inline std::optional<std::size_t> OtherModOf(std::string_view a_folder, std::string_view a_stem)
	{
		if (a_folder == "illuminated" || a_folder == "illuminatedanima" || a_folder == "illuminatedcreaturelightingng") {
			return std::nullopt;
		}
		for (const auto& f : kOtherFiles) {
			if (f.folder == a_folder && f.stem == a_stem) {
				return f.mod;
			}
		}
		for (std::size_t i = 0; i < kOtherUnnamed; ++i) {
			const auto& m = kOtherMods[i];
			if ((!m.folder.empty() && m.folder == a_folder) || (!m.stem.empty() && a_stem.starts_with(m.stem))) {
				return i;
			}
		}
		return kOtherUnnamed;
	}

	// a Light Placer file as compared everywhere: its path under Data\LightPlacer without .json, lower case, backslashes
	[[nodiscard]] inline std::string LightPlacerKey(std::string_view a_path)
	{
		auto key = Lower(a_path);
		std::ranges::replace(key, '/', '\\');
		return key;
	}

	// the file a Light Placer light came from, off the light's own name: LP_Light[<file>|<its light record>]... ; nullopt for any other light
	[[nodiscard]] inline std::optional<std::string> LightPlacerFile(std::string_view a_name)
	{
		constexpr std::string_view lead = "LP_Light[";
		if (!a_name.starts_with(lead)) {
			return std::nullopt;
		}
		a_name.remove_prefix(lead.size());
		const auto bar = a_name.find('|');
		if (bar == std::string_view::npos || bar == 0) {
			return std::nullopt;
		}
		return LightPlacerKey(a_name.substr(0, bar));
	}

	// Light Placer keeps why a light is hidden in the top byte of the light's first word, and stops judging its conditions while the
	// script bit is set (its own TogglePlacedLight sets that bit). Ours sets it too, with a mark of its own beside it, so a light some
	// script hid by itself stays hidden when ours lets go
	inline constexpr std::uint32_t kCullMask = 0xFF000000u;
	inline constexpr std::uint32_t kCullScript = 1u << 26;
	inline constexpr std::uint32_t kCullOurs = 1u << 29;

	[[nodiscard]] constexpr std::uint32_t CullWord(std::uint32_t a_word, bool a_out) noexcept
	{
		if (a_out) {
			return (a_word & kCullScript) ? a_word : a_word | kCullScript | kCullOurs;
		}
		return (a_word & kCullOurs) ? a_word & ~(kCullScript | kCullOurs) : a_word;
	}

	// the named mod a plugin file belongs to, by its lower-case file name (the extension does not count: a light-flagged copy keeps its name)
	[[nodiscard]] inline std::optional<std::size_t> NamedPluginMod(std::string_view a_file)
	{
		const auto stem = a_file.substr(0, a_file.rfind('.'));
		for (std::size_t i = 0; i < kOtherUnnamed; ++i) {
			if (!kOtherMods[i].plugin.empty() && kOtherMods[i].plugin == stem) {
				return i;
			}
		}
		return std::nullopt;
	}

	// Whether an ability effect a creature's race wears makes it another mod's at data load. a_files = the effect's plugin files, lower
	// case, the one that made it first. An effect that carries a light is its maker's. An effect that carries none is a named mod's
	// when that mod's plugin overrides it and holds a lit ability of its own: a script on the effect adds that ability when the creature
	// loads (Luminous Atronachs), so no record ties it to the race. A plugin with no lit ability (a download without lights) claims nothing.
	[[nodiscard]] inline std::optional<std::size_t> AbilityClaim(bool a_carriesLight, const std::vector<std::string>& a_files, const Lighters& a_hasLitAbility)
	{
		if (a_carriesLight) {
			return a_files.empty() ? kOtherUnnamed : NamedPluginMod(a_files.front()).value_or(kOtherUnnamed);
		}
		for (const auto& file : a_files) {
			if (const auto mod = NamedPluginMod(file); mod && a_hasLitAbility[*mod]) {
				return mod;
			}
		}
		return std::nullopt;
	}

	inline constexpr int kBrightnessMin = 10, kBrightnessMax = 300, kReachMin = 25, kReachMax = 300;

	// a percent setting in the menu's steps of 5, inside its bounds - the menu's slider and the settings file alike
	[[nodiscard]] constexpr int SnapPercent(int a_value, int a_min, int a_max) noexcept
	{
		return std::clamp((std::clamp(a_value, a_min, a_max) + 2) / 5 * 5, a_min, a_max);
	}

	// a skin's addons that count for the warning: those the game puts on a race that wears the skin; every one when no wearer is
	// known, or when no known wearer takes any of them (a race read off a template). A null addon never counts
	template <class Addons, class Races, class Fits>
	[[nodiscard]] auto AddonsWorn(const Addons& a_addons, const Races& a_races, Fits a_fits)
	{
		std::vector<std::ranges::range_value_t<Addons>> all;
		for (const auto& addon : a_addons) {
			if (addon) {
				all.push_back(addon);
			}
		}
		std::vector<std::ranges::range_value_t<Addons>> out;
		for (const auto& addon : all) {
			if (std::ranges::empty(a_races) || std::ranges::any_of(a_races, [&](const auto& a_race) { return a_fits(addon, a_race); })) {
				out.push_back(addon);
			}
		}
		return out.empty() ? all : out;
	}

	template <std::size_t N>
	[[nodiscard]] constexpr std::array<bool, N> AllOn() noexcept
	{
		std::array<bool, N> out{};
		out.fill(true);
		return out;
	}

	struct Settings
	{
		bool                          on{ true };
		std::array<bool, kKinds>      kinds{ true, true, true, true, true };
		bool                          natural{ false };
		int                           brightness{ 100 };
		int                           reach{ 100 };
		bool                          flicker{ true };
		bool                          deathFade{ true };
		bool                          surge{ true };
		bool                          noticed{ false };  // a creature's lights wait until it notices the player (his ask), off by default
		std::array<bool, kOthers>     theirs{};          // that mod's creatures keep its own light, ours off on them
		std::array<bool, kExtraCount> extras{ AllOn<kExtraCount>() };
		// the creatures switched off one by one in the menu (by their creature name); shared, so copying the settings stays cheap
		std::shared_ptr<const std::set<std::string, std::less<>>> creaturesOff;
	};

	// a creature's own menu switch: on unless the menu switched it off
	[[nodiscard]] inline bool CreatureOn(const Settings& a_s, std::string_view a_creature)
	{
		return !a_s.creaturesOff || !a_s.creaturesOff->contains(a_creature);
	}

	// switches one creature on or off: a new list each time, so a copy already handed out never changes under its reader
	inline void SetCreatureOn(Settings& a_s, std::string_view a_creature, bool a_on)
	{
		if (a_creature.empty() || CreatureOn(a_s, a_creature) == a_on) {
			return;
		}
		auto next = a_s.creaturesOff ? std::make_shared<std::set<std::string, std::less<>>>(*a_s.creaturesOff) :
		                               std::make_shared<std::set<std::string, std::less<>>>();
		if (a_on) {
			next->erase(next->find(a_creature));
		} else {
			next->emplace(a_creature);
		}
		a_s.creaturesOff = next->empty() ? nullptr : std::move(next);
	}

	[[nodiscard]] inline bool ReadBool(std::string_view a_value, bool a_default)
	{
		if (a_value == "1" || SameText(a_value, "true") || SameText(a_value, "on") || SameText(a_value, "yes")) {
			return true;
		}
		if (a_value == "0" || SameText(a_value, "false") || SameText(a_value, "off") || SameText(a_value, "no")) {
			return false;
		}
		return a_default;
	}

	[[nodiscard]] inline int ReadInt(std::string_view a_value, int a_default, int a_min, int a_max)
	{
		int        v = 0;
		const auto t = Trim(a_value);
		const auto [end, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
		return ec == std::errc{} && end == t.data() + t.size() ? std::clamp(v, a_min, a_max) : a_default;
	}

	namespace Detail
	{
		// one known key onto the settings; false for a key this version does not know
		inline bool ApplySetting(Settings& a_s, std::string_view a_key, std::string_view a_value)
		{
			const std::array<std::pair<std::string_view, bool*>, 6> switches{ { { "Lights", &a_s.on }, { "NaturalElements", &a_s.natural },
				{ "Flicker", &a_s.flicker }, { "DeathFade", &a_s.deathFade }, { "CastSurge", &a_s.surge }, { "DarkUntilNoticed", &a_s.noticed } } };
			for (const auto& [name, field] : switches) {
				if (SameText(a_key, name)) {
					*field = ReadBool(a_value, *field);
					return true;
				}
			}
			if (SameText(a_key, "Brightness")) {
				a_s.brightness = ReadInt(a_value, a_s.brightness, kBrightnessMin, kBrightnessMax);
				return true;
			}
			if (SameText(a_key, "Reach")) {
				a_s.reach = ReadInt(a_value, a_s.reach, kReachMin, kReachMax);
				return true;
			}
			for (std::size_t i = 0; i < kKinds; ++i) {
				if (SameText(a_key, "Kind" + std::string(kKindNames[i]))) {
					a_s.kinds[i] = ReadBool(a_value, a_s.kinds[i]);
					return true;
				}
			}
			for (std::size_t i = 0; i < kOthers; ++i) {
				if (SameText(a_key, kOtherMods[i].key)) {
					a_s.theirs[i] = ReadBool(a_value, a_s.theirs[i]);
					return true;
				}
			}
			for (std::size_t i = 0; i < kExtraCount; ++i) {
				if (SameText(a_key, kExtras[i])) {
					a_s.extras[i] = ReadBool(a_value, a_s.extras[i]);
					return true;
				}
			}
			if (SameText(a_key, "CreatureOff")) {
				SetCreatureOn(a_s, a_value, false);
				return !a_value.empty();
			}
			return false;
		}
	}

	// Settings.ini: key=value lines (a BOM, blanks, comments and unknown keys are passed over); returns how many keys were read
	inline std::size_t ReadSettings(std::istream& a_in, Settings& a_s)
	{
		std::size_t read = 0;
		std::string line;
		while (std::getline(a_in, line)) {
			const auto text = Trim(std::string_view(line).substr(0, line.find_first_of("#;")));
			const auto eq = text.find('=');
			if (!text.empty() && text.front() != '[' && eq != std::string_view::npos &&
				Detail::ApplySetting(a_s, Trim(text.substr(0, eq)), Trim(text.substr(eq + 1)))) {
				++read;
			}
		}
		return read;
	}

	inline void WriteSettings(const Settings& a_s, std::ostream& a_out)
	{
		a_out << "; Illuminated - Creature Lighting NG - written by its SKSE Menu Framework page. Change the settings there.\n[Settings]\n";
		a_out << "Lights=" << (a_s.on ? 1 : 0) << '\n';
		for (std::size_t i = 0; i < kKinds; ++i) {
			a_out << "Kind" << kKindNames[i] << '=' << (a_s.kinds[i] ? 1 : 0) << '\n';
		}
		a_out << "NaturalElements=" << (a_s.natural ? 1 : 0) << '\n';
		a_out << "Brightness=" << a_s.brightness << '\n';
		a_out << "Reach=" << a_s.reach << '\n';
		a_out << "Flicker=" << (a_s.flicker ? 1 : 0) << '\n';
		a_out << "DeathFade=" << (a_s.deathFade ? 1 : 0) << '\n';
		a_out << "CastSurge=" << (a_s.surge ? 1 : 0) << '\n';
		a_out << "DarkUntilNoticed=" << (a_s.noticed ? 1 : 0) << '\n';
		for (std::size_t i = 0; i < kOthers; ++i) {
			a_out << kOtherMods[i].key << '=' << (a_s.theirs[i] ? 1 : 0) << '\n';
		}
		for (std::size_t i = 0; i < kExtraCount; ++i) {
			a_out << kExtras[i] << '=' << (a_s.extras[i] ? 1 : 0) << '\n';
		}
		// one line per creature switched off; a creature with no line is on, so an older file reads unchanged
		if (a_s.creaturesOff) {
			for (const auto& creature : *a_s.creaturesOff) {
				a_out << "CreatureOff=" << creature << '\n';
			}
		}
	}

	// is this row lit at all with these settings (the lights' switch, its kind's switch, the natural-elements switch, its own switch, and
	// for a creature's own light, that creature's switch)
	[[nodiscard]] inline bool Shown(const Row& a_row, const Settings& a_s)
	{
		return a_s.on && a_s.kinds[static_cast<std::size_t>(a_row.kind)] && (a_s.natural || !Natural(a_row.element)) &&
		       (!a_row.extra || (*a_row.extra < kExtraCount && a_s.extras[*a_row.extra])) &&
		       (!OnThePicker(a_row.kind) || CreatureOn(a_s, a_row.creature));
	}

	// is a creature these mods light given back to them: to any one of them whose switch is on
	[[nodiscard]] inline bool GivenBack(const Lighters& a_lighters, const Settings& a_s) noexcept
	{
		for (std::size_t i = 0; i < kOthers; ++i) {
			if (a_lighters[i] && a_s.theirs[i]) {
				return true;
			}
		}
		return false;
	}

	// is one mod's light on a creature put out: only where ours would show there, and only while that mod's own switch is off
	[[nodiscard]] constexpr bool TheirLightOut(bool a_oursShows, bool a_theirSwitch) noexcept
	{
		return a_oursShows && !a_theirSwitch;
	}

	// ------------------------------------------------------------------ the light over time
	inline constexpr float kDeathFadeSeconds = 4.0f;
	inline constexpr float kEmberShare = 0.25f;  // what stays on an ember creature's remains: a quarter of its strength
	inline constexpr float kSurgeSeconds = 0.6f;
	inline constexpr float kSurgeLift = 0.6f;  // +60 % at the surge's top, easing back down

	// who gets a light within the cap: the living before the dead, a creature's glow before a pair of eyes (a crypt of draugr must not put
	// an atronach out), then the nearest
	struct Candidate
	{
		bool        dead{ false };
		float       distance{ 0.0f };
		std::size_t index{ 0 };  // the caller's own place for it
		bool        faint{ false };
	};

	[[nodiscard]] inline std::vector<std::size_t> ChooseLit(std::vector<Candidate> a_candidates, std::size_t a_cap)
	{
		std::ranges::stable_sort(a_candidates, [](const Candidate& a_l, const Candidate& a_r) {
			if (a_l.dead != a_r.dead) {
				return a_r.dead;
			}
			return a_l.faint != a_r.faint ? a_r.faint : a_l.distance < a_r.distance;
		});
		std::vector<std::size_t> chosen;
		for (std::size_t i = 0; i < a_candidates.size() && i < a_cap; ++i) {
			chosen.push_back(a_candidates[i].index);
		}
		return chosen;
	}

	// whether a lit creature keeps its lights at an event: a queued load hands over its body before the game stores it, so a creature
	// with no body stored yet keeps the lights it got from that load; one with a body keeps them only on that body, while they still show
	[[nodiscard]] constexpr bool KeepsLights(bool a_hasBody, bool a_sameBody, bool a_runOut, bool a_loading) noexcept
	{
		return a_hasBody ? a_sameBody && !a_runOut : a_loading;
	}

	// the share of its strength a light shows a_seconds after its creature died: 1 -> 0 over the fade (or at once without it),
	// an ember creature keeps kEmberShare
	[[nodiscard]] inline float DeathShare(float a_seconds, bool a_fade, bool a_ember) noexcept
	{
		const float floor = a_ember ? kEmberShare : 0.0f;
		if (!a_fade || a_seconds >= kDeathFadeSeconds) {
			return floor;
		}
		const float t = std::clamp(a_seconds / kDeathFadeSeconds, 0.0f, 1.0f);
		const float eased = 1.0f - t * t * (3.0f - 2.0f * t);  // smoothstep: no visible snap at either end
		return floor + (1.0f - floor) * eased;
	}

	// the lift a cast gives a_seconds after it began: up fast, back down over kSurgeSeconds; 1 outside it
	[[nodiscard]] inline float SurgeLift(float a_seconds) noexcept
	{
		if (a_seconds < 0.0f || a_seconds >= kSurgeSeconds) {
			return 1.0f;
		}
		const float t = a_seconds / kSurgeSeconds;
		const float up = t < 0.15f ? t / 0.15f : 1.0f - (t - 0.15f) / 0.85f;
		return 1.0f + kSurgeLift * std::clamp(up, 0.0f, 1.0f);
	}

	// ------------------------------------------------------------------ the keys the game plays (CreatureLights.cpp: a light's dimmer)
	struct Key
	{
		float time{ 0.0f };
		float value{ 0.0f };
	};
	inline constexpr float kKeysPerSecond = 30.0f;  // the shortest shape in Pulse.h (a shock spike) is 45 ms
	inline constexpr float kLoopSeconds = 12.0f;    // the flicker's loop; not every shape's period divides it, so the seam eases (below)
	inline constexpr float kCastSeconds = 60.0f;    // a cast's keys: its surge, then this long of its flicker before the loop comes round
	inline constexpr float kSeamSeconds = 0.25f;    // the last stretch of a loop eases to its first value, so nothing jumps at the seam

	// a light's fade over a_seconds: its flicker (kNone = steady), the cast's surge in front when asked, ending on its first value
	[[nodiscard]] inline std::vector<Key> FadeKeys(float a_fade, Pulse::Kind a_kind, std::uint32_t a_seed, float a_seconds, bool a_surge)
	{
		const auto       count = static_cast<std::size_t>(a_seconds * kKeysPerSecond) + 1;
		const float      first = a_fade * Pulse::Factor(a_kind, 0.0, a_seed);
		std::vector<Key> keys;
		keys.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			const float t = static_cast<float>(i) / kKeysPerSecond;
			float       v = a_fade * Pulse::Factor(a_kind, static_cast<double>(t), a_seed);
			if (a_surge) {
				v *= SurgeLift(t);
			}
			const float seam = (t - (a_seconds - kSeamSeconds)) / kSeamSeconds;
			if (seam > 0.0f) {
				v += (first - v) * (std::min)(seam, 1.0f);
			}
			keys.push_back({ t, v });
		}
		return keys;
	}

	// dark until noticed: a creature's lights follow how far its fight has gone, by the game's combat state (TESCombatEvent: 0 none, 1 in
	// combat, 2 searching) - dark while calm, part way while it searches, full in combat
	inline constexpr float kSearchShare = 0.5f;
	inline constexpr float kNoticeUpSeconds = 1.5f;    // the rise as it notices the player
	inline constexpr float kNoticeDownSeconds = 3.0f;  // the fall as it calms down

	[[nodiscard]] constexpr float NoticeShare(int a_state) noexcept
	{
		return a_state == 1 ? 1.0f : a_state == 2 ? kSearchShare :
		                                            0.0f;
	}

	// a light's fade from a_from to a_to (shares of it), eased over the rise or the fall; with a flicker its keys go on at the new share for
	// kCastSeconds. Played once: the last key holds, so the ramp never comes round again
	[[nodiscard]] inline std::vector<Key> NoticeKeys(float a_fade, Pulse::Kind a_kind, std::uint32_t a_seed, float a_from, float a_to)
	{
		const float      ramp = a_to >= a_from ? kNoticeUpSeconds : kNoticeDownSeconds;
		const float      seconds = a_kind == Pulse::Kind::kNone || a_to <= 0.0f ? ramp : ramp + kCastSeconds;
		const auto       count = static_cast<std::size_t>(seconds * kKeysPerSecond) + 1;
		std::vector<Key> keys;
		keys.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			const float t = static_cast<float>(i) / kKeysPerSecond;
			const float u = std::clamp(t / ramp, 0.0f, 1.0f);
			const float share = a_from + (a_to - a_from) * u * u * (3.0f - 2.0f * u);
			keys.push_back({ t, a_fade * share * Pulse::Factor(a_kind, static_cast<double>(t), a_seed) });
		}
		return keys;
	}

	// a dying light's fade: from what it shows now down to a_end (its ember's fade, or 0) over the death fade, or there at once without it
	[[nodiscard]] inline std::vector<Key> DeathKeys(float a_from, float a_end, bool a_fade)
	{
		if (!a_fade) {
			return { { 0.0f, a_end }, { 0.05f, a_end } };
		}
		const auto       count = static_cast<std::size_t>(kDeathFadeSeconds * 10.0f) + 1;
		std::vector<Key> keys;
		keys.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			const float t = static_cast<float>(i) / 10.0f;
			keys.push_back({ t, a_end + (a_from - a_end) * DeathShare(t, true, false) });
		}
		return keys;
	}
}
