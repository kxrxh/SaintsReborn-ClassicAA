param([Parameter(Mandatory=$true)][string]$GameRoot, [switch]$CheckOnly, [ValidateRange(1,64)][int]$Jobs=8)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\Tools.ps1"
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$sdk = Join-Path $GameRoot 'build\rexglue-sdk'
$menuPatch = Join-Path $PSScriptRoot 'renderer-extension\display-menu.patch'
$gpuPatch = Join-Path $PSScriptRoot 'renderer-extension\integration.patch'
$head = & git -C $GameRoot rev-parse HEAD
if ($LASTEXITCODE -or $head -ne 'b81a18ac40d7bf8fc711555f5a958e3a94a97c88') { throw 'This release requires Saints Reborn source b81a18a.' }
$head = & git -C $sdk rev-parse HEAD
if ($LASTEXITCODE -or $head -ne 'c94f5ebdcb3c9d1a460ca48e04f9758448f8d518') { throw 'This release requires the patched ReXGlue SDK c94f5eb.' }
foreach ($relative in @('build\game\CMakeCache.txt','build\rexglue-sdk\out\build\win-amd64\CMakeCache.txt','modding\include\wml.h','dist\saintsrow.exe','dist\rexgpu-xenos.dll')) {
    if (!(Test-Path -LiteralPath (Join-Path $GameRoot $relative))) { throw "Build Saints Reborn normally first. Missing: $relative" }
}
Initialize-BuildTools $GameRoot
$menuState = Get-PatchState $GameRoot $menuPatch
$gpuState = Get-PatchState $sdk $gpuPatch
$smaaFiles = @('backend.inc','shader_source.h','AreaTex.h','SearchTex.h')
foreach ($name in $smaaFiles) {
    $destination = Join-Path $sdk "src\graphics\d3d12\smaa\$name"
    if ((Test-Path -LiteralPath $destination) -and (Get-FileHash $destination).Hash -ne (Get-FileHash "$PSScriptRoot\renderer-extension\$name").Hash) {
        throw "Existing SMAA source differs: $destination"
    }
}
if ($CheckOnly) { Write-Output "Compatible: Display patch $menuState; renderer patch $gpuState. No game files changed."; return }
Assert-GameClosed $GameRoot
$active = Join-Path $GameRoot 'classic-aa-backups\active.json'
if (Test-Path -LiteralPath $active) { throw 'Classic AA has an active installer backup. Uninstall that installation first.' }
& "$PSScriptRoot\Build.ps1" -GameRoot $GameRoot
$backup = Join-Path $GameRoot ('classic-aa-backups\' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$paths = @('project\src\options_menu.cpp','build\rexglue-sdk\src\graphics\d3d12\command_processor.cpp','build\rexglue-sdk\include\rex\graphics\d3d12\command_processor.h','dist\saintsrow.exe','dist\rexgpu-xenos.dll','dist\mods\ClassicAA\ClassicAA.dll','dist\mods\ClassicAA\mod.ini','dist\mods\ClassicAA\README.md','dist\mods\ClassicAA\LICENSE')
$paths += $smaaFiles | ForEach-Object { "build\rexglue-sdk\src\graphics\d3d12\smaa\$_" }
$entries = @()
foreach ($relative in $paths) {
    $source = Join-Path $GameRoot $relative
    $saved = Join-Path $backup $relative
    $existed = Test-Path -LiteralPath $source
    if ($existed) { New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null; Copy-Item -LiteralPath $source -Destination $saved }
    $entries += [pscustomobject]@{path=$relative;existed=$existed;before=if($existed){(Get-FileHash $source).Hash}else{$null};after=$null}
}
# Write recovery information before any game/source changes.
$state = [pscustomobject]@{version='0.1.0';gameRoot=$GameRoot;backup=$backup;complete=$false;files=$entries}
$state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $active -Encoding UTF8
try {
    if ($menuState -eq 'Ready') { & git -C $GameRoot apply --ignore-space-change $menuPatch; if ($LASTEXITCODE) { throw 'Display patch failed.' } }
    if ($gpuState -eq 'Ready') { & git -C $sdk apply --ignore-space-change $gpuPatch; if ($LASTEXITCODE) { throw 'Renderer patch failed.' } }
    $smaa = Join-Path $sdk 'src\graphics\d3d12\smaa'
    New-Item -ItemType Directory -Path $smaa -Force | Out-Null
    foreach ($name in $smaaFiles) { Copy-Item -LiteralPath "$PSScriptRoot\renderer-extension\$name" -Destination $smaa -Force }
    & $CMake --build "$sdk\out\build\win-amd64" --config Release --target rexgpu-xenos -j $Jobs
    if ($LASTEXITCODE) { throw 'Renderer build failed.' }
    & $CMake --build "$GameRoot\build\game" --target saintsrow -j $Jobs
    if ($LASTEXITCODE) { throw 'Saints Reborn build failed.' }
    Assert-GameClosed $GameRoot
    Copy-Item -LiteralPath "$sdk\out\win-amd64\Release\rexgpu-xenos.dll" -Destination "$GameRoot\dist\rexgpu-xenos.dll" -Force
    Copy-Item -LiteralPath "$GameRoot\build\game\saintsrow.exe" -Destination "$GameRoot\dist\saintsrow.exe" -Force
    $mod = Join-Path $GameRoot 'dist\mods\ClassicAA'
    New-Item -ItemType Directory -Path $mod -Force | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot\build\ClassicAA.dll" -Destination $mod -Force
    foreach ($name in @('README.md','LICENSE')) { Copy-Item -LiteralPath "$PSScriptRoot\$name" -Destination $mod -Force }
    if (!(Test-Path -LiteralPath "$mod\mod.ini")) { Copy-Item -LiteralPath "$PSScriptRoot\mod.ini" -Destination $mod }
    foreach ($entry in $entries) { $entry.after = (Get-FileHash (Join-Path $GameRoot $entry.path)).Hash }
    $state.complete = $true
    $state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $active -Encoding UTF8
    Write-Output 'Installed. Enable Classic AA in Whompay''s Mod Loader, then use Options > Display > Anti-aliasing.'
} catch {
    # A failed build must not leave half-applied source or runtime replacements.
    foreach ($entry in $entries) {
        $destination = Join-Path $GameRoot $entry.path
        if ($entry.existed) {
            Copy-Item -LiteralPath (Join-Path $backup $entry.path) -Destination $destination -Force
            if ($entry.path -match '\.(cpp|h|inc)$') { (Get-Item -LiteralPath $destination).LastWriteTime = Get-Date }
        }
        elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
    }
    Remove-Item -LiteralPath $active -Force
    throw
}
