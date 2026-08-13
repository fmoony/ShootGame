[CmdletBinding()]
param(
    [string]$EngineRoot = "E:\Unreal_Engine\UE_5.6",
    [string]$ProjectPath = "",
    [ValidateSet("DebugGame", "Development")]
    [string]$Configuration = "Development"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ProjectPath))
{
    $ProjectPath = Join-Path $projectRoot "ShootGame.uproject"
}

$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$buildTool = Join-Path $EngineRoot "Engine\Build\BatchFiles\Build.bat"

if (-not (Test-Path -LiteralPath $buildTool -PathType Leaf))
{
    throw "Unreal build tool was not found: $buildTool"
}

Write-Host "[Build] Project: $ProjectPath"
Write-Host "[Build] Target: ShootGameEditor Win64 $Configuration"

& $buildTool `
    "ShootGameEditor" `
    "Win64" `
    $Configuration `
    "-Project=$ProjectPath" `
    "-WaitMutex" `
    "-FromMsBuild"

if ($LASTEXITCODE -ne 0)
{
    throw "ShootGameEditor build failed with exit code $LASTEXITCODE"
}

Write-Host "[Passed] ShootGameEditor build succeeded."
