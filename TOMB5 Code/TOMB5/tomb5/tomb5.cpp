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
#include "../specific/function_stubs.h"	// Log - the HDLOG settings line
#include "../specific/loadbar_math.h"	// the Loadbar style row's state

//----------------------------------------------------------------------------
// Discord Rich Presence: switched off in this fork.
//
// Two reasons, each sufficient on its own:
//
// 1. The application ID below belongs to the original project
//    (Trxyebeep/TOMB5). Shipped like that, players would appear in
//    Discord under that application, which is not this fork's identity.
// 2. The bundled discord-rpc.lib is built against the dynamic C runtime
//    (/MD). While it is linked in, the game cannot be linked statically,
//    and the EXE needs the Visual C++ runtime on the target machine -
//    exactly the dependency a single-file EXE is meant to avoid.
//
// The code stays in place and can be re-enabled via this one switch, with
// an application ID of its own; static linking falls with it again.
//----------------------------------------------------------------------------
#define TOMB5_DISCORD_RPC 0

#if TOMB5_DISCORD_RPC
#include "libs/discordRPC/discord_rpc.h"
#endif

#include "../game/gameflow.h"
#include "../game/lara.h"
#include "../specific/hd/hd_render.h"	// HDRenderMarbleSet

tomb5_options tomb5;

// The path of the installed Tomb Raider IV-VI Remastered, or an empty
// string. A path is a string and tomb5_options holds only what the registry
// likes as a DWORD, so it lives beside the struct rather than inside it.
// Empty means: let the Steam discovery in specific\hd\hd_install.cpp look.
char tomb5_hd_install[260];

// The same for Tomb Raider I-III Remastered, which holds the HD models of
// the four Tomb Raider III guests the randomizer can import. Empty means:
// let the same Steam discovery look for it too.
char tomb5_hd_install_tr3[260];

// "rgba" or "dxt5". A word rather than a number because a reader of the
// registry should be able to tell what it says without a table, and because
// there may one day be a third answer.
char tomb5_hd_texture_format[16];

