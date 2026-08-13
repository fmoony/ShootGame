[CmdletBinding()]
param(
    [string]$EngineRoot = "E:\Unreal_Engine\UE_5.6",
    [string]$ProjectPath = "",
    [string]$MapPath = "/Game/FirstPerson/Lvl_FirstPerson",
    [ValidateRange(1, 8)]
    [int]$ClientCount = 2,
    [ValidateRange(1024, 65535)]
    [int]$Port = 17777,
    [ValidateRange(10, 300)]
    [int]$StartupTimeoutSeconds = 90,
    [ValidateRange(1, 600)]
    [int]$SessionDurationSeconds = 10,
    [string]$SuccessMarker = "",
    [ValidateRange(1, 100)]
    [int]$SuccessMarkerCount = 1,
    [string]$FailureMarker = "AUTOMATION_TEST_FAILURE",
    [string[]]$ServerExtraArgs = @(),
    [string[]]$ClientExtraArgs = @()
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

function Test-LogContains
{
    param(
        [string]$Path,
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return $false
    }

    return [bool](Select-String -LiteralPath $Path -Pattern $Pattern -Quiet)
}

function Get-LogMatchCount
{
    param(
        [string]$Path,
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return 0
    }

    $matchingLines = @(
        Select-String -LiteralPath $Path -Pattern $Pattern -AllMatches
    )
    return [int]$matchingLines.Count
}

function Wait-ForLogPattern
{
    param(
        [System.Diagnostics.Process]$Process,
        [string]$LogPath,
        [string]$Pattern,
        [int]$TimeoutSeconds,
        [string]$Description
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline)
    {
        $Process.Refresh()
        if ($Process.HasExited)
        {
            throw "$Description process exited early with code $($Process.ExitCode). Log: $LogPath"
        }
        if (Test-LogContains -Path $LogPath -Pattern $Pattern)
        {
            return
        }
        Start-Sleep -Milliseconds 500
    }

    throw "Timed out waiting for $Description. Log: $LogPath"
}

function Stop-LaunchedProcess
{
    param([System.Diagnostics.Process]$Process)

    if ($null -eq $Process)
    {
        return
    }

    $Process.Refresh()
    if (-not $Process.HasExited)
    {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit(10000) | Out-Null
    }
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
$sessionRoot = Join-Path $projectRoot "Saved\Automation\Network\$timestamp"
New-Item -ItemType Directory -Path $sessionRoot -Force | Out-Null

$serverLog = Join-Path $sessionRoot "Server.log"
$serverProcess = $null
$clientProcesses = New-Object System.Collections.Generic.List[System.Diagnostics.Process]
$allLogs = New-Object System.Collections.Generic.List[string]
$allLogs.Add($serverLog)

$commonArguments = @(
    "-unattended",
    "-nop4",
    "-nosplash",
    "-NoSound",
    "-NullRHI",
    "-DisablePlugins=McpAutomationBridge"
)

try
{
    $serverArguments = @(
        $ProjectPath,
        $MapPath,
        "-server",
        "-port=$Port",
        "-ABSLOG=$serverLog"
    ) + $commonArguments + $ServerExtraArgs

    Write-Host "[Network] Starting server on 127.0.0.1:$Port"
    Write-Host "[Network] Session logs: $sessionRoot"
    $serverProcess = Start-Process `
        -FilePath $editorCommand `
        -ArgumentList (ConvertTo-ProcessArgumentLine $serverArguments) `
        -PassThru `
        -WindowStyle Hidden

    Wait-ForLogPattern `
        -Process $serverProcess `
        -LogPath $serverLog `
        -Pattern "IpNetDriver.*listening on port $Port" `
        -TimeoutSeconds $StartupTimeoutSeconds `
        -Description "server startup"

    Write-Host "[Network] Server is listening."

    for ($clientIndex = 1; $clientIndex -le $ClientCount; ++$clientIndex)
    {
        $clientLog = Join-Path $sessionRoot "Client${clientIndex}.log"
        $allLogs.Add($clientLog)
        $clientArguments = @(
            $ProjectPath,
            "127.0.0.1:$Port",
            "-game",
            "-client",
            "-ABSLOG=$clientLog"
        ) + $commonArguments + $ClientExtraArgs

        Write-Host "[Network] Starting client $clientIndex of $ClientCount"
        $clientProcess = Start-Process `
            -FilePath $editorCommand `
            -ArgumentList (ConvertTo-ProcessArgumentLine $clientArguments) `
            -PassThru `
            -WindowStyle Hidden
        $clientProcesses.Add($clientProcess)

        Wait-ForLogPattern `
            -Process $clientProcess `
            -LogPath $clientLog `
            -Pattern "Welcomed by server" `
            -TimeoutSeconds $StartupTimeoutSeconds `
            -Description "client $clientIndex connection"

        Write-Host "[Network] Client $clientIndex connected."
    }

    $deadline = (Get-Date).AddSeconds($SessionDurationSeconds)
    $successFound = [string]::IsNullOrWhiteSpace($SuccessMarker)
    $successMatchCount = 0
    while ((Get-Date) -lt $deadline)
    {
        $serverProcess.Refresh()
        if ($serverProcess.HasExited)
        {
            throw "Server exited during the session. Log: $serverLog"
        }

        foreach ($clientProcess in $clientProcesses)
        {
            $clientProcess.Refresh()
            if ($clientProcess.HasExited)
            {
                throw "A client exited during the session. Logs: $sessionRoot"
            }
        }

        $successMatchCount = 0
        foreach ($logPath in $allLogs)
        {
            if (-not [string]::IsNullOrWhiteSpace($FailureMarker) -and
                (Test-LogContains -Path $logPath -Pattern $FailureMarker))
            {
                throw "Failure marker '$FailureMarker' was found. Log: $logPath"
            }
            if (-not [string]::IsNullOrWhiteSpace($SuccessMarker))
            {
                $successMatchCount += Get-LogMatchCount -Path $logPath -Pattern $SuccessMarker
            }
        }

        $successFound = [string]::IsNullOrWhiteSpace($SuccessMarker) -or
            $successMatchCount -ge $SuccessMarkerCount

        if ($successFound -and -not [string]::IsNullOrWhiteSpace($SuccessMarker))
        {
            break
        }

        Start-Sleep -Milliseconds 500
    }

    if (-not $successFound)
    {
        throw "Found $successMatchCount of $SuccessMarkerCount required success marker(s) '$SuccessMarker'. Logs: $sessionRoot"
    }

    Write-Host "[Passed] Server and $ClientCount client(s) completed the network session."
}
finally
{
    foreach ($clientProcess in $clientProcesses)
    {
        Stop-LaunchedProcess -Process $clientProcess
    }
    Stop-LaunchedProcess -Process $serverProcess
    Write-Host "[Network] Launched processes were stopped."
}
