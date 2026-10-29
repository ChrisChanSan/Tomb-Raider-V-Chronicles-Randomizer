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
#include "camera.h"			// ModernCameraResetOrbit - the Control type row
#include "modern_state.h"		// ModernResetControls - the same row
#include "savegame.h"
#include "../specific/input.h"
#include "../specific/dxshell.h"		// keymap - the sliders and the key capture read it raw
#include "../specific/winmain.h"		// App - the Targeting row
#include "../specific/3dmath.h"		// phd_centerx - the geometry
#include "../specific/LoadSave.h"
#include "../specific/registry.h"
#include "../specific/drawbars.h"
#include "../specific/output.h"		// GetFixedScale - the slider's number
#include "../specific/loadbar_math.h"	// LOADBAR_* - the Loadbar style row
#include "../specific/audio.h"
#include "../specific/dxsound.h"
#include "../specific/function_stubs.h"	// Log, gLogEnabled - the MENULOG seam
#include "../specific/hd/hd_level.h"		// HDEnabled - the Shadow mode row's fifth state
#include "../specific/hd/hd_shadow.h"		// HDShadowBlobMode - what a stored 5 draws without HD
#include "../specific/hd/hd_switch.h"	// HDSwitchOption, HDDrawn - HD rows
#include "../specific/hd/hd_render.h"	// HDRenderMarbleSet - Reflections
#include "../tomb5/tomb5.h"
#include "../tomb5/troyestuff.h"

/* ========================================================================= */
/* The rows' own logic. Every function below is a block of base's DoOptions()
 * (specific\LoadSave.cpp) or of a Page* function (tomb5\troyestuff.cpp),
 * carried over statement for statement - the sounds included, because several
 * rows play one even when the value does not move and that is what the player
 * hears today. The return value is the one thing that is new: true when the
 * setting really changed, which is what decides whether anything is written
 * and whether MENULOG prints a line.
 *
 * The frame calls step() on every frame the cursor stands on the row, with
 * dir 0 when no direction is held. That is deliberate: base's rows are
 * written as "if (sel & bit) { ... }" blocks and several of them do work with
 * no input at all (Music volume calls ACMSetVolume, Targeting mirrors into
 * savegame), so a block that only ran on a keypress would not be the same
 * block.                                                                    */
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

	if (MusicVolume > MENU_SLIDER_TOP)
		MusicVolume = MENU_SLIDER_TOP;

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

	if (SFXVolume > MENU_SLIDER_TOP)
		SFXVolume = MENU_SLIDER_TOP;

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

	// The next existing language file in the chosen direction - missing
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
	ModernCameraResetOrbit();	// nor a camera offset seated under the other scheme
	// A binding the new scheme cannot honour must not survive either:
	// Modern takes the right stick for the camera and Tank keeps the
	// d-pad moving her, so the two roles the schemes disagree about -
	// the medipacks - move with the switch. Deliberately not
	// ApplyJoyDefaults(): a player who customised his pad must not lose
	// it for trying the other scheme.
	ApplyJoyScheme();
	// The keyboard's half of the same switch: each scheme keeps its own
	// set, so the two change places instead of one being repaired. It
	// recomputes conflict[] as well, because both rows the table is
	// derived from have just moved (specific/input.cpp).
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

	// S_UpdateInput's weapon seam reads this bit every tick
	// (specific/input.cpp), so flipping the row changes the draw key's
	// meaning at once, with no reload and nothing else to keep in step.
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.combat_hold = !tomb5.combat_hold;
	return 1;
}

/* Sprint mode and Duck mode: two rows of the same shape as Combat mode
 * above, placed directly under it alongside the Action split setting.
 *
 * The value words are the other way round from Combat mode's on
 * purpose: there 0 is Toggle, because the engine's own draw key is
 * already a toggle; here 0 is Hold, because the engine's own sprint and
 * duck keys are held. Both rows say "off = what the engine does today",
 * which is the rule, not the word. */
static const char* MenuValSprintMode(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.sprint_toggle ? "Toggle" : "Hold"));
	return buf;
}

static bool MenuStepSprintMode(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.sprint_toggle = !tomb5.sprint_toggle;
	// A latch must not outlive its own option: with both rows at Hold the mode
	// seam is not entered at all, so nothing in S_UpdateInput would ever clear
	// it again (specific\input.cpp).
	InputModeLatchReset();
	return 1;
}

static const char* MenuValDuckMode(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.duck_toggle ? "Toggle" : "Hold"));
	return buf;
}

static bool MenuStepDuckMode(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.duck_toggle = !tomb5.duck_toggle;
	InputModeLatchReset();		// the same reason as one row up
	return 1;
}

/* Grab mode: the fourth row of the same shape, on the mode sub-page
 * along with the three above it.
 *
 * 0 is Hold here too, and for the same reason as the two rows above: the
 * engine's own grab is held. What Toggle buys is the state doing the
 * holding instead of the thumb - see ModernGrabLatch (game/modern_move.cpp). */
static const char* MenuValGrabMode(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.grab_toggle ? "Toggle" : "Hold"));
	return buf;
}

static bool MenuStepGrabMode(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.grab_toggle = !tomb5.grab_toggle;
	// The same reason as the two rows above, and one of its own: a release
	// press left standing would take the next grab away as well.
	InputModeLatchReset();
	return 1;
}

/* Ledge hang: the fifth row on the mode sub-page, greyed under Tank
 * because the move leans on the Modern camera carry (MenuEnabledModern,
 * same predicate as the two Controls-page sliders). Flipping it only
 * stores the bit; it changes nothing else, so unlike Grab mode above it
 * needs no latch reset. */
static const char* MenuValLedgeHang(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.ledge_hang ? "on" : "off"));
	return buf;
}

static bool MenuStepLedgeHang(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.ledge_hang = !tomb5.ledge_hang;
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

	// S_UpdateInput builds the Action role's whole word from this bit
	// every tick, and the Secondary role only exists while it is set
	// (specific/input.cpp), so the row changes the keys' meaning at once
	// with nothing else to keep in step.
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.action_split = !tomb5.action_split;
	/* The pad has a set per setting: the flip seats the other one and
	 * parks this one, so neither is ever rewritten by the other - the
	 * player's own One-key layout is still there when he comes back. The
	 * keyboard needs no call of its own here: its slot 18 lives in both
	 * key sets already and is simply not offered under One key
	 * (MenuResolveKeybinds below). */
	ApplyJoySplit();
	/* The conflict table depends on this row too: KeySchemeConflicts
	 * lets the Secondary slot claim a key only while the role is live,
	 * so the row that makes it live - or inert - has to recompute the
	 * table, exactly as Reset and a capture do. Without this, the freed
	 * default key would stay silent until the next capture. */
	CheckKeyConflicts();
	return 1;
}

