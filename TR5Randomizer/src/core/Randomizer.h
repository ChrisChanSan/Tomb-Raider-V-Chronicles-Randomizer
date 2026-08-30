/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once
#include "Floor.h"
#include "Json.h"
#include "Rng.h"

#include <map>
#include <set>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Placement logic.
//
// Ground rules, all earned from earlier findings of this project and not
// negotiable:
//
// 1. NO removing of items. TR item indices are pure array positions; a
//    removed item shifts all following ones up and the triggers' "Object N"
//    references then point at the wrong object. Randomizing therefore
//    means: REWRITE existing items at their index (itemEdits). itemAdds
//    exists only for mandatory injections like Andrea3's crowbar, and those
//    append at the end, shifting nothing.
//
// 2. A pickup trigger sticks to the sector, not to the item. If an item is
//    moved, its trigger stays behind and never fires again. Affected items
//    are DERIVED from the level data (see findTriggerPinnedItems), not
//    maintained by hand. Excepted are triggers whose only further effect is
//    PlaySoundtrack - a missing music cue is a deliberately accepted
//    sacrifice (user decision); anything else would interfere with
//    progress, geometry or enemies.
//    Moving the trigger along later is planned, but requires floordata
//    editing and is explicitly scheduled after the basic framework.
//
// 3. OCB is set to 0 when moving. A pickup with a pedestal OCB (3/4/7/8) or
//    a hiding OCB (1) needs a matching special environment at the target;
//    without it, the item QUIETLY CANNOT BE PICKED UP - no crash, no
//    message, it just lies there forever. Confirmed empirically in this
//    project (Andrea3, item 101). Only OCB 0 reliably survives a move.
//
// 4. Everything is seed-driven and order-stable: item and sector lists are
//    sorted before every random decision, so the same seed yields the same
//    result regardless of file or map ordering.
//----------------------------------------------------------------------------

// Pickup type ranges, read straight from game/setup.cpp: exactly these two
// contiguous ranges get PickUpCollision assigned there
// (PUZZLE_ITEM1..BURNING_TORCH_ITEM and PISTOLS_ITEM..FLARE_INV_ITEM).
// So "what even is a pickable item" is not guessed.
constexpr int PICKUP_LO_1 = 172, PICKUP_HI_1 = 241;
constexpr int PICKUP_LO_2 = 334, PICKUP_HI_2 = 355;

inline bool isPickupType(int typeId)
{
    return (typeId >= PICKUP_LO_1 && typeId <= PICKUP_HI_1)
        || (typeId >= PICKUP_LO_2 && typeId <= PICKUP_HI_2);
}

// Operable fixtures: everything Lara must stand in front of and play an
// animation at (#120). An enemy must not stand there - its hitbox pushes
// her away, the animation never starts, and a door hanging off it stays
// closed forever.
//
// The four ranges are read from the TR5 slot list, not guessed:
//   242-249  Puzzle Hole 1-8
//   258-265  Key Hole 1-8
//   266-283  Switch Type 1-8, Airlock, Underwater 1-2, Turn, Cog, Lever,
//            Jump, Crowbar, Pulley, Crowdove
//   329-331  Sequence Switch 1-3
//
// Puzzle Done (250-257) is deliberately NOT included: that is the finished
// state, nobody operates anything there anymore.
inline bool isOperableFixture(int typeId)
{
    return (typeId >= 242 && typeId <= 249)
        || (typeId >= 258 && typeId <= 283)
        || (typeId >= 329 && typeId <= 331);
}

// Swarm critters: bat, rat, spider and the spider generator. If a pickup
// trigger fires only one of these, it does NOT pin the item.
//
// The user's reasoning: these critters chase Lara for a few seconds and
// then despawn on their own. Losing them changes nothing about the flow of
// the game. A dog or a SWAT does not disappear - which is why Andrea1's key
// item stays in place, and that was exactly the requirement.
//
// Across all 16 levels this exception hits exactly one case: Andrea2's item
// 121 (Puzzle Item3), whose pickup spawns a rat. Everything else that
// pickup triggers fire in the game are doors, traps, control objects or
// real enemies.
inline bool isDespawningSwarm(int typeId)
{
    return typeId >= 93 && typeId <= 96;   // Bat, Rat, Spider, Spider Generator
}

