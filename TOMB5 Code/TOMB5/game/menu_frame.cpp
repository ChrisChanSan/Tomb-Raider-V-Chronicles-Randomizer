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
#include "menu_frame.h"
#include "menu_nav.h"
#include "text.h"
#include "gameflow.h"
#include "sound.h"
#include "camera.h"			// ModernCameraResetOrbit - the Control type row (round 161a2)
#include "modern_state.h"		// ModernResetControls - the same row
#include "savegame.h"
#include "../specific/input.h"
#include "../specific/dxshell.h"		// keymap - the sliders and the key capture read it raw
#include "../specific/winmain.h"		// App - the Targeting row
#include "../specific/3dmath.h"		// phd_centerx - the geometry
#include "../specific/LoadSave.h"
#include "../specific/registry.h"
#include "../specific/drawbars.h"
#include "../specific/audio.h"
#include "../specific/dxsound.h"
#include "../specific/function_stubs.h"	// Log, gLogEnabled - the MENULOG seam
#include "../tomb5/tomb5.h"
#include "../tomb5/troyestuff.h"

/* ========================================================================= */
/* The rows' own logic. Every function below is a block of base's DoOptions()
 * (specific\LoadSave.cpp) or of a Page* function (tomb5\troyestuff.cpp),
 * carried over statement for statement - the sounds included, because several
 * rows play one even when the value does not move and that is what the player
 * hears today. The return value is the one thing that is new: true when the
 * SETTING really changed, which is what decides whether anything is written
 * and whether MENULOG prints a line.
 *
 * THE FRAME CALLS step() ON EVERY FRAME THE CURSOR STANDS ON THE ROW, with dir
 * 0 when no direction is held. That is deliberate: base's rows are written as
 * "if (sel & bit) { ... }" blocks and several of them do work with no input at
 * all (Music volume calls ACMSetVolume, Targeting mirrors into savegame), so a
 * block that only ran on a keypress would not be the same block.            */
/* ========================================================================= */

/* Backed up once per session, exactly as base's DoOptions did with its
 * sfx_backup_flag: the SFX row compares against sfx_bak to decide whether to
 * re-trigger the breath sample, and the quality row against sfx_quality_bak. */
static long sfx_bak;
static long sfx_quality_bak;
static long sfx_breath_db = -1;
static char sfx_backup_flag;

/* ---------------------------------------------------------------- Audio -- */

static long MenuNumMusicVolume(long)
{
	return MusicVolume;
}

static bool MenuStepMusicVolume(long, long dir)
{
	long old = MusicVolume;

	sfx_bak = SFXVolume;

	if (dir < 0)
		MusicVolume--;

	if (dir > 0)
		MusicVolume++;

	if (MusicVolume > 100)
		MusicVolume = 100;

	if (MusicVolume < 0)
		MusicVolume = 0;

	ACMSetVolume();
	return MusicVolume != old;
}

static long MenuNumSfxVolume(long)
{
	return SFXVolume;
}

static bool MenuStepSfxVolume(long, long dir)
{
	long old = SFXVolume;

	if (dir < 0)
		SFXVolume--;

	if (dir > 0)
		SFXVolume++;

	if (SFXVolume > 100)
		SFXVolume = 100;

	if (SFXVolume < 0)
		SFXVolume = 0;

	if (SFXVolume != sfx_bak)
	{
		if (sfx_breath_db == -1 || !DSIsChannelPlaying(0))
		{
			S_SoundStopAllSamples();
			sfx_bak = SFXVolume;
			sfx_breath_db = SoundEffect(SFX_LARA_BREATH, 0, SFX_DEFAULT);
			DSChangeVolume(0, -100 * (long(100 - SFXVolume) >> 1));
		}
		else if (sfx_breath_db != -1 && DSIsChannelPlaying(0))
			DSChangeVolume(0, -100 * ((100 - SFXVolume) >> 1));
	}

	return SFXVolume != old;
}

static const char* MenuValSoundQuality(long, char* buf)
{
	switch (SoundQuality)
	{
	case 0:
		strcpy(buf, SCRIPT_TEXT(TXT_Low));
		break;

	case 1:
		strcpy(buf, SCRIPT_TEXT(TXT_Medium));
		break;

	case 2:
		strcpy(buf, SCRIPT_TEXT(TXT_High));
		break;
	}

	return buf;
}

static bool MenuStepSoundQuality(long, long dir)
{
	long old = SoundQuality;

	sfx_bak = SFXVolume;

	if (dir < 0)
		SoundQuality--;

	if (dir > 0)
		SoundQuality++;

	if (SoundQuality > 2)
		SoundQuality = 2;

	if (SoundQuality < 0)
		SoundQuality = 0;

	if (SoundQuality != sfx_quality_bak)
	{
		S_SoundStopAllSamples();
		DXChangeOutputFormat(sfx_frequencies[SoundQuality], 0);
		sfx_quality_bak = SoundQuality;
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	}

	return SoundQuality != old;
}

static const char* MenuValLanguage(long, char* buf)
{
	strcpy(buf, LanguageName(Gameflow->Language));
	return buf;
}

static bool MenuStepLanguage(long, long dir)
{
	if (!dir)
		return 0;

	// The next EXISTING language file in the chosen direction - missing
	// ones are skipped (SwitchLanguage would fail silently otherwise).
	for (long n = Gameflow->Language + dir; n >= 0 && n < gfLanguageCount; n += dir)
	{
		if (SwitchLanguage(n))
		{
			LanguageChoice = n;	// an explicit choice from now on, registry "Language"
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			return 1;
		}
	}

	return 0;
}

/* ------------------------------------------------------------- Controls -- */

static const char* MenuValControlType(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.control_modern ? "Modern" : "Tank"));
	return buf;
}

static bool MenuStepControlType(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.control_modern = !tomb5.control_modern;
	ModernResetControls();		// a stale held course/spin/blocked-counter must not survive a scheme change (game/modern_state.h)
	ModernCameraResetOrbit();	// nor a camera offset seated under the other scheme (#161, round 161a2)
	// AND NOR MAY A BINDING THE NEW SCHEME CANNOT HONOUR: Modern takes the
	// right stick for the camera and Tank keeps the d-pad moving her, so the
	// two roles the schemes disagree about - the medipacks - move with the
	// switch. Deliberately NOT ApplyJoyDefaults(): a player who customised
	// his pad must not lose it for trying the other scheme.
	ApplyJoyScheme();
	// The keyboard's half of the same sentence (#161, round 161b2a, item 1):
	// each scheme keeps its OWN set, so the two change places instead of one
	// being repaired. It recomputes conflict[] as well, because both rows the
	// table is derived from have just moved (specific\input.cpp).
	ApplyKeyScheme();
	return 1;
}

static const char* MenuValCombatMode(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.combat_hold ? "Hold" : "Toggle"));
	return buf;
}

static bool MenuStepCombatMode(long, long dir)
{
	if (!dir)
		return 0;

	// Hold is wired since round 161b2a: S_UpdateInput's weapon seam reads this
	// bit every tick (specific\input.cpp), so flipping the row changes the draw
	// key's meaning at once, with no reload and nothing else to keep in step.
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.combat_hold = !tomb5.combat_hold;
	return 1;
}

static const char* MenuValAction(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.action_split ? "Separate" : "One key"));
	return buf;
}

static bool MenuStepAction(long, long dir)
{
	if (!dir)
		return 0;

	// Same as Combat mode above - drawn and saved, no reader yet.
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.action_split = !tomb5.action_split;
	return 1;
}

/* "Art der Steuerung ... Schlecht zu differenzieren wegen der neuen modernen
 * Steuerung" - so the row is called Control layout now, and it says what it
 * always meant: which set of button names and defaults the bindings below it
 * are written in. Internally ControlMethod is still 0/1 (keyboard/joystick)
 * and the three controller positions are method 1 with JoyStyle 0/1/2, so
 * every "ControlMethod == 1" in the game keeps running unchanged.
 *
 * THE FIFTH POSITION IS GONE. "Reset" was a menu action wearing a setting's
 * clothes; it is the centred action on the Key bindings page now, and with it
 * go base's reset_row flag and the nMask pin that kept the cursor on this row
 * while it was showing. */
