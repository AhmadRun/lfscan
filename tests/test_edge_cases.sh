#!/usr/bin/env bash
# Edge-case test suite for lfscan
# Run inside a Fedora container:
#   podman run --rm -v "$PWD:/src:z" -w /src fedora:44 bash tests/test_edge_cases.sh

set -u

BIN="${1:-./build/lfscan}"
echo "=== lfscan edge-case test suite ==="
echo "Binary: $BIN"
echo ""

pass=0
fail=0

check() {
    local label="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -q "$expected" 2>/dev/null; then
        echo "  PASS: $label"
        pass=$((pass+1))
    else
        echo "  FAIL: $label (expected to find: '$expected')"
        echo "    got: $actual"
        fail=$((fail+1))
    fi
}

check_not() {
    local label="$1" unexpected="$2" actual="$3"
    if echo "$actual" | grep -q "$unexpected" 2>/dev/null; then
        echo "  FAIL: $label (unexpectedly found: '$unexpected')"
        echo "    got: $actual"
        fail=$((fail+1))
    else
        echo "  PASS: $label"
        pass=$((pass+1))
    fi
}

cleanup() {
    rm -rf /tmp/lfscan_test_*
}

trap cleanup EXIT

# Point the default scan targets ($TMPDIR, ~/.cache) at an empty sandbox so
# the --delete/--quarantine tests below can't touch real files in /tmp or
# the user's cache. (/var/tmp is still scanned.)
mkdir -p /tmp/lfscan_test_sandbox/home
export TMPDIR=/tmp/lfscan_test_sandbox
export HOME=/tmp/lfscan_test_sandbox/home
unset TMP

# ── 1. Empty directory ──────────────────────────────────────────────
echo "[1] Empty directory"
mkdir -p /tmp/lfscan_test_empty
out=$($BIN -d 1 -t /tmp/lfscan_test_empty --no-color 2>/dev/null)
check "empty dir" "No files found" "$out"

# ── 2. Regular files ────────────────────────────────────────────────
echo "[2] Regular files"
mkdir -p /tmp/lfscan_test_reg
touch -t 202001010000 /tmp/lfscan_test_reg/old.log
touch /tmp/lfscan_test_reg/fresh.txt
out=$($BIN -d 1 -t /tmp/lfscan_test_reg --no-color 2>/dev/null)
check "detects old file" "old.log" "$out"
check_not "skips fresh file" "fresh.txt" "$out"

# ── 3. Pattern files ────────────────────────────────────────────────
echo "[3] Pattern files"
mkdir -p /tmp/lfscan_test_pat
touch /tmp/lfscan_test_pat/file.tmp
touch /tmp/lfscan_test_pat/notes.tmp
touch /tmp/lfscan_test_pat/regular.txt
out=$($BIN -d 3650 -t /tmp/lfscan_test_pat --no-color 2>/dev/null)
check "detects *.tmp (file.tmp)" "file.tmp" "$out"
check "detects *.tmp (notes.tmp)" "notes.tmp" "$out"
check_not "skips regular.txt" "regular.txt" "$out"

# ── 4. Broken symlinks ──────────────────────────────────────────────
echo "[4] Broken symlinks"
mkdir -p /tmp/lfscan_test_sym
ln -s /nonexistent/target /tmp/lfscan_test_sym/broken
ln -s /etc/hosts /tmp/lfscan_test_sym/valid 2>/dev/null || true
out=$($BIN -d 3650 -t /tmp/lfscan_test_sym --no-color 2>/dev/null)
check "detects broken symlink" "broken" "$out"
check_not "skips valid symlink" "valid" "$out"

# ── 5. Permission denied ────────────────────────────────────────────
echo "[5] Permission denied"
mkdir -p /tmp/lfscan_test_perm/subdir
touch /tmp/lfscan_test_perm/subdir/secret.txt
chmod 000 /tmp/lfscan_test_perm/subdir
out=$($BIN -d 1 -t /tmp/lfscan_test_perm --no-color 2>&1 || true)
check "permission error handled gracefully" "scan" "$out"
chmod 755 /tmp/lfscan_test_perm/subdir

