#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$SmokeDir,

    [switch]$AllowMissingGame
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$LogsRoot = Join-Path $RepoRoot ".agent\logs"

if (-not $SmokeDir) {
    $SmokeDir = Join-Path $LogsRoot "smoke_latest"
}

$SmokeDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SmokeDir)
$manifestPath = Join-Path $SmokeDir "smoke_manifest.json"

if (-not (Test-Path $manifestPath)) {
    Write-Error "Smoke manifest not found: $manifestPath"
    exit 1
}

$manifest = Get-Content -Raw -Path $manifestPath | ConvertFrom-Json
$errors = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

if (-not $manifest.build.success) {
    $errors.Add("Build did not succeed according to smoke manifest.")
}

if (-not $manifest.build.engineExe -or -not (Test-Path $manifest.build.engineExe)) {
    $errors.Add("Built engine executable was not found: $($manifest.build.engineExe)")
}

if (-not $manifest.build.shaderpackErf -or -not (Test-Path $manifest.build.shaderpackErf)) {
    $errors.Add("Built shaderpack.erf was not found: $($manifest.build.shaderpackErf)")
}

if (-not $manifest.launch.attempted) {
    if ($AllowMissingGame -and $manifest.launch.skipReason) {
        $warnings.Add("Launch was skipped: $($manifest.launch.skipReason)")
    } else {
        $errors.Add("Engine process launch was not attempted.")
    }
} else {
    if (-not $manifest.launch.started) {
        $errors.Add("Engine process launch was attempted but did not start.")
    }

    if (-not $manifest.launch.timedOut -and $manifest.launch.exitCode -ne $null -and [int]$manifest.launch.exitCode -ne 0) {
        $errors.Add("Engine exited with non-zero code $($manifest.launch.exitCode).")
    }

    if (-not $manifest.launch.shaderpackErf -or -not (Test-Path $manifest.launch.shaderpackErf)) {
        $errors.Add("Smoke launch shaderpack.erf was not found: $($manifest.launch.shaderpackErf)")
    }
}

$logPaths = @($manifest.build.log, $manifest.launch.stdout, $manifest.launch.stderr, $manifest.launch.engineLog)
$fatalPatterns = @(
    "Engine failure",
    "FATAL",
    "Unhandled exception",
    "Access violation",
    "Segmentation fault",
    "CMake Error",
    "Build FAILED",
    "fatal error"
)
$startupSignal = "reone smoke signal: engine startup"
if ($manifest.evalHints -and $manifest.evalHints.startupSignal) {
    $startupSignal = $manifest.evalHints.startupSignal
}

foreach ($path in $logPaths) {
    if ($path -and (Test-Path $path)) {
        $matches = Select-String -Path $path -Pattern $fatalPatterns -SimpleMatch -ErrorAction SilentlyContinue
        foreach ($match in $matches) {
            $errors.Add("Fatal pattern in $path line $($match.LineNumber): $($match.Line.Trim())")
        }
    }
}

$engineLog = $manifest.launch.engineLog
if ($manifest.launch.attempted -and $engineLog -and (Test-Path $engineLog)) {
    $engineLogText = Get-Content -Raw -Path $engineLog
    if ($null -eq $engineLogText) {
        $engineLogText = ""
    }
    if ($engineLogText.Trim().Length -gt 0) {
        if ($engineLogText -notmatch "(?m)^\s*(ERROR|WARN|INFO|DEBUG)\s+\[") {
            $errors.Add("engine.log exists but does not contain expected reone-formatted startup log lines.")
        }
        $escapedStartupSignal = [regex]::Escape($startupSignal)
        if ($engineLogText -notmatch "(?m)^\s*INFO\s+\[main\]\[global\]\s+$escapedStartupSignal\s*$") {
            $errors.Add("engine.log does not contain required startup signal: $startupSignal")
        }
    } else {
        $errors.Add("engine.log was empty; required startup signal was not emitted.")
    }
} elseif ($manifest.launch.attempted) {
    $errors.Add("engine.log was not available to inspect for required startup signal.")
}

if ($warnings.Count -gt 0) {
    foreach ($warning in $warnings) {
        Write-Warning $warning
    }
}

if ($errors.Count -gt 0) {
    foreach ($errorMessage in $errors) {
        Write-Error $errorMessage -ErrorAction Continue
    }
    exit 1
}

Write-Host "Smoke eval passed for $SmokeDir"
exit 0
