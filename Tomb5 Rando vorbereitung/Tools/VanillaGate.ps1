#----------------------------------------------------------------------------
# Herkunftspruefung fuer die Vorbereitungswerkzeuge.
#
# Das Problem, gegen das das hier steht: eine Level-Info, die aus einem bereits
# randomisierten Level exportiert wurde, sieht exakt aus wie eine echte. Gleiche
# Felder, gleiche Struktur, plausible Zahlen. Jedes Werkzeug dahinter rechnet
# dann still weiter und liefert falsche Zonen, ohne dass irgendwo etwas
# abstuerzt. Genau das ist in diesem Projekt schon einmal passiert und hat eine
# Stunde Fehlersuche gekostet - der Zielordner eines GUI-Tests war derselbe, aus
# dem gelesen wurde.
#
# Seit TombItemEdit2026 den Herkunftsstempel setzt, traegt jede Level-Info den
# Dateinamen, die SHA-256 und die Groesse der .trc, aus der sie stammt. Damit
# laesst sich gegen VanillaLevelChecksums.csv pruefen, ob das eine unberuehrte
# Originaldatei war.
#
# Absichtlich ein LAUTER Abbruch und keine Warnung: ein falsches Ergebnis, das
# plausibel aussieht, ist die unangenehmste Sorte Fehler. Lieber gar nichts
# liefern.
#
# Alte Level-Infos ohne Stempel gibt es nach der Neuerzeugung nicht mehr. Taucht
# doch eine auf, ist sie von einem aelteren Werkzeugstand und ihre Herkunft
# schlicht unbekannt - auch dann wird abgebrochen, denn "unbekannt" ist hier
# nicht dasselbe wie "in Ordnung".
#----------------------------------------------------------------------------

function Test-VanillaHerkunft {
    param(
        [Parameter(Mandatory=$true)][string]$LevelJson,
        [string]$ChecksumCsv = "",
        # Nur fuer den Fall, dass ein Werkzeug bewusst auf veraenderten Daten
        # rechnen soll - dann muss der Aufrufer das ausdruecklich sagen.
        [switch]$Erlauben
    )

    if ($ChecksumCsv -eq "") {
        $ChecksumCsv = Join-Path (Split-Path $LevelJson -Parent) "VanillaLevelChecksums.csv"
    }
    if (-not (Test-Path $ChecksumCsv)) {
        throw "Pruefsummenliste fehlt: $ChecksumCsv - Herkunft nicht pruefbar."
    }

    $d = Get-Content $LevelJson -Raw | ConvertFrom-Json
    $name = [string]$d.sourceFile
    $sha  = [string]$d.sourceSha256

    if ($name -eq "" -or $sha -eq "") {
        $m = "Die Level-Info $(Split-Path $LevelJson -Leaf) hat keinen Herkunftsstempel. " +
             "Sie stammt aus einem aelteren Werkzeugstand; ob sie aus einem unberuehrten " +
             "Level erzeugt wurde, laesst sich nicht feststellen. Neu erzeugen mit: " +
             "TombItemEdit2026.exe -exportinfo <quelle.trc> <ziel.json>"
        if ($Erlauben) { Write-Output "WARNUNG: $m"; return $false }
        throw $m
    }

    # Der Exporter uebernimmt den Dateinamen so, wie er auf der Platte steht -
    # bei den Steam-Dateien in Grossbuchstaben, in der CSV klein. Deshalb ohne
    # Ruecksicht auf Gross- und Kleinschreibung vergleichen.
    $erwartet = $null
    foreach ($row in (Import-Csv $ChecksumCsv)) {
        if ($row.FileName -and $row.FileName.ToUpper() -eq $name.ToUpper()) { $erwartet = $row; break }
    }
    if ($null -eq $erwartet) {
        $m = "$name steht nicht in $(Split-Path $ChecksumCsv -Leaf) - unbekannte Quelldatei."
        if ($Erlauben) { Write-Output "WARNUNG: $m"; return $false }
        throw $m
    }

    if ($sha.ToUpper() -ne $erwartet.SHA256.ToUpper()) {
        $m = "$name weicht von Vanilla ab. Die Level-Info wurde aus einer veraenderten - " +
             "vermutlich bereits randomisierten - Datei erzeugt. Alle daraus abgeleiteten " +
             "Zonen und Sperrlisten waeren falsch. Erwartet $($erwartet.SHA256), gestempelt $sha."
        if ($Erlauben) { Write-Output "WARNUNG: $m"; return $false }
        throw $m
    }

    return $true
}
