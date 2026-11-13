// StreamRows - see StreamRows.h
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// Moved unchanged out of Illuminated's StreamLights.cpp (its row half): every loader-thread 3D is queued for the main thread, every light
// is made, moved and taken off there, and nothing runs while no row, cloud or queued projectile is left (Awake).

#include "StreamRows.h"

#include "BeamRow.h"
#include "LightKit.h"

#include <chrono>
#include <format>

namespace StreamRows
{
	namespace
	{
		using LightKit::kK;
		constexpr float kLightSize = LightKit::kLightSize;

		Host                                   gHost{};
		std::unordered_map<RE::FormID, Recipe> gRows;                // projectile base -> its row (main thread after Claim)
		constexpr std::size_t                  kMostRowLights = 64;  // every row light in the scene together
		constexpr std::size_t                  kMostPerRow = 20;
		// off Community Shaders the game lets one piece of ground take only a few lights, so a row of 15 lit only the ground near her
		// hands; fewer, wider lights reach the whole stream (BeamRow::Lay makes each stronger to keep the row's light)
		constexpr std::size_t kMostPerRowPlain = 5;
		constexpr float       kRowPlain = 0.5f;     // every row off Community Shaders
		constexpr float       kRowHeight = 100.0f;  // a beam's usual height over the ground it lights
		constexpr int         kShaders = 0, kEnb = 1, kVanilla = 2;

		bool  InverseSquare() { return gHost.inverseSquare && gHost.inverseSquare(); }
		int   LightingNow() { return gHost.lighting ? gHost.lighting() : kVanilla; }
		float BrightnessNow() { return gHost.brightness ? gHost.brightness() : 1.0f; }
		float ReachNow() { return gHost.reach ? gHost.reach() : 1.0f; }
		bool  RowOn(const Recipe& a_rec) { return gHost.rowOn && gHost.rowOn(a_rec); }
		bool  CloudOn(RE::FormID a_base) { return gHost.cloudOn && gHost.cloudOn(a_base); }

		// the same row lifted the ground 3-33 per band on Community Shaders, 9-24 on Vanilla and 33-72 on ENB (2026-10-20 18:29-18:43):
		// Vanilla brighter, ENB softer, to align with Community Shaders
		[[nodiscard]] float RowStrengthFor(int a_lighting)
		{
			switch (a_lighting) {
			case kVanilla:
				return 1.1f;
			case kEnb:
				return 0.5f;  // the whole stream lit, not washed out
			default:
				return 1.0f;
			}
		}
		// Community Shaders draws a row light at 4x its fade, so twice the reach laid; drawn plain a row reaches only that
		[[nodiscard]] float RowReachFor(int a_lighting) { return a_lighting == kShaders ? 1.0f : 2.0f; }

		std::string Label(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "(none)";
			}
			const char* id = a_form->GetFormEditorID();
			return std::format("{:08X} {}", a_form->GetFormID(), id && *id ? id : "");
		}

		bool OnMainThread()
		{
			const auto* main = RE::Main::GetSingleton();
			return main && REX::W32::GetCurrentThreadId() == main->threadID;
		}

		RE::NiColor ColorOf(const RE::TESObjectLIGH* a_light)
		{
			if (!a_light) {
				return { 1.0f, 1.0f, 1.0f };
			}
			return { a_light->data.color.red / 255.0f, a_light->data.color.green / 255.0f, a_light->data.color.blue / 255.0f };
		}

		std::size_t gRowLights = 0;  // row lights in the scene now (under gLiveLock)

		// One lit projectile, keyed by its 3D ROOT, not its reference ID: a projectile's reference is temporary and the game hands its ID
		// to a new one. The root is held here, so its address is never another 3D's while it is listed.
		struct Live
		{
			RE::NiPointer<RE::NiAVObject>           root;
			RE::FormID                              base{ 0 };
			std::vector<RE::NiPointer<RE::BSLight>> lights;
			RE::NiPointer<RE::TESObjectREFR>        beam;  // its projectile, read every frame for its length
		};
		std::unordered_map<const RE::NiAVObject*, Live> gLive;
		std::mutex                                      gLiveLock;
		// a projectile dropped under gLiveLock is let go only after it: its last reference running the game's release (our hooked
		// Release3DRelatedData) inside the lock would take it again on the same thread
		std::vector<RE::NiPointer<RE::TESObjectREFR>> gLetGo;
		// what a loader thread built or released, waiting for the main thread (under gLiveLock): held by refcount, never a handle made
		// off the main thread, and a queued 3D's address is never another's when its turn comes
		std::vector<RE::NiPointer<RE::TESObjectREFR>> gToHang;
		std::vector<RE::NiPointer<RE::NiAVObject>>    gToDrop;
		std::uint32_t                                 gSweepClock = 0;  // frames since the last sweep (main thread)
		// a projectile that flies while its row is switched off or the light cap is full: its row is hung when that passes (main thread)
		std::vector<RE::NiPointer<RE::TESObjectREFR>> gParkedRows;
		constexpr std::size_t                         kMostParked = 64;

