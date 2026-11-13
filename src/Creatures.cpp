// Illuminated - Creature Lighting NG - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The creatures files (Data\SKSE\Plugins\IlluminatedCreatureLightingNG\Creatures\*.txt, read in name order; every row that names a creature
// lights it), the races and body models they name, and what the other creature lighting mods light: their Light Placer files and
// their abilities that carry a light - all read once at data load. A creature is found by a model its body wears (the skin's armor
// addons), else by its race, so a body replacer that keeps the race keeps the light. An ability can reach a creature no record
// shows (a mod's script adds it): the game's events say so, and what a creature wears is read off it when it loads, never on a timer.

#include "Plugin.h"

#include "FadePickerAPI.h"

#include <bit>

namespace Plugin
{
	namespace
	{
		constexpr const char* kFolder = "Data/SKSE/Plugins/IlluminatedCreatureLightingNG/Creatures";

		std::vector<Anima::Row>                                                 gRows;
		std::vector<CreatureGroup>                                              gCreatureList;  // written once at data load, read after
		std::atomic<bool>                                                       gCreatureListReady{ false };
		std::unordered_map<RE::TESRace*, std::vector<std::size_t>>              gByRace;
		std::unordered_map<std::string, std::vector<std::size_t>>               gByModel;    // NormalPath -> rows
		std::unordered_set<const RE::TESObjectARMO*>                            gRowSkins;   // every skin a character wears that holds a model of gByModel, or a row names
		std::unordered_map<const RE::TESObjectARMO*, std::vector<std::size_t>>  gBySkin;     // a skin record a row names (skin=) -> rows
		std::unordered_map<const RE::TESObjectARMO*, std::vector<RE::TESRace*>> gSkinRaces;  // every skin -> the races that wear it
		// an ability whose constant effect plays a shader a row names (shader=, a ghost's look) -> those rows; written at data load only
		std::unordered_map<const RE::SpellItem*, std::vector<std::size_t>> gGhostSpells;
		std::unordered_map<std::string, Anima::Lighters>                   gClaims;       // a body model other mods' Light Placer files light -> every one of those mods
		std::unordered_map<std::string, std::string>                       gClaimFolder;  // the same model -> the Light Placer folder of a mod we do not name
		// a Light Placer file, told by a light's own name: the mod it belongs to and the models it names
		struct ClaimFile
		{
			std::size_t                     mod{ Anima::kOtherUnnamed };
			std::unordered_set<std::string> models;
		};
		std::unordered_map<std::string, ClaimFile> gClaimFiles;
		Anima::Lighters                            gFound{};
		std::size_t                                gFiles{ 0 };
		// gFound and the menu's lists: written on the main thread (a creature met in play adds to them), read by the menu's render thread
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

		// a path as Light Placer writes it into a light's name: in the local code page. A name the code page cannot hold would stop its own read
		[[nodiscard]] std::string AsLightPlacerNames(const std::filesystem::path& a_path)
		{
			try {
				return a_path.string();
			} catch (const std::exception&) {
				return Utf8(a_path);
			}
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
				// the menu groups creatures by the file they come from, its name up to " - " ("Skyrim - Eyes and Machines" joins "Skyrim")
				const auto stem = Utf8(path.stem());
				const auto group = stem.substr(0, stem.find(" - "));
				for (auto& row : rows) {
					row.group = group;
				}
				std::ranges::move(rows, std::back_inserter(gRows));
				++gFiles;
			}
			if (files.empty()) {
				SKSE::log::warn("no creatures file in {} - nothing is lit", kFolder);
			}
			// a row moved from Illuminated / RELight in the release they drop it is left to an older one still loaded: a player who
			// updates the mods apart keeps the look they had, never two lights on one thing
			if (OlderCarrierLoaded()) {
				const auto before = gRows.size();
				std::erase_if(gRows, [](const Anima::Row& a_row) { return a_row.moved; });
				SKSE::log::info("creatures files: {} moved row(s) left to the older Illuminated / RELight loaded", before - gRows.size());
			}
			// the menu's creature switches, one per creature a body row lights, in the files' order of groups, sorted inside each
			gCreatureListReady.store(false, std::memory_order_release);
			gCreatureList.clear();
			for (const auto& row : gRows) {
				if (!Anima::OnThePicker(row.kind)) {
					continue;
				}
				const bool listed = std::ranges::any_of(gCreatureList, [&](const auto& a_g) { return std::ranges::find(a_g.second, row.creature) != a_g.second.end(); });
				if (listed) {
					continue;
				}
				auto group = std::ranges::find(gCreatureList, row.group, &CreatureGroup::first);
				if (group == gCreatureList.end()) {
					group = gCreatureList.insert(gCreatureList.end(), { row.group, {} });
				}
				group->second.push_back(row.creature);
			}
			for (auto& [name, creatures] : gCreatureList) {
				std::ranges::sort(creatures);
			}
			gCreatureListReady.store(true, std::memory_order_release);
		}

