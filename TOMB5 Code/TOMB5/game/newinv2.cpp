/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "../tomb5/pch.h"
#include "newinv2.h"
#include "secretreward.h"
#include "gameflow.h"
#include "effects.h"
#include "../specific/d3dmatrix.h"
#include "sound.h"
#include "objects.h"
#include "lara1gun.h"
#include "lara2gun.h"
#include "camera.h"
#include "control.h"
#include "lara.h"
#include "larafire.h"
#include "lara_states.h"
#include "../specific/LoadSave.h"
#include "../specific/input.h"
#include "../specific/output.h"
#include "health.h"
#include "../specific/3dmath.h"
#include "draw.h"
#include "subsuit.h"
#include "../specific/specificfx.h"
#include "../specific/polyinsert.h"
#include "text.h"
#include "../specific/audio.h"
#include "../specific/gamemain.h"
#include "../specific/file.h"
#include "spotcam.h"
#include "../specific/dxshell.h"
#include "savegame.h"
#include "../specific/hd/hd_object.h"	// draws the ring's items through the HD path
#include "inventory_interp.h"		// the ring's frame-rate interpolation
#include "laramisc.h"			// CheatLog - shares the seam with the hotkey cheats
#include "../tomb5/tomb5.h"			// tomb5.action_split, for the ring's confirm key
#include "interpolate.h"		// InterpIsTweenFrame, the tween-frame latch

static RINGME* rings[2];
static RINGME pcring1;
static RINGME pcring2;
static AMMOLIST ammo_object_list[3];
static MENUTHANG current_options[3];
static uchar go_left;
static uchar go_right;
static uchar go_up;
static uchar go_down;
static uchar go_select;
static uchar go_deselect;
static uchar left_repeat;
static uchar left_debounce;
static uchar right_repeat;
static uchar right_debounce;
static uchar up_debounce;
static uchar down_debounce;
static uchar select_debounce;
static uchar deselect_debounce;
static uchar friggrimmer;
static uchar friggrimmer2;
static char loading_or_saving;
static char use_the_bitch;
static short examine_mode = 0;
static short stats_mode = 0;
static uchar current_selected_option;
static char menu_active;
static char ammo_active;
static char oldLaraBusy;
static long xoffset;
static long yoffset;
static long OBJLIST_SPACING;
static long pcbright = 0x7F7F7F;
static short combine_obj1;
static short combine_obj2;
static short ammo_selector_fade_val;
static short ammo_selector_fade_dir;
static short combine_ring_fade_val;
static short combine_ring_fade_dir;
static short normal_ring_fade_val;
static short normal_ring_fade_dir;
static char ammo_selector_flag;
static char combine_type_flag;
static char seperate_type_flag;

static char* current_ammo_type;
static short AmountShotGunAmmo1 = 0;
static short AmountShotGunAmmo2 = 0;
static short AmountCrossBowAmmo1 = 0;
static short AmountCrossBowAmmo2 = 0;
static short AmountHKAmmo1 = 0;
static short AmountUziAmmo = 0;
static short AmountRevolverAmmo = 0;
static short AmountPistolsAmmo = 0;
static char CurrentPistolsAmmoType = 0;
static char CurrentUziAmmoType = 0;
static char CurrentRevolverAmmoType = 0;
static char CurrentShotGunAmmoType = 0;
static char CurrentGrenadeGunAmmoType = 0;
static char CurrentCrossBowAmmoType = 0;
static char StashedCurrentPistolsAmmoType = 0;
static char StashedCurrentUziAmmoType = 0;
static char StashedCurrentRevolverAmmoType = 0;
static char StashedCurrentShotGunAmmoType = 0;
static char StashedCurrentGrenadeGunAmmoType = 0;
static char StashedCurrentCrossBowAmmoType = 0;
static char Stashedcurrent_selected_option = 0;
static char num_ammo_slots;

static uchar keypadx = 0;
static uchar keypady = 0;
static uchar keypadnuminputs = 0;
static uchar keypadpause = 0;
static uchar keypadinputs[4];

long GLOBAL_enterinventory = NO_ITEM;
long GLOBAL_inventoryitemchosen = NO_ITEM;
long GLOBAL_lastinvitem = NO_ITEM;
long GLOBAL_invkeypadmode = 0;
long GLOBAL_invkeypadcombination = 0;
long InventoryActive = 0;

static short optmessages[11] =
{
	TXT_USE, TXT_CHOOSE_AMMO, TXT_COMBINE, TXT_SEPERATE, TXT_EQUIP, TXT_COMBINE_WITH,
	TXT_LOAD_GAME, TXT_SAVE_GAME, TXT_EXAMINE, TXT_Statistics, TXT_CHOOSE_WEAPON_MODE
};

static uchar wanky_secrets_table[18] = { 0, 3, 3, 3, 3, 3, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };

// How many secrets THIS level holds. The table stays private; the statistics
// screen needs the number and has no business knowing where it comes from.
// Indexed by gfLevelId, not by the slot - with a rolled level order the two
// are different numbers, and it is the level's identity that decides how
// many roses it was built with.
long LevelSecretTotal()
{
	return gfLevelId < 18 ? wanky_secrets_table[gfLevelId] : 0;
}

//----------------------------------------------------------------------------
// Secret bonus (TR2-style): collecting every secret in a level awards items.
//
// TR2 did this via the script - GF_SecretInvItems[] filled per level, granted
// by GF_ModifyInventory(level, TRUE) which both adds the item to the inventory
// and calls AddDisplayPickup() so it shows on screen (see TR2Main's
// game/gameflow.cpp:418). TOMB5 has no such script field, so the reward is
// defined here instead. Everything else it needs already exists in TOMB5:
// the per-level secret counter (savegame.Level.Secrets), the per-level total
// (wanky_secrets_table above), the item-granting path (DEL_picked_up_object)
// and the on-screen pickup display (AddDisplayPickup, game/health.cpp).
//
// wanky_secrets_table was verified against the actual level data before being
// relied on here: counting PICKUP_ITEM4 (object id 223) placements in the
// vanilla .trc files matches this table for all 13 PLAYABLE levels, including
// the two irregular ones (Deepsea Dive = 1, Sinking Submarine = 2).
//
// Two entries disagree, and both are Core's error, not ours: LVL5_DEL_LEVEL
// (a dev/test level) and the cutscene level richcut2 hold no rose at all yet
// stand at 3. For the bonus that is harmless - the counter can never reach
// the value, so it simply never fires there. Do NOT 'fix' the table to 0:
// the test below is an equality test, and 0 == 0 would hand out the bonus
// the moment such a level starts. The only visible effect is that the
// statistics line reads (0 / 3) in those two, which is the lesser evil.
//
// Object ids below are the ones DEL_picked_up_object() already understands,
// so each reward behaves exactly like picking that item up normally.
// Per-level tables are separate purely so they are easy to tune - they all
// start out pointing at the same default.
//----------------------------------------------------------------------------
struct SECRET_BONUS_ITEM
{
	short object_number;
	short count;
};

static const SECRET_BONUS_ITEM SecretBonusDefault[] =
{
	{ BIGMEDI_ITEM,         1 },
	{ SMALLMEDI_ITEM,       2 },
	{ FLARE_INV_ITEM,       2 },
	{ REVOLVER_AMMO_ITEM,   1 },
	{ 0,                    0 }		//terminator
};

static const SECRET_BONUS_ITEM* secret_bonus_table[18] =
{
	0,						//LVL5_TITLE - no secrets
	SecretBonusDefault,		//LVL5_STREETS_OF_ROME
	SecretBonusDefault,		//LVL5_TRAJAN_MARKETS
	SecretBonusDefault,		//LVL5_COLOSSEUM
	SecretBonusDefault,		//LVL5_BASE
	SecretBonusDefault,		//LVL5_SUBMARINE
	SecretBonusDefault,		//LVL5_DEEPSEA_DIVE
	SecretBonusDefault,		//LVL5_SINKING_SUBMARINE
	SecretBonusDefault,		//LVL5_GALLOWS_TREE
	SecretBonusDefault,		//LVL5_LABYRINTH
	SecretBonusDefault,		//LVL5_OLD_MILL
	SecretBonusDefault,		//LVL5_THIRTEENTH_FLOOR
	SecretBonusDefault,		//LVL5_ESCAPE_WITH_THE_IRIS
	SecretBonusDefault,		//LVL5_SECURITY_BREACH
	SecretBonusDefault,		//LVL5_RED_ALERT
	0,						//LVL5_GIBBY_LEVEL - dev level
	0,						//LVL5_DEL_LEVEL - dev level
	0						//LVL5_TOM_LEVEL - dev level
};

void GrantSecretBonus(long level)
{
	// The reward comes from SecretRewards.txt beside the executable now, so
	// the randomizer can roll it and a player can set it himself. The table
	// above is what a fresh file is written from; nothing reads it here any
	// more (secretreward.cpp).
	const SECRET_REWARD_ITEM* bonus;
	long count;

	bonus = SecretRewardFor(level, &count);

	if (!bonus)
		return;

	for (long b = 0; b < count; b++)
	{
		for (int i = 0; i < bonus[b].count; i++)
		{
			DEL_picked_up_object(bonus[b].object_number);
			AddDisplayPickup(bonus[b].object_number);
		}
	}
}

static ushort options_table[NUM_INVOBJ] =
{
	OPT_EQUIP | OPT_COMBINE | OPT_UZI,			//INV_UZI_ITEM
	OPT_EQUIP | OPT_COMBINE | OPT_PISTOLS,		//INV_PISTOLS_ITEM
	OPT_EQUIP | OPT_COMBINE | OPT_SHOTGUN,		//INV_SHOTGUN_ITEM
	OPT_EQUIP | OPT_COMBINE | OPT_REVOLVER,		//INV_REVOLVER_ITEM1
	OPT_EQUIP | OPT_SEPARATE | OPT_REVOLVER,	//INV_REVOLVER_ITEM2
	OPT_EQUIP | OPT_COMBINE | OPT_CROSSBOW,		//INV_CROSSBOW_AMMO2_ITEM1
	OPT_EQUIP | OPT_SEPARATE | OPT_CROSSBOW,	//INV_CROSSBOW_AMMO2_ITEM2
	OPT_EQUIP | OPT_COMBINE | OPT_HK,			//INV_HK_ITEM1
	OPT_EQUIP | OPT_SEPARATE | OPT_HK,			//INV_HK_ITEM2
	OPT_USE,									//INV_SHOTGUN_AMMO1_ITEM
	OPT_USE,									//INV_SHOTGUN_AMMO2_ITEM
	OPT_USE,									//INV_HK_AMMO_ITEM1
	OPT_USE,									//INV_HK_AMMO_ITEM2
	OPT_USE,									//INV_HK_AMMO_ITEM3
	OPT_USE,									//INV_HK_AMMO_ITEM4
	OPT_USE,									//INV_CROSSBOW_AMMO2_ITEM
	OPT_USE,									//INV_CROSSBOW_AMMO3_ITEM
	OPT_USE,									//INV_REVOLVER_AMMO_ITEM
	OPT_USE,									//INV_UZI_AMMO_ITEM
	OPT_USE,									//INV_PISTOLS_AMMO_ITEM
	OPT_USE | OPT_COMBINE,						//INV_LASERSIGHT_ITEM
	OPT_USE | OPT_COMBINE,						//INV_SILENCER_ITEM
	OPT_USE,									//INV_BIGMEDI_ITEM
	OPT_USE,									//INV_SMALLMEDI_ITEM
	OPT_USE,									//INV_BINOCULARS_ITEM
	OPT_USE,									//INV_FLARE_INV_ITEM
	OPT_STATS,									//INV_COMPASS_ITEM
	OPT_LOAD,									//INV_MEMCARD_LOAD_INV_ITEM
	OPT_SAVE,									//INV_MEMCARD_SAVE_INV_ITEM
	OPT_USE,									//INV_PUZZLE_ITEM1
	OPT_USE,									//INV_PUZZLE_ITEM2
	OPT_USE,									//INV_PUZZLE_ITEM3
	OPT_USE,									//INV_PUZZLE_ITEM4
	OPT_USE,									//INV_PUZZLE_ITEM5
	OPT_USE,									//INV_PUZZLE_ITEM6
	OPT_USE,									//INV_PUZZLE_ITEM7
	OPT_USE,									//INV_PUZZLE_ITEM8
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM1_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM1_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM2_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM2_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM3_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM3_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM4_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM4_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM5_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM5_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM6_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM6_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM7_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM7_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM8_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PUZZLE_ITEM8_COMBO2
	OPT_USE,									//INV_KEY_ITEM1
	OPT_USE,									//INV_KEY_ITEM2
	OPT_USE,									//INV_KEY_ITEM3
	OPT_USE,									//INV_KEY_ITEM4
	OPT_USE,									//INV_KEY_ITEM5
	OPT_USE,									//INV_KEY_ITEM6
	OPT_USE,									//INV_KEY_ITEM7
	OPT_USE,									//INV_KEY_ITEM8
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM1_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM1_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM2_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM2_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM3_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM3_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM4_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM4_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM5_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM5_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM6_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM6_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM7_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM7_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM8_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_KEY_ITEM8_COMBO2
	OPT_USE,									//INV_PICKUP_ITEM1
	OPT_USE,									//INV_PICKUP_ITEM2
	OPT_USE,									//INV_PICKUP_ITEM3
	OPT_USE,									//INV_PICKUP_ITEM4
	OPT_USE | OPT_COMBINE,						//INV_PICKUP_ITEM1_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PICKUP_ITEM1_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PICKUP_ITEM2_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PICKUP_ITEM2_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PICKUP_ITEM3_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PICKUP_ITEM3_COMBO2
	OPT_USE | OPT_COMBINE,						//INV_PICKUP_ITEM4_COMBO1
	OPT_USE | OPT_COMBINE,						//INV_PICKUP_ITEM4_COMBO2
	OPT_USE,									//INV_BURNING_TORCH_ITEM
	OPT_USE,									//INV_CROWBAR_ITEM
	OPT_EXAMINE,								//INV_EXAMINE1_ITEM
	OPT_EXAMINE,								//INV_EXAMINE2_ITEM
	OPT_EXAMINE,								//INV_EXAMINE3_ITEM
	OPT_USE,									//INV_WET_CLOTH
	OPT_EQUIP | OPT_GRAPPLE,					//INV_CROSSBOW_ITEM
	OPT_USE,									//INV_CROSSBOW_AMMO1_ITEM
	OPT_USE | OPT_COMBINE,						//INV_CLOTH
	OPT_USE | OPT_COMBINE,						//INV_BOTTLE
	0,											//INV_PUZZLE_HOLE8
};

