// Illuminated - Creature Lighting NG - tests for src/AnimaMath.h and the shared Floor Light Fix's src/FloorLightMath.h (no game needed).
// Copyright (C) 2026 izzydoingit. GPL-3.0-or-later.
//
// PC Runner\plugin build.py builds and runs them (xmake target testanima), then again under clang's address and
// undefined-behavior checkers. The run fails when a check fails, AND when fewer checks ran than kFloor (a test that stopped
// part way proves nothing).

#include "AnimaMath.h"
#include "FloorLightChecks.h"
#include "RowParkChecks.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace
{
	int gFailed = 0, gPassed = 0;

	void Check(bool a_ok, const char* a_what, int a_line)
	{
		if (a_ok) {
			++gPassed;
		} else {
			++gFailed;
			if (gFailed <= 30) {
				std::printf("FAIL line %d: %s\n", a_line, a_what);
			}
		}
	}
#define CHECK(x) Check((x), #x, __LINE__)

	// the fewest checks a whole run makes (raise it when checks are added; lower it only when checks are removed on purpose)
	constexpr int kFloor = 256 + FloorLightChecks::kCount + RowParkChecks::kCount;

	[[nodiscard]] bool Near(float a, float b, float a_eps = 0.001f) { return std::fabs(a - b) <= a_eps; }

	void CreatureFiles()
	{
		std::istringstream in(
			"\xEF\xBB\xBF# a comment\n"
			"[creature]\n"
			"name=Flame Atronach\n"
			"race=AtronachFlameRace\n"
			"element=fire\n"
			"color=255,140,40\n"
			"point=0,-0.7,94.4\n"
			"reach=512\n"
			"ember=yes\n"
			"\n"
			"[creature]\n"
			"name=no creature named\n"
			"color=1,2,3\n"
			"[creature]\n"
			"name=bad color\n"
			"race=X\n"
			"color=red\n"
			"[creature]\n"
			"name=Familiar\n"
			"model=Meshes/Actors/Canine/Character Assets Wolf/WolfFire.nif ; a trailing comment\n"
			"kind=eyes\n"
			"element=ice\n"
			"color=300,-5,12.6\n"
			"reach=99999\n"
			"strength=abc\n"
			"point=1,2,3\n"
			"point=4,5,6\n"
			"[other]\n"
			"race=ignored\n");
		std::size_t skipped = 0;
		const auto  rows = Anima::ReadCreatures(in, skipped);
		CHECK(rows.size() == 2);
		CHECK(skipped == 2);
		if (rows.size() != 2) {
			return;
		}
		const auto& a = rows[0];
		CHECK(a.name == "Flame Atronach");
		CHECK(a.races.size() == 1 && a.races[0] == "atronachflamerace");
		CHECK(a.element == Anima::Element::kFire);
		CHECK(a.kind == Anima::Kind::kGlow);
		CHECK((a.rgb == Anima::Rgb{ 255, 140, 40 }));
		CHECK(a.points.size() == 1 && Near(a.points[0].z, 94.4f) && Near(a.points[0].y, -0.7f));
		CHECK(Near(a.reach, 512.0f));
		CHECK(a.ember);
		const auto& b = rows[1];
		CHECK(b.models.size() == 1 && b.models[0] == "actors\\canine\\character assets wolf\\wolffire.nif");
		CHECK(b.kind == Anima::Kind::kEyes);
		CHECK(b.element == Anima::Element::kIce);
		CHECK((b.rgb == Anima::Rgb{ 255, 0, 13 }));
		std::istringstream huge("[creature]\nrace=X\ncolor=1e30,-1e30,0\n");  // clamped before the cast to int
		std::size_t        hugeSkipped = 0;
		const auto         hugeRows = Anima::ReadCreatures(huge, hugeSkipped);
		CHECK(hugeRows.size() == 1 && (hugeRows[0].rgb == Anima::Rgb{ 255, 0, 0 }));
		CHECK(Near(b.reach, 2048.0f));
		CHECK(Near(b.strength, 1.0f));
		CHECK(b.points.size() == 2);
		CHECK(!b.ember);

		std::istringstream spell("[creature]\nkind=burst\nrecord=0x19531~Dragonborn.esm\nsize=99\ncolor=255,140,40\n[creature]\nkind=spell\nsize=1.98\n");
		std::size_t        s1 = 0;
		const auto         sp = Anima::ReadCreatures(spell, s1);
		CHECK(sp.size() == 1 && s1 == 1);  // a row naming no creature, model or record is left out
		CHECK(!sp.empty() && sp[0].kind == Anima::Kind::kBurst && sp[0].records.size() == 1 && sp[0].records[0] == "0x19531~Dragonborn.esm");
		CHECK(!sp.empty() && Near(sp[0].size, 8.0f));

		// an art row (a hit art, an enchantment's art) stands on its art= line alone; a burst's 7.1 flash is kept, 99 stops at the top
		std::istringstream arts("[creature]\nkind=spell\nart=DLC01\\Effects\\SummonUndeadTarget.nif\nstrength=7.1\ncolor=97,78,255\n[creature]\nrace=X\nstrength=99\n");
		std::size_t        s5 = 0;
		const auto         ar = Anima::ReadCreatures(arts, s5);
		CHECK(ar.size() == 2 && s5 == 0);
		if (ar.size() == 2) {
			CHECK(ar[0].arts.size() == 1 && ar[0].arts[0] == "dlc01\\effects\\summonundeadtarget.nif");
			CHECK(ar[0].models.empty() && ar[0].records.empty());
			CHECK(Near(ar[0].strength, 7.1f));
			CHECK(Near(ar[1].strength, 12.0f));
		}
		std::istringstream impacts("[creature]\nkind=burst\nimpact=Data\\Meshes\\DLC02\\Effects\\DLC2HMDaedraImpact01.nif\ncolor=255,255,255\n");
		std::size_t        sImpact = 0;
		const auto         im = Anima::ReadCreatures(impacts, sImpact);
		CHECK(im.size() == 1 && sImpact == 0 && im[0].impacts.size() == 1 && im[0].impacts[0] == "dlc02\\effects\\dlc2hmdaedraimpact01.nif");

		// on a bone: one light per offset, or one on the bone itself; on a body without it: the row's points
		std::istringstream bone("[creature]\nrace=X\nnode=FireAtronach_Spine2 [Spn2]\noffset=0,0,5.8\noffset=1,2,3\noffset=bad\npoint=0,-0.7,94.4\n[creature]\nrace=Y\nnode=Z\npoint=1,1,1\npoint=2,2,2\n");
		std::size_t        s4 = 0;
		const auto         bones = Anima::ReadCreatures(bone, s4);
		CHECK(bones.size() == 2 && s4 == 0);
		if (bones.size() == 2) {
			CHECK(bones[0].nodes.size() == 1 && bones[0].nodes[0] == "FireAtronach_Spine2 [Spn2]");
			CHECK(bones[0].offsets.size() == 2);
			const auto onIt = Anima::SpotsOn(bones[0], true);
			CHECK(onIt.size() == 2 && Near(onIt[0].z, 5.8f) && Near(onIt[1].y, 2.0f));
			const auto offIt = Anima::SpotsOn(bones[0], false);
			CHECK(offIt.size() == 1 && Near(offIt[0].z, 94.4f));
			const auto bare = Anima::SpotsOn(bones[1], true);
			CHECK(bare.size() == 1 && Near(bare[0].x, 0.0f) && Near(bare[0].z, 0.0f));
			CHECK(Anima::SpotsOn(bones[1], false).size() == 2);
		}

		std::istringstream none("[creature]\nrace=X\ncolor=1,2,3\n");
		std::size_t        s2 = 0;
		const auto         one = Anima::ReadCreatures(none, s2);
		CHECK(one.size() == 1 && one[0].points.size() == 1 && Near(one[0].points[0].z, 0.0f));
		std::istringstream empty("");
		std::size_t        s3 = 0;
		CHECK(Anima::ReadCreatures(empty, s3).empty() && s3 == 0);

		// nan and inf read as numbers, and clamp lets a nan through: they never reach a light
		std::istringstream notNumbers("[creature]\nrace=X\nstrength=nan\nreach=inf\n");
		std::size_t        s6 = 0;
		const auto         kept = Anima::ReadCreatures(notNumbers, s6);
		CHECK(kept.size() == 1 && Near(kept[0].strength, 1.0f) && Near(kept[0].reach, 256.0f));
	}

	void Paths()
	{
		CHECK(Anima::NormalPath("Data/Meshes/Actors/A.NIF") == "actors\\a.nif");
		CHECK(Anima::NormalPath("meshes\\x\\y.nif") == "x\\y.nif");
		CHECK(Anima::NormalPath("Actors\\Wisp\\wisp.nif") == "actors\\wisp\\wisp.nif");
		CHECK(Anima::Trim("  a b \r") == "a b");
		CHECK(Anima::Trim("\xEF\xBB\xBFkey") == "key");
		const auto n = Anima::Numbers("1, 2.5 ,-3", 3);
		CHECK(n && n->size() == 3 && Near((*n)[1], 2.5f) && Near((*n)[2], -3.0f));
		CHECK(!Anima::Numbers("1,x,3", 3));
		CHECK(!Anima::Numbers("nan", 1));
		CHECK(!Anima::Numbers("inf", 1));
		CHECK(!Anima::Numbers("1,-inf,3", 3));
		const std::vector<std::string> keys{ "AshProjectile01", "" };
		CHECK(Anima::MeshKeyNames("Meshes/Magic/AshProjectile01.nif", keys));
		CHECK(Anima::MeshKeyNames("magic\\xashprojectile01b.nif", keys));  // RE::Light matches by a part of the name
		CHECK(!Anima::MeshKeyNames("ashprojectile01\\other.nif", keys));   // never by a folder
		CHECK(!Anima::MeshKeyNames("", keys));
		CHECK(!Anima::MeshKeyNames("a.nif", {}));
	}

	void Elements()
	{
		CHECK(Anima::Natural(Anima::Element::kIce));
		CHECK(Anima::Natural(Anima::Element::kWind));
		CHECK(Anima::Natural(Anima::Element::kWater));
		CHECK(Anima::Natural(Anima::Element::kStone));
		CHECK(!Anima::Natural(Anima::Element::kFire));
		CHECK(!Anima::Natural(Anima::Element::kShock));
		CHECK(!Anima::Natural(Anima::Element::kSpirit));
		CHECK(Anima::PulseOf(Anima::Element::kFire) == Pulse::Kind::kFire);
		CHECK(Anima::PulseOf(Anima::Element::kShock) == Pulse::Kind::kShock);
		CHECK(Anima::PulseOf(Anima::Element::kStone) == Pulse::Kind::kNone);
		CHECK(Anima::PulseOf(Anima::Element::kSpirit) == Pulse::Kind::kHealing);
	}

	void SettingsText()
	{
		Anima::Settings d;
		CHECK(d.on && !d.natural && d.brightness == 100 && d.reach == 100 && d.flicker && d.deathFade && d.surge);
		CHECK(std::ranges::none_of(d.theirs, [](bool a_on) { return a_on; }));  // ours is the default on every other mod's creatures
		// the menu's steps of 5, inside the bounds: the slider and the settings file use the one snap
		CHECK(Anima::SnapPercent(102, Anima::kReachMin, Anima::kReachMax) == 100 && Anima::SnapPercent(103, Anima::kReachMin, Anima::kReachMax) == 105);
		CHECK(Anima::SnapPercent(0, Anima::kReachMin, Anima::kReachMax) == Anima::kReachMin);
		CHECK(Anima::SnapPercent(100000, Anima::kBrightnessMin, Anima::kBrightnessMax) == Anima::kBrightnessMax);
		CHECK(Anima::SnapPercent(-100000, Anima::kBrightnessMin, Anima::kBrightnessMax) == Anima::kBrightnessMin);
		// a skin's addons that count: those a wearing race takes; all of them with no wearer known, or none that fits
		const auto fits = [](int a_addon, int a_race) { return a_addon == a_race; };
		CHECK((Anima::AddonsWorn(std::vector{ 1, 2, 3 }, std::vector{ 2 }, fits) == std::vector{ 2 }));
		CHECK((Anima::AddonsWorn(std::vector{ 1, 2, 3 }, std::vector{ 3, 1 }, fits) == std::vector{ 1, 3 }));
		CHECK((Anima::AddonsWorn(std::vector{ 1, 2, 3 }, std::vector<int>{}, fits) == std::vector{ 1, 2, 3 }));
		CHECK((Anima::AddonsWorn(std::vector{ 1, 2, 3 }, std::vector{ 9 }, fits) == std::vector{ 1, 2, 3 }));
		// a null addon (0 here) never counts, on either path
		CHECK((Anima::AddonsWorn(std::vector{ 0, 1, 2 }, std::vector<int>{}, fits) == std::vector{ 1, 2 }));
		CHECK((Anima::AddonsWorn(std::vector{ 0, 1, 2 }, std::vector{ 9 }, fits) == std::vector{ 1, 2 }));
		CHECK((Anima::AddonsWorn(std::vector{ 0, 1, 2 }, std::vector{ 2 }, fits) == std::vector{ 2 }));
		CHECK(Anima::AddonsWorn(std::vector{ 0, 0 }, std::vector<int>{}, fits).empty());
		CHECK(d.kinds[0] && d.kinds[1] && d.kinds[2] && d.kinds[3]);

		Anima::Settings s;
		s.on = false;
		s.kinds[2] = false;
		s.natural = true;
		s.brightness = 150;
		s.reach = 75;
		s.flicker = false;
		s.theirs = { true, false, true, false };
		s.extras[0] = false;
		std::ostringstream out;
		Anima::WriteSettings(s, out);
		std::istringstream in(out.str());
		Anima::Settings    r;
		const auto         keys = Anima::ReadSettings(in, r);
		CHECK(keys == 12 + Anima::kOthers + Anima::kExtraCount);
		CHECK(!r.on && !r.kinds[2] && r.kinds[1] && r.natural && r.brightness == 150 && r.reach == 75 && !r.flicker && r.deathFade);
		CHECK(r.theirs[0] && !r.theirs[1] && r.theirs[2] && !r.theirs[3]);
		CHECK(!r.extras[0]);                                                   // a light of its own switch keeps its off through the file
		CHECK(std::ranges::all_of(d.extras, [](bool a_on) { return a_on; }));  // every such light is on by default

		std::istringstream odd("\xEF\xBB\xBF[Settings]\nBrightness=99999\nReach=abc\nUnknown=1\nLights = off\n; Reach=10\n");
		Anima::Settings    o;
		CHECK(Anima::ReadSettings(odd, o) == 3);
		CHECK(o.brightness == Anima::kBrightnessMax);
		CHECK(o.reach == 100);
		CHECK(!o.on);

		// a creature's own switch: every creature is on until switched off; off ones go through the file one line each, and back on
		// leaves no line (an older file with no such line reads with every creature on)
		Anima::Settings c;
		CHECK(Anima::CreatureOn(c, "Flame Atronach") && !c.creaturesOff);
		Anima::SetCreatureOn(c, "Flame Atronach", false);
		Anima::SetCreatureOn(c, "Wisp (Soul Cairn)", false);
		CHECK(!Anima::CreatureOn(c, "Flame Atronach") && !Anima::CreatureOn(c, "Wisp (Soul Cairn)") && Anima::CreatureOn(c, "Spriggan"));
		const auto         held = c.creaturesOff;
		std::ostringstream cOut;
		Anima::WriteSettings(c, cOut);
		std::istringstream cIn(cOut.str());
		Anima::Settings    cr;
		Anima::ReadSettings(cIn, cr);
		CHECK(!Anima::CreatureOn(cr, "Flame Atronach") && !Anima::CreatureOn(cr, "Wisp (Soul Cairn)") && Anima::CreatureOn(cr, "Spriggan"));
		Anima::SetCreatureOn(c, "Flame Atronach", true);
		Anima::SetCreatureOn(c, "Wisp (Soul Cairn)", true);
		CHECK(!c.creaturesOff && held && held->size() == 2);  // a list already handed out never changes under its reader
	}

	void Showing()
	{
		Anima::Row      fire;
		Anima::Row      ice;
		Anima::Settings s;
		fire.element = Anima::Element::kFire;
		ice.element = Anima::Element::kIce;
		CHECK(Anima::Shown(fire, s));
		CHECK(!Anima::Shown(ice, s));  // the elements that should not glow are off by default
		s.natural = true;
		CHECK(Anima::Shown(ice, s));
		s.kinds[0] = false;
		CHECK(!Anima::Shown(fire, s));
		s.kinds[0] = true;
		s.on = false;
		CHECK(!Anima::Shown(fire, s));

		// a creature's own switch: its body rows (glow, eyes, machines) go off with it, a spell row of the same name does not; a row names
		// its creature with creature=, else by its own name
		std::istringstream named(
			"[creature]\nname=Draugr eyes\nrace=DraugrRace\nkind=eyes\ncreature=Draugr\ncolor=80,218,255\n"
			"[creature]\nname=Draugr\nrace=DraugrRace\ncolor=80,218,255\n"
			"[creature]\nname=Draugr\nmodel=magic\\shout.nif\nkind=spell\ncolor=1,2,3\n");
		std::size_t sNamed = 0;
		const auto  nr = Anima::ReadCreatures(named, sNamed);
		CHECK(nr.size() == 3 && nr[0].creature == "Draugr" && nr[1].creature == "Draugr");
		Anima::Settings off;
		Anima::SetCreatureOn(off, "Draugr", false);
		CHECK(nr.size() == 3 && !Anima::Shown(nr[0], off) && !Anima::Shown(nr[1], off) && Anima::Shown(nr[2], off));

		// a row of its own switch (his 575): read off switch=, shown only while that switch is on; a switch name no menu has leaves the row
		// out (counted), so no light is left on with nothing to turn it off
		std::istringstream steam(
			"[creature]\nname=Steam\nmodel=magic\\steam.nif\nkind=spell\nelement=fire\nswitch=CenturionSteam\ncolor=255,133,75\n"
			"[creature]\nname=Odd\nmodel=magic\\odd.nif\nkind=spell\nelement=fire\nswitch=NoSuchSwitch\ncolor=1,2,3\n");
		std::size_t skipped = 0;
		const auto  rows = Anima::ReadCreatures(steam, skipped);
		CHECK(rows.size() == 1 && skipped == 1);
		if (rows.size() == 1) {
			CHECK(rows[0].extra == std::optional<std::size_t>(0));
			Anima::Settings e;
			CHECK(Anima::Shown(rows[0], e));
			e.extras[0] = false;
			CHECK(!Anima::Shown(rows[0], e));
			e.extras[0] = true;
			e.kinds[static_cast<std::size_t>(Anima::Kind::kSpell)] = false;
			CHECK(!Anima::Shown(rows[0], e));  // its kind's switch still rules it
		}
		Anima::Row stray;
		stray.extra = Anima::kExtraCount;  // never made by the reader; a bad index lights nothing rather than reading past the switches
		CHECK(!Anima::Shown(stray, Anima::Settings{}));

		// a stream row (the beams and breaths StreamRows draws): read off stream= with its records; an unknown kind of row, or one that names no
		// record, is left out and counted
		std::istringstream streams(
			"[creature]\nname=Bolt\nkind=spell\nstream=bolt\nrecord=0x4EFDF~Skyrim.esm\ncolor=81,111,255\n"
			"[creature]\nname=Odd\nkind=spell\nstream=laser\nrecord=0x1~Skyrim.esm\ncolor=1,2,3\n"
			"[creature]\nname=Bare\nkind=spell\nstream=spray\nmodel=magic\\x.nif\ncolor=1,2,3\n");
		std::size_t sStream = 0;
		const auto  sr = Anima::ReadCreatures(streams, sStream);
		CHECK(sr.size() == 1 && sStream == 2);
		CHECK(!sr.empty() && sr[0].stream == Anima::Stream::kBolt && sr[0].records.size() == 1);

		// a moved row (lit by Illuminated / RELight before 1.1): moved=yes marks it, any other value leaves the row out (counted), and a row
		// without the line is not moved
		std::istringstream movedIn(
			"[creature]\nname=Moved\nmodel=magic\\a.nif\nkind=burst\nmoved=yes\ncolor=1,2,3\n"
			"[creature]\nname=Typo\nmodel=magic\\b.nif\nkind=burst\nmoved=yse\ncolor=1,2,3\n"
			"[creature]\nname=Own\nmodel=magic\\c.nif\nkind=burst\ncolor=1,2,3\n");
		std::size_t sMoved = 0;
		const auto  mv = Anima::ReadCreatures(movedIn, sMoved);
		CHECK(mv.size() == 2 && sMoved == 1);
		CHECK(mv.size() == 2 && mv[0].moved && !mv[1].moved);

		// a ghost row (his 573): read off shader= alone, it names a creature; it joins only a creature with no glow of its own
		std::istringstream ghost("[creature]\nname=Ghost\nshader=0x3B6CB~Skyrim.esm\nelement=spirit\ncolor=114,170,255\npoint=0,0,64\n");
		std::size_t        gs = 0;
		const auto         ghosts = Anima::ReadCreatures(ghost, gs);
		CHECK(ghosts.size() == 1 && gs == 0 && ghosts[0].shaders.size() == 1 && ghosts[0].shaders[0] == "0x3B6CB~Skyrim.esm");
		const auto glow = [](std::size_t a_i) { return a_i == 1; };  // row 1 a glow, row 2 eyes, row 9 the ghost
		CHECK((Anima::WithGhostRows(std::vector<std::size_t>{}, { 9 }, glow) == std::vector<std::size_t>{ 9 }));
		CHECK((Anima::WithGhostRows(std::vector<std::size_t>{ 2 }, { 9 }, glow) == std::vector<std::size_t>{ 2, 9 }));
		CHECK((Anima::WithGhostRows(std::vector<std::size_t>{ 1, 2 }, { 9 }, glow) == std::vector<std::size_t>{ 1, 2 }));
		CHECK((Anima::WithGhostRows(std::vector<std::size_t>{ 9 }, { 9 }, glow) == std::vector<std::size_t>{ 9 }));
	}

	void OtherMods()
	{
		CHECK(Anima::OtherModOf("illuminouselementals", "atronachflame") == std::optional<std::size_t>(1));
		CHECK(Anima::OtherModOf("dareniis creatures", "atronacharcane") == std::optional<std::size_t>(2));
		// the Emissive series by its exact folder and file; another mod's folder of the same plain name is not taken for it
		CHECK(Anima::OtherModOf("atronachs", "flame atronach") == std::optional<std::size_t>(Anima::kEmissive));
		CHECK(Anima::OtherModOf("spriggans", "spriggan") == std::optional<std::size_t>(Anima::kEmissive));
		CHECK(Anima::OtherModOf("netch", "emissive netch") == std::optional<std::size_t>(Anima::kEmissive));
		CHECK(Anima::OtherModOf("atronachs", "atronachflame") == std::optional<std::size_t>(Anima::kOtherUnnamed));
		CHECK(Anima::OtherModOf("soul gems", "emissive soul gems") == std::optional<std::size_t>(Anima::kOtherUnnamed));
		CHECK(!Anima::OtherModOf("illuminated", "explosions"));
		CHECK(!Anima::OtherModOf("illuminatedanima", "x"));
		CHECK(!Anima::OtherModOf("illuminatedcreaturelightingng", "x"));
		CHECK(Anima::OtherModOf("magic creatures of radiance", "atronachflame") == std::optional<std::size_t>(Anima::kOtherUnnamed));
	}

	// the mods that light a creature through an ability, Luminous Atronachs' route: its plugin overrides the creature's own effect, and a
	// script on that effect adds an ability of the plugin's whose effect carries the light
	void AbilityLights()
	{
		const auto luminous = Anima::NamedPluginMod("luminousatronachs.esp");
		CHECK(luminous && Anima::kOtherMods[*luminous].key == "TheirsLuminous");
		if (!luminous) {
			return;
		}
		CHECK(Anima::NamedPluginMod("luminousatronachs.esm") == luminous);
		CHECK(Anima::NamedPluginMod("luminousatronachs") == luminous);
		CHECK(!Anima::NamedPluginMod("skyrim.esm"));
		CHECK(!Anima::NamedPluginMod("luminousatronachs - patch.esp"));
		CHECK(!Anima::NamedPluginMod(""));

		Anima::Lighters none{};
		Anima::Lighters lit{};
		lit[*luminous] = true;
		const std::vector<std::string> game{ "skyrim.esm" };
		const std::vector<std::string> overridden{ "skyrim.esm", "unofficial skyrim special edition patch.esp", "luminousatronachs.esp" };
		const std::vector<std::string> made{ "luminousatronachs.esp" };
		const std::vector<std::string> stranger{ "skyrim.esm", "someonesmod.esp" };

		CHECK(!Anima::AbilityClaim(false, game, lit));                                                         // the game's own effect, no light of its own
		CHECK(Anima::AbilityClaim(false, overridden, lit) == luminous);                                        // the script's route: an override + a lit ability in that plugin
		CHECK(!Anima::AbilityClaim(false, overridden, none));                                                  // its download without lights overrides the effect and lights nothing
		CHECK(!Anima::AbilityClaim(false, stranger, lit));                                                     // another plugin overriding the effect claims nothing
		CHECK(Anima::AbilityClaim(true, made, none) == luminous);                                              // the effect itself carries the light: its maker's
		CHECK(Anima::AbilityClaim(true, stranger, none) == std::optional<std::size_t>(Anima::kOtherUnnamed));  // first file skyrim.esm: not named
		CHECK(Anima::AbilityClaim(true, {}, none) == std::optional<std::size_t>(Anima::kOtherUnnamed));
		CHECK(!Anima::AbilityClaim(false, {}, lit));

		// the same route in the other two: each plugin named by its own file, its download without lights claiming nothing
		const auto enhanced = Anima::NamedPluginMod("enhancedatronachs.esp");
		const auto illuminated = Anima::NamedPluginMod("lm_astronach.esp");
		CHECK(enhanced && Anima::kOtherMods[*enhanced].key == "TheirsEnhanced");
		CHECK(illuminated && Anima::kOtherMods[*illuminated].key == "TheirsIlluminatedAtronachs");
		CHECK(!Anima::NamedPluginMod("js emissive eyes se - johnskyrim.esp"));  // it takes over the eyes: no switch, no entry
		if (enhanced && illuminated) {
			Anima::Lighters withLights{};
			withLights[*enhanced] = true;
			const std::vector<std::string> effect{ "skyrim.esm", "enhancedatronachs.esp" };
			CHECK(Anima::AbilityClaim(false, effect, withLights) == enhanced);
			CHECK(!Anima::AbilityClaim(false, effect, none));
			CHECK(!Anima::AbilityClaim(false, std::vector<std::string>{ "skyrim.esm", "lm_astronach.esp" }, withLights));  // not that mod's light
		}
		// a row named only by the skin it wears (two looks of one body model) is a creature, not a block left out
		std::istringstream byskin("[creature]\nname=Elytra\nskin=0xA98~ccbgssse025-advdsgs.esm\ncolor=255,149,51\n");
		std::size_t        s6 = 0;
		const auto         skins = Anima::ReadCreatures(byskin, s6);
		CHECK(skins.size() == 1 && s6 == 0);
		CHECK(skins.size() == 1 && skins[0].skins.size() == 1 && skins[0].skins[0] == "0xA98~ccbgssse025-advdsgs.esm");

		Anima::Settings s;
		CHECK(!Anima::GivenBack(none, s));  // nobody lights it
		CHECK(!Anima::GivenBack(lit, s));   // ours is the default
		s.theirs[*luminous] = true;
		CHECK(Anima::GivenBack(lit, s));  // its switch gives the creature back
		CHECK(!Anima::GivenBack(none, s));
		lit[Anima::kOtherUnnamed] = true;
		CHECK(Anima::GivenBack(lit, s));  // a second mod lights it too and its switch is off: still given to the first
		s.theirs[*luminous] = false;
		CHECK(!Anima::GivenBack(lit, s));

		CHECK(Anima::TheirLightOut(true, false));    // ours shows, its switch off: put out
		CHECK(!Anima::TheirLightOut(true, true));    // its switch on: its own
		CHECK(!Anima::TheirLightOut(false, false));  // ours does not show there (ice, a kind off): theirs stays
	}

	// which rows go on Illuminated's or RELight's picker, and the line each saves under
	void Picker()
	{
		CHECK(Anima::OnThePicker(Anima::Kind::kGlow) && Anima::OnThePicker(Anima::Kind::kEyes) && Anima::OnThePicker(Anima::Kind::kMachines));
		CHECK(!Anima::OnThePicker(Anima::Kind::kSpell) && !Anima::OnThePicker(Anima::Kind::kBurst));
		CHECK(Anima::PickerKey("Flame Atronach") == "Creature|Flame Atronach");
		CHECK(Anima::PickerKey("Flame Atronach").find('=') == std::string::npos);
	}

	// glowing eyes and machines are two switches, and JS Emissive Eyes takes only the eyes
	void EyesAndMachines()
	{
		std::istringstream in("[creature]\nrace=A\nkind=eyes\n[creature]\nrace=B\nkind=machines\n");
		std::size_t        skipped = 0;
		const auto         rows = Anima::ReadCreatures(in, skipped);
		CHECK(rows.size() == 2 && skipped == 0);
		if (rows.size() != 2) {
			return;
		}
		CHECK(rows[0].kind == Anima::Kind::kEyes && rows[1].kind == Anima::Kind::kMachines);
		Anima::Settings s;
		CHECK(Anima::Shown(rows[0], s) && Anima::Shown(rows[1], s));  // both on by default
		s.kinds[static_cast<std::size_t>(Anima::Kind::kMachines)] = false;
		CHECK(Anima::Shown(rows[0], s) && !Anima::Shown(rows[1], s));
		s = {};
		s.kinds[static_cast<std::size_t>(Anima::Kind::kEyes)] = false;
		CHECK(!Anima::Shown(rows[0], s) && Anima::Shown(rows[1], s));

		std::ostringstream out;
		Anima::WriteSettings(s, out);
		std::istringstream back(out.str());
		Anima::Settings    r;
		Anima::ReadSettings(back, r);
		CHECK(!r.kinds[static_cast<std::size_t>(Anima::Kind::kEyes)] && r.kinds[static_cast<std::size_t>(Anima::Kind::kMachines)]);

		CHECK(Anima::TakenByEyesMod(Anima::Kind::kEyes, true));
		CHECK(!Anima::TakenByEyesMod(Anima::Kind::kMachines, true));  // its dwarven glow does not turn our machine lights off
		CHECK(!Anima::TakenByEyesMod(Anima::Kind::kGlow, true));
		CHECK(!Anima::TakenByEyesMod(Anima::Kind::kEyes, false));
	}

	// a Light Placer light's name and the word that hides it
	void TheirLights()
	{
		CHECK(Anima::LightPlacerFile("LP_Light[iLluminousElementals\\Wisp|MagicLightWhite01]#0") == std::optional<std::string>("illuminouselementals\\wisp"));
		CHECK(Anima::LightPlacerFile("LP_Light[Dareniis Creatures/AtronachArcane|L](3)#2") == std::optional<std::string>("dareniis creatures\\atronacharcane"));
		CHECK(!Anima::LightPlacerFile("IlluminatedCreatureLightingNGLight"));
		CHECK(!Anima::LightPlacerFile("LP_Node[x|1,2,3]#0"));
		CHECK(!Anima::LightPlacerFile("LP_Light[no bar]"));
		CHECK(!Anima::LightPlacerFile("LP_Light[|x]"));
		CHECK(!Anima::LightPlacerFile(""));
		CHECK(Anima::LightPlacerKey("Atronachs/Flame Atronach") == "atronachs\\flame atronach");

		constexpr std::uint32_t flags = 0x00000042u;
		const auto              out = Anima::CullWord(flags, true);
		CHECK((out & Anima::kCullMask) != 0 && (out & Anima::kCullScript) != 0 && (out & 0xFFu) == 0x42u);
		CHECK(Anima::CullWord(out, true) == out);       // out twice is out once
		CHECK(Anima::CullWord(out, false) == flags);    // back exactly as it was
		CHECK(Anima::CullWord(flags, false) == flags);  // nothing of ours to let go
		const std::uint32_t scripted = flags | Anima::kCullScript;
		CHECK(Anima::CullWord(scripted, true) == scripted);   // a script hid it by itself: no mark of ours
		CHECK(Anima::CullWord(scripted, false) == scripted);  // and it stays hidden
		const std::uint32_t conditions = flags | (1u << 24);  // hidden by its conditions too
		CHECK(Anima::CullWord(Anima::CullWord(conditions, true), false) == conditions);
	}

	void OverTime()
	{
		CHECK(Near(Anima::DeathShare(0.0f, true, false), 1.0f));
		CHECK(Near(Anima::DeathShare(Anima::kDeathFadeSeconds, true, false), 0.0f));
		CHECK(Near(Anima::DeathShare(100.0f, true, true), Anima::kEmberShare));
		CHECK(Near(Anima::DeathShare(0.0f, false, false), 0.0f));
		CHECK(Near(Anima::DeathShare(0.0f, false, true), Anima::kEmberShare));
		const float half = Anima::DeathShare(Anima::kDeathFadeSeconds / 2.0f, true, false);
		CHECK(half > 0.4f && half < 0.6f);
		float last = 2.0f;
		bool  falling = true;
		for (int i = 0; i <= 40; ++i) {
			const float v = Anima::DeathShare(Anima::kDeathFadeSeconds * static_cast<float>(i) / 40.0f, true, true);
			falling = falling && v <= last + 1e-6f;
			last = v;
		}
		CHECK(falling);
		CHECK(Near(Anima::SurgeLift(-1.0f), 1.0f));
		CHECK(Near(Anima::SurgeLift(Anima::kSurgeSeconds), 1.0f));
		CHECK(Near(Anima::SurgeLift(Anima::kSurgeSeconds * 0.15f), 1.0f + Anima::kSurgeLift));
		CHECK(Anima::SurgeLift(Anima::kSurgeSeconds * 0.5f) > 1.0f);
	}

	// who gets a light within the cap: a corpse with nothing left to show must not keep a living creature dark
	void TheCap()
	{
		using Chosen = std::vector<std::size_t>;
		CHECK(Anima::ChooseLit({}, 5).empty());
		CHECK((Anima::ChooseLit({ { false, 300.0f, 0 }, { false, 100.0f, 1 }, { false, 200.0f, 2 } }, 2) == Chosen{ 1, 2 }));  // the nearest
		CHECK((Anima::ChooseLit({ { true, 10.0f, 0 }, { false, 900.0f, 1 } }, 1) == Chosen{ 1 }));                             // the living first
		CHECK((Anima::ChooseLit({ { true, 10.0f, 0 }, { false, 900.0f, 1 } }, 2) == Chosen{ 1, 0 }));                          // an ember while there is room
		CHECK((Anima::ChooseLit({ { false, 10.0f, 0, true }, { false, 900.0f, 1, false } }, 1) == Chosen{ 1 }));               // a glow before a pair of eyes
		CHECK((Anima::ChooseLit({ { false, 10.0f, 0, true }, { true, 1.0f, 1, false } }, 1) == Chosen{ 0 }));                  // but the living first
		CHECK((Anima::ChooseLit({ { false, 1.0f, 0 } }, 0).empty()));                                                          // no room at all
		CHECK(Anima::KeepsLights(true, true, false, false));                                                                   // on the body it has
		CHECK(!Anima::KeepsLights(true, false, false, true));                                                                  // the game stored another body
		CHECK(!Anima::KeepsLights(true, true, true, false));                                                                   // its light has run out
		CHECK(Anima::KeepsLights(false, false, false, true));                                                                  // its body is still on its way from the loader thread
		CHECK(!Anima::KeepsLights(false, false, false, false));                                                                // its body has gone
		// a body's own row before its race's: a lich on the dragon priest race wears its own light, not the priest's eyes beside it
		CHECK((Anima::RowsFor({}, { 1, 2 }) == Chosen{ 1, 2 }));
		CHECK((Anima::RowsFor({ 5 }, { 1, 2 }) == Chosen{ 5 }));
		CHECK(Anima::RowsFor({}, {}).empty());
	}

	// the keys the game plays on a light's dimmer: no code of ours runs while they do, so each set has to be right alone
	void Keys()
	{
		const auto top = [](const std::vector<Anima::Key>& a_keys) {
			float most = 0.0f;
			for (const auto& k : a_keys) {
				most = (std::max)(most, k.value);
			}
			return most;
		};
		const auto steady = Anima::FadeKeys(2.0f, Pulse::Kind::kNone, 7u, Anima::kLoopSeconds, false);
		CHECK(steady.size() == 361);
		CHECK(Near(steady.front().time, 0.0f) && Near(steady.back().time, Anima::kLoopSeconds));
		CHECK(std::ranges::all_of(steady, [](const Anima::Key& k) { return Near(k.value, 2.0f); }));

		const auto fire = Anima::FadeKeys(2.0f, Pulse::Kind::kFire, 7u, Anima::kLoopSeconds, false);
		CHECK(Near(fire.front().value, fire.back().value));  // a loop never jumps at its seam
		CHECK(top(fire) > 2.0f && top(fire) <= 4.0f);
		CHECK(std::ranges::any_of(fire, [](const Anima::Key& k) { return k.value < 1.9f; }));  // it moves
		bool rising = true;
		for (std::size_t i = 1; i < fire.size(); ++i) {
			rising = rising && fire[i].time > fire[i - 1].time;  // the game plays keys in time order
		}
		CHECK(rising);
		const auto other = Anima::FadeKeys(2.0f, Pulse::Kind::kFire, 8u, Anima::kLoopSeconds, false);
		CHECK(!Near(fire[100].value, other[100].value) || !Near(fire[200].value, other[200].value));  // two creatures do not move together
		// every shape, its period dividing the loop or not: no jump at the seam, every key a number inside the flicker's range
		for (std::size_t k = 0; k < Pulse::kNames.size(); ++k) {
			const auto loop = Anima::FadeKeys(2.0f, static_cast<Pulse::Kind>(k), 7u, Anima::kLoopSeconds, false);
			CHECK(loop.size() == 361 && Near(loop.front().value, loop.back().value));
			CHECK(std::ranges::all_of(loop, [](const Anima::Key& a_k) { return std::isfinite(a_k.value) && a_k.value >= 0.1f - 0.001f && a_k.value <= 4.0f + 0.001f; }));
		}

		const auto surge = Anima::FadeKeys(1.0f, Pulse::Kind::kNone, 7u, Anima::kSurgeSeconds, true);
		CHECK(surge.size() == 19);
		CHECK(Near(surge.front().value, 1.0f) && Near(surge.back().value, 1.0f));  // from its fade, back to its fade
		CHECK(top(surge) > 1.0f + Anima::kSurgeLift * 0.9f && top(surge) <= 1.0f + Anima::kSurgeLift + 0.001f);

		const auto cast = Anima::FadeKeys(1.0f, Pulse::Kind::kFire, 7u, Anima::kCastSeconds, true);
		const auto calm = Anima::FadeKeys(1.0f, Pulse::Kind::kFire, 7u, Anima::kCastSeconds, false);
		CHECK(Near(cast.front().value, cast.back().value));
		CHECK(cast[3].value > calm[3].value * 1.4f);  // the surge is on top of the flicker
		CHECK(Near(cast[60].value, calm[60].value));  // and over by two seconds: only the flicker

		const auto ember = Anima::DeathKeys(1.5f, 0.25f, true);
		CHECK(Near(ember.front().value, 1.5f) && Near(ember.back().value, 0.25f));
		CHECK(Near(ember.back().time, Anima::kDeathFadeSeconds));
		bool falling = true;
		for (std::size_t i = 1; i < ember.size(); ++i) {
			falling = falling && ember[i].value <= ember[i - 1].value + 1e-5f;
		}
		CHECK(falling);
		const auto at2 = ember[20].value;
		CHECK(at2 > 0.5f && at2 < 1.3f);
		const auto out = Anima::DeathKeys(1.5f, 0.0f, true);
		CHECK(Near(out.back().value, 0.0f));
		const auto once = Anima::DeathKeys(1.5f, 0.25f, false);  // the fade switched off: straight to the end, the game needs two keys
		CHECK(once.size() == 2 && Near(once.front().value, 0.25f) && Near(once.back().value, 0.25f));
	}
}

int main()
{
	CreatureFiles();
	Paths();
	Elements();
	SettingsText();
	Showing();
	OtherMods();
	AbilityLights();
	TheirLights();
	EyesAndMachines();
	Picker();
	TheCap();
	OverTime();
	Keys();
	FloorLightChecks::Run(Check);
	RowParkChecks::Run(Check);
	std::printf("%d passed, %d failed\n", gPassed, gFailed);
	if (gPassed + gFailed < kFloor) {
		std::printf("FAIL: only %d checks ran, fewer than the floor %d\n", gPassed + gFailed, kFloor);
		return 1;
	}
	return gFailed ? 1 : 0;
}
