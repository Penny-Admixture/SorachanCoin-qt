// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018-2020 The SorachanCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
#ifndef BITCOIN_UINT256_H
#define BITCOIN_UINT256_H

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <prevector/prevector.h>
#include <stdint.h>

/** Base class without constructors for uint.
* This makes the compiler let u use it in a union.
*/
template<unsigned int BITS>
class base_uint {
private:
    static constexpr int WIDTH = BITS / 32;
    uint32_t pn[WIDTH];
protected:
    void set_pn(int index, uint32_t n) {pn[index] = n;}
    void set_mem(const void *p) {std::memcpy(pn, p, size());}
    size_t get_width() const {return WIDTH;}
public:
    uint32_t operator[](int index) const {return pn[index];}
    bool operator!() const {
        for (int i=0; i<WIDTH; ++i) {
            if (pn[i] != 0) return false;
        }
        return true;
    }
    const base_uint operator~() const {
        base_uint ret;
        for (int i=0; i<WIDTH; ++i) ret.pn[i] = ~pn[i];
        return ret;
    }
    const base_uint operator-() const {
        base_uint ret;
        for (int i=0; i<WIDTH; ++i) ret.pn[i] = ~pn[i];
        ++ret;
        return ret;
    }
    double getdouble() const {
        double ret = 0.0;
        double fact = 1.0;
        for (int i=0; i<WIDTH; ++i) {
            ret += fact * pn[i];
            fact *= 4294967296.0;
        }
        return ret;
    }
    base_uint &operator=(uint64_t b) {
        pn[0] = (uint32_t)b;
        pn[1] = (uint32_t)(b >> 32);
        for (int i=2; i<WIDTH; ++i) pn[i] = 0;
        return *this;
    }
    base_uint &operator^=(const base_uint &b) {
        for (int i=0; i<WIDTH; ++i) pn[i] ^= b.pn[i];
        return *this;
    }
    base_uint &operator&=(const base_uint &b) {
        for (int i=0; i<WIDTH; ++i) pn[i] &= b.pn[i];
        return *this;
    }
    base_uint &operator|=(const base_uint &b) {
        for (int i=0; i<WIDTH; ++i) pn[i] |= b.pn[i];
        return *this;
    }
    base_uint &operator^=(uint64_t b) {
        pn[0] ^= (uint32_t)b;
        pn[1] ^= (uint32_t)(b >> 32);
        return *this;
    }
    base_uint &operator|=(uint64_t b) {
        pn[0] |= (uint32_t)b;
        pn[1] |= (uint32_t)(b >> 32);
        return *this;
    }
    base_uint &operator<<=(unsigned int shift) {
        base_uint a(*this);
        for (int i=0; i<WIDTH; ++i) pn[i] = 0;
        int k = shift / 32;
        shift = shift % 32;
        for (int i=0; i<WIDTH; ++i) {
            if (i + k + 1 < WIDTH && shift != 0) pn[i + k + 1] |= (a.pn[i] >> (32 - shift));
            if (i + k < WIDTH) pn[i + k] |= (a.pn[i] << shift);
        }
        return *this;
    }
    base_uint &operator>>=(unsigned int shift) {
        base_uint a(*this);
        for (int i=0; i<WIDTH; ++i) pn[i] = 0;
        int k = shift / 32;
        shift = shift % 32;
        for (int i=0; i<WIDTH; ++i) {
            if (i - k - 1 >= 0 && shift != 0) pn[i - k - 1] |= (a.pn[i] << (32 - shift));
            if (i - k >= 0) pn[i - k] |= (a.pn[i] >> shift);
        }
        return *this;
    }
    base_uint &operator+=(const base_uint &b) {
        uint64_t carry = 0;
        for (int i=0; i<WIDTH; ++i) {
            uint64_t n = carry + pn[i] + b.pn[i];
            pn[i] = n & 0xffffffff;
            carry = n >> 32;
        }
        return *this;
    }
    base_uint &operator-=(const base_uint &b) {
        *this += -b;
        return *this;
    }
    base_uint &operator+=(uint64_t b64) {
        base_uint b;
        b = b64;
        *this += b;
        return *this;
    }
    base_uint &operator-=(uint64_t b64) {
        base_uint b;
        b = b64;
        *this += -b;
        return *this;
    }
    base_uint &operator++() {
        int i = 0;
        while (++pn[i] == 0 && i < WIDTH - 1) ++i;
        return *this;
    }
    const base_uint operator++(int) {
        const base_uint ret = *this;
        ++(*this);
        return ret;
    }
    base_uint &operator--() {
        int i = 0;
        while (--pn[i] == -1 && i < WIDTH - 1) ++i;
        return *this;
    }
    const base_uint operator--(int) {
        const base_uint ret = *this;
        --(*this);
        return ret;
    }
    bool operator<(const base_uint &b) const {
        for (int i=base_uint::WIDTH-1; i>=0; --i) {
            if (pn[i] < b.pn[i]) return true;
            else if (pn[i] > b.pn[i]) return false;
        }
        return false;
    }
    bool operator<=(const base_uint &b) const {
        for (int i=base_uint::WIDTH-1; i>=0; --i) {
            if (pn[i] < b.pn[i]) return true;
            else if (pn[i] > b.pn[i]) return false;
        }
        return true;
    }
    bool operator>(const base_uint &b) const {
        for (int i=base_uint::WIDTH-1; i>=0; --i) {
            if (pn[i] > b.pn[i]) return true;
            else if (pn[i] < b.pn[i]) return false;
        }
        return false;
    }
    bool operator>=(const base_uint &b) const {
        for (int i=base_uint::WIDTH-1; i>=0; --i) {
            if (pn[i] > b.pn[i]) return true;
            else if (pn[i] < b.pn[i]) return false;
        }
        return true;
    }
    bool operator==(const base_uint &b) const {
        for (int i=0; i<base_uint::WIDTH; ++i) {
            if (pn[i] != b.pn[i]) return false;
        }
        return true;
    }
    bool operator==(uint32_t b) const {
        if (pn[0] != b) return false;
        for (int i=1; i<base_uint::WIDTH; ++i) {
            if (pn[i] != 0) return false;
        }
        return true;
    }
    bool operator==(uint64_t b) const {
        if (pn[0] != (uint32_t)b) return false;
        if (pn[1] != (uint32_t)(b >> 32)) return false;
        for (int i=2; i<base_uint::WIDTH; ++i) {
            if (pn[i] != 0) return false;
        }
        return true;
    }
    bool operator!=(const base_uint &b) const {
        return !(*this == b);
    }
    bool operator!=(uint32_t b) const {
        return !(*this == b);
    }
    bool operator!=(uint64_t b) const {
        return !(*this == b);
    }
    std::string GetHex() const {
        char psz[sizeof(pn) * 2 + 1];
        for (unsigned int i=0; i<sizeof(pn); ++i) {
            ::sprintf(psz + i * 2, "%02x", ((unsigned char *)pn)[sizeof(pn) - i - 1]);
        }
        return std::string(psz, psz + sizeof(pn) * 2);
    }
    void SetHex(const char *psz) {
        for (int i=0; i<WIDTH; ++i) pn[i] = 0;

        // skip leading spaces
        while (::isspace(*psz)) ++psz;

        // skip 0x
        if (psz[0] == '0' && ::tolower(psz[1]) == 'x') psz += 2;

        // hex string to uint
        static const unsigned char phexdigit[256] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0, 0,0xa,0xb,0xc,0xd,0xe,0xf,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0xa,0xb,0xc,0xd,0xe,0xf,0,0,0,0,0,0,0,0,0 };
        const char *pbegin = psz;
        while (phexdigit[(unsigned char)*psz] || *psz == '0') ++psz;

