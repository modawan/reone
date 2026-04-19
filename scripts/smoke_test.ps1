#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$GameDir,

    [ValidateSet("auto", "k1", "k2")]
    [string]$Game = "auto",

    [string]$BuildDir,

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "RelWithDebInfo",

    [int]$TimeoutSeconds = 20,

    [switch]$NoBuild,

    [switch]$ForceBuild,

    [switch]$AllowMissingGame
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}
$BuildDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
$LogsRoot = Join-Path $RepoRoot ".agent\logs"
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$SmokeDir = Join-Path $LogsRoot "smoke_$timestamp"
$LatestSmokeDir = Join-Path $LogsRoot "smoke_latest"
New-Item -ItemType Directory -Force -Path $SmokeDir | Out-Null

$stdoutPath = Join-Path $SmokeDir "stdout.log"
$stderrPath = Join-Path $SmokeDir "stderr.log"
$engineLogPath = Join-Path $SmokeDir "engine.log"
$manifestPath = Join-Path $SmokeDir "smoke_manifest.json"
$startupSignal = "reone smoke signal: engine startup"
New-Item -ItemType File -Force -Path $stdoutPath | Out-Null
New-Item -ItemType File -Force -Path $stderrPath | Out-Null
New-Item -ItemType File -Force -Path $engineLogPath | Out-Null

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

function Resolve-GameDirectory {
    param(
        [string]$Path,
        [string]$RequestedGame
    )

    if (-not $Path) {
        if ($env:KOTOR1_DIR) {
            $Path = $env:KOTOR1_DIR
            if ($RequestedGame -eq "auto") {
                $RequestedGame = "k1"
            }
        } elseif ($env:KOTOR2_DIR) {
            $Path = $env:KOTOR2_DIR
            if ($RequestedGame -eq "auto") {
                $RequestedGame = "k2"
            }
        }
    }

    if (-not $Path) {
        return $null
    }

    $resolved = (Resolve-Path $Path).Path
    $k1Marker = Join-Path $resolved "swkotor.exe"
    $k2Marker = Join-Path $resolved "swkotor2.exe"

    if ($RequestedGame -eq "k1" -and -not (Test-Path $k1Marker)) {
        throw "KOTOR 1 marker executable was not found: $k1Marker"
    }
    if ($RequestedGame -eq "k2" -and -not (Test-Path $k2Marker)) {
        throw "KOTOR 2 marker executable was not found: $k2Marker"
    }
    if ($RequestedGame -eq "auto") {
        if (Test-Path $k1Marker) {
            $RequestedGame = "k1"
        } elseif (Test-Path $k2Marker) {
            $RequestedGame = "k2"
        } else {
            throw "No KOTOR marker executable was found in: $resolved"
        }
    }

    return [ordered]@{
        path = $resolved
        game = $RequestedGame
    }
}

function Write-Manifest {
    param([object]$Manifest)
    $Manifest | ConvertTo-Json -Depth 8 | Set-Content -Path $manifestPath -Encoding UTF8
}

function Publish-LatestSmoke {
    if (Test-Path $LatestSmokeDir) {
        Remove-Item -Recurse -Force -Path $LatestSmokeDir
    }
    Copy-Item -Recurse -Force -Path $SmokeDir -Destination $LatestSmokeDir
}

function Find-FatalMatches {
    param([string[]]$Paths)

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

    $matches = @()
    foreach ($path in $Paths) {
        if (Test-Path $path) {
            $matches += Select-String -Path $path -Pattern $fatalPatterns -SimpleMatch -ErrorAction SilentlyContinue
        }
    }

    return $matches
}

function Join-ProcessArguments {
    param([string[]]$Arguments)

    $quoted = foreach ($argument in $Arguments) {
        $escaped = $argument -replace '"', '\"'
        if ($escaped -match '\s') {
            '"' + $escaped + '"'
        } else {
            $escaped
        }
    }

    return ($quoted -join " ")
}

$manifest = [ordered]@{
    schema = 1
    createdAt = (Get-Date).ToString("o")
    repoRoot = $RepoRoot
    build = [ordered]@{
        success = $false
        action = "not-started"
        config = $Config
        target = "engine"
        buildDir = $BuildDir
        engineExe = $null
        shaderpackAction = "not-started"
        shaderpackErf = $null
        log = (Join-Path $LogsRoot "latest_build.log")
    }
    launch = [ordered]@{
        attempted = $false
        started = $false
        timedOut = $false
        timeoutSeconds = $TimeoutSeconds
        exitCode = $null
        game = $Game
        gameDir = $null
        skipReason = $null
        stdout = $stdoutPath
        stderr = $stderrPath
        engineLog = $engineLogPath
        shaderpackErf = $null
    }
    evalHints = [ordered]@{
        obviousFatalPatterns = @("Engine failure", "FATAL", "Unhandled exception", "Access violation", "Segmentation fault", "CMake Error", "Build FAILED", "fatal error")
        startupSignal = $startupSignal
    }
}

