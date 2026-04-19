#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$BuildDir,

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "RelWithDebInfo",

    [string]$Target = "engine",

    [switch]$Configure,

    [string[]]$CMakeArgs = @()
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}
$BuildDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
$LogsDir = Join-Path $RepoRoot ".agent\logs"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $LogsDir | Out-Null

function Resolve-CMakeExe {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    if ($env:CMAKE_EXE -and (Test-Path $env:CMAKE_EXE)) {
        return $env:CMAKE_EXE
    }

    $defaultPath = "C:\Program Files\CMake\bin\cmake.exe"
    if (Test-Path $defaultPath) {
        return $defaultPath
    }

    throw "cmake.exe was not found. Add CMake to PATH or set CMAKE_EXE."
}

function Resolve-VcpkgToolchain {
    if ($env:VCPKG_ROOT) {
        $fromEnv = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path $fromEnv) {
            return $fromEnv
        }
    }

    $defaultPath = "D:\vcpkg\scripts\buildsystems\vcpkg.cmake"
    if (Test-Path $defaultPath) {
        return $defaultPath
    }

    return $null
}

function Resolve-VcpkgRootFromToolchain {
    param([string]$Toolchain)

    if (-not $Toolchain) {
        return $null
    }

    $toolchainPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Toolchain)
    $buildsystemsDir = Split-Path -Parent $toolchainPath
    $scriptsDir = Split-Path -Parent $buildsystemsDir

    if ((Split-Path -Leaf $buildsystemsDir) -ne "buildsystems" -or
        (Split-Path -Leaf $scriptsDir) -ne "scripts") {
        return $null
    }

    return (Split-Path -Parent $scriptsDir)
}

function Resolve-VcpkgTargetTriplet {
    param([string[]]$Arguments)

    foreach ($argument in $Arguments) {
        if ($argument -like "-DVCPKG_TARGET_TRIPLET=*") {
            return $argument.Substring("-DVCPKG_TARGET_TRIPLET=".Length)
        }
    }

    if ($env:VCPKG_DEFAULT_TRIPLET) {
        return $env:VCPKG_DEFAULT_TRIPLET
    }

    return "x64-windows"
}

function Write-VcpkgPackageConfigHint {
    param(
        [string]$Toolchain,
        [string[]]$Arguments
    )

    $vcpkgRoot = Resolve-VcpkgRootFromToolchain -Toolchain $Toolchain
    if (-not $vcpkgRoot) {
        return
    }

    if (Test-Path (Join-Path $RepoRoot "vcpkg.json")) {
        return
    }

    $triplet = Resolve-VcpkgTargetTriplet -Arguments $Arguments
    $sdl3ConfigDir = Join-Path $vcpkgRoot "installed\$triplet\share\sdl3"
    if (Test-Path $sdl3ConfigDir) {
        return
    }

    $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
    $installCommand = "& `"$vcpkgExe`" install sdl3:${triplet}"
    Write-Warning "SDL3 vcpkg config was not found at $sdl3ConfigDir. CMake requires find_package(SDL3 CONFIG REQUIRED). Install it with '$installCommand' or set VCPKG_ROOT/CMAKE_TOOLCHAIN_FILE to a vcpkg tree that has sdl3:${triplet}."
}

function Invoke-CMakeLogged {
    param(
        [string[]]$Arguments,
        [string]$LogPath
    )

    Write-Host "Using CMake: $script:CMakeExe"
    Write-Host "cmake $($Arguments -join ' ')"
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $script:CMakeExe @Arguments 2>&1 | Tee-Object -FilePath $LogPath
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "CMake failed with exit code $exitCode. See $LogPath"
    }
}

function Test-CMakeProjectReady {
    if (-not (Test-Path $cachePath)) {
        return $false
    }

    $generatedProject = Get-ChildItem -Path $BuildDir -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -eq "build.ninja" -or $_.Name -eq "Makefile" -or $_.Extension -eq ".sln" } |
        Select-Object -First 1

    return $null -ne $generatedProject
}

$script:CMakeExe = Resolve-CMakeExe
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$buildLog = Join-Path $LogsDir "build_$timestamp.log"
$configureLog = Join-Path $LogsDir "configure_$timestamp.log"
$latestBuildLog = Join-Path $LogsDir "latest_build.log"
$latestConfigureLog = Join-Path $LogsDir "latest_configure.log"
$cachePath = Join-Path $BuildDir "CMakeCache.txt"

try {
    if ($Configure -or -not (Test-CMakeProjectReady)) {
        $configureArgs = @("-S", $RepoRoot, "-B", $BuildDir, "-DCMAKE_BUILD_TYPE=$Config")
        $vcpkgToolchain = Resolve-VcpkgToolchain
        if ($vcpkgToolchain) {
            $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain"
        }
        if ($CMakeArgs.Count -gt 0) {
            $configureArgs += $CMakeArgs
        }

        Write-VcpkgPackageConfigHint -Toolchain $vcpkgToolchain -Arguments $configureArgs
        Invoke-CMakeLogged -Arguments $configureArgs -LogPath $configureLog
        Copy-Item -Force -Path $configureLog -Destination $latestConfigureLog
    }

    $buildArgs = @("--build", $BuildDir, "--config", $Config)
    if ($Target) {
        $buildArgs += @("--target", $Target)
    }
    $buildArgs += "--parallel"

    Invoke-CMakeLogged -Arguments $buildArgs -LogPath $buildLog
    Copy-Item -Force -Path $buildLog -Destination $latestBuildLog

    Write-Host "Build succeeded for target '$Target' ($Config)."
    exit 0
} catch {
    if (Test-Path $buildLog) {
        Copy-Item -Force -Path $buildLog -Destination $latestBuildLog
    } elseif (Test-Path $configureLog) {
        Copy-Item -Force -Path $configureLog -Destination $latestBuildLog
    }
    if (Test-Path $configureLog) {
        Copy-Item -Force -Path $configureLog -Destination $latestConfigureLog
    }
    Write-Error $_
    exit 1
}