        --psz;
        unsigned char *p1 = (unsigned char *)pn;
        unsigned char *pend = p1 + WIDTH * 4;
        while (psz>=pbegin && p1<pend) {
            *p1 = phexdigit[(unsigned char)*psz--];
            if (psz>=pbegin) {
                *p1 |= (phexdigit[(unsigned char)*psz--] << 4);
                ++p1;
            }
        }
    }
    void SetHex(const std::string &str) {
        SetHex(str.c_str());
    }
    std::string ToString() const {
        return GetHex();
    }
    unsigned char *begin() {
        return (unsigned char *)&pn[0];
    }
    unsigned char *end() {
        return (unsigned char *)&pn[WIDTH];
    }
    const unsigned char *begin() const {
        return (const unsigned char *)&pn[0];
    }
    const unsigned char *end() const {
        return (const unsigned char *)&pn[WIDTH];
    }
    std::vector<unsigned char> getBytes() {
        return std::vector<unsigned char>(begin(), end());
    }
    size_t size() const {
        return sizeof(pn);
    }
    uint64_t Get64(int n = 0) const {
        return pn[2 * n] | (uint64_t)pn[2 * n + 1] << 32;
    }
    uint32_t Get32(int n = 0) const {
        return pn[n];
    }
    unsigned int GetSerializeSize() const {
        return sizeof(pn);
    }
    template<typename Stream>
    void Serialize(Stream &s) const {
        s.write((char *)pn, sizeof(pn));
    }
    template<typename Stream>
    void Unserialize(Stream &s) {
        s.read((char *)pn, sizeof(pn));
    }
};
typedef base_uint<160> base_uint160;
typedef base_uint<256> base_uint256;
typedef base_uint<512> base_uint512;
typedef base_uint<65536> base_uint65536;
typedef base_uint<131072> base_uint131072;