// Carriers in the sense of GetCarriedItems (game/setup.cpp:1925): an item
// that at level start collects the pickups in its immediate vicinity. They
// vanish from the room (room_number = NO_ROOM) and only reappear when the
// enemy dies or the crate is searched.
//
// The engine queries objects[...].intelligent. That is a property setup.cpp
// assigns slot by slot, not a contiguous number range - hence exactly the
// 24 slots that receive it there are listed here. Added to that is the
// range SEARCH_OBJECT1..SEARCH_OBJECT3, which GetCarriedItems explicitly
// also checks: the searchable crates and lockers. Across the sixteen levels
// they make up the majority of all carriers.
//
// Explicitly NOT included, though one would suspect them: Scientist (69)
// and Guardian (65) get no intelligent flag and carry nothing.
inline bool isCarrierType(int typeId)
{
    switch (typeId)
    {
    case 31: case 33: case 37: case 39: case 41: case 43: case 45: case 47:
    case 51: case 57: case 59: case 61: case 63: case 67: case 71: case 73:
    case 75: case 77: case 79: case 81: case 83: case 85: case 87: case 89:
        return true;                              // intelligent = 1
    default:
        return typeId >= 164 && typeId <= 168;    // SEARCH_OBJECT1..3 incl. MIPs
    }
}

// The box from GetCarriedItems, taken over unchanged: same room, less than
// 512 units in X and Z, less than 256 in Y. Deliberately a position test in
// world coordinates and not a sector test - the engine computes it this
// way, and a sector test would be wrong at the edges.
inline bool isWithinCarryBox(int bx, int by, int bz, int px, int py, int pz)
{
    return (bx - px < 512) && (px - bx < 512)
        && (by - py < 256) && (py - by < 256)
        && (bz - pz < 512) && (pz - bz < 512);
}

// numX/numZ are the RAW measure from the room header - the wall ring around
// counts too, so (numX-2) * (numZ-2) tiles are walkable. Needed in
// kSpaceNeeds (Randomizer.cpp): there the room size decides which enemies
// may enter at all.
struct RoomOrigin { int room = 0; int originX = 0; int originZ = 0; int numX = 0; int numZ = 0; };

struct ZoneSector
{
    int room = 0, sectorX = 0, sectorZ = 0;
    int zone = 0;
    int floorY = 0;

    // Clearance between floor and ceiling (#61). Below 768 Lara cannot
    // stand - her collision height is 762 (game\lara.cpp) - and
    // PickUpCollision explicitly refuses the wall grab in the duck state.
    int clearance = 0;

    // Edges of this sector that have a wall, as angles POINTING AT the wall
    // (0 = +Z, 16384 = +X, -32768 = -Z, -16384 = -X). Empty means: no wall,
    // hence no place for an OCB-2/9 pickup.
    //
    // Why the angle points at the wall and not away from it: the mesh of
    // these models sits around +450..500 in model Z and thus lands on the
    // wall surface, while Lara steps up to +215 in front of it. Measured
    // against vanilla Andrea2 item 121.
    std::vector<int> wallAngles;

    // Sectors Lara steps on shortly BEFORE this one on her first-access
    // path, about three steps ahead, best choice first. From the zone
    // export; the first-access tree lives there, only the finished answer
    // arrives here.
    //
    // There is exactly one purpose for this: the trigger of a newly placed
    // enemy. It must not lie on the enemy's own sector - otherwise the
    // enemy spawns into Lara - but it must be crossed reliably, or the
    // enemy never wakes up.
    std::vector<long long> approach;
};

// Sector key: room, sector X and sector Z packed into one number. The zone
// exports name sectors as "room,x,z"; here that becomes a comparable value
// so the sets below can work with set<long long>.
inline long long secKey(int room, int sx, int sz)
{
    return (static_cast<long long>(room) << 32) | (static_cast<long long>(sx) << 16) | static_cast<long long>(sz);
}

struct KeyConstraint
{
    int              typeId = 0;
    int              maxZone = 0;
    std::vector<int> allowRooms;     // empty = no restriction
    std::vector<int> excludeRooms;

