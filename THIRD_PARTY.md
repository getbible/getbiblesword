# Third-party software

getBibleSword embeds the official CrossWire SWORD engine in its official CLI and
shared-library artifacts. SWORD 1.9.0 is licensed
under GNU GPL version 2 and is downloaded from CrossWire's official source archive
with SHA-256 `42409cf3de2faf1108523e2c5ac0745d21f9ed2a5c78ed878ee9dcc303426b8a`.

Versioned releases attach the verified SWORD 1.9.0 source tarball beside the binary
archives. `libsword.a` is compiled as position-independent code and is not exposed
as a public dependency. The release build may dynamically link Linux system copies
of zlib, bzip2, xz, ICU and cURL; those libraries are not redistributed in the
archive.