#pragma warning(push)
#pragma warning(disable : 4838)
#pragma warning(disable : 4309)
INVOBJ inventry_objects_list[NUM_INVOBJ] =
{
	//main items
	{UZI_ITEM, -4, 0x3E8, 0x4000, 0x6000, 0x4000, 2, TXT_Uzi, -1},
	{PISTOLS_ITEM, 6, 0x3E8, 0x4000, 0xAD4C, 0xBD40, 2, TXT_Pistols, -1},
	{SHOTGUN_ITEM, -6, 0x280, 0x8000, 0xC000, 0x2000, 2, TXT_Shotgun, 1},
	{REVOLVER_ITEM, 0, 0x320, 0x4000, 0x2AAA, 0x3BC2, 2, TXT_Revolver, 1},
	{REVOLVER_ITEM, 0, 0x320, 0x4000, 0x2AAA, 0x3BC2, 2, TXT_Revolver_LaserSight, 7},
	{CROSSBOW_AMMO2_ITEM, 0, 0x44C, 0x4000, -0x1000, 0, 2, TXT_GRAP2, -1},
	{CROSSBOW_AMMO2_ITEM, 0, 0x44C, 0x4000, -0x1000, 0, 2, TXT_GRAP2, -1},
	{HK_ITEM, 0, 0x320, 0, 0xC000, 0, 2, TXT_HKSTRING0, -1},
	{HK_ITEM, 0, 0x320, 0, 0xC000, 0, 2, TXT_HKSTRING1, -1},
	{SHOTGUN_AMMO1_ITEM, 0, 0x1F4, 0x4000, 0, 0, 2, TXT_Shotgun_Normal_Ammo, -1},
	{SHOTGUN_AMMO2_ITEM, 0, 0x1F4, 0x4000, 0, 0, 2, TXT_Shotgun_Wideshot_Ammo, -1},
	{HK_AMMO_ITEM, 3, 0x320, 0x4000, 0, 0, 2, TXT_HKSTRING2, 2},
	{HK_AMMO_ITEM, 3, 0x320, 0x4000, 0, 0, 2, TXT_HKSTRING3, 4},
	{HK_AMMO_ITEM, 3, 0x320, 0x4000, 0, 0, 2, TXT_HKSTRING4, 8},
	{HK_AMMO_ITEM, 3, 0x320, 0x4000, 0, 0, 2, TXT_HKSTRING5, 1},
	{CROSSBOW_AMMO2_ITEM, 0, 0x44C, 0x4000, -0x1000, 0, 2, TXT_GRAP2, -1},
	{CROSSBOW_AMMO2_ITEM, 0, 0x44C, 0x4000, -0x1000, 0, 2, TXT_GRAP2, -1},
	{REVOLVER_AMMO_ITEM, 0, 0x2BC, 0x4000, -0xBB8, 0, 2, TXT_Revolver_Ammo, -1},
	{UZI_AMMO_ITEM, 5, 0x2BC, 0, 0x1508, 0, 2, TXT_Uzi_Ammo, -1},
	{PISTOLS_AMMO_ITEM, 4, 0x2BC, 0, 0x4000, 0, 2, TXT_Pistol_Ammo, -1},
	{LASERSIGHT_ITEM, 2, 0x2BC, 0x4000, 0x7D0, 0, 2, TXT_LaserSight, -1},
	{SILENCER_ITEM, 1, 0x384, 0, 0x7D0, 0, 2, TXT_Silencer, -1},
	{BIGMEDI_ITEM, 2, 0x320, 0, 0, 0, 2, TXT_Large_Medipack, -1},
	{SMALLMEDI_ITEM, 0, 0x200, 0, 0x5000, 0, 2, TXT_Small_Medipack, -1},
	{BINOCULARS_ITEM, -1, 0x2BC, 0x1000, 0x7D0, 0, 2, TXT_Binoculars, -1},
	{FLARE_INV_ITEM, 2, 0x44C, 0x4000, 0, 0, 2, TXT_Flares, -1},
	{COMPASS_ITEM, 2, 0x44C, 0x8000, 0, 0, 2, TXT_TMX, -1},
	{PC_LOAD_INV_ITEM, 52, 0x898, 0x8000, 0, 0, 2, TXT_Load, -1},
	{PC_SAVE_INV_ITEM, 52, 0x898, 0x8000, 0, 0, 2, TXT_Save, -1},

	//puzzles and their combines
	{PUZZLE_ITEM1, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM2, 14, 0x258, 0, 0xC000, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM3, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM4, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM5, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM6, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM7, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM8, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},

	{PUZZLE_ITEM1_COMBO1, 18, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM1_COMBO2, 18, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM2_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM2_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM3_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM3_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM4_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM4_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM5_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM5_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM6_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM6_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM7_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM7_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM8_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PUZZLE_ITEM8_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},

	//keys and their combines
	{KEY_ITEM1, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM3, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM4, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM5, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM6, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM7, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM8, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},

	{KEY_ITEM1_COMBO1, 18, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM1_COMBO2, 18, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM2_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM2_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM3_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM3_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM4_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM4_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM5_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM5_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM6_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM6_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM7_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM7_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM8_COMBO1, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{KEY_ITEM8_COMBO2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},

	//pickup items and their combines
	{PICKUP_ITEM1, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM2, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM3, 8, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM4, 2, 0x320, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM1_COMBO1, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM1_COMBO2, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM2_COMBO1, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM2_COMBO2, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM3_COMBO1, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM3_COMBO2, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM4_COMBO1, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},
	{PICKUP_ITEM4_COMBO2, 14, 0x4B0, 0, 0, 0, 2, TXT_Load, -1},

	//the rest
	{BURNING_TORCH_ITEM, 14, 0x4B0, 0, 0x4000, 0, 2, TXT_Load, -1},
	{CROWBAR_ITEM, 4, 0x76C, 0, 0x4000, 0, 2, TXT_Crowbar, -1},
	{EXAMINE1, 4, 0x514, 0, 0x4000, 0, 2, TXT_Load, -1},
	{EXAMINE2, 14, 0x4B0, 0, 0x4000, 0, 2, TXT_Load, -1},
	{EXAMINE3, 14, 0x4B0, 0, 0x4000, 0, 2, TXT_Load, -1},
	{WET_CLOTH, -50, 0x200, 0x4000, 0x3000, 0x5000, 2, TXT_Wet_Cloth, -1},
	{CROSSBOW_ITEM, 0, 0x384, 0x2000, 0x1800, 0,2 , TXT_GRAP1, 1},
	{CROSSBOW_AMMO1_ITEM, 0, 0x44C, 0x4000, -0x1000, 0, 2, TXT_GRAP2, -1},
	{CLOTH, -50, 0x200, 0x4000, 0x3000, 0x5000, 2, TXT_Cloth, -1 },
	{BOTTLE, 50, 0x320, 0xC000, 0, 0, 2, TXT_Bottle, -1},
	{PUZZLE_HOLE8, -10, 0x180, 0xC000, 0x6800, 0xC000, 10, TXT_Bottle, -1}
};
#pragma warning(pop)

COMBINELIST dels_handy_combine_table[24] =
{
	{combine_revolver_lasersight, INV_REVOLVER_ITEM1, INV_LASERSIGHT_ITEM, INV_REVOLVER_ITEM2},
	{combine_crossbow_lasersight, INV_CROSSBOW_AMMO2_ITEM1, INV_LASERSIGHT_ITEM, INV_CROSSBOW_AMMO2_ITEM2},
	{combine_HK_SILENCER, INV_HK_ITEM1, INV_SILENCER_ITEM, INV_HK_ITEM2},
	{combine_PuzzleItem1, INV_PUZZLE_ITEM1_COMBO1, INV_PUZZLE_ITEM1_COMBO2, INV_PUZZLE_ITEM1},
	{combine_PuzzleItem2, INV_PUZZLE_ITEM2_COMBO1, INV_PUZZLE_ITEM2_COMBO2, INV_PUZZLE_ITEM2},
	{combine_PuzzleItem3, INV_PUZZLE_ITEM3_COMBO1, INV_PUZZLE_ITEM3_COMBO2, INV_PUZZLE_ITEM3},
	{combine_PuzzleItem4, INV_PUZZLE_ITEM4_COMBO1, INV_PUZZLE_ITEM4_COMBO2, INV_PUZZLE_ITEM4},
	{combine_PuzzleItem5, INV_PUZZLE_ITEM5_COMBO1, INV_PUZZLE_ITEM5_COMBO2, INV_PUZZLE_ITEM5},
	{combine_PuzzleItem6, INV_PUZZLE_ITEM6_COMBO1, INV_PUZZLE_ITEM6_COMBO2, INV_PUZZLE_ITEM6},
	{combine_PuzzleItem7, INV_PUZZLE_ITEM7_COMBO1, INV_PUZZLE_ITEM7_COMBO2, INV_PUZZLE_ITEM7},
	{combine_PuzzleItem8, INV_PUZZLE_ITEM8_COMBO1, INV_PUZZLE_ITEM8_COMBO2, INV_PUZZLE_ITEM8},
	{combine_KeyItem1, INV_KEY_ITEM1_COMBO1, INV_KEY_ITEM1_COMBO2, INV_KEY_ITEM1},
	{combine_KeyItem2, INV_KEY_ITEM2_COMBO1, INV_KEY_ITEM2_COMBO2, INV_KEY_ITEM2},
	{combine_KeyItem3, INV_KEY_ITEM3_COMBO1, INV_KEY_ITEM3_COMBO2, INV_KEY_ITEM3},
	{combine_KeyItem4, INV_KEY_ITEM4_COMBO1, INV_KEY_ITEM4_COMBO2, INV_KEY_ITEM4},
	{combine_KeyItem5, INV_KEY_ITEM5_COMBO1, INV_KEY_ITEM5_COMBO2, INV_KEY_ITEM5},
	{combine_KeyItem6, INV_KEY_ITEM6_COMBO1, INV_KEY_ITEM6_COMBO2, INV_KEY_ITEM6},
	{combine_KeyItem7, INV_KEY_ITEM7_COMBO1, INV_KEY_ITEM7_COMBO2, INV_KEY_ITEM7},
	{combine_KeyItem8, INV_KEY_ITEM8_COMBO1, INV_KEY_ITEM8_COMBO2, INV_KEY_ITEM8},
	{combine_PickupItem1, INV_PICKUP_ITEM1_COMBO1, INV_PICKUP_ITEM1_COMBO2, INV_PICKUP_ITEM1},
	{combine_PickupItem2, INV_PICKUP_ITEM2_COMBO1, INV_PICKUP_ITEM2_COMBO2, INV_PICKUP_ITEM2},
	{combine_PickupItem3, INV_PICKUP_ITEM3_COMBO1, INV_PICKUP_ITEM3_COMBO2, INV_PICKUP_ITEM3},
	{combine_PickupItem4, INV_PICKUP_ITEM4_COMBO1, INV_PICKUP_ITEM4_COMBO2, INV_PICKUP_ITEM4},
	{combine_clothbottle, INV_CLOTH, INV_BOTTLE, INV_WET_CLOTH}
};

/* The ring's input on a frame that carries no logic step.
 *
 * The same shadow the title menu puts over TitleOptions, for the same reason
 * and with the same rule: the input is put aside, zeroed for the length of the
 * frame and put back byte for byte. Nothing the body reaches can then act on
 * a press twice, and nothing downstream sees a cleared debounce.
 *
 * A shadow rather than a clearing, following the same reasoning as
 * TitleInputShadow (game\gameflow.cpp): the debounce throws dbinput's own
 * incoming value away, so a clearing would not turn a held key into a press -
 * but SetDebounce is cleared inside the first tick of a ControlPhase call, so
 * a zero left behind would stand through a hitch that runs several ticks and
 * swallow the edge the first of them found. The six go_ flags are in the
 * shadow as well: do_debounced_joystick_poo rebuilds all six from `input` at
 * the top of every step frame and does not run on a tween frame at all, so on
 * such a frame they would otherwise still be standing from the step before
 * and the ring would act on one press twice.
 *
 * `input` is in it too for one test at the bottom of the loop:
 * `use_the_bitch && !input` waits for the button that used an item to be
 * released, and a zeroed input is a released one. */
struct InvInputShadow
{
	long input;
	long dbinput;
	uchar left, right, up, down, select, deselect;
	bool down_now;
};

static void ShadowRingInput(InvInputShadow* s)
{
	s->input = input;
	s->dbinput = dbinput;
	s->left = go_left;
	s->right = go_right;
	s->up = go_up;
	s->down = go_down;
	s->select = go_select;
	s->deselect = go_deselect;
	s->down_now = true;

	input = 0;
	dbinput = 0;
	go_left = 0;
	go_right = 0;
	go_up = 0;
	go_down = 0;
	go_select = 0;
	go_deselect = 0;
}

static void RestoreRingInput(InvInputShadow* s)
{
	if (!s->down_now)
		return;

	input = s->input;
	dbinput = s->dbinput;
	go_left = s->left;
	go_right = s->right;
	go_up = s->up;
	go_down = s->down;
	go_select = s->select;
	go_deselect = s->deselect;
	s->down_now = false;
}

/* The words' half of the cheat seam. The four word cheats fire on every
 * logic step their four letters are held - that is their own behaviour and
 * none of it changes here (three are idempotent and the fourth plays a
 * sound). The log line does not follow suit: a line per step would be
 * thirty a second, so only the log is put on the press edge; nothing the
 * cheat does hangs on the answer. One bit per word, in the order they stand
 * below.
 *
 * The bits are cleared only while the ring is open, so a word held as the
 * ring closes stays "held" until it is asked again - which costs at most one
 * missing log line and no game behaviour. */
static long CheatWordEdge(long bit, long down)
{
	static long word_held = 0;
	const long was = (word_held >> bit) & 1;

	if (down)
		word_held |= 1L << bit;
	else
		word_held &= ~(1L << bit);

	return down && !was;
}

long S_CallInventory2()
{
	FLOOR_INFO* floor;
	long return_value, val, flag;
	short room_number, item;

	if (gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE)
	{
		inventry_objects_list[INV_REVOLVER_ITEM1].objname = TXT_DESERT1;
		inventry_objects_list[INV_REVOLVER_ITEM2].objname = TXT_DESERT2;
		inventry_objects_list[INV_REVOLVER_AMMO_ITEM].objname = TXT_DESERT3;
	}
	else
	{
		inventry_objects_list[INV_REVOLVER_ITEM1].objname = TXT_Revolver;
		inventry_objects_list[INV_REVOLVER_ITEM2].objname = TXT_Revolver_LaserSight;
		inventry_objects_list[INV_REVOLVER_AMMO_ITEM].objname = TXT_Revolver_Ammo;
	}

	if (gfLevelId >= LVL5_THIRTEENTH_FLOOR && gfLevelId <= LVL5_RED_ALERT)
	{
		inventry_objects_list[INV_BINOCULARS_ITEM].scale1 = 900;
		inventry_objects_list[INV_BINOCULARS_ITEM].yrot = -0x8000;
		inventry_objects_list[INV_BINOCULARS_ITEM].objname = TXT_Headset;
	}
	else
	{
		inventry_objects_list[INV_BINOCULARS_ITEM].scale1 = 700;
		inventry_objects_list[INV_BINOCULARS_ITEM].yrot = 0x1000;
		inventry_objects_list[INV_BINOCULARS_ITEM].objname = TXT_Binoculars;
	}

	if (gfLevelId == LVL5_ESCAPE_WITH_THE_IRIS)
	{
		inventry_objects_list[INV_HK_ITEM1].xrot = 0x2100;
		inventry_objects_list[INV_HK_ITEM1].yrot = 0x4000;
		inventry_objects_list[INV_HK_ITEM1].zrot = 0x4000;
		inventry_objects_list[INV_HK_ITEM1].flags = 10;
		inventry_objects_list[INV_HK_ITEM1].yoff = -40;
	}
	else
	{
		inventry_objects_list[INV_HK_ITEM1].xrot = -0x4000;
		inventry_objects_list[INV_HK_ITEM1].yrot = 0;
		inventry_objects_list[INV_HK_ITEM1].zrot = 0;
		inventry_objects_list[INV_HK_ITEM1].flags = 2;
		inventry_objects_list[INV_HK_ITEM1].yoff = 0;
	}

	friggrimmer = 0;
	oldLaraBusy = lara.Busy != 0;

	if (input & IN_SELECT)
		friggrimmer = 1;

	// The same swallow for deselect: Escape carries IN_DESELECT and
	// IN_OPTION together (specific/input.cpp). IN_OPTION opens the ring, and
	// since the ring closes on a left-over deselect, the release of that
	// very press would shut it again in the same breath - one press, two
	// consumers.
	//
	// friggrimmer2 makes the ring ignore the deselect belonging to the press
	// that opened it. The flag clears itself on the first frame the button is
	// up, so opening with the pad's inventory button is unaffected, and the
	// next press - Escape or Circle - closes normally.
	//
	// P for pause never had this: it carries IN_PAUSE alone.
	if (input & IN_DESELECT)
		friggrimmer2 = 1;

	rings[RING_INVENTORY] = &pcring1;
	rings[RING_AMMO] = &pcring2;
	CreateMonoScreen();
	InventoryActive = 1;
	init_new_inventry();
	camera.number_frames = 2;
	return_value = 0;
	val = 0;

	/* The ring follows the frame rate from here on. The wiring is one call
	 * because game\inventory_interp.cpp owns the snapshot and the
	 * accumulator but not one of the ring's own fields - every one of them
	 * is a static of this file, and they stay that way. */
	{
		InvInterpSource src;

		src.ring[RING_INVENTORY] = rings[RING_INVENTORY];
		src.ring[RING_AMMO] = rings[RING_AMMO];
		src.ammo = ammo_object_list;
		src.ammoFade = &ammo_selector_fade_val;
		src.combineFade = &combine_ring_fade_val;
		src.normalFade = &normal_ring_fade_val;
		src.examine = &examine_mode;
		InvInterpOpen(&src, "open");
	}

	while (!reset_flag && !val)
	{
		/* The ring's own cadence. This is the first statement of the body
		 * for the same reason InterpCapturePrev is the first statement of
		 * ControlPhase's tick body: what the snapshot has to remember is
		 * the state this frame's step is about to leave behind.
		 *
		 * `invStep` is true on every frame at fps_mode 0 and on every
		 * second one at fps_mode 1, so the whole body below reads as the
		 * body it has always been, with the logic behind one test. */
		const bool invStep = InvInterpBeginFrame();
		InvInputShadow shadow;

		shadow.down_now = false;

		OBJLIST_SPACING = phd_centerx >> 1;
		S_InitialisePolyList();

		if (invStep)
		{
			SetDebounce = 1;
			S_UpdateInput();
			input = inputBusy;
			UpdatePulseColour();
			GameTimer++;
		}
		else
			ShadowRingInput(&shadow);

		if (dbinput & IN_OPTION)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			val = 1;
		}

		return_value = MainThread.ended;

		if (return_value)
		{
			RestoreRingInput(&shadow);
			InvInterpClose();
			return return_value;
		}

		S_DisplayMonoScreen();

		if (GlobalSoftReset)
		{
			GlobalSoftReset = 0;
			val = 1;
		}

		if (invStep)
			do_debounced_joystick_poo();

		item = rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem;

		/* The four cheats read keymap directly rather than through `input`,
		 * so the shadow above does not reach them and they would fire at
		 * the frame rate. They are put on the logic step with everything
		 * else; three of the four are idempotent and the fourth plays a
		 * sound. */
		if (invStep)
		{
			/* The five conditions match the five original cheats letter for
			 * letter; each still runs exactly where and when it did. The
			 * four that have a hotkey role also log through the seam below
			 * (CheatWordEdge above; BITS gets neither a role nor a line). */
			const long word_guns = item == INV_COMPASS_ITEM && keymap[DIK_G] && keymap[DIK_U] && keymap[DIK_N] && keymap[DIK_S];
			const long word_item = item == INV_COMPASS_ITEM && keymap[DIK_I] && keymap[DIK_T] && keymap[DIK_E] && keymap[DIK_M];
			const long word_skip = item == INV_COMPASS_ITEM && keymap[DIK_S] && keymap[DIK_K] && keymap[DIK_I] && keymap[DIK_P];
			const long word_heal = item == INV_COMPASS_ITEM && keymap[DIK_H] && keymap[DIK_E] && keymap[DIK_A] && keymap[DIK_L];

			if (word_guns)//GUNS
				dels_give_lara_guns_cheat();

			if (CheatWordEdge(0, word_guns))
				CheatLog("weapons", "word");

			if (item == INV_COMPASS_ITEM && keymap[DIK_B] && keymap[DIK_I] && keymap[DIK_T] && keymap[DIK_S])//BITS
			{
				savegame.CampaignSecrets[0] = 9;
				savegame.CampaignSecrets[1] = 9;
				savegame.CampaignSecrets[2] = 9;
				savegame.CampaignSecrets[3] = 9;
			}

			if (word_item)	//ITEM
				dels_give_lara_items_cheat();

			if (CheatWordEdge(1, word_item))
				CheatLog("items", "word");

			if (word_skip)	//SKIP
			{
				gfLevelComplete = gfCurrentLevel + 1;
				SCNoDrawLara = 0;
				bDisableLaraControl = 0;
			}

			if (CheatWordEdge(2, word_skip))
				CheatLog("skip", "word");

			if (word_heal)	//heal
				lara_item->hit_points = 1000;

			if (CheatWordEdge(3, word_heal))
				CheatLog("heal", "word");
		}

		if (GLOBAL_invkeypadmode)
			do_keypad_mode();
		else if (examine_mode)
			do_examine_mode();
		else if (stats_mode)
			do_stats_mode();
		else
		{
			draw_current_object_list(RING_INVENTORY);
			handle_inventry_menu();
			
			if (rings[RING_AMMO]->ringactive)
				draw_current_object_list(RING_AMMO);

			draw_ammo_selector();
			fade_ammo_selector();
		}

		// Circle/B shuts the ring, the way it backs out of every other menu
		// screen (the convention set in specific/input.cpp). Deliberately
		// not by giving Circle IN_OPTION: that bit is the inventory TOGGLE,
		// and a gameplay button carrying it would open the ring on every
		// roll - which is exactly why the ring used to be the one screen
		// Circle could not leave, while Escape could (Escape sets both bits).
		//
		// Read HERE, at the end of the frame, and only if go_deselect is
		// still standing: the modes above - keypad, examine, stats, the
		// combine ring, the ammo selector - consume it for their own way
		// back, and one press must not both leave a mode and shut the ring
		// behind it.
		if (go_deselect)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			go_deselect = 0;
			val = 1;
		}

		if (invStep && use_the_bitch && !input)
			val = 1;

		S_OutputPolyList();

		/* The ring's present follows DrawPhaseGame's own pattern to the line
		 * (game\draw.cpp): raise the rate around this present only, put the
		 * default two back straight afterwards, and let the frame be judged
		 * against the slot it really asked for - HDFramePresented reads
		 * gPresentMinTicks from inside DXShowFrame, so FPSLOG's `budget=` is
		 * the ring's own 6.1 ms at fps_mode 2 and not the 33.3 of the
		 * default. */
		RestoreRingInput(&shadow);
		gPresentMinTicks = InvFrameMinTicks();
		camera.number_frames = S_DumpScreen();
		gPresentMinTicks = 2;
		InvInterpPresent(camera.number_frames);

		if (loading_or_saving)
		{
			/* The load/save screen is a modal loop of its own at two ticks,
			 * and what comes back from it is minutes of wall-clock time away
			 * from the frame that went in. The latch goes down with the same
			 * movement: that loop's counters step once per present and
			 * always have, and its first iteration runs before its first
			 * S_DumpScreen puts the latch down for itself. */
			InvInterpCut("loadsave");
			InterpNoteMenuFrame(false);

			while (1)
			{
				flag = 0;
				S_InitialisePolyList();
				SetDebounce = 1;
				S_UpdateInput();
				input = inputBusy;
				UpdatePulseColour();

				if (loading_or_saving == 1)
					flag = go_and_load_game();
				else if (go_and_save_game())
					flag = 1;

				if (flag == 1)
				{
					if (loading_or_saving == flag)
					{
						return_value = 1;
						val = 1;
					}

					break;
				}
				else if (flag)
					break;
			}

			friggrimmer = 1;
			friggrimmer2 = 1;
			go_deselect = 0;
			deselect_debounce = 0;
			loading_or_saving = 0;
		}
	}

	InvInterpClose();	// end the ring's frame-rate interpolation for this session
	InitialisePickUpDisplay();
	GLOBAL_lastinvitem = rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem;
	update_laras_weapons_status();

	if (use_the_bitch && !GLOBAL_invkeypadmode)
		use_current_item();

	FreeMonoScreen();

	lara.Busy = oldLaraBusy & 1;
	InventoryActive = 0;

	if (GLOBAL_invkeypadmode)
	{
		val = 0;
		GLOBAL_invkeypadmode = 0;

		if (keypadnuminputs == 4)
			val = keypadinputs[0] * 1000 + keypadinputs[1] * 100 + keypadinputs[2] * 10 + keypadinputs[3];

		if (GLOBAL_invkeypadcombination == val)
		{
			room_number = lara_item->room_number;
			floor = GetFloor(lara_item->pos.x_pos, lara_item->pos.y_pos, lara_item->pos.z_pos, &room_number);
			GetHeight(floor, lara_item->pos.x_pos, lara_item->pos.y_pos, lara_item->pos.z_pos);
			TestTriggers(trigger_index, 1, 0);
		}
	}

	return return_value;
}