#ifdef CSCRIPT_PREVECTOR_ENABLE
typedef prevector<PREVECTOR_N, uint8_t> large_uint_vector;
#else
typedef std::vector<uint8_t> large_uint_vector;
#endif
#define UINT_DEF(obj_uint, base_uint) \
    class obj_uint : public base_uint { \
    public: \
        typedef base_uint basetype; \
        obj_uint() { \
            for (int i=0; i<(int)get_width(); ++i) set_pn(i, 0); \
        } \
        obj_uint(const basetype &b) {operator=((const basetype &)b);} \
        obj_uint(uint64_t b) {operator=((uint64_t)b);} \
        explicit obj_uint(const std::string &str) {SetHex(str);} \
        explicit obj_uint(const large_uint_vector &vch) { \
            if (vch.size()==size()) set_mem(&vch[0]); \
            else *this = 0; \
        } \
        obj_uint &operator=(const basetype &b) { \
            for (int i=0; i<(int)get_width(); ++i)set_pn(i, b[i]); \
            return *this; \
        } \
        obj_uint &operator=(uint64_t b) { \
            set_pn(0, (uint32_t)b); \
            set_pn(1, (uint32_t)(b >> 32)); \
            for (int i=2; i<(int)get_width(); ++i) set_pn(i, 0); \
            return *this; \
        } \
    }; \
