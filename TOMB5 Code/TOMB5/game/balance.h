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
 * Waffenschaden, Zaehigkeit und Schlagkraft der Gegner sowie die Hoerweite
 * der Levelkulisse ohne Neuuebersetzung einstellen koennen (Aufgabe #108).
 *
 * Fehlt die Datei, legt das Spiel sie beim Start mit den Originalwerten und
 * Erklaerungen an. Fehlt eine einzelne Zeile, gilt der Originalwert.
 */

struct BALANCE_INFO
{
	/* Schaden je Treffer. Die Engine haelt ihn in einem char, also 0..127. */
	long pistolen;
	long uzi;
	long schrotflinte;
	long armbrust;
	long fackel;
	long revolverFrueh;		/* bis einschliesslich Kolosseum */
	long revolverSpaet;		/* ab Ende Rom - das Spiel setzt beides je Level neu */
	long hkStand;			/* HK gezielt; lara1gun.cpp setzt beides je Schuss neu */
	long hkLauf;			/* HK aus der Bewegung */

	/* Prozentwerte, 100 = unveraendert */
	long gegnerLeben;
	long gegnerSchaden;
	long kulissenReichweite;
};

extern BALANCE_INFO balance;

void BalanceLaden();
void BalanceWaffenSetzen();
short GegnerSchaden(long schaden);
