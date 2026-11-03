# Classic AA for Saints Reborn

Native SMAA 1x High anti-aliasing for [Saints Reborn](https://github.com/whompay/SaintsReborn). Smooths edges while keeping the original colors and lighting.

[Download the latest release](https://github.com/kxrxh/SaintsReborn-ClassicAA/releases/latest)

- **Options > Display > Anti-aliasing > Off / SMAA High**.
- Remembers your choice between launches. No hotkey.
- No sharpening, color grading or temporal history.
- Skips menus by default.

## Install

**This version requires a working Saints Reborn source/build installation. It is not a DLL-only mod.** The installer adds the native renderer pass and Display-menu row, then builds them using your existing checkout. No game executable or game assets are distributed.

Requirements: Windows x64, Direct3D 12, Saints Reborn V1.2.2 / source `b81a18a`, its patched ReXGlue SDK `c94f5eb`, Git, Visual Studio 2022 C++ Build Tools with CMake/Ninja, and the LLVM 19.1.5 installation used by Saints Reborn. Build and run Saints Reborn normally first.

1. Download and extract `SaintsReborn-ClassicAA-v0.1.0.zip` from [Releases](https://github.com/kxrxh/SaintsReborn-ClassicAA/releases/latest).
2. Save and close the game.
3. Open PowerShell in the extracted folder and run:

```powershell
.\Install.ps1 -GameRoot "C:\Games\SaintsReborn"
```

4. Enable **Classic AA** in Whompay's Mod Loader and launch the game.
5. Choose **SMAA High** in **Options > Display > Anti-aliasing**.

Use `-CheckOnly` to check compatibility without changing files. The installer saves a rollback backup in `SaintsReborn/classic-aa-backups/` before making changes. Keep that backup.

## Settings and compatibility

Preferences are in `dist/mods/ClassicAA/mod.ini`. `enabled = 0` turns AA off; `bypass_menus = 0` also filters menus. The in-world HUD is part of the composed image and receives AA. SMAA replaces the renderer's FXAA while enabled. Other optional picture settings remain under their existing controls.

This release supports the pinned source/SDK versions above. It checks patch compatibility and stops on conflicts. Changes from other mods are preserved when the patches apply cleanly. Renderer replacements can conflict; do not replace `rexgpu-xenos.dll` with another mod's copy after installation.

To disable AA, choose **Off** or untick the mod and restart. For full removal, close the game and run:

```powershell
.\Uninstall.ps1 -GameRoot "C:\Games\SaintsReborn"
```

Removal restores the backed-up files. It stops if an affected file has since changed, to avoid overwriting later edits. Saves are never touched.

## Build and test

```powershell
.\Build.ps1 -GameRoot "C:\Games\SaintsReborn"
.\Test.ps1 -GameRoot "C:\Games\SaintsReborn"
```

The first command builds the controller in `build/`. The tests cover native GPU smoothing, exact preservation of flat colors and alpha, toggle-off, resizing, Display controls, saved preferences and menu bypass. GPU tests require D3D12; the debug layer is used when available. The installed version was also tested in game, including moving comparisons and Display navigation.

Renderer sources and integration patches are in `renderer-extension/`. `Install.ps1` builds the renderer and game locally. Troubleshooting: check `dist/mods/wml.log` for `[Classic AA]` and `dist/saintsrow_sdk.log` for `SMAA`.

## Credits and license

Author: **kxrxh**. Mod code: [MIT](LICENSE). SMAA by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez. Uses Saints Reborn / Whompay's Mod Loader by whompay and the ReXGlue renderer. See [third-party notices](THIRD_PARTY_NOTICES.md).
