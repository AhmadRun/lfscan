# lfscan

**Linux temp, cache & system cleanup tool** — reclaim disk space, manage package manager caches, and keep your system clean.

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org)
[![CMake](https://img.shields.io/badge/CMake-%3E%3D3.16-064F8C?logo=cmake)](https://cmake.org)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

---

## What is lfscan?

`lfscan` scans your Linux system for files and resources that are safe to clean — temporary files, orphaned data, outdated caches, and unused container images — and gives you full control over what gets removed.

### Why use it?

- **Recover disk space** — finds GBs of reclaimable space across temp dirs, build caches, container images, Flatpak runtimes, Snap revisions, and systemd journals.
- **Safe by default** — dry-run mode shows what would be deleted; nothing is removed without confirmation unless you pass `--force`.
- **Interactive & automated** — use the TUI for point-and-click cleanup, or `--watch` mode for periodic background scans with desktop notifications.
- **One tool, many targets** — instead of learning `docker prune`, `flatpak uninstall`, `journalctl --vacuum`, `npm cache clean`, etc., run `lfscan --container --flatpak --journal-vacuum 500M`.

---

## Key Features

| Category | What it scans / cleans |
|----------|------------------------|
| **Temp files** | `/tmp`, `/var/tmp`, `~/.cache`, system temp dirs |
| **Stale logs** | `/var/log` files past retention age |
| **Broken symlinks** | dangling symbolic links |
| **Orphaned packages** | files in `/etc` not owned by any RPM package (RPM distros only) |
| **Container runtimes** | Docker & Podman — unused images, containers, volumes, build cache |
| **Flatpak** | unused runtimes and applications |
| **Snap** | disabled Snap revisions (safe to remove) |
| **systemd journal** | report disk usage and vacuum to a target size |
| **Language caches** | Cargo (`~/.cargo`), npm (`~/.npm`), pip (`~/.cache/pip`), Go (`~/go/pkg/mod`), Gradle (`~/.gradle`), Maven (`~/.m2/repository`) |
| **Quarantine** | move flagged files to quarantine instead of deleting; restore individually or all at once |
| **History tracking** | SQLite database with scan and action history (WAL mode) |
| **Watch mode** | periodic re-scans with desktop notifications when new items appear |

---

## Installation

### Build from source

**Dependencies:** C++20 compiler, CMake ≥3.16, `git`, SQLite3, and `libncurses`/`libfreetype` (for TUI).

```bash
git clone git@github.com:AhmadRun/lfscan.git
cd lfscan
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cp build/lfscan /usr/local/bin/
```

#### Dependency quick-install by distro

<details>
<summary>Fedora / RHEL</summary>

```bash
sudo dnf install gcc-c++ cmake make git sqlite-devel freetype-devel ncurses-devel
```
</details>

<details>
<summary>Ubuntu / Debian</summary>

```bash
sudo apt install build-essential cmake git libsqlite3-dev libfreetype-dev libncurses-dev
```
</details>

<details>
<summary>Arch Linux</summary>

```bash
sudo pacman -S gcc cmake make git sqlite freetype2 ncurses
```
</details>

<details>
<summary>openSUSE</summary>

```bash
sudo zypper install gcc-c++ cmake make git sqlite3-devel freetype2-devel ncurses-devel
```
</details>

### Docker

```bash
docker build -t lfscan .
docker run -it --rm lfscan /tmp
```

For cleaning the host Docker daemon (DooD), mount the Docker socket:

```bash
docker run -it --rm \
    -v /var/run/docker.sock:/var/run/docker.sock \
    lfscan --container
```

### Flatpak

A Flatpak manifest is included for building locally:

```bash
flatpak-builder --user --install build-flatpak flatpak/org.lfscan.lfscan.yml
flatpak run org.lfscan.lfscan --help
```

> Flatpak distribution is planned for future releases.

---

## Usage Guide

### Quick start — scan temp files (dry-run)

```bash
lfscan
```

Lists recoverable space in `/tmp`, `/var/tmp`, and `~/.cache`. No files are deleted.

### Delete flagged files

```bash
lfscan --delete
```

Prompts for confirmation before deleting. Use `--force` to skip the prompt.

### Scan specific directories

```bash
lfscan -t /home/user/downloads -t /var/cache
```

### Quarantine files instead of deleting

```bash
lfscan --quarantine /home/user/lfscan-quarantine
```

Files are moved to the quarantine directory. To see what's quarantined:

```bash
lfscan --list-quarantine /home/user/lfscan-quarantine
```

Restore files by their IDs (the quarantine directory is required):

```bash
lfscan --quarantine /home/user/lfscan-quarantine --restore 3 --restore 7
```

Or restore everything:

```bash
lfscan --quarantine /home/user/lfscan-quarantine --restore-all
```

### Clean container runtimes

```bash
lfscan --container
lfscan --container --delete
```

Detects Docker or Podman automatically. Reports reclaimable space.

### Clean language build caches

```bash
lfscan --language-caches
```

Scans Cargo, npm, pip, Go, Gradle, and Maven caches.

### Manage systemd journal

```bash
# Report journal disk usage
lfscan --journal-clean

# Vacuum to 500 MB
lfscan --journal-vacuum 500M
```

> Vacuum requires root or `systemd-journal` group membership.

### Clean Flatpak

```bash
lfscan --flatpak
lfscan --flatpak --delete
```

### Clean Snap

```bash
lfscan --snap
lfscan --snap --delete
```

### Combine multiple cleaners

```bash
lfscan --language-caches --container --flatpak --journal-vacuum 200M
```

### JSON output

```bash
lfscan --json
lfscan --container --json
```

Pipe to `jq` for processing:

```bash
lfscan --json | jq '.cleaners[] | select(.category == "containers")'
```

### Interactive TUI

```bash
lfscan --tui
```

Launches a full-screen terminal UI with three tabs:

- **Scan** — run a scan and view results grouped by detector type
- **History** — browse past scans stored in the SQLite database
- **Watch** — start/stop periodic watch mode

### Watch mode (automated)

```bash
lfscan --watch --watch-interval 300
```

Re-scans every 300 seconds (default). Sends a desktop notification when new items are detected. Press Ctrl+C to stop.

### Track history

By default, history is stored at `~/.local/share/lfscan/history.db`:

```bash
lfscan --history-db /custom/path/history.db
```

---

## Examples at a glance

| Command | What it does |
|---------|--------------|
| `lfscan` | dry-run scan of system temp dirs |
| `lfscan --delete --force` | delete flagged files without confirmation |
| `lfscan -t /var/cache` | scan a custom directory |
| `lfscan --container --delete` | prune Docker/Podman unused resources |
| `lfscan --flatpak --snap` | report Flatpak + Snap reclaimable space |
| `lfscan --journal-vacuum 200M` | shrink journal to 200 MB |
| `lfscan --language-caches` | scan all language build caches |
| `lfscan --tui` | launch interactive TUI |
| `lfscan --watch` | watch mode with notifications |
| `lfscan --json \| jq .` | full machine-readable report |
| `lfscan --quarantine ~/lfscan-quarantine --restore 1` | restore one quarantined file by ID |

---

## Building from source (detailed)

```bash
git clone git@github.com:AhmadRun/lfscan.git
cd lfscan
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
sudo cp build/lfscan /usr/local/bin/
```

Build options:

| Flag | Effect |
|------|--------|
| `-DCMAKE_BUILD_TYPE=Release` | Optimized build (recommended) |
| `-DCMAKE_BUILD_TYPE=Debug` | Debug symbols, no optimizations |
| `-DSQLITE3_USE_SYSTEM=ON` | Use system SQLite3 instead of bundled |

---

## Project structure

```
lfscan/
├── CMakeLists.txt            # Build configuration
├── Dockerfile                # Multi-stage Ubuntu Docker image
├── flatpak/                  # Flatpak manifest (WIP)
│   └── org.lfscan.lfscan.yml
├── lfscan.spec               # RPM spec file
├── src/
│   ├── main.cpp              # Entry point, CLI flags, orchestrator
│   ├── *.cpp / *.hpp         # Detectors, cleaners, utilities
│   ├── tui_mode.cpp/hpp      # FTXUI interactive TUI
│   ├── history_db.cpp/hpp    # SQLite history tracking
│   ├── quarantine.cpp/hpp    # File quarantine logic
│   └── reporter.cpp/hpp      # Output formatting
└── tests/
    └── test_edge_cases.sh    # Shell-based test suite
```

---

## License

MIT
