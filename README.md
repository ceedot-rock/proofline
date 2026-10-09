# ProofLine

**Verifiable on-device visual quality inspection** — Stage I entry for the
ASUS UGen AI League Innovation Hackathon
(Battlefield Lightning — Lightweight Computing Track, Workplace AI theme).

Team: Slid Phi Labs.

## The idea in one paragraph

A factory camera passes or fails every part, but the verdict itself is not
evidence: logs can be edited, timestamps drift, and nobody can prove which
model decided what, when. ProofLine makes the verdict *evidence*. Each
inspection — one image in — produces a verdict (PASS/FAIL), the model id and
version, and a timestamp. That record is appended to a **tamper-evident
journal**: every entry is hash-chained to the previous one and digitally
signed (Ed25519). `proofline verify` replays the whole journal offline —
recomputing every hash, re-checking every signature, and re-hashing every
pinned image — so any auditor can confirm the record is intact without
trusting the device, the network, or us.

What this proves: *which* image got *which* verdict from *which* model at
*what* time, and that the record has not been altered since. What it does
**not** prove: that the model was right. Correctness is a model-validation
question; ProofLine proves **provenance**.

## Architecture

```
                    +------------------+
                    | Camera / image   |
                    | feed (PPM file)  |
                    +--------+---------+
                             |
                             v
                    +------------------+      +------------------+
                    | Defect model     |----->| Verdict builder  |
                    | PLACEHOLDER:     |      | PASS/FAIL, model |
                    | labeled heuristic|      | id+version,      |
                    | (Stage I)        |      | timestamp,       |
                    +------------------+      | image SHA-256    |
                                              +--------+---------+
                                                       |
                                                       v
                                              +------------------+
                                              | ProofLine journal|
                                              | proofline.journal|
                                              |  hash chain +    |
                                              |  Ed25519 per     |
                                              |  entry (this     |
                                              |  repo's format)  |
                                              +--------+---------+
                                                       |
                                        +--------------+--------------+
                                        |                             |
                                        v                             v
                              PmCp region "proofline"        proofline export
                              (pinned image blobs,           (auditor copy
                               crash-safe extent              of journal +
                               store, SHA-256)                proofline.pub)
                                        |
                                        v
                              +------------------+
                              | proofline verify |
                              | offline replay:  |
                              | chain + sigs +   |
                              | image hashes     |
                              +------------------+
```

