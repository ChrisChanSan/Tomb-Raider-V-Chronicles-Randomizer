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
 * Balance.txt - eine reine Textdatei neben der PCtomb5.exe, mit der Spieler
 * jede Waffe, jeden Gegner und jede Levelgefahr einzeln einstellen koennen,
 * ohne den Quelltext neu zu uebersetzen (Aufgabe #108).
 *
 * Fehlt die Datei, legt das Spiel sie beim Start mit den Originalwerten und
 * Erklaerungen an. Fehlt eine einzelne Zeile, gilt der Originalwert.
 *
 * Alle Werte sind absolut wie im Original. Die beiden Prozentzeilen
 * GegnerLeben und GegnerSchaden wirken zusaetzlich obendrauf, damit man das
 * ganze Spiel mit einer Zahl haerter oder weicher stellen kann, ohne die
 * Feinabstimmung darunter anzufassen.
 */

struct BALANCE_INFO
{
	/* --- Schaden von Laras Waffen. Die Engine haelt ihn in einem char. --- */
	long pistolen;
	long uzi;
	long schrotflinte;
	long armbrust;
	long fackel;
	long revolverFrueh;		/* bis einschliesslich Kolosseum */
	long revolverSpaet;		/* danach - setup.cpp setzt beides je Level neu */
	long hkStand;			/* HK gezielt; lara1gun.cpp setzt beides je Schuss neu */
	long hkLauf;			/* HK aus der Bewegung */

	/* --- Lebenspunkte der Gegner, je Objektplatz --- */
	long lpTRex;			/* liegt auf dem Platz SAS (TR3-Port) */
	long lpRaptor;			/* liegt auf dem Platz REAPER (TR3-Port) */
	long lpCompy;			/* liegt auf dem Platz SAILOR (TR3-Port) */
	long lpWachmannBlau;
	long lpSwat;
	long lpSwatPlus;
	long lpMafioso;
	long lpWissenschaftler;
	long lpKranfuehrer;
	long lpWachmannJoby;	/* BLUE_GUARD2, der zweite Wachmann aus joby3 */
	long lpMiniUBoot;
	long lpLoewe;
	long lpHund;
	long lpHusky;
	long lpKraehe;			/* traegt im Rando auch den Geier aus TR3 */
	long lpMafia2;
	long lpPierre;
	long lpLarson;
	long lpHitman;
	long lpScharfschuetze;
	long lpKoch;
	long lpTwoGun;
	long lpHydra;
	long lpKobold;
	long lpIrrlicht;
	long lpLabyrinthmonster;
	long lpSumpfmonster;
	long lpSkelett;
	long lpGladiator;
	long lpRoemischerGott;
	long lpRoemischerGott2;

	/* --- Schaden der Gegner an Lara, je Angriff --- */
	long sTRexTritt;
	long sTRexTrittLauf;
	long sTRexBiss;
	long sRaptorBiss;
	long sRaptorSprung;
	long sCompyBiss;
	long sHundBiss;
	long sHundSprung;
	long sHundNachsetzen;
	long sHuskyBiss;
	long sHuskySprung;
	long sLoeweSprung;
	long sLoeweTatze;
	long sKraeheHieb;
	long sFledermaeuse;
	long sSpinnen;
	long sKochMesser;
	long sSkelettHieb;
	long sGladiatorHieb;
	long sGottHieb;
	long sGottBlitz;
	long sHydraStrahl;
	long sKoboldWurf;
	long sLabyrinthmonster;
	long sSumpfmonster;
	long sTwoGunPlasma;
	long sTorpedo;

	/* Schusswaffen der menschlichen Gegner (ShotLara) */
	long sSoldatFrueh;		/* bis einschliesslich Old Mill */
	long sSoldatSpaet;
	long sSoldatKurz;
	long sHitmanSchuss;
	long sMafia2Schuss;
	long sPierreSchuss;
	long sLarsonSchuss;
	long sScharfschuetzeSchuss;

	/* --- Levelgefahren. Keine Gegner, deshalb ohne Prozentzeile. --- */
	long sAutogun;
	long sGunship;
	long sIris;
	long sArea51Laser;
	long sStromkabel;

	/* --- Prozentwerte, 100 = unveraendert --- */
	long waffenSchaden;
	long gegnerLeben;
	long gegnerSchaden;
	long kulissenReichweite;
};

extern BALANCE_INFO balance;

void BalanceLaden();
void BalanceWaffenSetzen();
void BalanceGegnerSetzen();
char WaffenSchaden(long basis);
short GegnerSchaden(long schaden);
