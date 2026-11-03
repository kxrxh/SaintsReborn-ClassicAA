param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\Tools.ps1"
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
Initialize-BuildTools $GameRoot
$output = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $output -Force | Out-Null
& $Compiler /nologo /std:c++20 /EHsc /O2 /MD /LD "/I$GameRoot\modding\include" "$PSScriptRoot\main.cpp" "/Fo$output\ClassicAA.obj" "/Fe$output\ClassicAA.dll" /link "/IMPLIB:$output\ClassicAA.lib"
if ($LASTEXITCODE) { throw 'Classic AA controller build failed.' }
