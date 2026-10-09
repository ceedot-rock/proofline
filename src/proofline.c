/* proofline.c — CLI for ProofLine: verifiable on-device visual inspection.
 *
 *   proofline keygen [--force]   generate the Ed25519 device keypair
 *   proofline inspect <image.ppm>  classify + append a signed journal entry
 *   proofline verify              replay the journal, check chain + signatures
 *   proofline export <path>      copy the journal for an auditor
 *
 * Files (all in the current directory):
 *   proofline.key      64-byte Ed25519 secret key (created 0600, never shared)
 *   proofline.pub      32-byte Ed25519 public key (shared with auditors)
 *   proofline.journal  the tamper-evident journal (hash-chained, signed)
 *   proofline.pmcp[.journal]  PmCp region: pinned image blobs + crash safety
 *
 * PmCp (https://github.com/ceedot-rock/pmcp, Apache-2.0) is the dependency:
 * image bytes are pinned as PmCp extents and hashed with PmCp's SHA-256.
 * The journal format, the hash chain, and the Ed25519 signing layer are
 * ProofLine's own (PmCp milestone 2 / Chamber signing is not built yet).
 */
#include "classifier.h"
#include "journal.h"
#include "sign.h"

#include "pmcp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define KEY_PATH "proofline.key"
#define PUB_PATH "proofline.pub"
#define JOURNAL_PATH "proofline.journal"
#define REGION_NAME "proofline"