try {
    $gameInfo = Resolve-GameDirectory -Path $GameDir -RequestedGame $Game
    if ($gameInfo) {
        $manifest.launch.game = $gameInfo.game
        $manifest.launch.gameDir = $gameInfo.path
    }

    $engineExe = Resolve-EngineExe
    if (($ForceBuild -or -not $engineExe) -and -not $NoBuild) {
        $manifest.build.action = "built"
        Write-Manifest -Manifest $manifest
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config -Target "engine"
        if ($LASTEXITCODE -ne 0) {
            $manifest.build.success = $false
            $manifest.launch.skipReason = "Build failed before launch. See $($manifest.build.log)."
            Write-Manifest -Manifest $manifest
            Publish-LatestSmoke
            exit $LASTEXITCODE
        }
        $engineExe = Resolve-EngineExe
    } elseif ($engineExe) {
        $manifest.build.action = "verified-existing"
    } else {
        $manifest.build.action = "missing"
    }

    if (-not $engineExe) {
        $manifest.build.success = $false
        $manifest.launch.skipReason = "engine.exe was not found before launch."
        Write-Manifest -Manifest $manifest
        Publish-LatestSmoke
        Write-Error "engine.exe was not found. Run scripts\build.ps1 or omit -NoBuild."
        exit 1
    }

    $manifest.build.success = $true
    $manifest.build.engineExe = $engineExe

    $shaderPackErf = Resolve-ShaderPackErf
    if (($ForceBuild -or -not $shaderPackErf) -and -not $NoBuild) {
        $manifest.build.shaderpackAction = "built"
        Write-Manifest -Manifest $manifest
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Config $Config -Target "create_shaderpack"
        if ($LASTEXITCODE -ne 0) {
            $manifest.build.success = $false
            $manifest.launch.skipReason = "Shader pack build failed before launch. See $($manifest.build.log)."
            Write-Manifest -Manifest $manifest
            Publish-LatestSmoke
            exit $LASTEXITCODE
        }
        $shaderPackErf = Resolve-ShaderPackErf
    } elseif ($shaderPackErf) {
        $manifest.build.shaderpackAction = "verified-existing"
    } else {
        $manifest.build.shaderpackAction = "missing"
    }

    if (-not $shaderPackErf) {
        $manifest.build.success = $false
        $manifest.launch.skipReason = "shaderpack.erf was not found before launch."
        Write-Manifest -Manifest $manifest
        Publish-LatestSmoke
        Write-Error "shaderpack.erf was not found. Build the create_shaderpack target or omit -NoBuild."
        exit 1
    }

    $manifest.build.shaderpackErf = $shaderPackErf

    if (-not $gameInfo) {
        $manifest.launch.skipReason = "No legal KOTOR or TSL game directory was provided. Pass -GameDir or set KOTOR1_DIR/KOTOR2_DIR."
        Write-Manifest -Manifest $manifest
        Publish-LatestSmoke
        if ($AllowMissingGame) {
            Write-Host $manifest.launch.skipReason
            exit 0
        }
        Write-Error $manifest.launch.skipReason
        exit 2
    }

    $manifest.launch.attempted = $true
    $manifest.launch.game = $gameInfo.game
    $manifest.launch.gameDir = $gameInfo.path
    $manifest.launch.shaderpackErf = Join-Path $SmokeDir "shaderpack.erf"
    Copy-Item -Force -Path $shaderPackErf -Destination $manifest.launch.shaderpackErf
    Write-Manifest -Manifest $manifest

    $engineArgs = @(
        "--game", $gameInfo.path,
        "--width", "640",
        "--height", "480",
        "--winscale", "100",
        "--fullscreen", "false",
        "--logsev", "1"
    )

    $process = Start-Process -FilePath $engineExe -ArgumentList (Join-ProcessArguments -Arguments $engineArgs) -WorkingDirectory $SmokeDir -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
    $manifest.launch.started = $true
    Write-Manifest -Manifest $manifest

    $finished = $process.WaitForExit($TimeoutSeconds * 1000)
    if ($finished) {
        $manifest.launch.exitCode = $process.ExitCode
    } else {
        $manifest.launch.timedOut = $true
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 250
    }

    if (-not (Test-Path $engineLogPath)) {
        New-Item -ItemType File -Path $engineLogPath | Out-Null
    }

    $fatalMatches = Find-FatalMatches -Paths @($stdoutPath, $stderrPath, $engineLogPath)
    Write-Manifest -Manifest $manifest
    Publish-LatestSmoke

    if ($fatalMatches.Count -gt 0) {
        Write-Error "Smoke found obvious fatal output. See $SmokeDir"
        exit 1
    }

    if ($manifest.launch.exitCode -ne $null -and [int]$manifest.launch.exitCode -ne 0) {
        Write-Error "Engine exited with code $($manifest.launch.exitCode). See $SmokeDir"
        exit 1
    }

    Write-Host "Smoke completed. Artifacts: $SmokeDir"
    exit 0
} catch {
    $manifest.launch.skipReason = $_.Exception.Message
    Write-Manifest -Manifest $manifest
    Publish-LatestSmoke
    Write-Error $_
    exit 1
}
