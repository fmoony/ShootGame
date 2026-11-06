[CmdletBinding()]
param([string]$EngineRoot = "")

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$EngineRoot = & (Join-Path $PSScriptRoot "ResolveShootGameEngine.ps1") -EngineRoot $EngineRoot
$editorPath = Join-Path $EngineRoot "Engine/Binaries/Win64/UnrealEditor.exe"
$projectPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "../../ShootGame.uproject")).Path
& $editorPath $projectPath
