/* sign.h — Ed25519 signing layer for ProofLine.
 *
 * Backend: libsodium (https://libsodium.org). The build uses the system
 * libsodium when pkg-config finds it; otherwise it compiles against the
 * vendored public headers in third_party/libsodium/include and links the
 * system libsodium shared library.
 *
 * PmCp milestone 2 (Chamber signing) is not built yet, so the signing
 * layer lives here, in the ProofLine repo. When Chamber signing lands,
 * this module is the seam to replace.
 */
#ifndef PROOFLINE_SIGN_H
#define PROOFLINE_SIGN_H

#include <stddef.h>

#define PL_PUBKEY_BYTES 32
#define PL_SECRETKEY_BYTES 64
#define PL_SIG_BYTES 64

/* Initialize the backend (libsodium). Returns 0 on success, -1 on failure. */
int pl_sign_init(void);

/* Generate a fresh Ed25519 keypair. Returns 0 on success, -1 on failure. */
int pl_keygen(unsigned char pk[PL_PUBKEY_BYTES],
              unsigned char sk[PL_SECRETKEY_BYTES]);

/* Detached signature over m[0..mlen). Returns 0 on success, -1 on failure. */
int pl_sign_detached(unsigned char sig[PL_SIG_BYTES],
                     const unsigned char *m, size_t mlen,
                     const unsigned char sk[PL_SECRETKEY_BYTES]);

/* Verify a detached signature. Returns 0 if valid, -1 if invalid/failed. */
int pl_verify_detached(const unsigned char sig[PL_SIG_BYTES],
                       const unsigned char *m, size_t mlen,
                       const unsigned char pk[PL_PUBKEY_BYTES]);

/* Fill buf with n cryptographically random bytes. */
void pl_randombytes(unsigned char *buf, size_t n);

#endif /* PROOFLINE_SIGN_H */
