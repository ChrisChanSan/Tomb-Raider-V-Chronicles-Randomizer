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

/* THE 46 ACCENTED LETTERS, and nothing else: the body of an initialiser
 * list, moved out of game\text.cpp unchanged.
 *
 * Each row is the two characters an accented letter is built from - the
 * base letter and the accent mark, both of them ordinary glyphs of the font
 * page. PrintString draws the base at the letter's place and the mark at
 * base->w / 2 + x - 3, y + base->YOffset over it; a row of two spaces means
 * the language file names a letter this font has no picture for.
 *
 * It lives in its own file for the same reason game\chardef.inc does: the
 * diagnostic tool FontProbe composes the same accented letters to show that
 * an HD umlaut sits exactly where the classic one sat, and a copy of the
 * table in the tool could drift away from this one. */

	{'{', ' '},		//ç
	{'u', '^'},		//ü
	{'e', '\\'},	//é
	{'a', ']'},		//â
	{'a', '^'},		//ä
	{'a', '['},		//à
	{'a', '\\'},	//á
	{'{', ' '},		//ç
	{'e', ']'},		//ê
	{'e', '^'},		//ë
	{'e', '['},		//è
	{'|', '^'},		//ï
	{'|', ']'},		//î
	{'|', '['},		//ì
	{'A', '^'},		//Ä
	{'A', ']'},		//Â
	{'E', '\\'},	//É
	{'A', '\\'},	//Á			//NEW
	{' ', ' '},		//empty
	{'o', ']'},		//ô
	{'o', '^'},		//ö
	{'o', '['},		//ò
	{'u', ']'},		//û
	{'u', '['},		//ù
	{'y', '^'},		//ÿ
	{'O', '^'},		//Ö
	{'U', '^'},		//Ü
	{' ', ' '},		//empty
	{'O', '\\'},	//Ó
	{' ', ' '},		//empty
	{' ', ' '},		//empty
	{' ', ' '},		//empty
	{'a', '\\'},	//á
	{'|', '\\'},	//í
	{'o', '\\'},	//ó
	{'u', '\\'},	//ú
	{'n', '_'},		//ñ
	{'N', '_'},		//Ñ
	{' ', ' '},		//empty
	{' ', ' '},		//empty
	{'}', ' '},		//¿
	{' ', ' '},		//empty
	{' ', ' '},		//empty
	{' ', ' '},		//empty
	{' ', ' '},		//empty
	{'~', ' '}		//¡
