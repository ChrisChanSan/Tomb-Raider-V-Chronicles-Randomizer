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

/*
 * Balance.txt - a plain text file next to PCtomb5.exe that lets players tune
 * every weapon, every enemy and every level hazard individually without
 * recompiling the source.
 *
 * If the file is missing, the game creates it at startup with the original
 * values and explanations. If a single line is missing, the original value
 * applies.
 *
 * All values are absolute as in the original. The two percentage lines
 * EnemyHealth and EnemyDamage apply on top, so the whole game can be made
 * harder or softer with one number without touching the fine tuning below.
 */

struct BALANCE_INFO
{
	/* --- Damage of Lara's weapons. The engine holds it in a char. --- */
	long pistols;
	long uzi;
	long shotgun;
	long crossbow;
	long torch;
	long revolverEarly;		/* up to and including Colosseum */
	long revolverLate;		/* afterwards - setup.cpp sets both per level */
	long hkStanding;		/* HK aimed; lara1gun.cpp sets both per shot */
	long hkMoving;			/* HK fired while moving */

	/* --- Enemy hit points, per object slot --- */
	long hpTRex;			/* lives on the SAS slot (TR3 port) */
	long hpRaptor;			/* lives on the REAPER slot (TR3 port) */
	long hpCompy;			/* lives on the SAILOR slot (TR3 port) */
	long hpGuardBlue;
	long hpSwat;
	long hpSwatPlus;
	long hpMafia;
	long hpScientist;
	long hpCraneGuy;
	long hpGuardJoby;		/* BLUE_GUARD2, the second guard from joby3 */
	long hpMiniSub;
	long hpLion;
	long hpDog;
	long hpHuskie;
	long hpCrow;			/* in the rando this also carries the TR3 vulture */
	long hpMafia2;
	long hpPierre;
	long hpLarson;
	long hpHitman;
	long hpCyborg;			/* the same model as the pool enemy, but mortal */
	long hpSniper;
	long hpChef;
	long hpBrawler;		/* JOBY2's cutscene soldier on the chef's control */
	long hpDrone;			/* RICH3's gunship at the vulture's size, a pool enemy */
	long hpTwoGun;
	long hpHydra;
	long hpImp;
	long hpWillOWisp;
	long hpMazeMonster;
	long hpSwampMonster;
	long hpSkeleton;
	long hpGladiator;
	long hpRomanGod;
	long hpRomanGod2;

	/* --- Enemy damage against Lara, per attack --- */
	long dmgTRexKick;
	long dmgTRexKickRun;
	long dmgTRexBite;
	long dmgRaptorBite;
	long dmgRaptorJump;
	long dmgCompyBite;
	long dmgDogBite;
	long dmgDogJump;
	long dmgDogChase;
	long dmgHuskieBite;
	long dmgHuskieJump;
	long dmgLionJump;
	long dmgLionPaw;
	long dmgCrowStrike;
	long dmgBats;
	long dmgSpiders;
	long dmgChefKnife;
	long dmgBrawlerFist;
	long dmgDrone;			/* one hit of its gun, every other frame of a burst */
	long dmgSkeletonStrike;
	long dmgGladiatorStrike;
	long dmgGodStrike;
	long dmgGodLightning;
	long dmgHydraBeam;
	long dmgImpThrow;
	long dmgMazeMonster;
	long dmgSwampMonster;
	long dmgTwoGunPlasma;
	long dmgTorpedo;

	/* Firearms of the human enemies (ShotLara) */
	long dmgSoldierEarly;	/* up to and including Old Mill */
	long dmgSoldierLate;
	long dmgSoldierShort;
	long dmgHitmanShot;
	long dmgCyborgShot;
	long dmgMafia2Shot;
	long dmgPierreShot;
	long dmgLarsonShot;
	long dmgSniperShot;

	/* --- Level hazards. Not enemies, hence outside the percentage line. --- */
	long dmgAutogun;
	long dmgGunship;
	long dmgIris;
	long dmgArea51Laser;
	long dmgPowerCable;

	/* --- Percentages, 100 = unchanged --- */
	long weaponDamage;
	long enemyHealth;
	long enemyDamage;
	long sceneryRange;
};

extern BALANCE_INFO balance;

void BalanceLoad();
void BalanceSetWeapons();
void BalanceSetEnemies();
char WeaponDamage(long base);
short EnemyDamage(long damage);
