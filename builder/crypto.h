#pragma once
#include "util.h"

namespace hb {
Bytes hmac_sha256(const Bytes& key, const Bytes& message);
// RFC 5869 extract plus the first expand block: exactly 32 bytes.
Bytes hkdf32(const Bytes& entry, const Bytes& salt, const Bytes& info);
// libsodium crypto_aead_xchacha20poly1305_ietf_decrypt; `sealed` is ciphertext followed by the 16-byte tag.
bool xchacha20poly1305_decrypt(Bytes& plain, const Bytes& sealed, const Bytes& aad, const uint8_t nonce[24], const uint8_t key[32]);
}
