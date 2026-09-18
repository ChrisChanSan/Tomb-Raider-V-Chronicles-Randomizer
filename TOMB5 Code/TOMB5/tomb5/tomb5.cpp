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
#include "tomb5.h"
#include "../specific/registry.h"

//----------------------------------------------------------------------------
// Discord Rich Presence: switched off in this fork.
//
// Two reasons, each sufficient on its own:
//
// 1. The application ID below belongs to the original project
//    (Trxyebeep/TOMB5). Shipped like that, our users would appear in
//    Discord under his application - that is not our identity and does
//    not belong to us.
// 2. The bundled discord-rpc.lib is built against the dynamic C runtime
//    (/MD). While it is linked in, the game cannot be linked statically,
//    and the EXE needs the Visual C++ runtime on the target machine.
//    Exactly what we want to spare the user: one file that simply starts.
//
// The code stays in place and can be re-enabled via this one switch - but
// then please with an own application ID and the knowledge that static
// linking falls with it again.
//----------------------------------------------------------------------------
#define TOMB5_DISCORD_RPC 0

#if TOMB5_DISCORD_RPC
#include "libs/discordRPC/discord_rpc.h"
#endif

#include "../game/gameflow.h"
#include "../game/lara.h"

tomb5_options tomb5;

// The path of the installed Tomb Raider IV-VI Remastered, or an empty string
// (#123). A path is a string and tomb5_options holds only what the registry
// likes as a DWORD, so it lives beside the struct rather than inside it.
// Empty means: let the Steam discovery in specific\hd\hd_install.cpp look.
char tomb5_hd_install[260];

// The same for Tomb Raider I-III Remastered (#123, stage S7-2), which holds
// the HD models of the four Tomb Raider III guests the randomizer can import.
// Empty means: let the same Steam discovery look for it too.
char tomb5_hd_install_tr3[260];

// "rgba" or "dxt5" (#123, stage S6). A word rather than a number because a
// reader of the registry should be able to tell what it says without a
// table, and because there may one day be a third answer.
char tomb5_hd_texture_format[16];

/* THE ONE OPTION THE DEVICE NEEDS BEFORE THERE ARE OPTIONS (#148, order 6).
 *
 * D3DPRESENT_INTERVAL_ONE is a device-CREATION parameter, and the first
 * device is created in WinMain (specific\winmain.cpp) - while
 * init_tomb5_stuff, which reads every tomb5 option, runs on the game thread
 * that is started afterwards. So at the moment the first device is made,
 * tomb5.vsync still holds the zero it was loaded with, and the option would
 * only take hold on an Alt+Enter.
 *
 * This reads that one value ahead of time, READ-ONLY: REG_PeekLong creates no
 * key, which is the whole point - OpenRegistry would create the tomb5 key
 * here and init_tomb5_stuff's first-run branch, which writes every default
 * there is, would never run again. On a first start the key is not there, the
 * peek misses and the default below is the same 1 the first-run branch will
 * write a moment later.
 *
 * Everything else about the option is ordinary: init_tomb5_stuff reads it
 * again with the rest, the menu row writes it, and the next device made picks
 * it up. */
void load_tomb5_present_settings()
{
	ulong v = 1;

	REG_PeekLong("tomb5", "vsync", v);
	tomb5.vsync = v > 1 ? 1 : v;
}

