/* journal.c — ProofLine tamper-evident journal implementation. */
#include "journal.h"
#include "sign.h"

#include "pmcp.h" /* pmcp_sha256 — PmCp is the hashing dependency */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void put_u16le(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put_u64le(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; i++)
        p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
}

static void put_f32le(unsigned char *p, float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    p[0] = (unsigned char)(u & 0xff);
    p[1] = (unsigned char)((u >> 8) & 0xff);
    p[2] = (unsigned char)((u >> 16) & 0xff);
    p[3] = (unsigned char)((u >> 24) & 0xff);
}

static uint16_t get_u16le(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

int pl_entry_canonical(const pl_entry_t *e, unsigned char out[PL_CANONICAL_MAX],
                       size_t *len) {
    size_t mid_len = strlen(e->model_id);
    size_t mver_len = strlen(e->model_ver);
    unsigned char *p = out;

    if (mid_len > PL_MODEL_ID_MAX || mver_len > PL_MODEL_VER_MAX)
        return -1;

    memcpy(p, PL_DOMAIN, PL_DOMAIN_LEN);
    p += PL_DOMAIN_LEN;
    put_u64le(p, e->seq);
    p += 8;
    memcpy(p, e->image_sha256, 32);
    p += 32;
    *p++ = (unsigned char)(e->verdict ? 1 : 0);
    *p++ = (unsigned char)mid_len;
    memcpy(p, e->model_id, mid_len);
    p += mid_len;
    *p++ = (unsigned char)mver_len;
    memcpy(p, e->model_ver, mver_len);
    p += mver_len;
    put_f32le(p, e->confidence);
    p += 4;
    put_u64le(p, (uint64_t)e->timestamp_unix);
    p += 8;
    memcpy(p, e->prev_hash, 32);
    p += 32;

    *len = (size_t)(p - out);
    return 0;
}

void pl_entry_hash(const pl_entry_t *e, unsigned char out[32]) {
    unsigned char canon[PL_CANONICAL_MAX];
    size_t len = 0;
    if (pl_entry_canonical(e, canon, &len) != 0) {
        memset(out, 0, 32);
        return;
    }
    pmcp_sha256(canon, len, out);
}

int pl_journal_append(const char *path, pl_entry_t *e,
                      const unsigned char sk[64], const char **errmsg) {
    unsigned char canon[PL_CANONICAL_MAX];
    size_t clen = 0;
    unsigned char rec[2 + PL_CANONICAL_MAX + 64];
    size_t reclen;
    int fd = -1;
    int rc = -1;

    static const char *E_CANON = "entry fields out of range";
    static const char *E_SIGN = "signing failed";
    static const char *E_OPEN = "cannot open journal file";
    static const char *E_HDR = "cannot write journal header";
    static const char *E_WRITE = "cannot append journal record";

    if (pl_entry_canonical(e, canon, &clen) != 0) {
        *errmsg = E_CANON;
        return -1;
    }
    if (pl_sign_detached(e->sig, canon, clen, sk) != 0) {
        *errmsg = E_SIGN;
        return -1;
    }

    put_u16le(rec, (uint16_t)clen);
    memcpy(rec + 2, canon, clen);
    memcpy(rec + 2 + clen, e->sig, 64);
    reclen = 2 + clen + 64;

    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        *errmsg = E_OPEN;
        return -1;
    }
    /* New file: write the magic header first. O_APPEND + single write()
     * per record keeps appends atomic for readers. */
    {
        unsigned char magic[PL_MAGIC_LEN];
        ssize_t n = 0;
        /* Detect a fresh file: size 0. (O_APPEND lseeks to end anyway.) */
        off_t end = lseek(fd, 0, SEEK_END);
        if (end < 0) {
            *errmsg = E_HDR;
            goto out;
        }
        if (end == 0) {
            memcpy(magic, PL_JOURNAL_MAGIC, PL_MAGIC_LEN);
            n = write(fd, magic, PL_MAGIC_LEN);
            if (n != PL_MAGIC_LEN) {
                *errmsg = E_HDR;
                goto out;
            }
        }
    }
    if (write(fd, rec, reclen) != (ssize_t)reclen) {
        *errmsg = E_WRITE;
        goto out;
    }
    rc = 0;
out:
    close(fd);
    return rc;
}