static const char* MenuValControlLayout(long, char* buf)
{
	if (!ControlMethod)
	{
		strcpy(buf, SCRIPT_TEXT(TXT_Keyboard));
		return buf;
	}

	// The joystick positions carry the chosen page's name. The page only
	// changes the DISPLAY of the button names (and the defaults on reset) -
	// the bit order comes from the active input path, see input.cpp.
	switch (ActiveJoyStyle())
	{
	case JOYSTYLE_PS5:
		strcpy(buf, "PS5 Controller");
		break;

	case JOYSTYLE_XBOX:
		strcpy(buf, "Xbox Controller");
		break;

	default:
		strcpy(buf, SCRIPT_TEXT(TXT_Joystick));
		break;
	}

	return buf;
}

static bool MenuStepControlLayout(long, long)
{
	long pos, padStep, was_method, was_style;

	was_method = ControlMethod;
	was_style = JoyStyle;
	pos = ControlMethod ? 1 + ActiveJoyStyle() : 0;

	// THIS ROW IGNORES THE FRAME'S OWN DIRECTION on purpose. On the keyboard
	// position the pad delivers no direction bits at all - every use of the
	// stick in input.cpp hangs on ControlMethod - so whoever paged over here
	// with the controller could not page back with it. So the stick is read
	// directly, on BOTH positions: if only on the keyboard one, a single
	// deflection counts twice (JoyMenuStep takes the step, ControlMethod
	// flips to the pad, and next frame the direction still being held reaches
	// dbinput for the first time and its edge detection calls that a fresh
	// press - one push, two pages, seen in game). While the stick is
	// deflected the step belongs to JoyMenuStep alone; the arrow keys keep
	// working through dbinput whenever the stick rests.
	padStep = JoyMenuStep();

	if ((dbinput & IN_LEFT && !JoyMenuDeflected()) || padStep < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		pos--;
	}

	if ((dbinput & IN_RIGHT && !JoyMenuDeflected()) || padStep > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		pos++;
	}

	if (pos > 3)	// was 4 at base - the Reset position left (see above)
		pos = 3;

	if (pos < 0)
		pos = 0;

	if (!pos)
		ControlMethod = 0;
	else
	{
		ControlMethod = 1;
		JoyStyle = pos - 1;	// an explicit choice from now on, not automatic
	}

	return ControlMethod != was_method || JoyStyle != was_style;
}

/* Camera speed and (from step 3) Dead zone are the modern control's own feel,
 * and under Tank neither number is read by anything. A row that cannot do
 * anything is offered in colour 3 and skipped rather than silently ignored. */
static bool MenuEnabledModern(long)
{
	return tomb5.control_modern != 0;
}

static long MenuNumCameraSpeed(long)
{
	return (long)tomb5.camera_speed;
}

static bool MenuStepCameraSpeed(long, long dir)
{
	// "Der rechte Stick muss noch schneller werden, der ist viel zu langsam.
	// Am besten ein Slider wie die SFX und Musik." The NUMBER only ever means
	// anything through game\modern_camera.cpp's ModernCameraSpeedRates - this
	// row just stores and clamps it.
	ulong old = tomb5.camera_speed;

	if (dir < 0 && tomb5.camera_speed > 0)
		tomb5.camera_speed--;

	if (dir > 0 && tomb5.camera_speed < 100)
		tomb5.camera_speed++;

	return tomb5.camera_speed != old;
}

static long MenuNumDeadZone(long)
{
	return (long)tomb5.dead_zone;
}

static bool MenuStepDeadZone(long, long dir)
{
	// "Das ist bei uns momentan noch ein fester Wert, den wie im Remaster
	// toggelbar fuer den User hier einbinden." Stored and clamped here like the
	// row above it; the eight widths it scales, and why 50 is the tuned engine
	// unchanged, live at ModernDeadZoneUser (game\modern_move.cpp).
	ulong old = tomb5.dead_zone;

	if (dir < 0 && tomb5.dead_zone > 0)
		tomb5.dead_zone--;

	if (dir > 0 && tomb5.dead_zone < 100)
		tomb5.dead_zone++;

	return tomb5.dead_zone != old;
}

static const char* MenuValTargeting(long, char* buf)
{
	if (App.AutoTarget)
		strcpy(buf, SCRIPT_TEXT(TXT_Automatic));
	else
		strcpy(buf, SCRIPT_TEXT(TXT_Manual));

	return buf;
}

static bool MenuStepTargeting(long, long dir)
{
	long old = App.AutoTarget;

	if (dir < 0)
	{
		if (App.AutoTarget)
			App.AutoTarget = 0;

		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	}

	if (dir > 0)
	{
		if (!App.AutoTarget)
			App.AutoTarget = 1;

		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	}

	savegame.AutoTarget = (uchar)App.AutoTarget;
	return App.AutoTarget != old;
}

static const char* MenuValAmmotypeHotkeys(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.ammotype_hotkeys ? "on" : "off"));
	return buf;
}

static bool MenuStepAmmotypeHotkeys(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.ammotype_hotkeys = !tomb5.ammotype_hotkeys;
	return 1;
}

/* -------------------------------------------------------------- Display -- */

static const char* MenuValFrameRate(long, char* buf)
{
	/* 30, 60 or the monitor's own rate. "Display" needs VSync - it is the
	 * blocking present that paces it - so with VSync off it runs at 60 and
	 * the row says which of the two the player is really getting. "30"/"60"
	 * are digits, not words, and stay unwrapped; "Display"/"Display (60)"
	 * have no German entry (the brief's table says they stay) - CustomText
	 * falls back to the English literal on its own when nothing matches. */
	strcpy(buf, tomb5.fps_mode == 2 ? CustomText(tomb5.vsync ? "Display" : "Display (60)") :
	            tomb5.fps_mode == 1 ? "60" : "30");
	return buf;
}

static bool MenuStepFrameRate(long, long dir)
{
	// Three values, so LEFT and RIGHT walk the ring in opposite directions
	// rather than both toggling.
	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.fps_mode = tomb5.fps_mode >= 2 ? 0 : tomb5.fps_mode + 1;
		return 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.fps_mode = tomb5.fps_mode == 0 ? 2 : tomb5.fps_mode - 1;
		return 1;
	}

	return 0;
}

static const char* MenuValVSync(long, char* buf)
{
	// The one row in the build that is not instant: VSync is a
	// device-creation parameter (D3D9 has no other way to ask for it), so it
	// is picked up the next time the game starts. The row says so on the
	// right rather than in a manual nobody has open.
	strcpy(buf, CustomText(tomb5.vsync ? "on (restart)" : "off (restart)"));
	return buf;
}

static bool MenuStepVSync(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.vsync = tomb5.vsync ? 0 : 1;
	return 1;
}

/* -------------------------------------------------------------- Graphics -- */
/* Every row below is a case of base's Page0 or Page1 (tomb5\troyestuff.cpp),
 * one function each instead of one shared switch - the row ledger names the
 * base line for each. Toggle rows return `!dir` unconditionally, exactly as
 * the model Display page's VSync row does, because base toggled on EITHER
 * direction (`dbinput & IN_LEFT || dbinput & IN_RIGHT`); wrapping/clamping
 * rows keep base's own per-direction shape, "changed" included where base
 * set it unconditionally inside a direction's block (Distance fog). */

static const char* MenuValFootprints(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.footprints ? "on" : "off"));
	return buf;
}

static bool MenuStepFootprints(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.footprints = !tomb5.footprints;
	return 1;
}

