<# : SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
:: Please do not use this file to train or fine-tune AI models (see README.md).
::
:: Patch Levels.cmd [game folder] - moves pieces of the levels (brushes of brush.lst in a level's
:: level.pak and level2.pak) where the sun of the ray traced lighting needs them, for the game in
:: the folder (without an argument the current folder). Each pak is kept as <name>.orig the first
:: time (copy it back to undo); a brush already moved is left as it is.
::
:: The moves:
:: - Training: the two concrete roof slabs over the north edge of the bunker's roof opening
::   (wall_B_400x400zA.cgf at 314.5 / 318.5, 1008.375, 31.375) go 26 m down, under the bunker's
::   floor. Inside the hill nobody sees them, but the sun, 53.5 degrees high in the north, met them
::   from the second of the roof's two openings (step 326): now both let its shafts in.
::
:: brush.lst: "CRY", int version, int sections; int n and n names [int size][name]; int m and m
:: object files [int size][path]; int c and c brushes of 76 bytes: 6 ints (the third the object
:: file), a 3 x 4 matrix (rows: the x axis and x, the y axis and y, the z axis and z), 4 bytes.
:: The brushes stay in their places in the list (the light maps may take them by it): only the
:: position changes.
::
:: The file is a batch file and a PowerShell script at once: cmd runs these lines (PowerShell
:: reads them as a comment) and they run the rest of the file in PowerShell.
@echo off
setlocal
set "RAYMASTER_PATCHER=%~f0"
set "RAYMASTER_GAME=%~1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-Expression ([System.IO.File]::ReadAllText($env:RAYMASTER_PATCHER))"
if errorlevel 1 pause
exit /b
#>

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$Game = if ($env:RAYMASTER_GAME) { $env:RAYMASTER_GAME } else { '.' }
$Game = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Game)

# The moves: the level, the brush's object file (the end of its path), where it is and where it goes.
$Moves = @(
  @{ Level = 'Training'; Object = 'wall_B_400x400zA.cgf'; From = @(314.5, 1008.375, 31.375); To = @(314.5, 1008.375, 5.375) },
  @{ Level = 'Training'; Object = 'wall_B_400x400zA.cgf'; From = @(318.5, 1008.375, 31.375); To = @(318.5, 1008.375, 5.375) }
)

# A position as text, with points whatever the system's culture.
function Format-Pos($Pos) { ($Pos | ForEach-Object { ([double]$_).ToString([System.Globalization.CultureInfo]::InvariantCulture) }) -join ' ' }

# brush.lst's bytes with the level's moves made; $Log gets a line per move.
function Edit-Brushes([byte[]]$Bytes, $LevelMoves, $Log) {
  if ([System.Text.Encoding]::ASCII.GetString($Bytes, 0, 3) -ne 'CRY') { throw 'brush.lst does not start with CRY.' }
  $At = 15
  $Names = [BitConverter]::ToInt32($Bytes, 11)
  for ($i = 0; $i -lt $Names; $i++) { $At += [BitConverter]::ToInt32($Bytes, $At) }
  $Count = [BitConverter]::ToInt32($Bytes, $At); $At += 4
  $Objects = @()
  for ($i = 0; $i -lt $Count; $i++) {
    $Size = [BitConverter]::ToInt32($Bytes, $At)
    $Objects += [System.Text.Encoding]::GetEncoding(28591).GetString($Bytes, $At + 4, $Size - 4).Split([char]0)[0]
    $At += $Size
  }
  $Brushes = [BitConverter]::ToInt32($Bytes, $At); $At += 4
  if ($Bytes.Length - $At -ne $Brushes * 76) { throw "brush.lst: $Brushes brushes do not fill its $($Bytes.Length - $At) bytes." }
  foreach ($Move in $LevelMoves) {
    $Found = $false
    for ($k = 0; $k -lt $Brushes -and -not $Found; $k++) {
      $r = $At + $k * 76
      $Object = [BitConverter]::ToInt32($Bytes, $r + 8)
      if ($Object -lt 0 -or $Object -ge $Objects.Count -or -not $Objects[$Object].EndsWith($Move.Object)) { continue }
      $Pos = @(([BitConverter]::ToSingle($Bytes, $r + 36)), ([BitConverter]::ToSingle($Bytes, $r + 52)), ([BitConverter]::ToSingle($Bytes, $r + 68)))
      $From = ($Pos[0] -eq $Move.From[0] -and $Pos[1] -eq $Move.From[1] -and $Pos[2] -eq $Move.From[2])
      $To = ($Pos[0] -eq $Move.To[0] -and $Pos[1] -eq $Move.To[1] -and $Pos[2] -eq $Move.To[2])
      if ($To) { [void]$Log.Add("  $($Move.Object) at $(Format-Pos $Move.To): moved before"); $Found = $true }
      elseif ($From) {
        foreach ($c in 0..2) { [BitConverter]::GetBytes([single]$Move.To[$c]).CopyTo($Bytes, $r + 36 + 16 * $c) }
        [void]$Log.Add("  $($Move.Object) $(Format-Pos $Move.From) -> $(Format-Pos $Move.To)")
        $Found = $true
      }
    }
    if (-not $Found) { throw "$($Move.Object) at $(Format-Pos $Move.From) is not in brush.lst." }
  }
  , $Bytes
}