/* The row is called Control layout, and it says what it always meant:
 * which set of button names and defaults the bindings below it are
 * written in. Internally ControlMethod is still 0/1 (keyboard/joystick)
 * and the three controller positions are method 1 with JoyStyle 0/1/2,
 * so every "ControlMethod == 1" in the game keeps running unchanged.
 *
 * The fifth position is gone: "Reset" was a menu action wearing a
 * setting's clothes; it is the centred action on the Key bindings page
 * now, and with it go base's reset_row flag and the nMask pin that kept
 * the cursor on this row while it was showing. */
static const char* MenuValControlLayout(long, char* buf)
{
	if (!ControlMethod)
	{
		strcpy(buf, SCRIPT_TEXT(TXT_Keyboard));
		return buf;
	}

	// The joystick positions carry the chosen page's name. The page only
	// changes the display of the button names (and the defaults on reset) -
	// the bit order comes from the active input path, see input.cpp.
	switch (ActiveJoyStyle())
	{
	case JOYSTYLE_PS5:
		strcpy(buf, CustomText("PS5 Controller"));
		break;

	case JOYSTYLE_XBOX:
		strcpy(buf, CustomText("Xbox Controller"));
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

	// This row ignores the frame's own direction on purpose. On the
	// keyboard position the pad delivers no direction bits at all -
	// every use of the stick in input.cpp hangs on ControlMethod - so
	// whoever paged over here with the controller could not page back
	// with it. So the stick is read directly, on both positions: if only
	// on the keyboard one, a single deflection counts twice (JoyMenuStep
	// takes the step, ControlMethod flips to the pad, and next frame the
	// direction still being held reaches dbinput for the first time and
	// its edge detection calls that a fresh press - one push, two
	// pages). While the stick is deflected the step belongs to
	// JoyMenuStep alone; the arrow keys keep working through dbinput
	// whenever the stick rests.
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

/* Camera speed and Dead zone are the modern control's own feel, and
 * under Tank neither number is read by anything. A row that cannot do
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
	// The number only ever means anything through
	// game/modern_camera.cpp's ModernCameraSpeedRates - this row just
	// stores and clamps it.
	ulong old = tomb5.camera_speed;

	if (dir < 0 && tomb5.camera_speed > 0)
		tomb5.camera_speed--;

	if (dir > 0 && tomb5.camera_speed < MENU_SLIDER_TOP)
		tomb5.camera_speed++;

	return tomb5.camera_speed != old;
}

static long MenuNumDeadZone(long)
{
	return (long)tomb5.dead_zone;
}

static bool MenuStepDeadZone(long, long dir)
{
	// Stored and clamped here like the row above it; the eight widths it
	// scales, and why 50 is the tuned engine unchanged, live at
	// ModernDeadZoneUser (game/modern_move.cpp).
	ulong old = tomb5.dead_zone;

	if (dir < 0 && tomb5.dead_zone > 0)
		tomb5.dead_zone--;

	if (dir > 0 && tomb5.dead_zone < MENU_SLIDER_TOP)
		tomb5.dead_zone++;

	return tomb5.dead_zone != old;
}

/* Mouse sensitivity and Invert mouse: the mouse's two rows, right after Dead
 * zone and gated the same way, because the mouse turns only the Modern
 * camera. Both are read by S_UpdateInput on every tick (specific/input.cpp),
 * so a change takes hold on the next one. */
static long MenuNumMouseSensitivity(long)
{
	return (long)tomb5.mouse_sensitivity;
}

static bool MenuStepMouseSensitivity(long, long dir)
{
	// Stored and clamped here; the gain it scales, a quarter at 0 to four
	// times at 100, lives at ModernCameraMouseGain (game/modern_camera.cpp).
	ulong old = tomb5.mouse_sensitivity;

	tomb5.mouse_sensitivity = (ulong)MenuSliderStep(
		(int)tomb5.mouse_sensitivity, (int)dir, MENU_SLIDER_TOP);
	return tomb5.mouse_sensitivity != old;
}

static const char* MenuValInvertMouse(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.mouse_invert ? "on" : "off"));
	return buf;
}

