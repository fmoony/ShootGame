[CmdletBinding()]
param(
	[ValidateSet('Blender', 'Sample', 'Import', 'Measure')][string]$Mode = 'Measure',
	[string]$Job = '',
	[string]$Log = '',
	[switch]$Down50,
	[switch]$Production
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$projectPath = Join-Path $projectRoot 'ShootGame.uproject'
if ($Mode -eq 'Blender')
{
	if (!$Job) { throw 'Blender requires a job JSON.' }
	& 'D:/Blender/blender-4.5.8-windows-x64/blender.exe' `
		'--background' '--factory-startup' '--python-exit-code' '1' `
		'--python' (Join-Path $PSScriptRoot 'blender_down.py') '--' $Job
}
else
{
	$engineArgs = @(
		$projectPath,
		'-DDC=NoZenLocalFallback',
		"-LocalDataCachePath=$projectRoot/Saved/RifleTools/DDC",
		'-unattended',
		'-nop4',
		'-nosplash',
		'-nullrhi')
	if ($Mode -eq 'Measure')
	{
		$engineArgs += '-run=ShooterRifleSegmented'
		if ($Production) { $engineArgs += '-ProductionMeasure' }
		if ($Down50) { $engineArgs += '-Down50' }
	}
	else
	{
		if (!$Job) { throw 'Sample / Import requires a job JSON.' }
		$jobData = Get-Content -LiteralPath $Job -Raw | ConvertFrom-Json
		if ($jobData.operation -ne $Mode.ToLower()) { throw 'Mode and operation differ.' }
		$engineArgs += @('-run=PythonScript', '-EnablePlugins=PythonScriptPlugin')
		$engineArgs += "-script=$PSScriptRoot/ue_job.py"
		$engineArgs += "-ToolConfig=$Job"
	}
	if ($Log) { $engineArgs += "-abslog=$Log" }
	& 'E:/Unreal_Engine/UE_5.6/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' @engineArgs
}
if ($LASTEXITCODE -ne 0) { throw "Rifle tool failed: $LASTEXITCODE" }
