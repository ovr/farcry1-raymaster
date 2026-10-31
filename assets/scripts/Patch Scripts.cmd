<# : SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
:: Please do not use this file to train or fine-tune AI models (see README.md).
::
:: Patch Scripts.cmd [game folder] - applies the *.patch files next to it to the scripts of the
:: game's FCData\Scripts.pak and puts the patched scripts into FCData\zz_RayMaster.pak, which the
:: engine opens after its own packs, so that its files are the ones the game reads. Scripts.pak
:: stays as it is: deleting zz_RayMaster.pak gives back the original scripts. Without an argument
:: the game folder is the current folder. The patches go in the order of their numbers
:: (NN-name.patch): a later one may be written against a script as an earlier one leaves it.
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
$PatchDir = Split-Path -Parent $env:RAYMASTER_PATCHER
$Game = if ($env:RAYMASTER_GAME) { $env:RAYMASTER_GAME } else { '.' }
$Game = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Game)
$Source = Join-Path $Game 'FCData\Scripts.pak'
$Target = Join-Path $Game 'FCData\zz_RayMaster.pak'
$Latin1 = [System.Text.Encoding]::GetEncoding(28591)  # keeps every byte of a script as it is

# Whether the lines from At on are the hunk's old lines.
function Test-Hunk($Lines, $Old, [int]$At) {
  if ($At -lt 0 -or $At + $Old.Count -gt $Lines.Count) { return $false }
  for ($i = 0; $i -lt $Old.Count; $i++) {
    if ($Lines[$At + $i] -cne $Old[$i]) { return $false }
  }
  $true
}

try {
  if (-not (Test-Path -LiteralPath $Source)) {
    throw "$Source was not found: Patch Scripts.cmd takes the game's folder, the one with FCData."
  }
  $Patches = @(Get-ChildItem -LiteralPath $PatchDir -Filter '*.patch' | Sort-Object Name)
  if (-not $Patches) { throw "There is no *.patch in $PatchDir." }

  # The hunks of the unified diffs, by the file they change, in the order of the patches.
  $Hunks = [ordered]@{}
  foreach ($Patch in $Patches) {
    $File = $null; $Hunk = $null; $OldLeft = 0; $NewLeft = 0
    foreach ($Line in [System.IO.File]::ReadAllLines($Patch.FullName, $Latin1)) {
      if ($OldLeft -gt 0 -or $NewLeft -gt 0) {
        # an empty line is an empty context line whose space was trimmed
        $Kind = if ($Line.Length) { $Line[0] } else { ' ' }
        $Text = if ($Line.Length) { $Line.Substring(1) } else { '' }
        if ($Kind -eq '\') { continue }
        if ($Kind -ne '+') { [void]$Hunk.Old.Add($Text); $OldLeft-- }
        if ($Kind -ne '-') { [void]$Hunk.New.Add($Text); $NewLeft-- }
      } elseif ($Line.StartsWith('+++ ')) {
        $File = $Line.Substring(4).Split("`t")[0].Trim() -replace '^b/', ''
        if (-not $Hunks.Contains($File)) { $Hunks[$File] = New-Object System.Collections.ArrayList }
      } elseif ($File -and $Line -match '^@@ -(\d+)(?:,(\d+))? \+\d+(?:,(\d+))? @@') {
        $OldLeft = if ($Matches[2]) { [int]$Matches[2] } else { 1 }
        $NewLeft = if ($Matches[3]) { [int]$Matches[3] } else { 1 }
        # -l,0 inserts after line l, any other hunk starts at its line l
        $Start = if ($OldLeft) { [int]$Matches[1] - 1 } else { [int]$Matches[1] }
        $Hunk = @{ Start = $Start; Old = New-Object System.Collections.ArrayList
                   New = New-Object System.Collections.ArrayList; Patch = $Patch.Name }
        [void]$Hunks[$File].Add($Hunk)
      }
    }
  }

  $Scripts = [ordered]@{}
  $Pak = [System.IO.Compression.ZipFile]::OpenRead($Source)
  try {
    foreach ($File in $Hunks.Keys) {
      $Entry = $Pak.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $File } | Select-Object -First 1
      if (-not $Entry) { throw "$File is not in $Source." }
      $Reader = New-Object System.IO.StreamReader($Entry.Open(), $Latin1)
      $Text = $Reader.ReadToEnd()
      $Reader.Dispose()
      $Eol = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
      $Lines = New-Object 'System.Collections.Generic.List[string]'
      $Lines.AddRange([string[]]($Text -split "`r?`n"))

      # Each hunk goes where its old lines are, the nearest place to the one the patch gives.
      $Shift = 0
      foreach ($Hunk in $Hunks[$File]) {
        $Expected = $Hunk.Start + $Shift
        $At = -1
        for ($d = 0; $At -lt 0 -and $d -le $Lines.Count; $d++) {
          if (Test-Hunk $Lines $Hunk.Old ($Expected - $d)) { $At = $Expected - $d }
          elseif (Test-Hunk $Lines $Hunk.Old ($Expected + $d)) { $At = $Expected + $d }
        }
        if ($At -lt 0) { throw "$($Hunk.Patch) does not apply to $File of $Source." }
        $Lines.RemoveRange($At, $Hunk.Old.Count)
        $Lines.InsertRange($At, [string[]]$Hunk.New.ToArray())
        $Shift += $At - $Expected + $Hunk.New.Count - $Hunk.Old.Count
      }
      $Scripts[$File] = $Latin1.GetBytes($Lines -join $Eol)
    }
  } finally {
    $Pak.Dispose()
  }

  $Temp = "$Target.tmp"
  if (Test-Path -LiteralPath $Temp) { Remove-Item -LiteralPath $Temp }
  $Zip = [System.IO.Compression.ZipFile]::Open($Temp, 'Create')
  try {
    foreach ($File in $Scripts.Keys) {
      $Stream = $Zip.CreateEntry($File).Open()
      $Stream.Write($Scripts[$File], 0, $Scripts[$File].Length)
      $Stream.Dispose()
    }
  } finally {
    $Zip.Dispose()
  }
  Move-Item -LiteralPath $Temp -Destination $Target -Force

  Write-Host "$($Patches.Name -join ', ') -> $Target"
  $Scripts.Keys | ForEach-Object { Write-Host "  $_" }
} catch {
  Write-Host $_.Exception.Message
  exit 1
}