static void usage(const char *prog) {
    fprintf(stderr,
            "ProofLine — verifiable on-device visual quality inspection.\n"
            "\n"
            "Usage:\n"
            "  %s keygen [--force]    Generate the Ed25519 device keypair.\n"
            "  %s inspect <image>     Classify a P6 PPM image, append a signed\n"
            "                         journal entry (verdict + model id +\n"
            "                         timestamp, hash-chained, Ed25519-signed).\n"
            "  %s verify              Replay the journal: check the hash chain,\n"
            "                         every signature, and every pinned image.\n"
            "  %s export <path>       Copy the journal file for an auditor.\n"
            "\n"
            "Notes:\n"
            "  - The Stage I classifier is a labeled placeholder heuristic\n"
            "    over synthetic images (see README: \"what's real\").\n"
            "  - PmCp (github.com/ceedot-rock/pmcp) provides the persistent\n"
            "    image-blob store and SHA-256; the journal and signing layer\n"
            "    are implemented in this repo.\n",
            prog, prog, prog, prog);
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Read an entire file into memory. Caller frees. Returns 0 on success. */
static int read_file(const char *path, unsigned char **out, size_t *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf = NULL;
    size_t cap = 0, n = 0;
    *out = NULL;
    *len = 0;
    if (!f)
        return -1;
    for (;;) {
        if (n == cap) {
            size_t ncap = cap ? cap * 2 : 65536;
            unsigned char *nb = realloc(buf, ncap);
            if (!nb) {
                free(buf);
                fclose(f);
                return -1;
            }
            buf = nb;
            cap = ncap;
        }
        size_t r = fread(buf + n, 1, cap - n, f);
        n += r;
        if (r == 0) {
            if (ferror(f)) {
                free(buf);
                fclose(f);
                return -1;
            }
            break;
        }
    }
    fclose(f);
    *out = buf;
    *len = n;
    return 0;
}

static void print_entry(const pl_entry_t *e) {
    char imghex[65], prevhex[65], sighex[129];
    char tbuf[32];
    time_t t = (time_t)e->timestamp_unix;
    struct tm tmv;
    pl_hex(e->image_sha256, 32, imghex);
    pl_hex(e->prev_hash, 32, prevhex);
    pl_hex(e->sig, 64, sighex);
    if (gmtime_r(&t, &tmv))
        strftime(tbuf, sizeof tbuf, "%Y-%m-%dT%H:%M:%SZ", &tmv);
    else
        snprintf(tbuf, sizeof tbuf, "?");
    printf("seq=%llu\n", (unsigned long long)e->seq);
    printf("verdict=%s\n", e->verdict ? "FAIL" : "PASS");
    printf("model=%s version=%s confidence=%.2f\n", e->model_id, e->model_ver,
           e->confidence);
    printf("timestamp=%lld (%s)\n", (long long)e->timestamp_unix, tbuf);
    printf("image_sha256=%s\n", imghex);
    printf("prev_hash=%.16s...\n", prevhex);
    printf("signature=%.16s...\n", sighex);
}

/* --- keygen --- */

static int cmd_keygen(int force) {
    unsigned char pk[PL_PUBKEY_BYTES], sk[PL_SECRETKEY_BYTES];
    FILE *f;

    if (!force && (file_exists(KEY_PATH) || file_exists(PUB_PATH))) {
        fprintf(stderr,
                "error: keypair already exists (%s / %s).\n"
                "Refusing to overwrite. Use --force to replace it (old\n"
                "signatures will no longer verify against the new key).\n",
                KEY_PATH, PUB_PATH);
        return 1;
    }
    if (pl_sign_init() != 0) {
        fprintf(stderr, "error: crypto backend init failed.\n");
        return 1;
    }
    if (pl_keygen(pk, sk) != 0) {
        fprintf(stderr, "error: key generation failed.\n");
        return 1;
    }
    f = fopen(KEY_PATH, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write %s.\n", KEY_PATH);
        return 1;
    }
    if (fwrite(sk, 1, sizeof sk, f) != sizeof sk) {
        fprintf(stderr, "error: cannot write %s.\n", KEY_PATH);
        fclose(f);
        return 1;
    }
    fclose(f);
    chmod(KEY_PATH, 0600);
    memset(sk, 0, sizeof sk);

    f = fopen(PUB_PATH, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write %s.\n", PUB_PATH);
        return 1;
    }
    if (fwrite(pk, 1, sizeof pk, f) != sizeof pk) {
        fprintf(stderr, "error: cannot write %s.\n", PUB_PATH);
        fclose(f);
        return 1;
    }
    fclose(f);

    {
        char hex[65];
        pl_hex(pk, sizeof pk, hex);
        printf("keypair written: %s (secret, mode 0600 — never share)\n", KEY_PATH);
        printf("                 %s (public — give to auditors)\n", PUB_PATH);
        printf("public key: %s\n", hex);
    }
    return 0;
}

/* Pin raw image bytes as a PmCp extent and return their SHA-256.
 * The extent name is img-<seq>. Returns 0 on success. */
static int pin_image(uint64_t seq, const unsigned char *bytes, size_t len,
                     unsigned char img_sha[32]) {
    pmcp_region_t *r = NULL;
    pmcp_extent_t *e = NULL;
    char name[64];
    int rc = -1;

    snprintf(name, sizeof name, "img-%llu", (unsigned long long)seq);
    r = pmcp_open(REGION_NAME);
    if (!r) {
        fprintf(stderr, "error: cannot open PmCp region \"%s\".\n", REGION_NAME);
        return -1;
    }
    /* Fresh bytes win: the journal entry binds the hash, so a replaced
     * blob is caught at verify time. */
    e = pmcp_extent(r, name);
    if (e) {
        pmcp_extent_free(e);
        e = NULL;
        if (pmcp_extent_delete(r, name) != 0) {
            fprintf(stderr, "error: cannot replace image extent %s.\n", name);
            goto out;
        }
    }
    e = pmcp_extent_create(r, name, bytes, len, PMCP_DTYPE_RAW);
    if (!e) {
        fprintf(stderr, "error: cannot pin image in PmCp region.\n");
        goto out;
    }
    if (pmcp_extent_hash(e, img_sha) != 0) {
        fprintf(stderr, "error: cannot hash pinned image.\n");
        goto out;
    }
    rc = 0;
out:
    if (e)
        pmcp_extent_free(e);
    pmcp_close(r);
    return rc;
}

/* --- inspect --- */

static int cmd_inspect(const char *image_path) {
    unsigned char *file_bytes = NULL, *rgb = NULL;
    size_t file_len = 0;
    int w = 0, h = 0;
    float confidence = 0.0f;
    int verdict;
    pl_entry_t *ents = NULL;
    size_t nent = 0;
    const char *errmsg = NULL;
    unsigned char sk[PL_SECRETKEY_BYTES];
    pl_entry_t e;
    unsigned char zero[32];
    FILE *kf;
    int rc = 1;

    if (pl_sign_init() != 0) {
        fprintf(stderr, "error: crypto backend init failed.\n");
        return 1;
    }
    kf = fopen(KEY_PATH, "rb");
    if (!kf) {
        fprintf(stderr,
                "error: no device keypair (%s missing).\n"
                "Run 'proofline keygen' first.\n",
                KEY_PATH);
        return 1;
    }
    if (fread(sk, 1, sizeof sk, kf) != sizeof sk) {
        fprintf(stderr, "error: %s is corrupt (want %d bytes).\n", KEY_PATH,
                PL_SECRETKEY_BYTES);
        fclose(kf);
        return 1;
    }
    fclose(kf);

    if (read_file(image_path, &file_bytes, &file_len) != 0 || file_len == 0) {
        fprintf(stderr, "error: cannot read image '%s'.\n", image_path);
        goto out;
    }
    if (pl_read_ppm(image_path, &rgb, &w, &h) != 0) {
        fprintf(stderr,
                "error: '%s' is not a readable binary PPM (P6) image.\n",
                image_path);
        goto out;
    }

    /* PLACEHOLDER classifier (see classifier.h): heuristic, not a model. */
    verdict = pl_classify(rgb, w, h, &confidence);

    memset(&e, 0, sizeof e);
    memset(zero, 0, sizeof zero);
    if (file_exists(JOURNAL_PATH)) {
        if (pl_journal_read_all(JOURNAL_PATH, &ents, &nent, &errmsg) != 0) {
            fprintf(stderr, "error: journal unreadable: %s.\n", errmsg);
            goto out;
        }
        e.seq = (uint64_t)nent;
        if (nent > 0)
            pl_entry_hash(&ents[nent - 1], e.prev_hash);
        else
            memcpy(e.prev_hash, zero, 32);
    } else {
        e.seq = 0;
        memcpy(e.prev_hash, zero, 32);
    }

    if (pin_image(e.seq, file_bytes, file_len, e.image_sha256) != 0)
        goto out;

    e.verdict = verdict;
    strncpy(e.model_id, PL_MODEL_ID, PL_MODEL_ID_MAX);
    strncpy(e.model_ver, PL_MODEL_VER, PL_MODEL_VER_MAX);
    e.confidence = confidence;
    e.timestamp_unix = (int64_t)time(NULL);

    if (pl_journal_append(JOURNAL_PATH, &e, sk, &errmsg) != 0) {
        fprintf(stderr, "error: journal append failed: %s.\n", errmsg);
        goto out;
    }

    print_entry(&e);
    rc = 0;
out:
    memset(sk, 0, sizeof sk);
    free(file_bytes);
    free(rgb);
    free(ents);
    return rc;
}

/* --- verify --- */

static int cmd_verify(void) {
    unsigned char pk[PL_PUBKEY_BYTES];
    pl_entry_t *ents = NULL;
    size_t nent = 0;
    const char *errmsg = NULL;
    pmcp_region_t *r = NULL;
    size_t ok = 0;
    FILE *pf;
    int rc = 1;

    if (pl_sign_init() != 0) {
        fprintf(stderr, "error: crypto backend init failed.\n");
        return 1;
    }
    if (!file_exists(JOURNAL_PATH)) {
        fprintf(stderr,
                "error: no journal found (%s missing).\n"
                "Run 'proofline inspect <image>' first.\n",
                JOURNAL_PATH);
        return 1;
    }
    pf = fopen(PUB_PATH, "rb");
    if (!pf) {
        fprintf(stderr,
                "error: no public key (%s missing).\n"
                "Without the device public key, signatures cannot be checked.\n",
                PUB_PATH);
        return 1;
    }
    if (fread(pk, 1, sizeof pk, pf) != sizeof pk) {
        fprintf(stderr, "error: %s is corrupt (want %d bytes).\n", PUB_PATH,
                PL_PUBKEY_BYTES);
        fclose(pf);
        return 1;
    }
    fclose(pf);

    if (pl_journal_read_all(JOURNAL_PATH, &ents, &nent, &errmsg) != 0) {
        fprintf(stderr, "error: journal unreadable: %s.\n", errmsg);
        printf("VERIFICATION FAILED: journal is corrupt or unreadable.\n");
        return 1;
    }

    r = pmcp_open(REGION_NAME);
    if (!r) {
        fprintf(stderr, "error: cannot open PmCp region \"%s\".\n", REGION_NAME);
        free(ents);
        return 1;
    }

    for (size_t i = 0; i < nent; i++) {
        pl_entry_t *e = &ents[i];
        unsigned char canon[PL_CANONICAL_MAX];
        size_t clen = 0;
        unsigned char expect_prev[32], h[32];
        char fail_reason[128] = "";
        char imghex[65];

        pl_hex(e->image_sha256, 32, imghex);

        if (e->seq != (uint64_t)i)
            snprintf(fail_reason, sizeof fail_reason,
                     "sequence break (entry %llu at position %zu)",
                     (unsigned long long)e->seq, i);
        else if (pl_entry_canonical(e, canon, &clen) != 0)
            snprintf(fail_reason, sizeof fail_reason, "entry out of range");
        else {
            if (i == 0)
                memset(expect_prev, 0, 32);
            else {
                pl_entry_t prev = ents[i - 1];
                memset(prev.sig, 0, sizeof prev.sig);
                pl_entry_hash(&prev, expect_prev);
            }
            if (memcmp(e->prev_hash, expect_prev, 32) != 0)
                snprintf(fail_reason, sizeof fail_reason, "hash chain broken");
            else if (pl_verify_detached(e->sig, canon, clen, pk) != 0)
                snprintf(fail_reason, sizeof fail_reason,
                         "bad Ed25519 signature");
            else {
                /* Re-hash the pinned image blob and compare. */
                char name[64];
                pmcp_extent_t *im;
                snprintf(name, sizeof name, "img-%llu",
                         (unsigned long long)e->seq);
                im = pmcp_extent(r, name);
                if (!im)
                    snprintf(fail_reason, sizeof fail_reason,
                             "pinned image blob %s missing", name);
                else {
                    /* Recompute the hash from the live heap bytes (not the
                     * stored op-time hash) so altered bytes are caught. */
                    const void *bp = pmcp_extent_ptr(im);
                    size_t blen = pmcp_extent_len(im);
                    if (!bp)
                        snprintf(fail_reason, sizeof fail_reason,
                                 "cannot read pinned image blob %s", name);
                    else {
                        pmcp_sha256(bp, blen, h);
                        if (memcmp(h, e->image_sha256, 32) != 0)
                            snprintf(fail_reason, sizeof fail_reason,
                                     "image hash mismatch (blob altered)");
                    }
                    pmcp_extent_free(im);
                }
            }
        }

        if (fail_reason[0]) {
            printf("seq=%llu verdict=%s img=%.16s... FAIL (%s)\n",
                   (unsigned long long)e->seq, e->verdict ? "FAIL" : "PASS",
                   imghex, fail_reason);
        } else {
            printf("seq=%llu verdict=%s img=%.16s... OK\n",
                   (unsigned long long)e->seq, e->verdict ? "FAIL" : "PASS",
                   imghex);
            ok++;
        }
    }

    pmcp_close(r);
    free(ents);

    if (ok == nent) {
        printf("%zu/%zu entries verified, chain intact.\n", ok, nent);
        rc = 0;
    } else {
        printf("VERIFICATION FAILED: %zu/%zu entries OK.\n", ok, nent);
        rc = 1;
    }
    return rc;
}

/* --- export --- */

static int cmd_export(const char *dest) {
    unsigned char *buf = NULL;
    size_t len = 0;
    FILE *f;

    if (!file_exists(JOURNAL_PATH)) {
        fprintf(stderr, "error: no journal found (%s missing).\n", JOURNAL_PATH);
        return 1;
    }
    if (read_file(JOURNAL_PATH, &buf, &len) != 0) {
        fprintf(stderr, "error: cannot read %s.\n", JOURNAL_PATH);
        return 1;
    }
    f = fopen(dest, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write '%s'.\n", dest);
        free(buf);
        return 1;
    }
    if (len && fwrite(buf, 1, len, f) != len) {
        fprintf(stderr, "error: cannot write '%s'.\n", dest);
        fclose(f);
        free(buf);
        return 1;
    }
    fclose(f);
    free(buf);
    printf("journal exported: %s (%zu bytes, %s alongside for auditors)\n", dest,
           len, PUB_PATH);
    return 0;
}

/* --- main --- */

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "keygen") == 0) {
        int force = argc > 2 && strcmp(argv[2], "--force") == 0;
        if (argc > 3 || (argc > 2 && !force)) {
            usage(argv[0]);
            return 2;
        }
        return cmd_keygen(force);
    }
    if (strcmp(argv[1], "inspect") == 0) {
        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }
        return cmd_inspect(argv[2]);
    }
    if (strcmp(argv[1], "verify") == 0) {
        if (argc != 2) {
            usage(argv[0]);
            return 2;
        }
        return cmd_verify();
    }
    if (strcmp(argv[1], "export") == 0) {
        if (argc != 3) {
            usage(argv[0]);
            return 2;
        }
        return cmd_export(argv[2]);
    }
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "help") == 0) {
        usage(argv[0]);
        return 0;
    }
    fprintf(stderr, "error: unknown command '%s'.\n\n", argv[1]);
    usage(argv[0]);
    return 2;
}