static const char* MenuValShadowMode(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.shadow_mode == 1 ? "Original" : tomb5.shadow_mode == 2 ? "Circle" :
	           tomb5.shadow_mode == 3 ? "Faded circle" : "PSX"));
	return buf;
}

static bool MenuStepShadowMode(long, long dir)
{
	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.shadow_mode = tomb5.shadow_mode >= 4 ? 1 : tomb5.shadow_mode + 1;
		return 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.shadow_mode = tomb5.shadow_mode <= 1 ? 4 : tomb5.shadow_mode - 1;
		return 1;
	}

	return 0;
}

static const char* MenuValFog(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.fog ? "on" : "off"));
	return buf;
}

static bool MenuStepFog(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.fog = !tomb5.fog;
	return 1;
}

static const char* MenuValHdTextures(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.hd_enable ? "on" : "off"));
	return buf;
}

static bool MenuStepHdTextures(long, long dir)
{
	if (!dir)
		return 0;

	// #123: the Remaster's room art, read from the player's own installation.
	// It is taken at the START of a level (base troyestuff.cpp Page0), so
	// this row shows what the NEXT level will do, not the level on screen.
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.hd_enable = !tomb5.hd_enable;
	return 1;
}

static const char* MenuValPsxSkies(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.PSX_skies ? "on" : "off"));
	return buf;
}

static bool MenuStepPsxSkies(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.PSX_skies = !tomb5.PSX_skies;
	return 1;
}

static const char* MenuValMonoScreenStyle(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.inv_bg_mode == 1 ? "Original" : tomb5.inv_bg_mode == 2 ? "TR4" : "Clear"));
	return buf;
}

static bool MenuStepMonoScreenStyle(long, long dir)
{
	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.inv_bg_mode = tomb5.inv_bg_mode >= 3 ? 1 : tomb5.inv_bg_mode + 1;
		return 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.inv_bg_mode = tomb5.inv_bg_mode <= 1 ? 3 : tomb5.inv_bg_mode - 1;
		return 1;
	}

	return 0;
}

static const char* MenuValShimmer(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.shimmer ? "on" : "off"));
	return buf;
}

static bool MenuStepShimmer(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.shimmer = !tomb5.shimmer;
	return 1;
}

static const char* MenuValDistanceFog(long, char* buf)
{
	sprintf(buf, "%i", (int)tomb5.distance_fog);
	return buf;
}

static bool MenuStepDistanceFog(long, long dir)
{
	// changed=1 fires even at the clamp, as it did at base (troyestuff.cpp
	// Page1 case 1<<7) - the save it triggers writes the same value again,
	// harmless, and this round leaves that quirk exactly where base put it.
	bool changed = 0;

	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.distance_fog++;

		if (tomb5.distance_fog > 30)
			tomb5.distance_fog = 30;

		changed = 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.distance_fog--;

		if (tomb5.distance_fog < 3)
			tomb5.distance_fog = 3;

		changed = 1;
	}

	return changed;
}

static const char* MenuValLookTransparency(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.look_transparency ? "on" : "off"));
	return buf;
}

static bool MenuStepLookTransparency(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.look_transparency = !tomb5.look_transparency;
	return 1;
}

static const char* MenuValStaticLighting(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.static_lighting ? "on" : "off"));
	return buf;
}

static bool MenuStepStaticLighting(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.static_lighting = !tomb5.static_lighting;
	return 1;
}

static const char* MenuValUwEffect(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.uw_dust == 1 ? "off" : tomb5.uw_dust == 2 ? "Original" : "TR4"));
	return buf;
}

static bool MenuStepUwEffect(long, long dir)
{
	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.uw_dust = tomb5.uw_dust >= 3 ? 1 : tomb5.uw_dust + 1;
		return 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.uw_dust = tomb5.uw_dust <= 1 ? 3 : tomb5.uw_dust - 1;
		return 1;
	}

	return 0;
}

/* ------------------------------------------------------------------ View -- */
/* All six rows of base's Page2 (troyestuff.cpp), offered on their own tab now
 * and no longer gated by "First person view" (ledger row 26 - "ohne den
 * Switch mit on oder off"). "First person view" itself moves in from Page1
 * as a normal toggle row, first on the tab as the brief's tree has it. */

static const char* MenuValFirstPersonView(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.first_person ? "on" : "off"));
	return buf;
}

static bool MenuStepFirstPersonView(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.first_person = !tomb5.first_person;
	return 1;
}

static const char* MenuValFieldOfView(long, char* buf)
{
	sprintf(buf, "%d", (int)tomb5.first_person_fov);
	return buf;
}

static bool MenuStepFieldOfView(long, long dir)
{
	// Steps of ten from 60 to 100 rather than a free slider (base
	// troyestuff.cpp Page2): the difference between neighbouring values is
	// already plain to see, and a stepped choice says what it is doing.
	if (dir < 0 && tomb5.first_person_fov > 60)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.first_person_fov -= 10;
		return 1;
	}

	if (dir > 0 && tomb5.first_person_fov < 100)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.first_person_fov += 10;
		return 1;
	}

	return 0;
}

static const char* MenuValDeathCamera(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.first_person_death ? "First person" : "Third person"));
	return buf;
}

static bool MenuStepDeathCamera(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.first_person_death = !tomb5.first_person_death;
	return 1;
}

static const char* MenuValFollowHeadBone(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.first_person_head ? "on" : "off"));
	return buf;
}

static bool MenuStepFollowHeadBone(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.first_person_head = !tomb5.first_person_head;
	return 1;
}

static const char* MenuValCameraDamping(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.first_person_smooth == 0 ? "off" :
	           tomb5.first_person_smooth == 1 ? "Light" :
	           tomb5.first_person_smooth == 2 ? "Medium" : "Strong"));
	return buf;
}

static bool MenuStepCameraDamping(long, long dir)
{
	if (dir < 0 && tomb5.first_person_smooth > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.first_person_smooth--;
		return 1;
	}

	if (dir > 0 && tomb5.first_person_smooth < 3)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.first_person_smooth++;
		return 1;
	}

	return 0;
}

static const char* MenuValBody(long, char* buf)
{
	// #123, fix round U/U4: "still" holds her body on one frame while she is
	// standing, crouching or kneeling still - the head still turns with the
	// look, the arms still aim and fire, and every step, jump, climb and pull
	// stays animated as before.
	strcpy(buf, CustomText(tomb5.first_person_body_still ? "still" : "animated"));
	return buf;
}

static bool MenuStepBody(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.first_person_body_still = !tomb5.first_person_body_still;
	return 1;
}

/* ------------------------------------------------------------------- HUD -- */
/* Base's Page0 rows that are about the heads-up display and the load screens,
 * grouped on their own tab so Graphics keeps its headroom (deliberate choice,
 * brief section "The tree he wants"). */

static const char* MenuValBarMode(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.bar_mode == 1 ? "Original" : tomb5.bar_mode == 2 ? "TR4" : "PSX"));
	return buf;
}

static bool MenuStepBarMode(long, long dir)
{
	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.bar_mode = tomb5.bar_mode >= 3 ? 1 : tomb5.bar_mode + 1;
		return 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.bar_mode = tomb5.bar_mode <= 1 ? 3 : tomb5.bar_mode - 1;
		return 1;
	}

	return 0;
}

static const char* MenuValBarPositions(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.bars_pos == 1 ? "Original" : tomb5.bars_pos == 2 ? "Improved" : "PSX"));
	return buf;
}

static bool MenuStepBarPositions(long, long dir)
{
	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.bars_pos = tomb5.bars_pos >= 3 ? 1 : tomb5.bars_pos + 1;
		return 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.bars_pos = tomb5.bars_pos <= 1 ? 3 : tomb5.bars_pos - 1;
		return 1;
	}

	return 0;
}

static const char* MenuValEnemyBars(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.enemy_bars ? "on" : "off"));
	return buf;
}

static bool MenuStepEnemyBars(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.enemy_bars = !tomb5.enemy_bars;
	return 1;
}