		void ParkRow(RE::TESObjectREFR* a_ref)
		{
			std::erase_if(gParkedRows, [](const auto& a_parked) {
				const auto* root = a_parked ? a_parked->Get3D() : nullptr;
				return !root || !root->parent;
			});
			if (a_ref && gParkedRows.size() < kMostParked) {
				gParkedRows.emplace_back(a_ref);
			}
		}

		// a cone's cloud: its art leaves world-space particles along the path, so its one light hangs in the node the cone hangs in -
		// which stays when the cone's 3D goes - and is moved there every frame (main thread)
		struct Cloud
		{
			RE::NiPointer<RE::NiAVObject>    root;  // the cone's 3D, while it flies
			RE::NiPointer<RE::TESObjectREFR> ref;
			RE::NiPointer<RE::NiNode>        holder;  // the node the cone hung in, which holds the light
			RE::NiPointer<RE::BSLight>       light;
			RE::FormID                       base{ 0 };
			RE::FormID                       spell{ 0 };  // its chosen color, read after the cone is gone too
			RE::NiPoint3                     launch;
			RE::NiPoint3                     spot;
			float                            sinceEnd{ -1.0f };  // seconds since the cone ended; below 0 while it flies
		};
		std::vector<Cloud>    gClouds;
		constexpr std::size_t kMostClouds = 8;
		// a cone whose cloud light could not hang yet (its 3D not in the world when it loads): tried every frame for a while
		struct CloudWait
		{
			RE::NiPointer<RE::TESObjectREFR> ref;
			RE::FormID                       base{ 0 };
			std::uint32_t                    frames{ 0 };
		};
		std::vector<CloudWait>  gCloudWait;
		constexpr std::uint32_t kCloudTries = 600;          // about ten seconds of frames: a cone's cast and its cloud's life
		constexpr float         kCloudReachShaders = 3.0f;  // a row's x1 left Ice Storm's cloud faint on Community Shaders

		std::atomic<bool> gAwake{ true };

		// ------------------------------------------------------------------ forgetting one live projectile
		// gLiveLock is held by every caller; the main thread's (the scene node is touched)
		void Forget(const RE::NiAVObject* a_root, RE::ShadowSceneNode* a_scene)
		{
			const auto it = gLive.find(a_root);
			if (it == gLive.end()) {
				return;
			}
			for (auto& bs : it->second.lights) {
				LightKit::TakeOff(a_scene, bs);
			}
			gRowLights -= (std::min)(gRowLights, it->second.lights.size());
			// a row switched off while its projectile lives leaves no light node behind on it - a row hung again later starts clean
			for (auto& bs : it->second.lights) {
				if (auto* light = bs ? bs->light.get() : nullptr; light && light->parent) {
					light->parent->DetachChild(light);
				}
			}
			if (it->second.beam) {
				gLetGo.push_back(std::move(it->second.beam));
			}
			gLive.erase(it);  // the last reference to the projectile's 3D may go here, on the main thread
		}

		// every live 3D a test picks, let go: all collected first, as Forget erases from gLive. gLiveLock is held by the caller.
		template <class Pick>
		std::size_t ForgetIf(RE::ShadowSceneNode* a_scene, Pick a_pick)
		{
			std::vector<const RE::NiAVObject*> picked;
			for (const auto& [root, live] : gLive) {
				if (a_pick(live)) {
					picked.push_back(root);
				}
			}
			for (const auto* root : picked) {
				Forget(root, a_scene);
			}
			return picked.size();
		}

		// the sweep: a projectile 3D the game has taken out of the world (no parent) is not live. gLiveLock is held by the caller.
		std::size_t SweepGone(RE::ShadowSceneNode* a_scene)
		{
			return ForgetIf(a_scene, [](const Live& a_live) { return !a_live.root || !a_live.root->parent; });
		}

		// Community Shaders draws an inverse-square point light at four times its fade and writes back the reach that gives (2026-10-20
		// 18:15: every row light read twice the reach laid, 389 for 197) - so the cutoff is set for that
		constexpr float kDrawnTimes = 4.0f;

		float IslRoom(float a_radius) { return a_radius * a_radius + kK * kLightSize * kLightSize * 0.5f; }

		// the strongest fade a row light of this reach can take on Community Shaders before its cutoff passes its top
		float IslTop(float a_radius) { return LightKit::Isl::kHighestCutoff * IslRoom(a_radius) / (kDrawnTimes * kK); }

