# third_party/libsodium

Compile-time-only vendored **public headers** from the official libsodium
release:

- Source: libsodium 1.0.20-RELEASE tarball, downloaded from GitHub
  (jedisct1/libsodium), tarball SHA-256:
  `8e5aeca07a723a27bbecc3beef14b0068d37e7fc0e97f51b3f1c82d2a58005c1`
- Contents: `include/sodium.h` and `include/sodium/*.h`, unmodified.
- License: ISC (see LICENSE.libsodium in this directory).

Why vendored: the build machine (and many minimal/edge targets) ships the
libsodium *runtime* (`libsodium.so.23`) without the `-dev` headers. These
headers let ProofLine compile against the real libsodium without a dev
package. If `pkg-config` finds libsodium, the Makefile prefers the system
headers instead.

At runtime ProofLine links the system `libsodium` shared library
(`-lsodium`). No libsodium *code* is copied into this repo.

One exception: `include/sodium/version.h` is *generated* by libsodium's
`./configure` and is not in the release tarball's include tree, so it was
generated here by hand from `version.h.in` with the 1.0.20 values
(VERSION=1.0.20, MAJOR=26, MINOR=2, not minimal). It is marked as generated
in its header comment.