    // Height limit of an island group. Y CONVENTION: in TR, SMALLER y is
    // HIGHER. "At most 1024 above the baseline" is therefore
    // floorY >= baseline - 1024, and a sector BELOW the bound is too high -
    // typically the top of a crate stack that Lara can only reach with
    // exactly the key that is supposed to lie there.
    bool             hasMinFloorY = false;
    int              minFloorY = 0;
    // Where this bound applies. Empty means everywhere. It is a property of
    // the GROUP, not the zone: joby2's zone 0 has 7756 sectors, the island
    // 1900 - attached to the zone it acted on half the level.
    std::set<long long> minFloorYScope;

    std::set<long long> islandSectors;     // allow list; empty = none
    std::set<long long> bannedSectors;     // the inverse: not here right now
    std::set<long long> forbiddenSectors;  // behind its own door

    // At least ONE instance of this type must lie in this set. Unlike all
    // the fields above this is not a filter for a single instance but a
    // statement about all instances together - see its handling in run().
    std::set<long long> requireInIsland;
};

struct PlacementOffset
{
    int dx = 0;
    int dy = 0;
    int dz = 0;
};

struct MarkerCount
{
    int typeId = 0;
    int sectors = 0;
    int rooms = 0;
};

struct Injection
{
    int         typeId = 0;
    int         room = 0;
    // A selection instead of ONE room: the seed picks a free valid sector
    // from all listed rooms. Meant for requirements of the kind "this must
    // lie somewhere in this part of the level" - rich2's mandatory Deagle
    // in the start island is the case (user wish: weapon guaranteed, place
    // different every time). If the list is empty, "room" applies.
    std::vector<int> rooms;
    // Optional fixed sector. Without it, the injection picks any free valid
    // sector of the room - right for Andrea3's crowbar, which may lie
    // anywhere in room 120. rich2 needs the opposite: three items on
    // exactly one tile, because the user picked the spot. A fixed sector
    // therefore does NOT occupy it for the next injection either -
    // stacking is intended here.
    bool        hasSector = false;
    int         sectorX = 0;
    int         sectorZ = 0;
    // Height offset against the sector floor. y grows DOWNWARDS in TR, so a
    // negative value lifts the item. Needed as soon as several injections
    // stand on the same tile and should not intersect.
    int         yOffset = 0;
    std::string reason;
};

struct LevelItem
{
    int index = 0, typeId = 0, room = 0;
    int x = 0, y = 0, z = 0;
    int sectorX = 0, sectorZ = 0;
    int ocb = 0;
    // No angle: the level info export does not provide one, and the patch
    // omits "angle" - a moved item thus keeps its orientation.
};

//----------------------------------------------------------------------------
// An item whose pickup trigger travels along.
//
// Normally a pickup trigger pins its item (rule 2): the trigger sticks to
// the sector, the item would wander off without it and its effect - a door,
// an enemy - would stay behind at the old spot. The coupled move solves
// that by relocating both together: one operation, not two.
//
// This is not allowed in general, only where it was explicitly curated.
// Today exactly one case: Andrea1 item 107, the key whose trigger also
// wakes the dog and a flame emitter.
//----------------------------------------------------------------------------
struct CoupledTrigger
{
    int itemIndex = 0;
    int room = 0, sectorX = 0, sectorZ = 0;   // where the trigger lies today
};

//----------------------------------------------------------------------------
// A sound an enemy needs but ITS level does not carry.
//
// A level only carries the samples it needs itself. If an enemy does not
// appear in it in vanilla, they are missing - it then stands there mute.
// Where the replacement comes from is curation (ENEMY_SOUND_FIX in
// ZoneAnalysis.ps1) and arrives ready-made in the zone export; here it is
// only passed on.
//----------------------------------------------------------------------------
struct EnemySound
{
    int typeId = 0;
    int map = 0;                 // the missing SFX number in the target level
    std::string fromLevel;
    int fromMap = 0;
};

struct RandoInput
{
    std::string              level;
    int                      maxZone = 0;
    std::vector<ZoneSector>  sectors;        // already only validForItems + reached
    std::vector<KeyConstraint> constraints;
    std::vector<int>         fixedItems;     // from the zone export (hand curation)
    std::vector<Injection>   injections;
    // typeId -> offset against the sector center at floor height. Needed
    // when an object's mesh origin does not sit where the model assumes it.
    // The global/per-level resolution already happens in the export, only
    // the final value arrives here. See TYPE_OFFSET in ZoneAnalysis.ps1.
    std::map<int, PlacementOffset> typeOffset;
    // Types whose OCB survives a move - the exception to rule 3. Only for
    // objects where the OCB describes the pickup style and not the old
    // location. See KEEP_OCB in ZoneAnalysis.ps1.
    std::set<int>            keepOcbTypes;
    std::vector<LevelItem>   items;          // all items of the level
    std::vector<RoomOrigin>  roomOrigins;
    std::set<int>            triggerPinned;  // derived, see rule 2