		// a light's strength, reach and color written whole - when it is made and again every frame a row moves
		void Dress(RE::NiPointLight* a_light, const RE::NiColor& a_color, float a_fade, float a_radius, bool a_isl)
		{
			const auto turn = LightKit::Turn();  // the light's fields have one writer at a time (LightKit.h); taken after gLiveLock
			auto&      data = a_light->GetLightRuntimeData();
			data.diffuse = a_color;
			data.fade = a_fade;
			data.radius = { a_radius, a_radius, kLightSize };
			a_light->SetLightAttenuation(a_radius);
			if (a_isl) {
				data.fade = (std::min)(a_fade, IslTop(a_radius));  // past its top the reach would grow instead
				LightKit::Isl::SetOwn(a_light);
				LightKit::Isl::SetCutoff(a_light, kDrawnTimes * kK * data.fade / IslRoom(a_radius));
			} else if (gHost.lightLimitFix && gHost.lightLimitFix()) {
				LightKit::Isl::SetOwnLinear(a_light);
			} else {
				data.ambient = LightKit::Tenth(a_color);
			}
		}

		// where a row starts and which way it runs: a bolt along its model's +Y from its root; a flame spray or cone along the
		// projectile's own aim from where it stands (a spray's model points another way: its forward is -X, sideways in third person)
		struct Axis
		{
			RE::NiPoint3 start;
			RE::NiPoint3 dir{ 0.0f, 1.0f, 0.0f };
		};

