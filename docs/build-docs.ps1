param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

$doxyfile = Join-Path $RepoRoot "docs/Doxyfile"
$outputDir = Join-Path $RepoRoot "build/docs"
$htmlDir = Join-Path $RepoRoot "build/docs/html"
$srcPicsDir = Join-Path $RepoRoot "pics"
$dstPicsDir = Join-Path $htmlDir "pics"
$navTreeDataPath = Join-Path $htmlDir "navtreedata.js"
$menuDataPath = Join-Path $htmlDir "menudata.js"

Push-Location $RepoRoot
try {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
    doxygen $doxyfile

    if (Test-Path $srcPicsDir) {
        New-Item -ItemType Directory -Force -Path $dstPicsDir | Out-Null
        Copy-Item -Path (Join-Path $srcPicsDir "*") -Destination $dstPicsDir -Force
    }

    if (Test-Path $navTreeDataPath) {
        $navTreeLines = Get-Content $navTreeDataPath
        $filteredLines = New-Object System.Collections.Generic.List[string]
        $skipDepth = 0

        foreach ($line in $navTreeLines) {
            if ($skipDepth -gt 0) {
                $skipDepth += ([regex]::Matches($line, '\[').Count - [regex]::Matches($line, '\]').Count)
                if ($skipDepth -le 0) {
                    $skipDepth = 0
                }
                continue
            }

            if ($line -match '^\s*\[\s*"Data Structures",\s*"annotated\.html",\s*\[$' -or
                $line -match '^\s*\[\s*"Files",\s*"files\.html",\s*\[$') {
                $skipDepth = [regex]::Matches($line, '\[').Count - [regex]::Matches($line, '\]').Count
                continue
            }

            if ($line -match '^\s*"annotated\.html"\s*$' -or $line -match '^\s*"files\.html"\s*$') {
                $filteredLines.Add('"index.html"')
                continue
            }

            $filteredLines.Add($line)
        }

        Set-Content -Path $navTreeDataPath -Value $filteredLines
    }

    if (Test-Path $menuDataPath) {
        @'
var menudata={children:[
{text:"Main Page",url:"index.html"},
{text:"Related Pages",url:"pages.html"}
]}
'@ | Set-Content -Path $menuDataPath
    }
}
finally {
    Pop-Location
}
