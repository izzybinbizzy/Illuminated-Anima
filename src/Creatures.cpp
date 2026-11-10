// Illuminated Anima - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The creatures files (Data\SKSE\Plugins\IlluminatedAnima\Creatures\*.txt, read in name order; every row that names a creature
// lights it), the races and body models they name, and what the other creature lighting mods light: their Light Placer files and
// their abilities that carry a light - all read once at data load. A creature is found by its race first, then by a model its body
// wears (the skin's armor addons), so a body replacer that keeps the race keeps the light. An ability can reach a creature no record
// shows (a mod's script adds it): the game's events say so, and what a creature wears is read off it when it loads, never on a timer.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kFolder = "Data/SKSE/Plugins/IlluminatedAnima/Creatures";

		std::vector<Anima::Row>                                                 gRows;
		std::unordered_map<RE::TESRace*, std::vector<std::size_t>>              gByRace;
		std::unordered_map<std::string, std::vector<std::size_t>>               gByModel;      // NormalPath -> rows
		std::unordered_set<const RE::TESObjectARMO*>                            gRowSkins;     // every skin a character wears that holds a model of gByModel, or a row names
		std::unordered_map<const RE::TESObjectARMO*, std::vector<std::size_t>>  gBySkin;       // a skin record a row names (skin=) -> rows
		std::unordered_map<const RE::TESObjectARMO*, std::vector<RE::TESRace*>> gSkinRaces;    // every skin -> the races that wear it
		std::unordered_map<std::string, Anima::Lighters>                        gClaims;       // a body model other mods' Light Placer files light -> every one of those mods
		std::unordered_map<std::string, std::string>                            gClaimFolder;  // the same model -> the Light Placer folder of a mod we do not name
		Anima::Lighters                                                         gFound{};
		std::size_t                                                             gFiles{ 0 };
		// gFound and the warning's lists: written on the main thread (a creature met in play adds to them), read by the menu's render thread
		std::mutex gOthersLock;

		// an addon the game puts on a race: a skin may also list another race's addon (vanilla dwarven skins carry the draugr body's)
		[[nodiscard]] bool Wears(const RE::TESObjectARMA* a_addon, RE::TESRace* a_race)
		{
			return a_addon && (!a_race || a_addon->IsValidRace(a_race));
		}

		// a path as UTF-8 for the log and the names (path::string() throws on a name the local code page cannot hold)
		[[nodiscard]] std::string Utf8(const std::filesystem::path& a_path)
		{
			const auto  u8 = a_path.u8string();
			std::string out(u8.size(), '\0');
			std::ranges::transform(u8, out.begin(), [](char8_t c) { return static_cast<char>(c); });
			return out;
		}

		void ReadFiles()
		{
			std::error_code                    ec;
			std::vector<std::filesystem::path> files;
			const auto                         root = std::filesystem::current_path(ec);  // the non-throwing form: no exception at data load
			for (std::filesystem::directory_iterator it(root / kFolder, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code one;
				if (it->is_regular_file(one) && Lower(Utf8(it->path().extension())) == ".txt") {
					files.push_back(it->path());
				}
			}
			std::ranges::sort(files);
			for (const auto& path : files) {
				std::ifstream in(path);
				std::size_t   skipped = 0;
				auto          rows = Anima::ReadCreatures(in, skipped);
				SKSE::log::info("creatures file {}: {} creature(s){}", Utf8(path.filename()), rows.size(),
					skipped ? std::format(", {} block(s) left out (no creature named, or a color that does not read)", skipped) : "");
				std::ranges::move(rows, std::back_inserter(gRows));
				++gFiles;
			}
			if (files.empty()) {
				SKSE::log::warn("no creatures file in {} - nothing is lit", kFolder);
			}
		}

		void IndexRows()
		{
			// the spell and burst rows light game records (SpellLights.cpp), never a creature's body
			const auto onBody = [](const Anima::Row& a_r) { return a_r.kind == Anima::Kind::kGlow || a_r.kind == Anima::Kind::kEyes; };
			for (std::size_t i = 0; i < gRows.size(); ++i) {
				if (!onBody(gRows[i])) {
					continue;
				}
				for (const auto& model : gRows[i].models) {
					gByModel[model].push_back(i);
				}
			}
			// every row of a race that names it (a creature's glow and its eyes are two rows)
			std::size_t races = 0;
			for (auto* race : LightKit::FormsOf<RE::TESRace>()) {
				const char* id = race ? race->formEditorID.c_str() : nullptr;
				if (!id || !*id) {
					continue;
				}
				const auto lower = Lower(id);
				for (std::size_t i = 0; i < gRows.size(); ++i) {
					if (onBody(gRows[i]) && std::ranges::find(gRows[i].races, lower) != gRows[i].races.end()) {
						gByRace[race].push_back(i);
					}
				}
				races += gByRace.contains(race) ? 1 : 0;
			}
			// the skins that wear a row's model, read once: asking whether a character could be ours then needs no list of its models
			const auto readSkin = [&](RE::TESObjectARMO* a_skin, RE::TESRace* a_race) {
				if (!a_skin) {
					return;
				}
				auto& races = gSkinRaces[a_skin];
				if (std::ranges::find(races, a_race) != races.end()) {
					return;
				}
				races.push_back(a_race);
				// wide on purpose: a creature whose race changes at run time still passes, and BodyModels takes its race's addons
				for (auto* addon : a_skin->armorAddons) {
					if (!addon) {
						continue;
					}
					for (auto& model : addon->bipedModels) {
						if (const char* path = model.GetModel(); path && *path && gByModel.contains(Anima::NormalPath(path))) {
							gRowSkins.insert(a_skin);
						}
					}
				}
			};
			// read even with no model named: the warning's skin= rows need each skin's wearers
			for (auto* race : LightKit::FormsOf<RE::TESRace>()) {
				readSkin(race ? race->skin : nullptr, race);
			}
			for (auto* npc : LightKit::FormsOf<RE::TESNPC>()) {
				readSkin(npc ? npc->skin : nullptr, npc ? npc->race : nullptr);
			}
			// a skin a row names by record: two looks of one body model (the elytra nymph's Mania and Dementia) told apart by what they wear
			for (std::size_t i = 0; i < gRows.size(); ++i) {
				if (!onBody(gRows[i])) {
					continue;
				}
				for (const auto& key : gRows[i].skins) {
					auto* form = FormOfKey(key);
					if (auto* skin = form ? form->As<RE::TESObjectARMO>() : nullptr) {
						gBySkin[skin].push_back(i);
						gRowSkins.insert(skin);
					}
				}
			}
			SKSE::log::info("creatures: {} row(s), {} race(s) of this load order named, {} body model(s) named, {} skin record(s) named, on {} skin(s)",
				gRows.size(), races, gByModel.size(), gBySkin.size(), gRowSkins.size());
		}

		// what the other creature lighting mods' Light Placer files light, by body model - only while Light Placer runs
		void ReadClaims()
		{
			if (!REX::W32::GetModuleHandleA("po3_LightPlacer.dll")) {
				SKSE::log::info("Light Placer is not loaded - no other creature lighting mod's file can light a creature");
				return;
			}
			std::error_code ec;
			const auto      root = std::filesystem::current_path(ec) / "Data" / "LightPlacer";
			std::size_t     files = 0;
			using enum std::filesystem::directory_options;
			for (std::filesystem::recursive_directory_iterator it(root, skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
				std::error_code one;
				if (!it->is_regular_file(one) || Lower(Utf8(it->path().extension())) != ".json") {
					continue;
				}
				const auto rel = it->path().lexically_relative(root);
				const auto folder = rel.begin() != rel.end() && std::next(rel.begin()) != rel.end() ? Lower(Utf8(*rel.begin())) : std::string{};
				const auto mod = Anima::OtherModOf(folder, Lower(Utf8(it->path().stem())));
				if (!mod) {
					continue;
				}
				std::ifstream in(it->path(), std::ios::binary);
				const auto    doc = nlohmann::json::parse(in, nullptr, false, true);  // no exceptions; a comment does not drop the file
				if (!doc.is_array()) {
					continue;
				}
				++files;
				// a named mod is offered by its files alone; any other Light Placer file only once it lights one of our creatures
				gFound[*mod] = gFound[*mod] || *mod != Anima::kOtherUnnamed;
				for (const auto& entry : doc) {
					const auto models = entry.is_object() ? entry.find("models") : entry.end();
					if (models == entry.end() || !models->is_array()) {
						continue;
					}
					for (const auto& m : *models) {
						if (!m.is_string()) {
							continue;
						}
						const auto path = Anima::NormalPath(m.get<std::string>());
						gClaims[path][*mod] = true;  // two mods on one model are both named, each with its own switch
						if (*mod == Anima::kOtherUnnamed) {
							gClaimFolder.try_emplace(path, Utf8(*rel.begin()));
						}
					}
				}
			}
			SKSE::log::info("other creature lighting mods: {} Light Placer file(s), {} body model(s) claimed", files, gClaims.size());
			for (std::size_t i = 0; i < Anima::kOthers; ++i) {
				if (gFound[i]) {
					SKSE::log::info("  {} is installed", Anima::kOtherMods[i].name);
				}
			}
		}

		// an actor's own skin, else its race's
		[[nodiscard]] RE::TESObjectARMO* SkinOf(RE::Actor* a_actor)
		{
			auto* npc = a_actor->GetActorBase();
			auto* race = a_actor->GetRace();
			return npc && npc->skin ? npc->skin : race ? race->skin :
			                                             nullptr;
		}

		// the models an actor's body wears: its skin's armor addons for its race and sex
		[[nodiscard]] std::vector<std::string> BodyModels(RE::Actor* a_actor)
		{
			std::vector<std::string> out;
			auto*                    npc = a_actor ? a_actor->GetActorBase() : nullptr;
			auto*                    race = a_actor ? a_actor->GetRace() : nullptr;
			auto*                    skin = a_actor ? SkinOf(a_actor) : nullptr;
			if (!skin) {
				return out;
			}
			const auto sex = npc ? npc->GetSex() : RE::SEX::kMale;
			for (auto* addon : skin->armorAddons) {
				if (!Wears(addon, race)) {
					continue;
				}
				const char* model = addon->bipedModels[sex == RE::SEX::kFemale ? 1 : 0].GetModel();
				// a female creature's addon may fill only the first slot
				if (!model || !*model) {
					model = addon->bipedModels[0].GetModel();
				}
				if (model && *model) {
					out.push_back(Anima::NormalPath(model));
				}
			}
			return out;
		}
	}

	namespace
	{
		std::array<std::vector<std::string>, Anima::kOthers> gReplaced;  // per other mod: the names of our creatures its config lights

		// which of our creatures each other mod's config lights instead of us: a row's own body models, and the models of every skin its races
		// wear - a race's own and its NPCs' (for the menu's warning)
		void FindReplaced()
		{
			gReplaced = {};
			if (gClaims.empty()) {
				return;
			}
			std::vector<std::vector<std::string>> models(gRows.size());
			for (std::size_t i = 0; i < gRows.size(); ++i) {
				models[i] = gRows[i].models;
			}
			for (const auto& [skin, rows] : gBySkin) {
				const auto wearers = gSkinRaces.find(skin);
				const auto worn = Anima::AddonsWorn(skin->armorAddons, wearers != gSkinRaces.end() ? wearers->second : std::vector<RE::TESRace*>{}, Wears);
				for (auto* addon : worn) {
					if (!addon) {
						continue;
					}
					for (auto& model : addon->bipedModels) {
						if (const char* path = model.GetModel(); path && *path) {
							for (const auto i : rows) {
								models[i].push_back(Anima::NormalPath(path));
							}
						}
					}
				}
			}
			// every skin a race of ours wears - its own and its NPCs' - and of each the addons the game puts on that race
			for (const auto& [skin, races] : gSkinRaces) {
				for (auto* race : races) {
					const auto rows = gByRace.find(race);
					if (rows == gByRace.end()) {
						continue;
					}
					for (auto* addon : skin->armorAddons) {
						if (!Wears(addon, race)) {
							continue;
						}
						for (auto& model : addon->bipedModels) {
							if (const char* path = model.GetModel(); path && *path) {
								for (const auto i : rows->second) {
									models[i].push_back(Anima::NormalPath(path));
								}
							}
						}
					}
				}
			}
			for (std::size_t i = 0; i < gRows.size(); ++i) {
				for (const auto& model : models[i]) {
					const auto it = gClaims.find(model);
					if (it == gClaims.end()) {
						continue;
					}
					for (std::size_t mod = 0; mod < Anima::kOthers; ++mod) {
						if (!it->second[mod]) {
							continue;
						}
						auto& names = gReplaced[mod];
						// a mod we do not name is told by its Light Placer folder
						const auto folder = gClaimFolder.find(model);
						const auto name = mod == Anima::kOtherUnnamed && folder != gClaimFolder.end() ?
						                      std::format("{} ({})", gRows[i].name, folder->second) :
						                      gRows[i].name;
						if (std::ranges::find(names, name) == names.end()) {
							names.push_back(name);
						}
						gFound[mod] = true;
					}
				}
			}
		}
	}

	namespace
	{
		// an ability whose magic effect carries a light: the mod it belongs to, and its plugin file for the warning when no mod is named.
		// Written only at data load, before the game's events are sunk: a sink reads this from any thread. Read before our own record
		// lights are pointed (SpellLights.cpp), so no light of ours counts.
		struct AbilityLight
		{
			std::size_t mod{ Anima::kOtherUnnamed };
			std::string file;
		};
		std::unordered_map<RE::FormID, AbilityLight>            gAbilityLights;
		std::unordered_map<const RE::TESRace*, Anima::Lighters> gRaceLights;  // the mods whose ability lights every creature of a race
		// main thread: the actors the game's events and loads say wear such an ability, and each pair of effect and race already warned of
		std::unordered_map<RE::FormID, Anima::Lighters> gWore;
		std::unordered_set<std::uint64_t>               gNamed;

		[[nodiscard]] std::vector<std::string> FilesOf(const RE::TESForm* a_form)
		{
			std::vector<std::string> out;
			if (const auto* files = a_form ? a_form->sourceFiles.array : nullptr) {
				for (const auto* file : *files) {
					if (file) {
						out.push_back(Lower(file->GetFilename()));
					}
				}
			}
			return out;
		}

		[[nodiscard]] bool IsAbility(const RE::SpellItem* a_spell)
		{
			return a_spell && a_spell->GetSpellType() == RE::MagicSystem::SpellType::kAbility;
		}

		// one of our rows goes into the warning under the mod that lights it (a mod we do not name is told by its plugin file)
		void AddReplaced(std::size_t a_mod, const Anima::Row& a_row, std::string_view a_file)
		{
			auto             name = a_mod == Anima::kOtherUnnamed ? std::format("{} ({})", a_row.name, a_file) : a_row.name;
			std::scoped_lock l{ gOthersLock };
			gFound[a_mod] = true;
			if (auto& names = gReplaced[a_mod]; std::ranges::find(names, name) == names.end()) {
				names.push_back(std::move(name));
			}
		}

		// every ability that carries a light, then the races that wear one or wear an effect a named mod's script hangs one on
		void FindAbilityLights()
		{
			Anima::Lighters named{};
			for (auto* spell : LightKit::FormsOf<RE::SpellItem>()) {
				if (!IsAbility(spell)) {
					continue;
				}
				for (const auto* effect : spell->effects) {
					const auto* base = effect ? effect->baseEffect : nullptr;
					if (!base || !base->data.light || gAbilityLights.contains(base->GetFormID())) {
						continue;
					}
					const auto files = FilesOf(base);
					const auto mod = Anima::AbilityClaim(true, files, named).value_or(Anima::kOtherUnnamed);
					gAbilityLights.emplace(base->GetFormID(), AbilityLight{ mod, files.empty() ? std::string{} : files.front() });
					named[mod] = mod != Anima::kOtherUnnamed;
				}
			}
			{
				std::scoped_lock l{ gOthersLock };
				for (std::size_t m = 0; m < Anima::kOthers; ++m) {
					gFound[m] = gFound[m] || named[m];
				}
			}
			for (const auto& [race, rows] : gByRace) {
				const auto* list = race ? race->actorEffects : nullptr;
				for (std::uint32_t s = 0; list && list->spells && s < list->numSpells; ++s) {
					if (!IsAbility(list->spells[s])) {
						continue;
					}
					for (const auto* effect : list->spells[s]->effects) {
						const auto* base = effect ? effect->baseEffect : nullptr;
						const auto  files = FilesOf(base);
						const auto  claim = base ? Anima::AbilityClaim(gAbilityLights.contains(base->GetFormID()), files, named) : std::nullopt;
						if (!claim) {
							continue;
						}
						gRaceLights[race][*claim] = true;
						for (const auto i : rows) {
							AddReplaced(*claim, gRows[i], files.empty() ? std::string_view{} : std::string_view(files.front()));
						}
					}
				}
			}
			SKSE::log::info("other creature lighting mods: {} abilit(ies) carry a light, {} race(s) lit by one", gAbilityLights.size(), gRaceLights.size());
		}

		// the rows that step aside while another mod's own switch is on: JS Emissive Eyes lights no light of its own, its eyes are art a
		// script plays once its menu sets a global for that creature, so the global is the only word there is
		struct RowGlobal
		{
			const RE::TESGlobal* global{ nullptr };
			std::size_t          mod{ Anima::kOtherUnnamed };
			std::string          file;
			bool                 told{ false };  // main thread: it is in the warning
		};
		std::unordered_map<std::size_t, std::vector<RowGlobal>> gRowGlobals;

		void FindRowGlobals()
		{
			Anima::Lighters found{};
			for (std::size_t i = 0; i < gRows.size(); ++i) {
				for (const auto& key : gRows[i].asideIf) {
					auto*      form = FormOfKey(key);
					auto*      global = form ? form->As<RE::TESGlobal>() : nullptr;
					const auto tilde = key.find('~');
					if (!global || tilde == std::string::npos) {
						continue;
					}
					auto       file = Lower(Anima::Trim(std::string_view(key).substr(tilde + 1)));
					const auto mod = Anima::NamedPluginMod(file).value_or(Anima::kOtherUnnamed);
					found[mod] = mod != Anima::kOtherUnnamed;
					gRowGlobals[i].push_back({ global, mod, std::move(file), false });
				}
			}
			std::scoped_lock l{ gOthersLock };
			for (std::size_t m = 0; m < Anima::kOthers; ++m) {
				gFound[m] = gFound[m] || found[m];
			}
		}

		// the effects a creature wears that carry a light, through the game's own walk of them (it holds their lock): marked after it, so no
		// lock of ours is taken inside the game's
		class WornLights final : public RE::MagicTarget::ForEachActiveEffectVisitor
		{
		public:
			RE::BSContainer::ForEachResult Accept(RE::ActiveEffect* a_effect) override
			{
				const auto* base = a_effect && a_effect->flags.none(RE::ActiveEffect::Flag::kInactive) ? a_effect->GetBaseObject() : nullptr;
				if (base && gAbilityLights.contains(base->GetFormID())) {
					found.push_back(base->GetFormID());
				}
				return RE::BSContainer::ForEachResult::kContinue;
			}

			std::vector<RE::FormID> found;
		};

		// this creature wears an ability that carries a light: it is marked, and our rows of it go into the warning once per effect and race
		void MarkWorn(RE::Actor* a_actor, RE::FormID a_effect, const AbilityLight& a_light)
		{
			gWore[a_actor->GetFormID()][a_light.mod] = true;
			const auto race = static_cast<std::uint64_t>(a_actor->GetRace() ? a_actor->GetRace()->GetFormID() : 0);
			if (gNamed.insert((static_cast<std::uint64_t>(a_effect) << 32) | race).second) {
				for (const auto i : RowsOf(a_actor)) {
					AddReplaced(a_light.mod, gRows[i], a_light.file);
				}
			}
		}
	}

	void NoteWornLight(RE::Actor* a_actor, RE::FormID a_effect)
	{
		if (const auto light = a_actor ? gAbilityLights.find(a_effect) : gAbilityLights.end(); light != gAbilityLights.end()) {
			MarkWorn(a_actor, a_effect, light->second);
		}
	}

	void ReadWornLights(RE::Actor* a_actor)
	{
		auto* target = a_actor ? a_actor->AsMagicTarget() : nullptr;
		if (!target || gAbilityLights.empty()) {
			return;
		}
		// a dead creature's abilities end with it, and its light with them: its mark stays, so ours does not come on over its remains
		if (!a_actor->IsDead()) {
			gWore.erase(a_actor->GetFormID());
		}
		WornLights worn;
		target->VisitEffects(worn);
		for (const auto effect : worn.found) {
			NoteWornLight(a_actor, effect);
		}
	}

	void ReadCreatures()
	{
		gRows.clear();
		gByRace.clear();
		gByModel.clear();
		gRowSkins.clear();
		gBySkin.clear();
		gSkinRaces.clear();
		gClaims.clear();
		gClaimFolder.clear();
		gAbilityLights.clear();
		gRaceLights.clear();
		gRowGlobals.clear();
		gFound = {};
		gFiles = 0;
		ReadFiles();
		IndexRows();
		ReadClaims();
		FindReplaced();
		gSkinRaces.clear();
		FindAbilityLights();
		FindRowGlobals();
		for (std::size_t m = 0; m < Anima::kOthers; ++m) {
			if (!gReplaced[m].empty()) {
				SKSE::log::info("  {} lights {} of our creature(s) instead of us - ours steps aside there", Anima::kOtherMods[m].name, gReplaced[m].size());
			}
		}
	}

	std::vector<std::string> ReplacedBy(std::size_t a_mod)
	{
		std::scoped_lock l{ gOthersLock };
		return a_mod < Anima::kOthers ? gReplaced[a_mod] : std::vector<std::string>{};
	}

	const std::vector<Anima::Row>& Rows()
	{
		return gRows;
	}

	std::vector<std::size_t> RowsOf(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return {};
		}
		// a body's own rows first (Anima::RowsFor): the skin record a row names, then the models its skin wears - only a skin a row
		// can name is asked for either
		std::vector<std::size_t> rows;
		if (const auto* skin = SkinOf(a_actor); gRowSkins.contains(skin)) {
			if (const auto it = gBySkin.find(skin); it != gBySkin.end()) {
				rows = it->second;
			}
			for (const auto& model : BodyModels(a_actor)) {
				const auto it = gByModel.find(model);
				if (it == gByModel.end()) {
					continue;
				}
				for (const auto i : it->second) {
					if (std::ranges::find(rows, i) == rows.end()) {
						rows.push_back(i);
					}
				}
			}
		}
		const auto race = gByRace.find(a_actor->GetRace());
		return Anima::RowsFor(rows, race != gByRace.end() ? race->second : std::vector<std::size_t>{});
	}

	bool CouldLight(RE::Actor* a_actor)
	{
		return a_actor && (gByRace.contains(a_actor->GetRace()) || gRowSkins.contains(SkinOf(a_actor)));
	}

	bool RowModelsAside(std::size_t a_row, const Anima::Settings& a_s)
	{
		for (const auto& model : gRows[a_row].models) {
			if (const auto it = gClaims.find(model); it != gClaims.end() && Anima::Aside(it->second, a_s)) {
				return true;
			}
		}
		return false;
	}

	bool StepsAside(RE::Actor* a_actor, const Anima::Settings& a_s)
	{
		if (!a_actor) {
			return false;
		}
		if (!gClaims.empty()) {
			for (const auto& model : BodyModels(a_actor)) {
				if (const auto it = gClaims.find(model); it != gClaims.end() && Anima::Aside(it->second, a_s)) {
					return true;
				}
			}
			// a row may name a model the creature wears beyond its skin: a wisp's look is an art object its ability attaches by script
			for (const auto i : RowsOf(a_actor)) {
				if (RowModelsAside(i, a_s)) {
					return true;
				}
			}
		}
		if (const auto it = gRaceLights.find(a_actor->GetRace()); it != gRaceLights.end() && Anima::Aside(it->second, a_s)) {
			return true;
		}
		const auto worn = gWore.find(a_actor->GetFormID());
		return worn != gWore.end() && Anima::Aside(worn->second, a_s);
	}

	bool RowStepsAside(std::size_t a_row, const Anima::Settings& a_s)
	{
		const auto it = gRowGlobals.find(a_row);
		if (it == gRowGlobals.end()) {
			return false;
		}
		bool aside = false;
		for (auto& one : it->second) {
			if (one.global->value == 0.0f) {
				continue;
			}
			if (!one.told) {
				AddReplaced(one.mod, gRows[a_row], one.file);
				one.told = true;
			}
			aside = aside || !a_s.onTop[one.mod];
		}
		return aside;
	}

	RE::TESForm* FormOfKey(std::string_view a_key)
	{
		const auto tilde = a_key.find('~');
		auto*      dh = RE::TESDataHandler::GetSingleton();
		if (tilde == std::string_view::npos || !dh) {
			return nullptr;
		}
		auto hex = Anima::Trim(a_key.substr(0, tilde));
		if (hex.starts_with("0x") || hex.starts_with("0X")) {
			hex.remove_prefix(2);
		}
		RE::FormID id = 0;
		const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), id, 16);
		if (hex.empty() || ec != std::errc{} || end != hex.data() + hex.size()) {
			return nullptr;
		}
		return dh->LookupForm(id, std::string(Anima::Trim(a_key.substr(tilde + 1))));
	}

	bool IsAbilityLight(RE::FormID a_effect) { return gAbilityLights.contains(a_effect); }

	void ForgetWornLights() { gWore.clear(); }

	Anima::Lighters OthersFound()
	{
		std::scoped_lock l{ gOthersLock };
		return gFound;
	}

	std::size_t CreatureRowCount() { return gRows.size(); }
	std::size_t FilesRead() { return gFiles; }
}