void init_new_inventry()
{
	examine_mode = 0;
	stats_mode = 0;
	AlterFOV(GAME_FOV);
	lara.Busy = 0;
	GLOBAL_inventoryitemchosen = NO_ITEM;
	left_debounce = 0;
	right_debounce = 0;
	up_debounce = 0;
	down_debounce = 0;
	go_left = 0;
	go_right = 0;
	go_up = 0;
	go_down = 0;
	select_debounce = 0;
	deselect_debounce = 0;
	go_select = 0;
	go_deselect = 0;
	left_repeat = 0;
	right_repeat = 0;
	loading_or_saving = 0;
	use_the_bitch = 0;

	if (lara.num_shotgun_ammo1 == -1)
		AmountShotGunAmmo1 = -1;
	else
		AmountShotGunAmmo1 = lara.num_shotgun_ammo1 / 6;

	if (lara.num_shotgun_ammo2 == -1)
		AmountShotGunAmmo2 = -1;
	else
		AmountShotGunAmmo2 = lara.num_shotgun_ammo2 / 6;

	AmountHKAmmo1 = lara.num_hk_ammo1;
	AmountCrossBowAmmo1 = lara.num_crossbow_ammo1;
	AmountCrossBowAmmo2 = lara.num_crossbow_ammo2;
	AmountUziAmmo = lara.num_uzi_ammo;
	AmountRevolverAmmo = lara.num_revolver_ammo;
	AmountPistolsAmmo = lara.num_pistols_ammo;
	construct_object_list();

	if (GLOBAL_enterinventory == NO_ITEM)
	{
		if (GLOBAL_lastinvitem != NO_ITEM)
		{
			if (have_i_got_item((short)GLOBAL_lastinvitem))
				setup_objectlist_startposition((short)GLOBAL_lastinvitem);

			GLOBAL_lastinvitem = NO_ITEM;
		}
	}
	else if (GLOBAL_enterinventory == 0xDEADBEEF)
	{
		GLOBAL_invkeypadmode = 1;
		init_keypad_mode();
		GLOBAL_enterinventory = NO_ITEM;
	}
	else
	{
		if (have_i_got_object((short)GLOBAL_enterinventory))
			setup_objectlist_startposition2((short)GLOBAL_enterinventory);

		GLOBAL_enterinventory = NO_ITEM;
	}

	ammo_selector_fade_val = 0;
	ammo_selector_fade_dir = 0;
	combine_ring_fade_val = 0;
	combine_ring_fade_dir = 0;
	combine_type_flag = 0;
	seperate_type_flag = 0;
	combine_obj1 = 0;
	combine_obj2 = 0;
	normal_ring_fade_val = 128;
	normal_ring_fade_dir = 0;
	handle_object_changeover(RING_INVENTORY);
}

void do_debounced_joystick_poo()
{
	go_left = 0;
	go_right = 0;
	go_up = 0;
	go_down = 0;
	go_select = 0;
	go_deselect = 0;

	if (input & IN_LEFT)
	{
		if (left_repeat >= 8)
			go_left = 1;
		else
			left_repeat++;

		if (!left_debounce)
			go_left = 1;

		left_debounce = 1;
	}
	else
	{
		left_debounce = 0;
		left_repeat = 0;
	}

	if (input & IN_RIGHT)
	{
		if (right_repeat >= 8)
			go_right = 1;
		else
			right_repeat++;

		if (!right_debounce)
			go_right = 1;

		right_debounce = 1;
	}
	else
	{
		right_debounce = 0;
		right_repeat = 0;
	}

	if (input & IN_FORWARD)
	{
		if (!up_debounce)
			go_up = 1;

		up_debounce = 1;
	}
	else
		up_debounce = 0;

	if (input & IN_BACK)
	{
		if (!down_debounce)
			go_down = 1;

		down_debounce = 1;
	}
	else
		down_debounce = 0;

	/* The ring confirms on the Action key, not on the Secondary one.
	 * IN_SELECT belongs to the Action role in both control schemes, so it
	 * always confirms; the IN_ACTION term is Core's and is kept for
	 * "One key" alone, where it is the same press and the same word. Under
	 * "Separate" IN_ACTION is the Secondary role - the fire key - and a
	 * trigger pull in the ring would equip whatever the cursor sat on. The
	 * guard is written out rather than the term simply dropped so that the
	 * "One key" default is byte-identical by construction and not by an
	 * argument about which bits travel together. */
	if (input & IN_SELECT || (!tomb5.action_split && input & IN_ACTION))
		select_debounce = 1;
	else
	{
		if (select_debounce == 1 && !friggrimmer)
			go_select = 1;

		select_debounce = 0;
		friggrimmer = 0;
	}

	if (input & IN_DESELECT)
		deselect_debounce = 1;
	else
	{
		if (deselect_debounce == 1 && !friggrimmer2)
			go_deselect = 1;

		deselect_debounce = 0;
		friggrimmer2 = 0;
	}
}

void DrawThreeDeeObject2D(long x, long y, long num, long shade, long xrot, long yrot, long zrot, long bright, long overlay)
{
	INVOBJ* objme;
	ITEM_INFO item;

	objme = &inventry_objects_list[num];
	item.pos.x_rot = short(xrot + objme->xrot);
	item.pos.y_rot = short(yrot + objme->yrot);
	item.pos.z_rot = short(zrot + objme->zrot);
	item.object_number = objme->object_number;
	phd_LookAt(0, 1024, 0, 100, 0, 200, 0);

	if (!bright)
		pcbright = 0x7F7F7F;
	else if (bright == 1)
		pcbright = 0x2F2F2F;
	else
		pcbright = RGBONLY(bright, bright, bright);

	aSetViewMatrix();
	phd_PushUnitMatrix();
	phd_TranslateRel(0, 0, objme->scale1);
	yoffset = objme->yoff + y;
	item.mesh_bits = objme->meshbits;
	xoffset = x;
	item.shade = -1;
	item.pos.x_pos = 0;
	item.pos.y_pos = 0;
	item.pos.z_pos = 0;
	item.room_number = 0;
	item.il.nCurrentLights = 0;
	item.il.nPrevLights = 0;
	item.il.ambient = 0x7F7F7F;
	item.anim_number = objects[item.object_number].anim_index;
	DrawInventoryItemMe(&item, shade, overlay, objme->flags & 8 ? 1 : 0);
	phd_PopMatrix();
	xoffset = phd_centerx;
	yoffset = phd_centery;
}

void DrawInventoryItemMe(ITEM_INFO* item, long shade, long overlay, long shagflag)
{
	ANIM_STRUCT* anim;
	OBJECT_INFO* object;
	PHD_VECTOR vec;
	short** meshpp;
	long* bone;
	short* rotation1;
	short* frmptr;
	ulong bit;
	long poppush, a;

	anim = &anims[item->anim_number];
	frmptr = anim->frame_ptr;
	object = &objects[item->object_number];
	phd_PushMatrix();

	if (item->object_number == HK_ITEM && gfLevelId == LVL5_ESCAPE_WITH_THE_IRIS)
		phd_TranslateRel(0, 70, 0);

	phd_TranslateRel(item->pos.x_pos, item->pos.y_pos, item->pos.z_pos);
	phd_RotYXZ(item->pos.y_rot, item->pos.x_rot, item->pos.z_rot);

	if (item->object_number == PUZZLE_HOLE8 && GLOBAL_invkeypadmode)
	{
		vec.x = 24576;
		vec.y = 16384;
		vec.z = 4096;
		ScaleCurrentMatrix(&vec);
	}
	
	bit = 1;
	meshpp = &meshes[object->mesh_index];
	bone = &bones[object->bone_index];

	/* Is this item drawn by the HD path?
	 *
	 * The same seam DrawAnimatingItem uses, in the one other place in this
	 * engine that walks a mesh tree and hands each node's mesh to the
	 * renderer. When the answer is true not one classic polygon of this item
	 * goes into the sort list: the walk below runs exactly as it always did -
	 * the same frame, the same bones, the same mesh bits - and instead of
	 * handing each node's mesh to phd_PutPolygonsPickup it hands the node's
	 * matrix to hd_object, which is what a skinned model wants.
	 *
	 * xoffset/yoffset are the item's place on the screen, which is what the
	 * classic call passes as the temporary projection centre; pcbright is the
	 * brightness for this item, which is the classic sun's colour. hd_object
	 * carries the first to the item's own draw, where it moves the
	 * projection, and takes the second as its one light.
	 *
	 * Both callers of this function come through here: the inventory's four
	 * (the ring, the ammo selector, the keypad, the examine screen) and
	 * S_DrawPickup's one - the little turning object in the corner after a
	 * pickup, drawn on the spot rather than through the classic path. Which
	 * of the two this is, and what hd_object does about it, is decided in
	 * HDInvBegin and nowhere else: these four call sites are one seam and
	 * stay one seam.
	 *
	 * False for an item the remaster ships no model for, for a slot whose
	 * skeleton the model does not fit, and with the HD path off. Then it is
	 * drawn exactly as it was, item by item, and the ring never has a hole in
	 * it. */
	const bool hd = HDInvBegin(item, object, (float)xoffset, (float)yoffset, pcbright);

	if (!shagflag)
		phd_TranslateRel(frmptr[6], frmptr[7], frmptr[8]);

	rotation1 = &frmptr[9];
	gar_RotYXZsuperpack(&rotation1, 0);

	/* The matrix is recorded whether or not mesh_bits shows the node, because
	 * a hidden node still carries the ones below it - the mask reaches the
	 * model as a mask and hides its triangles there instead, so the ring
	 * needs no flag of its own where TROne needed TRONE_LIGHT_EXACT_NODES. */
	if (hd)
		HDInvNode(0);
	else if (item->mesh_bits & 1)
	{
		if (overlay)
			phd_PutPolygonsPickup(*meshpp, (float)xoffset, (float)yoffset, pcbright);
		else
		{
			a = GlobalAlpha;
			GlobalAlpha = 0xFF000000;
			phd_PutPolygonsPickup(*meshpp, (float)xoffset, (float)yoffset, pcbright);
			GlobalAlpha = a;
		}
	}

	meshpp += 2;

	for (int i = 0; i < object->nmeshes - 1; i++, meshpp += 2, bone += 4)
	{
		poppush = *bone;

		if (poppush & 1)
			phd_PopMatrix();

		if (poppush & 2)
			phd_PushMatrix();

		phd_TranslateRel(bone[1], bone[2], bone[3]);
		gar_RotYXZsuperpack(&rotation1, 0);
		bit <<= 1;

		if (hd)
			HDInvNode(i + 1);
		else if (item->mesh_bits & bit)
		{
			if (overlay)
				phd_PutPolygonsPickup(*meshpp, (float)xoffset, (float)yoffset, pcbright);
			else
			{
				a = GlobalAlpha;
				GlobalAlpha = 0xFF000000;
				phd_PutPolygonsPickup(*meshpp, (float)xoffset, (float)yoffset, pcbright);
				GlobalAlpha = a;
			}
		}
	}

	/* The node walk is over. For a RING item HDInvEnd puts it on the frame's
	 * list - the very list the world's HD objects go on, drawn in the same
	 * bracket by the same HDObjectFrameDraw; for the pickup display it
	 * draws the one object on the spot, in a bracket of its own, because the
	 * frame's list has been drawn and closed by the time S_DrawPickup runs.
	 * Called on the one road out of this function, as HDObjectEnd is in
	 * game\draw.cpp. */
	if (hd)
		HDInvEnd();

	phd_PopMatrix();
}