**Layering, honestly stated.** PmCp (Persistent Memory Core Processing,
Apache-2.0, https://github.com/ceedot-rock/pmcp) is the *dependency*, not the
entry: it provides the persistent image-blob store (crash-safe extent
manager) and the self-contained SHA-256 used for every hash. The journal
*format*, the hash chain, and the Ed25519 signing layer are implemented in
this repo (PmCp milestone 2 / Chamber signing is not built yet — when it
lands, `src/sign.*` is the seam to replace).

## Quickstart

Prerequisites: a C compiler, `make`, Python 3 + Pillow (for the synthetic
image generator), and libsodium. If your system has `libsodium-dev`
(`pkg-config` finds it), that is used; otherwise the build compiles against
vendored libsodium *public headers* (`third_party/libsodium`) and links
your system's libsodium runtime.

```bash
# 1. Check out PmCp next to this repo (it is NOT vendored here)
git clone https://github.com/ceedot-rock/pmcp
git clone https://github.com/ceedot-rock/proofline
cd proofline

# 2. Build and run the full test suite (22 checks, all green)
make test

# 3. Try the demo by hand
./proofline keygen                                   # one-time device keypair
python3 tools/gen_images.py demo_images 2 2         # synthetic test images
./proofline inspect demo_images/good_000.ppm
./proofline inspect demo_images/bad_001.ppm
./proofline verify
./proofline export journal-for-auditor.bin           # hand this + proofline.pub out
```

## Demo transcript

```
$ ./proofline keygen
keypair written: proofline.key (secret, mode 0600 — never share)
                 proofline.pub (public — give to auditors)
public key: 8fc824b3...63777

$ ./proofline inspect demo_images/good_000.ppm
seq=0
verdict=PASS
model=heuristic-v0.1 version=0.1.0-stage1 confidence=1.00
timestamp=1791585604 (2026-10-09T22:40:04Z)
image_sha256=90cb6425...13b07
prev_hash=0000000000000000...
signature=9cae771d...6047...

$ ./proofline inspect demo_images/bad_001.ppm
seq=1
verdict=FAIL
...

$ ./proofline verify
seq=0 verdict=PASS img=90cb642534c051a1... OK
seq=1 verdict=FAIL img=0300c8156bd7bd4d... OK
2/2 entries verified, chain intact.
```

Tamper with one byte of `proofline.journal` and `verify` fails loudly:

```
$ ./proofline verify
seq=0 verdict=PASS img=90cb642534c051a1... OK
seq=1 verdict=FAIL img=0300c8156bd7bd4d... FAIL (bad Ed25519 signature)
VERIFICATION FAILED: 1/2 entries OK.
```

## CLI reference

| Command | What it does |
|---|---|
| `proofline keygen [--force]` | Generate the Ed25519 device keypair (`proofline.key` 0600, `proofline.pub`). Refuses to overwrite without `--force`. |
| `proofline inspect <image.ppm>` | Classify a binary PPM (P6) image, pin its bytes as a PmCp extent, append a signed journal entry, print the entry. |
| `proofline verify` | Replay the journal: per-entry chain link, signature, and pinned-image hash; prints OK/FAIL per entry and a final verdict. Exit 0 = intact, 1 = tampered. |
| `proofline export <path>` | Byte-identical copy of the journal for an auditor (pair with `proofline.pub`). |

Files created in the working directory: `proofline.key`, `proofline.pub`,
`proofline.journal`, `proofline.pmcp` + `proofline.pmcp.journal` (PmCp
region). Keep `proofline.key` secret; everything else is safe to share.

## Verdict schema

Each journal entry carries exactly what an auditor needs:

```
seq            uint64, 0-based position in the journal
image_sha256   SHA-256 of the pinned image bytes (PmCp)
verdict        PASS or FAIL
model_id       e.g. "heuristic-v0.1"
model_version  e.g. "0.1.0-stage1"
confidence     0..1 float from the classifier
timestamp      int64 unix seconds (UTC)
prev_hash      SHA-256 of the previous entry's canonical bytes (zeros for seq 0)
signature      Ed25519 over the canonical entry bytes above
```

The canonical byte layout is documented in `src/journal.h`. The signature
covers every field; the hash chain commits the order.

## Trust model — and its limits

- **Hash chain:** entry N commits to entry N−1. Altering any entry breaks
  every later link. Reordering or backdating is caught the same way.
- **Ed25519 per entry:** proves the entry was written by the holder of the
  device key. Verification needs only the public key — fully offline.
- **Pinned images:** verdicts bind to image bytes stored in the PmCp
  region; `verify` re-hashes the stored bytes and compares.

Limits, stated up front:

- ProofLine protects the record **after** it is written. It does not secure
  the camera feed itself.
- It does not prove the model was **correct** — only what it decided.
- Key custody is the operator's responsibility. A stolen device key can
  mint new (validly signed) history; Stage II includes a device
  key-provisioning plan.

## What's real vs. placeholder (read this before judging)

**Real:** the journal format, the hash chain, Ed25519 sign/verify via
libsodium, the PmCp integration (persistent blob store + SHA-256), offline
replay verification, tamper detection (journal-byte flip and image-blob
alteration both fail verification — covered by `make test`).

**Placeholder, labeled as such:** the defect classifier is a statistical
anomaly heuristic (grayscale mean/stddev, anomalous-pixel fraction), not a
trained model. The test images are **synthetic** (generated by
`tools/gen_images.py` — uniform surfaces plus noise; "defects" are drawn
scratches and blotches). Stage I validates the *mechanism* end to end;
Stage II compiles a real defect-detection model for the Hailo-10H on the
ASUS UGen300 and publishes measured throughput.

**Not claimed:** no cloud attestation, no integration with any external
signing service, no on-device benchmark numbers yet.

## Repository layout

```
proofline/
  src/                 proofline.c (CLI), journal.c/.h, classifier.c/.h,
                       sign.h + sign_sodium.c (Ed25519 layer)
  tools/gen_images.py  SYNTHETIC image generator (PIL) + labels.csv
  tests/test.sh        22-check suite: happy path + tamper detection
  third_party/libsodium  vendored libsodium *public headers* only (ISC),
                       compile-time fallback; see its README.md
  deck/                Stage I proposal deck (reference, not the build)
  .github/workflows/   CI: build + make test on ubuntu
```

PmCp is a *build dependency* (`PMCP_DIR`, default `../pmcp`) — it is never
copied into this repo.

## License

Apache-2.0 — see LICENSE. Copyright 2026 Slid Phi Labs / Corey Tasz.
