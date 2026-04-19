#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$Destination,

    [string]$SmokeDir
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$LogsRoot = Join-Path $RepoRoot ".agent\logs"

if (-not $Destination) {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $Destination = Join-Path $LogsRoot "capture_$timestamp"
}

$Destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$paths = @(
    (Join-Path $RepoRoot "engine.log"),
    (Join-Path $RepoRoot "build\bin\engine.log"),
    (Join-Path $RepoRoot "build\debug\bin\engine.log"),
    (Join-Path $LogsRoot "latest_build.log"),
    (Join-Path $LogsRoot "latest_configure.log")
)

if (-not $SmokeDir) {
    $SmokeDir = Join-Path $LogsRoot "smoke_latest"
}

if (Test-Path $SmokeDir) {
    $paths += $SmokeDir
}

foreach ($path in $paths) {
    if (Test-Path $path) {
        Copy-Item -Recurse -Force -Path $path -Destination $Destination
    }
}

Write-Host "Captured available logs to $Destination"
