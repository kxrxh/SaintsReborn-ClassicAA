# Classic AA guide

## Install

**Requires a working Saints Reborn source/build installation.** The Windows app adds the native renderer pass and Display-menu row, then builds them using your existing checkout. No game executable or game assets are distributed.

Requirements: Windows 10/11 x64 with .NET Framework 4.8, [PowerShell 7](https://github.com/PowerShell/PowerShell/releases/latest), Direct3D 12, Saints Reborn V1.2.2 / source `b81a18a`, its patched ReXGlue SDK `c94f5eb`, Git, Visual Studio 2022 C++ Build Tools with CMake/Ninja, and the LLVM 19.1.5 installation used by Saints Reborn. Build and run Saints Reborn normally first.

1. Download `SaintsReborn-ClassicAA-Setup-v0.2.1.exe` from [Releases](https://github.com/kxrxh/SaintsReborn-ClassicAA/releases/latest).
2. Save and close the game.
3. Open the app, choose the Saints Reborn folder and click **Install Classic AA**.
4. Enable **Classic AA** in Whompay's Mod Loader and launch the game.
5. Choose **SMAA High** in **Options > Display > Anti-aliasing**.

The app detects common install locations and shows its progress and activity log. It saves a rollback backup in `SaintsReborn/classic-aa-backups/` before making changes. Keep that backup.

## Updates and removal

Keep the setup app. **Check for updates** finds newer installers on this project's GitHub Releases page. Downloads are checked against the published SHA256 checksum before the new app opens. In the newer app, **Update** installs the bundled version and preserves your AA preferences.

For removal, close the game and click **Uninstall**. If an operation was interrupted, use **Recover installation**. The app keeps full activity logs in `%LOCALAPPDATA%/SaintsRebornClassicAA/logs/` and provides an **Open full log** button.

## Settings and compatibility

Preferences are in `dist/mods/ClassicAA/mod.ini`. `enabled = 0` turns AA off; `bypass_menus = 0` also filters menus. The in-world HUD is part of the composed image and receives AA. SMAA replaces the renderer's FXAA while enabled. Other optional picture settings remain under their existing controls.

This release supports the pinned source/SDK versions above. It checks patch compatibility and stops on conflicts. Changes from other mods are preserved when the patches apply cleanly. Renderer replacements can conflict; do not replace `rexgpu-xenos.dll` with another mod's copy after installation.

To disable AA, choose **Off** or untick the mod and restart. For command-line installation or removal, extract the source ZIP from Releases and use PowerShell 7:

```powershell
pwsh -File .\Install.ps1 -GameRoot "C:\Games\SaintsReborn"
pwsh -File .\Uninstall.ps1 -GameRoot "C:\Games\SaintsReborn"
```

Removal restores the backed-up files and can be retried if a file was locked. After a completed installation, it stops if an affected file has since changed, to avoid overwriting later edits. After an interrupted installation, it preserves unrecognized current file contents in a `recovery-*` folder inside the backup before restoring the originals. Saves are never touched.

Use `Install.ps1 -CheckOnly` to check compatibility without changing game files. The GUI runs the same scripts.

## Build and test

```powershell
pwsh -File .\Build.ps1 -GameRoot "C:\Games\SaintsReborn"
pwsh -File .\Test.ps1 -GameRoot "C:\Games\SaintsReborn"
pwsh -File .\installer\Build.ps1 -Test
```

The first command builds the controller in `build/`. The tests cover native GPU smoothing, exact preservation of flat colors and alpha, toggle-off, resizing, context resets, Display controls, saved preferences, menu bypass, restoration retries and incomplete-install recovery. Installer tests cover the embedded package, file-path validation, update URLs and checksums, Unicode paths, child-process errors and the actual WPF controls. GPU tests require D3D12; the debug layer is used when available.

Renderer sources and integration patches are in `renderer-extension/`. `Install.ps1` builds the renderer and game locally. Troubleshooting: check `dist/mods/wml.log` for `[Classic AA]` and `dist/saintsrow_sdk.log` for `SMAA`.

## Credits and license

Author: **kxrxh**. Mod code: [MIT](LICENSE). SMAA by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez. Uses Saints Reborn / Whompay's Mod Loader by whompay and the ReXGlue renderer. See [third-party notices](THIRD_PARTY_NOTICES.md).