long go_and_load_game()
{
	return LoadGame();
}

long go_and_save_game()
{
	return SaveGame();
}

void construct_combine_object_list()
{
	rings[RING_AMMO]->numobjectsinlist = 0;

	for (int i = 0; i < NUM_INVOBJ; i++)
		rings[RING_AMMO]->current_object_list[i].invitem = -1;

	if (!(gfLevelFlags & GF_YOUNGLARA))
	{
		if (lara.sixshooter_type_carried & W_PRESENT)
		{
			if (lara.sixshooter_type_carried & W_LASERSIGHT)
				insert_object_into_list_v2(INV_REVOLVER_ITEM2);
			else
				insert_object_into_list_v2(INV_REVOLVER_ITEM1);
		}

		if (lara.crossbow_type_carried & W_PRESENT && (gfLevelId < LVL5_THIRTEENTH_FLOOR || gfLevelId > LVL5_RED_ALERT))
		{
			if (lara.crossbow_type_carried & W_LASERSIGHT)
				insert_object_into_list_v2(INV_CROSSBOW_AMMO2_ITEM2);
			else
				insert_object_into_list_v2(INV_CROSSBOW_AMMO2_ITEM1);
		}

		if (lara.lasersight)
			insert_object_into_list_v2(INV_LASERSIGHT_ITEM);

		if (lara.silencer)
			insert_object_into_list_v2(INV_SILENCER_ITEM);
	}

	for (int i = 0; i < 16; i++)
	{
		if (lara.puzzleitemscombo & (1 << i))
			insert_object_into_list_v2(INV_PUZZLE_ITEM1_COMBO1 + i);
	}

	for (int i = 0; i < 16; i++)
	{
		if (lara.keyitemscombo & (1 << i))
			insert_object_into_list_v2(INV_KEY_ITEM1_COMBO1 + i);
	}

	for (int i = 0; i < 8; i++)
	{
		if (lara.pickupitemscombo & (1 << i))
			insert_object_into_list_v2(INV_PICKUP_ITEM1_COMBO1 + i);
	}

	if (lara.wetcloth == CLOTH_DRY)
		insert_object_into_list_v2(INV_CLOTH);

	if (lara.bottle)
		insert_object_into_list_v2(INV_BOTTLE);

	rings[RING_AMMO]->objlistmovement = 0;
	rings[RING_AMMO]->curobjinlist = 0;
	rings[RING_AMMO]->ringactive = 0;
}

void insert_object_into_list_v2(long num)
{
	if (options_table[num] & (OPT_COMBINE | OPT_UNUSED))
	{
		if (rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem != num)
		{
			rings[RING_AMMO]->current_object_list[rings[RING_AMMO]->numobjectsinlist].invitem = (short)num;
			rings[RING_AMMO]->current_object_list[rings[RING_AMMO]->numobjectsinlist].yrot = 0;
			rings[RING_AMMO]->current_object_list[rings[RING_AMMO]->numobjectsinlist++].bright = 32;
		}
	}
}

void construct_object_list()
{
	/* The list is being rebuilt, so every slot in the ring's snapshot may
	 * mean a different object a line from now. Called from init_new_inventry
	 * before the ring is even open (where it does nothing but clear a counter
	 * that is already clear), from a combine and a separate, and from the
	 * weapon hot keys. */
	InvInterpCut("rebuild");

	rings[RING_INVENTORY]->numobjectsinlist = 0;

	for (int i = 0; i < NUM_INVOBJ; i++)
		rings[RING_INVENTORY]->current_object_list[i].invitem = NO_ITEM;

	CurrentPistolsAmmoType = 0;
	CurrentUziAmmoType = 0;
	CurrentRevolverAmmoType = 0;
	CurrentShotGunAmmoType = 0;
	CurrentGrenadeGunAmmoType = 0;
	CurrentCrossBowAmmoType = 0;

	if (!(gfLevelFlags & GF_YOUNGLARA))
	{
		if (lara.pistols_type_carried & W_PRESENT)
			insert_object_into_list(INV_PISTOLS_ITEM);

		if (lara.uzis_type_carried & W_PRESENT)
			insert_object_into_list(INV_UZI_ITEM);
		else if (AmountUziAmmo)
			insert_object_into_list(INV_UZI_AMMO_ITEM);

		if (lara.sixshooter_type_carried & W_PRESENT)
		{
			if (lara.sixshooter_type_carried & W_LASERSIGHT)
				insert_object_into_list(INV_REVOLVER_ITEM2);
			else
				insert_object_into_list(INV_REVOLVER_ITEM1);
		}
		else if (AmountRevolverAmmo)
			insert_object_into_list(INV_REVOLVER_AMMO_ITEM);

		if (lara.shotgun_type_carried & W_PRESENT)
		{
			insert_object_into_list(INV_SHOTGUN_ITEM);

			if (lara.shotgun_type_carried & W_AMMO2)
				CurrentShotGunAmmoType = 1;
		}
		else
		{
			if (AmountShotGunAmmo1)
				insert_object_into_list(INV_SHOTGUN_AMMO1_ITEM);

			if (AmountShotGunAmmo2)
				insert_object_into_list(INV_SHOTGUN_AMMO2_ITEM);
		}

		if (lara.hk_type_carried & W_PRESENT)
		{
			if (lara.hk_type_carried & W_SILENCER)
				insert_object_into_list(INV_HK_ITEM2);
			else
				insert_object_into_list(INV_HK_ITEM1);

			if (lara.hk_type_carried & W_AMMO2)
				CurrentGrenadeGunAmmoType = 1;
			else if (lara.hk_type_carried & W_AMMO3)
				CurrentGrenadeGunAmmoType = 2;
		}
		else if (AmountHKAmmo1)
			insert_object_into_list(INV_HK_AMMO_ITEM4);

		if (lara.crossbow_type_carried & W_PRESENT)
		{
			if (gfLevelId < LVL5_THIRTEENTH_FLOOR || gfLevelId > LVL5_RED_ALERT)
			{
				if (lara.crossbow_type_carried & W_LASERSIGHT)
					insert_object_into_list(INV_CROSSBOW_AMMO2_ITEM2);
				else
					insert_object_into_list(INV_CROSSBOW_AMMO2_ITEM1);

				if (lara.crossbow_type_carried & W_AMMO2)
					CurrentCrossBowAmmoType = 1;
			}
			else
			{
				insert_object_into_list(INV_CROSSBOW_ITEM);
				CurrentCrossBowAmmoType = 0;
			}
		}
		else if (gfLevelId < LVL5_THIRTEENTH_FLOOR || gfLevelId > LVL5_RED_ALERT)
		{
			if (AmountCrossBowAmmo1)
				insert_object_into_list(INV_CROSSBOW_AMMO2_ITEM3);

			if (AmountCrossBowAmmo2)
				insert_object_into_list(INV_CROSSBOW_AMMO2_ITEM4);
		}
		else if (AmountCrossBowAmmo1)
			insert_object_into_list(INV_CROSSBOW_AMMO1_ITEM);

		if (lara.lasersight)
			insert_object_into_list(INV_LASERSIGHT_ITEM);

		if (lara.silencer)
			insert_object_into_list(INV_SILENCER_ITEM);

		if (lara.binoculars)
			insert_object_into_list(INV_BINOCULARS_ITEM);

		if (lara.num_flares)
			insert_object_into_list(INV_FLARE_INV_ITEM);
	}

	insert_object_into_list(INV_COMPASS_ITEM);

	if (lara.num_small_medipack)
		insert_object_into_list(INV_SMALLMEDI_ITEM);

	if (lara.num_large_medipack)
		insert_object_into_list(INV_BIGMEDI_ITEM);

	if (lara.crowbar)
		insert_object_into_list(INV_CROWBAR_ITEM);

	for (int i = 0; i < 8; i++)
	{
		if (lara.puzzleitems[i])
			insert_object_into_list(INV_PUZZLE_ITEM1 + i);
	}

	for (int i = 0; i < 16; i++)
	{
		if (lara.puzzleitemscombo & (1 << i))
			insert_object_into_list(INV_PUZZLE_ITEM1_COMBO1 + i);
	}

	for (int i = 0; i < 8; i++)
	{
		// Counter, not a bit - see LARA_INFO::keyitems in global\types.h.
		// One entry in the ring no matter how many copies are held; the
		// count itself is drawn by the "nummeup" block further down, exactly
		// like puzzle items.
		if (lara.keyitems[i])
			insert_object_into_list(INV_KEY_ITEM1 + i);
	}

	for (int i = 0; i < 16; i++)
	{
		if (lara.keyitemscombo & (1 << i))
			insert_object_into_list(INV_KEY_ITEM1_COMBO1 + i);
	}

	for (int i = 0; i < 4; i++)
	{
		if (lara.pickupitems & (1 << i))
			insert_object_into_list(INV_PICKUP_ITEM1 + i);
	}

	for (int i = 0; i < 8; i++)
	{
		if (lara.pickupitemscombo & (1 << i))
			insert_object_into_list(INV_PICKUP_ITEM1_COMBO1 + i);
	}

	if (lara.examine1)
		insert_object_into_list(INV_EXAMINE1);

	if (lara.examine2)
		insert_object_into_list(INV_EXAMINE2);

	if (lara.examine3)
		insert_object_into_list(INV_EXAMINE3);

	if (lara.wetcloth == CLOTH_WET)
		insert_object_into_list(INV_WET_CLOTH);

	if (lara.wetcloth == CLOTH_DRY)
		insert_object_into_list(INV_CLOTH);

	if (lara.bottle)
		insert_object_into_list(INV_BOTTLE);

	if (Gameflow->LoadSaveEnabled)
	{
		insert_object_into_list(INV_MEMCARD_LOAD_INV_ITEM);
		insert_object_into_list(INV_MEMCARD_SAVE_INV_ITEM);
	}

	rings[RING_INVENTORY]->objlistmovement = 0;
	rings[RING_INVENTORY]->curobjinlist = 0;
	rings[RING_INVENTORY]->ringactive = 1;
	rings[RING_AMMO]->objlistmovement = 0;
	rings[RING_AMMO]->curobjinlist = 0;
	rings[RING_AMMO]->ringactive = 0;
	handle_object_changeover(RING_INVENTORY);
	ammo_active = 0;
}

void insert_object_into_list(long num)
{
	rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->numobjectsinlist].invitem = (short)num;
	rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->numobjectsinlist].yrot = 0;
	rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->numobjectsinlist].bright = 32;
	rings[RING_INVENTORY]->numobjectsinlist++;
}

/* The two ring fades, split out of draw_current_object_list unchanged so
 * that the ring's logic step can gate the whole of it with one test instead
 * of a brace around forty lines. It steps the combine ring's fade in and out
 * and the inventory ring's, and hands the ring over when one of them reaches
 * its end - which is state, and state moves on the logic step and on no
 * other frame. */
static void step_ring_fades(long ringnum)
{
	if (ringnum == RING_AMMO)
	{
		ammo_selector_fade_val = 0;
		ammo_selector_fade_dir = 0;

		if (combine_ring_fade_dir == 1)
		{
			if (combine_ring_fade_val < 128)
				combine_ring_fade_val += 32;

			if (combine_ring_fade_val > 128)
			{
				combine_ring_fade_val = 128;
				combine_ring_fade_dir = 0;
			}
		}
		else if (combine_ring_fade_dir == 2)
		{
			combine_ring_fade_val -= 32;

			if (combine_ring_fade_val <= 0)
			{
				combine_ring_fade_val = 0;
				combine_ring_fade_dir = 0;

				if (combine_type_flag)
					normal_ring_fade_dir = 2;
				else
				{
					rings[RING_INVENTORY]->ringactive = 1;
					menu_active = 1;
					rings[RING_AMMO]->ringactive = 0;
					handle_object_changeover(RING_INVENTORY);
				}

				rings[RING_AMMO]->ringactive = 0;
			}
		}
	}
	else if (normal_ring_fade_dir == 1)
	{
		if (normal_ring_fade_val < 128)
			normal_ring_fade_val += 32;

		if (normal_ring_fade_val > 128)
		{
			normal_ring_fade_val = 128;
			normal_ring_fade_dir = 0;
			rings[RING_INVENTORY]->ringactive = 1;
			menu_active = 1;
		}

	}
	else if (normal_ring_fade_dir == 2)
	{
		normal_ring_fade_val -= 32;

		if (normal_ring_fade_val <= 0)
		{
			normal_ring_fade_val = 0;
			normal_ring_fade_dir = 1;

			if (combine_type_flag == 1)
			{
				combine_type_flag = 0;
				combine_these_two_objects(combine_obj1, combine_obj2);
			}
			else if (seperate_type_flag)
				seperate_object(rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem);

			handle_object_changeover(RING_INVENTORY);
		}
	}
}

