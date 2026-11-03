$ErrorActionPreference = 'Stop'
function Initialize-BuildTools([string]$GameRoot) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Visual Studio C++ Build Tools are required.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }
    if ($LASTEXITCODE) { throw 'Could not initialize the C++ build environment.' }
    $script:Compiler = Join-Path $GameRoot 'build\llvm-19.1.5\bin\clang-cl.exe'
    $script:CMake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    foreach ($tool in @($script:Compiler, $script:CMake)) {
        if (!(Test-Path -LiteralPath $tool)) { throw "Required build tool is missing: $tool" }
    }
    $env:PATH = (Split-Path $script:Compiler) + ';' + (Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja') + ';' + $env:PATH
}
function Get-PatchState([string]$Root, [string]$Patch) {
    & git -C $Root apply --check --ignore-space-change $Patch 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { return 'Ready' }
    & git -C $Root apply --reverse --check --ignore-space-change $Patch 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { return 'Applied' }
    throw "Patch conflicts with this checkout: $Patch"
}
function Assert-GameClosed([string]$GameRoot) {
    $exe = Join-Path $GameRoot 'dist\saintsrow.exe'
    if (Get-Process saintsrow -ErrorAction SilentlyContinue | Where-Object { !$_.Path -or $_.Path -eq $exe }) {
        throw 'Save and close Saints Reborn before installing or removing Classic AA.'
    }
}
