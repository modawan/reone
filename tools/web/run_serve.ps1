<#
.SYNOPSIS
  Run wasm diagnostics, then tools/web/serve.py from the repo root (clean slate-friendly).

.EXAMPLE
  pwsh tools/web/run_serve.ps1 --directory build-web/bin --port 11152
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

function Invoke-ReonePy {
    param([Parameter(Mandatory = $true)][string[]]$PyArguments)
    if (Get-Command py -ErrorAction SilentlyContinue) {
        & py -3 @PyArguments
    } elseif (Get-Command python -ErrorAction SilentlyContinue) {
        & python @PyArguments
    } elseif (Get-Command python3 -ErrorAction SilentlyContinue) {
        & python3 @PyArguments
    } else {
        Write-Error 'Need Python on PATH: py, python, or python3.'
        exit 127
    }
}

$diagArgs = @('tools/web/serve.py', '--diag') + @($args)
Invoke-ReonePy $diagArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$serveArgs = @('tools/web/serve.py') + @($args)
Invoke-ReonePy $serveArgs
