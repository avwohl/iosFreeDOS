# Disk Distribution System

This document explains how disk images and help content are distributed to FreeDOS app clients.

## Overview

Disk images, help files, and the disk catalog are stored in this repository under `/release_assets/` and distributed via GitHub Releases. Clients fetch the catalog and download assets on-demand from the GitHub Releases "latest" endpoint.

## Repository Structure

```
release_assets/
  disks.xml              # Disk catalog manifest
  help_index.json        # Help system topic index
  help_*.md              # Help topic markdown files
```

## The Disk Manifest (`disks.xml`)

The manifest is an XML file listing all available disk images:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<disks version="1">
    <disk>
        <filename>freedos.img</filename>
        <name>FreeDOS Boot Floppy</name>
        <type>floppy</type>
        <description>FreeDOS 1.4 boot floppy with R.COM and W.COM</description>
        <size>1474560</size>
        <license>GPL v2+</license>
        <sha256>CHECKSUM_HERE</sha256>
    </disk>
</disks>
```

### Manifest Fields

| Field | Required | Description |
|-------|----------|-------------|
| `filename` | Yes | Disk image filename |
| `name` | Yes | Display name shown to users |
| `type` | Yes | Disk type: `floppy`, `hdd`, or `iso` |
| `description` | Yes | Human-readable description |
| `size` | Yes | File size in bytes |
| `license` | Yes | License type |
| `sha256` | Yes | SHA256 checksum for integrity verification |

## GitHub Releases Distribution

### Release URLs

Clients use these hardcoded URLs:
```
Catalog:  https://github.com/avwohl/dosemu/releases/latest/download/disks.xml
Base URL: https://github.com/avwohl/dosemu/releases/latest/download/
```

### Creating a Release

Use the GitHub Actions workflow (Actions > Build Release Assets > Run workflow) or manually:

1. Update `release_assets/disks.xml` if adding/modifying disks
2. Generate SHA256 checksums: `shasum -a 256 release_assets/*`
3. Create a GitHub release and attach all files from `/release_assets/`
4. The release should be tagged (clients use `/latest/`)

## Client Implementation

Clients fetch `disks.xml` on app launch and cache it locally. If offline, the cached version is used as fallback. Downloads are verified against the manifest's SHA256 checksum.

## Privacy

No user data is sent to GitHub. Clients only download:
- The disk catalog manifest
- Disk image files (user-initiated)
- Help content files

See `PRIVACY.md` for full privacy policy.
