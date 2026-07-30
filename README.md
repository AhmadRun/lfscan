# lfscan

Linux temp, cache & system cleanup tool.

```
Usage: lfscan [OPTIONS]

Scans:
  /tmp, /var/tmp, ~/.cache …  stale logs, broken symlinks, orphaned packages

Cleaners:
  --container     Docker / Podman disk usage & prune
  --flatpak       Flatpak runtimes & apps
  --snap          Snap disabled revisions
  --journal-clean systemd journal disk usage
  --journal-vacuum <SIZE>   e.g. 500M
  --language-caches   Cargo, npm, pip, Go, Gradle, Maven caches

Actions:
  --delete         remove flagged files (confirmation required)
  --quarantine     move to quarantine instead of delete
  --restore <ID>   restore from quarantine (repeatable)
  --list-quarantine  list quarantined files

Other:
  --tui            interactive FTXUI terminal UI
  --watch          periodic re-scan with desktop notifications
  --history-db     SQLite history tracking (WAL mode)
  --json           JSON output
  --no-color       plain output
```

**Build:** `cmake -B build -S . && cmake --build build -j$(nproc)`  
**Docker:** `docker build -t lfscan . && docker run -it --rm lfscan /tmp`
