// Copyright (c) 2018-2021 The SorachanCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

# include <crypto/blake2.h>
# include <blake2.h>
# include <cleanse/cleanse.h>

namespace latest_crypto {

CBLAKE2::CBLAKE2() noexcept {
    Reset();
}

CBLAKE2& CBLAKE2::Write(const unsigned char* data, size_t len) noexcept {
    ::blake2s_update(&S, data, len);
    return *this;
}

void CBLAKE2::Finalize(unsigned char hash[OUTPUT_SIZE]) noexcept {
    ::blake2s_final(&S, hash, OUTPUT_SIZE);
}

CBLAKE2& CBLAKE2::Reset() noexcept {
    ::blake2s_init(&S, OUTPUT_SIZE); return *this;
}

void CBLAKE2::Clean() noexcept {
    cleanse::OPENSSL_cleanse(&S, sizeof(S));
}

} // latest_crypto