		Axis AxisOf(const RE::TESObjectREFR* a_ref, const RE::NiAVObject* a_root, bool a_beam)
		{
			if (a_beam) {
				return { a_root->world.translate, a_root->world.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f } };
			}
			const float pitch = a_ref->GetAngleX(), yaw = a_ref->GetAngleZ();
			return { a_ref->GetPosition(), { std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), -std::sin(pitch) } };
		}

		// how far along the row's axis the drawn art reaches (its geometry's bounds, our row lights not counted); 0 when nothing is
		// drawn. Main thread.
		float DrawnLength(RE::NiAVObject* a_root, const Axis& a_axis)
		{
			float reach = 0.0f;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				const auto& bound = a_geometry->worldBound;
				if (bound.radius > 0.0f) {
					reach = (std::max)(reach, (bound.center - a_axis.start).Dot(a_axis.dir) + bound.radius);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return std::isfinite(reach) ? reach : 0.0f;
		}

		// where a row runs right now and how long it is: a bolt that hit something runs straight from its root to that impact, one in
		// the open along its model as far as its art is drawn; a flame spray or cone along its aim, never past its newest impact. Never
		// past the range. Main thread.
		struct Path
		{
			Axis  axis;
			float length{ 0.0f };
		};

		Path PathOf(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_root, bool a_beam)
		{
			Path  path{ AxisOf(a_ref, a_root, a_beam) };
			auto* proj = a_ref ? a_ref->As<RE::Projectile>() : nullptr;
			if (!proj) {
				return path;
			}
			auto&       rt = proj->GetProjectileRuntimeData();
			const auto* base = proj->GetProjectileBase();
			// a cone flies: what is left of its range runs from where it is now
			path.length = base && base->IsCone() ? (std::max)(rt.range - rt.distanceMoved, 0.0f) : rt.range;
			// only a bolt stops where its art is drawn: a spray's geometry reads far shorter than its flames (Flames 230 of 768)
			if (a_beam) {
				if (const float drawn = DrawnLength(a_root, path.axis); drawn > 1.0f && drawn < path.length) {
					path.length = drawn;
				}
			}
			for (const auto* impact : rt.impacts) {  // the newest impact comes first
				if (impact) {
					const auto  hit = impact->desiredTargetLoc - path.axis.start;
					const float d = a_beam ? hit.Length() : hit.Dot(path.axis.dir);  // a spray along its aim: a passed impact is behind it
					if (d > 1.0f && d < rt.range && (a_beam || d < path.length)) {
						path.length = d;
						if (a_beam) {
							path.axis.dir = hit / d;
						}
					}
					break;
				}
			}
			if (!std::isfinite(path.length)) {
				path.length = 0.0f;
			}
			return path;
		}

		// a concentration stream whose caster no longer casts it (a weapon equipped mid-cast leaves the spray behind): its row rests
		// dark. A fire-and-forget bolt outlives its cast and is never cut. Main thread.
		[[nodiscard]] bool CasterStopped(RE::TESObjectREFR* a_ref)
		{
			auto* proj = a_ref ? a_ref->As<RE::Projectile>() : nullptr;
			if (!proj) {
				return false;
			}
			auto& rt = proj->GetProjectileRuntimeData();
			if (!rt.spell || rt.spell->GetCastingType() != RE::MagicSystem::CastingType::kConcentration) {
				return false;
			}
			// only a hand's cast is judged: a scripted cast goes through the instant caster, which never holds the spell
			if (rt.castingSource != RE::MagicSystem::CastingSource::kLeftHand && rt.castingSource != RE::MagicSystem::CastingSource::kRightHand) {
				return false;
			}
			const auto shooter = rt.shooter.get();
			auto*      actor = shooter ? shooter->As<RE::Actor>() : nullptr;
			if (!actor) {
				return false;
			}
			auto* caster = actor->GetMagicCaster(rt.castingSource);
			return !caster || caster->state.get() == RE::MagicCaster::State::kNone || caster->currentSpell != rt.spell;
		}

		std::optional<RE::NiColor> SpellColor(RE::FormID a_spell)
		{
			return a_spell && gHost.spellColor ? gHost.spellColor(a_spell) : std::nullopt;
		}

		// every row spread over its length, strength and reach laid again for that length (BeamRow::Lay), once a frame. Main thread,
		// gLiveLock held by the caller.
		void MoveRows()
		{
			const bool  isl = InverseSquare();
			const float rowBright = BrightnessNow(), bright = rowBright * RowStrengthFor(LightingNow()), reachPct = ReachNow();
			// the row flickers by itself while the host's Flicker is on - in its spell's shape when the host asks for it
			static const auto start = std::chrono::steady_clock::now();
			const double      seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			const bool        flickers = gHost.flicker && gHost.flicker();
			const bool        shaped = gHost.shaped && gHost.shaped();
			RE::NiUpdateData  update{};
			for (auto& [root, live] : gLive) {
				if (!live.beam || !live.root || live.lights.empty()) {
					continue;
				}
				const auto rec = gRows.find(live.base);
				if (rec == gRows.end()) {
					continue;
				}
				const bool  beam = rec->second.beam;
				const auto  path = PathOf(live.beam.get(), live.root.get(), beam);
				const auto& axis = path.axis;
				const float length = path.length;
				if (length <= 1.0f || CasterStopped(live.beam.get())) {  // nothing drawn, or no one casting it: the row rests dark
					for (auto& bs : live.lights) {
						if (auto* light = bs ? netimmerse_cast<RE::NiPointLight*>(bs->light.get()) : nullptr) {
							Dress(light, rec->second.color, 0.0f, 1.0f, isl);
						}
					}
					continue;
				}
				RE::NiColor color = rec->second.colorFrom ? ColorOf(rec->second.colorFrom) : rec->second.color;
				auto*       fired = live.beam ? live.beam->As<RE::Projectile>() : nullptr;
				auto*       spell = fired ? fired->GetProjectileRuntimeData().spell : nullptr;
				if (const auto own = SpellColor(spell ? spell->GetFormID() : 0)) {
					color = *own;  // the spell's chosen color reaches its row too
				}
				const auto                        kind = flickers && shaped ? LightKit::PulseKindOfSpell(spell) : Pulse::Kind::kNone;
				const auto                        seed = static_cast<std::uint32_t>(std::hash<const void*>{}(live.root.get()));
				const auto                        toLocal = live.root->world.Invert();
				static std::vector<BeamRow::Spot> row;  // main thread; its storage kept from row to row and frame to frame
				BeamRow::Lay(row, length, static_cast<int>(live.lights.size()), rec->second.reach, kRowHeight);
				for (std::size_t i = 0; i < live.lights.size(); ++i) {
					auto* bs = live.lights[i].get();
					auto* light = bs ? netimmerse_cast<RE::NiPointLight*>(bs->light.get()) : nullptr;
					if (!light) {
						continue;
					}
					if (i >= row.size()) {  // a beam cut so short it needs fewer: the spare lights rest dark at the hand
						Dress(light, color, 0.0f, 1.0f, isl);
						light->local.translate = {};
						continue;
					}
					const auto& s = row[i];
					// the radius is the scene's own units - the root's scale moves a child light's place, never its radius
					const float reach = s.reach * reachPct * RowReachFor(LightingNow());
					// at full strength five lights wash a stream out white off Community Shaders
					const float want = rec->second.fade * s.strength * bright * (isl ? 1.0f : kRowPlain);
					// a dimmer Brightness lowers the cap too, so the sliders still reach a capped row
					const float steady = BeamRow::Steady(want, isl ? IslTop(reach) * (std::min)(rowBright, 1.0f) : 0.0f);
					Dress(light, color, steady * (flickers ? BeamRow::Pulsing(kind, seconds, i, seed) : 1.0f), reach, isl);
					light->local.translate = toLocal * (axis.start + axis.dir * (s.along * length));  // on the path, in the root's units
					light->Update(update);
				}
			}
		}

		void DropCloud(Cloud& a_cloud, RE::ShadowSceneNode* a_scene)
		{
			if (!a_cloud.light) {
				return;
			}
			LightKit::TakeOff(a_scene, a_cloud.light);
			if (auto* light = a_cloud.light->light.get(); light && light->parent) {
				light->parent->DetachChild(light);
			}
		}

		// one light where a cone's cloud is, hung beside the cone and dark until MoveClouds places it; false: not now, try again (its
		// 3D not in the world yet - a cone has no parent node when its 3D loads). Main thread.
		[[nodiscard]] bool HangCloud(RE::TESObjectREFR* a_ref, RE::NiNode* a_root, RE::FormID a_base)
		{
			auto*      scene = LightKit::Scene();
			auto*      holder = a_root->parent;
			const bool listed = std::ranges::any_of(gClouds, [a_root](const Cloud& a_c) { return a_c.root.get() == a_root; });
			if (listed) {
				return true;
			}
			if (!scene || !holder || gClouds.size() >= kMostClouds || !CloudOn(a_base)) {
				return false;
			}
			auto* light = LightKit::CloneLight();
			if (!light) {
				return true;
			}
			light->name = gHost.cloudName;
			Dress(light, { 1.0f, 1.0f, 1.0f }, 0.0f, 1.0f, InverseSquare());
			light->local.translate = holder->world.Invert() * a_ref->GetPosition();
			light->local.scale = 1.0f;
			auto* bs = LightKit::Hang(holder, light, scene, 2.0f);
			if (!bs) {
				return false;
			}
			const auto* proj = a_ref->As<RE::Projectile>();
			const auto* spell = proj ? proj->GetProjectileRuntimeData().spell : nullptr;
			gClouds.push_back({ RE::NiPointer<RE::NiAVObject>(a_root), RE::NiPointer<RE::TESObjectREFR>(a_ref), RE::NiPointer<RE::NiNode>(holder),
				RE::NiPointer<RE::BSLight>(bs), a_base, spell ? spell->GetFormID() : 0, a_ref->GetPosition(), a_ref->GetPosition() });
			if (static std::size_t told = 0; told < 6) {
				++told;
				SKSE::log::info("[STREAM-CLOUD] {} | one light where its cloud is, held {:.1f} s after the cone ends", Label(a_ref->GetBaseObject()),
					BeamRow::kCloudLife);
			}
			return true;
		}

		// one cloud's light this frame: on its cone's path while the cone flies, then where it was, fading out over the cloud's life;
		// false once it has faded, its switch is off or the node holding it left the world
		[[nodiscard]] bool MoveCloud(Cloud& a_c, float a_delta)
		{
			if (a_c.root && a_c.root->parent && a_c.ref && a_c.ref->Get3D() == a_c.root.get()) {
				a_c.spot = a_c.launch + (a_c.ref->GetPosition() - a_c.launch) * BeamRow::kCloudAlong;
			} else if (a_c.sinceEnd < 0.0f) {
				a_c.sinceEnd = 0.0f;
				a_c.root.reset();  // the cone is let go: only where its cloud is stays
				a_c.ref.reset();
			} else {
				a_c.sinceEnd += a_delta;
			}
			auto*       light = a_c.light ? netimmerse_cast<RE::NiPointLight*>(a_c.light->light.get()) : nullptr;
			const auto  rec = gRows.find(a_c.base);
			const float left = BeamRow::CloudFade(a_c.sinceEnd, BeamRow::kCloudLife);
			if (!light || !a_c.holder || !a_c.holder->parent || rec == gRows.end() || left <= 0.0f || !CloudOn(a_c.base)) {
				return false;
			}
			const bool isl = InverseSquare();
			// one light, not a row: where the inverse-square cap holds, its reach (and with it that cap) is its own
			const float cloudReach = isl ? kCloudReachShaders : RowReachFor(LightingNow());
			const float reach = rec->second.reach * ReachNow() * cloudReach;
			const float rowBright = BrightnessNow();
			const float want = rec->second.fade * rowBright * RowStrengthFor(LightingNow());
			const float steady = BeamRow::Steady(want, isl ? IslTop(reach) * (std::min)(rowBright, 1.0f) : 0.0f);
			RE::NiColor color = rec->second.color;
			if (const auto own = SpellColor(a_c.spell)) {
				color = *own;
			}
			Dress(light, color, steady * left, reach, isl);
			light->local.translate = a_c.holder->world.Invert() * a_c.spot;
			RE::NiUpdateData update{};
			light->Update(update);
			return true;
		}

		void MoveClouds()
		{
			auto*       scene = LightKit::Scene();
			const float delta = std::clamp(RE::GetSecondsSinceLastFrame(), 0.0f, 0.25f);
			for (auto it = gClouds.begin(); it != gClouds.end();) {
				if (MoveCloud(*it, delta)) {
					++it;
				} else {
					DropCloud(*it, scene);
					it = gClouds.erase(it);
				}
			}
		}

		// ------------------------------------------------------------------ who draws a beam
		// the carriers in their fixed rank: Creatures first (every creature light is Creatures', his 2026-11-12 rule), then Illuminated,
		// then RELight - the first loaded one that claims a base draws it
		constexpr std::array<const wchar_t*, 3> kCarriers{ L"IlluminatedCreatureLightingNG.dll", L"Illuminated.dll", L"RELightSpellLightingNG.dll" };
		constexpr std::array<const char*, 3>    kCarrierNames{ "IlluminatedCreatureLightingNG.dll", "Illuminated.dll", "RELightSpellLightingNG.dll" };
		std::unordered_map<RE::FormID, bool>    gMine;  // a base hung once: whether this plugin draws it (main thread; cleared by Claim)

		HMODULE SelfModule()
		{
			HMODULE self = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&SelfModule), &self);
			return self;
		}

		[[nodiscard]] bool Mine(RE::FormID a_base)
		{
			if (const auto it = gMine.find(a_base); it != gMine.end()) {
				return it->second;
			}
			const auto owner = Owner(a_base);
			const bool mine = owner == "self";
			gMine.emplace(a_base, mine);
			if (!mine) {
				SKSE::log::info("[STREAM-ROW] {:08X} is drawn by {} - this plugin stands down for it", a_base, owner);
			}
			return mine;
		}

		// a bolt's, flame's or cone's row: as many small lights as its range needs (BeamRow::Lay), hung off its root, within the scene's
		// budget. Main thread.
		void HangRow(RE::TESObjectREFR* a_ref, RE::NiNode* a_root, RE::FormID a_base, const Recipe& a_rec)
		{
			auto* scene = LightKit::Scene();
			auto* proj = a_ref->As<RE::Projectile>();
			if (!scene || !proj || !Mine(a_base)) {
				return;
			}
			if (a_rec.cloud) {
				// one light where its cloud is, not a row along the cone; not hangable yet: tried every frame for a while
				if (!HangCloud(a_ref, a_root, a_base) && gCloudWait.size() < kMostClouds * 4) {
					gCloudWait.push_back({ RE::NiPointer<RE::TESObjectREFR>(a_ref), a_base });
				}
				return;
			}
			if (!RowOn(a_rec)) {
				ParkRow(a_ref);
				return;
			}
			std::size_t n = 0;
			{
				std::lock_guard l{ gLiveLock };
				if (gLive.contains(a_root)) {
					return;
				}
				if (gRowLights >= kMostRowLights) {
					SweepGone(scene);
				}
				const auto most = InverseSquare() ? kMostPerRow : kMostPerRowPlain;
				const auto want = BeamRow::Lay(proj->GetProjectileRuntimeData().range, static_cast<int>(most), a_rec.reach, kRowHeight);
				n = (std::min)(want.size(), kMostRowLights - (std::min)(gRowLights, kMostRowLights));
			}
			if (!n) {
				ParkRow(a_ref);  // the light cap is full: tried again when a row lets its lights go
				return;
			}
			const bool                              isl = InverseSquare();
			std::vector<RE::NiPointer<RE::BSLight>> made;
			for (std::size_t i = 0; i < n; ++i) {
				auto* light = LightKit::CloneLight();  // ReLight's master-and-clone (LightKit.h)
				if (!light) {
					break;
				}
				light->name = gHost.rowName;
				Dress(light, a_rec.color, 0.0f, 1.0f, isl);  // dark until MoveRows lays it out this frame
				light->local.translate = {};
				light->local.scale = 1.0f;
				if (auto* bs = LightKit::Hang(a_root, light, scene, 2.0f)) {
					made.emplace_back(bs);
				}
			}
			static std::size_t told = 0;
			if (told < 8 && !made.empty()) {
				++told;
				SKSE::log::info("[STREAM-ROW] {} | {} light(s) along {:.0f} | reach target {:.0f} | fade {:.2f}", Label(a_ref->GetBaseObject()),
					made.size(), proj->GetProjectileRuntimeData().range, a_rec.reach, a_rec.fade);
			}
			if (made.empty()) {
				ParkRow(a_ref);
				return;
			}
			std::lock_guard l{ gLiveLock };
			gRowLights += made.size();
			gLive.emplace(a_root, Live{ RE::NiPointer<RE::NiAVObject>(a_root), a_base, std::move(made), RE::NiPointer<RE::TESObjectREFR>(a_ref) });
			MoveRows();  // laid out at once, not a frame late
		}

		void HangFor(RE::TESObjectREFR* a_ref, RE::NiAVObject* a_object)
		{
			auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
			if (!base || !a_object || !a_object->AsNode()) {
				return;
			}
			if (const auto row = gRows.find(base->GetFormID()); row != gRows.end()) {
				HangRow(a_ref, a_object->AsNode(), base->GetFormID(), row->second);
			}
		}

		// called before the game releases the reference's 3D, so its root is still there to find the entry by
		void DropFor(RE::TESObjectREFR* a_ref)
		{
			auto* root = a_ref ? a_ref->Get3D() : nullptr;
			if (!root) {
				return;
			}
			std::lock_guard l{ gLiveLock };
			if (!gLive.contains(root)) {
				return;
			}
			if (OnMainThread()) {
				Forget(root, LightKit::Scene());
			} else {
				gToDrop.emplace_back(root);
			}
		}

		// ------------------------------------------------------------------ the hooks
		// Load3D builds a reference's 3D (vtable slot 0x6A); Release3DRelatedData takes it apart (0x6B). The idle path is the original
		// call and one lookup of the base in this plugin's rows.
		template <class T>
		struct Load3D
		{
			static RE::NiAVObject* thunk(T* a_this, bool a_backgroundLoading)
			{
				auto*       object = original(a_this, a_backgroundLoading);
				const auto* base = a_this ? a_this->GetBaseObject() : nullptr;
				if (object && base && gRows.contains(base->GetFormID())) {
					LightKit::Guarded("stream row (3D load)", [&]() {
						if (OnMainThread()) {
							HangFor(a_this, object);
						} else {
							std::lock_guard l{ gLiveLock };
							gToHang.emplace_back(a_this);  // held by its refcount, no handle made on this thread
						}
					});
					gAwake.store(true, std::memory_order_relaxed);  // after the work: the pass goes to sleep only under gLiveLock
				}
				return object;
			}

			static inline REL::Relocation<decltype(thunk)> original;

			static void Install() { original = REL::Relocation<std::uintptr_t>(T::VTABLE[0]).write_vfunc(0x6A, thunk); }
		};

		template <class T>
		struct Release3D
		{
			static void thunk(T* a_this)
			{
				const auto* base = a_this ? a_this->GetBaseObject() : nullptr;
				if (base && gRows.contains(base->GetFormID())) {
					LightKit::Guarded("stream row (3D release)", [a_this]() { DropFor(a_this); });
					gAwake.store(true, std::memory_order_relaxed);
				}
				original(a_this);
			}

			static inline REL::Relocation<decltype(thunk)> original;

			static void Install() { original = REL::Relocation<std::uintptr_t>(T::VTABLE[0]).write_vfunc(0x6B, thunk); }
		};
	}

	void Install(const Host& a_host)
	{
		gHost = a_host;
		Load3D<RE::BeamProjectile>::Install();  // a bolt's row of lights (BeamRow.h)
		Release3D<RE::BeamProjectile>::Install();
		Load3D<RE::FlameProjectile>::Install();  // a flame spray's and a cone's row, along their aim
		Release3D<RE::FlameProjectile>::Install();
		Load3D<RE::ConeProjectile>::Install();
		Release3D<RE::ConeProjectile>::Install();
	}

	std::size_t Claim(std::unordered_map<RE::FormID, Recipe> a_rows)
	{
		gRows = std::move(a_rows);
		gMine.clear();
		const auto beams = std::ranges::count_if(gRows, [](const auto& a_r) { return a_r.second.beam; });
		SKSE::log::info("stream rows: {} bolt(s) and {} spray(s) / cone(s) wear a row of lights along their real length (at most {} per row, {} in the scene)",
			beams, gRows.size() - static_cast<std::size_t>(beams), kMostPerRow, kMostRowLights);
		return gRows.size();
	}

	void Recolor(RE::FormID a_base, const Recipe& a_recipe)
	{
		if (const auto it = gRows.find(a_base); it != gRows.end()) {
			it->second = a_recipe;
		}
	}

	void Refresh()
	{
		// a row goes with its switch at once; it is hung again when the switch comes back while its projectile still flies
		auto*           scene = LightKit::Scene();
		std::lock_guard l{ gLiveLock };
		ForgetIf(scene, [](const Live& a_live) {
			const auto rec = gRows.find(a_live.base);
			const bool off = rec == gRows.end() || !RowOn(rec->second);
			if (off && rec != gRows.end()) {
				ParkRow(a_live.beam.get());
			}
			return off;
		});
	}

	bool Awake() { return gAwake.load(std::memory_order_relaxed); }

	void Frame()
	{
		if (!gAwake.load(std::memory_order_relaxed)) {
			return;
		}
		{
			// what the last frame dropped under gLiveLock, let go here with the lock free
			std::vector<RE::NiPointer<RE::TESObjectREFR>> letGo;
			{
				std::lock_guard l{ gLiveLock };
				letGo.swap(gLetGo);
			}
		}
		// a row its switch or the light cap held back: tried again while its projectile flies, let go when that ends
		if (!gParkedRows.empty()) {
			bool room = false;
			{
				std::lock_guard l{ gLiveLock };
				room = gRowLights < kMostRowLights;
			}
			auto parked = std::move(gParkedRows);
			gParkedRows.clear();
			for (const auto& ref : parked) {
				auto*      root = ref ? ref->Get3D() : nullptr;
				auto*      base = ref ? ref->GetBaseObject() : nullptr;
				const auto rec = base ? gRows.find(base->GetFormID()) : gRows.end();
				if (!root || !root->parent || !root->AsNode() || rec == gRows.end()) {
					continue;
				}
				if (room) {
					HangRow(ref.get(), root->AsNode(), base->GetFormID(), rec->second);  // parks it again while its row stays off
				} else {
					gParkedRows.push_back(ref);
				}
			}
		}
		std::erase_if(gCloudWait, [](CloudWait& a_w) {
			auto* root = a_w.ref ? a_w.ref->Get3D() : nullptr;
			return !root || !root->AsNode() || HangCloud(a_w.ref.get(), root->AsNode(), a_w.base) || ++a_w.frames >= kCloudTries;
		});
		if (!gClouds.empty()) {
			MoveClouds();  // a cone's cloud light: on its path, then where the cloud is, fading out
		}
		std::vector<RE::NiPointer<RE::TESObjectREFR>> hang;
		{
			std::lock_guard l{ gLiveLock };
			// about every five seconds, the lights whose 3D is gone are let go even when no cap was reached
			if (++gSweepClock >= 300 && !gLive.empty()) {
				gSweepClock = 0;
				SweepGone(LightKit::Scene());
			}
			if (gRowLights) {
				MoveRows();  // every row follows its real length this frame
			}
			if (gToHang.empty() && gToDrop.empty()) {
				if (gLive.empty() && gLetGo.empty() && gParkedRows.empty() && gCloudWait.empty() && gClouds.empty()) {
					gAwake.store(false, std::memory_order_relaxed);
				}
				return;
			}
			auto* scene = LightKit::Scene();
			for (const auto& root : gToDrop) {
				Forget(root.get(), scene);
			}
			gToDrop.clear();
			hang.swap(gToHang);
		}
		for (const auto& ref : hang) {
			// a 3D released before this frame is gone (or another one now): lit only when the 3D it holds now is in the world
			if (auto* root = ref ? ref->Get3D() : nullptr; root && root->parent) {
				HangFor(ref.get(), root);
			}
		}
	}

	void DropAll()
	{
		auto* scene = LightKit::Scene();
		// every projectile this drops is let go after the lock: declared before it, so it goes last
		std::vector<RE::NiPointer<RE::TESObjectREFR>> letGo;
		std::lock_guard                               l{ gLiveLock };
		ForgetIf(scene, [](const Live&) { return true; });
		letGo.insert(letGo.end(), std::make_move_iterator(gToHang.begin()), std::make_move_iterator(gToHang.end()));
		gToHang.clear();
		gToDrop.clear();
		for (auto& cloud : gClouds) {
			DropCloud(cloud, scene);
			letGo.push_back(std::move(cloud.ref));
		}
		gClouds.clear();
		for (auto& wait : gCloudWait) {
			letGo.push_back(std::move(wait.ref));
		}
		gCloudWait.clear();
		letGo.insert(letGo.end(), std::make_move_iterator(gParkedRows.begin()), std::make_move_iterator(gParkedRows.end()));
		gParkedRows.clear();
		letGo.insert(letGo.end(), std::make_move_iterator(gLetGo.begin()), std::make_move_iterator(gLetGo.end()));
		gLetGo.clear();
		gRowLights = 0;
	}

	bool Claims(RE::FormID a_base) { return gRows.contains(a_base); }

	std::string_view Owner(RE::FormID a_base)
	{
		const HMODULE self = SelfModule();
		for (std::size_t i = 0; i < kCarriers.size(); ++i) {
			const HMODULE module = GetModuleHandleW(kCarriers[i]);
			if (!module) {
				continue;
			}
			if (module == self) {
				if (Claims(a_base)) {
					return "self";
				}
				continue;
			}
			using ClaimsFn = bool (*)(std::uint32_t);
			if (const auto claims = reinterpret_cast<ClaimsFn>(GetProcAddress(module, "OurStreamRowsClaims"))) {
				if (claims(a_base)) {
					return kCarrierNames[i];
				}
			} else if (gHost.oldCarrier && gHost.oldCarrier(kCarrierNames[i], a_base)) {
				return kCarrierNames[i];  // an older carrier with no export still draws this beam itself
			}
		}
		return Claims(a_base) ? "self" : "";
	}

	bool IsRowLight(const RE::NiLight* a_light)
	{
		return a_light && (a_light->name == gHost.rowName || a_light->name == gHost.cloudName);
	}

	Counts Stats()
	{
		std::lock_guard l{ gLiveLock };
		return { gLive.size(), gRowLights, gParkedRows.size(), gClouds.size() };
	}
}

// the claim table's way in for the other carriers (StreamRows::Owner): true when this plugin claimed the base
extern "C" __declspec(dllexport) bool OurStreamRowsClaims(std::uint32_t a_base)
{
	return StreamRows::Claims(a_base);
}