static bool MenuStepInvertMouse(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.mouse_invert = !tomb5.mouse_invert;
	return 1;
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
	 * have no German entry - CustomText falls back to the English
	 * literal on its own when nothing matches. */
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

/* Gamma: the picture's one slider, under the two rows that pace it. Stored
 * and clamped here like the mouse's slider; the curve it picks and the pass
 * that draws it live at the one present (specific/dxshell.cpp, the gamma
 * pass), which reads tomb5.gamma on every frame - so the picture under the
 * menu follows the bar while it moves. 50 is the middle and no pass. */
static long MenuNumGamma(long)
{
	return (long)tomb5.gamma;
}

static bool MenuStepGamma(long, long dir)
{
	ulong old = tomb5.gamma;

	tomb5.gamma = (ulong)MenuSliderStep((int)tomb5.gamma, (int)dir,
		MENU_SLIDER_TOP);
	return tomb5.gamma != old;
}

/* -------------------------------------------------------------- Graphics -- */
/* Every row below is a case of base's Page0 or Page1
 * (tomb5/troyestuff.cpp), one function each instead of one shared
 * switch. Toggle rows return `!dir` unconditionally, exactly as the
 * Display page's VSync row does, because base toggled on either
 * direction (`dbinput & IN_LEFT || dbinput & IN_RIGHT`);
 * wrapping/clamping rows keep base's own per-direction shape, "changed"
 * included where base set it unconditionally inside a direction's block
 * (Distance fog). */

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

/* Shadow mode: base's four blobs, 1 Original, 2 Circle, 3 Faded circle and
 * 4 PSX, and a fifth state, 5 Projected - the HD Lara's projected shadow
 * (specific/hd/hd_shadow.h). The fifth state is offered only while HD is
 * built and drawn: HDEnabled(), the build gate with an installed Remaster
 * found, and HDDrawn(), the HD textures row (specific/hd/hd_switch.h). It asks
 * no level, so it answers the same in the title screen and in a running
 * level. Without HD the engine draws a stored 5 as the faded circle
 * (HDShadowBlobMode), so then the row is the four-state row and a stored 5
 * is shown and stepped as what is drawn: right gives PSX, left Circle. Only
 * a step writes the option, so a 5 survives a visit to the page with HD off.
 * Every other stored value steps as base's row: past the top to 1, below 1
 * to the top. The draw decides once a frame whether the projected shadow
 * runs (specific/hd/hd_laradraw.cpp, HDShadowFrameDecide), so a change here
 * shows on the next drawn frame of the running level; a frame without it
 * forgets the following, and the first frame with it places the shadow
 * where the light throws it at once (hd_shadow.h, HDShadowFollowStep). */
static ulong MenuShadowModeShown(bool hd)
{
	return hd ? tomb5.shadow_mode : (ulong)HDShadowBlobMode((long)tomb5.shadow_mode);
}

static const char* MenuValShadowMode(long, char* buf)
{
	const ulong shown = MenuShadowModeShown(HDEnabled() && HDDrawn());

	strcpy(buf, CustomText(shown == 1 ? "Original" : shown == 2 ? "Circle" :
	           shown == 3 ? "Faded circle" : shown == 5 ? "Projected" : "PSX"));
	return buf;
}

static bool MenuStepShadowMode(long, long dir)
{
	const bool hd = HDEnabled() && HDDrawn();
	const ulong shown = MenuShadowModeShown(hd);
	const ulong top = hd ? HD_SHADOW_MODE_PROJECTED : HD_SHADOW_MODE_PSX;

	if (dir > 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.shadow_mode = shown >= top ? 1 : shown + 1;
		return 1;
	}

	if (dir < 0)
	{
		SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
		tomb5.shadow_mode = shown <= 1 ? top : shown - 1;
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

/* HD build, the build gate's setting (the registry's hd_build). On, the
 * default, the HD art of an installed Remaster is built at every level
 * load, and the HD textures row and the HD/SD key switch between two
 * resident draws at once. Off, none of it is built: a load is as short as
 * without the Remaster, the draw is classic and the key has nothing to
 * switch (it says so on screen, specific/hd/hd_switch.cpp). The row shows
 * and steps tomb5_hd_build_next, which the gate takes at the next level
 * load (specific/file.cpp, S_LoadLevelFile) - the row shows what the next
 * level will do, so a step in a running level changes nothing there. A
 * stored value other than 0 shows as on and is kept as stored until a step
 * writes 0 or 1. */
static const char* MenuValHdBuild(long, char* buf)
{
	strcpy(buf, CustomText(tomb5_hd_build_next ? "on" : "off"));
	return buf;
}

static bool MenuStepHdBuild(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5_hd_build_next = tomb5_hd_build_next ? 0 : 1;
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

	// The draw of the world and the interface, HD or classic, from the
	// Remaster's art in the player's own installation. The row's value is
	// the HD/SD switch's state (specific/hd/hd_switch.h): a step flips it
	// through the switch's own setter, as a press of the key does, so the
	// level on screen follows on the next drawn frame wherever its HD data
	// are built (they are built at the load whatever the row says,
	// hd_build), and the row shows every flip of the key. MenuAfterChange
	// then saves the value with the tomb5 settings.
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	HDSwitchOption();
	return 1;
}

/* Body physics, the one row of this page that base's Page0 and Page1 do not
 * have: the chest's secondary motion on the HD Lara, tomb5.hd_softbody -
 * 0 off, 1 subtle, 2 normal, 3 strong (the strengths are
 * specific/hd/hd_softbody.h's). It stands under HD textures because it
 * moves only an HD Lara, and like that row it is offered whatever the level
 * on screen draws: with HD off, on a young-Lara level or for an outfit
 * whose region failed its checks the draw leaves the motion off by itself.
 * The draw compares the option with the strength it used last on every
 * frame and starts again from rest when they differ
 * (specific/hd/hd_laradraw.cpp, soft_constants), so a change here shows on
 * the next drawn frame of the running level. */
static const char* MenuValBodyPhysics(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.hd_softbody == 1 ? "Subtle" :
	           tomb5.hd_softbody == 2 ? "Normal" :
	           tomb5.hd_softbody == 3 ? "Strong" : "off"));
	return buf;
}

static bool MenuStepBodyPhysics(long, long dir)
{
	// Four states and the ends hold, as Camera damping; through
	// MenuSliderStep so that MenuTest can sweep the rule. A stored value out
	// of range counts as off, as the registry read takes it (tomb5/tomb5.cpp).
	ulong old = tomb5.hd_softbody > 3 ? 0 : tomb5.hd_softbody;
	ulong now = (ulong)MenuSliderStep((int)old, (int)dir, 3);

	if (now == old)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	tomb5.hd_softbody = now;
	return 1;
}

/* Reflections: the polished floors' mirror (specific/hd/hd_render.cpp, "the
 * marble floors"), the registry's hd_marble, which the game's start hands
 * to the renderer (tomb5/tomb5.cpp). The row shows the key - absent reads
 * as on, as the start reads it - and a press either way writes the other
 * value and hands it to the renderer at once (HDRenderMarbleSet, planes 0
 * keeping the frame's ceiling): the mirror's draw follows on the next
 * drawn frame, and off costs nothing from there; the floors' build follows
 * at the level's next load. Offered whatever the level on screen draws. */
static bool MenuReflectOn()
{
	ulong on = 1;

	REG_PeekLong("tomb5", "hd_marble", on);
	return on != 0;
}

static const char* MenuValReflect(long, char* buf)
{
	strcpy(buf, CustomText(MenuReflectOn() ? "on" : "off"));
	return buf;
}

static bool MenuStepReflect(long, long dir)
{
	char key[40];
	const bool on = !MenuReflectOn();

	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	sprintf(key, "hd_marble");
	OpenRegistry("tomb5");
	REG_WriteLong(key, on ? 1 : 0);
	CloseRegistry();
	HDRenderMarbleSet(on ? 1 : 0, 0);
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
	// Page1 case 1<<7) - the save it triggers writes the same value
	// again, harmless, and this quirk is left exactly where base put it.
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
/* All six rows of base's Page2 (troyestuff.cpp), offered on their own
 * tab now and no longer gated by "First person view". "First person
 * view" itself moves in from Page1 as a normal toggle row, first on
 * the tab. */

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
	// "still" holds her body on one frame while she is standing,
	// crouching or kneeling still - the head still turns with the look,
	// the arms still aim and fire, and every step, jump, climb and pull
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
/* Base's Page0 rows that are about the heads-up display and the load
 * screens, grouped on their own tab so Graphics keeps its headroom. */

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

/* Three states in the row's order: the TR5 bar, the TR4 bar, and off - no
 * loading screen for a short load, the level follows the screen before it
 * through black, while a load that builds the HD data shows its picture
 * and the TR5 bar (file.cpp, S_LoadLevelFile). "TR4" and "TR5" stay as
 * they are in every language; "off" is the menu's own entry. tr4_loadbar
 * follows the state: the TR4 bar's look asks it. */
static const char* MenuValLoadbarStyle(long, char* buf)
{
	strcpy(buf, CustomText(tomb5.loadbar == LOADBAR_OFF ? "off" :
		tomb5.loadbar == LOADBAR_TR4 ? "TR4" : "TR5"));
	return buf;
}

static bool MenuStepLoadbarStyle(long, long dir)
{
	if (!dir)
		return 0;

	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);

	if (dir > 0)
		tomb5.loadbar = tomb5.loadbar >= LOADBAR_OFF ? LOADBAR_TR5 :
			tomb5.loadbar + 1;
	else
		tomb5.loadbar = tomb5.loadbar <= LOADBAR_TR5 ? LOADBAR_OFF :
			tomb5.loadbar - 1;

	tomb5.tr4_loadbar = tomb5.loadbar == LOADBAR_TR4;
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
	/* The conflict table depends on this row too: KeySchemeConflicts
	 * lets the five cheat slots claim a key only while they are live, so
	 * the row that makes them live - or inert - has to recompute the
	 * table, exactly as Reset, a capture and the Action row do. Without
	 * this, the freed default key would stay silent until the next
	 * capture. */
	CheckKeyConflicts();
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
	// unchanged.
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

/* Waiting for the player to press the thing he wants bound. One role
 * at a time, and the role is the row's own param - base had to shift
 * the selection bit back down into an array index at two places and
 * recount the shift every time a row moved (specific/LoadSave.cpp at
 * base). */
static bool menu_waiting_for_key;
static long menu_wait_role;
// The action button still pinned the row selection when the wait was entered.
static bool menu_action_still_held;
// The mouse buttons already down when the wait was entered, each until it is
// let go: only a press made during the wait is taken.
static long menu_mouse_held;

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

	/* A mouse button is a code above every scan code (game/key_scheme.h), so
	 * it is named before the key-name table is indexed - the table's entries
	 * past 0xFF are Core's old joystick names and would call Mouse R "Joy 2".
	 * The same German/everything-else choice as the keys'. */
	if (!ControlMethod && KeySchemeIsMouse(layout[1][role]))
		return KeySchemeMouseName(layout[1][role], Gameflow->Language == 2);

	if (!ControlMethod)
		return MenuKeyboardNames()[layout[1][role]];

	if (jLayout[role] == -1)
		return "---";

	/* A chord prints as a pair: the Modifier, a plus, the button.
	 *
	 * The Modifier's half is the word "Mod" rather than its button's
	 * name. Measured on the real font (game/chardef.inc, the widths
	 * GetStringLength adds up): the value column is left-aligned at
	 * phd_centerx + (phd_centerx >> 2), which leaves 240 px to the
	 * right edge at 640x480, and "Touchpad+Stick R Down" - the longest
	 * pair a player can really make on the PS5 DirectInput page, where
	 * the Modifier's own default lives - is 273 px. It would run off
	 * the screen. "Mod+Stick R Down" is 215 px, twenty-five clear, and
	 * the scale is phd_winxmax / 512 on both sides at every other
	 * resolution (657 of 720 at 1920x1080). The Modifier's own row
	 * still shows the button's real name, so the player is never left
	 * guessing which button "Mod" is.
	 *
	 * An unbound Modifier with a chord stored reads "---+R3": the same
	 * "---" that means unbound one line above, in the half that is
	 * unbound. It is honest - that binding cannot fire - and it fits
	 * (198 px at its widest, "---+Stick R Down").
	 *
	 * The button's name goes through CustomText (game\gameflow.cpp): the
	 * shapes, the stick's and the d-pad's directions and the words among
	 * the names have their German; a letter, a number or a name printed on
	 * the pad falls back to itself. In German "Mod+Stick R Runter" is the
	 * widest pair and stays inside the column at every screen width. */
	if (JoyRoleIsChord(role))
	{
		sprintf(buf, "%s+%s", jLayout[JOY_ROLE_MODIFIER] == -1 ? "---" : "Mod",
			CustomText(JoyButtonName(jLayout[role])));
		return buf;
	}

	return CustomText(JoyButtonName(jLayout[role]));
}

static void MenuSelectBinding(long role)
{
	SoundEffect(SFX_MENU_SELECT, 0, SFX_ALWAYS);
	menu_wait_role = role;
	menu_waiting_for_key = 1;
	// The hold guard below (do not immediately capture the action button
	// as the new binding) only applies when the row was actually
	// selected with the pad button. If the selection came from the
	// keyboard (Enter), the guard would permanently block any attempt
	// to bind the action button itself.
	menu_action_still_held = ControlMethod == 1 && Key(jLayout[4] + 256);
	memset(keymap, 0, sizeof(keymap));
	menu_mouse_held = InputMouseButtons();
}

/* "Reset to defaults", the centred action above the list. It is
 * exactly what the Method row's fifth position did: the compiled-in
 * keyboard table and the pad defaults matching the chosen page and the
 * active input path - and the keyboard table it restores is the active
 * scheme's, which at Tank is the row it always was. Reset means "the
 * defaults of the scheme I am playing in", not "Core's defaults
 * whatever I am playing in". */
static void MenuResetBindings(long)
{
	SoundEffect(SFX_MENU_SELECT, 0, 2);
	// The copy base got wrong: "memcpy(layout[1], layout, 72)" copied 72
	// bytes into a destination that starts 36 bytes into a 72-byte
	// object: `short layout[2][18]` is 36 shorts altogether, so the
	// write spanned bytes [36,108) of it and the last 36 landed past
	// the end entirely - `long conflict[18]` is the next global
	// declared in the same file (specific/input.cpp), which makes it
	// the plausible target. Source and destination overlapped as well
	// ([0,72) against [36,108)), which is undefined for memcpy on its
	// own. The intent was always "put the compiled-in defaults over the
	// active bindings", and that is one row of the array: 36 bytes,
	// from layout[0] at Tank and from the Modern default row under
	// Modern (KeyDefaultRow). sizeof(layout[0]) is one row whatever the
	// array grows to, which is why it is written that way rather than
	// as 36.
	memmove(layout[1], KeyDefaultRow(), sizeof(layout[0]));
	ApplyJoyDefaults();	// defaults matching page AND input path (input.cpp)
	/* The conflict table is recomputed as well. Reset changes every
	 * active binding, and conflict[] is derived from them: which roles
	 * may still answer to their default key. Without this it kept the
	 * answer computed for the bindings the player just discarded, until
	 * the next capture happened to recompute it - so a role whose
	 * default key had been claimed stayed silent after a Reset that had
	 * freed it again. */
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
#define MENU_VALUE_IF(lbl, val, stp, sv, pred)	{ MROW_VALUE, lbl, -1, val, stp, sv, pred, 0, 0, 0, 0 }
#define MENU_SLIDER(lbl, txt, num, stp, sv, pred) { MROW_SLIDER, lbl, txt, 0, stp, sv, pred, 0, 0, num, 0 }
#define MENU_BIND(lbl, txt, role)		{ MROW_BINDING, lbl, txt, MenuValBinding, 0, MENU_SAVE_GAME, 0, 0, MenuSelectBinding, 0, role }

// Special Features is a title-screen thing: gfGameMode is 1 only there, and
// the pause menu cannot reach it at all (game\control.cpp gates the pause key
// on !gfGameMode). Offered in colour 3 in a pause session rather than silently
// missing.
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

/* The three rows that were on the Controls page moved here as the one
 * table line each they are - same callbacks, same save class, same
 * labels, so the MENULOG line each of them writes is word for word the
 * one it wrote before except for `page=Modes` in place of
 * `page=Controls`. Grab mode is the fourth and new, Ledge hang the
 * fifth and new; Ledge hang is greyed under Tank, like the Controls
 * page's own camera rows, because the move it switches on leans on the
 * Modern camera carry. Five rows: one page, no arrows (MENU_MAX_ROWS
 * is 12). */
static const menu_row menu_rows_modes[] =
{
	MENU_VALUE("Combat mode", MenuValCombatMode, MenuStepCombatMode, MENU_SAVE_TOMB5),
	MENU_VALUE("Sprint mode", MenuValSprintMode, MenuStepSprintMode, MENU_SAVE_TOMB5),
	MENU_VALUE("Duck mode", MenuValDuckMode, MenuStepDuckMode, MENU_SAVE_TOMB5),
	MENU_VALUE("Grab mode", MenuValGrabMode, MenuStepGrabMode, MENU_SAVE_TOMB5),
	MENU_VALUE_IF("Ledge hang", MenuValLedgeHang, MenuStepLedgeHang, MENU_SAVE_TOMB5, MenuEnabledModern)
};

static const menu_row menu_rows_controls[] =
{
	MENU_TAB("Key bindings", MENU_PAGE_KEYBINDS),
	MENU_TAB("View", MENU_PAGE_VIEW),
	// The third tab, directly after "View", where the five mode rows now
	// live.
	MENU_TAB("Modes", MENU_PAGE_MODES),
	MENU_VALUE("Control type", MenuValControlType, MenuStepControlType, MENU_SAVE_TOMB5),
	MENU_VALUE("Action", MenuValAction, MenuStepAction, MENU_SAVE_TOMB5),
	MENU_VALUE("Control layout", MenuValControlLayout, MenuStepControlLayout, MENU_SAVE_GAME),
	MENU_SLIDER("Camera speed", -1, MenuNumCameraSpeed, MenuStepCameraSpeed, MENU_SAVE_TOMB5, MenuEnabledModern),
	// The same slider, the same predicate, right after Camera speed.
	MENU_SLIDER("Dead zone", -1, MenuNumDeadZone, MenuStepDeadZone, MENU_SAVE_TOMB5, MenuEnabledModern),
	// The mouse's two rows, the same predicate again. Twelve rows now, the
	// page's MENU_MAX_ROWS, so it is still one page.
	MENU_SLIDER("Mouse sensitivity", -1, MenuNumMouseSensitivity, MenuStepMouseSensitivity, MENU_SAVE_TOMB5, MenuEnabledModern),
	MENU_VALUE_IF("Invert mouse", MenuValInvertMouse, MenuStepInvertMouse, MENU_SAVE_TOMB5, MenuEnabledModern),
	MENU_VALUE_TXT("Targeting", TXT_Targeting, MenuValTargeting, MenuStepTargeting, MENU_SAVE_GAME),
	MENU_VALUE("Ammotype hotkeys", MenuValAmmotypeHotkeys, MenuStepAmmotypeHotkeys, MENU_SAVE_TOMB5)
};

// All six always offered, not gated by tomb5.first_person.
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
	// The build gate, above the draw it feeds.
	MENU_VALUE("HD build", MenuValHdBuild, MenuStepHdBuild, MENU_SAVE_TOMB5),
	MENU_VALUE("HD textures", MenuValHdTextures, MenuStepHdTextures, MENU_SAVE_TOMB5),
	// Directly under the HD switch it depends on.
	MENU_VALUE("Body physics", MenuValBodyPhysics, MenuStepBodyPhysics, MENU_SAVE_TOMB5),
	// The polished floors' mirror, with the HD rows; it writes its own key.
	// Fifteen rows: the page pages eight and seven, with the arrow row
	// under them.
	MENU_VALUE("Reflections", MenuValReflect, MenuStepReflect, MENU_SAVE_NONE),
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
	MENU_VALUE("VSync", MenuValVSync, MenuStepVSync, MENU_SAVE_TOMB5),
	// A slider as the volume rows are, the same keys and pad; "Gamma" is
	// the word in every language the table has, so it needs no string row.
	MENU_SLIDER("Gamma", -1, MenuNumGamma, MenuStepGamma, MENU_SAVE_TOMB5, 0)
};

static const menu_row menu_rows_hud[] =
{
	MENU_VALUE("Bar mode", MenuValBarMode, MenuStepBarMode, MENU_SAVE_TOMB5),
	MENU_VALUE("Bar positions", MenuValBarPositions, MenuStepBarPositions, MENU_SAVE_TOMB5),
	MENU_VALUE("Enemy bars", MenuValEnemyBars, MenuStepEnemyBars, MENU_SAVE_TOMB5),
	MENU_VALUE("Ammo counter", MenuValAmmoCounter, MenuStepAmmoCounter, MENU_SAVE_TOMB5),
	MENU_VALUE("Gameover menu", MenuValGameoverMenu, MenuStepGameoverMenu, MENU_SAVE_TOMB5),
	MENU_VALUE("Loadbar style", MenuValLoadbarStyle, MenuStepLoadbarStyle, MENU_SAVE_TOMB5),
	MENU_VALUE("Loading text", MenuValLoadingText, MenuStepLoadingText, MENU_SAVE_TOMB5)
};

static const menu_row menu_rows_tomb5[] =
{
	MENU_VALUE("Cutscene skipper", MenuValCutsceneSkipper, MenuStepCutsceneSkipper, MENU_SAVE_TOMB5),
	MENU_VALUE("Cheats", MenuValCheats, MenuStepCheats, MENU_SAVE_TOMB5),
	// One literal for the two places this option is named: the title
	// screen's own row has said "Level Select", and a second spelling
	// would have been a second CustomText key for one setting.
	MENU_VALUE("Level Select", MenuValLevelSelect, MenuStepLevelSelect, MENU_SAVE_TOMB5),
	MENU_VALUE("Fix climb up delay", MenuValFixClimbUpDelay, MenuStepFixClimbUpDelay, MENU_SAVE_TOMB5),
	MENU_VALUE("Flexible crawling", MenuValFlexibleCrawling, MenuStepFlexibleCrawling, MENU_SAVE_TOMB5),
	MENU_VALUE("Crawl Tilting", MenuValCrawlTilting, MenuStepCrawlTilting, MENU_SAVE_TOMB5)
};

/* The two bindable lists. Row 0 of each is the centred reset action,
 * and it is the page's one fixed row - it is repeated on every page of
 * the split list, above it.
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
	// The Secondary row is still offered only under "Separate", but it
	// is no longer the last row of the list, so it can no longer be
	// dropped by counting one row fewer - MenuResolveKeybinds builds
	// the list instead. The pad list below still ends with it and still
	// uses the count.
	MENU_BIND("Secondary", -1, KEY_ROLE_SECOND),
	/* The hotkeys are one list with the rest, rather than a separate
	 * one, so every binding lives on the same Keybinds pages. They were
	 * hard-wired scan codes in specific/input.cpp; the rows below are
	 * the same keys, now bindable.
	 *
	 * The four weapon names the string wad has are taken from it rather
	 * than invented (the inventory calls them by these ids); the HK's
	 * name is the id its own inventory entry carries. The crossbow has
	 * no weapon-name string of its own - TXT_GRAP2 sits on the ammo
	 * rows - so it goes through CustomText like the cheat rows. */
	MENU_BIND("Small Medipack", TXT_Small_Medipack, KEY_ROLE_MEDI_SMALL),
	MENU_BIND("Large Medipack", TXT_Large_Medipack, KEY_ROLE_MEDI_LARGE),
	MENU_BIND("Save", TXT_Save, KEY_ROLE_SAVE),
	MENU_BIND("Load", TXT_Load, KEY_ROLE_LOAD),
	MENU_BIND("Pistols", TXT_Pistols, KEY_ROLE_WEAPON_FIRST + 0),
	MENU_BIND("Shotgun", TXT_Shotgun, KEY_ROLE_WEAPON_FIRST + 1),
	MENU_BIND("Uzi", TXT_Uzi, KEY_ROLE_WEAPON_FIRST + 2),
	MENU_BIND("Revolver", TXT_Revolver, KEY_ROLE_WEAPON_FIRST + 3),
	MENU_BIND("HK", TXT_HKSTRING0, KEY_ROLE_WEAPON_FIRST + 4),
	MENU_BIND("Crossbow", -1, KEY_ROLE_WEAPON_FIRST + 5),
	/* The five cheat roles are shown only while Cheats are on. A hidden
	 * role is inert - not even asked - exactly like Secondary under
	 * "One key", so a player with Cheats off sees the page he saw
	 * before. Their consumer is LaraCheatHotkeys (game/laramisc.cpp). */
	MENU_BIND("Fly mode", -1, KEY_ROLE_CHEAT_FLY),
	MENU_BIND("All weapons", -1, KEY_ROLE_CHEAT_GUNS),
	MENU_BIND("All items", -1, KEY_ROLE_CHEAT_ITEMS),
	MENU_BIND("Heal", -1, KEY_ROLE_CHEAT_HEAL),
	MENU_BIND("Skip level", -1, KEY_ROLE_CHEAT_SKIP),
	/* The HD/SD switch, shown whatever Cheats says: it is a comparison
	 * of the two draws, not a cheat. Its consumer is HDSwitchInput
	 * (specific/hd/hd_switch.cpp). The label is the same in every
	 * language, so CustomText hands it back unchanged. */
	MENU_BIND("HD / SD", -1, KEY_ROLE_HD_SWITCH)
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
	MENU_BIND("Secondary", -1, JOY_ROLE_SECOND),

	/* These thirteen rows are in the order of the four Separate default
	 * tables. The Modifier comes first because it is what the twelve
	 * below can be chorded against: with it unbound, every row under it
	 * can still be bound plainly, and no chord exists anywhere.
	 *
	 * The labels are the keyboard list's own - the same CustomText and
	 * the same script ids - because they name the same twelve things.
	 * Only "Modifier" is new, and only the pad has it. */
	MENU_BIND("Modifier", -1, JOY_ROLE_MODIFIER),
	MENU_BIND("Pistols", TXT_Pistols, JOY_ROLE_WEAPON_FIRST + 0),
	MENU_BIND("Shotgun", TXT_Shotgun, JOY_ROLE_WEAPON_FIRST + 1),
	MENU_BIND("Uzi", TXT_Uzi, JOY_ROLE_WEAPON_FIRST + 2),
	MENU_BIND("Revolver", TXT_Revolver, JOY_ROLE_WEAPON_FIRST + 3),
	MENU_BIND("HK", TXT_HKSTRING0, JOY_ROLE_WEAPON_FIRST + 4),
	MENU_BIND("Crossbow", -1, JOY_ROLE_WEAPON_FIRST + 5),
	MENU_BIND("Fly mode", -1, JOY_ROLE_CHEAT_FLY),
	MENU_BIND("All weapons", -1, JOY_ROLE_CHEAT_GUNS),
	MENU_BIND("All items", -1, JOY_ROLE_CHEAT_ITEMS),
	MENU_BIND("Heal", -1, JOY_ROLE_CHEAT_HEAL),
	MENU_BIND("Skip level", -1, JOY_ROLE_CHEAT_SKIP),
	MENU_BIND("HD / SD", -1, JOY_ROLE_HD_SWITCH)
};