void init_tomb5_stuff()
{
	char buf[40];
	bool first;

	OpenRegistry("tomb5");
	first = REG_KeyWasCreated();

	if (first)	//key was created = no settings found, write defaults
	{
		sprintf(buf, "footprints");
		tomb5.footprints = 1;							//footprints on
		REG_WriteBool(buf, tomb5.footprints);

		sprintf(buf, "shadow");
		tomb5.shadow_mode = 3;							//PSX like shadow
		REG_WriteLong(buf, tomb5.shadow_mode);

		sprintf(buf, "climbup");
		tomb5.fix_climb_up_delay = 1;					//no delay
		REG_WriteBool(buf, tomb5.fix_climb_up_delay);

		sprintf(buf, "flex_crawl");
		tomb5.flexible_crawling = 1;					//on
		REG_WriteBool(buf, tomb5.flexible_crawling);

		sprintf(buf, "cutseq_skipper");
		tomb5.cutseq_skipper = 0;						//off
		REG_WriteBool(buf, tomb5.cutseq_skipper);

		sprintf(buf, "cheats");
		tomb5.enable_cheats = 0;						//off
		REG_WriteBool(buf, tomb5.enable_cheats);

		sprintf(buf, "bar_pos");
		tomb5.bars_pos = 1;								//original
		REG_WriteLong(buf, tomb5.bars_pos);

		sprintf(buf, "enemy_bar");
		tomb5.enemy_bars = 0;							//off
		REG_WriteBool(buf, tomb5.enemy_bars);

		sprintf(buf, "ammo_counter");
		tomb5.ammo_counter = 0;							//off
		REG_WriteBool(buf, tomb5.ammo_counter);

		sprintf(buf, "gameover");
		tomb5.gameover = 1;								//on
		REG_WriteBool(buf, tomb5.gameover);

		sprintf(buf, "fog");
		tomb5.fog = 1;									//on
		REG_WriteBool(buf, tomb5.fog);

		sprintf(buf, "barMode");
		tomb5.bar_mode = 3;								//PSX
		REG_WriteLong(buf, tomb5.bar_mode);

		sprintf(buf, "crawltilt");
		tomb5.crawltilt = 1;							//on
		REG_WriteBool(buf, tomb5.crawltilt);

		sprintf(buf, "psxsky");
		tomb5.PSX_skies = 1;							//on
		REG_WriteBool(buf, tomb5.PSX_skies);

		sprintf(buf, "tr4LS");
		tomb5.tr4_loadscreens = 1;						//on
		REG_WriteBool(buf, tomb5.tr4_loadscreens);

		sprintf(buf, "tr4LB");
		tomb5.tr4_loadbar = 1;							//on
		REG_WriteBool(buf, tomb5.tr4_loadbar);

		sprintf(buf, "inv_bgM");
		tomb5.inv_bg_mode = 1;							//original
		REG_WriteLong(buf, tomb5.inv_bg_mode);

		sprintf(buf, "loadtxt");
		tomb5.loadingtxt = 1;							//on
		REG_WriteBool(buf, tomb5.loadingtxt);

		sprintf(buf, "shimmer");
		tomb5.shimmer = 1;								//on
		REG_WriteBool(buf, tomb5.shimmer);

		sprintf(buf, "distance_fog");
		tomb5.distance_fog = 12;						//default on PSX is 12
		REG_WriteLong(buf, tomb5.distance_fog);

		sprintf(buf, "ammotype_hotkeys");
		tomb5.ammotype_hotkeys = 1;						//on
		REG_WriteBool(buf, tomb5.ammotype_hotkeys);

		sprintf(buf, "ltransparency");
		tomb5.look_transparency = 0;					//off
		REG_WriteBool(buf, tomb5.look_transparency);

		sprintf(buf, "static_lighting");
		tomb5.static_lighting = 1;						//on
		REG_WriteBool(buf, tomb5.static_lighting);

		sprintf(buf, "uw_dust");
		tomb5.uw_dust = 2;								//original
		REG_WriteLong(buf, tomb5.uw_dust);

		sprintf(buf, "ego_cam");
		tomb5.first_person = 0;							//off (#126)
		REG_WriteBool(buf, tomb5.first_person);

		sprintf(buf, "ego_fov");
		tomb5.first_person_fov = 90;					//middle of 60..100 (#134)
		REG_WriteLong(buf, tomb5.first_person_fov);

		sprintf(buf, "ego_death");
		tomb5.first_person_death = 0;					//off, pull back as before (#134)
		REG_WriteBool(buf, tomb5.first_person_death);

		sprintf(buf, "ego_head");
		tomb5.first_person_head = 0;					//off (#134)
		REG_WriteBool(buf, tomb5.first_person_head);

		sprintf(buf, "ego_smooth");
		tomb5.first_person_smooth = 1;					//light (#134)
		REG_WriteLong(buf, tomb5.first_person_smooth);

		sprintf(buf, "ego_body_still");
		tomb5.first_person_body_still = 0;				//animated (#123, fix round U)
		REG_WriteBool(buf, tomb5.first_person_body_still);

		sprintf(buf, "level_select");
		tomb5.level_select = 0;							//off (#32)
		REG_WriteBool(buf, tomb5.level_select);

		sprintf(buf, "hd_enable");
		tomb5.hd_enable = 0;							//off (#123)
		REG_WriteBool(buf, tomb5.hd_enable);

		sprintf(buf, "hd_objects");
		tomb5.hd_objects = 1;							//on (#123, S3-2)
		REG_WriteBool(buf, tomb5.hd_objects);

		sprintf(buf, "hd_motion");
		tomb5.hd_motion = 1;							//on (#123, S5)
		REG_WriteBool(buf, tomb5.hd_motion);

		sprintf(buf, "hd_reflect");
		tomb5.hd_reflect = 1;							//on (#123, fix round R3b)
		REG_WriteBool(buf, tomb5.hd_reflect);

		sprintf(buf, "fps_mode");
		tomb5.fps_mode = 1;								//60, the default (#148)
		REG_WriteLong(buf, tomb5.fps_mode);

		sprintf(buf, "vsync");
		tomb5.vsync = 1;								//on (#148, order 6)
		REG_WriteLong(buf, tomb5.vsync);

		sprintf(buf, "hd_texture_budget_mb");
		tomb5.hd_texture_budget_mb = 0;					//work it out (#123, S6)
		REG_WriteLong(buf, tomb5.hd_texture_budget_mb);

		// The three Controls-page rows (#161, round 161a step 6): Tank,
		// Toggle, One key. All three are 0 and all three keep the engine
		// exactly as it was before this round - see their comments in
		// tomb5_options (global\types.h).
		sprintf(buf, "ctrl_modern");
		tomb5.control_modern = 0;						//off, Tank
		REG_WriteBool(buf, tomb5.control_modern);

		sprintf(buf, "combat_hold");
		tomb5.combat_hold = 0;							//off, Toggle
		REG_WriteBool(buf, tomb5.combat_hold);

		sprintf(buf, "action_split");
		tomb5.action_split = 0;							//off, One key
		REG_WriteBool(buf, tomb5.action_split);

		// #161, round 161a2b step 5: the fourth Controls-page row, a slider
		// rather than a toggle - 40, MODERN_CAMERA_SPEED_DEFAULT
		// (game/modern_camera.h), the rate round 161a2 already shipped
		// before this row could change it.
		sprintf(buf, "cam_speed");
		tomb5.camera_speed = 40;
		REG_WriteLong(buf, tomb5.camera_speed);

		// Empty: the Steam discovery looks for the Remaster by itself. A
		// path here overrides it - one of the two settings that is not a
		// number.
		sprintf(buf, "hd_install");
		tomb5_hd_install[0] = 0;
		REG_WriteString(buf, tomb5_hd_install);

		// The I-III bundle, for the Tomb Raider III guests (#123, S7-2).
		// Same rule: empty means "look for it yourself".
		sprintf(buf, "hd_install_tr3");
		tomb5_hd_install_tr3[0] = 0;
		REG_WriteString(buf, tomb5_hd_install_tr3);

		sprintf(buf, "hd_texture_format");
		strcpy(tomb5_hd_texture_format, "rgba");		//lossless (#123, S6)
		REG_WriteString(buf, tomb5_hd_texture_format);
	}
	else	//Key already exists, settings already written, read them. also falls back to default if a smartass manually deletes a single value
	{
		sprintf(buf, "footprints");
		REG_ReadBool(buf, tomb5.footprints, 1);

		sprintf(buf, "shadow");
		REG_ReadLong(buf, tomb5.shadow_mode, 3);

		sprintf(buf, "climbup");
		REG_ReadBool(buf, tomb5.fix_climb_up_delay, 1);

		sprintf(buf, "flex_crawl");
		REG_ReadBool(buf, tomb5.flexible_crawling, 1);

		sprintf(buf, "cutseq_skipper");
		REG_ReadBool(buf, tomb5.cutseq_skipper, 0);

		sprintf(buf, "cheats");
		REG_ReadBool(buf, tomb5.enable_cheats, 0);

		sprintf(buf, "bar_pos");
		REG_ReadLong(buf, tomb5.bars_pos, 1);

		sprintf(buf, "enemy_bar");
		REG_ReadBool(buf, tomb5.enemy_bars, 0);

		sprintf(buf, "ammo_counter");
		REG_ReadBool(buf, tomb5.ammo_counter, 0);

		sprintf(buf, "gameover");
		REG_ReadBool(buf, tomb5.gameover, 1);

		sprintf(buf, "fog");
		REG_ReadBool(buf, tomb5.fog, 1);

		sprintf(buf, "barMode");
		REG_ReadLong(buf, tomb5.bar_mode, 3);

		sprintf(buf, "crawltilt");
		REG_ReadBool(buf, tomb5.crawltilt, 1);

		sprintf(buf, "psxsky");
		REG_ReadBool(buf, tomb5.PSX_skies, 1);

		sprintf(buf, "tr4LS");
		REG_ReadBool(buf, tomb5.tr4_loadscreens, 1);

		sprintf(buf, "tr4LB");
		REG_ReadBool(buf, tomb5.tr4_loadbar, 1);

		sprintf(buf, "inv_bgM");
		REG_ReadLong(buf, tomb5.inv_bg_mode, 1);

		sprintf(buf, "loadtxt");
		REG_ReadBool(buf, tomb5.loadingtxt, 1);

		sprintf(buf, "shimmer");
		REG_ReadBool(buf, tomb5.shimmer, 1);

		sprintf(buf, "distance_fog");
		REG_ReadLong(buf, tomb5.distance_fog, 12);

		sprintf(buf, "ammotype_hotkeys");
		REG_ReadBool(buf, tomb5.ammotype_hotkeys, 1);

		sprintf(buf, "ltransparency");
		REG_ReadBool(buf, tomb5.look_transparency, 1);

		sprintf(buf, "static_lighting");
		REG_ReadBool(buf, tomb5.static_lighting, 1);

		sprintf(buf, "uw_dust");
		REG_ReadLong(buf, tomb5.uw_dust, 2);

		sprintf(buf, "ego_cam");
		REG_ReadBool(buf, tomb5.first_person, 0);

		// A value from an older build, or a hand-edited one, must not take
		// the view apart: anything outside the range falls back to 90.
		sprintf(buf, "ego_fov");
		REG_ReadLong(buf, tomb5.first_person_fov, 90);
		if (tomb5.first_person_fov < 60 || tomb5.first_person_fov > 100)
			tomb5.first_person_fov = 90;

		sprintf(buf, "ego_death");
		REG_ReadBool(buf, tomb5.first_person_death, 0);

		sprintf(buf, "ego_head");
		REG_ReadBool(buf, tomb5.first_person_head, 0);

		sprintf(buf, "ego_smooth");
		REG_ReadLong(buf, tomb5.first_person_smooth, 1);
		if (tomb5.first_person_smooth > 3)
			tomb5.first_person_smooth = 1;

		sprintf(buf, "ego_body_still");
		REG_ReadBool(buf, tomb5.first_person_body_still, 0);

		sprintf(buf, "level_select");
		REG_ReadBool(buf, tomb5.level_select, 0);

		sprintf(buf, "hd_enable");
		REG_ReadBool(buf, tomb5.hd_enable, 0);

		sprintf(buf, "hd_objects");
		REG_ReadBool(buf, tomb5.hd_objects, 1);

		sprintf(buf, "hd_motion");
		REG_ReadBool(buf, tomb5.hd_motion, 1);

		sprintf(buf, "hd_reflect");
		REG_ReadBool(buf, tomb5.hd_reflect, 1);

		/* #148: 0 is 30, 1 is 60 and 2 is the display's own refresh rate
		 * (order 6). Anything above 2 is not a mode that exists and reads
		 * back as the default. */
		sprintf(buf, "fps_mode");
		REG_ReadLong(buf, tomb5.fps_mode, 1);
		if (tomb5.fps_mode > 2)
			tomb5.fps_mode = 1;

		/* #148, order 6. Read BEFORE the device is made - DXCreate takes the
		 * present interval from it (specific\dxshell.cpp) and load_new_tomb5_
		 * settings runs in WinMain ahead of the first DXCreate. */
		sprintf(buf, "vsync");
		REG_ReadLong(buf, tomb5.vsync, 1);
		if (tomb5.vsync > 1)
			tomb5.vsync = 1;

		sprintf(buf, "hd_texture_budget_mb");
		REG_ReadLong(buf, tomb5.hd_texture_budget_mb, 0);

		// #161, round 161a step 6 - same three, same defaults as the
		// first-run branch above. This runs once at process startup, before
		// there is a Lara to steer, so it needs no ModernResetControls()
		// call of its own: game\modern_state.cpp's statics start zeroed by
		// the loader exactly as tomb5.control_modern does, and its own tick
		// guard treats the very first tick as a break in the chain (see its
		// comment) and resets itself. The calls that ARE needed are elsewhere
		// and both have a level in front of them: the Controls page, where the
		// row is switched at runtime with a level already loaded (round 161a
		// step 7), and the level start itself (game\gameflow.cpp - added in
		// that round's second review pass, where the tick guard turned out not
		// to be able to see a level change at all).
		sprintf(buf, "ctrl_modern");
		REG_ReadBool(buf, tomb5.control_modern, 0);

		sprintf(buf, "combat_hold");
		REG_ReadBool(buf, tomb5.combat_hold, 0);

		sprintf(buf, "action_split");
		REG_ReadBool(buf, tomb5.action_split, 0);

		// #161, round 161a2b step 5. Same clamp discipline as ego_fov/fps_mode
		// above: a value from an older build (there was none before this
		// round) or a hand-edited one must not hand the orbit a rate past
		// what the slider itself can ask for.
		sprintf(buf, "cam_speed");
		REG_ReadLong(buf, tomb5.camera_speed, 40);
		if (tomb5.camera_speed > 100)
			tomb5.camera_speed = 100;

		sprintf(buf, "hd_install");
		REG_ReadString(buf, tomb5_hd_install, sizeof(tomb5_hd_install), "");

		sprintf(buf, "hd_install_tr3");
		REG_ReadString(buf, tomb5_hd_install_tr3,
			sizeof(tomb5_hd_install_tr3), "");

		sprintf(buf, "hd_texture_format");
		REG_ReadString(buf, tomb5_hd_texture_format,
			sizeof(tomb5_hd_texture_format), "rgba");

		// A word this engine does not know is the default word, not a reason
		// to stop: the setting is typed by hand into the registry.
		if (_stricmp(tomb5_hd_texture_format, "dxt5") &&
			_stricmp(tomb5_hd_texture_format, "rgba"))
			strcpy(tomb5_hd_texture_format, "rgba");
	}

	CloseRegistry();
}

