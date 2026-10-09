/* journal.h — ProofLine tamper-evident journal.
 *
 * On-disk format ("proofline.journal", append-only):
 *   offset 0: 4 bytes magic "PLJ1"
 *   then a sequence of records; each record is ONE write() with O_APPEND:
 *     u16le rec_len        (length of canonical bytes that follow)
 *     u8[rec_len] canonical entry bytes (see pl_entry_canonical)
 *     u8[64] Ed25519 signature over the canonical bytes
 *
 * Canonical entry bytes (also what entry_hash = SHA-256 covers):
 *   "PROOFLINE1" (10 bytes, domain separator)
 *   u64le seq
 *   u8[32] image_sha256
 *   u8 verdict (0 = PASS, 1 = FAIL)
 *   u8 mid_len, u8[mid_len] model_id      (max 63)
 *   u8 mver_len, u8[mver_len] model_version (max 31)
 *   f32le confidence
 *   i64le timestamp_unix
 *   u8[32] prev_hash  (entry_hash of the previous entry; zeros for seq 0)
 *
 * Hashing uses PmCp's self-contained SHA-256 (pmcp_sha256). PmCp is the
 * dependency; the journal format and the signing layer are ProofLine's own.
 */
#ifndef PROOFLINE_JOURNAL_H
#define PROOFLINE_JOURNAL_H

#include <stddef.h>
#include <stdint.h>

#define PL_JOURNAL_MAGIC "PLJ1"
#define PL_MAGIC_LEN 4
#define PL_MODEL_ID_MAX 63
#define PL_MODEL_VER_MAX 31
#define PL_DOMAIN "PROOFLINE1"
#define PL_DOMAIN_LEN 10

/* Max canonical length: 10+8+32+1 + (1+63) + (1+31) + 4+8+32 = 191 */
#define PL_CANONICAL_MAX 191

typedef struct {
    uint64_t seq;
    unsigned char image_sha256[32];
    int verdict; /* 0 = PASS, 1 = FAIL */
    char model_id[PL_MODEL_ID_MAX + 1];
    char model_ver[PL_MODEL_VER_MAX + 1];
    float confidence;
    int64_t timestamp_unix;
    unsigned char prev_hash[32];
    unsigned char sig[64]; /* valid after append/read; zeroed before signing */
} pl_entry_t;

/* Serialize e (without sig) into out; sets *len. Returns 0 on success. */
int pl_entry_canonical(const pl_entry_t *e, unsigned char out[PL_CANONICAL_MAX],
                       size_t *len);

/* entry_hash = SHA-256(canonical bytes). Uses pmcp_sha256. */
void pl_entry_hash(const pl_entry_t *e, unsigned char out[32]);

/* Append e to path: signs canonical bytes with sk, single O_APPEND write.
 * Creates the file (with magic) if absent. Returns 0 on success, -1 on error.
 * On error, *errmsg points at a static string describing the failure. */
int pl_journal_append(const char *path, pl_entry_t *e,
                      const unsigned char sk[64], const char **errmsg);

/* Read and parse every record in path. Allocates *out (caller frees).
 * Returns 0 on success; -1 on error with *errmsg set. A structurally
 * invalid record is an error (not a silent skip). */
int pl_journal_read_all(const char *path, pl_entry_t **out, size_t *n,
                        const char **errmsg);

/* Hex helpers. hex must have room for 2*len+1 bytes. */
void pl_hex(const unsigned char *bin, size_t len, char *hex);
int pl_unhex(const char *hex, unsigned char *bin, size_t len);

#endif /* PROOFLINE_JOURNAL_H */