/* The keyboard's list is built at runtime rather than kept static. Two
 * roles are hidden rather than shown grey: Secondary unless "Action"
 * says Separate (with "One key" the role produces nothing at all, so a
 * row that let the player bind it would be a row that does nothing),
 * and the five cheat roles unless Cheats are on. A hidden row is
 * inert: the drive loop can only reach rows this function hands out,
 * so a hidden role is never even asked to be bound, which is exactly
 * what "the page looks as it did before" means for a player with both
 * settings off.
 *
 * Dropping the last row by counting one fewer - which is all this
 * function did while Secondary was last - no longer works, because the
 * hotkeys sit behind it. The pad's list is built the same way, and for
 * the same reason: Secondary is no longer last there either. The
 * Modifier, the six weapons and the HD/SD switch are always offered -
 * the Modifier is how a chord is made at all, and a role the player has
 * not bound simply says "---".
 *
 * One buffer serves both lists, sized by the longer of the two so that
 * neither can outgrow it whichever way the tables move. */
#define MENU_KEYBINDS_KBD_ROWS	(sizeof(menu_rows_keybinds_kbd) / sizeof(menu_rows_keybinds_kbd[0]))
#define MENU_KEYBINDS_JOY_ROWS	(sizeof(menu_rows_keybinds_joy) / sizeof(menu_rows_keybinds_joy[0]))

