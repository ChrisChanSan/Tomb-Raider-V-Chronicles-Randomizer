# Assembles the shippable folder: randomizer, engine, bundled source code
# and the licenses.
#
# The package is 64-bit only. The engine is be64.bat's
# release64\PCtomb5.exe, shipped as PCTomb5.exe - the name the game and
# Steam start; the 32-bit release\PCtomb5_x86.exe of be.bat is built on
# request, to compare, and never goes in. Beside the two EXEs: the films'
# three FFmpeg DLLs from vendor\ffmpeg-min\bin, and the three Visual C++
# runtime DLLs both programs
# import (/MD) - vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll - from
# the compiler's redist folder: $env:VCToolsRedistDir (set by vcvars64.bat),
# or -RedistDir when the script runs outside a vcvars64 shell. No installer.
#
# Why the COMPLETE engine source must come along and not only our changed
# files: TOMB5 is under the GNU GPL v3. Our version is a derivative work,
# and GPLv3 section 6 demands, when distributing a binary, the complete
# corresponding source code - not a set of changes plus a pointer to the
# upstream project. The tree is 8 MB, so including it completely costs
# practically nothing and makes the question moot. Section 5a additionally
# demands a clear notice of the modification including a date: that is
# TOMB5 Code\MODIFICATIONS.md.
#
# What deliberately does NOT go in here: game data. Neither levels nor
# music, videos or language files. Tomb Raider Chronicles belongs to its
# rights holders; a player brings their own installation, and the
# randomizer copies what it needs on first start by itself.
param(
    [string]$OutDir = "",
    [string]$RedistDir = "",
    [switch]$Clean
)
$ErrorActionPreference = "Stop"

# Everything relative to the script location (Tools -> two levels up = repo
# root), so the script runs on any machine and carries no developer paths.
$Root      = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ($OutDir -eq "") { $OutDir = Join-Path $Root "Release Package" }
$EngineSrc = "$Root\TOMB5 Code"
$EngineExe = "$Root\release64\PCtomb5.exe"
$FfmpegBin = "$Root\vendor\ffmpeg-min\bin"
$FfmpegDlls = @('avutil-61.dll', 'avcodec-63.dll', 'avformat-63.dll')
# The runtime set, as the import tables of the three EXEs name it.
$CrtDlls   = @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll')
if ($RedistDir -eq "") { $RedistDir = "$env:VCToolsRedistDir" }
$RandoRoot = "$Root\TR5Randomizer"
$RandoExe  = "$RandoRoot\build\TR5Randomizer.exe"
# The MIT license comes from the VERSIONED imgui (external\imgui) - the
# loose download folder under "Preparation" is untracked and
# does not exist in a fresh clone at all.
$ImGui     = "$Root\TR5Randomizer\external\imgui"
$ZoneTools = "$Root\Preparation\Tools"
$LevelInfo = "$Root\Preparation\Level Infos"

function Say($t) { Write-Output $t }