void draw_current_object_list(long ringnum)
{
	long n, maxobj, xoff, shade, minobj, objmeup, nummeup, activenum, ymeup, x, y;
	short yrot;
	char textbufme[128];

	if (rings[ringnum]->numobjectsinlist <= 0)
		return;

	/* Every step in this function is on the ring's logic frame, gated by the
	 * tween latch, which the ring raises for itself (game\inventory_interp.cpp)
	 * now that its present no longer waits two ticks. At fps_mode 0 it is
	 * false on every frame, step_ring_fades below runs from the same point
	 * in the function, and this function runs as it always has. */
	const bool invStep = !InterpIsTweenFrame();

	if (invStep)
		step_ring_fades(ringnum);

	/* Where the row stands in this picture. objlistmovement is the slide, 0
	 * to 65536 in eight steps of 8192, and it is the one field of the ring
	 * that vanilla steps after it has drawn with it - the block at the
	 * bottom of this function. So the value the picture wants is not the
	 * live one but the blend of the two the last two steps produced, and it
	 * is read once, here, rather than at each of the eight places below that
	 * would otherwise reach for the field directly. The stepping block at
	 * the bottom keeps the live field, which is the logic; this is the
	 * picture.
	 *
	 * The two small decisions that hang off it follow the picture too: which
	 * icon spins rather than spins back, and which one the brightness ramp
	 * walks towards, are read from `move` and not from the field. At fps_mode
	 * 0 they are the same number. At fps_mode 1 they are the same answer: the
	 * step frame's factor is a half, and half of a pair whose newer half is
	 * the live value keeps its sign and keeps being zero or not. At fps_mode
	 * 2 the factor starts near nothing, so `move` can lag the field by up to
	 * one step when a turn begins - and then the icon centred in the picture
	 * is the one that spins and brightens, matching what the eye sees.
	 * Nothing of the ring's own state is read from it: the slide, the
	 * selected slot and the sound of a turn are all below, off the live
	 * field. */
	const long move = InvDrawMove(ringnum, rings[ringnum]->objlistmovement);
	const long curDrawn = InvDrawCur(ringnum, rings[ringnum]->curobjinlist,
		rings[ringnum]->numobjectsinlist);

	minobj = 0;
	maxobj = 0;
	xoff = 0;
	n = 0;

	if (rings[ringnum]->numobjectsinlist != 1)
		xoff = (OBJLIST_SPACING * move) >> 16;

	if (rings[ringnum]->numobjectsinlist == 2)
	{
		minobj = -1;
		maxobj = 0;
		n = curDrawn - 1;
	}

	if (rings[ringnum]->numobjectsinlist == 3 || rings[ringnum]->numobjectsinlist == 4)
	{
		minobj = -2;
		maxobj = 1;
		n = curDrawn - 2;
	}

	if (rings[ringnum]->numobjectsinlist >= 5)
	{
		minobj = -3;
		maxobj = 2;
		n = curDrawn - 3;
	}

	if (n < 0)
		n += rings[ringnum]->numobjectsinlist;

	if (move < 0)
		maxobj++;

	for (int i = minobj; i <= maxobj; i++)
	{
		if (i == minobj)
		{
			if (move < 0)
				shade = 0;
			else
				shade = move >> 9;
		}
		else if (i == minobj + 1 && maxobj != minobj + 1)
		{
			if (move < 0)
				shade = 128 - ((-128 * move) >> 16);
			else
				shade = 128;
		}
		else if (i != maxobj)
			shade = 128;
		else if (move < 0)
			shade = (-128 * move) >> 16;
		else
			shade = 128 - short(move >> 9);

		if (!minobj && !maxobj)
			shade = 128;

		// The two ring fades are stepped before the icon is drawn, so their
		// pair is (previous, live) and the blend is asked for here.
		if (ringnum == RING_AMMO && combine_ring_fade_val < 128 && shade)
			shade = InvDrawCombineFade(combine_ring_fade_val);
		else if (ringnum == RING_INVENTORY && normal_ring_fade_val < 128 && shade)
			shade = InvDrawNormalFade(normal_ring_fade_val);

		if (!i)
		{
			nummeup = 0;
			objmeup = inventry_objects_list[rings[ringnum]->current_object_list[n].invitem].object_number;

			if (objmeup == BIGMEDI_ITEM)
				nummeup = lara.num_large_medipack;
			else if (objmeup == SMALLMEDI_ITEM)
				nummeup = lara.num_small_medipack;
			else if (objmeup == FLARE_INV_ITEM)
				nummeup = lara.num_flares;
			else if (objmeup >= PUZZLE_ITEM1 && objmeup <= PUZZLE_ITEM8)
			{
				nummeup = lara.puzzleitems[objmeup - PUZZLE_ITEM1];

				if (nummeup <= 1)
					nummeup = 0;
			}
			// Keys are counted (see LARA_INFO::keyitems in global\types.h),
			// so they get the same "x N" label as puzzle items - a single
			// copy stays unlabelled, two or more show the number. Without
			// this a second copy of a key would be invisible to the player
			// even though the engine keeps it.
			else if (objmeup >= KEY_ITEM1 && objmeup <= KEY_ITEM8)
			{
				nummeup = lara.keyitems[objmeup - KEY_ITEM1];

				if (nummeup <= 1)
					nummeup = 0;
			}
			else if (objmeup == SHOTGUN_AMMO1_ITEM)
				nummeup = lara.num_shotgun_ammo1 == -1 ? lara.num_shotgun_ammo1 : lara.num_shotgun_ammo1 / 6;
			else if (objmeup == SHOTGUN_AMMO2_ITEM)
				nummeup = lara.num_crossbow_ammo2 == -1 ? lara.num_shotgun_ammo2 : lara.num_shotgun_ammo2 / 6;
			else if (objmeup == HK_AMMO_ITEM)
				nummeup = lara.num_hk_ammo1;
			else if (objmeup == CROSSBOW_AMMO1_ITEM)
				nummeup = lara.num_crossbow_ammo1;
			else if (objmeup == CROSSBOW_AMMO2_ITEM)
				nummeup = lara.num_crossbow_ammo2;
			else if (objmeup == REVOLVER_AMMO_ITEM)
				nummeup = lara.num_revolver_ammo;
			else if (objmeup == UZI_AMMO_ITEM)
				nummeup = lara.num_uzi_ammo;
			else if (objmeup == BOTTLE)
				nummeup = lara.bottle;
			else if (objmeup == PICKUP_ITEM4)
				nummeup = savegame.Level.Secrets;

			if (!nummeup)
				sprintf(textbufme, SCRIPT_TEXT(inventry_objects_list[rings[ringnum]->current_object_list[n].invitem].objname));
			else if (objmeup == PICKUP_ITEM4)
				sprintf(textbufme, SCRIPT_TEXT(TXT_DELSECRET), nummeup, wanky_secrets_table[gfLevelId]);
			else if (nummeup == -1)
				sprintf(textbufme, SCRIPT_TEXT(TXT_Unlimited_s), SCRIPT_TEXT(inventry_objects_list[rings[ringnum]->current_object_list[n].invitem].objname));
			else
				sprintf(textbufme, "%d x %s", nummeup, SCRIPT_TEXT(inventry_objects_list[rings[ringnum]->current_object_list[n].invitem].objname));

			if (ringnum == RING_INVENTORY)
				ymeup = long(phd_centery - (phd_winymax + 1) / 16.0F * 3.0F);
			else
				ymeup = long((phd_winymax + 1) / 16.0F * 3.0F + phd_centery);

			PrintString(phd_centerx, ymeup, 8, textbufme, FF_CENTER);
		}

		// The spin and the brightness are stepped on the logic frame and
		// drawn from the blend on every frame.
		if (invStep)
		{
			if (!i && !move)
			{
				if (inventry_objects_list[rings[ringnum]->current_object_list[n].invitem].flags & 2)
					rings[ringnum]->current_object_list[n].yrot += 1022;
			}
			else
				spinback(&rings[ringnum]->current_object_list[n].yrot);
		}

		yrot = (short)InvDrawSpin(ringnum, n, rings[ringnum]->current_object_list[n].invitem,
			rings[ringnum]->current_object_list[n].yrot);

		if (!move)
			activenum = 0;
		else if (move > 0)
			activenum = -1;
		else
			activenum = 1;

		if (invStep)
		{
			if (i == activenum)
			{
				if (rings[ringnum]->current_object_list[n].bright < 160)
					rings[ringnum]->current_object_list[n].bright += 16;

				if (rings[ringnum]->current_object_list[n].bright > 160)
					rings[ringnum]->current_object_list[n].bright = 160;
			}
			else
			{
				if (rings[ringnum]->current_object_list[n].bright > 32)
					rings[ringnum]->current_object_list[n].bright -= 16;

				if (rings[ringnum]->current_object_list[n].bright < 32)
					rings[ringnum]->current_object_list[n].bright = 32;
			}
		}

		x = xoff + phd_centerx + i * OBJLIST_SPACING;
		y = ringnum == RING_INVENTORY ? GetFixedScale(84) : GetFixedScale(380);
		DrawThreeDeeObject2D(x, y, rings[ringnum]->current_object_list[n].invitem, shade, 0, yrot, 0,
			InvDrawBright(ringnum, n, rings[ringnum]->current_object_list[n].invitem,
				rings[ringnum]->current_object_list[n].bright), 0);

		n++;

		if (n >= rings[ringnum]->numobjectsinlist)
			n = 0;
	}

	/* The slide is stepped last, where vanilla steps it. It is the ring's
	 * own state - the row's place, the selected slot, the sound of a turn -
	 * so it moves on the logic frame and on no other. What the picture above
	 * drew with is `move`, the blend of the two steps before this one, for
	 * the reason given above. */
	if (rings[ringnum]->ringactive && invStep)
	{
		if (rings[ringnum]->numobjectsinlist != 1 && (ringnum != 1 || combine_ring_fade_val == 128))
		{
			if (rings[ringnum]->objlistmovement > 0)
				rings[ringnum]->objlistmovement += 8192;

			if (rings[ringnum]->objlistmovement < 0)
				rings[ringnum]->objlistmovement -= 8192;

			if (go_left && !rings[ringnum]->objlistmovement)
			{
				SoundEffect(SFX_MENU_ROTATE, 0, SFX_ALWAYS);
				rings[ringnum]->objlistmovement += 8192;

				if (ammo_selector_flag)
					ammo_selector_fade_dir = 2;
			}

			if (go_right && !rings[ringnum]->objlistmovement)
			{
				SoundEffect(SFX_MENU_ROTATE, 0, SFX_ALWAYS);
				rings[ringnum]->objlistmovement -= 8192;

				if (ammo_selector_flag)
					ammo_selector_fade_dir = 2;
			}

			if (rings[ringnum]->objlistmovement >= 65536)
			{
				rings[ringnum]->curobjinlist--;

				if (rings[ringnum]->curobjinlist < 0)
					rings[ringnum]->curobjinlist = rings[ringnum]->numobjectsinlist - 1;

				rings[ringnum]->objlistmovement = 0;

				if (ringnum == RING_INVENTORY)
					handle_object_changeover(0);
			}
			else if (rings[ringnum]->objlistmovement < -65535)
			{
				rings[ringnum]->curobjinlist++;

				if (rings[ringnum]->curobjinlist >= rings[ringnum]->numobjectsinlist)
					rings[ringnum]->curobjinlist = 0;

				rings[ringnum]->objlistmovement = 0;

				if (ringnum == RING_INVENTORY)
					handle_object_changeover(0);
			}
		}
	}
}

void handle_object_changeover(long ringnum)
{
	current_selected_option = 0;
	menu_active = 1;
	setup_ammo_selector();
}

void handle_inventry_menu()
{
	long n, opts, ypos, num;
	short obj1, obj2;

	if (rings[RING_AMMO]->ringactive)
	{
		PrintString(phd_centerx, phd_centery, 1, SCRIPT_TEXT(optmessages[5]), FF_CENTER);

		if (rings[RING_INVENTORY]->objlistmovement || rings[RING_AMMO]->objlistmovement)
			return;

		if (go_select)
		{
			obj1 = rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem;
			obj2 = rings[RING_AMMO]->current_object_list[rings[RING_AMMO]->curobjinlist].invitem;

			if (do_these_objects_combine(obj1, obj2))
			{
				combine_ring_fade_dir = 2;
				combine_type_flag = 1;
				combine_obj1 = rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem;
				combine_obj2 = rings[RING_AMMO]->current_object_list[rings[RING_AMMO]->curobjinlist].invitem;
				SoundEffect(SFX_MENU_COMBINE, 0, SFX_ALWAYS);
			}
			else
			{
				SayNo();
				combine_ring_fade_dir = 2;
			}
		}

		if (go_deselect)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			combine_ring_fade_dir = 2;
			go_deselect = 0;
		}

		return;
	}

	num = rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem;
	opts = options_table[num];

	for (int i = 0; i < 3; i++)
	{
		current_options[i].type = ITYPE_EMPTY;
		current_options[i].text = 0;
	}

	n = 0;

	if (!ammo_active)
	{
		if (opts & OPT_LOAD)
		{
			current_options[n].type = ITYPE_LOAD;
			current_options[n++].text = SCRIPT_TEXT(optmessages[6]);
		}

		if (opts & OPT_SAVE)
		{
			current_options[n].type = ITYPE_SAVE;
			current_options[n++].text = SCRIPT_TEXT(optmessages[7]);
		}

		if (opts & OPT_EXAMINE)
		{
			current_options[n].type = ITYPE_EXAMINE;
			current_options[n++].text = SCRIPT_TEXT(optmessages[8]);
		}

		if (opts & OPT_STATS)
		{
			current_options[n].type = ITYPE_STATS;
			current_options[n++].text = SCRIPT_TEXT(optmessages[9]);
		}

		if (opts & OPT_USE)
		{
			current_options[n].type = ITYPE_USE;
			current_options[n++].text = SCRIPT_TEXT(optmessages[0]);
		}

		if (opts & OPT_EQUIP)
		{
			current_options[n].type = ITYPE_EQUIP;
			current_options[n++].text = SCRIPT_TEXT(optmessages[4]);
		}

		if (opts & (OPT_SHOTGUN | OPT_CROSSBOW))
		{
			current_options[n].type = ITYPE_CHOOSEAMMO;
			current_options[n++].text = SCRIPT_TEXT(optmessages[1]);
		}

		if (opts & OPT_HK)
		{
			current_options[n].type = ITYPE_CHOOSEAMMO;
			current_options[n++].text = SCRIPT_TEXT(optmessages[10]);
		}

		if (opts & OPT_COMBINE)
		{
			if (is_item_currently_combinable((short)num))
			{
				current_options[n].type = ITYPE_COMBINE;
				current_options[n++].text = SCRIPT_TEXT(optmessages[2]);
			}
		}

		if (opts & OPT_UNUSED)
		{
			current_options[n].type = ITYPE_COMBINE;
			current_options[n++].text = SCRIPT_TEXT(optmessages[2]);
		}

		if (opts & OPT_SEPARATE)
		{
			current_options[n].type = ITYPE_SEPERATE;
			current_options[n++].text = SCRIPT_TEXT(optmessages[3]);
		}
	}
	else
	{
		current_options[n].type = ITYPE_AMMO1;
		current_options[n++].text = SCRIPT_TEXT(inventry_objects_list[ammo_object_list[0].invitem].objname);
		current_options[n].type = ITYPE_AMMO2;
		current_options[n++].text = SCRIPT_TEXT(inventry_objects_list[ammo_object_list[1].invitem].objname);

		if (opts & OPT_HK)
		{
			current_options[n].type = ITYPE_AMMO3;
			current_options[n++].text = SCRIPT_TEXT(inventry_objects_list[ammo_object_list[2].invitem].objname);
		}

		current_selected_option = current_ammo_type[0];
	}

	ypos = phd_centery - font_height;

	if (n == 1)
		ypos += font_height;
	else if (n == 2)
		ypos += font_height >> 1;

	for (int i = 0; i < n; i++)
	{
		PrintString(phd_centerx, ypos, i == current_selected_option ? 1 : 5, current_options[i].text, FF_CENTER);
		ypos += font_height;
	}

	if (menu_active && !rings[RING_INVENTORY]->objlistmovement && !rings[RING_AMMO]->objlistmovement)
	{
		if (go_up && current_selected_option > 0)
		{
			current_selected_option--;
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		}
		else if (go_down && current_selected_option < n - 1)
		{
			current_selected_option++;
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		}

		if (ammo_active)
		{
			if (go_left && current_selected_option > 0)
			{
				current_selected_option--;
				SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			}

			if (go_right && current_selected_option < n - 1)
			{
				current_selected_option++;
				SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			}

			current_ammo_type[0] = current_selected_option;
		}

		if (go_select)
		{
			if (current_options[current_selected_option].type != ITYPE_EQUIP && current_options[current_selected_option].type != ITYPE_USE)
				SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);

			/* Every one of these is a cut: a mode opening, the ammo ring
			 * taking over, the combine ring being built, the load or save
			 * screen. What is on screen after one of them is not a neighbour
			 * of what was on screen before it, so the ring's snapshot is
			 * dropped and the next step frame draws live. One call at the
			 * head of the switch covers all of them. */
			if (current_options[current_selected_option].type != ITYPE_EMPTY)
				InvInterpCut("menu");

			switch (current_options[current_selected_option].type)
			{
			case ITYPE_CHOOSEAMMO:
				rings[RING_INVENTORY]->ringactive = 0;
				ammo_active = 1;
				Stashedcurrent_selected_option = current_selected_option;
				StashedCurrentPistolsAmmoType = CurrentPistolsAmmoType;
				StashedCurrentUziAmmoType = CurrentUziAmmoType;
				StashedCurrentRevolverAmmoType = CurrentRevolverAmmoType;
				StashedCurrentShotGunAmmoType = CurrentShotGunAmmoType;
				StashedCurrentGrenadeGunAmmoType = CurrentGrenadeGunAmmoType;
				break;

			case ITYPE_LOAD:
				loading_or_saving = 1;
				break;

			case ITYPE_SAVE:
				loading_or_saving = 2;
				break;

			case ITYPE_EXAMINE:
				examine_mode = 1;
				break;

			case ITYPE_STATS:
				stats_mode = 1;
				break;

			case ITYPE_AMMO1:
			case ITYPE_AMMO2:
			case ITYPE_AMMO3:
				ammo_active = 0;
				rings[RING_INVENTORY]->ringactive = 1;
				current_selected_option = 0;
				break;

			case ITYPE_COMBINE:
				construct_combine_object_list();
				rings[RING_INVENTORY]->ringactive = 0;
				rings[RING_AMMO]->ringactive = 1;
				ammo_selector_flag = 0;
				menu_active = 0;
				combine_ring_fade_dir = 1;
				break;

			case ITYPE_SEPERATE:
				seperate_type_flag = 1;
				normal_ring_fade_dir = 2;
				break;

			case ITYPE_EQUIP:
			case ITYPE_USE:
				menu_active = 0;
				use_the_bitch = 1;
				break;
			}
		}

		if (go_deselect && ammo_active)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			InvInterpCut("ammo_close");	// closing the ammo selector is a cut, not a step
			go_deselect = 0;
			ammo_active = 0;
			rings[RING_INVENTORY]->ringactive = 1;
			CurrentPistolsAmmoType = StashedCurrentPistolsAmmoType;
			CurrentUziAmmoType = StashedCurrentUziAmmoType;
			CurrentRevolverAmmoType = StashedCurrentRevolverAmmoType;
			CurrentShotGunAmmoType = StashedCurrentShotGunAmmoType;
			CurrentGrenadeGunAmmoType = StashedCurrentGrenadeGunAmmoType;
			CurrentCrossBowAmmoType = StashedCurrentCrossBowAmmoType;
			current_selected_option = Stashedcurrent_selected_option;
		}
	}
}