int pl_journal_read_all(const char *path, pl_entry_t **out, size_t *n,
                        const char **errmsg) {
    static const char *E_OPEN = "cannot open journal file";
    static const char *E_READ = "cannot read journal file";
    static const char *E_MAGIC = "bad journal magic (not a ProofLine journal)";
    static const char *E_TRUNC = "truncated journal record";
    static const char *E_BIG = "journal record too large";
    static const char *E_BAD = "malformed journal record";
    static const char *E_NOMEM = "out of memory";

    int fd = -1;
    unsigned char *buf = NULL;
    size_t cap = 0, len = 0;
    pl_entry_t *ents = NULL;
    size_t nent = 0, ecap = 0;

    *out = NULL;
    *n = 0;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        *errmsg = E_OPEN;
        return -1;
    }
    for (;;) {
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 65536;
            unsigned char *nb = realloc(buf, ncap);
            if (!nb) {
                *errmsg = E_NOMEM;
                goto fail;
            }
            buf = nb;
            cap = ncap;
        }
        ssize_t r = read(fd, buf + len, cap - len);
        if (r < 0) {
            *errmsg = E_READ;
            goto fail;
        }
        if (r == 0)
            break;
        len += (size_t)r;
    }
    close(fd);
    fd = -1;

    if (len < PL_MAGIC_LEN ||
        memcmp(buf, PL_JOURNAL_MAGIC, PL_MAGIC_LEN) != 0) {
        *errmsg = E_MAGIC;
        goto fail;
    }

    {
        size_t off = PL_MAGIC_LEN;
        while (off < len) {
            uint16_t clen;
            const unsigned char *canon;
            const unsigned char *sig;
            pl_entry_t e;
            size_t p;
            size_t mid_len, mver_len;

            if (len - off < 2) {
                *errmsg = E_TRUNC;
                goto fail;
            }
            clen = get_u16le(buf + off);
            if (clen == 0 || clen > PL_CANONICAL_MAX) {
                *errmsg = E_BIG;
                goto fail;
            }
            if (len - off < (size_t)2 + clen + 64) {
                *errmsg = E_TRUNC;
                goto fail;
            }
            canon = buf + off + 2;
            sig = canon + clen;

            /* Parse canonical fields back into an entry. */
            memset(&e, 0, sizeof e);
            p = 0;
            if (clen < PL_DOMAIN_LEN ||
                memcmp(canon, PL_DOMAIN, PL_DOMAIN_LEN) != 0) {
                *errmsg = E_BAD;
                goto fail;
            }
            p = PL_DOMAIN_LEN;
            e.seq = 0;
            for (int i = 0; i < 8; i++)
                e.seq |= ((uint64_t)canon[p + i]) << (8 * i);
            p += 8;
            if (p + 32 + 1 > clen) {
                *errmsg = E_BAD;
                goto fail;
            }
            memcpy(e.image_sha256, canon + p, 32);
            p += 32;
            e.verdict = canon[p++] ? 1 : 0;
            if (p + 1 > clen) {
                *errmsg = E_BAD;
                goto fail;
            }
            mid_len = canon[p++];
            if (mid_len > PL_MODEL_ID_MAX || p + mid_len + 1 > clen) {
                *errmsg = E_BAD;
                goto fail;
            }
            memcpy(e.model_id, canon + p, mid_len);
            e.model_id[mid_len] = '\0';
            p += mid_len;
            mver_len = canon[p++];
            if (mver_len > PL_MODEL_VER_MAX || p + mver_len + 4 + 8 + 32 > clen) {
                *errmsg = E_BAD;
                goto fail;
            }
            memcpy(e.model_ver, canon + p, mver_len);
            e.model_ver[mver_len] = '\0';
            p += mver_len;
            {
                uint32_t u = (uint32_t)canon[p] |
                             ((uint32_t)canon[p + 1] << 8) |
                             ((uint32_t)canon[p + 2] << 16) |
                             ((uint32_t)canon[p + 3] << 24);
                memcpy(&e.confidence, &u, 4);
            }
            p += 4;
            e.timestamp_unix = 0;
            for (int i = 0; i < 8; i++)
                e.timestamp_unix |= ((int64_t)canon[p + i]) << (8 * i);
            p += 8;
            memcpy(e.prev_hash, canon + p, 32);
            p += 32;
            if (p != clen) {
                *errmsg = E_BAD;
                goto fail;
            }
            memcpy(e.sig, sig, 64);

            if (nent == ecap) {
                size_t ncap = ecap ? ecap * 2 : 16;
                pl_entry_t *ne = realloc(ents, ncap * sizeof *ne);
                if (!ne) {
                    *errmsg = E_NOMEM;
                    goto fail;
                }
                ents = ne;
                ecap = ncap;
            }
            ents[nent++] = e;
            off += 2 + clen + 64;
        }
    }

    free(buf);
    *out = ents;
    *n = nent;
    return 0;

fail:
    if (fd >= 0)
        close(fd);
    free(buf);
    free(ents);
    return -1;
}

void pl_hex(const unsigned char *bin, size_t len, char *hex) {
    static const char *D = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        hex[2 * i] = D[(bin[i] >> 4) & 0xf];
        hex[2 * i + 1] = D[bin[i] & 0xf];
    }
    hex[2 * len] = '\0';
}

int pl_unhex(const char *hex, unsigned char *bin, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned v = 0;
        for (int k = 0; k < 2; k++) {
            char c = hex[2 * i + k];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                v |= (unsigned)(c - 'A' + 10);
            else
                return -1;
        }
        bin[i] = (unsigned char)v;
    }
    return 0;
}
