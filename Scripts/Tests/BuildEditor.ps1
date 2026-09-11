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

# UBA 规避（2026-09-11）：本机 UBA 执行器（Detours 拦截 cl）对当前 ShootGame 测试模块的
# 某 unity 编译单元稳定误报 C4756（常量算法溢出，无源码位置）；同一命令同一环境用
# 普通 cl / UBT 本地执行器均通过。详见当日开发记录。工具链更新后可移除本参数复验。
& $buildTool `
    "ShootGameEditor" `
    "Win64" `
    $Configuration `
    "-Project=$ProjectPath" `
    "-WaitMutex" `
    "-FromMsBuild" `
    "-NoUBA"

if ($LASTEXITCODE -ne 0)
{
    throw "ShootGameEditor build failed with exit code $LASTEXITCODE"
}

Write-Host "[Passed] ShootGameEditor build succeeded."
