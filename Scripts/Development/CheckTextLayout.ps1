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

function Add-DisplayColumn
{
	param([int]$Column, [char]$Character)

	if ($Character -eq "`t")
	{
		return $Column + 4 - ($Column % 4)
	}

	$CodePoint = [int]$Character
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
		return $Column + 2
	}

	return $Column + 1
}

function Update-BlockCommentState
{
	param(
		[string]$Line,
		[bool]$InBlockComment,
		[string]$Extension)

	$IsPowerShell = $Extension -eq '.ps1'
	$InString = $false
	$StringChar = [char]0
	$Escaped = $false
	$Index = 0
	$Length = $Line.Length

	while ($Index -lt $Length)
	{
		$Ch = $Line[$Index]

		if ($InString)
		{
			if ($Escaped)
			{
				$Escaped = $false
			}
			elseif ($IsPowerShell)
			{
				if ($Ch -eq '`')
				{
					$Escaped = $true
				}
				elseif ($Ch -eq $StringChar)
				{
					if ($StringChar -eq "'" -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq "'")
					{
						++$Index
					}
					else
					{
						$InString = $false
					}
				}
			}
			else
			{
				if ($Ch -eq '\')
				{
					$Escaped = $true
				}
				elseif ($Ch -eq $StringChar)
				{
					$InString = $false
				}
			}

			++$Index
			continue
		}

		if ($InBlockComment)
		{
			if ($IsPowerShell)
			{
				if ($Ch -eq '#' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '>')
				{
					$InBlockComment = $false
					$Index += 2
				}
				else
				{
					++$Index
				}
			}
			else
			{
				if ($Ch -eq '*' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '/')
				{
					$InBlockComment = $false
					$Index += 2
				}
				else
				{
					++$Index
				}
			}

			continue
		}

		if ($Ch -eq '"' -or $Ch -eq "'")
		{
			$InString = $true
			$StringChar = $Ch
			++$Index
			continue
		}

		if ($IsPowerShell)
		{
			if ($Ch -eq '#')
			{
				break
			}

			if ($Ch -eq '<' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '#')
			{
				$InBlockComment = $true
				$Index += 2
				continue
			}
		}
		else
		{
			if ($Ch -eq '/' -and (($Index + 1) -lt $Length))
			{
				$Next = $Line[$Index + 1]
				if ($Next -eq '/')
				{
					break
				}

				if ($Next -eq '*')
				{
					$InBlockComment = $true
					$Index += 2
					continue
				}
			}
		}

		++$Index
	}

	return $InBlockComment
}

function Test-CommentOnlyLine
{
	param(
		[string]$Line,
		[bool]$StartInBlockComment,
		[string]$Extension)

	if ($StartInBlockComment)
	{
		return $true
	}

	$Trimmed = $Line.Trim()
	if ($Extension -eq '.ps1')
	{
		return $Trimmed.StartsWith('#')
	}

	return $Trimmed.StartsWith('//') -or $Trimmed.StartsWith('/*')
}

function Get-CommentWidthVerdict
{
	param(
		[string]$Line,
		[int]$Width,
		[string]$Extension)

	if ($Width -le 120)
	{
		return [pscustomobject]@{ Status = 'Pass'; Reason = '' }
	}

	$Content = $Line.Trim()
	if ($Extension -eq '.ps1')
	{
		$Content = $Content -replace '^[#\s]+', ''
	}
	else
	{
		$Content = $Content -replace '^[/\*\s]+', ''
	}

	if ($Content -match '^(https?://|/Game/|/Engine/|/Script/)')
	{
		return [pscustomobject]@{ Status = 'Pass'; Reason = 'URL or asset path line is exempt' }
	}

	return [pscustomobject]@{ Status = 'Fail'; Reason = "comment width ${Width} exceeds 120" }
}

function Get-CodeWidthPolicy
{
	param(
		[string]$Line,
		[int]$Width,
		[bool]$StartInBlockComment,
		[string]$Extension)

	$IsPowerShell = $Extension -eq '.ps1'
	$InBlockComment = $StartInBlockComment
	$InString = $false
	$StringChar = [char]0
	$Escaped = $false
	$ParenDepth = 0
	$BraceDepth = 0
	$BracketDepth = 0
	$Pairs = [System.Collections.Generic.List[object]]::new()
	$OpenStack = [System.Collections.Generic.List[object]]::new()
	$Commas = [System.Collections.Generic.List[object]]::new()
	$CommentSpans = [System.Collections.Generic.List[object]]::new()
	$CommentStart = -1
	$HasAngle = $false
	$HasRawString = $Line -match 'R"'
	$Unbalanced = $false
	$Col = 0
	$Index = 0
	$Length = $Line.Length

	while ($Index -lt $Length)
	{
		$Ch = $Line[$Index]

		if ($InString)
		{
			if ($Escaped)
			{
				$Escaped = $false
			}
			elseif ($IsPowerShell)
			{
				if ($Ch -eq '`')
				{
					$Escaped = $true
				}
				elseif ($Ch -eq $StringChar)
				{
					if ($StringChar -eq "'" -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq "'")
					{
						$Col = Add-DisplayColumn $Col $Ch
						++$Index
						$Ch = $Line[$Index]
					}
					else
					{
						$InString = $false
					}
				}
			}
			else
			{
				if ($Ch -eq '\')
				{
					$Escaped = $true
				}
				elseif ($Ch -eq $StringChar)
				{
					$InString = $false
				}
			}

			$Col = Add-DisplayColumn $Col $Ch
			++$Index
			continue
		}

		if ($InBlockComment)
		{
			if ($IsPowerShell)
			{
				if ($Ch -eq '#' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '>')
				{
					$CommentSpans.Add([pscustomobject]@{ Start = $CommentStart; End = $Col })
					$InBlockComment = $false
					$Index += 2
					$Col = Add-DisplayColumn $Col '#'
					$Col = Add-DisplayColumn $Col '>'
					continue
				}
			}
			elseif ($Ch -eq '*' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '/')
			{
				$CommentSpans.Add([pscustomobject]@{ Start = $CommentStart; End = $Col })
				$InBlockComment = $false
				$Index += 2
				$Col = Add-DisplayColumn $Col '*'
				$Col = Add-DisplayColumn $Col '/'
				continue
			}

			$Col = Add-DisplayColumn $Col $Ch
			++$Index
			continue
		}

		if ($IsPowerShell)
		{
			if ($Ch -eq '#')
			{
				$CommentSpans.Add([pscustomobject]@{ Start = $Col; End = -1 })
				break
			}

			if ($Ch -eq '<' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '#')
			{
				$CommentStart = $Col
				$InBlockComment = $true
				$Index += 2
				$Col = Add-DisplayColumn $Col '<'
				$Col = Add-DisplayColumn $Col '#'
				continue
			}
		}
		else
		{
			if ($Ch -eq '/' -and (($Index + 1) -lt $Length))
			{
				$Next = $Line[$Index + 1]
				if ($Next -eq '/')
				{
					$CommentSpans.Add([pscustomobject]@{ Start = $Col; End = -1 })
					break
				}

				if ($Next -eq '*')
				{
					$CommentStart = $Col
					$InBlockComment = $true
					$Index += 2
					$Col = Add-DisplayColumn $Col '/'
					$Col = Add-DisplayColumn $Col '*'
					continue
				}
			}
		}

		if ($Ch -eq '"' -or $Ch -eq "'")
		{
			$InString = $true
			$StringChar = $Ch
			$Col = Add-DisplayColumn $Col $Ch
			++$Index
			continue
		}

		switch ($Ch)
		{
			'('
			{
				$OpenStack.Add([pscustomobject]@{
					ContentDepth = $ParenDepth + 1
					BaseBrace = $BraceDepth
					BaseBracket = $BracketDepth
					Col = $Col })
				++$ParenDepth
			}
			')'
			{
				if ($ParenDepth -gt 0 -and $OpenStack.Count -gt 0)
				{
					$Item = $OpenStack[$OpenStack.Count - 1]
					$OpenStack.RemoveAt($OpenStack.Count - 1)
					$Pairs.Add([pscustomobject]@{
						ContentDepth = $Item.ContentDepth
						BaseBrace = $Item.BaseBrace
						BaseBracket = $Item.BaseBracket
						OpenCol = $Item.Col
						CloseCol = $Col })
					--$ParenDepth
				}
				else
				{
					$Unbalanced = $true
				}
			}
			'{' { ++$BraceDepth }
			'}'
			{
				if ($BraceDepth -gt 0) { --$BraceDepth }
				else { $Unbalanced = $true }
			}
			'[' { ++$BracketDepth }
			']'
			{
				if ($BracketDepth -gt 0) { --$BracketDepth }
				else { $Unbalanced = $true }
			}
			','
			{
				$Commas.Add([pscustomobject]@{
					Depth = $ParenDepth
					Brace = $BraceDepth
					Bracket = $BracketDepth
					Col = $Col })
			}
			'<' { $HasAngle = $true }
			'>' { $HasAngle = $true }
		}

		$Col = Add-DisplayColumn $Col $Ch
		++$Index
	}

	$EndInBlockComment = $InBlockComment
	if ($InBlockComment -and $CommentStart -ge 0)
	{
		$CommentSpans.Add([pscustomobject]@{ Start = $CommentStart; End = -1 })
	}

	if ($InString -or $OpenStack.Count -gt 0)
	{
		$Unbalanced = $true
	}

	if ($Width -gt 140)
	{
		return [pscustomobject]@{
			Status = 'Fail'
			Reason = "code width ${Width} exceeds 140"
			EndInBlockComment = $EndInBlockComment }
	}

	foreach ($Span in $CommentSpans)
	{
		if ($Span.End -lt 0 -or $Span.Start -ge 120 -or $Span.End -ge 120)
		{
			return [pscustomobject]@{
				Status = 'Fail'
				Reason = 'comment portion exceeds 120'
				EndInBlockComment = $EndInBlockComment }
		}
	}

	foreach ($Pair in $Pairs)
	{
		if ($Pair.OpenCol -lt 120 -and $Pair.CloseCol -ge 120)
		{
			$LastCommaCol = -1
			foreach ($Comma in $Commas)
			{
				if ($Comma.Depth -eq $Pair.ContentDepth -and
					$Comma.Brace -eq $Pair.BaseBrace -and
					$Comma.Bracket -eq $Pair.BaseBracket -and
					$Comma.Col -gt $Pair.OpenCol -and
					$Comma.Col -lt $Pair.CloseCol)
				{
					$LastCommaCol = $Comma.Col
				}
			}

			$LastParameterStart = if ($LastCommaCol -ge 0) { $LastCommaCol + 1 } else { $Pair.OpenCol + 1 }
			if ($LastParameterStart -ge 120)
			{
				return [pscustomobject]@{
					Status = 'Fail'
					Reason = "width ${Width} exceeds 120 before the last parameter"
					EndInBlockComment = $EndInBlockComment }
			}
		}
	}

	if ($Unbalanced -or $HasRawString)
	{
		return [pscustomobject]@{
			Status = 'Warn'
			Reason = 'unable to verify last-parameter boundary'
			EndInBlockComment = $EndInBlockComment }
	}

	if ($HasAngle -and $Commas.Count -gt 0)
	{
		return [pscustomobject]@{
			Status = 'Warn'
			Reason = 'angle brackets make parameter boundaries uncertain'
			EndInBlockComment = $EndInBlockComment }
	}

	return [pscustomobject]@{
		Status = 'Pass'
		Reason = ''
		EndInBlockComment = $EndInBlockComment }
}


function Get-LineStructure
{
	param(
		[string]$Line,
		[bool]$StartInBlockComment,
		[string]$Extension)

	$IsPowerShell = $Extension -eq '.ps1'
	$InBlockComment = $StartInBlockComment
	$InString = $false
	$StringChar = [char]0
	$Escaped = $false
	$HasComment = $StartInBlockComment
	$ParenDelta = 0
	$Index = 0
	$Length = $Line.Length

	while ($Index -lt $Length)
	{
		$Ch = $Line[$Index]

		if ($InString)
		{
			if ($Escaped)
			{
				$Escaped = $false
			}
			elseif ($IsPowerShell)
			{
				if ($Ch -eq '`')
				{
					$Escaped = $true
				}
				elseif ($Ch -eq $StringChar)
				{
					if ($StringChar -eq "'" -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq "'")
					{
						++$Index
					}
					else
					{
						$InString = $false
					}
				}
			}
			else
			{
				if ($Ch -eq '\')
				{
					$Escaped = $true
				}
				elseif ($Ch -eq $StringChar)
				{
					$InString = $false
				}
			}

			++$Index
			continue
		}

		if ($InBlockComment)
		{
			$HasComment = $true
			if ($IsPowerShell)
			{
				if ($Ch -eq '#' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '>')
				{
					$InBlockComment = $false
					$Index += 2
					continue
				}
			}
			elseif ($Ch -eq '*' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '/')
			{
				$InBlockComment = $false
				$Index += 2
				continue
			}

			++$Index
			continue
		}

		if ($Ch -eq '"' -or $Ch -eq "'")
		{
			$InString = $true
			$StringChar = $Ch
			++$Index
			continue
		}

		if ($IsPowerShell)
		{
			if ($Ch -eq '#')
			{
				$HasComment = $true
				break
			}

			if ($Ch -eq '<' -and (($Index + 1) -lt $Length) -and $Line[$Index + 1] -eq '#')
			{
				$HasComment = $true
				$InBlockComment = $true
				$Index += 2
				continue
			}
		}
		else
		{
			if ($Ch -eq '/' -and (($Index + 1) -lt $Length))
			{
				$Next = $Line[$Index + 1]
				if ($Next -eq '/')
				{
					$HasComment = $true
					break
				}

				if ($Next -eq '*')
				{
					$HasComment = $true
					$InBlockComment = $true
					$Index += 2
					continue
				}
			}
		}

		if ($Ch -eq '(')
		{
			++$ParenDelta
		}
		elseif ($Ch -eq ')')
		{
			--$ParenDelta
		}

		++$Index
	}

	return [pscustomobject]@{
		HasComment = $HasComment
		ParenDelta = $ParenDelta
		EndInBlockComment = $InBlockComment }
}

function Test-MergeablePair
{
	param([pscustomobject]$Left, [pscustomobject]$Right)

	$LeftTrimmed = $Left.Trim
	$RightTrimmed = $Right.Trim
	if (!$LeftTrimmed -or !$RightTrimmed)
	{
		return 'None'
	}

	if ($RightTrimmed -match '^(\)|,|\.|->|::)')
	{
		return 'Clear'
	}

	if ($LeftTrimmed -match '\($')
	{
		return 'Clear'
	}

	if ($LeftTrimmed -match ',$')
	{
		if ($Left.ParenAtStart -gt 0 -or $Left.ParenDelta -gt 0)
		{
			return 'Clear'
		}

		return 'None'
	}

	if ($LeftTrimmed -match '(\.|->|::|&&|\|\||\?|=|\+|-|\*|/)$')
	{
		return 'Clear'
	}

	if ($LeftTrimmed -match '\breturn$')
	{
		return 'Clear'
	}

	return 'None'
}

function Join-LinePair
{
	param([string]$Left, [string]$Right)

	$LeftTrimmedEnd = $Left.TrimEnd()
	$RightTrimmed = $Right.Trim()
	if ($LeftTrimmedEnd -match '[\(\[{]$')
	{
		return $LeftTrimmedEnd + $RightTrimmed
	}

	return $LeftTrimmedEnd + ' ' + $RightTrimmed
}

function Test-JoinedWidth
{
	param([string]$Text, [string]$Extension)

	$Width = Get-DisplayWidth $Text
	if ($Width -le 120)
	{
		return $true
	}

	if ($Width -le 140)
	{
		$Verdict = Get-CodeWidthPolicy $Text $Width $false $Extension
		return $Verdict.Status -eq 'Pass'
	}

	return $false
}

function Join-LineRange
{
	param([string[]]$Lines, [int]$Start, [int]$End)

	$Current = $Lines[$Start]
	for ($LineIndex = $Start + 1; $LineIndex -le $End; ++$LineIndex)
	{
		$Current = Join-LinePair $Current $Lines[$LineIndex]
	}

	return $Current
}

function Get-ParameterGroupInfo
{
	param([string]$Text)

	$InString = $false
	$StringChar = [char]0
	$Escaped = $false
	$ParenDepth = 0
	$Stack = [System.Collections.Generic.List[object]]::new()
	$Last = $null
	$Index = 0
	$Length = $Text.Length

	while ($Index -lt $Length)
	{
		$Ch = $Text[$Index]

		if ($InString)
		{
			if ($Escaped) { $Escaped = $false }
			elseif ($Ch -eq '\') { $Escaped = $true }
			elseif ($Ch -eq $StringChar) { $InString = $false }
			++$Index
			continue
		}

		if ($Ch -eq '"' -or $Ch -eq "'")
		{
			$InString = $true
			$StringChar = $Ch
			++$Index
			continue
		}

		if ($Ch -eq '(')
		{
			$Stack.Add([pscustomobject]@{
				OpenIndex = $Index
				ContentDepth = $ParenDepth + 1
				CloseIndex = -1 })
			++$ParenDepth
		}
		elseif ($Ch -eq ')' -and $Stack.Count -gt 0)
		{
			$Last = $Stack[$Stack.Count - 1]
			$Last.CloseIndex = $Index
			$Stack.RemoveAt($Stack.Count - 1)
			--$ParenDepth
		}

		++$Index
	}

	return $Last
}

function Test-HasTopLevelComma
{
	param([string]$Text, [object]$GroupInfo)

	$InString = $false
	$StringChar = [char]0
	$Escaped = $false
	$RelativeDepth = 0
	$Index = $GroupInfo.OpenIndex + 1
	$End = $GroupInfo.CloseIndex - 1

	while ($Index -le $End)
	{
		$Ch = $Text[$Index]

		if ($InString)
		{
			if ($Escaped) { $Escaped = $false }
			elseif ($Ch -eq '\') { $Escaped = $true }
			elseif ($Ch -eq $StringChar) { $InString = $false }
			++$Index
			continue
		}

		if ($Ch -eq '"' -or $Ch -eq "'")
		{
			$InString = $true
			$StringChar = $Ch
			++$Index
			continue
		}

		if ($Ch -eq '(') { ++$RelativeDepth }
		elseif ($Ch -eq ')') { --$RelativeDepth }
		elseif ($Ch -eq ',' -and $RelativeDepth -eq 0)
		{
			return $true
		}

		++$Index
	}

	return $false
}

function Get-LineTopCommaState
{
	param([string]$Line, [int]$StartParenDepth, [int]$ContentDepth)

	$InString = $false
	$StringChar = [char]0
	$Escaped = $false
	$ParenDepth = $StartParenDepth
	$Count = 0
	$NotAtEnd = $false
	$Index = 0
	$Length = $Line.Length
	$TrimmedLength = $Line.TrimEnd().Length

	while ($Index -lt $Length)
	{
		$Ch = $Line[$Index]

		if ($InString)
		{
			if ($Escaped) { $Escaped = $false }
			elseif ($Ch -eq '\') { $Escaped = $true }
			elseif ($Ch -eq $StringChar) { $InString = $false }
			++$Index
			continue
		}

		if ($Ch -eq '"' -or $Ch -eq "'")
		{
			$InString = $true
			$StringChar = $Ch
			++$Index
			continue
		}

		if ($Ch -eq '(') { ++$ParenDepth }
		elseif ($Ch -eq ')') { --$ParenDepth }
		elseif ($Ch -eq ',' -and $ParenDepth -eq $ContentDepth)
		{
			++$Count
			if ($Index -lt ($TrimmedLength - 1))
			{
				$NotAtEnd = $true
			}
		}

		++$Index
	}

	return [pscustomobject]@{ Count = $Count; NotAtEnd = $NotAtEnd }
}

function Test-OnePerLineLayout
{
	param(
		[string[]]$Lines,
		[int]$Start,
		[int]$End,
		[object[]]$Structures,
		[int]$ContentDepth)

	for ($LineIndex = $Start; $LineIndex -le $End; ++$LineIndex)
	{
		$State = Get-LineTopCommaState $Lines[$LineIndex] $Structures[$LineIndex].ParenAtStart $ContentDepth
		if ($State.Count -gt 1 -or $State.NotAtEnd)
		{
			return $false
		}
	}

	return $true
}

function Get-RedundantBreaks
{
	param(
		[string[]]$Lines,
		[string]$Extension,
		[System.Collections.Generic.HashSet[int]]$ChangedLines)

	$Structures = [System.Collections.Generic.List[object]]::new()
	$InBlockComment = $false
	$ParenDepth = 0
	foreach ($Line in $Lines)
	{
		$Structure = Get-LineStructure $Line $InBlockComment $Extension
		$Structures.Add([pscustomobject]@{
			HasComment = $Structure.HasComment
			ParenAtStart = $ParenDepth
			ParenDelta = $Structure.ParenDelta
			Trim = $Line.Trim() })
		$InBlockComment = $Structure.EndInBlockComment
		$ParenDepth += $Structure.ParenDelta
	}

	$Breaks = [System.Collections.Generic.List[object]]::new()
	$Index = 0
	while ($Index -lt $Structures.Count)
	{
		$Start = $Structures[$Index]
		if ($Start.HasComment -or !$Start.Trim -or $Start.Trim -match '^[#{}]' -or
			$Start.Trim -match '[;{}]$' -or $Start.Trim.Contains(';'))
		{
			++$Index
			continue
		}

		$Current = $Lines[$Index]
		$EndIndex = $Index
		$Cursor = $Index
		while (($Cursor + 1) -lt $Structures.Count)
		{
			$Next = $Structures[$Cursor + 1]
			if ($Next.HasComment -or !$Next.Trim -or $Next.Trim -match '^[#{}]')
			{
				break
			}

			$LeftContext = [pscustomobject]@{
				Trim = $Current
				ParenAtStart = $Structures[$Cursor].ParenAtStart
				ParenDelta = $Structures[$Cursor].ParenDelta }
			$Mode = Test-MergeablePair $LeftContext $Next
			if ($Mode -eq 'None')
			{
				break
			}

			$Candidate = Join-LinePair $Current $Lines[$Cursor + 1]
			if ($Candidate.Contains(';') -and !$Candidate.TrimEnd().EndsWith(';'))
			{
				break
			}

			$Current = $Candidate
			$EndIndex = $Cursor + 1
			if ($Current.TrimEnd().EndsWith(';'))
			{
				break
			}

			++$Cursor
		}

		if ($EndIndex -eq $Index)
		{
			++$Index
			continue
		}

		$RunText = Join-LineRange $Lines $Index $EndIndex
		$RunLineCount = $EndIndex - $Index + 1
		$Kind = $null
		$Width = 0
		$Split = -1

		$AdjacentLiteralTail = $false
		if ($EndIndex -eq $Index + 1 -and ($EndIndex + 1) -lt $Structures.Count)
		{
			$After = $Structures[$EndIndex + 1]
			if ($After.ParenAtStart -gt 0 -and !$After.HasComment -and $After.Trim -and
				$After.Trim -notmatch '^[#{}]' -and !$RunText.TrimEnd().EndsWith(';'))
			{
				$AdjacentLiteralTail = $true
			}
		}

		if (!$AdjacentLiteralTail -and (Test-JoinedWidth $RunText $Extension))
		{
			$Kind = 'JoinToOne'
			$Width = Get-DisplayWidth $RunText
		}
		else
		{
			for ($SplitIndex = $EndIndex - 1; $SplitIndex -ge $Index; --$SplitIndex)
			{
				$Prefix = Join-LineRange $Lines $Index $SplitIndex
				$Suffix = Join-LineRange $Lines ($SplitIndex + 1) $EndIndex
				if ((Test-JoinedWidth $Prefix $Extension) -and (Test-JoinedWidth $Suffix $Extension))
				{
					$Split = $SplitIndex
					break
				}
			}

			if ($Split -ge 0)
			{
				if ($RunLineCount -gt 2)
				{
					$Kind = 'PackToTwo'
					$Width = Get-DisplayWidth (Join-LineRange $Lines $Index $Split)
				}
			}
			elseif ($RunLineCount -ge 3)
			{
				$GroupInfo = Get-ParameterGroupInfo $RunText
				$LastLineStart = if ($RunLineCount -ge 2) { (Join-LineRange $Lines $Index ($EndIndex - 1)).Length } else { 0 }
				if ($GroupInfo -and $GroupInfo.OpenIndex -lt $LastLineStart -and
					(Test-HasTopLevelComma $RunText $GroupInfo) -and
					!(Test-OnePerLineLayout $Lines $Index $EndIndex $Structures $GroupInfo.ContentDepth))
				{
					$Kind = 'OnePerLineNeeded'
					$Width = $RunLineCount
				}
			}
		}

		if ($Kind)
		{
			$AllChanged = $true
			$AnyChanged = $false
			for ($LineNumber = $Index + 1; $LineNumber -le $EndIndex + 1; ++$LineNumber)
			{
				if ($ChangedLines.Contains($LineNumber))
				{
					$AnyChanged = $true
				}
				else
				{
					$AllChanged = $false
				}
			}

			if ($AnyChanged)
			{
				$Breaks.Add([pscustomobject]@{
					Start = $Index
					End = $EndIndex
					Kind = $Kind
					Width = $Width
					Split = $Split
					AllChanged = $AllChanged })
			}
		}

		$Index = $EndIndex + 1
	}

	return $Breaks
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
	$Warnings = [System.Collections.Generic.List[string]]::new()

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
		$InBlockComment = $false
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
				if (!$IsMarkdown)
				{
					$InBlockComment = Update-BlockCommentState $Line $InBlockComment $Extension
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

			$Width = Get-DisplayWidth $Line
			if ($IsMarkdown)
			{
				$Limit = if ($InFence) { 120 } else { 100 }
				$IsExplicitException = $Line -match 'https?://' -or $Line -match 'text-layout-ignore:'
				if (!$IsExplicitException -and $Width -gt $Limit)
				{
					$Failures.Add("${RelativePath}:${LineNumber}: display width ${Width} exceeds ${Limit}")
				}
			}
			else
			{
				$IsCommentLine = Test-CommentOnlyLine $Line $InBlockComment $Extension
				if ($IsCommentLine)
				{
					$CommentVerdict = Get-CommentWidthVerdict $Line $Width $Extension
					if ($CommentVerdict.Status -eq 'Fail')
					{
						$Failures.Add("${RelativePath}:${LineNumber}: $($CommentVerdict.Reason)")
					}
					$InBlockComment = Update-BlockCommentState $Line $InBlockComment $Extension
				}
				elseif ($Width -le 120)
				{
					$InBlockComment = Update-BlockCommentState $Line $InBlockComment $Extension
				}
				else
				{
					$CodeVerdict = Get-CodeWidthPolicy $Line $Width $InBlockComment $Extension
					$InBlockComment = $CodeVerdict.EndInBlockComment
					if ($CodeVerdict.Status -eq 'Fail')
					{
						$Failures.Add("${RelativePath}:${LineNumber}: $($CodeVerdict.Reason)")
					}
					elseif ($CodeVerdict.Status -eq 'Warn')
					{
						$Warnings.Add("${RelativePath}:${LineNumber}: $($CodeVerdict.Reason)")
					}
				}
			}

			if ($IsFenceLine)
			{
				$InFence = !$InFence
			}
		}

		if (!$IsMarkdown -and $Extension -ne '.ps1')
		{
			$Breaks = Get-RedundantBreaks (Get-Content -LiteralPath $FullPath -Encoding UTF8) $Extension $LinesToCheck[$RelativePath]
			foreach ($Break in $Breaks)
			{
				switch ($Break.Kind)
				{
					'JoinToOne'
					{
						$Message = "${RelativePath}:$($Break.Start + 1): redundant line break: " +
							"lines $($Break.Start + 1)-$($Break.End + 1) can be joined into one line (width $($Break.Width))"
					}
					'PackToTwo'
					{
						$Message = "${RelativePath}:$($Break.Start + 1): redundant line break: " +
							"lines $($Break.Start + 1)-$($Break.End + 1) can be packed into two lines"
					}
					'OnePerLineNeeded'
					{
						$Message = "${RelativePath}:$($Break.Start + 1): parameter list spans " +
							"lines $($Break.Start + 1)-$($Break.End + 1); put every parameter on its own line"
					}
				}

				if ($Break.AllChanged)
				{
					$Failures.Add($Message)
				}
				else
				{
					$Warnings.Add("$Message - historical lines involved")
				}
			}
		}
	}

	if ($Warnings.Count -gt 0)
	{
		$Warnings | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
		Write-Host "Layout warnings require manual confirmation."
	}

	if ($Failures.Count -gt 0)
	{
		$Failures | ForEach-Object { Write-Host $_ -ForegroundColor Red }
		exit 1
	}

	Write-Host "Text layout check passed. Scope=$Scope Files=$($LinesToCheck.Count) Warnings=$($Warnings.Count)"
}
finally
{
	Pop-Location
}
