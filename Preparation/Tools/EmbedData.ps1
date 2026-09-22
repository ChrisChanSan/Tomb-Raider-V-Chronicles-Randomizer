# Modified 2026 by ChrisChanSan.
#
# Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
# TR5 Randomizer project. The individual changes are recorded in the git
# history of this source tree.
#
# TOMB5 is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. See the LICENSE file at the root of this source tree.
#

# Bakes the precomputed data into the randomizer's source code.
#
# Produces TR5Randomizer\src\core\EmbeddedData.cpp: one zlib stream per file
# as a byte table, plus an index, unpacked at run time with the zlib the
# randomizer already links (DataFile.cpp). A file in data\ beside the EXE
# always wins over the baked copy, which is only the floor - see
# EmbeddedData.h.
#
# Run after every re-curation, once the zone exports in build\data changed
# (ZoneAnalysis.ps1, or BuildRelease.ps1 which regenerates all fifteen), then
# rebuild with b1.bat/b2.bat. The generated .cpp is tracked in the
# repository rather than left gitignored like build\data.
#
# The build itself never calls this script, so b1.bat/b2.bat need only a
# compiler and no PowerShell, which GPLv3's buildable-from-source
# requirement demands.
#
# PowerShell rather than a small C++ tool, because the files it reads are
# themselves produced by PowerShell. The one cost is that .NET's
# DeflateStream writes raw deflate while zlib's uncompress() wants the RFC
# 1950 wrapper, so the two header bytes and the Adler-32 trailer below are
# written by hand; a build with data\ renamed away round-trips every stream
# through uncompress() and fails loudly on a wrong header or checksum.
param(
    # Where the files to bake lie. The developer mirror, not the release
    # folder: build\data is what b1/b2 run against.
    [string]$DataDir = "",
    [string]$Out     = ""
)
$ErrorActionPreference = "Stop"

# Everything relative to the script location (Tools -> two levels up = repo
# root), so the script runs on any machine and carries no developer paths.
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ($DataDir -eq "") { $DataDir = Join-Path $Root "TR5Randomizer\build\data" }
if ($Out     -eq "") { $Out     = Join-Path $Root "TR5Randomizer\src\core\EmbeddedData.cpp" }

if (-not (Test-Path $DataDir)) { throw "data folder not found: $DataDir" }

#-----------------------------------------------------------------------------
# Which files. Named explicitly rather than "everything in the folder": the
# folder also holds data\secrets (hand-maintained lists, deliberately NOT
# baked - see EmbeddedData.h) and, on a developer machine, whatever else got
# dropped there while testing.
#-----------------------------------------------------------------------------
$files = @()
$files += Get-ChildItem -Path (Join-Path $DataDir "zones_*.json") -File | Sort-Object Name
foreach ($csv in @("VanillaLevelChecksums.csv", "Tr3Checksums.csv")) {
    $p = Join-Path $DataDir $csv
    if (-not (Test-Path $p)) { throw "missing: $p" }
    $files += Get-Item $p
}

# A loud abort rather than a smaller EXE that quietly lacks a level. Fifteen
# is the campaign; Title has no zone export and never had one.
$zoneCount = @($files | Where-Object { $_.Name -like 'zones_*' }).Count
if ($zoneCount -ne 15) {
    throw "expected 15 zone exports in $DataDir, found $zoneCount - regenerate them (BuildRelease.ps1) before baking."
}

#-----------------------------------------------------------------------------
# raw bytes -> zlib stream (RFC 1950)
#
# 0x78 0x9C is the usual header: CM=8 (deflate), CINFO=7 (32K window),
# FDICT=0, and (0x78*256 + 0x9C) is divisible by 31, which is the check
# zlib's inflate performs on those two bytes. The trailer is the Adler-32 of
# the UNPACKED data, big endian.
#-----------------------------------------------------------------------------
function ConvertTo-ZlibStream([byte[]]$raw) {
    $mem = New-Object System.IO.MemoryStream
    $def = New-Object System.IO.Compression.DeflateStream(
        $mem, [System.IO.Compression.CompressionMode]::Compress, $true)
    if ($raw.Length -gt 0) { $def.Write($raw, 0, $raw.Length) }
    $def.Dispose()
    $deflated = $mem.ToArray()
    $mem.Dispose()

    # Adler-32. $sumA and $sumB spelled out on purpose: PowerShell does not
    # tell $a from $A, and a pair of one-letter variables risks silently
    # returning plausible zeros.
    $sumA = 1
    $sumB = 0
    foreach ($byte in $raw) {
        $sumA = ($sumA + $byte) % 65521
        $sumB = ($sumB + $sumA) % 65521
    }

    $out = New-Object 'System.Collections.Generic.List[byte]'
    $out.Add([byte]0x78)
    $out.Add([byte]0x9C)
    $out.AddRange($deflated)
    $out.Add([byte](($sumB -shr 8) -band 0xFF))
    $out.Add([byte]( $sumB        -band 0xFF))
    $out.Add([byte](($sumA -shr 8) -band 0xFF))
    $out.Add([byte]( $sumA        -band 0xFF))
    return $out.ToArray()
}

