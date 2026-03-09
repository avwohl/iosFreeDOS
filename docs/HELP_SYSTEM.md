# Remote Help System

This document describes the remote help system used by the FreeDOS app.

## Overview

Help documentation is hosted in GitHub Releases and fetched on-demand by clients. This allows:
- Updating help content without app updates
- Reducing app bundle size
- Consistent documentation across all platforms

## Architecture

```
GitHub Release Assets:
  help_index.json        # Index of all help topics
  help_quick_start.md    # Quick start guide
  help_file_transfer.md  # File transfer guide
  help_networking.md     # Networking setup guide
```

Clients fetch from:
```
https://github.com/avwohl/dosemu/releases/latest/download/
```

## help_index.json Format

```json
{
  "version": 1,
  "base_url": "https://github.com/avwohl/dosemu/releases/latest/download/",
  "topics": [
    {
      "id": "quick_start",
      "title": "Quick Start Guide",
      "description": "Getting started with FreeDOS",
      "filename": "help_quick_start.md"
    }
  ]
}
```

### Fields

| Field | Description |
|-------|-------------|
| `version` | Index version number (increment when structure changes) |
| `base_url` | Base URL for fetching help files |
| `topics` | Array of available help topics |
| `topics[].id` | Unique identifier for the topic |
| `topics[].title` | Display title |
| `topics[].description` | Short description for topic list |
| `topics[].filename` | Filename to fetch (appended to base_url) |

## Client Implementation

1. On help view open, fetch `help_index.json`
2. Display topic list with title and description
3. When user selects a topic, fetch the markdown file from `{base_url}{filename}`
4. Render markdown content
5. Cache fetched files locally; fall back to cache when offline

## Adding New Help Topics

1. Create the markdown file: `release_assets/help_newtopic.md`
2. Add entry to `release_assets/help_index.json`
3. Create new GitHub release with updated assets

## Current Help Topics

| ID | Title | Filename |
|----|-------|----------|
| quick_start | Quick Start Guide | help_quick_start.md |
| file_transfer | File Transfer (R/W) | help_file_transfer.md |
| networking | Networking (NE2000) | help_networking.md |
