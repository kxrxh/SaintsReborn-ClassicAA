param([Parameter(Mandatory=$true)][string]$GameRoot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\scripts\Tools.ps1"
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
Assert-GameClosed $GameRoot
$active = Join-Path $GameRoot 'classic-aa-backups\active.json'
if (!(Test-Path -LiteralPath $active)) { throw 'No Classic AA installer backup was found.' }
$state = Get-Content -LiteralPath $active -Raw | ConvertFrom-Json
$prefix = $GameRoot.TrimEnd('\') + '\'
$backupPrefix = Join-Path $GameRoot 'classic-aa-backups\'
$backup = [IO.Path]::GetFullPath($state.backup)
if ($state.gameRoot -ne $GameRoot -or !$state.complete -or !$backup.StartsWith($backupPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Backup metadata does not match this installation.' }
foreach ($entry in $state.files) {
    $destination = [IO.Path]::GetFullPath((Join-Path $GameRoot $entry.path))
    $saved = [IO.Path]::GetFullPath((Join-Path $backup $entry.path))
    if (!$destination.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or !$saved.StartsWith($backup+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid backup path.' }
    # Changing the saved AA preference is normal; it must not block removal.
    if (!(Test-Path -LiteralPath $destination) -or ($entry.path -ne 'dist\mods\ClassicAA\mod.ini' -and (Get-FileHash $destination).Hash -ne $entry.after)) { throw "File changed since installation; nothing restored: $($entry.path)" }
    if ($entry.existed -and (!(Test-Path -LiteralPath $saved) -or (Get-FileHash $saved).Hash -ne $entry.before)) { throw "Backup is missing or damaged: $($entry.path)" }
}
foreach ($entry in $state.files) {
    $destination = Join-Path $GameRoot $entry.path
    if ($entry.existed) {
        Copy-Item -LiteralPath (Join-Path $backup $entry.path) -Destination $destination -Force
        if ($entry.path -match '\.(cpp|h|inc)$') { (Get-Item -LiteralPath $destination).LastWriteTime = Get-Date }
    }
    else { Remove-Item -LiteralPath $destination -Force }
}
Move-Item -LiteralPath $active -Destination (Join-Path $backup 'restored.json')
Write-Output 'Restored the files from before installation. The rollback backup has been kept.'
