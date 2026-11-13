// Illuminated - Creature Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The rows of light along the creature beams and breaths: a stream= row names its projectile records, and the shared StreamRows module
// (Illuminated's row code, kept equal in every carrier) draws them with this plugin's switches and sliders. One mod draws a given beam: the
// carriers answer in a fixed rank, Creatures first. An Illuminated older than 3.1 or a RELight older than 4.1 still draws these beams with no
// way to ask it, so with one loaded this plugin claims none of them. The rows move once a frame from the player's update, and only while one
// lives.

#include "Plugin.h"
#include "StreamRows.h"

namespace Plugin
{
	namespace
	{
		std::atomic<bool>              gStreams{ false };  // a stream row was claimed at data load: the player-update hook is in
		std::unordered_set<RE::FormID> gNaturalBases;      // written once at data load, read after: the bases whose row is an ice / wind / water / stone one

		// the rows and row lights hung since the game started, for devbench: each frame's rise over the last frame's count, main thread only
		std::atomic<std::uint64_t> gRowsSeen{ 0 };
		std::atomic<std::uint64_t> gLightsSeen{ 0 };
		StreamRows::Counts         gLast{};

		void CountRows()
		{
			const auto now = StreamRows::Stats();
			if (now.rows > gLast.rows) {
				gRowsSeen += now.rows - gLast.rows;
			}
			if (now.lights > gLast.lights) {
				gLightsSeen += now.lights - gLast.lights;
			}
			gLast = now;
		}

		// a Recipe's spray flag is this host's own kind: the row lights only with the natural-elements switch (Anima::Shown's rule)
		[[nodiscard]] bool RowsOn(bool a_natural)
		{
			const auto s = SettingsNow();
			return s.on && s.kinds[static_cast<std::size_t>(Anima::Kind::kSpell)] && (s.natural || !a_natural);
		}

		// an older carrier with no export, which still draws the beams it always drew: its own file version, read once per module
		[[nodiscard]] bool OlderCarrier(const char* a_module, RE::FormID)
		{
			static std::mutex                            lock;
			static std::unordered_map<std::string, bool> older;
			const std::string                            name(a_module ? a_module : "");
			std::scoped_lock                             l{ lock };
			if (const auto it = older.find(name); it != older.end()) {
				return it->second;
			}
			const auto first = name == "Illuminated.dll" ? REL::Version(3, 1, 0, 0) : name == "RELightSpellLightingNG.dll" ? REL::Version(4, 1, 0, 0) :
			                                                                                                                 REL::Version();
			bool       old = false;
			if (const auto module = GetModuleHandleA(name.c_str()); module && first != REL::Version()) {
				std::array<wchar_t, MAX_PATH> path{};
				if (GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size())) > 0) {
					const auto version = REL::GetFileVersion(path.data());
					old = !version || *version < first;
					SKSE::log::info("stream rows: {} {} - {}", name, version ? version->string() : "(no version)",
						old ? "it draws its beams itself, so this plugin stands down for them" : "it asks before drawing");
				}
			}
			older.emplace(name, old);
			return old;
		}

		// once a frame in the world: the original update, then one relaxed read while no row lives
		struct PlayerUpdate
		{
			static void thunk(RE::PlayerCharacter* a_this, float a_delta)
			{
				func(a_this, a_delta);
				if (StreamRows::Awake()) {
					Guarded("stream rows (frame)", []() {
						StreamRows::Frame();
						CountRows();
					});
				} else if (gLast.rows != 0) {
					// asleep means nothing lives: a drop no frame saw must not hide the next row of the same size from the count
					gLast = {};
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void InstallStreams()
	{
		std::unordered_map<RE::FormID, StreamRows::Recipe> rows;
		const auto&                                        all = Rows();
		for (const auto& row : all) {
			if (row.stream == Anima::Stream::kNone) {
				continue;
			}
			// a row's own switch= is not carried by the shared rows' switches, so such a row is left out rather than lit past its switch
			if (row.extra) {
				SKSE::log::info("stream rows: {} has its own switch, which a stream row cannot follow - left out", row.name);
				continue;
			}
			const bool               natural = Anima::Natural(row.element);
			const StreamRows::Recipe recipe{ .color = { row.rgb[0] / 255.0f, row.rgb[1] / 255.0f, row.rgb[2] / 255.0f },
				.fade = row.strength,
				.reach = row.reach,
				.beam = row.stream == Anima::Stream::kBolt,
				.spray = natural,
				.colorFrom = nullptr,
				.cloud = row.stream == Anima::Stream::kCloud };
			for (const auto& key : row.records) {
				auto* form = FormOfKey(key);
				if (const auto* proj = form ? form->As<RE::BGSProjectile>() : nullptr) {
					rows.insert_or_assign(proj->GetFormID(), recipe);
					if (natural) {
						gNaturalBases.insert(proj->GetFormID());
					} else {
						gNaturalBases.erase(proj->GetFormID());
					}
				} else if (form || key.find('~') != std::string::npos) {
					SKSE::log::info("stream rows: {} names {} - not a projectile of this load order", row.name, key);
				}
			}
		}
		if (rows.empty()) {
			return;
		}
		// the shared claim table ranks Creatures first, so its own Owner() never reaches an older carrier: an Illuminated below 3.1 or a
		// RELight below 4.1 draws these beams with no way to ask, so here nothing is claimed and they keep drawing them, never two rows
		if (OlderCarrierLoaded()) {
			SKSE::log::info("stream rows: {} beam(s) left to the older carrier loaded, which draws them itself", rows.size());
			return;
		}
		StreamRows::Host host{};
		host.rowName = "IlluminatedCreatureLightingNGStreamRow";
		host.cloudName = "IlluminatedCreatureLightingNGStreamCloud";
		host.rowOn = [](const StreamRows::Recipe& a_recipe) { return RowsOn(a_recipe.spray); };
		host.cloudOn = [](RE::FormID a_base) { return RowsOn(gNaturalBases.contains(a_base)); };
		host.brightness = []() { return static_cast<float>(SettingsNow().brightness) / 100.0f; };
		host.reach = []() { return static_cast<float>(SettingsNow().reach) / 100.0f; };
		host.flicker = []() { return SettingsNow().flicker; };
		host.shaped = []() { return false; };
		host.lighting = []() { return static_cast<int>(LightingFound()); };
		host.inverseSquare = []() { return InverseSquare(); };
		host.lightLimitFix = []() { return LightLimitFix(); };
		host.oldCarrier = OlderCarrier;
		StreamRows::Install(host);
		StreamRows::Claim(std::move(rows));
		// PlayerCharacter::Update sits one slot higher on VR (every slot from 0x82 on)
		PlayerUpdate::func = REL::Relocation<std::uintptr_t>(RE::VTABLE_PlayerCharacter[0]).write_vfunc(REL::Relocate(0xAD, 0xAD, 0xAE), PlayerUpdate::thunk);
		gStreams = true;
	}

	void RefreshStreams()
	{
		if (gStreams) {
			StreamRows::Refresh();
		}
	}

	void DropStreams()
	{
		if (gStreams) {
			StreamRows::DropAll();
			gLast = {};
		}
	}

	bool OlderCarrierLoaded()
	{
		return OlderCarrier("Illuminated.dll", 0) || OlderCarrier("RELightSpellLightingNG.dll", 0);
	}

	std::array<std::uint64_t, 4> StreamCounts()
	{
		const auto now = gStreams ? StreamRows::Stats() : StreamRows::Counts{};
		return { now.rows, now.lights, gRowsSeen.load(std::memory_order_relaxed), gLightsSeen.load(std::memory_order_relaxed) };
	}
}