static menu_row menu_keybinds_built[MENU_KEYBINDS_KBD_ROWS > MENU_KEYBINDS_JOY_ROWS
	? MENU_KEYBINDS_KBD_ROWS : MENU_KEYBINDS_JOY_ROWS];

static void MenuResolveKeybinds(const menu_row** rows, long* num)
{
	const menu_row* src;
	long i, out, total, second, cheat, cheat_last;

	if (ControlMethod == 1)
	{
		src = menu_rows_keybinds_joy;
		total = MENU_KEYBINDS_JOY_ROWS;
		second = JOY_ROLE_SECOND;
		cheat = JOY_ROLE_CHEAT_FLY;
		cheat_last = JOY_ROLE_CHEAT_SKIP;
	}
	else
	{
		src = menu_rows_keybinds_kbd;
		total = MENU_KEYBINDS_KBD_ROWS;
		second = KEY_ROLE_SECOND;
		cheat = KEY_ROLE_CHEAT_FLY;
		cheat_last = KEY_ROLE_CHEAT_SKIP;
	}

	out = 0;

	for (i = 0; i < total; i++)
	{
		const menu_row* row = &src[i];

		if (row->kind == MROW_BINDING)
		{
			if (row->param == second && !tomb5.action_split)
				continue;

			// Named at both ends: the HD/SD row behind the five is always
			// shown.
			if (row->param >= cheat && row->param <= cheat_last &&
				!tomb5.enable_cheats)
				continue;
		}

		menu_keybinds_built[out++] = *row;
	}

	*rows = menu_keybinds_built;
	*num = out;
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
	{ "Credits",      -1,          0, 0,                          0, 0 },	// the existing screen
	{ "Modes",        -1,          MENU_ROWS(menu_rows_modes),    0, 0 }
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

/* The "Game" registry block is written when it changes. Without it,
 * every key rebind, volume drag, sound quality change, targeting
 * toggle, language switch and control layout change would live in memory
 * until a clean exit - a crash would throw the lot away while every tomb5
 * toggle is already on disk. The flag is set by the rows whose save
 * is MENU_SAVE_GAME and cleared by the one call, when the page carrying
 * them is left. Never per frame: SaveSettings() opens the key and
 * writes about forty values. */
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

/* MENULOG slider: the way a selected slider row is held, and by which hand.
 * One line when the way or its hands change while a slider row is selected -
 * a hold arriving, a release, the other hand taking over - and none while
 * they stay, so a held slider writes one line and not one per present; a
 * slider row selected with nothing held writes nothing. `src` names the hands
 * (MenuSliderHands, game/menu_nav.h): key, pad, pad+key, or - for none.
 * `shadow` is 1 when the title handed this present a zeroed input and dbinput
 * (TitleInputShadowed, game/gameflow.cpp), which the slider does not read. */
static void MenuLogSlider(long page, const menu_row* row, long dir)
{
	static const char* const src_name[4] = { "-", "key", "pad", "pad+key" };
	static const menu_row* logged_row;
	static long logged_dir;
	static long logged_hands;
	const long bit = dir < 0 ? IN_LEFT : IN_RIGHT;
	long hands;
	bool fresh;

	if (!gLogEnabled)
		return;

	hands = MenuSliderHands(dir, (inputBusy & bit) != 0,
		keymap[dir < 0 ? DIK_LEFT : DIK_RIGHT] != 0,
		(InputPadDirection() & bit) != 0);

	if (row == logged_row && dir == logged_dir && hands == logged_hands)
		return;

	fresh = row != logged_row;
	logged_row = row;
	logged_dir = dir;
	logged_hands = hands;

	if (fresh && !dir)
		return;

	Log("MENULOG slider page=%s row=%s dir=%ld src=%s shadow=%d", menu_pages[page].name, row->label,
		dir, src_name[hands & 3], TitleInputShadowed() ? 1 : 0);
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
	// (specific/registry.cpp). Both are live values at any moment, so
	// writing them mid-session writes exactly what a clean exit would
	// have written then.
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
// function shared by both callers, so without this the menu would resume
// wherever the last session left it.
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
 * held down, not tapped once. The rule, LEFT winning, is MenuSliderHeldDir
 * (game/menu_nav.h).
 *
 * The word is inputBusy, the one S_UpdateInput builds for every caller, and
 * not `input`. On the title screen ControlPhase masks `input` down to IN_LOOK
 * after every read while the title's flyby holds bDisableLaraControl, zeroes
 * it while one of the title's cutscenes runs, and keeps dbinput for the menu
 * (game/control.cpp); so there the stick, the d-pad and the direction keys
 * never reached a slider, at any frame rate, and the arrow keys did only
 * through keymap. The pause menu calls
 * S_UpdateInput itself, and its `input` carries the same two bits as
 * inputBusy, so in a level nothing changes. The title's input shadow
 * (game/gameflow.cpp) zeroes input and dbinput on a present without a tick
 * and leaves inputBusy standing, as it leaves keymap: a held slider moves on
 * every present by either road, and a held slider is not an edge. */
static long MenuSliderDir()
{
	return MenuSliderHeldDir((inputBusy & IN_LEFT) != 0,
		(inputBusy & IN_RIGHT) != 0, keymap[DIK_LEFT] != 0,
		keymap[DIK_RIGHT] != 0);
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
 * frame's own path: the seam names it and the Game block is marked. Setting
 * menu_game_dirty by hand would save correctly but leave MENULOG silent on
 * the one page whose whole purpose is changing values. The role's row is
 * looked up rather than passed, because the capture runs from the drive
 * loop's head, before the row under the cursor is fetched. */
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
 * (specific/LoadSave.cpp) with one change: the role is menu_wait_role,
 * so the two shift-then-count blocks that turned the selection bit
 * back into an array index are gone. */
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

		/* A mouse button, taken on its press and never on its release: a
		 * button held when the wait began counts only once it has been let go
		 * and pressed again, and the wait ends with the first capture, so one
		 * press binds one role. A key pressed in the same frame has already
		 * ended the wait above and wins. The lowest button wins a tie.
		 *
		 * The mouse does not navigate the menu, so no button needs to be
		 * refused the way Return and the arrows are. */
		const long buttons = InputMouseButtons();
		const long fresh = buttons & ~menu_mouse_held;

		menu_mouse_held &= buttons;

		if (menu_waiting_for_key && fresh)
		{
			for (lp = 0; lp < KEY_MOUSE_BUTTONS; lp++)
				if (fresh & (1L << lp))
					break;

			menu_waiting_for_key = 0;
			layout[1][menu_wait_role] = (short)(KEY_MOUSE_FIRST + lp);
			MenuBindingChanged();
		}
	}

	if (ControlMethod == 1)
	{
		jread = ReadJoystick(jx, jy);

		// Drop the hold guard as soon as the button is actually up, and check
		// that on every frame of the wait - not inside "if (jread)"
		// below: jread is zero exactly when the player lets go, so the
		// release would never be seen and the Action button could not be bound
		// to itself at all.
		if (!Key(jLayout[4] + 256))
			menu_action_still_held = 0;

		/* The Modifier makes the next press a chord. The raw reading is
		 * what this capture has always used, and it is still raw here -
		 * PadRoleFires is the rule for play, and a capture that went
		 * through it could never record a chord at all (a plain read is
		 * suppressed while the Modifier is held, which is the point).
		 *
		 * The Modifier's own bit is taken out of the reading first, or
		 * the highest-bit search below would find it instead of the
		 * button the player pressed with it - bit 13, the DualSense's
		 * touchpad click, is above every face button on that page. With
		 * the bit gone, `jread` is zero until a second button really
		 * arrives, and the `if` below simply waits, which is exactly
		 * "the Modifier alone is not a binding".
		 *
		 * Except on the Modifier's own row, where it is the one thing
		 * that can be bound and must therefore stay in the reading. */
		const long mbit = jLayout[JOY_ROLE_MODIFIER];
		const long chord = (menu_wait_role != JOY_ROLE_MODIFIER && PadBitDown((unsigned long)jread, mbit)) ? 1 : 0;

		if (chord)
			jread &= ~(1L << mbit);

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
			// the player just pressed to select this row, and is very likely
			// still physically held on this very next frame (this capture
			// reads the raw, non-debounced joystick state). The keyboard loop
			// above excludes DIK_RETURN for exactly the same reason.
			if (lp2 != jLayout[4] || !menu_action_still_held)
			{
				jLayout[menu_wait_role] = lp2;
				JoyRoleSetChord(menu_wait_role, chord);

				/* The conflict is the pair and not the button. R3 and
				 * Modifier+R3 are two bindings and coexist - that is what
				 * a chord is for - so only a role on the same button
				 * and the same side of the Modifier is the one that has
				 * to give way. Without the second term, a default row
				 * would unbind half of itself the first time anything
				 * was rebound onto it.
				 *
				 * Binding the Modifier onto a used button clears that
				 * button whole. Its own chord flag is 0, so the pair
				 * test above already takes every plain role sitting
				 * there; the second term takes the chord ones too,
				 * which the pair test would have left. They are just as
				 * broken: a chord on the Modifier's own button would
				 * fire when the Modifier alone is pressed, and
				 * PadRoleFires refuses it anyway - this is the same
				 * answer said where the player can see it happen. */
				for (i = 0; i < JOY_ROLES; i++)
					if (i != menu_wait_role && jLayout[i] == lp2 &&
						(JoyRoleIsChord(i) == chord || menu_wait_role == JOY_ROLE_MODIFIER))
					{
						jLayout[i] = -1;
						JoyRoleSetChord(i, 0);
					}

				menu_waiting_for_key = 0;
				MenuBindingChanged();
			}
		}
	}

	CheckKeyConflicts();
	dbinput = 0;
}

