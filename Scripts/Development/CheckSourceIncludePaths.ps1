param(
	[string]$SourceRoot = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($SourceRoot))
{
	$SourceRoot = Join-Path $ProjectRoot "Source\ShootGame"
}

$ResolvedSourceRoot = (Resolve-Path $SourceRoot).Path
$HeaderFiles = Get-ChildItem -LiteralPath $ResolvedSourceRoot -Recurse -File -Filter "*.h"
$SourceFiles = Get-ChildItem -LiteralPath $ResolvedSourceRoot -Recurse -File |
	Where-Object { $_.Extension -in ".h", ".cpp" }
$HeadersByName = @{}
$HeaderPaths = @{}

foreach ($HeaderFile in $HeaderFiles)
{
	$RelativePath = $HeaderFile.FullName.Substring($ResolvedSourceRoot.Length + 1).Replace("\", "/")
	$HeaderPaths[$RelativePath] = $true
	if (-not $HeadersByName.ContainsKey($HeaderFile.Name))
	{
		$HeadersByName[$HeaderFile.Name] = @()
	}

	$HeadersByName[$HeaderFile.Name] += $HeaderFile
}

$Errors = [System.Collections.Generic.List[string]]::new()
foreach ($SourceFile in $SourceFiles)
{
	$RelativeSourcePath = $SourceFile.FullName.Substring($ResolvedSourceRoot.Length + 1).Replace("\", "/")
	$LineNumber = 0
	foreach ($Line in [IO.File]::ReadLines($SourceFile.FullName))
	{
		++$LineNumber
		if ($Line -notmatch '^\s*#include\s+"([^"]+)"')
		{
			continue
		}

		$IncludePath = $Matches[1]
		if ($IncludePath.EndsWith(".generated.h"))
		{
			continue
		}

		$IncludeName = [IO.Path]::GetFileName($IncludePath)
		if (-not $HeadersByName.ContainsKey($IncludeName))
		{
			continue
		}

		if ($IncludePath.Contains("\") -or $IncludePath.StartsWith("./") -or $IncludePath.Contains("../"))
		{
			$Errors.Add("${RelativeSourcePath}:${LineNumber}: 项目头文件禁止使用相对路径：$IncludePath")
			continue
		}

		if ($IncludePath.Contains("/"))
		{
			if (-not $HeaderPaths.ContainsKey($IncludePath))
			{
				$Errors.Add("${RelativeSourcePath}:${LineNumber}: 项目头文件路径不存在：$IncludePath")
			}

			continue
		}

		$Targets = @($HeadersByName[$IncludeName])
		$IsOwnHeader = $SourceFile.Extension -eq ".cpp" -and $Targets.Count -eq 1 -and
			$SourceFile.BaseName -eq $Targets[0].BaseName -and
			$SourceFile.DirectoryName -eq $Targets[0].DirectoryName
		$IsModuleRootHeader = $Targets.Count -eq 1 -and $Targets[0].DirectoryName -eq $ResolvedSourceRoot
		if (-not $IsOwnHeader -and -not $IsModuleRootHeader)
		{
			$Errors.Add("${RelativeSourcePath}:${LineNumber}: 跨目录项目头文件必须使用模块根路径：$IncludePath")
		}
	}
}

if ($Errors.Count -gt 0)
{
	$Errors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
	Write-Host "Source include path check failed. Errors=$($Errors.Count)" -ForegroundColor Red
	exit 1
}

Write-Host "Source include path check passed. Files=$($SourceFiles.Count) Headers=$($HeaderFiles.Count)"