static const char* MenuValAmmoCounter(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.ammo_counter ? "on" : "off"));
	return buf;
}

static bool MenuStepAmmoCounter(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.ammo_counter = !tomb5.ammo_counter;
	return 1;
}

static const char* MenuValGameoverMenu(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.gameover ? "on" : "off"));
	return buf;
}

static bool MenuStepGameoverMenu(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.gameover = !tomb5.gameover;
	return 1;
}

static const char* MenuValTr4Loadscreens(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.tr4_loadscreens ? "on" : "off"));
	return buf;
}

static bool MenuStepTr4Loadscreens(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.tr4_loadscreens = !tomb5.tr4_loadscreens;
	return 1;
}

static const char* MenuValLoadbarStyle(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.tr4_loadbar ? "TR4" : "TR5"));	// both stay, no German entry
	return buf;
}

static bool MenuStepLoadbarStyle(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.tr4_loadbar = !tomb5.tr4_loadbar;
	return 1;
}

static const char* MenuValLoadingText(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.loadingtxt ? "on" : "off"));
	return buf;
}

static bool MenuStepLoadingText(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.loadingtxt = !tomb5.loadingtxt;
	return 1;
}

/* ------------------------------------------------------------ tomb5 options -- */
/* What is left of base's Page0/Page1 once Graphics, HUD and View have taken
 * their rows: six switches with no better home yet. */

static const char* MenuValFixClimbUpDelay(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.fix_climb_up_delay ? "on" : "off"));
	return buf;
}

static bool MenuStepFixClimbUpDelay(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.fix_climb_up_delay = !tomb5.fix_climb_up_delay;
	return 1;
}

static const char* MenuValFlexibleCrawling(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.flexible_crawling ? "on" : "off"));
	return buf;
}

static bool MenuStepFlexibleCrawling(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.flexible_crawling = !tomb5.flexible_crawling;
	return 1;
}

static const char* MenuValCutsceneSkipper(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.cutseq_skipper ? "on" : "off"));
	return buf;
}

static bool MenuStepCutsceneSkipper(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.cutseq_skipper = !tomb5.cutseq_skipper;
	return 1;
}

static const char* MenuValCheats(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.enable_cheats ? "on" : "off"));
	return buf;
}

static bool MenuStepCheats(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.enable_cheats = !tomb5.enable_cheats;
	return 1;
}

static const char* MenuValLevelSelect(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.level_select ? "on" : "off"));
	return buf;
}

static bool MenuStepLevelSelect(long, long dir)
{
	if (!dir)
		return 0;

	// Adds/removes the title menu's own "Level Select" row (gameflow.cpp) -
	// a side effect of the variable, not of this handler; carried over
	// unchanged (ledger row 27).
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.level_select = !tomb5.level_select;
	return 1;
}

static const char* MenuValCrawlTilting(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.crawltilt ? "on" : "off"));
	return buf;
}

static bool MenuStepCrawlTilting(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.crawltilt = !tomb5.crawltilt;
	return 1;
}

/* --------------------------------------------------------- Key bindings -- */

/* Waiting for the player to press the thing he wants bound. One role at a
 * time, and the role is the ROW'S OWN param - base had to shift the selection
 * bit back down into an array index at two places and recount the shift every
 * time a row moved (specific\LoadSave.cpp:619-633 and :667-680 at base). */
static bool menu_waiting_for_key;
static long menu_wait_role;
// The action button still pinned the row selection when the wait was entered.
static bool menu_action_still_held;

static const char** MenuKeyboardNames()
{
	// A hardcoded German/everything-else choice, independent of CustomText's
	// five-language table - the key NAMES come from their own scan-code table.
	return Gameflow->Language == 2 ? GermanKeyboard : KeyboardButtons;
}

static const char* MenuValBinding(long role, char* buf)
{
	if (menu_waiting_for_key && menu_wait_role == role)
	{
		strcpy(buf, SCRIPT_TEXT(TXT_Waiting));
		return buf;
	}

	if (!ControlMethod)
		return MenuKeyboardNames()[layout[1][role]];

	if (jLayout[role] == -1)
		return "---";

	return JoyButtonName(jLayout[role]);
}

static void MenuSelectBinding(long role)
{
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	menu_wait_role = role;
	menu_waiting_for_key = 1;
	// The hold guard below (do not immediately capture the action button as
	// the new binding) only applies when the row was REALLY selected with the
	// pad button. If the selection came from the keyboard (Enter), the guard
	// used to PERMANENTLY block any attempt to bind the action button itself.
	menu_action_still_held = ControlMethod == 1 && Key(jLayout[4] + 256);
	memset(keymap, 0, sizeof(keymap));
}

/* "Reset to defaults", the centred action above the list. It is exactly what
 * the Method row's fifth position did: the compiled-in keyboard table AND the
 * pad defaults matching the chosen page and the active input path - and since
 * round 161b2a (item 1) the keyboard table it restores is the ACTIVE SCHEME'S,
 * which at Tank is the row it always was. Reset means "the defaults of the
 * scheme I am playing in", not "Core's defaults whatever I am playing in". */
static void MenuResetBindings(long)
{
	SoundEffect(SFX_MENU_SELECT, 0, 2);
	// THE COPY BASE GOT WRONG (menu-map R3). "memcpy(layout[1], layout, 72)"
	// copied 72 bytes into a destination that starts 36 bytes into a 72-byte
	// object: `short layout[2][18]` is 36 shorts altogether, so the write
	// spanned bytes [36,108) of it and the last 36 landed past the end
	// entirely - `long conflict[18]` is the next global declared in the same
	// file (specific\input.cpp:427), which makes it the plausible target. Source
	// and destination overlapped as well ([0,72) against [36,108)), which is
	// undefined for memcpy on its own. The intent was always "put the
	// compiled-in defaults over the active bindings", and that is one row of
	// the array: 36 bytes, from layout[0] at Tank and from the Modern default
	// row under Modern (KeyDefaultRow, #161 round 161b2a). sizeof(layout[0])
	// is one row whatever the array grows to, which is why it is written that
	// way rather than as 36.
	memmove(layout[1], KeyDefaultRow(), sizeof(layout[0]));
	ApplyJoyDefaults();	// defaults matching page AND input path (input.cpp)
	/* AND THE CONFLICT TABLE IS RECOMPUTED (#161, round 161b2a, item 1 - the
	 * base defect round 161b1 reported and deliberately left alone). Reset
	 * changes every active binding, and conflict[] is derived from them: which
	 * roles may still answer to their default key. Without this it kept the
	 * answer computed for the bindings the player just discarded, until the
	 * next capture happened to recompute it - so a role whose default key had
	 * been claimed stayed silent after a Reset that had freed it again. This is
	 * the one behaviour of this round that a Tank player can see. */
	CheckKeyConflicts();
}

/* ========================================================================= */
/* The pages. Read them as the tree: a centred row opens something, a
 * left-aligned row is a setting.                                            */
/* ========================================================================= */

#define MENU_TAB(lbl, page)			{ MROW_TAB, lbl, -1, 0, 0, MENU_SAVE_NONE, 0, page, 0, 0, 0 }
#define MENU_TAB_TXT_IF(lbl, txt, page, pred)	{ MROW_TAB, lbl, txt, 0, 0, MENU_SAVE_NONE, pred, page, 0, 0, 0 }
#define MENU_ACTION(lbl, act)			{ MROW_ACTION, lbl, -1, 0, 0, MENU_SAVE_NONE, 0, 0, act, 0, 0 }
#define MENU_VALUE(lbl, val, stp, sv)		{ MROW_VALUE, lbl, -1, val, stp, sv, 0, 0, 0, 0, 0 }
#define MENU_VALUE_TXT(lbl, txt, val, stp, sv)	{ MROW_VALUE, lbl, txt, val, stp, sv, 0, 0, 0, 0, 0 }
#define MENU_SLIDER(lbl, txt, num, stp, sv, pred) { MROW_SLIDER, lbl, txt, 0, stp, sv, pred, 0, 0, num, 0 }
#define MENU_BIND(lbl, txt, role)		{ MROW_BINDING, lbl, txt, MenuValBinding, 0, MENU_SAVE_GAME, 0, 0, MenuSelectBinding, 0, role }

