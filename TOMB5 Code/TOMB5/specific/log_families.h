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
#pragma once

/* The -log families a test build alone carries.
 *
 * Each name below is true in a test build (TOMB5_TEST_BUILD, tomb5.vcxproj:
 * Asan|x64 and be64.bat test) and false in a shipping one. A site of such a
 * family tests its name before gLogEnabled -
 *
 *	if (LogFam_CAMLOG && gLogEnabled)
 *
 * - so in a shipping EXE the compiler drops the block, its line and the
 * counters that serve only the line, and the family's word is not in the
 * file. The lines stay in the source as they were; a test build writes
 * them with -log as before. A family not named here writes in every build,
 * with -log (docs/Engine-Manual.md, the table of the families).
 *
 * REPLAYLOG, STATELOG and RNGTRACE stand inside the define already,
 * ASANLOG inside the sanitizer's own (__SANITIZE_ADDRESS__); they are named
 * here so that the list is the whole one. */
#ifdef TOMB5_TEST_BUILD
constexpr bool kLogTestBuild = true;
#else
constexpr bool kLogTestBuild = false;
#endif

constexpr bool LogFam_ARMLOG = kLogTestBuild;
constexpr bool LogFam_ASANLOG = kLogTestBuild;
constexpr bool LogFam_BRAWLLOG = kLogTestBuild;
constexpr bool LogFam_BRIDGELOG = kLogTestBuild;
constexpr bool LogFam_CAMLOG = kLogTestBuild;
constexpr bool LogFam_COGLOG = kLogTestBuild;
constexpr bool LogFam_COMBATLOG = kLogTestBuild;
constexpr bool LogFam_DOORLOG = kLogTestBuild;
constexpr bool LogFam_DRONELOG = kLogTestBuild;
constexpr bool LogFam_HAIRLOG = kLogTestBuild;
constexpr bool LogFam_HIDELOG = kLogTestBuild;
constexpr bool LogFam_HIGHLOG = kLogTestBuild;
constexpr bool LogFam_INTERPLOG = kLogTestBuild;
constexpr bool LogFam_INVLOG = kLogTestBuild;
constexpr bool LogFam_JOINTLOG = kLogTestBuild;
constexpr bool LogFam_KEYFARLOG = kLogTestBuild;
constexpr bool LogFam_KEYLOG = kLogTestBuild;
constexpr bool LogFam_KEYSEAMLOG = kLogTestBuild;
constexpr bool LogFam_LARALOG = kLogTestBuild;
constexpr bool LogFam_LIPLOG = kLogTestBuild;
constexpr bool LogFam_MODELOG = kLogTestBuild;
constexpr bool LogFam_MODERNLOG = kLogTestBuild;
constexpr bool LogFam_MOUSELOG = kLogTestBuild;
constexpr bool LogFam_PADLOG = kLogTestBuild;
constexpr bool LogFam_POISONLOG = kLogTestBuild;
constexpr bool LogFam_REPLAYLOG = kLogTestBuild;
constexpr bool LogFam_RNGLOG = kLogTestBuild;
constexpr bool LogFam_ROPELOG = kLogTestBuild;
constexpr bool LogFam_SPARKLOG = kLogTestBuild;
constexpr bool LogFam_SPLASHLOG = kLogTestBuild;
constexpr bool LogFam_SPRINGLOG = kLogTestBuild;
constexpr bool LogFam_STATELOG = kLogTestBuild;
constexpr bool LogFam_SWAPLOG = kLogTestBuild;
constexpr bool LogFam_TICKLOG = kLogTestBuild;
constexpr bool LogFam_TURNLOG = kLogTestBuild;
constexpr bool LogFam_VISLOG = kLogTestBuild;
constexpr bool LogFam_XRAYLOG = kLogTestBuild;
constexpr bool LogFam_ZIPLOG = kLogTestBuild;
constexpr bool LogFam_RNGTRACE = kLogTestBuild;
constexpr bool LogFam_ROOMLIST = kLogTestBuild;
constexpr bool LogFam_ROOMWALK = kLogTestBuild;
constexpr bool LogFam_ROOMFLASH = kLogTestBuild;
constexpr bool LogFam_ROOMPOP = kLogTestBuild;
constexpr bool LogFam_ZIPSKIP = kLogTestBuild;