# ── 6. Non-existent target ──────────────────────────────────────────
echo "[6] Non-existent target"
out=$($BIN -d 1 -t /tmp/lfscan_test_nonexistent --no-color 2>&1 || true)
check "nonexistent target warning" "does not exist" "$out"

# ── 7. Files with special characters ────────────────────────────────
echo "[7] Special characters"
mkdir -p /tmp/lfscan_test_spc
touch -t 202001010000 '/tmp/lfscan_test_spc/file with spaces.log'
touch -t 202001010000 '/tmp/lfscan_test_spc/file_with_(parens).tmp'
out=$($BIN -d 1 -t /tmp/lfscan_test_spc --no-color 2>/dev/null)
check "handles spaces" "file with spaces.log" "$out"
check "handles parens" "parens" "$out"

# ── 8. Deeply nested directory ──────────────────────────────────────
echo "[8] Deep nesting (does not crash)"
rm -rf /tmp/lfscan_test_deep
mkdir -p /tmp/lfscan_test_deep
d=/tmp/lfscan_test_deep
for i in $(seq 1 60); do
    mkdir -p "$d/level$i"
    d="$d/level$i"
done
touch -t 202001010000 "$d/deep_file.txt"
if $BIN -d 1 -t /tmp/lfscan_test_deep --no-color > /dev/null 2>&1; then
    echo "  PASS: deep nesting (no crash)"; pass=$((pass+1))
else
    echo "  FAIL: deep nesting crashed"; fail=$((fail+1))
fi

# ── 9. Open file filtering ──────────────────────────────────────────
echo "[9] Open file filtering"
mkdir -p /tmp/lfscan_test_open
touch -t 202001010000 /tmp/lfscan_test_open/held.txt
touch -t 202001010000 /tmp/lfscan_test_open/free.txt
tail -f /tmp/lfscan_test_open/held.txt &
tpid=$!
sleep 0.3
out=$($BIN -d 1 -t /tmp/lfscan_test_open --no-color 2>/dev/null)
check_not "skips open file (held)" "held.txt" "$out"
check "detects free file" "free.txt" "$out"
kill $tpid 2>/dev/null || true

# ── 10. Blacklist enforcement ───────────────────────────────────────
echo "[10] Blacklist enforcement"
mkdir -p /tmp/lfscan_test_blk
touch -t 202001010000 /tmp/lfscan_test_blk/safe.txt
touch -t 202001010000 /tmp/lfscan_test_blk/keep_me.txt
out=$($BIN -d 1 -t /tmp/lfscan_test_blk -b /tmp/lfscan_test_blk/keep_me.txt --no-color 2>/dev/null)
echo "$out" | grep -q "protected paths" && { echo "  PASS: blacklist enforcement"; ((pass++)); } || { echo "  FAIL: blacklist enforcement"; ((fail++)); }

# ── 11. --json output validity ──────────────────────────────────────
echo "[11] JSON output"
mkdir -p /tmp/lfscan_test_json
touch -t 202001010000 /tmp/lfscan_test_json/entry.json
out=$($BIN -d 1 -t /tmp/lfscan_test_json --json 2>/dev/null)
check "JSON output" '"path"' "$out"
check "JSON total_files" '"total_files"' "$out"

# ── 12. --delete with --force ──────────────────────────────────────
echo "[12] Delete with --force"
mkdir -p /tmp/lfscan_test_del
touch -t 202001010000 /tmp/lfscan_test_del/to_delete.tmp
out=$($BIN -d 1 -t /tmp/lfscan_test_del --delete --force --no-color 2>/dev/null)
check "delete reported" "Deleted 1" "$out"
[ ! -f /tmp/lfscan_test_del/to_delete.tmp ] && { echo "  PASS: file actually deleted"; ((pass++)); } || { echo "  FAIL: file still exists"; ((fail++)); }