#-----------------------------------------------------------------------------
# Emit
#-----------------------------------------------------------------------------
$hex = New-Object 'string[]' 256
for ($i = 0; $i -lt 256; $i++) { $hex[$i] = ('0x{0:X2},' -f $i) }

$sb = New-Object System.Text.StringBuilder
function Emit($text) { [void]$sb.Append($text); [void]$sb.Append("`r`n") }

Emit '/*'
Emit ' * TR5 Randomizer'
Emit ' * Copyright (C) 2026 ChrisChanSan'
Emit ' *'
Emit ' * This program is free software: you can redistribute it and/or modify it'
Emit ' * under the terms of the GNU General Public License as published by the Free'
Emit ' * Software Foundation, either version 3 of the License, or (at your option)'
Emit ' * any later version.'
Emit ' *'
Emit ' * This program is distributed in the hope that it will be useful, but WITHOUT'
Emit ' * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or'
Emit ' * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for'
Emit ' * more details.'
Emit ' *'
Emit ' * You should have received a copy of the GNU General Public License along'
Emit ' * with this program. If not, see <https://www.gnu.org/licenses/>.'
Emit ' */'
Emit '//----------------------------------------------------------------------------'
Emit '// GENERATED FILE - DO NOT EDIT BY HAND.'
Emit '//'
Emit '// Made by Preparation\Tools\EmbedData.ps1 out of TR5Randomizer\build\data.'
Emit '// Every table below is one zlib stream (RFC 1950), unpacked at run time by'
Emit '// DataFile.cpp. What this is for, and why a file in data\ beside the EXE'
Emit '// still wins over the copy baked in here, stands in EmbeddedData.h.'
Emit '//'
Emit '// Edits here are lost on the next re-curation. The hand-written half is in'
Emit '// EmbeddedData.h and DataFile.cpp.'
Emit '//----------------------------------------------------------------------------'
Emit '#include "EmbeddedData.h"'
Emit ''
Emit 'namespace'
Emit '{'

$index = @()
$totalRaw = 0
$totalPacked = 0
$n = 0

foreach ($f in $files) {
    $raw    = [System.IO.File]::ReadAllBytes($f.FullName)
    $packed = ConvertTo-ZlibStream $raw

    $sym = "kBlob$n"
    # Plain numbers, no thousands separators: {0:N0} would follow the
    # machine's locale, and a generated file must not differ between two
    # developers' regions.
    Emit ("    // {0} - {1} bytes, packed to {2}" -f $f.Name, $raw.Length, $packed.Length)
    Emit "    const unsigned char $sym[] = {"
    [void]$sb.Append('        ')
    for ($i = 0; $i -lt $packed.Length; $i++) {
        [void]$sb.Append($hex[$packed[$i]])
        if ((($i + 1) % 20) -eq 0 -and ($i + 1) -lt $packed.Length) {
            [void]$sb.Append("`r`n        ")
        }
    }
    Emit ''
    Emit '    };'

    $index += [pscustomobject]@{ Name = $f.Name; Sym = $sym; Raw = $raw.Length }
    $totalRaw    += $raw.Length
    $totalPacked += $packed.Length
    $n++

    Write-Output ("  {0,-28} {1,9:N0} -> {2,8:N0}" -f $f.Name, $raw.Length, $packed.Length)
}

Emit '}'
Emit ''
Emit 'namespace EmbeddedData'
Emit '{'
Emit '    // extern on the definition too: a const array without it would get'
Emit '    // internal linkage and the declaration in the header would never find it.'
Emit '    extern const File g_files[] = {'
foreach ($e in $index) {
    Emit ('        {{ "{0}", {1}, sizeof({1}), {2} }},' -f $e.Name, $e.Sym, $e.Raw)
}
Emit '    };'
Emit '    extern const size_t g_fileCount = sizeof(g_files) / sizeof(g_files[0]);'
Emit '}'

# UTF-8 without a BOM, and CRLF as everywhere else in this tree. Written in
# one go from a StringBuilder - never Get-Content -Raw plus WriteAllText,
# which is how a BOM-less file gets its encoding applied twice.
[System.IO.File]::WriteAllText($Out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))

Write-Output ""
Write-Output ("{0} files, {1:N0} bytes -> {2:N0} packed ({3:N1} %)" -f `
    $files.Count, $totalRaw, $totalPacked, (100.0 * $totalPacked / $totalRaw))
Write-Output ("written: {0} ({1:N0} bytes)" -f $Out, (Get-Item $Out).Length)