if ($Clean -and (Test-Path $OutDir)) {
    Say "cleaning $OutDir"
    Get-ChildItem $OutDir -Force | Remove-Item -Recurse -Force
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

# --- 1. Engine ---------------------------------------------------------------
if (-not (Test-Path $EngineExe)) { throw "engine not built: $EngineExe" }
Copy-Item $EngineExe "$OutDir\PCTomb5.exe" -Force
Say ("Engine        : PCTomb5.exe ({0:N0} KB)" -f ((Get-Item $EngineExe).Length/1KB))

# --- 1b. The DLLs beside the EXEs --------------------------------------------
# The films: the engine loads the three FFmpeg DLLs at run time from its own
# folder (specific\fmv_av.cpp). LGPL, linked dynamically; the licence text
# goes into Licenses\ below.
foreach ($d in $FfmpegDlls) {
    $p = Join-Path $FfmpegBin $d
    if (-not (Test-Path $p)) { throw "FFmpeg DLL missing: $p" }
    Copy-Item $p $OutDir -Force
}
Say "FFmpeg        : $($FfmpegDlls -join ', ')"
# The C++ runtime, unchanged from the compiler's redist folder: the
# x64\Microsoft.VC14?.CRT folder under it, its version read, not written
# down here. One set serves the engine and the randomizer.
if ($RedistDir -eq "") {
    throw "no redist folder: run under vcvars64 (VCToolsRedistDir) or give -RedistDir"
}
$crtDir = Get-ChildItem (Join-Path $RedistDir "x64") -Directory -ErrorAction SilentlyContinue |
          Where-Object { $_.Name -like "Microsoft.VC14?.CRT" } |
          Sort-Object Name | Select-Object -Last 1
if (-not $crtDir) { throw "no Microsoft.VC14?.CRT folder in $(Join-Path $RedistDir 'x64')" }
foreach ($d in $CrtDlls) {
    $p = Join-Path $crtDir.FullName $d
    if (-not (Test-Path $p)) { throw "runtime DLL missing: $p" }
    Copy-Item $p $OutDir -Force
}
Say ("C++ runtime   : {0} ({1}, from {2})" -f ($CrtDlls -join ', '),
     (Get-Item (Join-Path $crtDir.FullName $CrtDlls[0])).VersionInfo.FileVersion,
     $crtDir.FullName)

# --- 2. Randomizer -----------------------------------------------------------
if (Test-Path $RandoExe) {
    Copy-Item $RandoExe "$OutDir\TR5Randomizer.exe" -Force
    Say ("Randomizer    : TR5Randomizer.exe ({0:N0} KB)" -f ((Get-Item $RandoExe).Length/1KB))
} else {
    Say "Randomizer    : not built yet - skipped"
}

# --- 3. Precomputed data the randomizer reads --------------------------------
# This way the tool needs NO PowerShell at runtime. That was an explicit
# requirement: a PowerShell dependency would be unreasonable for normal
# users and moreover regularly draws the attention of virus scanners.
# This folder is the randomizer's only copy of the files below - the EXEs
# carry none (src\core\DataFile.h) - so the fifteen zone exports, the two
# checksum lists and data\secrets must all land here.
$dataOut = "$OutDir\data"
New-Item -ItemType Directory -Path $dataOut -Force | Out-Null
Copy-Item "$LevelInfo\VanillaLevelChecksums.csv" $dataOut -Force
# The Tomb Raider III files the four guests were ported against, by size
# and SHA256. The randomizer names the edition of a player's files with it
# (Tr3Edition), and a player whose dinosaurs look wrong can check by hand
# whether their installation is the one the port was built from.
Copy-Item "$Root\Preparation\Tr3Checksums.csv" $dataOut -Force
$zones = 0
foreach ($f in (Get-ChildItem "$LevelInfo\*.json" | Sort-Object Name)) {
    if ($f.BaseName -eq 'Title') { continue }
    & "$ZoneTools\ZoneAnalysis.ps1" -LevelJson $f.FullName -LevelName $f.BaseName `
        -ExportZonesJson "$dataOut\zones_$($f.BaseName).json" *> $null
    $zones++
}
# level_<name>.json is NOT shipped any more, and that is the point of the
# change: it was 45 MB, the randomizer read three arrays out of it (1.2% of
# the bytes), and those three come out of the .trc it opens anyway - proven
# level by level with InfoCompare.exe before the switch. What stays is the
# zone export, which is this project's own curation and not a copy of Core's
# level data. The level infos are still needed HERE, at build time, because
# ZoneAnalysis reads the sector grid from them.
Say "Data          : $zones zone exports + checksum list"

# The secret lists. The GUI looks for them in data\secrets next to the EXE
# (findSecretSources) - without them both dropdowns stay empty and the whole
# secret feature silently does not exist for anyone but us. They are
# hand-maintained text, not generated, so they are copied, not rebuilt.
$secretsOut = "$dataOut\secrets"
New-Item -ItemType Directory -Path $secretsOut -Force | Out-Null
$secretsIn = "$Root\Preparation\Curation\Secrets"
$lists = 0
foreach ($f in (Get-ChildItem "$secretsIn\*.txt" | Sort-Object Name)) {
    Copy-Item $f.FullName $secretsOut -Force
    $lists++
}
Say "Secret lists  : $lists files (data\secrets)"

# --- 3b. trview type names ---------------------------------------------------
# Fourteen slots that a level viewer would otherwise name wrongly or not at
# all: four (31/43/53/75) carry different contents afterwards - the TR3
# guests overwrite SAS, CROW, SAILOR and REAPER; two (47/55) are ordinary
# Chronicles slots trview has no entry for; six (460/461/469/471/472/473)
# lie beyond trview's list, which ends at id 452; and two it knows but cannot
# tell apart - 41 and 85 are both called "Dog" although only one is the
# husky, and "Soldier Statue" cannot tell apart the two Roman gods that can
# stand in one level.
#
# Not needed to play. It is here because anyone reporting an oddity is asked
# to look at the level, and a viewer showing "Crow" where a vulture stands
# turns a clear report into a confusing one.
$viewOut = "$OutDir\trview"
$typesIn = "$Root\Preparation\Item Infos\types.json"
if (Test-Path $typesIn) {
    New-Item -ItemType Directory -Path $viewOut -Force | Out-Null
    Copy-Item $typesIn "$viewOut\types.json" -Force
    Say "trview names  : types.json (trview\)"
}

# --- 4. Source code, GPLv3 obligation ----------------------------------------
$srcOut = "$OutDir\Source\TOMB5 engine"
New-Item -ItemType Directory -Path $srcOut -Force | Out-Null
$skipDirs = '\\(Build|Release|Debug|x64|\.vs|ipch)\\'
$skipExt  = @('.obj','.pdb','.exe','.dll','.ipdb','.iobj','.tlog','.log','.user','.aps')
$copied = 0
foreach ($f in (Get-ChildItem $EngineSrc -Recurse -File)) {
    # The skip list is matched against the path INSIDE the engine folder,
    # never against the clone's own location: a checkout under a folder
    # called x64, Release or Debug would otherwise ship no source at all.
    $rel = $f.FullName.Substring($EngineSrc.Length).TrimStart('\')
    if (('\' + $rel) -match $skipDirs) { continue }
    if ($skipExt -contains $f.Extension.ToLower()) { continue }
    $dst = Join-Path $srcOut $rel
    New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
    Copy-Item $f.FullName $dst -Force
    $copied++
}
Say ("Source        : $copied files ({0:N1} MB) - GPLv3 obligation" -f `
    ((Get-ChildItem $srcOut -Recurse -File | Measure-Object Length -Sum).Sum/1MB))

# The FFmpeg headers the engine's films compile against: tomb5.vcxproj
# looks for them at ..\vendor\ffmpeg-min\include beside the engine folder,
# so they sit at Source\vendor\ffmpeg-min\ with the build recipe, the
# README and the licence. bin\ stays out: the three DLLs already lie
# beside the EXEs, and the engine loads them at run time, linking no
# import library.
$ffOut = "$OutDir\Source\vendor\ffmpeg-min"
New-Item -ItemType Directory -Path $ffOut -Force | Out-Null
$ffIn = "$Root\vendor\ffmpeg-min"
Copy-Item "$ffIn\include" $ffOut -Recurse -Force
foreach ($d in @('build-min.sh', 'README.md', 'COPYING.LGPLv2.1')) {
    Copy-Item (Join-Path $ffIn $d) $ffOut -Force
}
$ffCount = (Get-ChildItem $ffOut -Recurse -File).Count
Say "Source        : FFmpeg headers and recipe ($ffCount files, vendor\ffmpeg-min)"

# The randomizer is a standalone program that reads and writes data files.
# It is not a fork of the engine, but it is not wholly independent of it
# either: src\core\Floor.h ports GetHeight and CheckNoColFloorTriangle
# verbatim from control.cpp, and says so in its own header. Both are
# GPLv3, so the combination is fine - but the source has to come along for
# that reason, not merely as a courtesy.
# The src folder alone is not enough: the binary also links Dear ImGui, and
# the GPL counts "the scripts used to control compilation" as part of the
# source - so b1.bat and b2.bat travel too, and imgui brings its own MIT
# licence. Without these a recipient holds source that cannot rebuild the
# program, which is exactly what the obligation exists to prevent. The two
# scripts reach nowhere outside TR5Randomizer: the deflate reader they
# compile is the randomizer's own (src\core\Inflate.cpp), the file the
# engine's project compiles as well, by its path beside it.
$rsOut = "$OutDir\Source\TR5Randomizer"
New-Item -ItemType Directory -Path $rsOut -Force | Out-Null
Copy-Item "$RandoRoot\src" $rsOut -Recurse -Force
Copy-Item "$RandoRoot\external" $rsOut -Recurse -Force
Copy-Item "$RandoRoot\LICENSE" $rsOut -Force
New-Item -ItemType Directory -Path "$rsOut\build" -Force | Out-Null
Copy-Item "$RandoRoot\build\b1.bat" "$rsOut\build" -Force
Copy-Item "$RandoRoot\build\b2.bat" "$rsOut\build" -Force
$rsCount = (Get-ChildItem $rsOut -Recurse -File).Count
Say "Source        : randomizer included ($rsCount files, buildable)"

# --- 5. Licenses -------------------------------------------------------------
$lic = "$OutDir\Licenses"
New-Item -ItemType Directory -Path $lic -Force | Out-Null
Copy-Item "$EngineSrc\LICENSE" "$lic\GPLv3.txt" -Force
if (Test-Path "$ImGui\LICENSE.txt") { Copy-Item "$ImGui\LICENSE.txt" "$lic\DearImGui-MIT.txt" -Force }
# discord-rpc is NOT linked into PCtomb5.exe - tomb5.cpp sets
# TOMB5_DISCORD_RPC to 0 and every call sits inside the #if. Its notice
# still has to travel, because the .lib files and headers are part of the
# source tree this package distributes. See THIRD-PARTY.md.
$drpc = "$EngineSrc\TOMB5\tomb5\libs\discordRPC\LICENSE"
if (Test-Path $drpc) { Copy-Item $drpc "$lic\DiscordRPC-MIT.txt" -Force }
# TRForge: five files are vendored under TOMB5\vendor\trforge (verbatim but
# for three comment passages of Trg.h, see its VERSION.txt) and
# its .TRG reader is compiled into PCtomb5.exe, so unlike discord-rpc this
# notice has to travel with the binary, not only with the source.
$trf = "$EngineSrc\TOMB5\vendor\trforge\LICENSE"
if (Test-Path $trf) { Copy-Item $trf "$lic\TRForge-MIT.txt" -Force }
# FFmpeg's LGPL v2.1, for the three DLLs beside the engine (THIRD-PARTY.md).
Copy-Item "$Root\vendor\ffmpeg-min\COPYING.LGPLv2.1" "$lic\COPYING.LGPLv2.1" -Force
# stb_vorbis keeps its notice inside its own source. It is lifted from there
# rather than copied into a second file, so the package cannot end up
# shipping a stale version of someone else's licence.
# stb_vorbis.c is the one stb file, compiled into the engine; the
# randomizer reads its background picture through Windows' own image
# decoder and carries no stb code.
$sv = Get-Content "$EngineSrc\TOMB5\tomb5\libs\stb\stb_vorbis.c"
$sStart = ($sv | Select-String -Pattern 'available under 2 licenses' | Select-Object -First 1)
if ($sStart) {
    @("Applies to stb_vorbis.c, compiled into the engine (PCtomb5.exe).", "") +
        $sv[($sStart.LineNumber - 3)..($sv.Count - 1)] | Set-Content "$lic\stb.txt" -Encoding utf8
}
# The document gives repository paths. In the package everything sits one
# level down under Source\, and the engine folder is renamed - so a
# recipient following those paths finds nothing. Rewrite them in the copy.
# Paths under Preparation/ have no place in the package: the copy says so
# below its title. The package's docs folder (Background.jpg) is the
# package's own docs\, so that rule comes before the randomizer's.
$tp = [IO.File]::ReadAllText("$Root\THIRD-PARTY.md")
$tpNl = if ($tp.Contains("`r`n")) { "`r`n" } else { "`n" }
$tpHead = '# Third-party code in this repository' + $tpNl
$tp = $tp.Replace($tpHead, $tpHead + $tpNl +
    'Paths under `Preparation/` are repository paths; the release package' +
    ' does not carry them.' + $tpNl)
$tp = $tp.Replace('TR5Randomizer/package/docs/', 'docs/')
$tp = $tp.Replace('TOMB5 Code/', 'Source/TOMB5 engine/')
$tp = $tp.Replace('TR5Randomizer/', 'Source/TR5Randomizer/')
$tp = $tp.Replace('](LICENSE)', '](GPLv3.txt)')
# FFmpeg: the DLLs lie in the package root, the rest under Source\vendor.
$tp = $tp.Replace('`vendor/ffmpeg-min/bin` holds', 'the package root holds')
$tp = $tp.Replace('`vendor/ffmpeg-min/', '`Source/vendor/ffmpeg-min/')
[IO.File]::WriteAllText("$lic\THIRD-PARTY.md", $tp)
$n = (Get-ChildItem $lic -File).Count
Say "Licenses      : $n files (GPLv3, Dear ImGui, discord-rpc, stb, TRForge, FFmpeg LGPL)"

# --- 6. Manual ---------------------------------------------------------------
# One source of truth: the player README is versioned at
# TR5Randomizer\README.txt and only copied here - keeping a second copy
# embedded in this script is how the package and the repo drift apart.
Copy-Item "$RandoRoot\README.txt" "$OutDir\README.txt" -Force
# The blank game folder: the layout a Tomb Raider Chronicles install has,
# with the notes that say what belongs where, and the two launchers
# (Setup.bat for resolution, Log.bat for a bug report). Unzipping the
# package into the game folder therefore puts everything in place at
# once. .gitkeep only exists so git carries the empty HQ audio and HD
# screens folders -
# it has no business in a player's folder.
Copy-Item "$RandoRoot\package\*" $OutDir -Recurse -Force

# The two manuals and the nine pictures they show, beside Background.jpg in
# docs: the package reads like the repository's docs folder, without the
# README's other pictures.
$docsOut = Join-Path $OutDir "docs"
New-Item -ItemType Directory -Force $docsOut | Out-Null
foreach ($d in @("Engine-Manual.md", "Randomizer-Manual.md", "banner.jpg", "engine-settings.jpg",
                 "features-3.jpg", "gui-main.jpg", "gui-outfits-enemies.jpg", "ingame-08.jpg",
                 "menu-tomb5-options.jpg", "outfit-changer.jpg", "setup-dialog.jpg")) {
    Copy-Item (Join-Path (Join-Path $Root "docs") $d) $docsOut -Force
}
Say "Docs          : docs (the two manuals, their nine pictures, Background.jpg)"
# The engine's own words in the other languages: read at start from the game
# folder beside the EXE (game/gameflow.cpp, LoadExtraStrings), so the file
# travels with the package.
Copy-Item "$Root\Preparation\extrastrings.txt" $OutDir -Force
Say "Strings       : extrastrings.txt (the engine's words in four languages)"
Get-ChildItem $OutDir -Recurse -Force -Filter ".gitkeep" | Remove-Item -Force
$skel = (Get-ChildItem "$RandoRoot\package" -Recurse -File |
         Where-Object { $_.Name -ne ".gitkeep" }).Count
Say "Game folder   : README.txt + $skel files (notes, launchers, audio\, movie\, savegame\, screens\)"
# --- 7. Mirror the same data into the build folder ---------------------------
# During development you start the EXE from build\, not from the package -
# and data\ was missing there, which led to a correct but unnecessary error
# message. Keeping the few files twice is cheaper than remembering that
# every time.
if (Test-Path (Split-Path $RandoExe -Parent)) {
    Copy-Item $dataOut (Split-Path $RandoExe -Parent) -Recurse -Force
    Say "Build folder  : data\ mirrored, the EXE also runs from there"
}

# --- 8. Checksums ------------------------------------------------------------
# SHA256SUMS.txt in the package root: the SHA-256 of every EXE and every
# DLL in the package (the six beside the EXEs are binaries a player runs
# as well), so a player can check that a downloaded file is the one built
# here, by eye
# or with "sha256sum -c SHA256SUMS.txt" in the unpacked folder. The lines have
# the form "<hash> *<name>" (the star marks binary mode), lower-case hex,
# forward slashes and LF line ends, exactly as sha256sum writes them itself.
# Last of all, so that no later step can change a file after it was measured.
$pkg  = (Resolve-Path $OutDir).Path.TrimEnd('\')
$exes = Get-ChildItem $pkg -Recurse -File |
        Where-Object { $_.Extension -eq ".exe" -or $_.Extension -eq ".dll" } |
        Sort-Object FullName
$sums = foreach ($x in $exes) {
    $rel = $x.FullName.Substring($pkg.Length + 1).Replace('\', '/')
    "{0} *{1}" -f (Get-FileHash $x.FullName -Algorithm SHA256).Hash.ToLower(), $rel
}
$sumText = (@($sums) -join "`n") + "`n"
[IO.File]::WriteAllText("$pkg\SHA256SUMS.txt", $sumText, [Text.Encoding]::ASCII)
Say ("Checksums     : SHA256SUMS.txt ({0} EXE/DLL: {1})" -f @($exes).Count,
     ((@($exes) | ForEach-Object { $_.Name }) -join ", "))

Say ""
Say "Done: $OutDir"