// Special Features is a title-screen thing: gfGameMode is 1 only there, and
// the pause menu cannot reach it at all (game\control.cpp gates the pause key
// on !gfGameMode). Offered in colour 3 in a pause session rather than silently
// missing, which is what the old page did.
static bool MenuEnabledSpecialFeatures(long)
{
	return gfGameMode == 1;
}

static const menu_row menu_rows_options[] =
{
	MENU_TAB("Audio", MENU_PAGE_AUDIO),
	MENU_TAB("Controls", MENU_PAGE_CONTROLS),
	MENU_TAB("Graphics", MENU_PAGE_GRAPHICS),
	MENU_TAB("HUD", MENU_PAGE_HUD),
	MENU_TAB("tomb5 options", MENU_PAGE_TOMB5),
	MENU_TAB_TXT_IF("Special Features", TXT_Special_Features, MENU_PAGE_SPECIAL, MenuEnabledSpecialFeatures),
	MENU_TAB("Credits", MENU_PAGE_CREDITS)
};

static const menu_row menu_rows_audio[] =
{
	MENU_SLIDER("Music Volume", TXT_Music_Volume, MenuNumMusicVolume, MenuStepMusicVolume, MENU_SAVE_GAME, 0),
	MENU_SLIDER("SFX Volume", TXT_SFX_Volume, MenuNumSfxVolume, MenuStepSfxVolume, MENU_SAVE_GAME, 0),
	MENU_VALUE_TXT("Sound Quality", TXT_Sound_Quality, MenuValSoundQuality, MenuStepSoundQuality, MENU_SAVE_GAME),
	MENU_VALUE("Language", MenuValLanguage, MenuStepLanguage, MENU_SAVE_GAME)
};

static const menu_row menu_rows_controls[] =
{
	MENU_TAB("Key bindings", MENU_PAGE_KEYBINDS),
	MENU_TAB("View", MENU_PAGE_VIEW),
	MENU_VALUE("Control type", MenuValControlType, MenuStepControlType, MENU_SAVE_TOMB5),
	MENU_VALUE("Combat mode", MenuValCombatMode, MenuStepCombatMode, MENU_SAVE_TOMB5),
	MENU_VALUE("Action", MenuValAction, MenuStepAction, MENU_SAVE_TOMB5),
	MENU_VALUE("Control layout", MenuValControlLayout, MenuStepControlLayout, MENU_SAVE_GAME),
	MENU_SLIDER("Camera speed", -1, MenuNumCameraSpeed, MenuStepCameraSpeed, MENU_SAVE_TOMB5, MenuEnabledModern),
	// #161, round 161b1 item 5 - the same slider, the same predicate, in the
	// place the user's tree gives it: right after Camera speed.
	MENU_SLIDER("Dead zone", -1, MenuNumDeadZone, MenuStepDeadZone, MENU_SAVE_TOMB5, MenuEnabledModern),
	MENU_VALUE_TXT("Targeting", TXT_Targeting, MenuValTargeting, MenuStepTargeting, MENU_SAVE_GAME),
	MENU_VALUE("Ammotype hotkeys", MenuValAmmotypeHotkeys, MenuStepAmmotypeHotkeys, MENU_SAVE_TOMB5)
};

// All six always offered, not gated by tomb5.first_person (ledger row 26).
static const menu_row menu_rows_view[] =
{
	MENU_VALUE("First person view", MenuValFirstPersonView, MenuStepFirstPersonView, MENU_SAVE_TOMB5),
	MENU_VALUE("Field of view", MenuValFieldOfView, MenuStepFieldOfView, MENU_SAVE_TOMB5),
	MENU_VALUE("Death camera", MenuValDeathCamera, MenuStepDeathCamera, MENU_SAVE_TOMB5),
	MENU_VALUE("Follow head bone", MenuValFollowHeadBone, MenuStepFollowHeadBone, MENU_SAVE_TOMB5),
	MENU_VALUE("Camera damping", MenuValCameraDamping, MenuStepCameraDamping, MENU_SAVE_TOMB5),
	MENU_VALUE("Body", MenuValBody, MenuStepBody, MENU_SAVE_TOMB5)
};

static const menu_row menu_rows_graphics[] =
{
	MENU_TAB("Display", MENU_PAGE_DISPLAY),
	MENU_VALUE("HD textures", MenuValHdTextures, MenuStepHdTextures, MENU_SAVE_TOMB5),
	MENU_VALUE("Fog", MenuValFog, MenuStepFog, MENU_SAVE_TOMB5),
	MENU_VALUE("Distance fog", MenuValDistanceFog, MenuStepDistanceFog, MENU_SAVE_TOMB5),
	MENU_VALUE("Shimmer", MenuValShimmer, MenuStepShimmer, MENU_SAVE_TOMB5),
	MENU_VALUE("Mono screen style", MenuValMonoScreenStyle, MenuStepMonoScreenStyle, MENU_SAVE_TOMB5),
	MENU_VALUE("Shadow mode", MenuValShadowMode, MenuStepShadowMode, MENU_SAVE_TOMB5),
	MENU_VALUE("PSX skies", MenuValPsxSkies, MenuStepPsxSkies, MENU_SAVE_TOMB5),
	MENU_VALUE("Static lighting", MenuValStaticLighting, MenuStepStaticLighting, MENU_SAVE_TOMB5),
	MENU_VALUE("UW Effect", MenuValUwEffect, MenuStepUwEffect, MENU_SAVE_TOMB5),
	MENU_VALUE("Look transparency", MenuValLookTransparency, MenuStepLookTransparency, MENU_SAVE_TOMB5),
	MENU_VALUE("Footprints", MenuValFootprints, MenuStepFootprints, MENU_SAVE_TOMB5)
};

static const menu_row menu_rows_display[] =
{
	MENU_VALUE("Frame rate", MenuValFrameRate, MenuStepFrameRate, MENU_SAVE_TOMB5),
	MENU_VALUE("VSync", MenuValVSync, MenuStepVSync, MENU_SAVE_TOMB5)
};

static const menu_row menu_rows_hud[] =
{
	MENU_VALUE("Bar mode", MenuValBarMode, MenuStepBarMode, MENU_SAVE_TOMB5),
	MENU_VALUE("Bar positions", MenuValBarPositions, MenuStepBarPositions, MENU_SAVE_TOMB5),
	MENU_VALUE("Enemy bars", MenuValEnemyBars, MenuStepEnemyBars, MENU_SAVE_TOMB5),
	MENU_VALUE("Ammo counter", MenuValAmmoCounter, MenuStepAmmoCounter, MENU_SAVE_TOMB5),
	MENU_VALUE("Gameover menu", MenuValGameoverMenu, MenuStepGameoverMenu, MENU_SAVE_TOMB5),
	MENU_VALUE("TR4 loadscreens", MenuValTr4Loadscreens, MenuStepTr4Loadscreens, MENU_SAVE_TOMB5),
	MENU_VALUE("Loadbar style", MenuValLoadbarStyle, MenuStepLoadbarStyle, MENU_SAVE_TOMB5),
	MENU_VALUE("Loading text", MenuValLoadingText, MenuStepLoadingText, MENU_SAVE_TOMB5)
};

