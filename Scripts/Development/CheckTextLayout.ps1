param(
	[ValidateSet("Staged", "WorkingTree", "All")]
	[string]$Scope = "Staged"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Get-DisplayWidth
{
	param([string]$Text)

	$Width = 0
	foreach ($Character in $Text.ToCharArray())
	{
		$CodePoint = [int]$Character
		if ($Character -eq "`t")
		{
			$Width += 4 - ($Width % 4)
			continue
		}

		$IsWide =
			($CodePoint -ge 0x1100 -and $CodePoint -le 0x115F) -or
			($CodePoint -ge 0x2E80 -and $CodePoint -le 0xA4CF) -or
			($CodePoint -ge 0xAC00 -and $CodePoint -le 0xD7A3) -or
			($CodePoint -ge 0xF900 -and $CodePoint -le 0xFAFF) -or
			($CodePoint -ge 0xFE10 -and $CodePoint -le 0xFE6F) -or
			($CodePoint -ge 0xFF00 -and $CodePoint -le 0xFF60) -or
			($CodePoint -ge 0xFFE0 -and $CodePoint -le 0xFFE6)
		if ($IsWide)
		{
			$Width += 2
		}
		else
		{
			$Width += 1
		}
	}

	return $Width
}

function Get-ChangedLines
{
	param([string]$DiffScope)

	$Arguments = @("-c", "core.quotepath=false", "diff")
	if ($DiffScope -eq "Staged")
	{
		$Arguments += "--cached"
	}
	$Arguments += @(
		"--unified=0",
		"--no-color",
		"--diff-filter=ACMR",
		"--",
		"*.h",
		"*.cpp",
		"*.cs",
		"*.ps1",
		"*.md")

	$ChangedLines = @{}
	$CurrentFile = $null
	$NewLineNumber = 0
	foreach ($DiffLine in (& git @Arguments))
	{
		if ($DiffLine -match '^\+\+\+ b/(.+)$')
		{
			$CurrentFile = $Matches[1]
			if (!$ChangedLines.ContainsKey($CurrentFile))
			{
				$ChangedLines[$CurrentFile] = [System.Collections.Generic.HashSet[int]]::new()
			}
			continue
		}

		if ($DiffLine -match '^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@')
		{
			$NewLineNumber = [int]$Matches[1]
			continue
		}

		if (!$CurrentFile -or $DiffLine.StartsWith("diff --git"))
		{
			continue
		}

		if ($DiffLine.StartsWith("+") -and !$DiffLine.StartsWith("+++"))
		{
			[void]$ChangedLines[$CurrentFile].Add($NewLineNumber)
			++$NewLineNumber
		}
		elseif (!$DiffLine.StartsWith("-") -and !$DiffLine.StartsWith("\"))
		{
			++$NewLineNumber
		}
	}

	return $ChangedLines
}

function Get-AllLines
{
	$AllLines = @{}
	$Files = & rg --files Source Plugins Scripts Docs AGENTS.md `
		-g "*.h" -g "*.cpp" -g "*.cs" -g "*.ps1" -g "*.md"
	foreach ($File in $Files)
	{
		$FullPath = Join-Path $ProjectRoot $File
		if (!(Test-Path -LiteralPath $FullPath))
		{
			continue
		}

		$LineCount = (Get-Content -LiteralPath $FullPath -Encoding UTF8).Count
		$AllLines[$File] = [System.Collections.Generic.HashSet[int]]::new()
		for ($LineNumber = 1; $LineNumber -le $LineCount; ++$LineNumber)
		{
			[void]$AllLines[$File].Add($LineNumber)
		}
	}

	return $AllLines
}

Push-Location $ProjectRoot
try
{
	$LinesToCheck = if ($Scope -eq "All") { Get-AllLines } else { Get-ChangedLines $Scope }
	$Failures = [System.Collections.Generic.List[string]]::new()

	foreach ($RelativePath in ($LinesToCheck.Keys | Sort-Object))
	{
		$FullPath = Join-Path $ProjectRoot $RelativePath
		if (!(Test-Path -LiteralPath $FullPath))
		{
			continue
		}

		$Extension = [IO.Path]::GetExtension($RelativePath).ToLowerInvariant()
		$IsMarkdown = $Extension -eq ".md"
		$InFence = $false
		$LineNumber = 0
		foreach ($Line in Get-Content -LiteralPath $FullPath -Encoding UTF8)
		{
			++$LineNumber
			$IsFenceLine = $IsMarkdown -and $Line -match '^\s*(```|~~~)'
			if (!$LinesToCheck[$RelativePath].Contains($LineNumber))
			{
				if ($IsFenceLine)
				{
					$InFence = !$InFence
				}
				continue
			}

			if ($Line -match '[ \t]+$')
			{
				$Failures.Add("${RelativePath}:${LineNumber}: trailing whitespace")
			}

			$IsBlockCommentContinuation = $Line -match '^ \*(?:/|\s|$)'
			if (!$IsMarkdown -and !$IsBlockCommentContinuation -and $Line -match '^ +\S')
			{
				$Failures.Add("${RelativePath}:${LineNumber}: code indentation must use tabs")
			}

			$Limit = if ($IsMarkdown -and !$InFence) { 100 } else { 120 }
			$IsExplicitException = $Line -match 'https?://' -or $Line -match 'text-layout-ignore:'
			$Width = Get-DisplayWidth $Line
			if (!$IsExplicitException -and $Width -gt $Limit)
			{
				$Failures.Add("${RelativePath}:${LineNumber}: display width ${Width} exceeds ${Limit}")
			}

			if ($IsFenceLine)
			{
				$InFence = !$InFence
			}
		}
	}

	if ($Failures.Count -gt 0)
	{
		$Failures | ForEach-Object { Write-Host $_ -ForegroundColor Red }
		exit 1
	}

	Write-Host "Text layout check passed. Scope=$Scope Files=$($LinesToCheck.Count)"
}
finally
{
	Pop-Location
}
