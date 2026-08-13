[CmdletBinding()]
param(
    [string]$TestFilter = "ShootGame",
    [string]$EngineRoot = "E:\Unreal_Engine\UE_5.6",
    [string]$ProjectPath = "",
    [string]$ReportRoot = "",
    [switch]$WithRendering
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ProjectPath))
{
    $ProjectPath = Join-Path $projectRoot "ShootGame.uproject"
}
if ([string]::IsNullOrWhiteSpace($ReportRoot))
{
    $ReportRoot = Join-Path $projectRoot "Saved\Automation"
}

$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf))
{
    throw "UnrealEditor-Cmd.exe was not found: $editorCommand"
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$safeFilter = $TestFilter -replace '[^A-Za-z0-9_.-]', '_'
$reportPath = Join-Path $ReportRoot "Reports\${timestamp}_${safeFilter}"
$logPath = Join-Path $ReportRoot "Logs\${timestamp}_${safeFilter}.log"
New-Item -ItemType Directory -Path $reportPath -Force | Out-Null
New-Item -ItemType Directory -Path (Split-Path $logPath -Parent) -Force | Out-Null

$arguments = @(
    $ProjectPath,
    "-ExecCmds=Automation RunTests $TestFilter; Quit",
    "-ReportExportPath=$reportPath",
    "-ABSLOG=$logPath",
    "-TestExit=Automation Test Queue Empty",
    "-unattended",
    "-nop4",
    "-nosplash",
    "-NoSound",
    "-stdout",
    "-FullStdOutLogOutput"
)

if (-not $WithRendering)
{
    $arguments += "-NullRHI"
}

Write-Host "[Automation] Filter: $TestFilter"
Write-Host "[Automation] Report: $reportPath"
Write-Host "[Automation] Log: $logPath"

& $editorCommand @arguments
$editorExitCode = $LASTEXITCODE

if ($editorExitCode -ne 0)
{
    throw "Automation process failed with exit code $editorExitCode. Log: $logPath"
}

$reportIndex = Join-Path $reportPath "index.json"
if (-not (Test-Path -LiteralPath $reportIndex -PathType Leaf))
{
    throw "Automation report was not generated. Log: $logPath"
}

$report = Get-Content -LiteralPath $reportIndex -Raw | ConvertFrom-Json
$succeededCount = [int]$report.succeeded
$warningCount = [int]$report.succeededWithWarnings
$failedCount = [int]$report.failed
$notRunCount = [int]$report.notRun

if ($failedCount -gt 0)
{
    throw "$failedCount automation test(s) failed. Report: $reportIndex"
}
if (($succeededCount + $warningCount) -eq 0)
{
    throw "No automation tests completed. Check the filter and log: $logPath"
}

Write-Host "[Passed] Automation run completed."
Write-Host "[Summary] Passed=$succeededCount Warnings=$warningCount Failed=$failedCount NotRun=$notRunCount"
Write-Host "[Result] $reportIndex"
