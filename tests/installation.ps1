#requires -Version 7.0
param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
. "$repo\scripts\Tools.ps1"
$temporary = Join-Path $repo ('build\installation-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
try {
    # Check patches against the user's real source, only in an isolated copy.
    $fixtures = @(
        @{root=$GameRoot;patch="$repo\renderer-extension\display-menu.patch";files=@('project\src\options_menu.cpp');name='game'},
        @{root="$GameRoot\build\rexglue-sdk";patch="$repo\renderer-extension\integration.patch";files=@('src\graphics\d3d12\command_processor.cpp','include\rex\graphics\d3d12\command_processor.h');name='sdk'}
    )
    foreach ($fixture in $fixtures) {
        $root = Join-Path $temporary $fixture.name
        foreach ($relative in $fixture.files) {
            $destination = Join-Path $root $relative
            New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
            Copy-Item -LiteralPath (Join-Path $fixture.root $relative) -Destination $destination
        }
        & git -C $root init --quiet
        if ($LASTEXITCODE) { throw 'Could not create patch fixture.' }
        if ((Get-PatchState $root $fixture.patch) -eq 'Applied') {
            & git -C $root apply --reverse --ignore-space-change $fixture.patch
            if ($LASTEXITCODE) { throw 'Could not prepare unpatched fixture.' }
        }
        if ((Get-PatchState $root $fixture.patch) -ne 'Ready') { throw 'Forward patch check failed.' }
        & git -C $root apply --ignore-space-change $fixture.patch
        if ($LASTEXITCODE -or (Get-PatchState $root $fixture.patch) -ne 'Applied') { throw 'Patch did not apply.' }
    }
    # Removal must reject later source changes, permit normal preference edits,
    # restore the original file, and leave unrelated files untouched.
    $root = Join-Path $temporary 'removal'
    $backup = Join-Path $root 'classic-aa-backups\test'
    New-Item -ItemType Directory -Path "$backup\project" -Force | Out-Null
    New-Item -ItemType Directory -Path "$root\project" -Force | Out-Null
    New-Item -ItemType Directory -Path "$root\dist\mods\ClassicAA" -Force | Out-Null
    [IO.File]::WriteAllText("$backup\project\test.cpp", 'before')
    [IO.File]::WriteAllText("$root\project\test.cpp", 'after')
    [IO.File]::WriteAllText("$root\dist\mods\ClassicAA\mod.ini", 'enabled=1')
    [IO.File]::WriteAllText("$root\unrelated.txt", 'keep')
    $entries = @(
        @{path='project\test.cpp';existed=$true;before=(Get-FileHash "$backup\project\test.cpp").Hash;after=(Get-FileHash "$root\project\test.cpp").Hash},
        @{path='dist\mods\ClassicAA\mod.ini';existed=$false;before=$null;after=(Get-FileHash "$root\dist\mods\ClassicAA\mod.ini").Hash}
    )
    @{gameRoot=$root;backup=$backup;complete=$true;files=$entries} | ConvertTo-Json -Depth 6 | Set-Content "$root\classic-aa-backups\active.json"
    [IO.File]::WriteAllText("$root\project\test.cpp", 'later edit')
    $blocked = $false
    try { & "$repo\Uninstall.ps1" -GameRoot $root } catch { if ($_.ToString() -match 'File changed since installation') {$blocked=$true} else {throw} }
    if (!$blocked -or [IO.File]::ReadAllText("$root\project\test.cpp") -ne 'later edit') { throw 'Removal did not protect later edits.' }
    [IO.File]::WriteAllText("$root\project\test.cpp", 'after')
    [IO.File]::WriteAllText("$root\dist\mods\ClassicAA\mod.ini", 'enabled=0')
    $lock = [IO.File]::Open("$root\dist\mods\ClassicAA\mod.ini", [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $blocked = $false
    try {
        try { & "$repo\Uninstall.ps1" -GameRoot $root } catch { $blocked=$true }
    } finally { $lock.Dispose() }
    if (!$blocked -or [IO.File]::ReadAllText("$root\project\test.cpp") -ne 'before') { throw 'The locked-file fixture did not interrupt restoration after the first file.' }
    & "$repo\Uninstall.ps1" -GameRoot $root
    if ([IO.File]::ReadAllText("$root\project\test.cpp") -ne 'before' -or [IO.File]::ReadAllText("$root\unrelated.txt") -ne 'keep' -or (Test-Path "$root\dist\mods\ClassicAA\mod.ini")) { throw 'Removal restoration failed.' }
    # Incomplete backups lack final hashes; preserve unknown contents before recovery.
    [IO.File]::WriteAllText("$root\project\test.cpp", 'unrecorded edit')
    [IO.File]::WriteAllText("$root\dist\mods\ClassicAA\mod.ini", 'enabled=0')
    foreach ($entry in $entries) { $entry.after=$null }
    @{gameRoot=$root;backup=$backup;complete=$false;files=$entries} | ConvertTo-Json -Depth 6 | Set-Content "$root\classic-aa-backups\active.json"
    Remove-Item -LiteralPath "$backup\restored.json"
    & "$repo\Uninstall.ps1" -GameRoot $root
    $recovery = @(Get-ChildItem -LiteralPath $backup -Directory -Filter 'recovery-*')
    if ($recovery.Count -ne 1) { throw 'Expected one preserved recovery folder.' }
    $recovery = $recovery[0]
    if ([IO.File]::ReadAllText("$root\project\test.cpp") -ne 'before' -or [IO.File]::ReadAllText("$($recovery.FullName)\project\test.cpp") -ne 'unrecorded edit' -or [IO.File]::ReadAllText("$($recovery.FullName)\dist\mods\ClassicAA\mod.ini") -ne 'enabled=0') { throw 'Incomplete-install recovery lost original or unrecognized contents.' }
    Write-Output 'PASS: patches, later-edit protection, locked-file retry, and incomplete-install recovery with preserved unknown contents.'
} finally {
    $resolved = [IO.Path]::GetFullPath($temporary)
    $allowed = [IO.Path]::GetFullPath((Join-Path $repo 'build')) + '\'
    if (!$resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid temporary test path.' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
