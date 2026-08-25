# Assembles the shippable folder: randomizer, engine, bundled source code
# and the licenses.
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
# What DELIBERATELY does NOT go in here: game data. Neither levels nor
# music, videos or language files. Tomb Raider Chronicles belongs to its
# rights holders; the user brings their own installation, and the
# randomizer copies what it needs on first start by itself.
param(
    [string]$OutDir = "",
    [switch]$Clean
)
$ErrorActionPreference = "Stop"

# Everything relative to the script location (Tools -> two levels up = repo
# root), so the script runs on any machine and carries no developer paths
# (#72).
$Root      = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ($OutDir -eq "") { $OutDir = Join-Path $Root "Release Paket" }
$EngineSrc = "$Root\TOMB5 Code"
$EngineExe = "$Root\release\PCtomb5.exe"
$RandoRoot = "$Root\TR5Randomizer"
$RandoExe  = "$RandoRoot\build\TR5Randomizer.exe"
# The MIT license comes from the VERSIONED imgui (external\imgui) - the
# loose download folder under "Tomb5 Rando vorbereitung" is untracked and
# does not exist in a fresh clone at all.
$ImGui     = "$Root\TR5Randomizer\external\imgui"
$ZoneTools = "$Root\Tomb5 Rando vorbereitung\Tools"
$LevelInfo = "$Root\Tomb5 Rando vorbereitung\Level Infos"

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
$dataOut = "$OutDir\data"
New-Item -ItemType Directory -Path $dataOut -Force | Out-Null
Copy-Item "$LevelInfo\VanillaLevelChecksums.csv" $dataOut -Force
$zones = 0
foreach ($f in (Get-ChildItem "$LevelInfo\*.json" | Sort-Object Name)) {
    if ($f.BaseName -eq 'Title') { continue }
    & "$ZoneTools\ZoneAnalyse.ps1" -LevelJson $f.FullName -LevelName $f.BaseName `
        -ExportZonesJson "$dataOut\zones_$($f.BaseName).json" *> $null
    Copy-Item $f.FullName "$dataOut\level_$($f.Name)" -Force
    $zones++
}
Say "Data          : $zones zone exports + level infos + checksum list"

# --- 4. Source code, GPLv3 obligation ----------------------------------------
$srcOut = "$OutDir\Source\TOMB5"
New-Item -ItemType Directory -Path $srcOut -Force | Out-Null
$skipDirs = '\\(Build|Release|Debug|x64|\.vs|ipch)\\'
$skipExt  = @('.obj','.pdb','.exe','.dll','.ipdb','.iobj','.tlog','.log','.user','.aps')
$copied = 0
foreach ($f in (Get-ChildItem $EngineSrc -Recurse -File)) {
    if ($f.FullName -match $skipDirs) { continue }
    if ($skipExt -contains $f.Extension.ToLower()) { continue }
    $rel = $f.FullName.Substring($EngineSrc.Length).TrimStart('\')
    $dst = Join-Path $srcOut $rel
    New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
    Copy-Item $f.FullName $dst -Force
    $copied++
}
Say ("Source        : $copied files ({0:N1} MB) - GPLv3 obligation" -f `
    ((Get-ChildItem $srcOut -Recurse -File | Measure-Object Length -Sum).Sum/1MB))

# The randomizer is a standalone program that only reads and writes data
# files - no derivative work of the engine. Its source still comes along,
# because that is the standard we hold ourselves to anyway.
$rsOut = "$OutDir\Source\TR5Randomizer"
New-Item -ItemType Directory -Path $rsOut -Force | Out-Null
Copy-Item "$RandoRoot\src" $rsOut -Recurse -Force
Say "Source        : randomizer included"

# --- 5. Licenses -------------------------------------------------------------
$lic = "$OutDir\Licenses"
New-Item -ItemType Directory -Path $lic -Force | Out-Null
Copy-Item "$EngineSrc\LICENSE" "$lic\TOMB5-GPLv3.txt" -Force
if (Test-Path "$ImGui\LICENSE.txt") { Copy-Item "$ImGui\LICENSE.txt" "$lic\DearImGui-MIT.txt" -Force }
Say "Licenses      : GPLv3 (engine), MIT (Dear ImGui)"

# --- 6. Manual ---------------------------------------------------------------
# One source of truth: the player README is versioned at
# TR5Randomizer\README.txt and only copied here - keeping a second copy
# embedded in this script is how the package and the repo drift apart.
Copy-Item "$RandoRoot\README.txt" "$OutDir\README.txt" -Force
Say "Manual        : README.txt"
# --- 7. Mirror the same data into the build folder ---------------------------
# During development you start the EXE from build\, not from the package -
# and data\ was missing there, which led to a correct but unnecessary error
# message. Keeping the few files twice is cheaper than remembering that
# every time.
if (Test-Path (Split-Path $RandoExe -Parent)) {
    Copy-Item $dataOut (Split-Path $RandoExe -Parent) -Recurse -Force
    Say "Build folder  : data\ mirrored, the EXE also runs from there"
}

Say ""
Say "Done: $OutDir"