    // Rooms with the water flag (waterRoom from the level export). User
    // requirement 2026-08-14: no enemies may be placed there - land enemies
    // can neither walk nor attack under water and would just stand there
    // helplessly. Affects the extra-enemy spots; swapped enemies stand at
    // Core's own positions and remain untouched.
    std::set<int>            waterRooms;

    // Individual types that must not lie in certain rooms. Unlike
    // noWeaponRooms (bars ALL weapons from a room) and unlike a
    // KeyConstraint's excludeRooms (which only steer the moving of existing
    // items), this ban applies on EVERY placement path - moving, weapon
    // scattering, item abundance - because it sits directly in
    // sectorAllowedFor.
    //
    // Only case so far: rich2's HK (345) must not enter the start island.
    // The X-ray scanner in room 9 reacts - checked in game - ONLY to it;
    // other weapons may lie there, otherwise the enemy-heavy level start
    // would have to be survived with pistols alone.
    std::map<int, std::set<int>> typeNoRooms;

    // The same one level finer: individual SECTORS per type (#61,
    // typeNoSectors in the export). Needed when a room mostly suits a type
    // and only a few tiles in it do not - and above all for types WITHOUT a
    // constraint: andy3's chalk and crowbar are tools without a pickup, the
    // key constraints' bannedSectors never reach them. Checked outside the
    // constraint branch like typeNoRooms, so it applies on every placement
    // path.
    std::map<int, std::set<long long>> typeNoSectors;

    // Items their antitrigger must no longer touch (flag bit 0x80).
    // Curated, independent of the enemy automatics: rich2's door 171 is to
    // stay open once it has been open.
    std::set<int>            antitriggerImmuneItems;

    // Items an AntiTrigger or HeavyAntiTrigger points at. If such a trigger
    // points at an ACTIVE intelligent enemy, it deletes it from the level
    // without dying and without fading (hit_points = -16384,
    // DisableBaddieAI, KillItem - control.cpp:2422). In vanilla that is
    // intentional, because the enemy is supposed to be done there; a
    // swapped one may still be fighting and vanished mid-fight (seen in
    // game: Andrea1 item 34 under allsanity, antitrigger 46 in room 88).
    //
    // Such items now swap along and receive Core's own antitrigger immunity
    // (IFL_ANTITRIGGER_ONESHOT) in their flag word - the antitrigger then
    // bounces off and the enemy dies normally. This acts per ITEM: Andy2's
    // antitrigger on items 150-154 stays in force for the unswapped
    // siblings. Only five enemies in the whole game are affected at all
    // (Andrea1 34, Andy2 151, rich1 180, rich2 26 and 111 - measured), and
    // Andrea1 34 already drops out through the OCB rule.
    std::set<int>            antitriggered;

    // Model slots the level carries - read from the file by the level check
    // in ApplyPatch (markTriggerSafeSectors). Item abundance only scatters
    // types whose model is present; a pickup without a model would be an
    // invisible stumbling block. For the weapon set, whatever the import
    // brings in comes on top - that is not in this list then.
    std::set<int>            presentSlots;

    // Types this level must NOT carry. Filled by the level check in
    // ApplyPatch (markTriggerSafeSectors): Andrea2 carries in slot 47 only
    // Pierre's CUTSCENE variant (death anim +12 has state 9 and jumps into
    // a living anim - made for the Trajan finale scene, never for
    // PierreControl; the level has no type-47 items either). Because the
    // slot "exists", neither cross would import it nor would the pool avoid
    // it - a Pierre placed there freezes and dies in an endless loop (seen
    // in game). Replacing is out of the question: the finale cutscene needs
    // exactly this set.
    std::set<int>            forbiddenTypes;

    // Rooms in which no weapon may lie, and what counts as a weapon.
    // joby3: a shotgun that randomly wandered into the chef area triggers
    // the same glitch as displaced pistols. That is not a property of a key
    // type but of the level, and therefore also affects ordinary pickups.
    std::set<int>            noWeaponRooms;
    std::set<int>            weaponTypes;

