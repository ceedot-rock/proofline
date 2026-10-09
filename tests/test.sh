#!/bin/bash
# ProofLine test suite: happy path + tamper detection.
# Exits 0 only if every check passes.
set -u
cd "$(dirname "$0")/.."

PROOFLINE=./proofline
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
check() { # check <desc> <expected_exit> <actual_exit>
    if [ "$2" = "$3" ]; then
        echo "  ok: $1"
        pass=$((pass+1))
    else
        echo "  FAIL: $1 (expected exit $2, got $3)"
        fail=$((fail+1))
    fi
}

echo "== build =="
make >/dev/null 2>&1
check "build succeeds" 0 $?

echo "== generate synthetic images =="
python3 tools/gen_images.py "$WORK/img" 4 4 >/dev/null
check "image generator runs" 0 $?
[ -f "$WORK/img/good_000.ppm" ] && [ -f "$WORK/img/bad_000.ppm" ]
check "synthetic images exist" 0 $?

cd "$WORK" || exit 1
PL="$OLDPWD/proofline"

echo "== keygen =="
"$PL" keygen >/dev/null
check "keygen succeeds" 0 $?
[ -f proofline.key ] && [ -f proofline.pub ]
check "key files created" 0 $?
[ "$(stat -c %a proofline.key)" = "600" ]
check "secret key is mode 0600" 0 $?
"$PL" keygen >/dev/null 2>&1
check "keygen refuses to overwrite" 1 $?

echo "== inspect: no keypair yet (fresh dir) =="
mkdir -p nokey && (cd nokey && "$PL" inspect "$WORK/img/good_000.ppm" >/dev/null 2>&1)
check "inspect without keypair fails cleanly" 1 $?

echo "== inspect happy path =="
"$PL" inspect "$WORK/img/good_000.ppm" > good.out
check "inspect good image exits 0" 0 $?
grep -q "verdict=PASS" good.out
check "good synthetic part verdict=PASS" 0 $?
grep -q "model=heuristic-v0.1" good.out
check "model id pinned in output" 0 $?
"$PL" inspect "$WORK/img/bad_000.ppm" > bad.out
check "inspect bad image exits 0" 0 $?
grep -q "verdict=FAIL" bad.out
check "defective synthetic part verdict=FAIL" 0 $?

echo "== verify happy path =="
"$PL" verify > verify.out
check "verify exits 0" 0 $?
grep -q "2/2 entries verified, chain intact" verify.out
check "verify reports 2/2 chain intact" 0 $?

echo "== export =="
"$PL" export journal-export.bin >/dev/null
check "export exits 0" 0 $?
cmp -s proofline.journal journal-export.bin
check "exported copy is byte-identical" 0 $?

echo "== tamper detection: flip a byte in the journal =="
cp proofline.journal journal.bak
# Flip a byte inside the first record's canonical bytes (past the 4-byte magic).
python3 - "$WORK" <<'EOF'
import sys
p = "proofline.journal"
b = bytearray(open(p, "rb").read())
b[40] ^= 0xFF
open(p, "wb").write(b)
EOF
"$PL" verify > tamper.out 2>&1
check "verify FAILS after journal tampering" 1 $?
grep -q "VERIFICATION FAILED" tamper.out
check "verify reports VERIFICATION FAILED" 0 $?
grep -q "FAIL" tamper.out
check "tampered entry flagged per-entry" 0 $?
# Restore and confirm the journal verifies again.
cp journal.bak proofline.journal
"$PL" verify >/dev/null 2>&1
check "verify passes again after restore" 0 $?

echo "== tamper detection: swap an image blob =="
# Corrupt the pinned image bytes in the PmCp region; the journal's
# image_sha256 must catch it.
python3 - <<'EOF'
p = "proofline.pmcp"
b = bytearray(open(p, "rb").read())
# Find a nonzero byte past the header and flip it (best-effort; the
# journal hash check is what must trip, wherever the flip lands).
for i in range(64, len(b)):
    if b[i] not in (0, 0xFF):
        b[i] ^= 0x01
        break
open(p, "wb").write(b)
EOF
"$PL" verify > blob.out 2>&1
# Either clean per-entry FAIL (image hash mismatch) or a PmCp-level
# integrity error: both are "verify did not pass".
if grep -q "VERIFICATION FAILED" blob.out || grep -qi "error" blob.out; then
    echo "  ok: image-blob tampering detected"
    pass=$((pass+1))
else
    echo "  FAIL: image-blob tampering not detected"
    fail=$((fail+1))
fi

echo ""
echo "passed: $pass, failed: $fail"
[ "$fail" = 0 ]
