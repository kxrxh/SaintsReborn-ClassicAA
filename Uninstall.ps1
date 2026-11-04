#requires -Version 7.0
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
if ($state.gameRoot -ne $GameRoot -or !$backup.StartsWith($backupPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Backup metadata does not match this installation.' }
$pending = @()
foreach ($entry in $state.files) {
    $destination = [IO.Path]::GetFullPath((Join-Path $GameRoot $entry.path))
    $saved = [IO.Path]::GetFullPath((Join-Path $backup $entry.path))
    if (!$destination.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or !$saved.StartsWith($backup+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid backup path.' }
    if ($entry.existed -and (!(Test-Path -LiteralPath $saved) -or (Get-FileHash $saved).Hash -ne $entry.before)) { throw "Backup is missing or damaged: $($entry.path)" }
    $current = if (Test-Path -LiteralPath $destination) { (Get-FileHash $destination).Hash } else { $null }
    if (($entry.existed -and $current -eq $entry.before) -or (!$entry.existed -and !$current)) { continue }
    # Changing the saved AA preference is normal; it must not block removal.
    if ($state.complete -and (!$current -or ($entry.path -ne 'dist\mods\ClassicAA\mod.ini' -and $current -ne $entry.after))) { throw "File changed since installation; nothing restored: $($entry.path)" }
    $pending += $entry
}
$recovery = Join-Path $backup ('recovery-' + [Guid]::NewGuid().ToString('N'))
foreach ($entry in $pending) {
    $destination = Join-Path $GameRoot $entry.path
    # An interrupted install may not have recorded its final hashes yet.
    if (!$state.complete -and (Test-Path -LiteralPath $destination) -and (Get-FileHash $destination).Hash -ne $entry.after) {
        $preserved = Join-Path $recovery $entry.path
        New-Item -ItemType Directory -Path (Split-Path $preserved) -Force | Out-Null
        Copy-Item -LiteralPath $destination -Destination $preserved
    }
    if ($entry.existed) {
        New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $backup $entry.path) -Destination $destination -Force
        if ($entry.path -match '\.(cpp|h|inc)$') { (Get-Item -LiteralPath $destination).LastWriteTime = Get-Date }
    }
    elseif (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
}
Move-Item -LiteralPath $active -Destination (Join-Path $backup 'restored.json')
if (Test-Path -LiteralPath $recovery) { Write-Output "Preserved interrupted-install files in: $recovery" }
Write-Output 'Restored the files from before installation. The rollback backup has been kept.'