/* The one option the device needs before there are options.
 *
 * D3DPRESENT_INTERVAL_ONE is a device-creation parameter, and the first
 * device is created in WinMain (specific\winmain.cpp) - while
 * init_tomb5_stuff, which reads every tomb5 option, runs on the game thread
 * that is started afterwards. So at the moment the first device is made,
 * tomb5.vsync still holds the zero it was loaded with, and the option would
 * only take hold on an Alt+Enter.
 *
 * This reads that one value ahead of time, read-only: REG_PeekLong creates no
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
		tomb5.shadow_mode = 5;							//projected shadow for the HD Lara, faded circle for every blob
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

		sprintf(buf, "tr4LB");
		tomb5.tr4_loadbar = 1;							//on
		REG_WriteBool(buf, tomb5.tr4_loadbar);

		sprintf(buf, "loadbar");
		tomb5.loadbar = LOADBAR_TR4;					//the TR4 bar, as tr4LB
		REG_WriteLong(buf, tomb5.loadbar);

		sprintf(buf, "loadfade_ms");
		tomb5.loadfade_ms = 150;						//off's fade to black
		REG_WriteLong(buf, tomb5.loadfade_ms);

		sprintf(buf, "loadbar_min_ms");
		tomb5.loadbar_min_ms = 500;						//the brake
		REG_WriteLong(buf, tomb5.loadbar_min_ms);

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
		tomb5.first_person = 0;							//off
		REG_WriteBool(buf, tomb5.first_person);

		sprintf(buf, "ego_fov");
		tomb5.first_person_fov = 90;					//middle of 60..100
		REG_WriteLong(buf, tomb5.first_person_fov);

		sprintf(buf, "ego_death");
		tomb5.first_person_death = 0;					//off, pull back as before
		REG_WriteBool(buf, tomb5.first_person_death);

		sprintf(buf, "ego_head");
		tomb5.first_person_head = 0;					//off
		REG_WriteBool(buf, tomb5.first_person_head);

		sprintf(buf, "ego_smooth");
		tomb5.first_person_smooth = 1;					//light
		REG_WriteLong(buf, tomb5.first_person_smooth);

		sprintf(buf, "ego_body_still");
		tomb5.first_person_body_still = 0;				//animated
		REG_WriteBool(buf, tomb5.first_person_body_still);

		sprintf(buf, "level_select");
		tomb5.level_select = 0;							//off
		REG_WriteBool(buf, tomb5.level_select);

		sprintf(buf, "hd_enable");
		tomb5.hd_enable = 0;							//off
		REG_WriteBool(buf, tomb5.hd_enable);

		sprintf(buf, "hd_objects");
		tomb5.hd_objects = 1;							//on
		REG_WriteBool(buf, tomb5.hd_objects);

		sprintf(buf, "hd_motion");
		tomb5.hd_motion = 1;							//on
		REG_WriteBool(buf, tomb5.hd_motion);

		sprintf(buf, "hd_reflect");
		tomb5.hd_reflect = 1;							//on
		REG_WriteBool(buf, tomb5.hd_reflect);

		sprintf(buf, "hd_reach");
		tomb5.hd_reach = 1;								//on
		REG_WriteBool(buf, tomb5.hd_reach);

		sprintf(buf, "hd_softbody");
		tomb5.hd_softbody = 0;							//off
		REG_WriteLong(buf, tomb5.hd_softbody);

		sprintf(buf, "hd_water");
		tomb5.hd_water = 1;								//on
		REG_WriteBool(buf, tomb5.hd_water);

		sprintf(buf, "hd_air");
		tomb5.hd_air = 1;								//on
		REG_WriteLong(buf, tomb5.hd_air);

		sprintf(buf, "hd_vis");
		tomb5.hd_vis = 1;								//the visibility path
		REG_WriteLong(buf, tomb5.hd_vis);

		// The marble floors on, every plane a frame shows (see the read
		// below); the options' Reflections row writes hd_marble after this.
		sprintf(buf, "hd_marble");
		REG_WriteLong(buf, 1);
		HDRenderMarbleSet(1, 64);

		sprintf(buf, "hd_threads");
		tomb5.hd_threads = 0;							//the machine's
		REG_WriteLong(buf, tomb5.hd_threads);

		// The Display page's slider: 50, the middle, where no pass runs.
		sprintf(buf, "gamma");
		tomb5.gamma = 50;
		REG_WriteLong(buf, tomb5.gamma);

		sprintf(buf, "hd_build");
		tomb5.hd_build = 1;								//on
		REG_WriteLong(buf, tomb5.hd_build);

		sprintf(buf, "fps_mode");
		tomb5.fps_mode = 1;								//60, the default
		REG_WriteLong(buf, tomb5.fps_mode);

		sprintf(buf, "vsync");
		tomb5.vsync = 1;								//on
		REG_WriteLong(buf, tomb5.vsync);

		sprintf(buf, "hd_texture_budget_mb");
		tomb5.hd_texture_budget_mb = 0;					//work it out
		REG_WriteLong(buf, tomb5.hd_texture_budget_mb);

		// The three Controls-page rows: Tank, Toggle, One key. All three
		// are 0 and all three keep the engine's original behaviour - see
		// their comments in tomb5_options (global\types.h).
		sprintf(buf, "ctrl_modern");
		tomb5.control_modern = 0;						//off, Tank
		REG_WriteBool(buf, tomb5.control_modern);

		sprintf(buf, "combat_hold");
		tomb5.combat_hold = 0;							//off, Toggle
		REG_WriteBool(buf, tomb5.combat_hold);

		sprintf(buf, "action_split");
		tomb5.action_split = 0;							//off, One key
		REG_WriteBool(buf, tomb5.action_split);

		// The two mode rows under Combat mode. Both 0 = Hold, which is the
		// engine's own behaviour: the bit lasts as long as the key is down
		// (their comments in tomb5_options, global\types.h).
		sprintf(buf, "sprint_toggle");
		tomb5.sprint_toggle = 0;						//off, Hold
		REG_WriteBool(buf, tomb5.sprint_toggle);

		sprintf(buf, "duck_toggle");
		tomb5.duck_toggle = 0;							//off, Hold
		REG_WriteBool(buf, tomb5.duck_toggle);

		// The fourth mode row, on the Modes sub-page with the other three.
		// 0 = Hold again, which is the engine holding on for as long as the
		// key is down (its comment in tomb5_options, global\types.h).
		sprintf(buf, "grab_toggle");
		tomb5.grab_toggle = 0;							//off, Hold
		REG_WriteBool(buf, tomb5.grab_toggle);

		// The fifth mode row, on the Modes sub-page with the other four.
		// Off keeps running off an edge running off it, as it always was.
		sprintf(buf, "ledge_hang");
		tomb5.ledge_hang = 0;							//off
		REG_WriteBool(buf, tomb5.ledge_hang);

		// The fourth Controls-page row, a slider rather than a toggle - 40,
		// MODERN_CAMERA_SPEED_DEFAULT (game/modern_camera.h).
		sprintf(buf, "cam_speed");
		tomb5.camera_speed = 40;
		REG_WriteLong(buf, tomb5.camera_speed);

		// The fifth Controls-page row, the second slider. 50 is the middle
		// of the bar and the one value at which ModernDeadZoneUser
		// reproduces every MODERN_DEAD_* constant exactly
		// (game/modern_move.h).
		sprintf(buf, "dead_zone");
		tomb5.dead_zone = 50;
		REG_WriteLong(buf, tomb5.dead_zone);

		// The two mouse rows under Dead zone. 50 is the middle of the bar,
		// the gain game/modern_camera.h states unscaled; the invert is off.
		sprintf(buf, "mouse_sens");
		tomb5.mouse_sensitivity = 50;
		REG_WriteLong(buf, tomb5.mouse_sensitivity);

		sprintf(buf, "mouse_invert");
		tomb5.mouse_invert = 0;							//off
		REG_WriteBool(buf, tomb5.mouse_invert);

		// Empty: the Steam discovery looks for the Remaster by itself. A
		// path here overrides it - one of the two settings that is not a
		// number.
		sprintf(buf, "hd_install");
		tomb5_hd_install[0] = 0;
		REG_WriteString(buf, tomb5_hd_install);

		// The I-III bundle, for the Tomb Raider III guests. Same rule:
		// empty means "look for it yourself".
		sprintf(buf, "hd_install_tr3");
		tomb5_hd_install_tr3[0] = 0;
		REG_WriteString(buf, tomb5_hd_install_tr3);

		sprintf(buf, "hd_texture_format");
		strcpy(tomb5_hd_texture_format, "rgba");		//lossless
		REG_WriteString(buf, tomb5_hd_texture_format);
	}
	else	//Key already exists, settings already written, read them. also falls back to default if a smartass manually deletes a single value
	{
		sprintf(buf, "footprints");
		REG_ReadBool(buf, tomb5.footprints, 1);

		sprintf(buf, "shadow");
		REG_ReadLong(buf, tomb5.shadow_mode, 5);

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

		sprintf(buf, "tr4LB");
		REG_ReadBool(buf, tomb5.tr4_loadbar, 1);

		/* The row's three states. Absent - every older registry - it reads
		 * as tr4LB said, and that is written as its value; tr4LB follows
		 * it from here on (the TR4 bar's look). */
		sprintf(buf, "loadbar");
		{
			const bool found = REG_ReadLong(buf, tomb5.loadbar,
				LoadBarModeOf(false, 0, tomb5.tr4_loadbar));

			tomb5.loadbar = LoadBarModeOf(found, tomb5.loadbar,
				tomb5.tr4_loadbar);
			tomb5.tr4_loadbar = tomb5.loadbar == LOADBAR_TR4;
		}

		sprintf(buf, "loadfade_ms");
		REG_ReadLong(buf, tomb5.loadfade_ms, 150);

		// The brake: the loading screen stands at least this long.
		sprintf(buf, "loadbar_min_ms");
		REG_ReadLong(buf, tomb5.loadbar_min_ms, 500);

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

		sprintf(buf, "hd_reach");
		REG_ReadBool(buf, tomb5.hd_reach, 1);

		/* 0 off, 1 subtle, 2 normal, 3 strong; absent - every older
		 * registry - and anything above 3 read as off. */
		sprintf(buf, "hd_softbody");
		REG_ReadLong(buf, tomb5.hd_softbody, 0);
		if (tomb5.hd_softbody > 3)
			tomb5.hd_softbody = 0;

		// Absent - every older registry - reads as on.
		sprintf(buf, "hd_water");
		REG_ReadBool(buf, tomb5.hd_water, 1);

		// Absent - every older registry - reads as 1, the default.
		sprintf(buf, "hd_air");
		REG_ReadLong(buf, tomb5.hd_air, 1);

		if (tomb5.hd_air > 2)
			tomb5.hd_air = 1;

		// Absent - every older registry - reads as 1, the visibility path.
		sprintf(buf, "hd_vis");
		REG_ReadLong(buf, tomb5.hd_vis, 1);

		if (tomb5.hd_vis > 1)
			tomb5.hd_vis = 0;

		/* The marble floors' mirror (the options' Reflections row) and the
		 * most planes a frame mirrors, a ceiling over what a frame shows
		 * (specific\hd\hd_render.cpp, "the marble floors"; 64 is
		 * hd_marble_math.h's HD_MARBLE_PLANES_MOST), handed to the renderer
		 * before any level is built. Absent - every older registry - reads
		 * as on and 64. The ceiling has a key of its own, not
		 * hd_marble_planes: REG_ReadLong writes its default where a key is
		 * absent, so existing registries hold hd_marble_planes as 2, a
		 * value that would hold the mirror to two planes. */
		{
			ulong marble = 1, planes = 64;

			sprintf(buf, "hd_marble");
			REG_ReadLong(buf, marble, 1);
			sprintf(buf, "hd_marble_ceiling");
			REG_ReadLong(buf, planes, 64);
			HDRenderMarbleSet(marble > 1 ? 1 : (int)marble, (int)planes);
		}

		// Absent - every older registry - reads as 0, the machine's count.
		sprintf(buf, "hd_threads");
		REG_ReadLong(buf, tomb5.hd_threads, 0);

		// Missing - every older registry - reads as 50, the middle, where
		// the picture is the engine's own; out of range lands on the middle
		// too, for dead_zone's reason: a hand-edited value says nothing
		// about which end was meant.
		sprintf(buf, "gamma");
		REG_ReadLong(buf, tomb5.gamma, 50);
		if (tomb5.gamma > 100)
			tomb5.gamma = 50;

		// Absent - every older registry - reads as 1: built when found.
		sprintf(buf, "hd_build");
		REG_ReadLong(buf, tomb5.hd_build, 1);

		/* 0 is 30, 1 is 60 and 2 is the display's own refresh rate.
		 * Anything above 2 is not a mode that exists and reads back as the
		 * default. */
		sprintf(buf, "fps_mode");
		REG_ReadLong(buf, tomb5.fps_mode, 1);
		if (tomb5.fps_mode > 2)
			tomb5.fps_mode = 1;

		/* Read before the device is made - DXCreate takes the present
		 * interval from it (specific\dxshell.cpp) and load_new_tomb5_
		 * settings runs in WinMain ahead of the first DXCreate. */
		sprintf(buf, "vsync");
		REG_ReadLong(buf, tomb5.vsync, 1);
		if (tomb5.vsync > 1)
			tomb5.vsync = 1;

		sprintf(buf, "hd_texture_budget_mb");
		REG_ReadLong(buf, tomb5.hd_texture_budget_mb, 0);

		// Same three, same defaults as the first-run branch above. This runs
		// once at process startup, before there is a Lara to steer, so it
		// needs no ModernResetControls() call of its own: game\modern_state.
		// cpp's statics start zeroed by the loader exactly as
		// tomb5.control_modern does, and its own tick guard treats the very
		// first tick as a break in the chain (see its comment) and resets
		// itself. The calls that are needed are elsewhere and both have a
		// level in front of them: the Controls page, where the row is
		// switched at runtime with a level already loaded, and the level
		// start itself (game\gameflow.cpp).
		sprintf(buf, "ctrl_modern");
		REG_ReadBool(buf, tomb5.control_modern, 0);

		sprintf(buf, "combat_hold");
		REG_ReadBool(buf, tomb5.combat_hold, 0);

		sprintf(buf, "action_split");
		REG_ReadBool(buf, tomb5.action_split, 0);

		// Absent - every older registry - reads as 0, Hold, so an existing
		// installation keeps the behaviour it had; REG_ReadBool writes the
		// default back, so the two names exist from the first launch of
		// this build.
		sprintf(buf, "sprint_toggle");
		REG_ReadBool(buf, tomb5.sprint_toggle, 0);

		sprintf(buf, "duck_toggle");
		REG_ReadBool(buf, tomb5.duck_toggle, 0);

		// Same rule as the two above: absent - every older registry - reads
		// as 0, Hold, so an existing installation holds on exactly as it
		// did.
		sprintf(buf, "grab_toggle");
		REG_ReadBool(buf, tomb5.grab_toggle, 0);

		// Same rule again: absent reads as 0, off, so running off an edge
		// keeps running off it as it always did.
		sprintf(buf, "ledge_hang");
		REG_ReadBool(buf, tomb5.ledge_hang, 0);

		// Same clamp discipline as ego_fov/fps_mode above: a value from an
		// older build or a hand-edited one must not hand the orbit a rate
		// past what the slider itself can ask for.
		sprintf(buf, "cam_speed");
		REG_ReadLong(buf, tomb5.camera_speed, 40);
		if (tomb5.camera_speed > 100)
			tomb5.camera_speed = 100;

		// Guarded like cam_speed above but landing on the default rather
		// than on the nearest end, because an out-of-range value here says
		// nothing about which end was meant and the middle is the only
		// value with a known feel: a hand-edited 400 would multiply every
		// dead zone by eight and leave the sticks dead in their whole
		// travel. Missing or out of range is 50, the value that reproduces
		// the tuned constants (the field is a ulong, so a negative cannot
		// arrive).
		sprintf(buf, "dead_zone");
		REG_ReadLong(buf, tomb5.dead_zone, 50);
		if (tomb5.dead_zone > 100)
			tomb5.dead_zone = 50;

		// Missing - every older registry - reads as 50, the middle; out of
		// range lands on the middle too, for dead_zone's reason: a hand-edited
		// value says nothing about which end was meant.
		sprintf(buf, "mouse_sens");
		REG_ReadLong(buf, tomb5.mouse_sensitivity, 50);
		if (tomb5.mouse_sensitivity > 100)
			tomb5.mouse_sensitivity = 50;

		sprintf(buf, "mouse_invert");
		REG_ReadBool(buf, tomb5.mouse_invert, 0);

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

	/* HDLOG settings, behind -log, once at the start: the draw a game starts
	 * with (hd_enable, the HD textures row: 1 HD, 0 classic) and the build
	 * gate (hd_build: 0 builds nothing HD, any other value builds a level's
	 * HD data whenever an installation is found). */
	Log("HDLOG settings hd_enable=%d hd_build=%lu", tomb5.hd_enable ? 1 : 0,
		(unsigned long)tomb5.hd_build);
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

	sprintf(buf, "tr4LB");
	REG_WriteBool(buf, tomb5.tr4_loadbar);

	sprintf(buf, "loadbar");
	REG_WriteLong(buf, tomb5.loadbar);

	sprintf(buf, "loadfade_ms");
	REG_WriteLong(buf, tomb5.loadfade_ms);

	sprintf(buf, "loadbar_min_ms");
	REG_WriteLong(buf, tomb5.loadbar_min_ms);

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

	sprintf(buf, "hd_reach");
	REG_WriteBool(buf, tomb5.hd_reach);

	sprintf(buf, "hd_softbody");
	REG_WriteLong(buf, tomb5.hd_softbody);

	sprintf(buf, "hd_water");
	REG_WriteBool(buf, tomb5.hd_water);

	sprintf(buf, "hd_air");
	REG_WriteLong(buf, tomb5.hd_air);

	sprintf(buf, "hd_vis");
	REG_WriteLong(buf, tomb5.hd_vis);

	sprintf(buf, "hd_threads");
	REG_WriteLong(buf, tomb5.hd_threads);

	// The Display page's slider.
	sprintf(buf, "gamma");
	REG_WriteLong(buf, tomb5.gamma);

	sprintf(buf, "hd_build");
	REG_WriteLong(buf, tomb5.hd_build);

	sprintf(buf, "fps_mode");
	REG_WriteLong(buf, tomb5.fps_mode);

	sprintf(buf, "vsync");
	REG_WriteLong(buf, tomb5.vsync);

	// The three Controls-page rows.
	sprintf(buf, "ctrl_modern");
	REG_WriteBool(buf, tomb5.control_modern);

	sprintf(buf, "combat_hold");
	REG_WriteBool(buf, tomb5.combat_hold);

	sprintf(buf, "action_split");
	REG_WriteBool(buf, tomb5.action_split);

	// The two mode rows.
	sprintf(buf, "sprint_toggle");
	REG_WriteBool(buf, tomb5.sprint_toggle);

	sprintf(buf, "duck_toggle");
	REG_WriteBool(buf, tomb5.duck_toggle);

	// The fourth mode row.
	sprintf(buf, "grab_toggle");
	REG_WriteBool(buf, tomb5.grab_toggle);

	// The fifth mode row.
	sprintf(buf, "ledge_hang");
	REG_WriteBool(buf, tomb5.ledge_hang);

	// The fourth Controls-page row.
	sprintf(buf, "cam_speed");
	REG_WriteLong(buf, tomb5.camera_speed);

	// The fifth, the dead zone slider.
	sprintf(buf, "dead_zone");
	REG_WriteLong(buf, tomb5.dead_zone);

	// The two mouse rows under it.
	sprintf(buf, "mouse_sens");
	REG_WriteLong(buf, tomb5.mouse_sensitivity);

	sprintf(buf, "mouse_invert");
	REG_WriteBool(buf, tomb5.mouse_invert);

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
