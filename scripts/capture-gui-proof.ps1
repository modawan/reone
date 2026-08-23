# Copyright (c) 2026 The reone project contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# Captures the GUI proof matrix as PNGs. See doc/gui-capture-proof.md.
param(
    [Parameter(Mandatory = $true)][string]$Kotor1Dir,
    [Parameter(Mandatory = $true)][string]$Kotor2Dir,
    [string]$OutputDir = "",
    [string[]]$States = @(),
    [int[]]$Widths = @(),
    [switch]$NoWorld,
    [switch]$VerifyReproducibility
)

$ErrorActionPreference = "Stop"

$repoDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$enginePath = Join-Path $repoDir "build\bin\engine.exe"
$shaderPackPath = Join-Path $repoDir "build\bin\shaderpack.erf"
foreach ($required in @($enginePath, $shaderPackPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Release build artifact not found: $required"
    }
}
foreach ($gameDir in @($Kotor1Dir, $Kotor2Dir)) {
    if (-not (Test-Path -LiteralPath $gameDir -PathType Container)) {
        throw "Game directory not found: $gameDir"
    }
}
$ffmpeg = (Get-Command ffmpeg -ErrorAction Stop).Source

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $repoDir "build\gui-proof"
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$runDir = Join-Path $OutputDir ".runtime"
Remove-Item -LiteralPath $runDir -Recurse -Force -ErrorAction Ignore
New-Item -ItemType Directory -Force -Path $OutputDir, $runDir | Out-Null
Copy-Item -LiteralPath $shaderPackPath -Destination (Join-Path $runDir "shaderpack.erf") -Force