		// a see-through creature (the game's ghosts, a spirit summon) wears an ability whose constant effect plays a ghost shader: every
		// such ability is found once, so an actor is matched by the abilities of its own spell list (his 573)
		template <class OnBody>
		void IndexGhosts(OnBody a_onBody)
		{
			std::unordered_map<const RE::TESEffectShader*, std::vector<std::size_t>> byShader;
			for (std::size_t i = 0; i < gRows.size(); ++i) {
				if (!a_onBody(gRows[i])) {
					continue;
				}
				for (const auto& key : gRows[i].shaders) {
					auto* form = FormOfKey(key);
					if (const auto* shader = form ? form->As<RE::TESEffectShader>() : nullptr) {
						byShader[shader].push_back(i);
					}
				}
			}
			if (byShader.empty()) {
				return;
			}
			std::unordered_map<const RE::EffectSetting*, const std::vector<std::size_t>*> byEffect;
			for (const auto* effect : LightKit::FormsOf<RE::EffectSetting>()) {
				if (effect && effect->data.castingType == RE::MagicSystem::CastingType::kConstantEffect) {
					if (const auto it = byShader.find(effect->data.effectShader); it != byShader.end()) {
						byEffect.emplace(effect, &it->second);
					}
				}
			}
			for (const auto* spell : LightKit::FormsOf<RE::SpellItem>()) {
				if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kAbility) {
					continue;
				}
				std::vector<std::size_t> rows;
				for (const auto* effect : spell->effects) {
					const auto it = effect ? byEffect.find(effect->baseEffect) : byEffect.end();
					if (it == byEffect.end()) {
						continue;
					}
					for (const auto i : *it->second) {
						if (std::ranges::find(rows, i) == rows.end()) {
							rows.push_back(i);
						}
					}
				}
				if (!rows.empty()) {
					gGhostSpells.emplace(spell, std::move(rows));
				}
			}
			SKSE::log::info("creatures: {} ghost shader(s) named, {} effect(s) and {} abilit(ies) that play one", byShader.size(), byEffect.size(), gGhostSpells.size());
		}

		// each ghost ability in an actor's own spell list (its base's, the copy the game made of a template's included) -> its rows; a_visit
		// returns false to stop. None for most characters, after a short look at their few abilities
		template <class Visit>
		void ForEachGhostSpell(RE::Actor* a_actor, Visit a_visit)
		{
			if (gGhostSpells.empty()) {
				return;
			}
			auto*       npc = a_actor->GetActorBase();
			const auto* list = npc ? npc->actorEffects : nullptr;
			if (!list || !list->spells) {
				return;
			}
			for (std::uint32_t s = 0; s < list->numSpells; ++s) {
				if (const auto it = gGhostSpells.find(list->spells[s]); it != gGhostSpells.end() && !a_visit(it->second)) {
					return;
				}
			}
		}

		[[nodiscard]] bool HasGhostSpell(RE::Actor* a_actor)
		{
			bool found = false;
			ForEachGhostSpell(a_actor, [&](const std::vector<std::size_t>&) {
				found = true;
				return false;
			});
			return found;
		}

		// the rows of every ghost ability it wears, each once: two abilities (an eyes shader and a glow shader) both count
		[[nodiscard]] std::vector<std::size_t> GhostRowsOf(RE::Actor* a_actor)
		{
			std::vector<std::size_t> out;
			ForEachGhostSpell(a_actor, [&](const std::vector<std::size_t>& a_rows) {
				for (const auto i : a_rows) {
					if (std::ranges::find(out, i) == out.end()) {
						out.push_back(i);
					}
				}
				return true;
			});
			return out;
		}

		void IndexRows()
		{
			// the spell and burst rows light game records (SpellLights.cpp), never a creature's body
			const auto onBody = [](const Anima::Row& a_r) { return a_r.kind == Anima::Kind::kGlow || a_r.kind == Anima::Kind::kEyes || a_r.kind == Anima::Kind::kMachines; };
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
				// only a real race is a wearer: a null one would make AddonsWorn count every addon of the skin
				auto& races = gSkinRaces[a_skin];
				if (a_race) {
					if (std::ranges::find(races, a_race) != races.end()) {
						return;
					}
					races.push_back(a_race);
				}
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
			IndexGhosts(onBody);
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
				// exactly as Light Placer takes a file: a ".JSON" it never reads lights nothing
				if (!it->is_regular_file(one) || it->path().extension() != ".json") {
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
				auto stem = rel;
				stem.replace_extension();
				auto& claimFile = gClaimFiles[Anima::LightPlacerKey(AsLightPlacerNames(stem))];
				claimFile.mod = *mod;
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
						claimFile.models.insert(path);
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
		std::array<std::vector<std::string>, Anima::kOthers> gAlsoLit;  // per other mod: the names of our creatures its config lights

		// which of our creatures each other mod's config lights too: a row's own body models, and the models of every skin its races
		// wear - a race's own and its NPCs' (for the menu's list)
		void FindAlsoLit()
		{
			gAlsoLit = {};
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
						auto& names = gAlsoLit[mod];
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

		// one of our rows goes into the menu's list under the mod that lights it (a mod we do not name is told by its plugin file)
		void AddAlsoLit(std::size_t a_mod, const Anima::Row& a_row, std::string_view a_file)
		{
			auto             name = a_mod == Anima::kOtherUnnamed ? std::format("{} ({})", a_row.name, a_file) : a_row.name;
			std::scoped_lock l{ gOthersLock };
			gFound[a_mod] = true;
			if (auto& names = gAlsoLit[a_mod]; std::ranges::find(names, name) == names.end()) {
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
							AddAlsoLit(*claim, gRows[i], files.empty() ? std::string_view{} : std::string_view(files.front()));
						}
					}
				}
			}
			SKSE::log::info("other creature lighting mods: {} abilit(ies) carry a light, {} race(s) lit by one", gAbilityLights.size(), gRaceLights.size());
		}

		// JS Emissive Eyes loaded: it takes over the eyes, whatever its own menu says for a creature. Written at data load, read from any thread after
		bool gEyesMod{ false };

		void FindEyesMod()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			gEyesMod = dh && std::ranges::any_of(std::array{ "JS Emissive Eyes SE - Johnskyrim.esp", "JS Emissive Eyes SE - Johnskyrim.esl" }, [&](const char* a_file) {
				return dh->LookupLoadedModByName(a_file) || dh->LookupLoadedLightModByName(a_file);
			});
			if (gEyesMod) {
				SKSE::log::info("JS Emissive Eyes is loaded: it handles the eyes, ours stay off");
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

		// a Light-archetype effect holds its light, and the game adds that light back to the scene at every update, so it is only ever hidden.
		// Collected after the game's own walk of them, as WornLights is
		class WornLightEffects final : public RE::MagicTarget::ForEachActiveEffectVisitor
		{
		public:
			RE::BSContainer::ForEachResult Accept(RE::ActiveEffect* a_effect) override
			{
				auto*       effect = a_effect ? skyrim_cast<RE::LightEffect*>(a_effect) : nullptr;
				const auto* base = effect && effect->light ? effect->GetBaseObject() : nullptr;
				if (const auto it = base ? gAbilityLights.find(base->GetFormID()) : gAbilityLights.end(); it != gAbilityLights.end()) {
					found.emplace_back(effect->light, it->second.mod);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			}

			std::vector<std::pair<RE::NiPointer<RE::NiPointLight>, std::size_t>> found;
		};

		// the ability lights we hid: only those are shown again, so a light the game or another mod hid stays hidden. Held by reference,
		// so a freed light never leaves its address to another one; let go once we are their last holder
		std::vector<RE::NiPointer<RE::NiPointLight>> gHidAbility;  // main thread only

		// another mod's Light Placer light out, or back as it was: Light Placer's own word for it, so its conditions leave it alone meanwhile
		void SetPlacerLight(RE::NiPointLight* a_light, bool a_out)
		{
			auto&      word = a_light->GetLightRuntimeData().ambient.red;
			const auto was = std::bit_cast<std::uint32_t>(word);
			const auto now = Anima::CullWord(was, a_out);
			word = std::bit_cast<float>(now);
			// always, as Light Placer's own cull does: when it re-dresses a light it rewrites the word and leaves this flag as it was
			a_light->SetAppCulled((now & Anima::kCullMask) != 0);
		}

		// this creature wears an ability that carries a light: it is marked, and our rows of it go into the menu's list once per effect and race
		void MarkWorn(RE::Actor* a_actor, RE::FormID a_effect, const AbilityLight& a_light)
		{
			gWore[a_actor->GetFormID()][a_light.mod] = true;
			const auto race = static_cast<std::uint64_t>(a_actor->GetRace() ? a_actor->GetRace()->GetFormID() : 0);
			if (gNamed.insert((static_cast<std::uint64_t>(a_effect) << 32) | race).second) {
				for (const auto i : RowsOf(a_actor)) {
					AddAlsoLit(a_light.mod, gRows[i], a_light.file);
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
		gGhostSpells.clear();
		gClaims.clear();
		gClaimFolder.clear();
		gClaimFiles.clear();
		gAbilityLights.clear();
		gRaceLights.clear();
		gFound = {};
		gFiles = 0;
		ReadFiles();
		IndexRows();
		ReadClaims();
		FindAlsoLit();
		gSkinRaces.clear();
		FindAbilityLights();
		FindEyesMod();
		for (std::size_t m = 0; m < Anima::kOthers; ++m) {
			if (!gAlsoLit[m].empty()) {
				SKSE::log::info("  {} lights {} of our creature(s) too - ours replaces it there unless its switch gives them back", Anima::kOtherMods[m].name, gAlsoLit[m].size());
			}
		}
	}

	std::vector<std::string> AlsoLitBy(std::size_t a_mod)
	{
		std::scoped_lock l{ gOthersLock };
		return a_mod < Anima::kOthers ? gAlsoLit[a_mod] : std::vector<std::string>{};
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
		auto       out = Anima::RowsFor(rows, race != gByRace.end() ? race->second : std::vector<std::size_t>{});
		if (const auto ghost = GhostRowsOf(a_actor); !ghost.empty()) {
			out = Anima::WithGhostRows(std::move(out), ghost, [](std::size_t i) { return gRows[i].kind == Anima::Kind::kGlow; });
		}
		return out;
	}

	bool CouldLight(RE::Actor* a_actor)
	{
		return a_actor && (gByRace.contains(a_actor->GetRace()) || gRowSkins.contains(SkinOf(a_actor)) || HasGhostSpell(a_actor));
	}

	bool RowModelsLitByOthers(std::size_t a_row)
	{
		return std::ranges::any_of(gRows[a_row].models, [](const std::string& a_model) { return gClaims.contains(a_model); });
	}

	bool GivenToThem(RE::Actor* a_actor, const Anima::Settings& a_s)
	{
		if (!a_actor) {
			return false;
		}
		const auto claimed = [&](const std::string& a_model) {
			const auto it = gClaims.find(a_model);
			return it != gClaims.end() && Anima::GivenBack(it->second, a_s);
		};
		if (!gClaims.empty()) {
			if (std::ranges::any_of(BodyModels(a_actor), claimed)) {
				return true;
			}
			// a row may name a model the creature wears beyond its skin: a wisp's look is an art object its ability attaches by script
			for (const auto i : RowsOf(a_actor)) {
				if (std::ranges::any_of(gRows[i].models, claimed)) {
					return true;
				}
			}
		}
		if (const auto it = gRaceLights.find(a_actor->GetRace()); it != gRaceLights.end() && Anima::GivenBack(it->second, a_s)) {
			return true;
		}
		const auto worn = gWore.find(a_actor->GetFormID());
		return worn != gWore.end() && Anima::GivenBack(worn->second, a_s);
	}

	namespace
	{
		// Illuminated's or RELight's Choose Your Own Color page, when one is installed: main thread after data load
		const FadePicker::API*   gPicker{ nullptr };
		const char*              gPickerOwner{ nullptr };
		std::vector<std::string> gPickerKeys;  // by row; empty for a row not on the page

		struct Picked
		{
			bool                 off{ false };
			bool                 colored{ false };
			std::array<float, 3> rgb{};
		};

		[[nodiscard]] std::optional<Picked> PickedFor(std::size_t a_row)
		{
			if (!gPicker || a_row >= gPickerKeys.size() || gPickerKeys[a_row].empty()) {
				return std::nullopt;
			}
			Picked p;
			return gPicker->choiceFor(gPickerKeys[a_row].c_str(), p.rgb.data(), &p.off, &p.colored) ? std::optional<Picked>(p) : std::nullopt;
		}

		void PickerChanged()
		{
			static_cast<void>(Later("color picked", []() { ApplyNow(); }));
		}
	}

	void OfferToPicker()
	{
		constexpr std::array<std::pair<const char*, const char*>, 2> providers{ { { "Illuminated.dll", "Illuminated" }, { "RELightSpellLightingNG.dll", "RELight" } } };
		for (const auto& [dll, name] : providers) {
			const auto handle = REX::W32::GetModuleHandleA(dll);
			const auto request = handle ? reinterpret_cast<FadePicker::Request>(REX::W32::GetProcAddress(handle, FadePicker::kRequest)) : nullptr;
			gPicker = request ? request(FadePicker::kVersion) : nullptr;
			if (gPicker) {
				gPickerOwner = name;
				break;
			}
		}
		if (!gPicker) {
			SKSE::log::info("no Choose Your Own Color page to join: neither Illuminated nor RELight offers one");
			return;
		}
		gPickerKeys.assign(gRows.size(), {});
		std::vector<FadePicker::Item> items;
		for (std::size_t i = 0; i < gRows.size(); ++i) {
			if (Anima::OnThePicker(gRows[i].kind) && !gRows[i].name.empty()) {
				gPickerKeys[i] = Anima::PickerKey(gRows[i].name);
				items.push_back({ gPickerKeys[i].c_str(), gRows[i].name.c_str() });
			}
		}
		gPicker->addItems(items.data(), static_cast<std::uint32_t>(items.size()), "Creatures");
		gPicker->listen(PickerChanged);
		SKSE::log::info("{} creature(s) offered to {}'s Choose Your Own Color page", items.size(), gPickerOwner);
	}

	const char* PickerOwner() { return gPickerOwner; }

	std::optional<std::array<float, 3>> PickedColor(std::size_t a_row)
	{
		const auto p = PickedFor(a_row);
		return p && p->colored ? std::optional<std::array<float, 3>>(p->rgb) : std::nullopt;
	}

	bool RowShows(std::size_t a_row, const Anima::Settings& a_s)
	{
		if (!Anima::Shown(gRows[a_row], a_s) || Anima::TakenByEyesMod(gRows[a_row].kind, gEyesMod)) {
			return false;
		}
		const auto p = PickedFor(a_row);
		return !p || !p->off;
	}

	bool EyesModLoaded() { return gEyesMod; }

	void TheirLights(RE::Actor* a_actor, RE::NiAVObject* a_root, const Anima::Settings& a_s, bool a_oursHung)
	{
		const auto rows = RowsOf(a_actor);
		if (rows.empty()) {
			return;
		}
		// theirs goes out only where ours shows: an ice creature with that switch off, one past the cap and one whose lights failed keep
		// the other mod's light
		const bool ours = a_oursHung && std::ranges::any_of(rows, [&](std::size_t a_i) { return RowShows(a_i, a_s); });
		if (a_root && !gClaimFiles.empty()) {
			auto models = BodyModels(a_actor);
			for (const auto i : rows) {
				models.insert(models.end(), gRows[i].models.begin(), gRows[i].models.end());
			}
			// a light is another mod's only when its file names a model this creature wears, and every model a file names is in gClaims
			if (std::ranges::any_of(models, [](const std::string& a_model) { return gClaims.contains(a_model); })) {
				RE::BSVisit::TraverseScenegraphLights(a_root, [&](RE::NiPointLight* a_light) {
					const auto file = Anima::LightPlacerFile(a_light->name.c_str());
					const auto it = file ? gClaimFiles.find(*file) : gClaimFiles.end();
					if (it != gClaimFiles.end() && std::ranges::any_of(models, [&](const std::string& a_model) { return it->second.models.contains(a_model); })) {
						SetPlacerLight(a_light, Anima::TheirLightOut(ours, a_s.theirs[it->second.mod]));
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
		}
		if (auto* target = gAbilityLights.empty() ? nullptr : a_actor->AsMagicTarget()) {
			// a light whose effect has ended is held by us alone: let go before the walk
			std::erase_if(gHidAbility, [](const RE::NiPointer<RE::NiPointLight>& a_held) { return !a_held || a_held->GetRefCount() <= 1; });
			WornLightEffects worn;
			target->VisitEffects(worn);
			for (const auto& [light, mod] : worn.found) {
				const auto held = std::ranges::find_if(gHidAbility, [&](const RE::NiPointer<RE::NiPointLight>& a_held) { return a_held.get() == light.get(); });
				if (Anima::TheirLightOut(ours, a_s.theirs[mod])) {
					if (held != gHidAbility.end() || !light->GetAppCulled()) {
						if (held == gHidAbility.end()) {
							gHidAbility.push_back(light);
						}
						light->SetAppCulled(true);
					}
				} else if (held != gHidAbility.end()) {
					gHidAbility.erase(held);
					light->SetAppCulled(false);
				}
			}
		}
	}

	bool OthersLightBodies() { return !gClaims.empty(); }

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

	void ForgetWornLights()
	{
		gWore.clear();
		gHidAbility.clear();
	}

	Anima::Lighters OthersFound()
	{
		std::scoped_lock l{ gOthersLock };
		return gFound;
	}

	std::size_t CreatureRowCount() { return gRows.size(); }
	std::size_t FilesRead() { return gFiles; }

	const std::vector<CreatureGroup>& CreatureList()
	{
		// devbench may ask from its own thread while data load still builds the list: empty until it is whole, fixed after
		static const std::vector<CreatureGroup> none;
		return gCreatureListReady.load(std::memory_order_acquire) ? gCreatureList : none;
	}
}
