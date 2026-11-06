[CmdletBinding()]
param(
	[string]$EngineRoot = "",
	[switch]$RegisterAssociation
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$projectPath = Join-Path $PSScriptRoot "../../ShootGame.uproject"
$association = (Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json).EngineAssociation
$registryPath = "HKCU:\Software\Epic Games\Unreal Engine\Builds"
$registeredRoot = Get-ItemPropertyValue -LiteralPath $registryPath -Name $association -ErrorAction SilentlyContinue

if (-not $EngineRoot)
{
	$EngineRoot = $env:SHOOTGAME_ENGINE_ROOT
}
if (-not $EngineRoot)
{
	$EngineRoot = $registeredRoot
}
if (-not $EngineRoot)
{
	throw "Specify -EngineRoot, set SHOOTGAME_ENGINE_ROOT, or register the project's engine association."
}

$resolvedRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$versionPath = Join-Path $resolvedRoot "Engine/Build/Build.version"
$version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json
$editorPath = Join-Path $resolvedRoot "Engine/Binaries/Win64/UnrealEditor.exe"
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8 -or $version.PatchVersion -ne 4 -or
	-not (Test-Path -LiteralPath $editorPath -PathType Leaf))
{
	throw "ShootGame requires a built UE 5.8.4 Windows editor."
}

if ($RegisterAssociation)
{
	if ($registeredRoot -and ((Resolve-Path -LiteralPath $registeredRoot).Path -ne $resolvedRoot))
	{
		throw "The project engine association is already registered to a different directory."
	}
	if (-not $registeredRoot)
	{
		New-Item -Path $registryPath -Force | Out-Null
		New-ItemProperty -LiteralPath $registryPath -Name $association -Value $resolvedRoot -PropertyType String | Out-Null
	}
}

$resolvedRoot