static const menu_row menu_rows_tomb5[] =
{
	MENU_VALUE("Cutscene skipper", MenuValCutsceneSkipper, MenuStepCutsceneSkipper, MENU_SAVE_TOMB5),
	MENU_VALUE("Cheats", MenuValCheats, MenuStepCheats, MENU_SAVE_TOMB5),
	// ONE literal for the two places this option is named (brief item 4): the
	// title screen's own row has said "Level Select" since round 161a, and a
	// second spelling would have been a second CustomText key for one setting.
	MENU_VALUE("Level Select", MenuValLevelSelect, MenuStepLevelSelect, MENU_SAVE_TOMB5),
	MENU_VALUE("Fix climb up delay", MenuValFixClimbUpDelay, MenuStepFixClimbUpDelay, MENU_SAVE_TOMB5),
	MENU_VALUE("Flexible crawling", MenuValFlexibleCrawling, MenuStepFlexibleCrawling, MENU_SAVE_TOMB5),
	MENU_VALUE("Crawl Tilting", MenuValCrawlTilting, MenuStepCrawlTilting, MENU_SAVE_TOMB5)
};

/* The two bindable lists. Row 0 of each is the centred reset action, and it is
 * the page's one FIXED row - it is repeated on every page of the split list,
 * above it, which is where the user's screen has it.
 *
 * The keyboard list's first four rows are glyphs rather than words (up, down,
 * left, right) exactly as at base: they are layout slots 0-3, Lara's Tank
 * movement keys, which were never given a text label in the string wad.
 * CustomText hands an unknown literal straight back, so the glyphs go through
 * the same path as every other label without a special case. */
static const menu_row menu_rows_keybinds_kbd[] =
{
	MENU_ACTION("Reset to defaults", MenuResetBindings),
	MENU_BIND("\x18", -1, 0),
	MENU_BIND("\x1A", -1, 1),
	MENU_BIND("\x19", -1, 2),
	MENU_BIND("\x1B", -1, 3),
	MENU_BIND("Duck", TXT_Duck, 4),
	MENU_BIND("Dash", TXT_Dash, 5),
	MENU_BIND("Walk", TXT_Walk, 6),
	MENU_BIND("Jump", TXT_Jump, 7),
	MENU_BIND("Action", TXT_Action, 8),
	MENU_BIND("Draw Weapon", TXT_Draw_Weapon, 9),
	MENU_BIND("Use Flare", TXT_Use_Flare, 10),
	MENU_BIND("Look", TXT_Look, 11),
	MENU_BIND("Roll", TXT_Roll, 12),
	MENU_BIND("Inventory", TXT_Inventory, 13),
	MENU_BIND("Step Left", TXT_Step_Left, 14),
	MENU_BIND("Step Right", TXT_Step_Right, 15),
	// LAST ON PURPOSE, IN BOTH LISTS: MenuResolveKeybinds hands out one row
	// fewer with "One key", so the row that disappears is this one and every
	// row above it keeps its place on the page (#161, round 161b2c, item 2).
	MENU_BIND("Secondary", -1, KEY_ROLE_SECOND)
};

static const menu_row menu_rows_keybinds_joy[] =
{
	MENU_ACTION("Reset to defaults", MenuResetBindings),
	MENU_BIND("Duck", TXT_Duck, 0),
	MENU_BIND("Dash", TXT_Dash, 1),
	MENU_BIND("Walk", TXT_Walk, 2),
	MENU_BIND("Jump", TXT_Jump, 3),
	MENU_BIND("Action", TXT_Action, 4),
	MENU_BIND("Draw Weapon", TXT_Draw_Weapon, 5),
	MENU_BIND("Use Flare", TXT_Use_Flare, 6),
	MENU_BIND("Look", TXT_Look, 7),
	MENU_BIND("Roll", TXT_Roll, 8),
	MENU_BIND("Inventory", TXT_Inventory, 9),
	MENU_BIND("Pause", -1, 10),
	MENU_BIND("Save", TXT_Save, 11),
	MENU_BIND("Load", TXT_Load, 12),
	MENU_BIND("Small Medipack", TXT_Small_Medipack, 13),
	MENU_BIND("Large Medipack", TXT_Large_Medipack, 14),
	MENU_BIND("Secondary", -1, JOY_ROLE_SECOND)
};

/* THE SECONDARY ROW IS OFFERED ONLY UNDER "Separate" (#161, round 161b2c,
 * item 2). With "One key" the role produces nothing at all - the two
 * producers in specific\input.cpp do not even call Key() for it - so a row
 * that let the player bind it would be a row that does nothing, and the page
 * has to look exactly as it did at base. This page already builds its list at
 * run time, which is why dropping the last row costs one line and no second
 * pair of tables. */
static void MenuResolveKeybinds(const menu_row** rows, long* num)
{
	if (ControlMethod == 1)
	{
		*rows = menu_rows_keybinds_joy;
		*num = sizeof(menu_rows_keybinds_joy) / sizeof(menu_rows_keybinds_joy[0]);
	}
	else
	{
		*rows = menu_rows_keybinds_kbd;
		*num = sizeof(menu_rows_keybinds_kbd) / sizeof(menu_rows_keybinds_kbd[0]);
	}

	if (!tomb5.action_split)
		(*num)--;
}

#define MENU_ROWS(a)	a, sizeof(a) / sizeof(a[0])

// POSITIONAL: this order must match enum menu_page_id exactly (menu_frame.h).
static const menu_page menu_pages[MENU_PAGE_COUNT] =
{
	{ "Options",      TXT_Options, MENU_ROWS(menu_rows_options),  0, 0 },
	{ "Audio",        -1,          MENU_ROWS(menu_rows_audio),    0, 0 },
	{ "Controls",     -1,          MENU_ROWS(menu_rows_controls), 0, 0 },
	{ "Key bindings", -1,          0, 0,                          1, MenuResolveKeybinds },
	{ "View",         -1,          MENU_ROWS(menu_rows_view),     0, 0 },
	{ "Graphics",     -1,          MENU_ROWS(menu_rows_graphics), 0, 0 },
	{ "Display",      -1,          MENU_ROWS(menu_rows_display),  0, 0 },
	{ "HUD",          -1,          MENU_ROWS(menu_rows_hud),      0, 0 },
	{ "tomb5 options", -1,         MENU_ROWS(menu_rows_tomb5),    0, 0 },
	{ "Special Features", -1,      0, 0,                          0, 0 },	// the existing screen
	{ "Credits",      -1,          0, 0,                          0, 0 }	// the existing screen
};

/* ========================================================================= */
/* The frame itself.                                                         */
/* ========================================================================= */

// Where the player is. The stack is what makes IN_DESELECT mean "back to the
// page that opened this one, with the cursor on the row that did it" without
// any page having to know its own parent.
static long menu_stack[8];
static long menu_depth;
// False between two visits, so that the first frame of a visit knows it is one
// and MENULOG says so.
static bool menu_open;
static long menu_cursor[MENU_PAGE_COUNT];
static long menu_page_no[MENU_PAGE_COUNT];

/* ITEM 3: the "Game" registry block is written when it changes. Until this
 * round every key rebind, volume drag, sound quality change, targeting toggle,
 * language switch and control layout change lived in memory until a clean exit
 * (menu-map R4) - a crash threw the lot away while every tomb5 toggle was
 * already on disk. The flag is set by the rows whose save is MENU_SAVE_GAME
 * and cleared by the one call, when the page carrying them is left. Never per
 * frame: SaveSettings() opens the key and writes about forty values. */
static bool menu_game_dirty;

// A slider is held down on purpose, so it is written when it stops moving, not
// while it moves. Which slider is waiting is remembered rather than which page,
// because only one row can be selected at a time.
static const menu_row* menu_slider_unsaved;

static void MenuLogEnter(long page)
{
	if (gLogEnabled)
		Log("MENULOG enter page=%s", menu_pages[page].name);
}

static void MenuLogSet(long page, const menu_row* row, const char* value)
{
	if (gLogEnabled)
		Log("MENULOG set page=%s row=%s value=%s", menu_pages[page].name, row->label, value ? value : "");
}

// Block A is written whole on every edit, as it always was; block B is only
// marked here and written when the page is left.
static void MenuAfterChange(long page, const menu_row* row, const char* value)
{
	MenuLogSet(page, row, value);

	if (row->save == MENU_SAVE_TOMB5)
		save_new_tomb5_settings();
	else if (row->save == MENU_SAVE_GAME)
		menu_game_dirty = 1;
}

