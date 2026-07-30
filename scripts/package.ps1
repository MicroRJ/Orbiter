[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packageName = 'Orbiter-v0.1.0-win64'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [IO.Path]::GetFullPath((Join-Path $scriptRoot '..'))
$distRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'dist'))
$stagePath = [IO.Path]::GetFullPath((Join-Path $distRoot $packageName))
$zipPath = [IO.Path]::GetFullPath((Join-Path $distRoot ($packageName + '.zip')))
$hashPath = [IO.Path]::GetFullPath((Join-Path $distRoot ($packageName + '.zip.sha256')))
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Test-PathEqual {
	param(
		[Parameter(Mandatory = $true)][string]$Left,
		[Parameter(Mandatory = $true)][string]$Right
	)

	return [StringComparer]::OrdinalIgnoreCase.Equals(
		[IO.Path]::GetFullPath($Left),
		[IO.Path]::GetFullPath($Right)
	)
}

function Assert-OrdinaryPath {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][bool]$Directory
	)

	if (-not (Test-Path -LiteralPath $Path)) {
		return
	}

	$item = Get-Item -LiteralPath $Path -Force
	if ([bool]($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
		throw "Refusing to package through a reparse point: $Path"
	}
	if ($item.PSIsContainer -ne $Directory) {
		throw "Unexpected output type at: $Path"
	}
}

function Remove-ExactOutput {
	param(
		[Parameter(Mandatory = $true)][string]$Path,
		[Parameter(Mandatory = $true)][string]$ExpectedLeaf,
		[Parameter(Mandatory = $true)][bool]$Directory
	)

	$fullPath = [IO.Path]::GetFullPath($Path)
	if (
		-not (Test-PathEqual ([IO.Path]::GetDirectoryName($fullPath)) $distRoot) -or
		-not [StringComparer]::Ordinal.Equals([IO.Path]::GetFileName($fullPath), $ExpectedLeaf)
	) {
		throw "Refusing to remove an unverified package output: $fullPath"
	}

	if (Test-Path -LiteralPath $fullPath) {
		Assert-OrdinaryPath -Path $fullPath -Directory $Directory
		if ($Directory) {
			Remove-Item -LiteralPath $fullPath -Recurse -Force
		}
		else {
			Remove-Item -LiteralPath $fullPath -Force
		}
	}
}

function New-ManifestEntry {
	param(
		[Parameter(Mandatory = $true)][string]$Source,
		[Parameter(Mandatory = $true)][string]$Destination
	)

	return [pscustomobject]@{
		Source = $Source
		Destination = $Destination
	}
}

$manifest = @(
	(New-ManifestEntry 'build/release/Orbiter.exe' 'Orbiter.exe'),
	(New-ManifestEntry 'README.md' 'README.md'),
	(New-ManifestEntry 'LICENSE' 'LICENSE'),
	(New-ManifestEntry 'THIRD_PARTY_NOTICES.md' 'THIRD_PARTY_NOTICES.md'),
	(New-ManifestEntry 'data/default_debugger.cfg' 'data/default_debugger.cfg'),
	(New-ManifestEntry 'data/fonts/Saira/static/Saira-Medium.ttf' 'data/fonts/Saira/static/Saira-Medium.ttf'),
	(New-ManifestEntry 'data/fonts/Saira/OFL.txt' 'data/fonts/Saira/OFL.txt'),
	(New-ManifestEntry 'src/fonts/ttf/freetype/FTL.TXT' 'licenses/FreeType-FTL.txt')
)

# These are public release documents when present. No other root files are copied.
foreach ($optionalDocument in @('CHANGELOG.md', 'RELEASE_NOTES.md')) {
	if (Test-Path -LiteralPath (Join-Path $repoRoot $optionalDocument) -PathType Leaf) {
		$manifest += New-ManifestEntry $optionalDocument $optionalDocument
	}
}

foreach ($entry in $manifest) {
	$sourcePath = [IO.Path]::GetFullPath((Join-Path $repoRoot $entry.Source))
	if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
		throw "Required release input is missing: $($entry.Source)"
	}
}

$msfGifSource = Join-Path $repoRoot 'src/vendor/msf_gif.h'
if (-not (Test-Path -LiteralPath $msfGifSource -PathType Leaf)) {
	throw 'Required MSF GIF license source is missing: src/vendor/msf_gif.h'
}

if (Test-Path -LiteralPath $distRoot) {
	Assert-OrdinaryPath -Path $distRoot -Directory $true
}
else {
	$null = New-Item -ItemType Directory -Path $distRoot
}

Remove-ExactOutput -Path $stagePath -ExpectedLeaf $packageName -Directory $true
Remove-ExactOutput -Path $zipPath -ExpectedLeaf ($packageName + '.zip') -Directory $false
Remove-ExactOutput -Path $hashPath -ExpectedLeaf ($packageName + '.zip.sha256') -Directory $false

