#----------------------------------------------------------------------------
# Origin check for the preparation tools.
#
# A level info exported from an already randomized level looks exactly like
# a genuine one - same fields, same structure, plausible numbers - so a tool
# reading it would quietly keep computing and deliver wrong zones without
# anything crashing.
#
# TombItemEdit2026 stamps every level info with the file name, SHA-256 and
# size of the .trc it came from, which lets this check compare it against
# VanillaLevelChecksums.csv to confirm it is an untouched original file.
#
# It aborts loudly rather than warning: a wrong result that still looks
# plausible is the worst kind of error, so it is better to deliver nothing
# at all - including when a level info carries no stamp (an older tool made
# it) or the stamp names a file the checksum list does not know, since
# "unknown origin" is not the same as "confirmed vanilla".
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
