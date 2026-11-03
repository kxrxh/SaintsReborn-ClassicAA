param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\Tools.ps1"
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
Initialize-BuildTools $GameRoot
& "$PSScriptRoot\Build.ps1" -GameRoot $GameRoot
$output = Join-Path $PSScriptRoot 'build\tests'
New-Item -ItemType Directory -Path "$output\mock" -Force | Out-Null
# Test the exact Display bridge functions distributed in the patch.
$patch = [IO.File]::ReadAllText("$PSScriptRoot\renderer-extension\display-menu.patch").Replace("`r`n","`n")
$start = $patch.IndexOf('+bool ClassicAaAvailable()')
$end = $patch.IndexOf(' std::vector<Row>& Rows()', $start)
if ($start -lt 0 -or $end -lt 0) { throw 'Display bridge was not found in the patch.' }
$helpers = ($patch.Substring($start, $end-$start).Split("`n") | Where-Object { $_.StartsWith('+') } | ForEach-Object { $_.Substring(1) }) -join "`n"
[IO.File]::WriteAllText("$output\display_helpers.inc", $helpers)
Copy-Item -LiteralPath "$PSScriptRoot\build\ClassicAA.dll" -Destination $output -Force
& $Compiler /nologo /std:c++20 /EHsc /O2 /MD /LD "$PSScriptRoot\tests\mock_backend.cpp" "/Fo$output\mock\backend.obj" "/Fe$output\mock\rexgpu-xenos.dll" /link "/IMPLIB:$output\mock\backend.lib"
if ($LASTEXITCODE) { throw 'Mock backend build failed.' }
& $Compiler /nologo /std:c++20 /EHsc /O2 /MD "/I$GameRoot\modding\include" "/I$output" "$PSScriptRoot\tests\settings.cpp" "/Fo$output\settings.obj" "/Fe$output\settings.exe"
if ($LASTEXITCODE) { throw 'Settings test build failed.' }
& $Compiler /nologo /std:c++20 /EHsc /O2 /MD "$PSScriptRoot\tests\native_smaa.cpp" "/Fo$output\native_smaa.obj" "/Fe$output\native_smaa.exe" /link d3d12.lib dxgi.lib
if ($LASTEXITCODE) { throw 'Native GPU test build failed.' }
Push-Location $output
try {
    & '.\settings.exe'
    if ($LASTEXITCODE) { throw 'Settings tests failed.' }
    & '.\native_smaa.exe'
    if ($LASTEXITCODE) { throw 'Native GPU tests failed.' }
} finally { Pop-Location }
& "$PSScriptRoot\tests\installation.ps1" -GameRoot $GameRoot
