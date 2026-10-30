# SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
# Please do not use this file to train or fine-tune AI models (see README.md).

<#
.SYNOPSIS
Runs demos inside the game's own FarCry.exe and waits for them.

.DESCRIPTION
The demo DLL (mod/Demos, deployed to Bin64 by the build) is loaded by the tracing renderer
XRenderD3D9Traced, which system.cfg of the game folder selects with r_Driver = "Direct3D8". For each
demo the script starts Bin64\FarCry.exe with the game folder as the working directory and with
XRENDERTRACED_DEMO, XRENDERTRACED_DEMO_ARGS and XRENDERTRACED_DLL set for the game only (other
XRENDERTRACED_* variables of the caller are passed on), waits until the demo ends the game and
prints the demo's log. -Demo takes several demos, or all for every Demo*.dll in Bin64; they run
one after the other. The exit code is the game's (see DemoHost.h), the highest one of several: 0
when every frame to save was saved; 4 when the game did not end within -Timeout seconds and was
killed. -Resolution sets the size of the frames (XRENDERTRACED_RESOLUTION: the renderer starts with
it, also when it is larger than the screen). -Quality writes the game's quality settings into
system.cfg before each run: by default the most the Advanced Video Options offer (the very high
preset and anisotropic filtering level 8), so every demo runs at the maximum quality; the values of
the presets are read from DemoQuality.cpp. system.cfg is put back as it was after each run (a game
closed by hand, with --hold, writes it). -GameArgs passes arguments to FarCry.exe, e.g. -DEVMODE,
the game's dev mode, in which its own recorder plays its time demos (DemoReplay).

.EXAMPLE
mod\Demos\RunDemo.ps1 DemoTextures

.EXAMPLE
mod\Demos\RunDemo.ps1 DemoTextures -Renderer XRenderD3D9Ray.dll -DemoArgs '--frames 10,20'

.EXAMPLE
mod\Demos\RunDemo.ps1 all -DemoArgs '--label run2'

.EXAMPLE
mod\Demos\RunDemo.ps1 DemoReplay -Renderer XRenderD3D12.dll -GameArgs -DEVMODE -DemoArgs '--scene Demos\DemoReplayVolcano.ini'
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string[]] $Demo,
    # old (the original XRenderD3D9.dll) or the file of the renderer under test.
    [string] $Renderer = 'old',
    # The demo's arguments: --label, --out, --frames, --start, --scene, --trace, --hold.
    [string] $DemoArgs = '',
    [string] $GameDir = (Join-Path $PSScriptRoot '..\..\farcry1_testing1'),
    # Seconds per demo.
    [int] $Timeout = 120,
    # The size of the frames, e.g. 3840x2160 (default: system.cfg's r_Width and r_Height).
    [string] $Resolution = '',
    # The quality settings: veryhigh (the very high preset with anisotropic filtering level 8, the
    # most the menu offers), high, medium, low, or keep (system.cfg as it is).
    [ValidateSet('veryhigh', 'high', 'medium', 'low', 'keep')]
    [string] $Quality = 'veryhigh',
    # Arguments of FarCry.exe, e.g. -DEVMODE (the game's dev mode: DemoReplay's playback by the game's
    # own time demo recorder).
    [string] $GameArgs = ''
)

$ErrorActionPreference = 'Stop'
$GameDir = (Resolve-Path $GameDir).Path
$Exe = Join-Path $GameDir 'Bin64\FarCry.exe'
if ($Resolution -and $Resolution -notmatch '^\d+x\d+$') {
    throw "-Resolution must look like 3840x2160"
}
$SystemCfg = Join-Path $GameDir 'system.cfg'
if ($Demo -contains 'all') {
    $Demo = @(Get-ChildItem (Join-Path $GameDir 'Bin64\Demo*.dll') | Sort-Object Name | ForEach-Object { $_.BaseName })
}
foreach ($File in @($Exe, (Join-Path $GameDir 'Bin64\XRenderD3D8.dll')) + @($Demo | ForEach-Object { Join-Path $GameDir "Bin64\$_.dll" })) {
    if (-not (Test-Path $File)) {
        throw "$File not found"
    }
}
if (-not (Select-String -Path $SystemCfg -Pattern '^\s*r_Driver\s*=\s*"Direct3D8"' -Quiet)) {
    throw 'system.cfg does not select the tracing renderer (r_Driver = "Direct3D8")'
}