/* The two screens that still keep their own code: Special Features and
 * the Credits (troyestuff.cpp). "tomb5 options" became a table like any
 * other page. They are pages of the tree like any other; only what
 * happens inside them is still theirs. */
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

/* Where a slider row's number starts: right of the bar's white frame, one
 * gap of GetFixedScale(4) after it, left-aligned as a value row's text is.
 * The bar is DoSlider's own (specific/drawbars.cpp): x 400 of 640, scaled
 * by phd_winxmax / 640, GetFixedScale(200) wide, and its frame one pixel
 * wider on each side - the same arithmetic here, so the number follows the
 * bar at every resolution.
 *
 * The gap is measured at the tightest layout. Up to 4:3 the bar's width is
 * scaled by the width, so the space right of it is a sixteenth of the
 * screen. At 640x480 the frame's right edge lies at 600.375; Direct3D 9
 * puts pixel centres on whole numbers and DrawColoredRect adds no offset,
 * so pixel 600 is the frame's last and 39 pixels stay free. "100", the
 * widest number a 0..100 slider shows, is 31 there (game/chardef.inc's
 * digits, scaled by phd_winxmax / 512 as game/text.cpp does), and the 8
 * left over go 3 to the bar's side and 5 to the edge's. Wider screens
 * scale the bar by the height and only gain room on the right. */