    // Items whose trigger travels along. They may move despite
    // triggerPinned - precisely because their trigger does not stay behind.
    std::vector<CoupledTrigger> coupledTriggers;

    // Enemy types that may be added to this level. Hand curation from the
    // zone export - empty means: none. An enemy only belongs here once it
    // has proven in game that it works without a level script.
    std::vector<int>         enemyTypes;

    // Samples that some of these types will not find in the level.
    std::vector<EnemySound>  enemySounds;

    // Sectors that can accept a trigger, as secKey. Evaluated as soon as
    // coupledTriggers or enemyTypes is non-empty.
    //
    // This set comes from the floordata of the real level, not from the
    // level info export: whether a sector tolerates a trigger depends on
    // entry kinds (lava, climb, monkey, TrigTrigger) of which the export
    // knows only two. Guessing here would be silent level damage - see
    // Tr5Level::canAppendTrigger.
    std::set<long long>      triggerSafeSectors;

    // Sectors behind a deadstop (NO_KEY_ISLAND in the zone export as
    // keyBarredSectors). No key item may lie there.
    //
    // Why a separate set and not via the zone border: the border hangs off
    // a key constraint, and those only exist for types with a pickup.
    // andy3's chalk and crowbar are tools without a pickup - ZoneAnalysis
    // derives no border for them at all, so the deadstop never reached
    // them (#61).
    std::set<long long>      keyBarredSectors;

    // Types EXEMPT from keyBarredSectors (#61, keyBarredExemptTypes in the
    // export). Needed as soon as an island is meant FOR certain types and
    // another type needs the opposite: andy3's island keeps chalk and
    // crowbar on the start island and bars everything else from it - but
    // the coin of all things must NOT go there. Without the exemption both
    // rules applied, and a single room was left over.
    std::set<int>            keyBarredExemptTypes;

    // Items whose code bits are preset (#61). The third way besides "leave
    // it be" and "take the trigger along": a HeavyTrigger cannot be taken
    // along, so the curation relieves the item of its condition. Whoever is
    // listed here is thereby also freed from trigger pinning.
    std::map<int, int>       flagPresets;   // item index -> flags

    // Slope entries of all placement sectors (only sectors WITH an entry;
    // whoever is missing is flat). Read from the floordata of the REAL
    // level (markTriggerSafeSectors) - the zone export knows only one
    // floorY per sector and cannot tell flat and sloped apart.
    //
    // Needed for two things, both in floorPosAt (Floor.h):
    //  1. The ITEM y. An item is drawn by the engine at exactly its y; on a
    //     slope the sector's floorY is the highest corner - without the
    //     conversion the item hangs in the air (seen in game, #86).
    //     Walkable slopes (up to slope 2) are fully valid item spots, Core
    //     itself places pickups there - a blanket ban cost andy1-3 almost
    //     all sectors (user find).
    //  2. Barring the true exceptions: slides (slope > 2, Lara cannot pick
    //     anything up there) and the collision-free triangle of the NOCOLF
    //     sectors (there is no floor there).
    // Enemies do not strictly need either - the engine puts them on the
    // floor every frame via GetHeight - but they get their y computed
    // anyway, so they do not drop in half a sector deep.
    std::map<long long, SlopeInfo> slopes;
};

struct RandoOptions
{
    bool shuffleKeyItems = true;
    bool shuffleOtherPickups = true;

    // Add extra enemies to the level. How many: 0 means none.
    // The types come from RandoInput::enemyTypes, not from here.
    bool addEnemies = false;
    int  enemiesPerLevel = 6;

    // Swap existing enemies among each other. Only the type changes, the
    // item index stays - so all the triggers' "Object N" references remain
    // valid and every enemy still wakes up where it was meant to.
    //
    // Swapping happens exclusively among the level's curated types
    // (RandoInput::enemyTypes); hand-curated items stay excluded.
    bool swapEnemies = false;

