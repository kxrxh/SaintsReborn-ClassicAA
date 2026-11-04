# Changelog

## 0.2.1

- Use the public latest-release link for update checks, avoiding GitHub API rate limits.

## 0.2.0

- Add a native Windows app for installation, updates, removal and recovery.
- Check downloaded installer updates against the published SHA256 checksum.
- Allow retrying interrupted removal and recovering incomplete installations.
- Preserve unrecognized files during incomplete-install recovery.
- Require PowerShell 7 for the installer and build scripts.
- Release AA work buffers when rebuilding the renderer after a resolution change.

## 0.1.0

- Native SMAA 1x High with an Off / SMAA High selector in Display settings.
- Saved preference and automatic menu bypass.
- Source installer with compatibility checks and rollback backups.
