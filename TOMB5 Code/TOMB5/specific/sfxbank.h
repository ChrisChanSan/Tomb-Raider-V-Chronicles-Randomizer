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

// READING A MAIN.SFX, and nothing else (#153, round M1).
//
// The parsing that used to sit inside hqsfx.cpp, lifted out so that the
// engine and the SfxProbe diagnostic tool read a bank through the SAME code -
// the way hd_install and hd_lara are shared. hqsfx.cpp keeps the engine's
// half: the level bridge, the substitution and the log.
//
// The file is the player's own data, never shipped by this project, and is
// read where it lies. Its layout, all little endian (the header is the
// remaster's own invention - Tomb Raider 2 and 3, the only ones with a real
// MAIN.SFX, start at offset 0 with the first RIFF):
//
//   0x0000  1000 B  sample_lut[500], int16, -1 for "no sound"
//   0x03E8     4 B  number of SAMPLE_INFOs
//   0x03EC     8 B each  SAMPLE_INFO[], byte for byte the engine's own struct
//           4000 B  500 x int64, a bit mask of the levels each sound is used
//                   in - not needed here, but it is what makes the file
//                   self-describing
//           then    the RIFFs, back to back, ending on the last byte
//
// That the first RIFF lands exactly where the header ends, and that the chain
// of RIFFs comes out exactly on the last byte, is what turns a guess about
// the layout into a test of it.
//
// WHERE THE LAYOUT COMES FROM (stated here in audit round A1, where it had
// been left unsaid): from the bytes of the player's own MAIN.SFX and from
// nothing else. No remaster program was opened, read or disassembled for it -
// the table above is a reading of one data file, and the two tests in the
// paragraph above are what proved the reading right. The work is recorded in
// commit dbc0b30 (#117), whose message carries the measurements: 463 samples,
// three of them resampled after all, and the residue test that showed the
// rest are not the 22050 Hz originals upsampled.

#define SFX_LUT_ENTRIES		500
#define SFX_MAX_SAMPLES		4096	// sanity bound; the shipped file has 473

struct SfxBank;

/* Opens and validates. Returns null and sets *why (never null, a plain
 * English clause) when the file is not a bank of this shape. */
SfxBank* SfxBankOpen(const char* path, const char** why);
void SfxBankClose(SfxBank* bank);

int SfxBankSamples(const SfxBank* bank);	// RIFFs in the chain
int SfxBankInfos(const SfxBank* bank);		// SAMPLE_INFO entries
int SfxBankIdsUsed(const SfxBank* bank);	// LUT entries that name one
long SfxBankLargest(const SfxBank* bank);	// the largest RIFF, in bytes

// sound id -> SAMPLE_INFO index, -1 for "this bank has no such sound".
int SfxBankLut(const SfxBank* bank, int soundId);
// An info's first sample number and how many takes follow it. The variation
// count sits in bits 2..5 of flags, the same place sound.cpp reads it from.
int SfxBankInfoNumber(const SfxBank* bank, int info);
int SfxBankInfoVariations(const SfxBank* bank, int info);

/* WHICH LEVELS THE BANK SAYS THIS SOUND BELONGS TO (#153, round M3): the
 * 64 bit field the format keeps for every sound id, one bit per level FILE.
 * 0 for an id the bank does not name.
 *
 * WHICH BIT IS WHICH LEVEL IS NOWHERE IN THE FILE and is not written down
 * here either - hqsfx.cpp works it out for the level it is loading, by
 * asking which bit's set of ids covers the level's own soundmap. Proven on
 * the sixteen classic levels: every one of them matches exactly one bit and
 * matches it completely (ANDREA1 bit 1, ANDREA2 2, ANDREA3 3, JOBY2 4,
 * JOBY3 5, JOBY4 6, JOBY5 7, ANDY1 8, ANDY2 9, ANDY3 10, RICH1 11, RICH2 12,
 * RICH3 14), with the runner-up covering at most two thirds. */
long long SfxBankMask(const SfxBank* bank, int soundId);

/* WHICH OF THOSE SIXTY-FOUR BITS IS THE LEVEL WHOSE SOUND MAP THIS IS
 * (#153, round M3), or -1 when none of them fits.
 *
 * Nothing in the file says which bit is which level and nothing here writes
 * it down: a level's own sound map IS the answer. Every id the level has and
 * the bit claims counts for that bit, every id the level has and the bit does
 * not claim counts against it, and the best score wins - which stops a
 * neighbour's bit from winning on the sounds every level shares.
 *
 * Proven over the sixteen classic levels: each matches exactly one bit and
 * matches it COMPLETELY - all of its own ids, none missing - while the
 * runner-up covers at most two thirds. ANDREA1 is bit 1, ANDREA2 2, ANDREA3
 * 3, JOBY2 4, JOBY3 5, JOBY4 6, JOBY5 7, ANDY1 8, ANDY2 9, ANDY3 10, RICH1
 * 11, RICH2 12, RICH3 14. Worked out rather than tabulated, because a table
 * of level names is a table a randomized level or a level pack would be
 * silently wrong about.
 *
 * `levelLut` is the level's own sound map, `entries` its length. Both the
 * engine's bridge and SfxProbe ask this one function. */
