// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018-2021 The SorachanCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MERKLE_TREE_H
#define BITCOIN_MERKLE_TREE_H

#include <prevector/prevector.h>

// Memory all load, Create Hash Tree
template <typename T, typename SRC>
class CMerkleTree
{
#ifdef BLOCK_PREVECTOR_ENABLE
    using vMerkle_t = prevector<PREVECTOR_BLOCK_N, T>;
#else
    using vMerkle_t = std::vector<T>;
#endif
//private:
    //CMerkleTree(const CMerkleTree &)=delete;
    //CMerkleTree(const CMerkleTree &&)=delete;
    //CMerkleTree &operator=(const CMerkleTree &)=delete;
    //CMerkleTree &operator=(const CMerkleTree &&)=delete;
protected:
    std::vector<SRC> vtx;
    mutable vMerkle_t vMerkleTree;

    CMerkleTree() {SetNull();}
    virtual ~CMerkleTree() {}
    void SetNull() {
        vtx.clear();
        vMerkleTree.clear();
    }

public:
    T BuildMerkleTree() const;
    vMerkle_t GetMerkleBranch(int nIndex) const;
    static T CheckMerkleBranch(T hash, const vMerkle_t &vMerkleBranch, int nIndex);
};

#endif // BITCOIN_MERKLE_TREE_H