    // Cross enemy mix: the pool for swapping and extra enemies is extended
    // with types from OTHER levels (global pool table in Randomizer.cpp,
    // from the user's list "Enemy Pools.txt"). At most three foreign types
    // per level - each costs texture pages and sample memory in the
    // target. Models and sounds travel along during patching via the
    // native transfer (moveableAdds/soundAdds); whatever the level already
    // knows is skipped. Only effective together with swapEnemies or
    // addEnemies - alone the checkbox changes nothing.
    bool crossEnemies = false;

    // Allsanity: like crossEnemies but WITHOUT the three-type limit - the
    // whole pool is available to the level, and when swapping, every role
    // rolls freely from the full pool instead of only every fourth.
    // Maximum variety (user wish). Still only what the run actually uses
    // is imported - so the texture page budget stays in bounds (measured:
    // 3 foreign types +15..25 pages of 256; even the full pool stays well
    // below the limit together with weapons and outfit).
    bool allsanity = false;

    // Enemy types deselected by the user (window "Enemy types", #95).
    // The DESELECTION is what gets saved, not the selection: that way any
    // type added later is automatically on, without old settings files
    // silently excluding it.
    //
    // The filter trims EXCLUSIVELY the choice from the pool. Whatever a
    // chosen row pulls in technically never sees it: the anim base type of
    // the soldier family (basisSlot), mandatory companions like the
    // Hydra's projectile core (begleiter) and the MESHSWAP halves
    // (swapVon/swapDst). Otherwise an enemy would stand in the level
    // without its animation set.
    std::set<int> disabledEnemies;

    // Third-party installations for the guests from Tomb Raider 3 (#92,
    // #102).
    //
    // We ship no game data - the run fetches model, textures and sounds at
    // runtime from HERE. If a path is missing, the corresponding types
    // silently drop out of the pool. The run must NEVER fail because
    // someone does not own the games.
    std::wstring tr3Dir;
    std::wstring tr3GoldDir;

    // Every level randomly gets one of the three outfits: Lara_Andrea
    // (donor Andrea1), Lara_Joby (joby2) or Lara_Rich (rich2, catsuit).
    // Slots 8/9/10 are transferred (LARA_SKIN, LARA_SKIN_JOINTS,
    // LARA_SCREAM) plus the donor's LaraType byte - NEVER slot 0, which
    // carries the level's animations. joby4 is exempt (diving suit, stays
    // vanilla anyway); the andy levels always get a replacement, their own
    // outfit is the young Lara.
    bool randomOutfits = false;

    // The wardrobe: which outfits the roll may NOT draw.
    //
    // Held is the DESELECTION, as with disabledEnemies and for the same
    // reason: an outfit added later is then automatically on and does not
    // fall silently through an old settings file.
    //
    // Ids rather than indices ("andrea", "joby", "rich" plus the ids from
    // Tr3Source::outfits()), so a new outfit appearing in the middle of the
    // list cannot shift what an existing config had chosen.
    std::set<std::string> outfitsOff;

    // Guest outfits from Tomb Raider 3 may be drawn at all. Separate from the
    // pool because it also decides whether the user's TR3 installation is
    // read during a run.
    bool tr3Outfits = false;

    // Every level gets the full weapon set (pistols, Uzi, shotgun, Desert
    // Eagle, HK including ammo and inventory slots). The donor rules come
    // from the weapon work and are built and checked in all 13 levels
    // one by one: Andrea3 fills only what is
    // missing; joby2 always replaces the revolver with the Desert Eagle
    // (user requirement); rich1 supplies the HK except to rich2, which
    // keeps its own special pose. joby4 stays vanilla (user decision).
    bool allWeapons = false;

    // Item abundance: scatter this many extra consumables per level
    // (0 = off). Only what the level already knows is placed - ammo,
    // medipacks, flares -, weighted by frequency; details at the scatter
    // block in Randomizer.cpp. Sparse levels are the reason: Andy2 has ten
    // pickups across 254 rooms with 348 valid placement tiles.
    //
    // There is no reverse direction ("fewer than vanilla"): removing items
    // would shift all subsequent indices and break every "Object N"
    // reference of the triggers.
    int  extraPickups = 0;
};

struct RandoReport
{
    int movedKeyItems = 0;
    int movedOtherItems = 0;
    int pinnedItems = 0;
    int injected = 0;
    int enemiesAdded = 0;
    int enemiesSwapped = 0;
    int weaponsScattered = 0;           // scattered weapon/ammo items
    int extraPickups = 0;               // extra equipment (abundance)
    int crossTypesUsed = 0;             // foreign enemy types in use
    std::vector<std::string> notes;     // findings for the UI
    bool ok = true;
    std::string error;
};