static void MenuFlushGame()
{
	if (!menu_game_dirty)
		return;

	menu_game_dirty = 0;
	// SaveSettings() also writes the window position and the display mode
	// (specific\registry.cpp:462-468). Both are live values at any moment, so
	// writing them mid-session writes exactly what a clean exit would have
	// written then - measured before this call was added.
	SaveSettings();
}

// A slider that stopped moving, or whose row or page was left.
static void MenuSettleSlider(long page)
{
	const menu_row* row = menu_slider_unsaved;
	char buf[32];

	if (!row)
		return;

	menu_slider_unsaved = 0;
	sprintf(buf, "%d", (int)(row->slider ? row->slider(row->param) : 0));
	MenuAfterChange(page, row, buf);
}

static void MenuGoTo(long page)
{
	MenuSettleSlider(menu_stack[menu_depth]);
	MenuFlushGame();

	if (menu_depth < (long)(sizeof(menu_stack) / sizeof(menu_stack[0])) - 1)
		menu_depth++;

	menu_stack[menu_depth] = page;
	menu_cursor[page] = 0;
	menu_page_no[page] = 0;
	MenuLogEnter(page);
}

static void MenuBack()
{
	MenuSettleSlider(menu_stack[menu_depth]);
	MenuFlushGame();

	if (menu_depth > 0)
		menu_depth--;

	MenuLogEnter(menu_stack[menu_depth]);
}

// Leaving Options altogether: the next entry starts on the Options page, from
// the title screen and from the pause menu alike. DoOptions() itself is one
// function shared by both callers (menu-map R1), so without this the menu
// would resume wherever the last session left it.
static void MenuLeave()
{
	MenuSettleSlider(menu_stack[menu_depth]);
	MenuFlushGame();
	menu_depth = 0;
	menu_stack[0] = MENU_PAGE_OPTIONS;
	menu_cursor[MENU_PAGE_OPTIONS] = 0;
	menu_page_no[MENU_PAGE_OPTIONS] = 0;
	menu_waiting_for_key = 0;
	menu_open = 0;
}

/* The continuous word, not the debounced one: a feel slider is meant to be
 * held down, not tapped once. LEFT wins if both ever arrived together, which
 * no hand and no stick can do - base would have applied both and netted zero. */
static long MenuSliderDir()
{
	if (input & IN_LEFT || keymap[DIK_LEFT])
		return -1;

	if (input & IN_RIGHT || keymap[DIK_RIGHT])
		return 1;

	return 0;
}

static long MenuStepDir()
{
	if (dbinput & IN_LEFT)
		return -1;

	if (dbinput & IN_RIGHT)
		return 1;

	return 0;
}

/* A finished rebind is a value change like any other, so it goes through the
 * frame's own path: the seam names it and the Game block is marked. It used to
 * set menu_game_dirty by hand, which saved correctly but left MENULOG silent on
 * the one page whose whole purpose is changing values (found by review). The
 * role's row is looked up rather than passed, because the capture runs from the
 * drive loop's head, before the row under the cursor is fetched. */
static void MenuBindingChanged()
{
	const menu_row* rows;
	const menu_row* row;
	char buf[256];
	long num, i;

	MenuResolveKeybinds(&rows, &num);

	for (i = 0; i < num; i++)
	{
		row = &rows[i];

		if (row->kind == MROW_BINDING && row->param == menu_wait_role)
		{
			buf[0] = 0;
			MenuAfterChange(MENU_PAGE_KEYBINDS, row, row->value ? row->value(row->param, buf) : "");
			return;
		}
	}

	// A role with no row of its own would still have to be saved.
	menu_game_dirty = 1;
}

/* Waiting for a key or a button. Carried over from base's DoOptions()
 * (specific\LoadSave.cpp:596-730) with one change: the role is
 * menu_wait_role, so the two shift-then-count blocks that turned the
 * selection bit back into an array index are gone. */
static void MenuCaptureBinding()
{
	long jread, jx, jy, lp, lp2, i;

	if (keymap[DIK_ESCAPE])
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		dbinput = 0;
		menu_waiting_for_key = 0;
		return;
	}

	if (!ControlMethod)
	{
		const char** names = MenuKeyboardNames();

		for (lp = 0; lp < 255; lp++)
		{
			if (keymap[lp] && names[lp])
			{
				// Return and the four arrows are the menu's own keys and
				// must not be bound over the navigation.
				if (lp != DIK_RETURN && lp != DIK_LEFT && lp != DIK_RIGHT && lp != DIK_UP && lp != DIK_DOWN)
				{
					menu_waiting_for_key = 0;
					layout[1][menu_wait_role] = (short)lp;
					MenuBindingChanged();
				}
			}
		}
	}

	if (ControlMethod == 1)
	{
		jread = ReadJoystick(jx, jy);

		// Drop the hold guard as soon as the button is actually up, and check
		// that on EVERY frame of the wait - it used to sit inside "if (jread)"
		// below, and jread is zero exactly when the user lets go, so the
		// release was never seen and the Action button could not be bound to
		// itself at all.
		if (!Key(jLayout[4] + 256))
			menu_action_still_held = 0;

		if (jread)
		{
			lp = jread >> 1;
			lp2 = 0;

			while (lp)
			{
				lp >>= 1;
				lp2++;
			}

			// Ignore the button currently bound to Action/Select - it is what
			// the user just pressed to select this row, and is very likely
			// still physically held on this very next frame (this capture
			// reads the raw, non-debounced joystick state). The keyboard loop
			// above excludes DIK_RETURN for exactly the same reason.
			if (lp2 != jLayout[4] || !menu_action_still_held)
			{
				jLayout[menu_wait_role] = lp2;

				// If another action already used this same button or
				// stick direction, unbind it (-1 marks unbound).
				for (i = 0; i < JOY_ROLES; i++)
					if (i != menu_wait_role && jLayout[i] == lp2)
						jLayout[i] = -1;

				menu_waiting_for_key = 0;
				MenuBindingChanged();
			}
		}
	}

	CheckKeyConflicts();
	dbinput = 0;
}

/* The two screens that still keep their own code: Special Features and the
 * Credits (troyestuff.cpp). "tomb5 options" left this list in step 2 - it is
 * a table like any other page now. They are pages of the tree like any
 * other; only what happens INSIDE them is still theirs. */
static ulong menu_legacy_sel;

static bool MenuRunLegacy(long page)
{
	long legacy_menu;

	legacy_menu = 1;

	if (page == MENU_PAGE_CREDITS)
		CreditsScreen(font_height - (font_height >> 1), legacy_menu, menu_legacy_sel, menu_legacy_sel);
	else if (page == MENU_PAGE_SPECIAL)
		legacy_menu = SpecialFeaturesPageRun() ? 0 : 1;
	else
		return 0;

	if (!legacy_menu)
		MenuBack();

	return 1;
}

static void MenuEnterLegacy(long page)
{
	if (page == MENU_PAGE_SPECIAL)
		CalculateNumSpecialFeatures();
}