void save_new_tomb5_settings()
{
	char buf[40];
	OpenRegistry("tomb5");

	sprintf(buf, "footprints");
	REG_WriteBool(buf, tomb5.footprints);

	sprintf(buf, "shadow");
	REG_WriteLong(buf, tomb5.shadow_mode);

	sprintf(buf, "climbup");
	REG_WriteBool(buf, tomb5.fix_climb_up_delay);

	sprintf(buf, "flex_crawl");
	REG_WriteBool(buf, tomb5.flexible_crawling);

	sprintf(buf, "cutseq_skipper");
	REG_WriteBool(buf, tomb5.cutseq_skipper);

	sprintf(buf, "cheats");
	REG_WriteBool(buf, tomb5.enable_cheats);

	sprintf(buf, "bar_pos");
	REG_WriteLong(buf, tomb5.bars_pos);

	sprintf(buf, "enemy_bar");
	REG_WriteBool(buf, tomb5.enemy_bars);

	sprintf(buf, "ammo_counter");
	REG_WriteBool(buf, tomb5.ammo_counter);

	sprintf(buf, "gameover");
	REG_WriteBool(buf, tomb5.gameover);

	sprintf(buf, "fog");
	REG_WriteBool(buf, tomb5.fog);

	sprintf(buf, "barMode");
	REG_WriteLong(buf, tomb5.bar_mode);

	sprintf(buf, "crawltilt");
	REG_WriteBool(buf, tomb5.crawltilt);

	sprintf(buf, "psxsky");
	REG_WriteBool(buf, tomb5.PSX_skies);

	sprintf(buf, "tr4LS");
	REG_WriteBool(buf, tomb5.tr4_loadscreens);

	sprintf(buf, "tr4LB");
	REG_WriteBool(buf, tomb5.tr4_loadbar);

	sprintf(buf, "inv_bgM");
	REG_WriteLong(buf, tomb5.inv_bg_mode);

	sprintf(buf, "loadtxt");
	REG_WriteBool(buf, tomb5.loadingtxt);

	sprintf(buf, "shimmer");
	REG_WriteBool(buf, tomb5.shimmer);

	sprintf(buf, "distance_fog");
	REG_WriteLong(buf, tomb5.distance_fog);

	sprintf(buf, "ammotype_hotkeys");
	REG_WriteBool(buf, tomb5.ammotype_hotkeys);

	sprintf(buf, "ltransparency");
	REG_WriteBool(buf, tomb5.look_transparency);

	sprintf(buf, "static_lighting");
	REG_WriteBool(buf, tomb5.static_lighting);

	sprintf(buf, "uw_dust");
	REG_WriteLong(buf, tomb5.uw_dust);

	sprintf(buf, "ego_cam");
	REG_WriteBool(buf, tomb5.first_person);

	sprintf(buf, "ego_fov");
	REG_WriteLong(buf, tomb5.first_person_fov);

	sprintf(buf, "ego_death");
	REG_WriteBool(buf, tomb5.first_person_death);

	sprintf(buf, "ego_head");
	REG_WriteBool(buf, tomb5.first_person_head);

	sprintf(buf, "ego_smooth");
	REG_WriteLong(buf, tomb5.first_person_smooth);

	sprintf(buf, "ego_body_still");
	REG_WriteBool(buf, tomb5.first_person_body_still);

	sprintf(buf, "level_select");
	REG_WriteBool(buf, tomb5.level_select);

	sprintf(buf, "hd_enable");
	REG_WriteBool(buf, tomb5.hd_enable);

	sprintf(buf, "hd_objects");
	REG_WriteBool(buf, tomb5.hd_objects);

	sprintf(buf, "hd_motion");
	REG_WriteBool(buf, tomb5.hd_motion);

	sprintf(buf, "hd_reflect");
	REG_WriteBool(buf, tomb5.hd_reflect);

	sprintf(buf, "fps_mode");
	REG_WriteLong(buf, tomb5.fps_mode);

	sprintf(buf, "vsync");
	REG_WriteLong(buf, tomb5.vsync);

	// #161, round 161a step 6 - the three Controls-page rows.
	sprintf(buf, "ctrl_modern");
	REG_WriteBool(buf, tomb5.control_modern);

	sprintf(buf, "combat_hold");
	REG_WriteBool(buf, tomb5.combat_hold);

	sprintf(buf, "action_split");
	REG_WriteBool(buf, tomb5.action_split);

	// #161, round 161a2b step 5 - the fourth Controls-page row.
	sprintf(buf, "cam_speed");
	REG_WriteLong(buf, tomb5.camera_speed);

	CloseRegistry();
}

