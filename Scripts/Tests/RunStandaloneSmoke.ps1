[CmdletBinding()]
param(
    [string]$EngineRoot = "E:\Unreal_Engine\UE_5.6",
    [string]$ProjectPath = "",
    [string]$MapPath = "/Game/Shooter/Maps/Lvl_Shooter",
    [ValidateRange(1, 120)]
    [int]$DurationSeconds = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function ConvertTo-ProcessArgumentLine
{
    param([string[]]$Arguments)

    $quotedArguments = foreach ($argument in $Arguments)
    {
        if ($argument -match '[\s"]')
        {
            '"' + ($argument -replace '"', '\"') + '"'
        }
        else
        {
            $argument
        }
    }
    return $quotedArguments -join ' '
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ProjectPath))
{
    $ProjectPath = Join-Path $projectRoot "ShootGame.uproject"
}
$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path

$editorCommand = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
if (-not (Test-Path -LiteralPath $editorCommand -PathType Leaf))
{
    throw "UnrealEditor-Cmd.exe was not found: $editorCommand"
}

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logRoot = Join-Path $projectRoot "Saved\Automation\Standalone"
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$logPath = Join-Path $logRoot "${timestamp}.log"
$process = $null

try
{
    $arguments = @(
        $ProjectPath,
        $MapPath,
        "-game",
        "-unattended",
        "-nop4",
        "-nosplash",
        "-NoSound",
        "-NullRHI",
        "-DDC-ForceMemoryCache",
        "-DisablePlugins=McpAutomationBridge",
        "-ABSLOG=$logPath"
    )

    Write-Host "[Standalone] Starting Shooter map smoke test."
    Write-Host "[Standalone] Log: $logPath"
    $process = Start-Process `
        -FilePath $editorCommand `
        -ArgumentList (ConvertTo-ProcessArgumentLine $arguments) `
        -PassThru `
        -WindowStyle Hidden

    $startupDeadline = (Get-Date).AddSeconds(90)
    $worldStarted = $false
    while ((Get-Date) -lt $startupDeadline)
    {
        $process.Refresh()
        if ($process.HasExited)
        {
            throw "Standalone process exited early with code $($process.ExitCode). Log: $logPath"
        }

        if (Test-Path -LiteralPath $logPath -PathType Leaf)
        {
            $worldStarted = [bool](Select-String `
                -LiteralPath $logPath `
                -Pattern "Bringing World .* up for play" `
                -Quiet)
        }
        if ($worldStarted)
        {
            break
        }
        Start-Sleep -Milliseconds 500
    }

    if (-not $worldStarted)
    {
        throw "Timed out waiting for standalone world startup. Log: $logPath"
    }

    $deadline = (Get-Date).AddSeconds($DurationSeconds)
    while ((Get-Date) -lt $deadline)
    {
        $process.Refresh()
        if ($process.HasExited)
        {
            throw "Standalone process exited during smoke test. Log: $logPath"
        }
        Start-Sleep -Milliseconds 500
    }

    $failure = Select-String `
        -LiteralPath $logPath `
        -Pattern "Fatal error|Unhandled Exception|EXCEPTION_ACCESS_VIOLATION|Accessed None" `
        -Quiet
    if ($failure)
    {
        throw "Standalone failure marker was found. Log: $logPath"
    }

    Write-Host "[Passed] Standalone Shooter map remained healthy."
}
finally
{
    if ($null -ne $process)
    {
        $process.Refresh()
        if (-not $process.HasExited)
        {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit(10000) | Out-Null
        }
    }
    Write-Host "[Standalone] Launched process was stopped."
}