static long MenuSliderNumberX()
{
	return long(400.0F * (float)phd_winxmax / 640.0F) + GetFixedScale(200) + 1
		+ GetFixedScale(4);
}

/* Whether a slider's number carries a percent sign. Every slider's value is
 * its share of the bar, 0..100, so the number reads as a percentage - the
 * gamma row's too, where it is the slider's position, 50 the middle, and not
 * a strength. Both fonts hold the sign: game/chardef.inc's row 4 (15 wide,
 * scaled like a digit), and the HD atlas matches that row at score 1.000.
 *
 * The sign is drawn where the widest text, "100%", ends at least one gap,
 * GetFixedScale(4), before the screen's edge - the same gap the number keeps
 * after the bar's frame. The position does not move for it. Up to 4:3 the
 * room right of the bar is a sixteenth of the screen and "100%" does not fit:
 * 49 pixels against the 36 behind the gap at 640x480, 13 past the edge. There
 * the number stands without the sign rather than cross the edge or push the
 * bar; from about 1.46:1 on, 3:2 and wider, it fits at every size. */
static bool MenuSliderSignFits()
{
	char top[16];

	sprintf(top, "%d%%", MENU_SLIDER_TOP);
	return MenuSliderNumberX() + GetStringLength(top, 0, 0)
		+ GetFixedScale(4) <= phd_winxmax + 1;
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
		// visit would open straight into it. The same line stands at the end
		// of the ordinary path below, so a legacy page now leaves the tree
		// exactly the way every other page does.
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

	/* Which rows this page shows. The fixed rows come first and are on
	 * every page of a split list; the rest is paged in balanced halves
	 * (game/menu_nav.h). */
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
			// kept rather than re-derived (specific/LoadSave.cpp at base).
			DoSlider(400, y + 4 - (font_height >> 1), 200, 16, row->slider ? row->slider(row->param) : 0,
				0x1F1F1F, 0x3F3FFF, cur == i && enabled[i] ? 0x7F7F7F : 0x3F3F3F);
			/* The bar's number beside it, every frame, so it moves with the
			 * bar while a key or the pad is held: the value the bar is given,
			 * written as MenuSettleSlider writes it to the log, in the value
			 * rows' font and colours, with its percent sign where the screen
			 * has room for it. */
			sprintf(buf, MenuSliderSignFits() ? "%d%%" : "%d",
				(int)(row->slider ? row->slider(row->param) : 0));
			PrintString(MenuSliderNumberX(), y,
				(uchar)(!enabled[i] ? 3 : (cur == i ? 1 : 6)), buf, 0);
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
		// (tomb5/troyestuff.cpp).
		col = (uchar)(cur == nvis ? 1 : 6);
		PrintString(phd_centerx - (phd_centerx >> 3), MENU_BOTTOM_ROW * font_height, col, "\x19", 0);
		PrintString(phd_centerx + (phd_centerx >> 3), MENU_BOTTOM_ROW * font_height, col, "\x1B", 0);
		/* Between the two arrows, which page of how many ("1/N"), in the
		 * arrows' own colour, and inside the same `if` - it says nothing
		 * on a page that does not page.
		 *
		 * It fits, measured on the real font rather than assumed. The
		 * arrows sit at phd_centerx -/+ (phd_centerx >> 3), left-aligned,
		 * and the left/right glyphs (CharDef[99] and [101],
		 * game/chardef.inc) are 13 wide before the resolution scale of
		 * phd_winxmax / 512 - 16 at 640x480. So the gap between the two
		 * glyphs runs from centerx - 24 to centerx + 40, and "1/3", the
		 * widest string any list of this tree produces (the longest is
		 * the keyboard's at 3 pages), is 6 + 9 + 8 = 23 before the scale
		 * and 27 after it, centred: centerx - 13 to centerx + 14. Ten
		 * pixels of air on the tight side at 4:3, and the scale
		 * multiplies both sides alike at every other resolution. */
		sprintf(buf, "%ld/%ld", page_no + 1, pages);
		PrintString(phd_centerx, MENU_BOTTOM_ROW * font_height, col, buf, FF_CENTER);
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
				MenuLogSlider(page, row, dir);

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
