// Illuminated - Creature Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The spells creatures use and their explosions, lit the game's own way (Illuminated's record route, RecordLights.cpp): the
// game already gives a spell its lights through four record slots - a magic effect's casting light (the art in a creature's
// hands or mouth while it casts), a projectile's light, an explosion's light and a hazard's light - and it makes, moves and
// removes those lights itself, on every lighting and in VR. So each record a spell or burst row names (by its model, or by the
// record itself) is pointed, in memory, at a light of ours made from the row: the row's color, strength and reach, written in
// Community Shaders' inverse-square words on Community Shaders. Nothing of ours runs while the game plays; a menu switch points
// the records back at their own lights (or none). Nothing reaches a save: these are base records the game never writes.
//
// RE::Light files, only when the download carries them and RE::Light runs: a record with a config there is lit by RE::Light, which
// hangs an "RL" light on the reference as it loads, so the record keeps its own light and no light of ours is pointed at it. Nothing
// announces those lights, so each kind of reference RE::Light lights is hooked after it (its Load3D hooks go in at plugin load, ours at
// data load, so the original call runs its light-making first), and a row the menu switched off culls them there - but never on a record
// another mod's RE::Light file names too, whose light may be theirs. Without the files no hook goes in at all.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::uint32_t kWhiteLight = 0x012FD6;  // Skyrim.esm MagicLightWhite01: a plain light to copy where a record has none

		struct Target
		{
			RE::TESObjectLIGH** slot{ nullptr };
			RE::TESObjectLIGH*  own{ nullptr };  // the record's own light (or none), given back when the row is not shown
			RE::TESObjectLIGH*  ours{ nullptr };
			std::size_t         row{ 0 };
			const RE::TESForm*  record{ nullptr };
			const char*         model{ nullptr };  // what RE::Light's mesh keys are matched on; none for a casting art, which it cannot reach
			bool                inFiles{ false };  // an RE::Light file of ours lights this record
			bool                shared{ false };   // another mod's RE::Light file names it too: its "RL" light may be theirs, never culled
		};
		std::vector<Target> gTargets;

		constexpr const char*       kReLightConfigs = "Data/SKSE/Plugins/ReLight/Configs";
		constexpr std::wstring_view kOurFolder = L"illuminated creature lighting ng";  // ours, under kReLightConfigs (lower case)

		// written at data load before any hook goes in; read from loader threads
		std::unordered_map<RE::FormID, std::size_t> gInFiles;  // a base record -> its place in gTargets
		std::vector<LightKit::Relaxed<bool>>        gCull;     // by that place: its row is off, so RE::Light's lights on it go out
		LightKit::Relaxed<bool>                     gAnyCull{ false };

		// RE::Light named a light of its own "RL" and hung it under the reference; a culled one lights nothing (RELight's Options.cpp way)
		void CullFileLights(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_node) noexcept
		{
			Guarded("RE::Light file lights", [&]() {
				const auto* base = a_ref->GetBaseObject();
				const auto  it = base ? gInFiles.find(base->GetFormID()) : gInFiles.end();
				if (it == gInFiles.end() || !gCull[it->second]) {
					return;
				}
				RE::BSVisit::TraverseScenegraphLights(a_node, [](RE::NiPointLight* a_light) {
					if (a_light && std::string_view(a_light->name.c_str()).starts_with("RL")) {
						a_light->SetAppCulled(true);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			});
		}

		// every class RE::Light hooks for this. With no row off this is the original call and one read
		template <class T>
		struct FileLoad3D
		{
			static RE::NiAVObject* thunk(T* a_this, bool a_background)
			{
				auto* node = func(a_this, a_background);
				if (gAnyCull && node && a_this) {
					CullFileLights(a_this, node);
				}
				return node;
			}
			static void                                    Install() { func = REL::Relocation<std::uintptr_t>(T::VTABLE[0]).write_vfunc(0x6A, thunk); }
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct ConfigKeys
		{
			std::vector<std::string>               keys;
			std::unordered_set<const RE::TESForm*> bases;
		};

		[[nodiscard]] bool IsOurFolder(const std::filesystem::path& a_path)
		{
			const auto name = a_path.filename().wstring();
			return std::ranges::equal(name, kOurFolder, [](wchar_t a, wchar_t b) { return std::towlower(a) == b; });
		}

		// the mesh keys and base forms the RE::Light configs under kReLightConfigs name: ours (our folder) or every other mod's
		void ReadConfigs(bool a_ours, ConfigKeys& a_out)
		{
			std::error_code ec;
			const auto      root = std::filesystem::current_path(ec) / kReLightConfigs;
			using enum std::filesystem::directory_options;
			for (std::filesystem::recursive_directory_iterator it(root, skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code one;
				if (it.depth() == 0 && it->is_directory(one) && IsOurFolder(it->path()) != a_ours) {
					it.disable_recursion_pending();
					continue;
				}
				if (it.depth() == 0 && a_ours) {
					continue;  // a file straight in the configs folder is no file of ours
				}
				if (!it->is_regular_file(one) || it->path().extension() != ".json") {
					continue;
				}
				std::ifstream in(it->path(), std::ios::binary);
				const auto    doc = nlohmann::json::parse(in, nullptr, false, true);
				if (!doc.is_array()) {
					continue;
				}
				for (const auto& entry : doc) {
					if (!entry.is_object()) {
						continue;
					}
					if (const auto mesh = entry.find("meshPath"); mesh != entry.end() && mesh->is_array()) {
						for (const auto& key : *mesh) {
							if (key.is_string()) {
								a_out.keys.push_back(key.get<std::string>());
							}
						}
					}
					if (const auto id = entry.find("baseID"); id != entry.end() && id->is_string()) {
						if (const auto* form = FormOfKey(id->get<std::string>())) {
							a_out.bases.insert(form);
						}
					}
				}
			}
		}

		[[nodiscard]] bool Names(const ConfigKeys& a_files, const Target& a_t)
		{
			return a_t.record && (a_files.bases.contains(a_t.record) || (a_t.model && Anima::MeshKeyNames(a_t.model, a_files.keys)));
		}

		// which records our RE::Light files name, by their base form or a mesh key; none unless RE::Light runs and the files are there
		void FindFileRows()
		{
			if (!REX::W32::GetModuleHandleA("ReLight.dll")) {
				return;
			}
			ConfigKeys ours;
			ReadConfigs(true, ours);
			if (ours.keys.empty() && ours.bases.empty()) {
				return;
			}
			ConfigKeys others;
			ReadConfigs(false, others);
			gCull.assign(gTargets.size(), false);
			std::size_t shared = 0;
			for (std::size_t i = 0; i < gTargets.size(); ++i) {
				auto& t = gTargets[i];
				if (Names(ours, t)) {
					t.inFiles = true;
					t.shared = Names(others, t);
					shared += t.shared ? 1 : 0;
					gInFiles.try_emplace(t.record->GetFormID(), i);
				}
			}
			SKSE::log::info("creature spells: RE::Light runs with our files: {} record(s) lit by them, {} also named by another mod's file (never culled)",
				gInFiles.size(), shared);
			if (gInFiles.empty()) {
				return;
			}
			FileLoad3D<RE::BarrierProjectile>::Install();
			FileLoad3D<RE::BeamProjectile>::Install();
			FileLoad3D<RE::ConeProjectile>::Install();
			FileLoad3D<RE::MissileProjectile>::Install();
			FileLoad3D<RE::ArrowProjectile>::Install();
			FileLoad3D<RE::GrenadeProjectile>::Install();
			FileLoad3D<RE::Hazard>::Install();
			FileLoad3D<RE::Explosion>::Install();
		}

		// a light of ours for a row, copied from the record's own light (its flags and model) or the game's plain white one
		[[nodiscard]] RE::TESObjectLIGH* MakeLight(const Anima::Row& a_row, const RE::TESObjectLIGH* a_base)
		{
			auto* out = a_base ? LightKit::NewForm<RE::TESObjectLIGH>() : nullptr;
			if (!out) {
				return nullptr;
			}
			out->data = a_base->data;
			out->fade = a_base->fade;
			out->emittanceColor = a_base->emittanceColor;
			out->lensFlare = a_base->lensFlare;
			out->SetModel(a_base->GetModel());
			out->data.color = RE::Color(static_cast<std::uint8_t>(a_row.rgb[0]), static_cast<std::uint8_t>(a_row.rgb[1]),
				static_cast<std::uint8_t>(a_row.rgb[2]), 0);
			return out;
		}

		// the row's strength and reach onto our light, the sliders on them (Community Shaders: inverse square, the cutoff in the falloff
		// field and the size in the field of view, as it reads a record; else a plain light whose radius carries the reach)
		void Dress(RE::TESObjectLIGH* a_light, const Anima::Row& a_row, const Anima::Settings& a_s)
		{
			const float strength = a_row.strength * static_cast<float>(a_s.brightness) / 100.0f;
			const float reach = a_row.reach * static_cast<float>(a_s.reach) / 100.0f;
			if (InverseSquare()) {
				a_light->fade = strength;
				a_light->data.radius = static_cast<std::uint32_t>(reach);
				a_light->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(a_light->data.flags.underlying() | LightKit::kRecordInverseSquare);
				a_light->data.fov = LightKit::LightPlacerSize(a_row.size);
				a_light->data.fallofExponent = LightKit::CutoffFor(strength, reach, a_row.size);
			} else {
				const auto plain = LightKit::PlainAt(strength, reach, reach);
				a_light->fade = plain.fade;
				a_light->data.radius = static_cast<std::uint32_t>(plain.radius);
			}
		}

		void Add(RE::TESObjectLIGH*& a_slot, std::size_t a_row, const RE::TESObjectLIGH* a_white, const RE::TESForm* a_record, const char* a_model)
		{
			if (std::ranges::any_of(gTargets, [&](const Target& a_t) { return a_t.slot == &a_slot; })) {
				return;  // the first row that names a record wins
			}
			auto* ours = MakeLight(Rows()[a_row], a_slot ? a_slot : a_white);
			if (ours) {
				gTargets.push_back({ &a_slot, a_slot, ours, a_row, a_record, a_model, false });
			}
		}
	}

	void ReadSpellRecords()
	{
		gTargets.clear();
		const auto&                                  rows = Rows();
		std::unordered_map<std::string, std::size_t> byModel;
		for (std::size_t i = 0; i < rows.size(); ++i) {
			if (rows[i].kind != Anima::Kind::kSpell && rows[i].kind != Anima::Kind::kBurst) {
				continue;
			}
			for (const auto& model : rows[i].models) {
				byModel.try_emplace(model, i);
			}
		}
		auto*       dh = RE::TESDataHandler::GetSingleton();
		const auto* white = dh ? dh->LookupForm<RE::TESObjectLIGH>(kWhiteLight, "Skyrim.esm") : nullptr;
		const auto  rowOf = [&](const char* a_model) -> std::optional<std::size_t> {
			if (!a_model || !*a_model) {
				return std::nullopt;
			}
			const auto it = byModel.find(Anima::NormalPath(a_model));
			return it != byModel.end() ? std::optional<std::size_t>(it->second) : std::nullopt;
		};
		// the records named by form first: they win over a model shared with other records
		for (std::size_t i = 0; i < rows.size(); ++i) {
			for (const auto& key : rows[i].records) {
				auto* form = FormOfKey(key);
				if (auto* p = form ? form->As<RE::BGSProjectile>() : nullptr) {
					Add(p->data.light, i, white, p, p->GetModel());
				} else if (auto* e = form ? form->As<RE::BGSExplosion>() : nullptr) {
					Add(e->data.light, i, white, e, e->GetModel());
				} else if (auto* h = form ? form->As<RE::BGSHazard>() : nullptr) {
					Add(h->data.light, i, white, h, h->GetModel());
				} else if (auto* m = form ? form->As<RE::EffectSetting>() : nullptr) {
					Add(m->data.light, i, white, m, nullptr);
				} else if (form || key.find('~') != std::string::npos) {
					SKSE::log::info("creature spells: {} names {} - not a projectile, explosion, hazard or magic effect of this load order",
						rows[i].name, key);
				}
			}
		}
		if (!byModel.empty()) {
			for (auto* p : LightKit::FormsOf<RE::BGSProjectile>()) {
				if (const auto r = p ? rowOf(p->GetModel()) : std::nullopt) {
					Add(p->data.light, *r, white, p, p->GetModel());
				}
			}
			for (auto* e : LightKit::FormsOf<RE::BGSExplosion>()) {
				if (const auto r = e ? rowOf(e->GetModel()) : std::nullopt) {
					Add(e->data.light, *r, white, e, e->GetModel());
				}
			}
			for (auto* h : LightKit::FormsOf<RE::BGSHazard>()) {
				if (const auto r = h ? rowOf(h->GetModel()) : std::nullopt) {
					Add(h->data.light, *r, white, h, h->GetModel());
				}
			}
			// a magic effect's casting light shows while its casting art is in the caster's hands (or a creature's mouth)
			for (auto* m : LightKit::FormsOf<RE::EffectSetting>()) {
				const auto* art = m ? m->data.castingArt : nullptr;
				if (const auto r = art ? rowOf(art->GetModel()) : std::nullopt) {
					Add(m->data.light, *r, white, m, nullptr);
				}
			}
		}
		SKSE::log::info("creature spells: {} record(s) given a light of ours", gTargets.size());
		FindFileRows();
	}

	void ApplySpellRecords(const Anima::Settings& a_s)
	{
		const auto& rows = Rows();
		bool        anyCull = false;
		for (std::size_t i = 0; i < gTargets.size(); ++i) {
			auto& t = gTargets[i];
			// another mod's Light Placer file on this art lights it instead: ours steps aside, as on a creature's body
			const bool shown = t.row < rows.size() && Anima::Shown(rows[t.row], a_s) && !RowModelsAside(t.row, a_s);
			if (t.inFiles) {
				const bool cull = !shown && !t.shared;
				gCull[i] = cull;
				anyCull = anyCull || cull;
			}
			const bool ours = shown && !t.inFiles;
			if (ours) {
				Dress(t.ours, rows[t.row], a_s);
			}
			*t.slot = ours ? t.ours : t.own;
		}
		gAnyCull = anyCull;
	}

	std::size_t SpellRecordCount()
	{
		return gTargets.size();
	}
}