void setup_ammo_selector()
{
	long n, opts;

	n = 0;
	opts = options_table[rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem];
	ammo_selector_flag = 0;
	num_ammo_slots = 0;

	if (rings[RING_AMMO]->ringactive)
		return;

	ammo_object_list[0].yrot = 0;
	ammo_object_list[1].yrot = 0;
	ammo_object_list[2].yrot = 0;

	if (!(opts & (OPT_SHOTGUN | OPT_CROSSBOW | OPT_HK | OPT_UZI | OPT_PISTOLS | OPT_REVOLVER | OPT_GRAPPLE)))
		return;

	ammo_selector_flag = 1;
	ammo_selector_fade_dir = 1;

	if (opts & OPT_UZI)
	{
		ammo_object_list[0].invitem = INV_UZI_AMMO_ITEM;
		ammo_object_list[0].amount = AmountUziAmmo;
		num_ammo_slots = 1;
		current_ammo_type = &CurrentUziAmmoType;
	}

	if (opts & OPT_PISTOLS)
	{
		ammo_object_list[0].invitem = INV_PISTOLS_AMMO_ITEM;
		ammo_object_list[0].amount = -1;
		num_ammo_slots = 1;
		current_ammo_type = &CurrentPistolsAmmoType;
	}

	if (opts & OPT_REVOLVER)
	{
		ammo_object_list[0].invitem = INV_REVOLVER_AMMO_ITEM;
		ammo_object_list[0].amount = AmountRevolverAmmo;
		num_ammo_slots = 1;
		current_ammo_type = &CurrentRevolverAmmoType;
	}

	if (opts & OPT_CROSSBOW)
	{
		ammo_object_list[n].invitem = INV_CROSSBOW_AMMO2_ITEM3;
		ammo_object_list[n++].amount = AmountCrossBowAmmo1;
		ammo_object_list[n].invitem = INV_CROSSBOW_AMMO2_ITEM4;
		ammo_object_list[n++].amount = AmountCrossBowAmmo2;
		num_ammo_slots = (char)n;
		current_ammo_type = &CurrentCrossBowAmmoType;
	}

	if (opts & OPT_HK)
	{
		ammo_object_list[n].invitem = INV_HK_AMMO_ITEM1;
		ammo_object_list[n++].amount = AmountHKAmmo1;
		ammo_object_list[n].invitem = INV_HK_AMMO_ITEM2;
		ammo_object_list[n++].amount = AmountHKAmmo1;
		ammo_object_list[n].invitem = INV_HK_AMMO_ITEM3;
		ammo_object_list[n++].amount = AmountHKAmmo1;
		num_ammo_slots = (char)n;
		current_ammo_type = &CurrentGrenadeGunAmmoType;
	}

	if (opts & OPT_SHOTGUN)
	{
		ammo_object_list[n].invitem = INV_SHOTGUN_AMMO1_ITEM;
		ammo_object_list[n++].amount = AmountShotGunAmmo1;
		ammo_object_list[n].invitem = INV_SHOTGUN_AMMO2_ITEM;
		ammo_object_list[n++].amount = AmountShotGunAmmo2;
		num_ammo_slots = (char)n;
		current_ammo_type = &CurrentShotGunAmmoType;
	}

	if (opts & OPT_GRAPPLE)
	{
		ammo_object_list[0].invitem = INV_CROSSBOW_AMMO1_ITEM;
		ammo_object_list[0].amount = AmountCrossBowAmmo1;
		num_ammo_slots = 1;
		current_ammo_type = &CurrentCrossBowAmmoType;
	}
}

void fade_ammo_selector()
{
	/* The ammo selector's fade is stepped here, after draw_ammo_selector has
	 * already drawn with it - so its pair is the two previous generations
	 * and the step is on the logic frame. */
	if (InterpIsTweenFrame())
		return;

	if (rings[RING_INVENTORY]->ringactive && (right_repeat >= 8 || left_repeat >= 8))
		ammo_selector_fade_val = 0;
	else if (ammo_selector_fade_dir == 1)
	{
		if (ammo_selector_fade_val < 128)
			ammo_selector_fade_val += 32;

		if (ammo_selector_fade_val > 128)
		{
			ammo_selector_fade_val = 128;
			ammo_selector_fade_dir = 0;
		}
	}
	else if (ammo_selector_fade_dir == 2)
	{
		if (ammo_selector_fade_val > 0)
			ammo_selector_fade_val -= 32;

		if (ammo_selector_fade_val < 0)
		{
			ammo_selector_fade_val = 0;
			ammo_selector_fade_dir = 0;
		}
	}
}

void draw_ammo_selector()
{
	INVOBJ* objme;
	long xpos, x, y;
	short yrot, invitem;
	char cunter[256];

	if (!ammo_selector_flag)
		return;

	xpos = (2 * phd_centerx - OBJLIST_SPACING) >> 1;

	if (num_ammo_slots == 2)
		xpos -= OBJLIST_SPACING / 2;
	else if (num_ammo_slots == 3)
		xpos -= OBJLIST_SPACING;

	// Stepped on the logic frame, drawn from the blend on every frame.
	const bool invStep = !InterpIsTweenFrame();
	const long fade = InvDrawAmmoFade(ammo_selector_fade_val);

	for (int i = 0; i < num_ammo_slots; i++)
	{
		invitem = ammo_object_list[i].invitem;
		objme = &inventry_objects_list[invitem];

		if (invStep)
		{
			if (i == current_ammo_type[0])
			{
				if (objme->flags & 2)
					ammo_object_list[i].yrot += 1022;
			}
			else
				spinback(&ammo_object_list[i].yrot);
		}

		yrot = (short)InvDrawAmmoSpin(i, invitem, ammo_object_list[i].yrot);

		if (i == current_ammo_type[0])
		{
			if (ammo_object_list[i].amount == -1)
				sprintf(cunter, SCRIPT_TEXT(TXT_Unlimited_s), SCRIPT_TEXT(inventry_objects_list[invitem].objname));
			else
				sprintf(cunter, "%d x %s", ammo_object_list[i].amount, SCRIPT_TEXT(inventry_objects_list[invitem].objname));

			if (ammo_selector_fade_val)
				PrintString(phd_centerx, phd_centery + 3 * font_height - 9, 8, cunter, FF_CENTER);
		}

		x = xpos + GetFixedScale(80);
		y = GetFixedScale(380);
		DrawThreeDeeObject2D(x, y, invitem, fade, 0, yrot, 0, (i == current_ammo_type[0]) ? 0 : 1, 0);
		xpos += OBJLIST_SPACING;
	}
}

void spinback(ushort* cock)
{
	ushort val, val2;

	val = *cock;

	if (val)
	{
		if (val <= 32768)
		{
			val2 = val;

			if (val2 < 1022)
				val = 1022;
			else if (val2 > 16384)
				val2 = 16384;

			val -= (val2 >> 3);

			if (val > 32768)
				val = 0;
		}
		else
		{
			val2 = -val;

			if (val2 < 1022)
				val = 1022;
			else if (val2 > 16384)
				val2 = 16384;

			val += (val2 >> 3);

			if (val < 32768)
				val = 0;
		}

		*cock = val;
	}
}

void update_laras_weapons_status()
{
	if (lara.shotgun_type_carried & W_PRESENT)
	{
		lara.shotgun_type_carried &= ~(W_AMMO1 | W_AMMO2 | W_AMMO3);

		if (CurrentShotGunAmmoType)
			lara.shotgun_type_carried |= W_AMMO2;
		else
			lara.shotgun_type_carried |= W_AMMO1;
	}

	if (lara.hk_type_carried & W_PRESENT)
	{
		lara.hk_type_carried &= ~(W_AMMO1 | W_AMMO2 | W_AMMO3);

		if (CurrentGrenadeGunAmmoType == 0)
			lara.hk_type_carried |= W_AMMO1;
		else if (CurrentGrenadeGunAmmoType == 1)
			lara.hk_type_carried |= W_AMMO2;
		else if (CurrentGrenadeGunAmmoType == 2)
			lara.hk_type_carried |= W_AMMO3;
	}

	if (lara.crossbow_type_carried & W_PRESENT)
	{
		lara.crossbow_type_carried &= ~(W_AMMO1 | W_AMMO2 | W_AMMO3);

		if (CurrentCrossBowAmmoType)
			lara.crossbow_type_carried |= W_AMMO2;
		else
			lara.crossbow_type_carried |= W_AMMO1;
	}
}

long is_item_currently_combinable(short obj)
{
	for (int i = 0; i < 24; i++)
	{
		if (dels_handy_combine_table[i].item1 == obj && have_i_got_item(dels_handy_combine_table[i].item2))
			return 1;

		if (dels_handy_combine_table[i].item2 == obj && have_i_got_item(dels_handy_combine_table[i].item1))
			return 1;
	}

	return 0;
}

long have_i_got_item(short obj)
{
	for (int i = 0; i < NUM_INVOBJ; i++)
	{
		if (rings[RING_INVENTORY]->current_object_list[i].invitem == obj)
			return 1;
	}

	return 0;
}

long do_these_objects_combine(long obj1, long obj2)
{
	for (int i = 0; i < 24; i++)
	{
		if (dels_handy_combine_table[i].item1 == obj1 && dels_handy_combine_table[i].item2 == obj2)
			return 1;

		if (dels_handy_combine_table[i].item1 == obj2 && dels_handy_combine_table[i].item2 == obj1)
			return 1;
	}

	return 0;
}

void combine_these_two_objects(short obj1, short obj2)
{
	long n;

	for (n = 0; n < 24; n++)
	{
		if (dels_handy_combine_table[n].item1 == obj1 && dels_handy_combine_table[n].item2 == obj2)
			break;

		if (dels_handy_combine_table[n].item1 == obj2 && dels_handy_combine_table[n].item2 == obj1)
			break;
	}

	dels_handy_combine_table[n].combine_routine(0);
	construct_object_list();
	setup_objectlist_startposition(dels_handy_combine_table[n].combined_item);
	handle_object_changeover(0);
}

void seperate_object(short obj)
{
	long n;

	for (n = 0; n < 24; n++)
	{
		if (dels_handy_combine_table[n].combined_item == obj)
			break;
	}

	dels_handy_combine_table[n].combine_routine(1);
	construct_object_list();
	setup_objectlist_startposition(dels_handy_combine_table[n].item1);
}

void combine_HK_SILENCER(long flag)
{
	if (flag)
	{
		lara.silencer = 1;
		lara.hk_type_carried &= ~W_SILENCER;
		
	}
	else
	{
		lara.silencer = 0;
		lara.hk_type_carried |= W_SILENCER;
	}
}

void combine_revolver_lasersight(long flag)
{
	if (flag)
	{
		lara.lasersight = 1;
		lara.sixshooter_type_carried &= ~W_LASERSIGHT;
	}
	else
	{
		lara.lasersight = 0;
		lara.sixshooter_type_carried |= W_LASERSIGHT;
	}

	if (lara.gun_status && lara.gun_type == WEAPON_REVOLVER)
	{
		undraw_pistol_mesh_right(WEAPON_REVOLVER);
		draw_pistol_meshes(WEAPON_REVOLVER);
	}
}

void combine_crossbow_lasersight(long flag)
{
	if (flag)
	{
		lara.lasersight = 1;
		lara.crossbow_type_carried &= ~W_LASERSIGHT;
	}
	else
	{
		lara.lasersight = 0;
		lara.crossbow_type_carried |= W_LASERSIGHT;
	}

	if (lara.gun_status && lara.gun_type == WEAPON_CROSSBOW)
	{
		undraw_shotgun_meshes(WEAPON_CROSSBOW);
		draw_shotgun_meshes(WEAPON_CROSSBOW);
	}
}

void combine_PuzzleItem1(long flag)
{
	lara.puzzleitemscombo &= ~3;
	lara.puzzleitems[0] = 1;
}

void combine_PuzzleItem2(long flag)
{
	lara.puzzleitemscombo &= ~0xC;
	lara.puzzleitems[1] = 1;
}

void combine_PuzzleItem3(long flag)
{
	lara.puzzleitemscombo &= ~0x30;
	lara.puzzleitems[2] = 1;
}

void combine_PuzzleItem4(long flag)
{
	lara.puzzleitemscombo &= ~0xC0;
	lara.puzzleitems[3] = 1;
}

void combine_PuzzleItem5(long flag)
{
	lara.puzzleitemscombo &= ~0x300;
	lara.puzzleitems[4] = 1;
}

void combine_PuzzleItem6(long flag)
{
	lara.puzzleitemscombo &= ~0xC00;
	lara.puzzleitems[5] = 1;
}

void combine_PuzzleItem7(long flag)
{
	lara.puzzleitemscombo &= ~0x3000;
	lara.puzzleitems[6] = 1;
}

void combine_PuzzleItem8(long flag)
{
	lara.puzzleitemscombo &= ~0xC000;
	lara.puzzleitems[7] = 1;
}

// Keys are counted, not flagged (LARA_INFO::keyitems, global\types.h).
// Every place that hands Lara a key goes through here, so the 0..255 ceiling
// of the one-byte counter is enforced in exactly one spot and can never wrap
// back to zero - which would look to the rest of the engine like "key lost".
static void add_key_item(long slot)
{
	if (slot < 0 || slot > 7)
		return;

	if (lara.keyitems[slot] < 255)
		lara.keyitems[slot]++;
}

// Counterpart for a single consumed copy (a lock swallowing one key). Floored
// at zero for the same reason: 0 - 1 on an unsigned byte is 255, i.e. a lock
// would conjure up 255 keys out of nothing.
static void sub_key_item(long slot)
{
	if (slot < 0 || slot > 7)
		return;

	if (lara.keyitems[slot])
		lara.keyitems[slot]--;
}

// Combining two halves yields one whole key. Deliberately "+1" and not
// "= 1" like the combine_PuzzleItem* siblings above: the bit field this
// replaced could never take a key away either, and with counting keys a
// plain "= 1" would silently discard copies Lara already carries.
void combine_KeyItem1(long flag)
{
	add_key_item(0);
	lara.keyitemscombo &= ~3;
}

void combine_KeyItem2(long flag)
{
	add_key_item(1);
	lara.keyitemscombo &= ~0xC;
}