# ── 13. Quarantine mode ─────────────────────────────────────────────
echo "[13] Quarantine"
mkdir -p /tmp/lfscan_test_q
touch -t 202001010000 /tmp/lfscan_test_q/q_file.tmp
mkdir -p /tmp/lfscan_test_quar
echo 'y' | $BIN -d 1 -t /tmp/lfscan_test_q --quarantine /tmp/lfscan_test_quar --force --no-color 2>/dev/null
[ -f /tmp/lfscan_test_quar/manifest.txt ] && { echo "  PASS: quarantine created manifest"; ((pass++)); } || { echo "  FAIL: no manifest"; ((fail++)); }
[ ! -f /tmp/lfscan_test_q/q_file.tmp ] && { echo "  PASS: file moved to quarantine"; ((pass++)); } || { echo "  FAIL: file still in original location"; ((fail++)); }

# ── 14. TOML config file ────────────────────────────────────────────
echo "[14] Config file"
mkdir -p /tmp/lfscan_test_cfg
touch -t 202001010000 /tmp/lfscan_test_cfg/cfg_old.log
cat > /tmp/lfscan_test_cfg/test.toml << 'TOML'
max_age = 1
targets = ["/tmp/lfscan_test_cfg"]
TOML
out=$($BIN -c /tmp/lfscan_test_cfg/test.toml --no-color 2>/dev/null)
check "config file honored" "cfg_old.log" "$out"

# ── 15. Multi-threaded scan ────────────────────────────────────────
echo "[15] Multi-threaded scan (progress on stderr)"
rm -rf /tmp/lfscan_test_mt
mkdir -p /tmp/lfscan_test_mt
for i in $(seq 1 20); do
    touch -t 202001010000 "/tmp/lfscan_test_mt/file$i.old"
done
# Pipe through a filter to isolate our test dir's file count
out=$($BIN -d 1 -t /tmp/lfscan_test_mt --no-color 2>/dev/null)
count=$(echo "$out" | grep -c '/tmp/lfscan_test_mt' 2>/dev/null || true)
if [ "$count" -eq 20 ]; then
    echo "  PASS: multi-file detection (20 files)"
    pass=$((pass+1))
else
    echo "  FAIL: multi-file detection (expected 20, got $count)"
    fail=$((fail+1))
fi

# ── 16. Relative symlinks ───────────────────────────────────────────
echo "[16] Relative symlinks"
mkdir -p /tmp/lfscan_test_rel
touch /tmp/lfscan_test_rel/real.txt
ln -s real.txt /tmp/lfscan_test_rel/rel_valid
ln -s missing.txt /tmp/lfscan_test_rel/rel_broken
out=$(cd / && $BIN -d 3650 -t /tmp/lfscan_test_rel --no-color 2>/dev/null)
check_not "valid relative symlink not flagged" "rel_valid" "$out"
check "broken relative symlink flagged" "rel_broken" "$out"

# ── 17. Quarantine restore ──────────────────────────────────────────
echo "[17] Quarantine restore"
mkdir -p /tmp/lfscan_test_rs
touch -t 202001010000 /tmp/lfscan_test_rs/back.tmp
$BIN -d 1 -t /tmp/lfscan_test_rs --quarantine /tmp/lfscan_test_rsq --force --no-color >/dev/null 2>&1
out=$($BIN --quarantine /tmp/lfscan_test_rsq --restore 1 --no-color 2>&1)
check "restore reported" "Restored 1" "$out"
[ -f /tmp/lfscan_test_rs/back.tmp ] && { echo "  PASS: file restored"; ((pass++)); } || { echo "  FAIL: file not restored"; ((fail++)); }
out=$($BIN --restore 1 --no-color 2>&1 || true)
check "restore without a quarantine dir errors" "need --quarantine" "$out"

# ── 18. Bad config value ────────────────────────────────────────────
echo "[18] Bad config value"
mkdir -p /tmp/lfscan_test_badcfg
printf 'max_age = abc\ntargets = ["/tmp/lfscan_test_badcfg"]\n' > /tmp/lfscan_test_badcfg/bad.toml
out=$($BIN -c /tmp/lfscan_test_badcfg/bad.toml --no-color 2>&1)
check "bad config value warns instead of crashing" "invalid max_age" "$out"

# ── Summary ─────────────────────────────────────────────────────────
echo ""
echo "=== Results: $pass passed, $fail failed ==="
[ "$fail" -eq 0 ] && echo "All edge-case tests passed."