// One selectable enemy kind in the pool, for the "Enemy types" window (#95).
//
// "source" says which game the kind comes from (#102):
//   nullptr    lives in Tomb Raider 5 itself
//   "tr3"      needs a Tomb Raider 3 installation
//   "tr3gold"  needs The Lost Artifact
//
// We ship no game data. Without the matching path the kind stays grayed out
// in the UI instead of failing during the run.
struct EnemyKind { int typeId; const char* name; const char* source; };

// One entry of the wardrobe (#122). source is "" for the three outfits TR5
// brings itself and the installation key ("tr3") for a guest - the window
// greys out a guest whose game is not there, exactly as it does with the
// animals.
struct OutfitKind { const char* id; const char* name; const char* source; };

namespace Rando
{
    // The selectable enemy kinds with display names, in table order.
    //
    // The list lives next to the pool table kCross in Randomizer.cpp and
    // must grow with it - whoever adds a row there adds one here. A kind
    // missing here cannot be deselected; one that exists only here has a
    // checkbox without effect.
    std::vector<EnemyKind> enemyKinds();

    // Every outfit that can be drawn, in the order the user sees. ONE list
    // for the window and for the roll - the enemy list had to be kept in
    // step with its pool table by hand, and this one is built from the same
    // sources the roll reads, so it cannot fall out of step at all.
    std::vector<OutfitKind> outfitKinds();

    // Reads zone export and level info into one input set and derives the
    // trigger-pinned items along the way.
    bool buildInput(const JsonValue& zoneExport,
                    const JsonValue& levelInfo,
                    RandoInput& out,
                    std::string& error);

    // Rule 2: which items sit on a pickup trigger whose effect goes beyond
    // PlaySoundtrack? Public so the self-test and later the UI can show the
    // same list.
    std::set<int> findTriggerPinnedItems(const JsonValue& levelInfo,
                                         std::vector<std::string>* explain = nullptr);

    // Produces the patch. The seed determines the result completely.
    bool run(const RandoInput& in,
             const RandoOptions& opt,
             uint64_t seed,
             JsonValue& patchOut,
             RandoReport& report);

    // May this type lie on this sector? The one place where all constraints
    // of an instance are checked - used by the placement AND by the debug
    // tool. Deliberately shared: a second copy of this filter would drift
    // apart sooner or later, and then the debug picture would show
    // something different from what the randomizer does.
    //
    // `mustBeOnIsland` concerns only requireInIsland - that is a statement
    // about all instances together and therefore applies exactly to the one
    // the run forces in.
    // itemOcb: the OCB of the item to be placed. Only 2 and 9 change
    // anything - those are the wall grabs (crowbar and joby crow), and they
    // need a wall edge and standing height (#61). 0 for everything else and
    // for paths without a concrete item (injections).
    bool sectorAllowedFor(const RandoInput& in,
                          const KeyConstraint* k,
                          const ZoneSector& s,
                          int typeId,
                          bool mustBeOnIsland,
                          int itemOcb = 0);

    // Floor height and walkability at a point of the sector (localX/localZ
    // in 0..1023, usually 512 + type offset). yOut is ALWAYS filled - even
    // on a slide the height is right, only nobody may stand there.
    // Sectors without a slope entry are flat: yOut = floorY, true.
    bool floorAt(const RandoInput& in, const ZoneSector& s,
                 int localX, int localZ, int& yOut);

    // Debug tool: puts markers on sectors instead of moving items.
    //
    // Two layers on top of each other. `fillType` goes onto EVERY valid
    // sector - that is the area where anything may lie at all. Above it,
    // per constraint type, its own narrower area. Where both lie, both item
    // kinds are allowed; where only the fill type lies, exclusively
    // ordinary equipment.
    //
    // fillType 0 omits the fill layer. The result is a patch with itemAdds
    // - only for trview, because with more than 256 items the engine no
    // longer loads the level (MAX_ITEMS, hard abort in specific/file.cpp).
    bool buildMarkerPatch(const RandoInput& in,
                          int fillType,
                          JsonValue& patchOut,
                          std::vector<MarkerCount>& perType,
                          int& fillSectors);
}