void combine_KeyItem3(long flag)
{
	add_key_item(2);
	lara.keyitemscombo &= ~0x30;
}

void combine_KeyItem4(long flag)
{
	add_key_item(3);
	lara.keyitemscombo &= ~0xC0;
}

void combine_KeyItem5(long flag)
{
	add_key_item(4);
	lara.keyitemscombo &= ~0x300;
}

void combine_KeyItem6(long flag)
{
	add_key_item(5);
	lara.keyitemscombo &= ~0xC00;
}

void combine_KeyItem7(long flag)
{
	add_key_item(6);
	lara.keyitemscombo &= ~0x3000;
}

void combine_KeyItem8(long flag)
{
	add_key_item(7);
	lara.keyitemscombo &= ~0xC000;
}

void combine_PickupItem1(long flag)
{
	lara.pickupitems |= 1;
	lara.pickupitemscombo &= ~3;
}

void combine_PickupItem2(long flag)
{
	lara.pickupitems |= 2;
	lara.pickupitemscombo &= ~0xC;
}

void combine_PickupItem3(long flag)
{
	lara.pickupitems |= 4;
	lara.pickupitemscombo &= ~0x30;
}

void combine_PickupItem4(long flag)
{
	lara.pickupitems |= 8;
	lara.pickupitemscombo &= ~0xC0;
}

void combine_clothbottle(long flag)
{
	lara.wetcloth = CLOTH_WET;
	lara.bottle--;
}

void setup_objectlist_startposition(short newobj)
{
	for (int i = 0; i < NUM_INVOBJ; i++)
	{
		if (rings[RING_INVENTORY]->current_object_list[i].invitem == newobj)
			rings[RING_INVENTORY]->curobjinlist = i;
	}
}

void setup_objectlist_startposition2(short newobj)
{
	short invitem;

	// The ring's slots past numobjectsinlist hold NO_ITEM, and that -1
	// indexed the table 20 bytes before its start. The walk keeps the
	// sibling's bound above and skips them: an empty slot is never the
	// object asked for, as it never matches the sibling's test either.
	for (int i = 0; i < NUM_INVOBJ; i++)
	{
		invitem = rings[RING_INVENTORY]->current_object_list[i].invitem;

		if (invitem >= 0 && inventry_objects_list[invitem].object_number == newobj)
			rings[RING_INVENTORY]->curobjinlist = i;
	}
}

void use_current_item()
{
	long OldBinocular;
	short invobject, gmeobject, state;

	OldBinocular = BinocularRange;
	oldLaraBusy = 0;
	BinocularRange = 0;
	lara_item->mesh_bits = -1;
	invobject = rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem;
	gmeobject = inventry_objects_list[invobject].object_number;

	if (gfLevelId == LVL5_DEEPSEA_DIVE && gmeobject == PUZZLE_ITEM1)
	{
		FireChaff();
		return;
	}

	if (lara.water_status == LW_ABOVE_WATER || lara.water_status == LW_WADE)
	{
		if (gmeobject == PISTOLS_ITEM)
		{
			lara.request_gun_type = WEAPON_PISTOLS;

			if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_PISTOLS)
				lara.gun_status = LG_DRAW_GUNS;

			return;
		}

		if (gmeobject == UZI_ITEM)
		{
			lara.request_gun_type = WEAPON_UZI;

			if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_UZI)
				lara.gun_status = LG_DRAW_GUNS;

			return;
		}

		if (gmeobject == SHOTGUN_ITEM || gmeobject == REVOLVER_ITEM || gmeobject == HK_ITEM || gmeobject == CROSSBOW_ITEM)
		{
			state = lara_item->current_anim_state;

			if (lara.gun_status == LG_HANDS_BUSY || state == AS_ALL4S || state == AS_CRAWL || state == AS_ALL4TURNL || state == AS_ALL4TURNR ||
				state == AS_CRAWLBACK || state == AS_CRAWL2HANG || state == AS_DUCK || state == AS_DUCKROTL || state == AS_DUCKROTR)
			{
				SayNo();
				return;
			}

			if (gmeobject == SHOTGUN_ITEM)
			{
				lara.request_gun_type = WEAPON_SHOTGUN;

				if (lara.gun_status == LG_NO_ARMS && lara.gun_type == SHOTGUN_ITEM)
					lara.gun_status = LG_DRAW_GUNS;
			}
			else if (gmeobject == REVOLVER_ITEM)
			{
				lara.request_gun_type = WEAPON_REVOLVER;

				if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_REVOLVER)
					lara.gun_status = LG_DRAW_GUNS;
			}
			else if (gmeobject == HK_ITEM)
			{
				lara.request_gun_type = WEAPON_HK;

				if (lara.gun_status == LG_NO_ARMS && lara.gun_type == HK_ITEM)
					lara.gun_status = LG_DRAW_GUNS;
			}
			else
			{
				lara.request_gun_type = WEAPON_CROSSBOW;

				if (lara.gun_status == LG_NO_ARMS && lara.gun_type == WEAPON_CROSSBOW)
					lara.gun_status = LG_DRAW_GUNS;
			}

			return;
		}
	}

	if (gmeobject == FLARE_INV_ITEM)
	{
		state = lara_item->current_anim_state;

		if (lara.gun_status != LG_NO_ARMS || state == AS_ALL4S || state == AS_CRAWL || state == AS_ALL4TURNL || state == AS_ALL4TURNR || state == AS_CRAWLBACK || state == AS_CRAWL2HANG)
			SayNo();
		else if (lara.gun_type != WEAPON_FLARE)
		{
			input = IN_FLARE;
			LaraGun();
			input = 0;
		}

		return;
	}

	if (invobject == INV_BINOCULARS_ITEM)
	{
		if ((lara_item->current_anim_state == AS_STOP && lara_item->anim_number == ANIM_BREATH ||
			lara.IsDucked && !(input & IN_DUCK)) && !SniperCamActive && !bUseSpotCam && !bTrackCamInit)
		{
			oldLaraBusy = 1;
			BinocularRange = 128;

			if (lara.gun_status != LG_NO_ARMS)
				lara.gun_status = LG_UNDRAW_GUNS;
		}

		if (OldBinocular)
			BinocularRange = OldBinocular;
		else
			BinocularOldCamera = camera.old_type;

		return;
	}

	if (invobject == INV_SMALLMEDI_ITEM)
	{
		if ((lara_item->hit_points <= 0 || lara_item->hit_points >= 1000) && !lara.poisoned)
		{
			SayNo();
			return;
		}

		if (lara.num_small_medipack != -1)
			lara.num_small_medipack--;

		lara.dpoisoned = 0;
		lara_item->hit_points += 500;

		if (lara_item->hit_points > 1000)
			lara_item->hit_points = 1000;

		SoundEffect(SFX_MENU_MEDI, 0, SFX_ALWAYS);
		savegame.Game.HealthUsed++;
		return;
	}

	if (invobject == INV_BIGMEDI_ITEM)
	{
		if ((lara_item->hit_points <= 0 || lara_item->hit_points >= 1000) && !lara.poisoned)
		{
			SayNo();
			return;
		}

		if (lara.num_large_medipack != -1)
			lara.num_large_medipack--;

		lara.dpoisoned = 0;
		lara_item->hit_points += 1000;

		if (lara_item->hit_points > 1000)
			lara_item->hit_points = 1000;

		SoundEffect(SFX_MENU_MEDI, 0, SFX_ALWAYS);
		savegame.Game.HealthUsed++;
		return;
	}

	GLOBAL_inventoryitemchosen = gmeobject;
}

void DEL_picked_up_object(short objnum)
{
	switch (objnum)
	{
	case UZI_ITEM:

		if (!(lara.uzis_type_carried & W_PRESENT))
			lara.uzis_type_carried = W_PRESENT | W_AMMO1;

		if (lara.num_uzi_ammo != -1)
			lara.num_uzi_ammo += 30;

		return;

	case PISTOLS_ITEM:

		if (!(lara.uzis_type_carried & W_PRESENT))
			lara.pistols_type_carried = W_PRESENT | W_AMMO1;

		lara.num_pistols_ammo = -1;
		return;

	case SHOTGUN_ITEM:

		if (!(lara.shotgun_type_carried & W_PRESENT))
			lara.shotgun_type_carried = W_PRESENT | W_AMMO1;

		if (lara.num_shotgun_ammo1 != -1)
			lara.num_shotgun_ammo1 += 36;

		return;

	case REVOLVER_ITEM:

		if (!(lara.sixshooter_type_carried & W_PRESENT))
			lara.sixshooter_type_carried = W_PRESENT | W_AMMO1;

		if (lara.num_revolver_ammo != -1)
			lara.num_revolver_ammo += 6;

		return;

	case CROSSBOW_ITEM:

		if (gfLevelId < LVL5_THIRTEENTH_FLOOR || gfLevelId > LVL5_RED_ALERT)
		{
			if (!(lara.crossbow_type_carried & W_PRESENT))
				lara.crossbow_type_carried = W_PRESENT | W_AMMO1;

			if (lara.num_crossbow_ammo1 != -1)
				lara.num_crossbow_ammo1 += 10;
		}
		else
		{
			lara.crossbow_type_carried = W_PRESENT | W_LASERSIGHT | W_AMMO1;
			lara.num_crossbow_ammo2 = 0;
		}

		return;

	case HK_ITEM:
		SetCutNotPlayed(23);

		if (!(lara.hk_type_carried & W_PRESENT))
			lara.hk_type_carried = W_PRESENT | W_AMMO1;

		if (gfLevelId != LVL5_ESCAPE_WITH_THE_IRIS)
			if (lara.num_hk_ammo1 != -1)
				lara.num_hk_ammo1 += 30;

		return;

	case SHOTGUN_AMMO1_ITEM:

		if (lara.num_shotgun_ammo1 != -1)
			lara.num_shotgun_ammo1 += 36;

		return;

	case SHOTGUN_AMMO2_ITEM:

		if (lara.num_shotgun_ammo2 != -1)
			lara.num_shotgun_ammo2 += 36;

		return;

	case HK_AMMO_ITEM:

		if (lara.num_hk_ammo1 != -1)
			lara.num_hk_ammo1 += 30;

		return;

	case CROSSBOW_AMMO1_ITEM:

		if (lara.num_crossbow_ammo1 != -1)
			lara.num_crossbow_ammo1++;

		return;

	case CROSSBOW_AMMO2_ITEM:

		if (lara.num_crossbow_ammo2 != -1)
			lara.num_crossbow_ammo2 += 10;

		return;

	case REVOLVER_AMMO_ITEM:

		if (lara.num_revolver_ammo != -1)
			lara.num_revolver_ammo += 6;

		return;

	case UZI_AMMO_ITEM:

		if (lara.num_uzi_ammo != -1)
			lara.num_uzi_ammo += 30;

		return;
		
	case FLARE_INV_ITEM:

		if (lara.num_flares != -1)
			lara.num_flares += 12;

		return;

	case SILENCER_ITEM:

		if (!((lara.uzis_type_carried | lara.pistols_type_carried | lara.shotgun_type_carried | lara.sixshooter_type_carried |
			lara.crossbow_type_carried | lara.hk_type_carried) & W_SILENCER))
			lara.silencer = 1;

		return;

	case LASERSIGHT_ITEM:

		if (!((lara.uzis_type_carried | lara.pistols_type_carried | lara.shotgun_type_carried | lara.sixshooter_type_carried |
			lara.crossbow_type_carried | lara.hk_type_carried) & W_LASERSIGHT))
			lara.lasersight = 1;

		return;

	case BIGMEDI_ITEM:

		if (lara.num_large_medipack != -1)
			lara.num_large_medipack++;

		return;

	case SMALLMEDI_ITEM:

		if (lara.num_small_medipack != -1)
			lara.num_small_medipack++;

		return;

	case BINOCULARS_ITEM:
		lara.binoculars = 1;
		return;

	case PICKUP_ITEM4:
		IsAtmospherePlaying = 0;
		S_CDPlay(6, 0);
		lara.pickupitems |= 8;
		savegame.Level.Secrets++;
		savegame.Game.Secrets++;

		if (gfLevelId >= LVL5_THIRTEENTH_FLOOR && gfLevelId <= LVL5_RED_ALERT)
			savegame.CampaignSecrets[3]++;
		else if (gfLevelId >= LVL5_BASE && gfLevelId <= LVL5_SINKING_SUBMARINE)
			savegame.CampaignSecrets[2]++;
		else if (gfLevelId >= LVL5_STREETS_OF_ROME && gfLevelId <= LVL5_COLOSSEUM)
			savegame.CampaignSecrets[0]++;
		else if (gfLevelId >= LVL5_GALLOWS_TREE && gfLevelId <= LVL5_OLD_MILL)
			savegame.CampaignSecrets[1]++;

		//TR2-style reward for collecting every secret in this level (see
		//GrantSecretBonus above). Deliberately an equality test, so it fires
		//exactly once, on the pickup that completes the set - reloading a save
		//made afterwards cannot re-trigger it, since no further secret pickup
		//happens. A level whose table value it can never reach simply never
		//gets a bonus.
		if (gfLevelId < 18 && savegame.Level.Secrets == wanky_secrets_table[gfLevelId])
			GrantSecretBonus(gfCurrentLevel);

		return;

	case CROWBAR_ITEM:
		lara.crowbar = 1;
		return;

	case EXAMINE1:
		lara.examine1 = 1;
		return;

	case EXAMINE2:
		lara.examine2 = 1;
		return;

	case EXAMINE3:
		lara.examine3 = 1;
		return;

	case WET_CLOTH:
		lara.wetcloth = CLOTH_WET;
		return;

	case CLOTH:
		lara.wetcloth = CLOTH_DRY;
		return;

	case BOTTLE:
		lara.bottle++;
		return;

	default:

		if (objnum >= PICKUP_ITEM1 && objnum <= PICKUP_ITEM3)
			lara.pickupitems |= 1 << (objnum - PICKUP_ITEM1);
		else if (objnum >= PICKUP_ITEM1_COMBO1 && objnum <= PICKUP_ITEM4_COMBO2)
			lara.pickupitemscombo |= 1 << (objnum - PICKUP_ITEM1_COMBO1);
		else if (objnum >= KEY_ITEM1 && objnum <= KEY_ITEM8)
			add_key_item(objnum - KEY_ITEM1);	// count it; a second copy of the same key would otherwise be silently dropped
		else if (objnum >= KEY_ITEM1_COMBO1 && objnum <= KEY_ITEM8_COMBO2)
			lara.keyitemscombo |= 1 << (objnum - KEY_ITEM1_COMBO1);
		else if (objnum >= PUZZLE_ITEM1 && objnum <= PUZZLE_ITEM8)
			lara.puzzleitems[objnum - PUZZLE_ITEM1]++;
		else if (objnum >= PUZZLE_ITEM1_COMBO1 && objnum <= PUZZLE_ITEM8_COMBO2)
			lara.puzzleitemscombo |= 1 << (objnum - PUZZLE_ITEM1_COMBO1);
	}
}