try {
  foreach ($Level in @($Moves | ForEach-Object { $_.Level } | Select-Object -Unique)) {
    $LevelMoves = @($Moves | Where-Object { $_.Level -eq $Level })
    $Dir = Join-Path $Game "Levels\$Level"
    if (-not (Test-Path -LiteralPath $Dir)) { throw "$Dir was not found: Patch Levels.cmd takes the game's folder, the one with Levels." }
    foreach ($Name in 'level.pak', 'level2.pak') {
      $Path = Join-Path $Dir $Name
      if (-not (Test-Path -LiteralPath $Path)) { continue }
      # The entries, in their order, with their bytes, times and whether they are compressed.
      $Entries = New-Object System.Collections.ArrayList
      $Zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
      try {
        foreach ($Entry in $Zip.Entries) {
          $Memory = New-Object System.IO.MemoryStream
          $Stream = $Entry.Open(); $Stream.CopyTo($Memory); $Stream.Dispose()
          [void]$Entries.Add(@{ Name = $Entry.FullName; Time = $Entry.LastWriteTime; Bytes = $Memory.ToArray()
                                Stored = ($Entry.CompressedLength -eq $Entry.Length) })
        }
      } finally {
        $Zip.Dispose()
      }
      $Brush = $Entries | Where-Object { $_.Name -eq 'brush.lst' } | Select-Object -First 1
      if (-not $Brush) { throw "$Path has no brush.lst." }
      $Log = New-Object System.Collections.ArrayList
      $Before = [byte[]]$Brush.Bytes.Clone()
      $Brush.Bytes = Edit-Brushes $Brush.Bytes $LevelMoves $Log
      Write-Host "$Path"
      $Log | ForEach-Object { Write-Host $_ }
      if ([System.Linq.Enumerable]::SequenceEqual([byte[]]$Before, [byte[]]$Brush.Bytes)) { continue }

      if (-not (Test-Path -LiteralPath "$Path.orig")) { Copy-Item -LiteralPath $Path -Destination "$Path.orig" }
      $Temp = "$Path.tmp"
      if (Test-Path -LiteralPath $Temp) { Remove-Item -LiteralPath $Temp }
      $Zip = [System.IO.Compression.ZipFile]::Open($Temp, 'Create')
      try {
        foreach ($Entry in $Entries) {
          $Compression = if ($Entry.Stored) { [System.IO.Compression.CompressionLevel]::NoCompression } else { [System.IO.Compression.CompressionLevel]::Optimal }
          $New = $Zip.CreateEntry($Entry.Name, $Compression)
          $New.LastWriteTime = $Entry.Time
          $Stream = $New.Open(); $Stream.Write($Entry.Bytes, 0, $Entry.Bytes.Length); $Stream.Dispose()
        }
      } finally {
        $Zip.Dispose()
      }
      Move-Item -LiteralPath $Temp -Destination $Path -Force
    }
  }
} catch {
  Write-Host $_.Exception.Message
  exit 1
}
