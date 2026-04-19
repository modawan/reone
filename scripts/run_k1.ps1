#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameDir = $env:KOTOR1_DIR,

    [string]$BuildDir,

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "RelWithDebInfo",

    [switch]$Dev,

    [ValidateRange(0, 3)]
    [int]$LogSeverity,

    [string[]]$EngineArgs = @()
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}
$BuildDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)

function Resolve-EngineExe {
    $candidates = @(
        (Join-Path $BuildDir "bin\engine.exe"),
        (Join-Path $BuildDir "debug\bin\engine.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $found = Get-ChildItem -Path $BuildDir -Recurse -Filter "engine.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        return $found.FullName
    }

    return $null
}

function Resolve-ShaderPackErf {
    $candidates = @(
        (Join-Path $BuildDir "bin\shaderpack.erf"),
        (Join-Path $BuildDir "debug\bin\shaderpack.erf")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    $found = Get-ChildItem -Path $BuildDir -Recurse -Filter "shaderpack.erf" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) {
        return $found.FullName
    }

    return $null
}

$engineExe = Resolve-EngineExe
if (-not $engineExe) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config -Target "engine"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    $engineExe = Resolve-EngineExe
}

$shaderPackErf = Resolve-ShaderPackErf
if (-not $shaderPackErf) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config -Target "create_shaderpack"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    $shaderPackErf = Resolve-ShaderPackErf
}

if (-not $engineExe) {
    Write-Error "engine.exe was not found after build."
    exit 1
}

if (-not $shaderPackErf) {
    Write-Error "shaderpack.erf was not found after building create_shaderpack."
    exit 1
}

if (-not $GameDir) {
    Write-Error "Pass -GameDir or set KOTOR1_DIR to a legal KOTOR 1 install directory."
    exit 2
}

$GameDir = (Resolve-Path $GameDir).Path
$marker = Join-Path $GameDir "swkotor.exe"
if (-not (Test-Path $marker)) {
    Write-Error "KOTOR 1 marker executable was not found: $marker"
    exit 2
}

if ($Dev) {
    $hasDevArg = $false
    foreach ($arg in $EngineArgs) {
        if ($arg -eq "--dev" -or $arg -like "--dev=*") {
            $hasDevArg = $true
            break
        }
    }
    if (-not $hasDevArg) {
        $EngineArgs = @("--dev", "true") + $EngineArgs
    }
}

if ($PSBoundParameters.ContainsKey("LogSeverity")) {
    $hasLogSeverityArg = $false
    foreach ($arg in $EngineArgs) {
        if ($arg -eq "--logsev" -or $arg -like "--logsev=*") {
            $hasLogSeverityArg = $true
            break
        }
    }
    if (-not $hasLogSeverityArg) {
        $EngineArgs = @("--logsev", "$LogSeverity") + $EngineArgs
    }
}

Push-Location (Split-Path -Parent $engineExe)
try {
    & $engineExe "--game" $GameDir @EngineArgs
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