void RPC_Init()
{
#if TOMB5_DISCORD_RPC
	DiscordEventHandlers handlers;

	memset(&handlers, 0, sizeof(handlers));
	Discord_Initialize("959220032787869751", &handlers, 1, 0);
#endif
}

const char* RPC_GetLevelName()
{
	if (gfCurrentLevel == LVL5_TITLE)
	{
		if (bDoCredits)
			return "In Credits";
		else
			return "In Title";
	}
	else
		return SCRIPT_TEXT(gfLevelNames[gfCurrentLevel]);
}

const char* RPC_GetTimer()
{
	long sec, days, hours, min;
	static char buf[64];

	sec = GameTimer / 30;
	days = sec / 86400;
	hours = (sec % 86400) / 3600;
	min = (sec / 60) % 60;
	sec = (sec % 60);
	sprintf(buf, "Time Taken: %02d:%02d:%02d", (days * 24) + hours, min, sec);
	return buf;
}

const char* RPC_GetLevelPic()
{
	switch (gfCurrentLevel)
	{
	case LVL5_STREETS_OF_ROME:
		return "rome";

	case LVL5_TRAJAN_MARKETS:
		return "streets";

	case LVL5_COLOSSEUM:
		return "colosseum";

	case LVL5_BASE:
		return "base";

	case LVL5_SUBMARINE:
		return "submarine";

	case LVL5_DEEPSEA_DIVE:
		return "deepsea";

	case LVL5_SINKING_SUBMARINE:
		return "sink";

	case LVL5_GALLOWS_TREE:
		return "gallow1";

	case LVL5_LABYRINTH:
		return "laby";

	case LVL5_OLD_MILL:
		return "mill";

	case LVL5_THIRTEENTH_FLOOR:
		return "13th";

	case LVL5_ESCAPE_WITH_THE_IRIS:
		return "iris";

	case LVL5_SECURITY_BREACH:
		return "breach";

	case LVL5_RED_ALERT:
		return "alert";

	default:
		return "default";
	}
	
}

const char* RPC_GetHealthPic()
{
	if (lara_item->hit_points > 666)
		return "green";

	if (lara_item->hit_points > 333)
		return "yellow";

	return "red";
}

const char* RPC_GetHealthPercentage()
{
	static char buf[32];

	sprintf(buf, "Health: %i%%", lara_item->hit_points / 10);
	return buf;
}

void RPC_Update()
{
#if TOMB5_DISCORD_RPC
	DiscordRichPresence RPC;

	memset(&RPC, 0, sizeof(RPC));

	RPC.details = RPC_GetLevelName();
	RPC.largeImageKey = RPC_GetLevelPic();
	RPC.largeImageText = gfCurrentLevel == LVL5_COLOSSEUM ? "BOO" : RPC.details;	//xoxo

	RPC.smallImageKey = RPC_GetHealthPic();
	RPC.smallImageText = RPC_GetHealthPercentage();

	RPC.state = RPC_GetTimer();

	RPC.instance = 1;
	Discord_UpdatePresence(&RPC);
#endif
}

void RPC_close()
{
#if TOMB5_DISCORD_RPC
	Discord_Shutdown();
#endif
}