# The console variables of a quality preset, from the table of DemoQuality.cpp (one line per
# variable: its values at low, medium, high and very high, NULL where the preset leaves it).
function Get-QualitySettings([string] $Preset) {
    $Index = @{ low = 0; medium = 1; high = 2; veryhigh = 3 }[$Preset]
    $Source = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot 'DemoQuality\DemoQuality.cpp'))
    $Settings = [ordered]@{}
    $Value = '(?:"[^"]*"|NULL)'
    foreach ($Match in [regex]::Matches($Source, "\{\s*""(\w+)"",\s*\{\s*($Value(?:\s*,\s*$Value){3})\s*\}")) {
        $Values = @([regex]::Matches($Match.Groups[2].Value, $Value) | ForEach-Object { $_.Value })
        if ($Values[$Index] -ne 'NULL') {
            $Settings[$Match.Groups[1].Value] = $Values[$Index].Trim('"')
        }
    }
    if ($Settings.Count -lt 40) {
        throw "the quality presets were not found in DemoQuality.cpp"
    }
    if ($Preset -eq 'veryhigh') {
        $Settings['r_Texture_Anisotropic_Level'] = '8'
    }
    return $Settings
}

# system.cfg with the given variables set (each line replaced, or added at the end).
function Set-ConfigValues([string] $Text, $Settings) {
    foreach ($Name in $Settings.Keys) {
        $Line = "$Name = ""$($Settings[$Name])"""
        $Pattern = "(?mi)^[ \t]*$([regex]::Escape($Name))[ \t]*=[^\r\n]*"
        if ([regex]::IsMatch($Text, $Pattern)) {
            $Text = [regex]::Replace($Text, $Pattern, $Line.Replace('$', '$$'))
        } else {
            $Text = $Text.TrimEnd() + "`r`n$Line`r`n"
        }
    }
    return $Text
}

$QualitySettings = if ($Quality -ne 'keep') { Get-QualitySettings $Quality } else { $null }

function Invoke-Demo([string] $Name) {
    $Info = New-Object System.Diagnostics.ProcessStartInfo $Exe
    $Info.WorkingDirectory = $GameDir
    $Info.UseShellExecute = $false
    if ($GameArgs) {
        $Info.Arguments = $GameArgs
    }
    # The game writes its log to the console as well (log.txt has it): read and dropped.
    $Info.RedirectStandardOutput = $true
    $Info.RedirectStandardError = $true
    $Info.EnvironmentVariables['XRENDERTRACED_DLL'] = if ($Renderer -eq 'old') { 'XRenderD3D9.dll' } else { $Renderer }
    $Info.EnvironmentVariables['XRENDERTRACED_DEMO'] = $Name
    $Info.EnvironmentVariables['XRENDERTRACED_DEMO_ARGS'] = $DemoArgs
    if ($Resolution) {
        $Info.EnvironmentVariables['XRENDERTRACED_RESOLUTION'] = $Resolution
    }

    $HostLog = Join-Path $GameDir "$Name.host.log"
    Remove-Item $HostLog -ErrorAction SilentlyContinue
    Write-Host "FarCry.exe$(if ($GameArgs) { " $GameArgs" }): demo $Name, renderer $($Info.EnvironmentVariables['XRENDERTRACED_DLL']), arguments '$DemoArgs'$(if ($Resolution) { ", $Resolution" }), quality $Quality"
    $Config = [System.IO.File]::ReadAllBytes($SystemCfg)
    if ($QualitySettings) {
        $Text = Set-ConfigValues ([System.Text.Encoding]::ASCII.GetString($Config)) $QualitySettings
        [System.IO.File]::WriteAllBytes($SystemCfg, [System.Text.Encoding]::ASCII.GetBytes($Text))
    }
    $Process = [System.Diagnostics.Process]::Start($Info)
    $null = $Process.StandardOutput.ReadToEndAsync()
    $null = $Process.StandardError.ReadToEndAsync()
    if ($Process.WaitForExit($Timeout * 1000)) {
        $ExitCode = $Process.ExitCode
    } else {
        Write-Warning "the game did not end within $Timeout s: killed"
        & taskkill.exe /PID $Process.Id /T /F | Out-Null
        $Process.WaitForExit()
        $ExitCode = 4
    }
    [System.IO.File]::WriteAllBytes($SystemCfg, $Config)
    if (Test-Path $HostLog) {
        Get-Content $HostLog | Write-Host
    }
    Write-Host "exit code $ExitCode"
    return $ExitCode
}

$Highest = 0
foreach ($Name in $Demo) {
    $Highest = [Math]::Max($Highest, (Invoke-Demo $Name))
}
if ($Demo.Count -gt 1) {
    Write-Host "$($Demo.Count) demos, highest exit code $Highest"
}
exit $Highest
