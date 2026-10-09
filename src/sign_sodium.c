/* sign_sodium.c — libsodium backend for the ProofLine signing layer. */
#include "sign.h"

#include <sodium.h>

int pl_sign_init(void) {
    return sodium_init() < 0 ? -1 : 0;
}

int pl_keygen(unsigned char pk[PL_PUBKEY_BYTES],
              unsigned char sk[PL_SECRETKEY_BYTES]) {
    if (crypto_sign_keypair(pk, sk) != 0)
        return -1;
    return 0;
}

int pl_sign_detached(unsigned char sig[PL_SIG_BYTES],
                     const unsigned char *m, size_t mlen,
                     const unsigned char sk[PL_SECRETKEY_BYTES]) {
    unsigned long long siglen = 0;
    if (crypto_sign_detached(sig, &siglen, m, (unsigned long long)mlen, sk) != 0)
        return -1;
    if (siglen != PL_SIG_BYTES)
        return -1;
    return 0;
}

int pl_verify_detached(const unsigned char sig[PL_SIG_BYTES],
                       const unsigned char *m, size_t mlen,
                       const unsigned char pk[PL_PUBKEY_BYTES]) {
    return crypto_sign_verify_detached(sig, m, (unsigned long long)mlen, pk) == 0
               ? 0
               : -1;
}

void pl_randombytes(unsigned char *buf, size_t n) {
    randombytes_buf(buf, n);
}