try {
	$null = New-Item -ItemType Directory -Path $stagePath

	foreach ($entry in $manifest) {
		$sourcePath = [IO.Path]::GetFullPath((Join-Path $repoRoot $entry.Source))
		$destinationPath = [IO.Path]::GetFullPath((Join-Path $stagePath $entry.Destination))
		$expectedPrefix = $stagePath.TrimEnd('\') + '\'
		if (-not $destinationPath.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
			throw "Manifest destination escapes the package directory: $($entry.Destination)"
		}

		$destinationDirectory = [IO.Path]::GetDirectoryName($destinationPath)
		if (-not (Test-Path -LiteralPath $destinationDirectory)) {
			$null = New-Item -ItemType Directory -Path $destinationDirectory
		}
		Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
	}

	$msfGifText = [IO.File]::ReadAllText($msfGifSource)
	$msfGifLicense = [Text.RegularExpressions.Regex]::Match(
		$msfGifText,
		'(?ms)^ALTERNATIVE A - MIT License\r?\n.*?(?=^------------------------------------------------------------------------------\r?\nALTERNATIVE B - Public Domain)'
	)
	if (
		-not $msfGifLicense.Success -or
		$msfGifLicense.Value -notmatch 'Copyright \(c\) 2021 Miles Fogle' -or
		$msfGifLicense.Value -notmatch 'THE SOFTWARE IS PROVIDED "AS IS"'
	) {
		throw 'Could not safely extract the MSF GIF MIT license.'
	}
	$msfGifLicensePath = Join-Path $stagePath 'licenses/MSF-GIF-MIT.txt'
	[IO.File]::WriteAllText(
		$msfGifLicensePath,
		("MSF GIF 2.2`r`n`r`n" + $msfGifLicense.Value.Trim() + "`r`n"),
		$utf8NoBom
	)

	$allowedFiles = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
	foreach ($entry in $manifest) {
		$null = $allowedFiles.Add($entry.Destination.Replace('\', '/'))
	}
	$null = $allowedFiles.Add('licenses/MSF-GIF-MIT.txt')

	$blockedExtensions = @('.nes', '.nesstate', '.orbiter', '.gif', '.log', '.dump', '.zip', '.7z')
	$textExtensions = @('.cfg', '.md', '.txt')
	foreach ($file in Get-ChildItem -LiteralPath $stagePath -Recurse -File) {
		$relativePath = $file.FullName.Substring($stagePath.Length + 1).Replace('\', '/')
		if (-not $allowedFiles.Contains($relativePath)) {
			throw "Unexpected file entered the package: $relativePath"
		}
		if ($blockedExtensions -contains $file.Extension.ToLowerInvariant()) {
			throw "Forbidden release file type entered the package: $relativePath"
		}
		if ($textExtensions -contains $file.Extension.ToLowerInvariant()) {
			$text = [IO.File]::ReadAllText($file.FullName)
			if ($text -match '(?i)(?:(?<![A-Za-z0-9])[A-Z]:[\\/]|\\\\[A-Za-z0-9._-]+\\)') {
				throw "A machine-local absolute path was found in: $relativePath"
			}
		}
	}

	$defaultConfig = [IO.File]::ReadAllText((Join-Path $stagePath 'data/default_debugger.cfg'))
	if ($defaultConfig -match '(?im)^[ \t]*rom[ \t]+\S+') {
		throw 'The default debugger configuration contains a ROM path.'
	}

	$releaseExecutable = Join-Path $stagePath 'Orbiter.exe'
	$executableBytes = [IO.File]::ReadAllBytes($releaseExecutable)
	$absolutePathPattern = '(?i)[A-Z]:[\\/][A-Za-z0-9._ -]+(?:[\\/][A-Za-z0-9._ -]+)+'
	$executableText = [Text.Encoding]::ASCII.GetString($executableBytes)
	$executableWideText = [Text.Encoding]::Unicode.GetString($executableBytes)
	$embeddedAbsolutePaths = @(
		[Text.RegularExpressions.Regex]::Matches(
			$executableText,
			$absolutePathPattern
		) | ForEach-Object { $_.Value }
		[Text.RegularExpressions.Regex]::Matches(
			$executableWideText,
			$absolutePathPattern
		) | ForEach-Object { $_.Value } | Sort-Object -Unique
	)
	if ($embeddedAbsolutePaths.Count) {
		throw "Machine-local paths are embedded in Orbiter.exe: $($embeddedAbsolutePaths -join ', ')"
	}

	Compress-Archive -LiteralPath $stagePath -DestinationPath $zipPath -CompressionLevel Optimal
	$archiveHash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
	[IO.File]::WriteAllText(
		$hashPath,
		($archiveHash.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($zipPath) + "`r`n"),
		$utf8NoBom
	)
}
catch {
	Remove-ExactOutput -Path $stagePath -ExpectedLeaf $packageName -Directory $true
	Remove-ExactOutput -Path $zipPath -ExpectedLeaf ($packageName + '.zip') -Directory $false
	Remove-ExactOutput -Path $hashPath -ExpectedLeaf ($packageName + '.zip.sha256') -Directory $false
	throw
}

Write-Host "Release directory: $stagePath"
Write-Host "Release archive:   $zipPath"
Write-Host "SHA-256:           $($archiveHash.Hash.ToLowerInvariant())"