void MenuFrameRun()
{
	const menu_page* pg;
	const menu_row* rows;
	const menu_row* row;
	unsigned char enabled[MENU_MAX_ROWS + 2];
	long visible[MENU_MAX_ROWS + 1];
	char buf[256];
	long page, num, fixed, body, body_max, pages, page_no, first, shown;
	long nvis, ncur, cur, i, y, dir;
	uchar col;
	bool slider_page;

	if (!(sfx_backup_flag & 1))
	{
		sfx_backup_flag |= 1;
		sfx_bak = SFXVolume;
	}

	if (!(sfx_backup_flag & 2))
	{
		sfx_backup_flag |= 2;
		sfx_quality_bak = SoundQuality;
	}

	page = menu_stack[menu_depth];

	if (!menu_open)
	{
		menu_open = 1;
		MenuLogEnter(page);
	}

	if (MenuRunLegacy(page))
	{
		// The pause key closes the whole menu from wherever the player stands,
		// and the two legacy screens do not consume it (CreditsScreen takes
		// only IN_DESELECT|IN_SELECT) - so without this the caller would shut
		// Options while the frame still had Credits on its stack, and the next
		// visit would open straight into it. Found by review; the same line
		// stands at the end of the ordinary path below, so a legacy page now
		// leaves the tree exactly the way every other page does.
		if (dbinput & IN_PAUSE)
			MenuLeave();

		return;
	}

	pg = &menu_pages[page];

	if (pg->resolve)
		pg->resolve(&rows, &num);
	else
	{
		rows = pg->rows;
		num = pg->num_rows;
	}

	/* WHICH ROWS THIS PAGE SHOWS. The fixed rows come first and are on every
	 * page of a split list; the rest is paged in balanced halves
	 * (game\menu_nav.h). "Wenn nicht alle Keybinds auf eine Seite passen,
	 * machen wir das wie in den Tomb-Optionen mit mehreren Seiten nach links
	 * und rechts mit den Pfeilen am unteren Bildschirmrand." */
	fixed = pg->fixed_rows;
	body = num - fixed;
	body_max = MENU_MAX_ROWS - fixed;
	pages = MenuPageCount(body, body_max);
	page_no = menu_page_no[page];

	if (page_no >= pages)
		page_no = 0;

	first = MenuPageFirstRow(body, body_max, page_no);
	shown = MenuRowsOnPage(body, body_max, page_no);
	nvis = 0;

	for (i = 0; i < fixed; i++)
		visible[nvis++] = i;

	for (i = 0; i < shown; i++)
		visible[nvis++] = fixed + first + i;

	for (i = 0; i < nvis; i++)
	{
		row = &rows[visible[i]];
		enabled[i] = (unsigned char)(row->enabled ? (row->enabled(row->param) ? 1 : 0) : 1);
	}

	ncur = nvis;

	if (pages > 1)
		enabled[ncur++] = 1;	// the arrow row is a row of the cursor's, as it is in the tomb5 options

	cur = MenuClampCursor(enabled, ncur, menu_cursor[page]);
	menu_cursor[page] = cur;
	menu_page_no[page] = page_no;

	/* ----------------------------------------------------------- draw -- */

	if (pg->title_id >= 0)
		PrintString(phd_centerx, MENU_TITLE_ROW * font_height, 6, SCRIPT_TEXT(pg->title_id), FF_CENTER);
	else
		PrintString(phd_centerx, MENU_TITLE_ROW * font_height, 6, (char*)CustomText(pg->name), FF_CENTER);

	for (i = 0; i < nvis; i++)
	{
		row = &rows[visible[i]];
		y = (MENU_FIRST_ROW + i) * font_height;
		col = (uchar)(!enabled[i] ? 3 : (cur == i ? 1 : 2));
		buf[0] = 0;

		if (row->kind == MROW_TAB || row->kind == MROW_ACTION)
		{
			// Centred: this row opens or does something. That is the whole
			// difference the player is meant to read off the screen.
			if (row->text_id >= 0)
				PrintString(phd_centerx, y, col, SCRIPT_TEXT(row->text_id), FF_CENTER);
			else
				PrintString(phd_centerx, y, col, (char*)CustomText(row->label), FF_CENTER);

			continue;
		}

		if (row->text_id >= 0)
			PrintString(phd_centerx >> 2, y, col, SCRIPT_TEXT(row->text_id), 0);
		else
			PrintString(phd_centerx >> 2, y, col, (char*)CustomText(row->label), 0);

		if (row->kind == MROW_SLIDER)
		{
			// x = 400 and the "+ 4 - (font_height >> 1)" centring term inside
			// a taller text row are the options page's own slider geometry,
			// kept rather than re-derived (specific\LoadSave.cpp:1021 at base).
			DoSlider(400, y + 4 - (font_height >> 1), 200, 16, row->slider ? row->slider(row->param) : 0,
				0x1F1F1F, 0x3F3FFF, cur == i && enabled[i] ? 0x7F7F7F : 0x3F3F3F);
			continue;
		}

		if (row->value)
			PrintString(phd_centerx + (phd_centerx >> 2), y, (uchar)(!enabled[i] ? 3 : (cur == i ? 1 : 6)),
				(char*)row->value(row->param, buf), 0);
	}

	if (pages > 1)
	{
		// One line, pinned to the bottom of the screen whatever the page is -
		// MENU_BOTTOM_ROW, never after the last drawn row - and drawn only
		// while the list really has more than one page, which is the `if`
		// above. The glyphs are the ones the tomb5 options already page with
		// (tomb5\troyestuff.cpp:120-121).
		col = (uchar)(cur == nvis ? 1 : 6);
		PrintString(phd_centerx - (phd_centerx >> 3), MENU_BOTTOM_ROW * font_height, col, "\x19", 0);
		PrintString(phd_centerx + (phd_centerx >> 3), MENU_BOTTOM_ROW * font_height, col, "\x1B", 0);
	}

	/* ---------------------------------------------------------- drive -- */

	if (menu_waiting_for_key)
		MenuCaptureBinding();	// clears dbinput, so nothing below fires this frame

	if (dbinput & IN_FORWARD)
	{
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		cur = MenuCursorStep(enabled, ncur, cur, -1);
	}

	if (dbinput & IN_BACK)
	{
		SoundEffect(SFX_MENU_CHOOSE, 0, SFX_ALWAYS);
		cur = MenuCursorStep(enabled, ncur, cur, 1);
	}

	if (cur != menu_cursor[page])
	{
		// The cursor left a slider that was still unwritten.
		MenuSettleSlider(page);
		menu_cursor[page] = cur;
	}

	slider_page = 0;

	if (pages > 1 && cur == nvis)
	{
		// The arrow row. Both arrows, both directions, and it wraps.
		dir = MenuStepDir();

		if (dir)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			menu_page_no[page] = MenuPageStep(page_no, pages, dir);
			// Stay on the arrows - the next page may have a different
			// number of rows, so the clamp is asked for the last one
			// rather than a number worked out here.
			menu_cursor[page] = MENU_MAX_ROWS + 1;
		}
	}
	else if (cur >= 0 && cur < nvis && enabled[cur])
	{
		row = &rows[visible[cur]];

		switch (row->kind)
		{
		case MROW_TAB:

			if (dbinput & IN_SELECT)
			{
				SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
				MenuEnterLegacy(row->target);
				MenuGoTo(row->target);
				return;
			}

			break;

		case MROW_ACTION:

			if (dbinput & IN_SELECT && row->select)
			{
				row->select(row->param);
				menu_game_dirty = 1;	// every action in the tree today changes block B
			}

			break;

		case MROW_BINDING:

			if (dbinput & IN_SELECT && row->select)
				row->select(row->param);

			break;

		case MROW_SLIDER:

			slider_page = 1;

			if (row->step)
			{
				dir = MenuSliderDir();

				if (row->step(row->param, dir))
					menu_slider_unsaved = row;
				else
					MenuSettleSlider(page);
			}

			break;

		case MROW_VALUE:
		default:

			if (row->step)
			{
				buf[0] = 0;

				if (row->step(row->param, MenuStepDir()))
					MenuAfterChange(page, row, row->value ? row->value(row->param, buf) : "");
			}

			break;
		}
	}

	if (!slider_page)
		MenuSettleSlider(page);

	if (dbinput & IN_DESELECT)
	{
		if (menu_depth > 0)
		{
			SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
			MenuBack();
			dbinput &= ~IN_DESELECT;
		}
		else
			MenuLeave();	// the caller sees the bit and closes the menu
	}

	if (dbinput & IN_PAUSE)
		MenuLeave();		// the pause menu's own way out, from any depth
}
