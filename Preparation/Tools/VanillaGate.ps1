#----------------------------------------------------------------------------
# Origin check for the preparation tools.
#
# The problem this stands against: a level info exported from an already
# randomized level looks exactly like a genuine one. Same fields, same
# structure, plausible numbers. Every tool behind it then quietly keeps
# computing and delivers wrong zones without anything crashing anywhere.
# Exactly that has happened once in this project and cost an hour of bug
# hunting - the target folder of a GUI test was the same one being read
# from.
#
# Since TombItemEdit2026 sets the origin stamp, every level info carries the
# file name, the SHA-256 and the size of the .trc it came from. With that it
# can be checked against VanillaLevelChecksums.csv whether it was an
# untouched original file.
#
# Deliberately a LOUD abort and no warning: a wrong result that looks
# plausible is the most unpleasant kind of error. Better to deliver nothing
# at all.
#
# Old level infos without a stamp no longer exist after the regeneration.
# Should one turn up anyway, it is from an older tool state and its origin
# simply unknown - then it aborts too, because "unknown" is not the same as
# "fine" here.
#----------------------------------------------------------------------------

function Test-VanillaOrigin {
    param(
        [Parameter(Mandatory=$true)][string]$LevelJson,
        [string]$ChecksumCsv = "",
        # Only for the case that a tool is meant to compute on changed data
        # deliberately - then the caller must say so explicitly.
        [switch]$Allow
    )

    if ($ChecksumCsv -eq "") {
        $ChecksumCsv = Join-Path (Split-Path $LevelJson -Parent) "VanillaLevelChecksums.csv"
    }
    if (-not (Test-Path $ChecksumCsv)) {
        throw "checksum list missing: $ChecksumCsv - origin not checkable."
    }

    $d = Get-Content $LevelJson -Raw | ConvertFrom-Json
    $name = [string]$d.sourceFile
    $sha  = [string]$d.sourceSha256

    if ($name -eq "" -or $sha -eq "") {
        $m = "The level info $(Split-Path $LevelJson -Leaf) has no origin stamp. " +
             "It comes from an older tool state; whether it was generated from an " +
             "untouched level cannot be determined. Regenerate with: " +
             "TombItemEdit2026.exe -exportinfo <source.trc> <target.json>"
        if ($Allow) { Write-Output "WARNING: $m"; return $false }
        throw $m
    }

    # The exporter takes the file name as it stands on disk - upper case for
    # the Steam files, lower case in the CSV. Hence compare without regard
    # to case.
    $expected = $null
    foreach ($row in (Import-Csv $ChecksumCsv)) {
        if ($row.FileName -and $row.FileName.ToUpper() -eq $name.ToUpper()) { $expected = $row; break }
    }
    if ($null -eq $expected) {
        $m = "$name does not stand in $(Split-Path $ChecksumCsv -Leaf) - unknown source file."
        if ($Allow) { Write-Output "WARNING: $m"; return $false }
        throw $m
    }

    if ($sha.ToUpper() -ne $expected.SHA256.ToUpper()) {
        $m = "$name deviates from vanilla. The level info was generated from a changed - " +
             "presumably already randomized - file. All zones and bar lists derived from " +
             "it would be wrong. Expected $($expected.SHA256), stamped $sha."
        if ($Allow) { Write-Output "WARNING: $m"; return $false }
        throw $m
    }

    return $true
}
