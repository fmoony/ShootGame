[CmdletBinding()]
param(
    [string]$EngineRoot = "E:\Unreal_Engine\UE_5.6",
    [string]$ProjectPath = "",
    [string]$TestFilter = "ShootGame",
    [string]$MapPath = "/Game/Shooter/Maps/Lvl_Shooter",
    [ValidateRange(1, 8)]
    [int]$ClientCount = 2,
    [ValidateRange(1024, 65535)]
    [int]$Port = 17777,
    [ValidateRange(1, 600)]
    [int]$SessionDurationSeconds = 45,
    [switch]$WithRendering
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ProjectPath))
{
    $ProjectPath = Join-Path $projectRoot "ShootGame.uproject"
}
$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runRoot = Join-Path $projectRoot "Saved\Automation\Runs\$timestamp"
$summaryPath = Join-Path $runRoot "Summary.json"
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

$summary = [ordered]@{
    startedAt = (Get-Date).ToString("o")
    finishedAt = $null
    status = "Running"
    project = $ProjectPath
    testFilter = $TestFilter
    stages = @()
    error = $null
}

function Save-RunSummary
{
    $summary.finishedAt = (Get-Date).ToString("o")
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
}

function Invoke-TestStage
{
    param(
        [string]$Name,
        [string]$ScriptPath,
        [hashtable]$Arguments
    )

    $stageStartedAt = Get-Date
    $stage = [ordered]@{
        name = $Name
        status = "Running"
        startedAt = $stageStartedAt.ToString("o")
        durationSeconds = 0.0
        error = $null
    }

    Write-Host ""
    Write-Host "========== $Name =========="

    try
    {
        & $ScriptPath @Arguments
        $stage.status = "Passed"
    }
    catch
    {
        $stage.status = "Failed"
        $stage.error = $_.Exception.Message
        throw
    }
    finally
    {
        $stage.durationSeconds = [Math]::Round(((Get-Date) - $stageStartedAt).TotalSeconds, 2)
        $summary.stages += [PSCustomObject]$stage
        Save-RunSummary
    }
}

$buildScript = Join-Path $PSScriptRoot "BuildEditor.ps1"
$automationScript = Join-Path $PSScriptRoot "RunAutomation.ps1"
$networkScript = Join-Path $PSScriptRoot "RunNetworkSession.ps1"
$abilityCleanupScript = Join-Path $PSScriptRoot "RunAbilityCleanup.ps1"
$standaloneScript = Join-Path $PSScriptRoot "RunStandaloneSmoke.ps1"

try
{
    Invoke-TestStage -Name "Build" -ScriptPath $buildScript -Arguments @{
        EngineRoot = $EngineRoot
        ProjectPath = $ProjectPath
    }

    $automationArguments = @{
        EngineRoot = $EngineRoot
        ProjectPath = $ProjectPath
        TestFilter = $TestFilter
    }
    if ($WithRendering)
    {
        $automationArguments.WithRendering = $true
    }
    Invoke-TestStage -Name "Automation" -ScriptPath $automationScript -Arguments $automationArguments

    Invoke-TestStage -Name "Standalone" -ScriptPath $standaloneScript -Arguments @{
        EngineRoot = $EngineRoot
        ProjectPath = $ProjectPath
        MapPath = $MapPath
    }

    Invoke-TestStage -Name "DedicatedNetwork" -ScriptPath $networkScript -Arguments @{
        EngineRoot = $EngineRoot
        ProjectPath = $ProjectPath
        MapPath = $MapPath
        ClientCount = $ClientCount
        Port = $Port
        SessionDurationSeconds = $SessionDurationSeconds
        SuccessMarker = "AUTOMATION_TEST_CLIENT_SUCCESS"
        SuccessMarkerCount = $ClientCount
        ServerExtraArgs = @("-ShootGameNetworkTest")
    }

    Invoke-TestStage -Name "ListenNetwork" -ScriptPath $networkScript -Arguments @{
        EngineRoot = $EngineRoot
        ProjectPath = $ProjectPath
        MapPath = $MapPath
        ClientCount = 1
        ServerMode = "Listen"
        Port = $Port + 1
        SessionDurationSeconds = [Math]::Max($SessionDurationSeconds, 35)
        SuccessMarker = "AUTOMATION_TEST_CLIENT_SUCCESS"
        SuccessMarkerCount = 1
        ServerExtraArgs = @(
            "-ShootGameNetworkTest",
            "-ShootGameSkipRemoteMontage",
            "-ShootGameSkipRemoteCurrentWeapon"
        )
        ClientExtraArgs = @(
            "-ShootGameSkipRemoteCurrentWeapon"
        )
    }

    Invoke-TestStage -Name "EmulatedNetwork" -ScriptPath $networkScript -Arguments @{
        EngineRoot = $EngineRoot
        ProjectPath = $ProjectPath
        MapPath = $MapPath
        ClientCount = $ClientCount
        Port = $Port + 2
        SessionDurationSeconds = [Math]::Max($SessionDurationSeconds, 60)
        SuccessMarker = "AUTOMATION_TEST_CLIENT_SUCCESS"
        SuccessMarkerCount = $ClientCount
        ServerExtraArgs = @(
            "-ShootGameNetworkTest",
            "-ShootGameSkipRemoteMontage",
            "-PktLag=100",
            "-PktLoss=2"
        )
        ClientExtraArgs = @(
            "-PktLag=100",
            "-PktLoss=2"
        )
    }

    Invoke-TestStage -Name "DisconnectCleanup" -ScriptPath $abilityCleanupScript -Arguments @{
        EngineRoot = $EngineRoot
        ProjectPath = $ProjectPath
        MapPath = $MapPath
        Port = $Port + 3
        SessionDurationSeconds = [Math]::Max($SessionDurationSeconds, 45)
    }

    $summary.status = "Passed"
    Write-Host ""
    Write-Host "[Passed] Full validation completed."
}
catch
{
    $summary.status = "Failed"
    $summary.error = $_.Exception.Message
    Write-Host ""
    Write-Host "[Failed] Full validation stopped: $($summary.error)"
    throw
}
finally
{
    Save-RunSummary
    Write-Host "[Summary] $summaryPath"
}