if ($VerifyReproducibility) {
    $baselineDir = Join-Path $runDir "reproducibility-baseline"
    New-Item -ItemType Directory -Force -Path $baselineDir | Out-Null

    if (-not ("Reone.GuiCaptureProof.PixelComparer" -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.IO;

namespace Reone.GuiCaptureProof
{
    public sealed class PixelDifference
    {
        public bool Identical { get; set; }
        public int MaxChannelDelta { get; set; }
        public long DifferentChannels { get; set; }
        public long DifferentPixels { get; set; }
    }

    public static class PixelComparer
    {
        public static PixelDifference Compare(string firstPath, string secondPath, long expectedLength)
        {
            byte[] first = File.ReadAllBytes(firstPath);
            byte[] second = File.ReadAllBytes(secondPath);
            if (first.LongLength != expectedLength || second.LongLength != expectedLength)
            {
                throw new InvalidDataException(String.Format(
                    "Expected {0} decoded RGBA bytes, got {1} and {2}",
                    expectedLength, first.LongLength, second.LongLength));
            }

            var result = new PixelDifference { Identical = true };
            for (int offset = 0; offset < first.Length; offset += 4)
            {
                bool pixelDiffers = false;
                for (int channel = 0; channel < 4; ++channel)
                {
                    int delta = Math.Abs(first[offset + channel] - second[offset + channel]);
                    if (delta == 0)
                    {
                        continue;
                    }
                    result.Identical = false;
                    pixelDiffers = true;
                    ++result.DifferentChannels;
                    result.MaxChannelDelta = Math.Max(result.MaxChannelDelta, delta);
                }
                if (pixelDiffers)
                {
                    ++result.DifferentPixels;
                }
            }
            return result;
        }
    }
}
'@
    }
}

function Compare-CapturePixels([string]$firstPng, [string]$secondPng, [int]$width, [int]$height) {
    $firstRaw = Join-Path $runDir "reproducibility-first.rgba"
    $secondRaw = Join-Path $runDir "reproducibility-second.rgba"
    Remove-Item -LiteralPath $firstRaw, $secondRaw -Force -ErrorAction Ignore
    try {
        & $ffmpeg -y -loglevel error -i $firstPng -map 0:v:0 -frames:v 1 -f rawvideo -pix_fmt rgba $firstRaw
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed to decode capture for reproducibility comparison: $firstPng" }
        & $ffmpeg -y -loglevel error -i $secondPng -map 0:v:0 -frames:v 1 -f rawvideo -pix_fmt rgba $secondRaw
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed to decode capture for reproducibility comparison: $secondPng" }

        $expectedLength = [long]$width * [long]$height * 4L
        return [Reone.GuiCaptureProof.PixelComparer]::Compare($firstRaw, $secondRaw, $expectedLength)
    } finally {
        Remove-Item -LiteralPath $firstRaw, $secondRaw -Force -ErrorAction Ignore
    }
}

$resolutions = @(
    @{ W = 1024; H = 768 },
    @{ W = 3440; H = 1440 }
) | Where-Object { $Widths.Count -eq 0 -or $Widths -contains $_.W }
if ($resolutions.Count -eq 0) { throw "No configured width matches: $($Widths -join ', ')" }

# The main menu is up by frame 310; earlier frames are startup movie.
$readyFrame = 310
$startupStates = @(
    @{ Id = "startup-003"; Frame = 3 },
    @{ Id = "startup-120"; Frame = 120 },
    @{ Id = "startup-240"; Frame = 240 },
    @{ Id = "main-menu"; Frame = $readyFrame }
)

function New-GameStates([string]$module, [string]$swoop, [bool]$party, [string]$dialog, [int]$dialogFrame, [string]$computerModule, [string]$computerDialog, [string]$scrollModule, [string]$scrollDialog) {
    $items = if ($party) {
        @("w_blaste_01", "w_blaste_02 2", "w_blaste_03", "w_blaste_04", "w_blaste_05 2", "w_blaste_06", "w_blaste_07 2", "w_blaste_08")
    } else {
        @("g_w_blstrcrbn001", "g_w_blstrpstl001 2", "g_w_blstrrfl001", "g_w_bowcstr001", "g_w_dsrptpstl001 2", "g_w_dsrptrfl001", "g_w_ionblstr02 2", "g_w_ionrfl01")
    }
    $fixture = @("selectleader") + ($items | ForEach-Object { "additem $_" })
    $dialogEntrySkips = "autoskipentries " + (("1 " * 32).Trim())

    $states = [System.Collections.Generic.List[object]]::new()
    $startupStates | ForEach-Object { $states.Add($_) }
    $states.AddRange([object[]]@(
        @{ Id = "character-generation"; Frame = 60; Commands = @("openchargen class") },
        @{ Id = "character-quick-or-custom"; Frame = 60; Commands = @("warp $module", "openchargen quick-or-custom") },
        @{ Id = "character-quick"; Frame = 60; Commands = @("warp $module", "openchargen quick") },
        @{ Id = "character-portrait"; Frame = 60; Commands = @("warp $module", "openchargen portrait") },
        @{ Id = "character-name"; Frame = 60; Commands = @("warp $module", "openchargen name") },
        @{ Id = "character-custom"; Frame = 60; Commands = @("warp $module", "openchargen custom") },
        @{ Id = "character-abilities"; Frame = 60; Commands = @("warp $module", "openchargen abilities") },
        @{ Id = "character-skills"; Frame = 60; Commands = @("warp $module", "openchargen skills select") },
        @{ Id = "character-feats"; Frame = 60; Commands = @("warp $module", "openchargen feats select") },
        @{ Id = "character-powers"; Frame = 60; Commands = @("warp $module", "openchargen powers select") },
        @{ Id = "character-level-up"; Frame = 60; Commands = @("warp $module", "openchargen level-up") },
        @{ Id = "container"; Frame = 30; Commands = (@("warp $module") + $fixture + @("opencontainer")) },
        @{ Id = "gameplay"; Frame = $readyFrame; Commands = @("warp $module", "showhud") },
        @{ Id = "combat-action-sequence"; Frame = $readyFrame; Commands = @("warp $module", "showhud combat") },
        @{ Id = "area-transition"; Frame = 30; Commands = @("warp $module", "showtransition Scaled Area Transition") },
        @{ Id = "swoop"; Frame = 30; Commands = @("warp $swoop", "showgallerymode swoop") },
        @{ Id = "pazaak-wager"; Frame = 30; Commands = @("warp $module", "givegold 100", "showgallerymode pazaak wager") },
        @{ Id = "pazaak-setup"; Frame = 30; Commands = @("warp $module", "showgallerymode pazaak setup") },
        @{ Id = "pazaak-board"; Frame = 30; Commands = @("warp $module", "showgallerymode pazaak board") },
        @{ Id = "dialog"; Frame = $dialogFrame; Commands = @("warp $module", "startconversation $dialog") },
        @{ Id = "dialog-options"; Frame = 30; Commands = @("warp $module", "autoskipenable 1", $dialogEntrySkips, "autoskipreplies 0", "startconversation $dialog"); BeforeCaptureCommands = @("selectdialogoption 0"); AfterCommands = @("autoskipenable 0") },
        # More replies than the bottom band can hold, so the reply list
        # presents its scroll bar beside the prose in the 4:3 safe area.
        @{ Id = "dialog-scrollbar"; Frame = 30; Commands = @("warp $scrollModule", "autoskipenable 1", "autoskipentries 1", "autoskipreplies 0", "startconversation $scrollDialog"); BeforeCaptureCommands = @("selectdialogoption 0"); AfterCommands = @("autoskipenable 0") },
        @{ Id = "computer"; Frame = $readyFrame; Commands = @("warp $computerModule", "startconversation $computerDialog") }
    ))

    $tabs = @("equipment", "equipment-items", "inventory", "character", "abilities", "messages", "journal", "map", "options")
    if ($party) { $tabs += "party" }
    foreach ($tab in $tabs) {
        $needsItems = $tab -eq "inventory" -or $tab -eq "equipment-items"
        $commands = @("warp $module") + $(if ($needsItems) { $fixture } else { @() }) + @("openmenu $tab")
        $states.Add(@{ Id = $tab; Frame = $readyFrame; Commands = $commands })
    }

    if ($party) {
        # A mid-game roster: Handmaiden occupies the slot she shares with
        # Disciple, Mira and Hanharr are both away, and the available-slot
        # count sits centred in its strip above the portraits.
        $roster = @(
            "addavailablenpc 0 p_atton",
            "addavailablenpc 1 p_baodur",
            "addavailablenpc 2 p_mand",
            "addavailablenpc 4 p_handmaiden",
            "addavailablenpc 6 p_kreia",
            "addavailablenpc 8 p_t3m4",
            "addavailablenpc 9 p_visas")
        $states.Add(@{ Id = "party-roster"; Frame = $readyFrame; Commands = (@("warp $module") + $roster + @("openmenu party")) })
    }

    $states.Add(@{ Id = "bark-bubble"; Frame = 30; Commands = @("warp $module",
        "showbark 30 Scaled bark bubble text must wrap without clipping at every gallery resolution, even when the message is deliberately long enough to span multiple lines") })
    $states.Add(@{ Id = "confirmation-popup"; Frame = 30; Commands = @("warp $module",
        "showpopup i_attack Confirmation popup icon and message must scale together at every gallery resolution") })
    $states.Add(@{ Id = "combined-overlays"; Frame = 30; Commands = @("warp $module", "showhud combat",
        "showbark 30 Simultaneous overlay compatibility check with combat controls, selected object presentation, message bubble, and modal confirmation popup visible together",
        "showpopup i_attack Confirmation popup displayed over the complete simultaneous combat overlay state") })
    return $states
}

$games = @(
    @{ Id = "kotor1"; Dir = $Kotor1Dir; States = (New-GameStates "danm14aa" "tar_m03mg" $false "dan14_adam" 310 "end_m01ab" "end_securitycomp" "ebo_m40ad" "ebn12_galaxymap") },
    @{ Id = "kotor2"; Dir = $Kotor2Dir; States = (New-GameStates "101per" "371nar" $true "101atton" 900 "101per" "admlog" "101per" "3cfd") }
)

$count = 0
$reproducibilityFailures = [System.Collections.Generic.List[object]]::new()
foreach ($game in $games) {
    foreach ($res in $resolutions) {
        $selected = @($game.States | Where-Object { $States.Count -eq 0 -or $States -contains $_.Id })

        # One engine process per game and resolution. Every state uses the
        # game's default presentation scales, including the 50% list density.
        $lines = [System.Collections.Generic.List[string]]::new()
        if ($NoWorld) {
            $lines.Add("graphics off")
        } else {
            $lines.Add("graphics on")
        }
        $frame = 0
        foreach ($state in $selected) {
            $isStartup = -not $state.Commands
            if (-not $isStartup -and $frame -lt $readyFrame) {
                $lines.Add($(if ($frame -eq 0) { "skipmovie" } else { "pause $($readyFrame - $frame)" }))
                $frame = $readyFrame
            }
            # A batch is one process and one generator, so seeding only once per run lets drift
            # accumulate. Two builds then disagree on list contents while every frame and baseline
            # still matches to the pixel.
            #
            # Seeded after the wait above rather than at the top of the loop, so that the frames
            # rendered to reach the ready point cannot move the generator before a state runs its
            # own commands. Two builds need not consume at the same rate across those frames.
            if ($NoWorld) { $lines.Add("seed 1337") }
            if ($state.Commands) { $state.Commands | ForEach-Object { $lines.Add($_) } }
            $delay = if ($isStartup) { $state.Frame - $frame } else { $state.Frame }
            if ($isStartup) { $frame = $state.Frame }
            if ($delay -gt 0) { $lines.Add("pause $delay") }
            if ($state.BeforeCaptureCommands) { $state.BeforeCaptureCommands | ForEach-Object { $lines.Add($_) } }
            $lines.Add("capture $((Join-Path $runDir "$($state.Id).tga").Replace('\', '/'))")
            if ($state.AfterCommands) { $state.AfterCommands | ForEach-Object { $lines.Add($_) } }
        }
        $lines.Add("quit")
        $commandPath = Join-Path $runDir "commands.txt"
        Set-Content -LiteralPath $commandPath -Value $lines -Encoding utf8

        $capturePasses = if ($VerifyReproducibility) { 2 } else { 1 }
        for ($capturePass = 1; $capturePass -le $capturePasses; ++$capturePass) {
            $passSuffix = if ($VerifyReproducibility) { " (reproducibility pass $capturePass of 2)" } else { "" }
            Write-Host "$($game.Id) $($res.W)x$($res.H) default scales ($($selected.Count) captures)$passSuffix"
            Push-Location $runDir
            try {
                & $enginePath --game $game.Dir --commands-file $commandPath `
                    --width $res.W --height $res.H --winscale 100 --fullscreen 0 `
                    --headless 1 --dev 0 --vsync 0 --pbr 0 `
                    --guiscale 1 --guiborderscale 1 --guilistscale 0.5
                if ($LASTEXITCODE -ne 0) { throw "Engine exited with ${LASTEXITCODE}" }
            } finally {
                Pop-Location
            }

            foreach ($state in $selected) {
                $tga = Join-Path $runDir "$($state.Id).tga"
                if (-not (Test-Path -LiteralPath $tga -PathType Leaf)) {
                    throw "Capture produced no image: $($game.Id)/$($state.Id)"
                }
                $fileName = "$($game.Id)-$($state.Id)-$($res.W)x$($res.H).png"
                $isBaseline = $VerifyReproducibility -and $capturePass -eq 1
                $png = Join-Path $(if ($isBaseline) { $baselineDir } else { $OutputDir }) $fileName
                & $ffmpeg -y -loglevel error -i $tga $png
                if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed for $($state.Id)" }
                Remove-Item -LiteralPath $tga -Force

                if ($isBaseline) {
                    continue
                }
                $count++
                if ($VerifyReproducibility) {
                    $baselinePng = Join-Path $baselineDir $fileName
                    $difference = Compare-CapturePixels $baselinePng $png $res.W $res.H
                    if (-not $difference.Identical) {
                        $reproducibilityFailures.Add(@{
                            Game = $game.Id
                            State = $state.Id
                            Width = $res.W
                            Height = $res.H
                            MaxChannelDelta = $difference.MaxChannelDelta
                            DifferentChannels = $difference.DifferentChannels
                            DifferentPixels = $difference.DifferentPixels
                        })
                    }
                }
            }
        }
    }
}

if ($reproducibilityFailures.Count -gt 0) {
    $failureFormat = "Capture failed to reproduce: {0}/{1} at {2}x{3} (maximum RGBA channel difference: {4}; differing pixels: {5}; differing channels: {6})"
    $failureLines = $reproducibilityFailures | ForEach-Object {
        $failureFormat -f $_.Game, $_.State, $_.Width, $_.Height, $_.MaxChannelDelta,
            $_.DifferentPixels, $_.DifferentChannels
    }
    throw ($failureLines -join [Environment]::NewLine)
}

Remove-Item -LiteralPath $runDir -Recurse -Force -ErrorAction Ignore
if ($VerifyReproducibility) { Write-Host "Reproducibility verified for $count captures" }
Write-Host "$count captures written to $OutputDir"
