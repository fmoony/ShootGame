[CmdletBinding()]
param(
    [string]$TestFilter = "ShootGame",
	[string]$EngineRoot = "",
    [string]$ProjectPath = "",
    [string]$ReportRoot = "",
	[string]$ExpectedTestsPath = (Join-Path $PSScriptRoot "ShootGame.AutomationTests.txt"),
	[string[]]$ExpectedSkippedTest = @(),
    [switch]$WithRendering
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$EngineRoot = & (Join-Path $PSScriptRoot "../Development/ResolveShootGameEngine.ps1") -EngineRoot $EngineRoot

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

# 默认全量和普通前缀筛选都从已审核的清单推导预期用例。
$manifest = Get-Content -LiteralPath $ExpectedTestsPath | Where-Object { $_ -and -not $_.StartsWith("#") }
$expected = if ($PSBoundParameters.ContainsKey("ExpectedTestsPath"))
{
	$manifest
}
else
{
	$manifest | Where-Object {
		$_ -eq $TestFilter -or $_.StartsWith("$TestFilter.", [StringComparison]::OrdinalIgnoreCase)
	}
}
$expected = @($expected)
if ($expected.Count -eq 0)
{
	throw "No discovery baseline for '$TestFilter'. Supply -ExpectedTestsPath for a custom filter."
}
if (@($expected | Sort-Object -Unique).Count -ne $expected.Count)
{
	throw "The discovery baseline contains duplicate tests: $ExpectedTestsPath"
}
foreach ($test in $ExpectedSkippedTest)
{
	if ($test -cnotin $expected)
	{
		throw "Expected skipped test is outside the discovery baseline: $test"
	}
}

$timestamp = "{0}_{1}" -f (Get-Date -Format "yyyyMMdd_HHmmss"), [guid]::NewGuid().ToString("N").Substring(0, 8)
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
    "-DDC-ForceMemoryCache",
    "-DisablePlugins=McpAutomationBridge",
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

$inProcessCount = [int]$report.inProcess
$discovered = @($report.tests | ForEach-Object { $_.fullTestPath })
$missing = @($expected | Where-Object { $_ -cnotin $discovered })
$unexpected = @($discovered | Where-Object { $_ -cnotin $expected })
if ($missing.Count -or $unexpected.Count -or $discovered.Count -ne $expected.Count)
{
	$details = "Missing=[$($missing -join ', ')] Unexpected=[$($unexpected -join ', ')]"
	throw "Discovery mismatch. $details Expected=$($expected.Count) Discovered=$($discovered.Count). Report: $reportIndex"
}
$skipped = @($report.tests | Where-Object { $_.state -eq "NotRun" } | ForEach-Object { $_.fullTestPath })
$unexpectedSkips = @($skipped | Where-Object { $_ -cnotin $ExpectedSkippedTest })
if ($unexpectedSkips.Count -or $notRunCount -ne $skipped.Count)
{
	throw "Unexpected skipped tests (NotRun=$notRunCount): $($unexpectedSkips -join ', '). Report: $reportIndex"
}
if ($inProcessCount -ne 0 -or $succeededCount + $warningCount + $failedCount + $notRunCount -ne $discovered.Count)
{
	throw "Automation report has incomplete or inconsistent results. Report: $reportIndex"
}

if ($failedCount -gt 0)
{
    throw "$failedCount automation test(s) failed. Report: $reportIndex"
}
if (($succeededCount + $warningCount) -eq 0)
{
    throw "No automation tests completed. Check the filter and log: $logPath"
}

Write-Host "[Passed] Automation run completed."
Write-Host "[Summary] Discovered=$($discovered.Count) Passed=$succeededCount Warnings=$warningCount Failed=$failedCount NotRun=$notRunCount"
Write-Host "[Result] $reportIndex"