void NailInvItem(short objnum)
{
	switch (objnum)
	{
	case UZI_ITEM:
		lara.uzis_type_carried = W_NONE;
		lara.num_uzi_ammo = 0;
		break;

	case PISTOLS_ITEM:
		LHolster = LARA_HOLSTERS;
		lara.holster = LARA_HOLSTERS;
		lara.pistols_type_carried = W_NONE;
		lara.gun_status = LG_NO_ARMS;
		lara.last_gun_type = WEAPON_NONE;
		lara.gun_type = WEAPON_NONE;
		lara.request_gun_type = WEAPON_NONE;
		break;

	case SHOTGUN_ITEM:
		lara.shotgun_type_carried = W_NONE;
		lara.num_shotgun_ammo1 = 0;
		break;

	case REVOLVER_ITEM:
		lara.sixshooter_type_carried = W_NONE;
		lara.num_revolver_ammo = 0;
		break;

	case CROSSBOW_ITEM:
		lara.crossbow_type_carried = W_NONE;
		lara.num_crossbow_ammo1 = 0;
		break;

	case HK_ITEM:
		lara.hk_type_carried = W_NONE;
		lara.num_hk_ammo1 = 0;
		break;

	case FLARE_INV_ITEM:
		lara.num_flares = 0;
		break;

	case SILENCER_ITEM:
		lara.silencer = W_NONE;
		break;

	case LASERSIGHT_ITEM:
		lara.lasersight = W_NONE;
		break;

	case BIGMEDI_ITEM:
		lara.num_large_medipack = 0;
		break;

	case SMALLMEDI_ITEM:
		lara.num_small_medipack = 0;
		break;

	case BINOCULARS_ITEM:
		lara.binoculars = W_NONE;
		break;

	case CROWBAR_ITEM:
		lara.crowbar = 0;
		break;

	case EXAMINE1:
		lara.examine1 = 0;
		break;

	case EXAMINE2:
		lara.examine2 = 0;
		break;

	case EXAMINE3:
		lara.examine3 = 0;
		break;

	case WET_CLOTH:
		lara.wetcloth = CLOTH_MISSING;
		break;

	case CLOTH:
		lara.wetcloth = CLOTH_MISSING;
		break;

	case BOTTLE:
		lara.bottle = 0;
		break;

	default:

		if (objnum >= PICKUP_ITEM1 && objnum <= PICKUP_ITEM4)
			lara.pickupitems &= ~(1 << (objnum - PICKUP_ITEM1));
		else if (objnum >= PICKUP_ITEM1_COMBO1 && objnum <= PICKUP_ITEM4_COMBO2)
			lara.pickupitemscombo &= ~(1 << (objnum - PICKUP_ITEM1_COMBO1));
		else if (objnum >= KEY_ITEM1 && objnum <= KEY_ITEM8)
			lara.keyitems[objnum - KEY_ITEM1] = 0;	// script takeaway: ALL copies, like puzzleitems below
		else if (objnum >= KEY_ITEM1_COMBO1 && objnum <= KEY_ITEM8_COMBO2)
			lara.keyitemscombo &= ~(1 << (objnum - KEY_ITEM1_COMBO1));
		else if (objnum >= PUZZLE_ITEM1 && objnum <= PUZZLE_ITEM8)
			lara.puzzleitems[objnum - PUZZLE_ITEM1] = 0;
		else if (objnum >= PUZZLE_ITEM1_COMBO1 && objnum <= PUZZLE_ITEM8_COMBO2)
			lara.puzzleitemscombo &= ~(1 << (objnum - PUZZLE_ITEM1_COMBO1));

		break;
	}
}

long have_i_got_object(short object_number)
{
	if (object_number >= PUZZLE_ITEM1_COMBO1 && object_number <= PUZZLE_ITEM8_COMBO2)
		return (lara.puzzleitemscombo >> (object_number - PUZZLE_ITEM1_COMBO1)) & 1;

	if (object_number >= PUZZLE_ITEM1 && object_number <= PUZZLE_ITEM8)
		return lara.puzzleitems[object_number - PUZZLE_ITEM1];

	if (object_number >= KEY_ITEM1_COMBO1 && object_number <= KEY_ITEM8_COMBO2)
		return (lara.keyitemscombo >> (object_number - KEY_ITEM1_COMBO1)) & 1;

	if (object_number >= KEY_ITEM1 && object_number <= KEY_ITEM8)
		return lara.keyitems[object_number - KEY_ITEM1];	// count; every caller uses it as a truth value

	if (object_number >= PICKUP_ITEM1_COMBO1 && object_number <= PICKUP_ITEM4_COMBO2)
		return (lara.pickupitemscombo >> (object_number - PICKUP_ITEM1_COMBO1)) & 1;

	if (object_number >= PICKUP_ITEM1 && object_number <= PICKUP_ITEM4)
		return (lara.pickupitems >> (object_number - PICKUP_ITEM1)) & 1;

	if (object_number == CROWBAR_ITEM)
		return lara.crowbar;

	if (object_number == WET_CLOTH)
		return lara.wetcloth & 2;

	return 0;
}

void remove_inventory_item(short object_number)
{
	if (object_number >= PUZZLE_ITEM1_COMBO1 && object_number <= PUZZLE_ITEM8_COMBO2)
		lara.puzzleitemscombo &= ~(1 << (object_number + 76));

	if (object_number >= PUZZLE_ITEM1 && object_number <= PUZZLE_ITEM8)
		lara.puzzleitems[object_number - PUZZLE_ITEM1]--;

	if (object_number >= KEY_ITEM1_COMBO1 && object_number <= KEY_ITEM8_COMBO2)
		lara.keyitemscombo &= ~(1 << (object_number + 52));

	// One copy, not the whole stack - this is the lock swallowing a key
	// (game\pickup.cpp, KeyHole handling), the exact mirror of the
	// puzzleitems[]-- two blocks up. A counter needs only the plain slot
	// index; the bit field's equivalent relied on the x86 shift masking the
	// count to 5 bits.
	if (object_number >= KEY_ITEM1 && object_number <= KEY_ITEM8)
		sub_key_item(object_number - KEY_ITEM1);

	if (object_number >= PICKUP_ITEM1_COMBO1 && object_number <= PICKUP_ITEM4_COMBO2)
		lara.pickupitemscombo &= ~(1 << (object_number + 32));

	if (object_number >= PICKUP_ITEM1 && object_number <= PICKUP_ITEM4)
		lara.pickupitems &= ~(1 << (object_number + 36));
}

long convert_obj_to_invobj(short obj)
{
	for (int i = 0; i < NUM_INVOBJ; i++)
	{
		if (inventry_objects_list[i].object_number == obj)
			return i;
	}

	return INV_MEMCARD_LOAD_INV_ITEM;
}

long convert_invobj_to_obj(long obj)
{
	return inventry_objects_list[obj].object_number;
}

void init_keypad_mode()
{
	keypadx = 0;
	keypady = 0;
	keypadnuminputs = 0;
	keypadpause = 0;
	keypadinputs[0] = 0;
	keypadinputs[1] = 0;
	keypadinputs[2] = 0;
	keypadinputs[3] = 0;
}

void do_keypad_mode()
{
	INVOBJ* objme;
	ulong meshbits;
	long x, y;
	uchar bits;
	char buf[5];

	objme = &inventry_objects_list[INV_PUZZLE_HOLE8];
	meshbits = 0x1FFF;

	//highlight selected buttons
	for (int i = 0; i < (long)keypadnuminputs; i++)
	{
		bits = keypadinputs[i];

		if (!bits)
			bits = 11;

		meshbits &= ~(1 << bits);			//hide the base mesh
		meshbits |= 1 << (bits + 12);		//show the "selected" mesh
	}

	//flash selection randomly
	bits = (3 * keypady + keypadx) + 1;

	if (!(GnFrameCounter & 2) && !keypadpause)
	{
		meshbits &= ~(1 << (bits + 12));
		meshbits |= 1 << bits;
	}
	else
	{
		meshbits &= ~(1 << bits);
		meshbits |= 1 << (bits + 12);
	}

	objme->meshbits = meshbits;

	x = phd_centerx;
	y = phd_centery + GetFixedScale(16);
	DrawThreeDeeObject2D(x, y, INV_PUZZLE_HOLE8, 128, 0x8000, 0x4000, 0x4000, 0, 0);
	PrintString(x, y - GetFixedScale(64), 6, SCRIPT_TEXT(TXT_keypad), FF_CENTER);

	buf[0] = '-';
	buf[1] = '-';
	buf[2] = '-';
	buf[3] = '-';
	buf[4] = 0;

	for (int i = 0; i < keypadnuminputs; i++)
		buf[i] = keypadinputs[i] + '0';

	PrintString(x, y + GetFixedScale(128), 1, buf, FF_CENTER);

	if (keypadpause)
	{
		/* keypadpause is a one-second countdown in presents - thirty of them
		 * - and the values it is tested against below are the moments it
		 * plays a tone on. Both halves are inside the ring's logic frame: the
		 * countdown so that the second stays a second at any frame rate, and
		 * the tones so that one of them is not played again on every frame of
		 * the step it belongs to. */
		if (!InterpIsTweenFrame())
		{
			x = keypadinputs[0] * 1000 + keypadinputs[1] * 100 + keypadinputs[2] * 10 + keypadinputs[3];

			if (GLOBAL_invkeypadcombination == x)
			{
				if (keypadpause == 30 || keypadpause == 20 || keypadpause == 10)
					SoundEffect(SFX_KEYPAD_ENTRY_YES, 0, SFX_ALWAYS);
			}
			else if (keypadpause == 30 || keypadpause == 25 || keypadpause == 20 || keypadpause == 15 || keypadpause == 10 || keypadpause == 5)
				SoundEffect(SFX_KEYPAD_ENTRY_NO, 0, SFX_ALWAYS | SFX_SETVOL | 0x1000);

			keypadpause--;

			if (!keypadpause)
			{
				menu_active = 0;
				use_the_bitch = 1;
			}
		}

		return;
	}

	if (go_select)
	{
		bits = (3 * keypady + keypadx) + 1;

		if (bits == 10)	//# button
		{
			SoundEffect(SFX_KEYPAD_HASH, 0, SFX_ALWAYS);
			keypadnuminputs = 0;
			return;
		}

		if (bits == 11)			//0 button
			bits = 0;
		else if (bits == 12)	//* button
		{
			keypadpause = 30;
			SoundEffect(SFX_KEYPAD_STAR, 0, SFX_ALWAYS);
			return;
		}

		if (keypadnuminputs == 4)	//already entered all 4 digits
			return;

		for (int i = 0; i < keypadnuminputs; i++)	//can't push the same digit
		{
			if (keypadinputs[i] == bits)
				return;
		}
		
		SoundEffect(SFX_KEYPAD_0 + bits, 0, SFX_ALWAYS);
		keypadinputs[keypadnuminputs++] = bits;

		if (keypadnuminputs == 4)
		{
			keypadx = 2;
			keypady = 3;
		}
	}

	if (go_left && keypadx)
		keypadx--;

	if (go_right && keypadx < 2)
		keypadx++;

	if (go_up && keypady)
		keypady--;

	if (go_down && keypady < 3)
		keypady++;
}

void do_examine_mode()
{
	INVOBJ* objme;
	long x, y;
	short invitem, scale;

	invitem = rings[RING_INVENTORY]->current_object_list[rings[RING_INVENTORY]->curobjinlist].invitem;
	objme = &inventry_objects_list[invitem];
	scale = objme->scale1;

	// The zoom steps on the logic frame and is drawn from the blend.
	if (!InterpIsTweenFrame())
	{
		examine_mode += 8;

		if (examine_mode > 128)
			examine_mode = 128;
	}

	objme->scale1 = 300;
	x = phd_centerx;
	y = GetFixedScale(256);
	DrawThreeDeeObject2D(x, y, invitem, InvDrawExamine(examine_mode), 0x8000, 0x4000, 0x4000, 96, 0);
	objme->scale1 = scale;

	if (go_deselect)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		go_deselect = 0;
		examine_mode = 0;
		InvInterpCut("examine_close");	// closing the examine view is a cut, not a step
	}
}

void do_stats_mode()
{
	/* stats_mode is a counter and nothing reads it as a value - the stats
	 * page is drawn from the savegame, not from a fade - so it is held on a
	 * tween frame rather than blended. It steps once per logic frame, which
	 * is the rate it was written for. */
	if (!InterpIsTweenFrame())
	{
		stats_mode += 8;

		if (stats_mode > 128)
			stats_mode = 128;
	}

	DisplayStatsUCunt();

	if (go_deselect)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		go_deselect = 0;
		stats_mode = 0;
		InvInterpCut("stats_close");	// closing the stats page is a cut, not a step
	}
}

void dels_give_lara_items_cheat()
{
	long piss;

	if (objects[CROWBAR_ITEM].loaded)
		lara.crowbar = 1;

	for (piss = 0; piss < 8; piss++)
	{
		if (objects[PUZZLE_ITEM1 + piss].loaded)
			lara.puzzleitems[piss] = 1;
	}

	for (piss = 0; piss < 8; piss++)
	{
		// "piss" is the slot index 0..7 here, so this only ever touched the
		// eight key bits. Adding a copy rather than forcing the count to 1
		// keeps the old behaviour: the bit field never took a key away, and
		// the ITEM cheat must not start eating the player's stock either.
		if (objects[KEY_ITEM1 + piss].loaded)
			add_key_item(piss);
	}

	for (piss = 0; piss < 3; piss++)
	{
		if (objects[PICKUP_ITEM1 + piss].loaded)
			lara.pickupitems |= (1 << (piss & 0x1F));
	}

	if (gfLevelId == LVL5_SUBMARINE)
	{
		lara.puzzleitems[0] = 0;
		lara.puzzleitemscombo = 0;
		lara.keyitemscombo = 0;
		lara.pickupitemscombo = 0;
	}

	if (gfLevelId == LVL5_OLD_MILL)
	{
		lara.puzzleitems[2] = 0;
		lara.puzzleitemscombo = 0;
		lara.keyitemscombo = 0;
		lara.pickupitemscombo = 0;
	}
}

void dels_give_lara_guns_cheat()
{
	if (objects[FLARE_INV_ITEM].loaded)
		lara.num_flares = -1;

	lara.num_small_medipack = -1;
	lara.num_large_medipack = -1;

	if (!(gfLevelFlags & GF_YOUNGLARA))
	{
		if (objects[SHOTGUN_ITEM].loaded)
		{
			lara.num_shotgun_ammo1 = -1;
			lara.num_shotgun_ammo2 = -1;
			lara.shotgun_type_carried |= W_PRESENT;
		}

		if (objects[REVOLVER_ITEM].loaded)
		{
			lara.num_revolver_ammo = -1;
			lara.sixshooter_type_carried |= W_PRESENT;
		}

		if (objects[CROSSBOW_ITEM].loaded)
		{
			lara.num_crossbow_ammo1 = -1;
			lara.num_crossbow_ammo2 = -1;
			lara.crossbow_type_carried |= W_PRESENT;

			if (gfLevelId < LVL5_GIBBY_LEVEL)
			{
				lara.crossbow_type_carried = W_PRESENT | W_LASERSIGHT | W_AMMO1;
				lara.num_crossbow_ammo2 = 0;
			}
		}

		if (objects[HK_ITEM].loaded)
		{
			lara.num_hk_ammo1 = -1;
			lara.hk_type_carried |= W_PRESENT;
		}

		if (objects[UZI_ITEM].loaded)
		{
			lara.num_uzi_ammo = -1;
			lara.uzis_type_carried |= W_PRESENT;
		}

		if (objects[LASERSIGHT_ITEM].loaded)
			lara.lasersight = 1;

		if (objects[SILENCER_ITEM].loaded)
			lara.silencer = 1;
	}
}

long LoadGame()
{
	return S_LoadSave(IN_LOAD, 1, 1) < 0 ? -1 : 1;
}

long SaveGame()
{
	input = 0;
	dbinput = 0;
	return S_LoadSave(IN_SAVE, 1, 1) < 0 ? -1 : 1;
}

void DelDrawSprite(long x, long y, long def, long z)
{
	SPRITESTRUCT* sprite;
	D3DTLVERTEX v[4];
	TEXTURESTRUCT tex;
	long x1, y1, x2, y2;

	sprite = &spriteinfo[objects[DEFAULT_SPRITES].mesh_index + def];

	if (z >= 200)
		z = long(f_mzfar - 20.0F);
	else
		z = long(f_mznear + 20.0F);

	x1 = long((float)x * (float)phd_centerx * (1.0F / 256.0F));
	x2 = long((float((sprite->width >> 8) + x + 1)) * (float)phd_centerx * (1.0F / 256.0F));
	y1 = long((float)y * (float)phd_centery * (1.0F / 120.0F));
	y2 = long((float((sprite->height >> 8) + y + 1)) * (float)phd_centery * (1.0F / 120.0F));
	setXY4(v, x1, y1, x2, y1, x2, y2, x1, y2, z, clipflags);

	v[0].specular = 0xFF000000;
	v[1].specular = 0xFF000000;
	v[2].specular = 0xFF000000;
	v[3].specular = 0xFF000000;
	v[0].color = 0xFFFFFFFF;
	v[1].color = 0xFFFFFFFF;
	v[2].color = 0xFFFFFFFF;
	v[3].color = 0xFFFFFFFF;

	tex.drawtype = 1;
	tex.flag = 0;
	tex.tpage = sprite->tpage;
	tex.u1 = sprite->x1;
	tex.v1 = sprite->y1;
	tex.u2 = sprite->x2;
	tex.v2 = sprite->y1;
	tex.u3 = sprite->x2;
	tex.v3 = sprite->y2;
	tex.u4 = sprite->x1;
	tex.v4 = sprite->y2;
	AddQuadClippedSorted(v, 0, 1, 2, 3, &tex, 0);
}
