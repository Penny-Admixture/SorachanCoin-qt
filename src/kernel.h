// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PPCOIN_KERNEL_H
#define PPCOIN_KERNEL_H

#include <main.h>
#include <wallet.h>
#include <block/block_check.h>

using MapModifierCheckpoints = std::map<int, unsigned int>;
class MMCP_startup;

template <typename T>
class bitkernel : private no_instance
{
    friend class MMCP_startup;
private:
    // Hard checkpoints of stake
    static MapModifierCheckpoints mapStakeModifierCheckpoints;
    static MapModifierCheckpoints mapStakeModifierCheckpointsTestNet;

    // MODIFIER_INTERVAL_RATIO:
    // ratio of group interval length between the last group and the first group
    static constexpr int MODIFIER_INTERVAL_RATIO = 3;

    // Protocol switch time for fixed kernel modifier interval
    static constexpr unsigned int nModifierSwitchTime  = timestamps::GENESIS_TIME_STAMP;
    static constexpr unsigned int nModifierTestSwitchTime = timestamps::GENESIS_TIME_STAMP;

    static bool GetLastStakeModifier(const CBlockIndex_impl<T> *pindex, uint64_t &nStakeModifier, int64_t &nModifierTime);
    static int64_t GetStakeModifierSelectionIntervalSection(int nSection);
    static int64_t GetStakeModifierSelectionInterval();
    static bool SelectBlockFromCandidates(std::vector<std::pair<int64_t, T> > &vSortedByTimestamp, std::map<T, const CBlockIndex_impl<T> *> &mapSelectedBlocks, int64_t nSelectionIntervalStop, uint64_t nStakeModifierPrev, const CBlockIndex_impl<T> **pindexSelected);

    static bool GetKernelStakeModifier(T hashBlockFrom, uint64_t &nStakeModifier, int &nStakeModifierHeight, int64_t &nStakeModifierTime, bool fPrintProofOfStake);

    // Check whether stake kernel meets hash target
    // Sets hashProofOfStake on success return
    static bool CheckStakeKernelHash(unsigned int nBits, const CBlock_impl<T> &blockFrom, uint32_t nTxPrevOffset, const CTransaction_impl<T> &txPrev, const COutPoint_impl<T> &prevout, uint32_t nTimeTx, T &hashProofOfStake, T &targetProofOfStake, bool fPrintProofOfStake=false);

public:
    static const MapModifierCheckpoints &getMapStakeModifierCheckpoints() {return bitkernel<T>::mapStakeModifierCheckpoints;}
    static const MapModifierCheckpoints &getMapStakeModifierCheckpointsTestnet() {return bitkernel<T>::mapStakeModifierCheckpointsTestNet;}

    // Note: user must upgrade before the protocol switch deadline, otherwise it's required to
    // re-download the blockchain. The timestamp of upgrade is recorded in the blockchain database.
    // ChainDB upgrade time
    static unsigned int nModifierUpgradeTime;

    // Whether the given block is subject to new modifier protocol
    static bool IsFixedModifierInterval(unsigned int nTimeBlock);

    // Compute the hash modifier for proof-of-stake
    static bool ComputeNextStakeModifier(const CBlockIndex_impl<T> *pindexCurrent, uint64_t &nStakeModifier, bool &fGeneratedStakeModifier);

    // The stake modifier used to hash for a stake kernel is chosen as the stake
    // modifier about a selection interval later than the coin generating the kernel
    static bool GetKernelStakeModifier(T hashBlockFrom, uint64_t &nStakeModifier);

    // Scan given kernel for solutions
    static bool ScanKernelForward(unsigned char *kernel, uint32_t nBits, uint32_t nInputTxTime, int64_t nValueIn, std::pair<uint32_t, uint32_t> &SearchInterval, std::vector<std::pair<T, uint32_t> > &solutions);

    // Check kernel hash target and coinstake signature
    // Sets hashProofOfStake on success return
    static bool CheckProofOfStake(const CTransaction_impl<T> &tx, unsigned int nBits, T &hashProofOfStake, T &targetProofOfStake);

    // Get stake modifier checksum
    static uint32_t GetStakeModifierChecksum(const CBlockIndex_impl<T> *pindex);

    // Check stake modifier hard checkpoints
    static bool CheckStakeModifierCheckpoints(int nHeight, uint32_t nStakeModifierChecksum);

    // Get time weight using supplied timestamps
    static int64_t GetWeight(int64_t nIntervalBeginning, int64_t nIntervalEnd) {
        //
        // Kernel hash weight starts from 0 at the 30-day min age
        // this change increases active coins participating the hash and helps
        // to secure the network when proof-of-stake difficulty is low
        //
        // Maximum TimeWeight is 90 days.

        return std::min(nIntervalEnd - nIntervalBeginning - block_check::nStakeMinAge, (int64_t)block_check::nStakeMaxAge);
    }
};

#endif // PPCOIN_KERNEL_H
