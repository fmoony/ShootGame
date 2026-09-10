[CmdletBinding()]
param(
    [string]$EngineRoot = "E:\Unreal_Engine\UE_5.6",
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Label = "Current",
    [ValidateRange(0.1, 10.0)]
    [double]$NearClip = 1.0,
    [ValidateRange(0.0, 50.0)]
    [double]$Clearance = 8.0,
    [switch]$Exercise,
    [switch]$PistolReview,
    [switch]$LightingOnly,
    [ValidateRange(-1.0, 50.0)]
    [double]$CompositionDrop = -1.0,
    [int]$TimeoutSeconds = 600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$projectPath = Join-Path $projectRoot "ShootGame.uproject"
$outputRoot = Join-Path $projectRoot "Saved\Automation\FirstPersonCapture\$Label"
if (Test-Path -LiteralPath $outputRoot) { throw "Capture label already exists: $outputRoot" }
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$logPath = Join-Path $outputRoot "Game.log"
$clipText = $NearClip.ToString([Globalization.CultureInfo]::InvariantCulture)
$clearanceText = $Clearance.ToString([Globalization.CultureInfo]::InvariantCulture)
$captureMode = if ($Exercise) { " exercise" } elseif ($PistolReview) { " pistolreview" } else { "" }
if ($Exercise -and $PistolReview) { throw "Exercise and PistolReview are mutually exclusive." }
$dropText = $CompositionDrop.ToString([Globalization.CultureInfo]::InvariantCulture)
$materialCommand = if ($LightingOnly) { ",ShowFlag.Materials 0" } else { "" }
$arguments = @(
    "`"$projectPath`"", "/Game/Shooter/Maps/Lvl_Shooter", "-game", "-RenderOffscreen",
    "-windowed", "-ResX=1280", "-ResY=720", "-unattended", "-NoSound", "-nosplash", "-UseFixedTimeStep", "-FPS=30",
    "-DDC-ForceMemoryCache", "-DisablePlugins=McpAutomationBridge", "-ShootGameFirstPersonCapture",
    "`"-ABSLOG=$logPath`"", "-ShootGameCaptureDrop=$dropText",
    "`"-ExecCmds=t.MaxFPS 30,r.MotionBlurQuality 0$materialCommand,ShootGame.FirstPerson.NearClip $clipText,ShootGame.FirstPerson.Clearance $clearanceText,ShootGame.FirstPerson.Capture $Label$captureMode`""
)
$process = Start-Process -FilePath (Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe") `
    -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.Id | Set-Content -LiteralPath (Join-Path $outputRoot "ProcessId.txt")
try {
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) { throw "Capture timed out: $logPath" }
    if (-not (Select-String -LiteralPath $logPath -SimpleMatch "FIRST_PERSON_CAPTURE_COMPLETE" -Quiet)) {
        throw "Capture did not finish: $logPath"
    }
    $rows = Import-Csv -LiteralPath (Join-Path $outputRoot "Depth.csv")
    # 动画自身允许不同于参考骨架；Proxy 在每帧比较修正前后实际骨长、手位、视点与肘向。
    if (Select-String -LiteralPath $logPath -SimpleMatch "FIRST_PERSON_POSE_FAILURE" -Quiet) {
        throw "First-person pose invariants failed: $logPath"
    }
    $reloadCases = @($rows | Where-Object { $_.reloading -eq '1' } | Group-Object weapon,pitch)
    $expectedCases = if ($Exercise) { 4 } elseif ($PistolReview) { 10 } else { 28 }
    if ($reloadCases.Count -ne $expectedCases) { throw "Expected $expectedCases reload cases; got $($reloadCases.Count): $logPath" }
    if ($Exercise) {
        foreach ($weaponRows in ($rows | Group-Object weapon)) {
            $beforeFire = @($weaponRows.Group | Where-Object { [double]$_.time -lt 1.0 })
            $afterFire = @($weaponRows.Group | Where-Object { [double]$_.time -gt 1.4 -and [double]$_.time -lt 2.0 })
            if ($beforeFire.Count -eq 0 -or $afterFire.Count -eq 0 -or
                [int]$afterFire[-1].ammo -ge [int]$beforeFire[0].ammo) {
                throw "Firing did not consume ammunition: $($weaponRows.Name)"
            }
        }
    }
    Write-Host "[Passed] Captured all $expectedCases reload cases: $outputRoot"
    Write-Host "Visual inspection of PNG files is required; this is not an automatic no-clipping verdict."
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
}
