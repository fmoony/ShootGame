[CmdletBinding()]
param(
    [string]$EngineRoot = "E:\Unreal_Engine\UE_5.6",
    [string]$ProjectPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ProjectPath))
{
    $ProjectPath = Join-Path $projectRoot "ShootGame.uproject"
}

$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$unrealBuildTool = Join-Path `
    $EngineRoot `
    "Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"
if (-not (Test-Path -LiteralPath $unrealBuildTool -PathType Leaf))
{
    throw "UnrealBuildTool was not found: $unrealBuildTool"
}

$refreshMutex = [System.Threading.Mutex]::new(
    $false,
    "Local\ShootGameVisualStudioProjectRefresh")
$mutexAcquired = $false

try
{
    $mutexAcquired = $refreshMutex.WaitOne(0)
    if (-not $mutexAcquired)
    {
        throw "Another ShootGame Visual Studio project refresh is already running."
    }

    Write-Host "[ProjectFiles] Project: $ProjectPath"
    Write-Host "[ProjectFiles] Generating Visual Studio 2022 project files."

    & $unrealBuildTool `
        "-ProjectFiles" `
        "-Project=$ProjectPath" `
        "-Game" `
        "-Rocket" `
        "-Progress"

    if ($LASTEXITCODE -ne 0)
    {
        throw "Visual Studio project refresh failed with exit code $LASTEXITCODE"
    }

    Write-Host "[Passed] Visual Studio project files were refreshed."
    Write-Host "[VisualStudio] If Reload All is shown, it loads the latest generated project state."
}
finally
{
    if ($mutexAcquired)
    {
        $refreshMutex.ReleaseMutex()
    }
    $refreshMutex.Dispose()
}