inline bool operator==(const obj_uint &a, uint64_t b) { return (base_uint)a == b; } \
inline bool operator!=(const obj_uint &a, uint64_t b) { return (base_uint)a != b; } \
inline const obj_uint operator<<(const base_uint &a, unsigned int shift) { return obj_uint(a) <<= shift; } \
inline const obj_uint operator>>(const base_uint &a, unsigned int shift) { return obj_uint(a) >>= shift; } \
inline const obj_uint operator<<(const obj_uint &a, unsigned int shift)  { return obj_uint(a) <<= shift; } \
inline const obj_uint operator>>(const obj_uint &a, unsigned int shift)  { return obj_uint(a) >>= shift; } \
inline const obj_uint operator^(const base_uint &a, const base_uint &b)  { return obj_uint(a) ^= b; } \
inline const obj_uint operator&(const base_uint &a, const base_uint &b)  { return obj_uint(a) &= b; } \
inline const obj_uint operator|(const base_uint &a, const base_uint &b)  { return obj_uint(a) |= b; } \
inline const obj_uint operator+(const base_uint &a, const base_uint &b)  { return obj_uint(a) += b; } \
inline const obj_uint operator-(const base_uint &a, const base_uint &b)  { return obj_uint(a) -= b; } \
inline bool operator<(const base_uint &a, const obj_uint &b)  { return (base_uint)a <  (base_uint)b; } \
inline bool operator<=(const base_uint &a, const obj_uint &b) { return (base_uint)a <= (base_uint)b; } \
inline bool operator>(const base_uint &a, const obj_uint &b)  { return (base_uint)a >  (base_uint)b; } \
inline bool operator>=(const base_uint &a, const obj_uint &b) { return (base_uint)a >= (base_uint)b; } \
inline bool operator==(const base_uint &a, const obj_uint &b) { return (base_uint)a == (base_uint)b; } \
inline bool operator!=(const base_uint &a, const obj_uint &b) { return (base_uint)a != (base_uint)b; } \
inline const obj_uint operator^(const base_uint &a, const obj_uint &b) { return (base_uint)a ^ (base_uint)b; } \
inline const obj_uint operator&(const base_uint &a, const obj_uint &b) { return (base_uint)a & (base_uint)b; } \
inline const obj_uint operator|(const base_uint &a, const obj_uint &b) { return (base_uint)a | (base_uint)b; } \
inline const obj_uint operator+(const base_uint &a, const obj_uint &b) { return (base_uint)a + (base_uint)b; } \
inline const obj_uint operator-(const base_uint &a, const obj_uint &b) { return (base_uint)a - (base_uint)b; } \
inline bool operator<(const obj_uint &a, const base_uint &b)  { return (base_uint)a <  (base_uint)b; } \
inline bool operator<=(const obj_uint &a, const base_uint &b) { return (base_uint)a <= (base_uint)b; } \
inline bool operator>(const obj_uint &a, const base_uint &b)  { return (base_uint)a >  (base_uint)b; } \
inline bool operator>=(const obj_uint &a, const base_uint &b) { return (base_uint)a >= (base_uint)b; } \
inline bool operator==(const obj_uint &a, const base_uint &b) { return (base_uint)a == (base_uint)b; } \
inline bool operator!=(const obj_uint &a, const base_uint &b) { return (base_uint)a != (base_uint)b; } \
inline const obj_uint operator^(const obj_uint &a, const base_uint &b) { return (base_uint)a ^ (base_uint)b; } \
inline const obj_uint operator&(const obj_uint &a, const base_uint &b) { return (base_uint)a & (base_uint)b; } \
inline const obj_uint operator|(const obj_uint &a, const base_uint &b) { return (base_uint)a | (base_uint)b; } \
inline const obj_uint operator+(const obj_uint &a, const base_uint &b) { return (base_uint)a + (base_uint)b; } \
inline const obj_uint operator-(const obj_uint &a, const base_uint &b) { return (base_uint)a - (base_uint)b; } \
inline bool operator<(const obj_uint &a, const obj_uint &b)  { return (base_uint)a <  (base_uint)b; } \
inline bool operator<=(const obj_uint &a, const obj_uint &b) { return (base_uint)a <= (base_uint)b; } \
inline bool operator>(const obj_uint &a, const obj_uint &b)  { return (base_uint)a >  (base_uint)b; } \
inline bool operator>=(const obj_uint &a, const obj_uint &b) { return (base_uint)a >= (base_uint)b; } \
inline bool operator==(const obj_uint &a, const obj_uint &b) { return (base_uint)a == (base_uint)b; } \
inline bool operator!=(const obj_uint &a, const obj_uint &b) { return (base_uint)a != (base_uint)b; } \
inline const obj_uint operator^(const obj_uint &a, const obj_uint &b) { return (base_uint)a ^ (base_uint)b; } \
inline const obj_uint operator&(const obj_uint &a, const obj_uint &b) { return (base_uint)a & (base_uint)b; } \
inline const obj_uint operator|(const obj_uint &a, const obj_uint &b) { return (base_uint)a | (base_uint)b; } \
inline const obj_uint operator+(const obj_uint &a, const obj_uint &b) { return (base_uint)a + (base_uint)b; } \
inline const obj_uint operator-(const obj_uint &a, const obj_uint &b) { return (base_uint)a - (base_uint)b; }

UINT_DEF(uint160, base_uint160)
UINT_DEF(uint256, base_uint256)
UINT_DEF(uint512, base_uint512)
UINT_DEF(uint65536, base_uint65536)
UINT_DEF(uint131072, base_uint131072)

#endif