int SfxBankLevelBit(const SfxBank* bank, const short* levelLut, long entries);

/* IS A PAIRING PLAUSIBLE BY LENGTH (#153, round M3 - the rule itself is as
 * old as #117)? Both sides hold the same recording, so at twice the rate the
 * bank's sample must be about twice as long; further off than a fifth is not
 * the same sound. `pcmBytes22k` of 0 means "do not judge", which is what the
 * barefoot set uses - a different recording on purpose.
 *
 * Here rather than in hqsfx.cpp so that SfxProbe judges a pairing with the
 * engine's own arithmetic instead of a copy of it. */
bool SfxPairPlausible(long bankDataBytes, long pcmBytes22k);

/* One sample's RIFF, read into the bank's own buffer - valid until the next
 * call on this bank. Null when the number is out of range or the read failed.
 * *bytes gets its length including the 8 byte RIFF header. */
const char* SfxBankSample(SfxBank* bank, int sample, long* bytes);
// Its length without reading it.
long SfxBankSampleBytes(const SfxBank* bank, int sample);

/* WHERE THE TWO CHUNKS LIE (#153, round M1, second fix round).
 *
 * The check below WALKS the chunks, because a fact or a LIST block may stand
 * in front of fmt and a wav out of any editor may well have one - Reaper,
 * Pro Tools and libsndfile all write a 28 byte JUNK block first, as room to
 * grow into RF64. A caller that only judges the file needs none of this; a
 * caller that then hands the format block and the audio to XAudio2 must know
 * where the walk found them instead of assuming fmt comes first, which is the
 * assumption this struct exists to retire.
 *
 * All offsets are from the first byte of the RIFF and point at a chunk's
 * BODY, past its eight byte header - fmtAt is the WAVEFORMATEX itself. */
struct SfxRiffParts
{
	long fmtAt;
	long fmtBytes;
	long dataAt;
	long dataBytes;
	long byteRate;
};

/* WHAT A PAYLOAD HAS TO BE for this engine to hand it to XAudio2: a RIFF
 * WAVE, uncompressed PCM (tag 1), mono, 16 bit, with a data chunk that lies
 * inside the file. Returns false and sets *why when it is not.
 *
 * *dataBytes gets the data chunk's length and *byteRate the format's bytes
 * per second, which is what the caller needs to compare two recordings of the
 * same sound at two different rates. *parts, when asked for, gets the whole
 * of the above and is only written when the answer is true. */
bool SfxRiffCheck(const char* riff, long bytes, long* dataBytes, long* byteRate,
	const char** why, SfxRiffParts* parts = 0);

/* A loose <NNNN>.wav from a mod, read whole into a malloc'ed buffer the
 * caller owns (free with std::free). Null and *why on trouble. `cap` is the
 * largest file that will be read at all - a guard, not a format rule. */
char* SfxWavLoad(const char* path, long cap, long* bytes, const char** why);

/* THE CHAIN OF BANKS, ONE CANDIDATE AT A TIME (#153, round M1, fix round).
 *
 * `link` counts from 0 and the order is the engine's: every active mod that
 * ships 5\DATA\MAIN.SFX first, in the scan's own order, then
 * <gameFolder>\audio\MAIN.SFX, then the installed remaster's own bank read
 * where it lies. False means `link` is past the end.
 *
 * ONE ORDER IN ONE PLACE. The parsing was shared from the start, but the
 * ORDER was written twice - once in HQSfxOpen and once in SfxProbe - and a
 * tool holding its own copy of a rule is the drift this very round found and
 * fixed in ModelProbe's page rule. Both walk this function now, so a later
 * round that changes the order changes it for the tool as well.
 *
 * LAZY ON PURPOSE. The last link is the installed remaster and finding it
 * walks the registry and every Steam library, so nothing is looked up until
 * the link that needs it is asked for. A caller that stops early never pays.
 *
 * `gameFolder` empty means the current directory, which is what the engine
 * hands in - its audio\MAIN.SFX is then the relative path it always was.
 * `install` empty means ask HDInstallRoot(). `modIndex` gets the mod that
 * owns the link, or -1. A link can come back true with an EMPTY path - "there
 * is none to look at" - and the caller reports that and goes on.
 *
 * Needs the mods scanned; HDModsScan() before the first call. */
bool SfxBankChainLink(int link, const char* gameFolder, const char* install,
	char* path, int pathSize, char* label, int labelSize, int* modIndex);
