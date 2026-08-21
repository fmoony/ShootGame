[CmdletBinding()]
param(
    [string]$EngineRoot = "E:\Unreal_Engine\UE_5.6",
    [string]$ProjectPath = "",
    [string]$MapPath = "/Game/Shooter/Maps/Lvl_Shooter",
    [ValidateRange(1024, 65534)]
    [int]$Port = 17780,
    [ValidateRange(10, 300)]
    [int]$SessionDurationSeconds = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$networkScript = Join-Path $PSScriptRoot "RunNetworkSession.ps1"

Write-Host "[AbilityCleanup] Verifying active GA_Reload cancellation on disconnect."
& $networkScript `
    -EngineRoot $EngineRoot `
    -ProjectPath $ProjectPath `
    -MapPath $MapPath `
    -ClientCount 2 `
    -Port $Port `
    -SessionDurationSeconds $SessionDurationSeconds `
    -SuccessMarker "AUTOMATION_TEST_DISCONNECT_SUCCESS" `
    -SuccessMarkerCount 1 `
    -DisconnectClientIndex 1 `
    -DisconnectReadyMarker "AUTOMATION_TEST_DISCONNECT_RELOAD_READY" `
    -DisconnectReadyMarkerCount 2 `
    -ServerExtraArgs @(
        "-ShootGameNetworkTest",
        "-ShootGameDisconnectTest"
    )

Write-Host "[AbilityCleanup] Verifying active GA_Equip cancellation on death and disconnect."
& $networkScript `
    -EngineRoot $EngineRoot `
    -ProjectPath $ProjectPath `
    -MapPath $MapPath `
    -ClientCount 2 `
    -Port ($Port + 1) `
    -SessionDurationSeconds $SessionDurationSeconds `
    -SuccessMarker "AUTOMATION_TEST_EQUIP_CLEANUP_SUCCESS" `
    -SuccessMarkerCount 2 `
    -DisconnectClientIndex 1 `
    -DisconnectReadyMarker "AUTOMATION_TEST_DISCONNECT_EQUIP_READY" `
    -DisconnectReadyMarkerCount 1 `
    -ServerExtraArgs @(
        "-ShootGameNetworkTest",
        "-ShootGameDisconnectTest",
        "-ShootGameDisconnectEquip"
    )

Write-Host "[Passed] Ability cleanup sessions completed."
