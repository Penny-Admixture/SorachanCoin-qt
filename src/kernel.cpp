// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2013-2015 The Novacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kernel.h>
#include <kernel_worker.h>
#include <txdb.h>

template <typename T>
unsigned int bitkernel<T>::nModifierUpgradeTime = 0;
template <typename T>
MapModifierCheckpoints bitkernel<T>::mapStakeModifierCheckpoints = {};
template <typename T>
MapModifierCheckpoints bitkernel<T>::mapStakeModifierCheckpointsTestNet = {};

//
// Hard checkpoints of stake modifiers to ensure they are deterministic
//
class MMCP_startup {
public:
    MMCP_startup() {
        // mainchain
        bitkernel<uint256>::mapStakeModifierCheckpoints =
            {
                { 0, 0xe00670bu },
                { 377262, 0x10e0e614u },
                { 426387, 0xdf71ab5fu },
                { 434550, 0x2511363fu }
            };
        bitkernel<uint256>::mapStakeModifierCheckpointsTestNet =
            {
                { 0, 0xfd11f4e7u }
                //{ 10, 0xfd11f4e7u } // [OK] NG test
            };

        // finexDriveChain
        bitkernel<uint65536>::mapStakeModifierCheckpoints = {};
        bitkernel<uint65536>::mapStakeModifierCheckpointsTestNet = {};
    }
};
MMCP_startup mmcp;

// Whether the given block is subject to new modifier protocol
template <typename T>
bool bitkernel<T>::IsFixedModifierInterval(unsigned int nTimeBlock) {
    return (nTimeBlock >= (args_bool::fTestNet? nModifierTestSwitchTime : nModifierSwitchTime));
}

// Get the last stake modifier and its generation time from a given block
template <typename T>
bool bitkernel<T>::GetLastStakeModifier(const CBlockIndex_impl<T> *pindex, uint64_t &nStakeModifier, int64_t &nModifierTime)
{
    if (! pindex) {
        return logging::error("bitkernel::GetLastStakeModifier: null pindex");
    }

    while (pindex && pindex->get_pprev() && !pindex->GeneratedStakeModifier())
    {
        pindex = pindex->get_pprev();
    }

    if (! pindex->GeneratedStakeModifier()) {
        return logging::error("bitkernel::GetLastStakeModifier: no generation at genesis block");
    }
    
    nStakeModifier = pindex->get_nStakeModifier();
    nModifierTime = pindex->GetBlockTime();
    return true;
}

// Get selection interval section (in seconds)
template <typename T>
int64_t bitkernel<T>::GetStakeModifierSelectionIntervalSection(int nSection)
{
    assert (nSection >= 0 && nSection < 64);
    return (block_check::nModifierInterval * 63 / (63 + ((63 - nSection) * (MODIFIER_INTERVAL_RATIO - 1))));
}

// Get stake modifier selection interval (in seconds)
template <typename T>
int64_t bitkernel<T>::GetStakeModifierSelectionInterval()
{
    int64_t nSelectionInterval = 0;
    for (int nSection=0; nSection<64; ++nSection)
    {
        nSelectionInterval += GetStakeModifierSelectionIntervalSection(nSection);
    }
    return nSelectionInterval;
}

//
// select a block from the candidate blocks in vSortedByTimestamp, excluding
// already selected blocks in vSelectedBlocks, and with timestamp up to
// nSelectionIntervalStop.
//
template <typename T>
bool bitkernel<T>::SelectBlockFromCandidates(std::vector<std::pair<int64_t, T> > &vSortedByTimestamp, std::map<T, const CBlockIndex_impl<T> *> &mapSelectedBlocks, int64_t nSelectionIntervalStop, uint64_t nStakeModifierPrev, const CBlockIndex_impl<T> **pindexSelected)
{
    bool fSelected = false;
    T hashBest = 0;
    *pindexSelected = (const CBlockIndex *)0;
    for(const std::pair<int64_t, T> &item: vSortedByTimestamp)
    {
        if (! block_info::mapBlockIndex.count(item.second)) {
            return logging::error("bitkernel::SelectBlockFromCandidates: failed to find block index for candidate block %s", item.second.ToString().c_str());
        }

        const CBlockIndex *pindex = block_info::mapBlockIndex[item.second];
        if (fSelected && pindex->GetBlockTime() > nSelectionIntervalStop) {
            break;
        }
        if (mapSelectedBlocks.count(pindex->GetBlockHash()) > 0) {
            continue;
        }

        // compute the selection hash by hashing its proof-hash and the
        // previous proof-of-stake modifier
        T hashProof = pindex->IsProofOfStake()? pindex->get_hashProofOfStake() : pindex->GetBlockHash();
        CDataStream ss(SER_GETHASH, 0);
        ss << hashProof << nStakeModifierPrev;
        T hashSelection = hash_basis::Hash(ss.begin(), ss.end());

        // the selection hash is divided by 2**32 so that proof-of-stake block
        // is always favored over proof-of-work block. this is to preserve
        // the energy efficiency property
        if (pindex->IsProofOfStake()) {
            hashSelection >>= 32;
        }
        if (fSelected && hashSelection < hashBest) {
            hashBest = hashSelection;
            *pindexSelected = pindex;
        } else if (! fSelected) {
            fSelected = true;
            hashBest = hashSelection;
            *pindexSelected = pindex;
        }
    }
    if (args_bool::fDebug && map_arg::GetBoolArg("-printstakemodifier")) {
        logging::LogPrintf("bitkernel::SelectBlockFromCandidates: selection hash=%s\n", hashBest.ToString().c_str());
    }
    return fSelected;
}

//
// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
// Stake modifier consists of bits each of which is contributed from a
// selected block of a given block group in the past.
// The selection of a block is based on a hash of the block's proof-hash and
// the previous stake modifier.
// Stake modifier is recomputed at a fixed time interval instead of every 
// block. This is to make it difficult for an attacker to gain control of
// additional bits in the stake modifier, even after generating a chain of
// blocks.
//
template <typename T>
bool bitkernel<T>::ComputeNextStakeModifier(const CBlockIndex_impl<T> *pindexCurrent, uint64_t &nStakeModifier, bool &fGeneratedStakeModifier)
{
    nStakeModifier = 0;
    fGeneratedStakeModifier = false;
    const CBlockIndex *pindexPrev = pindexCurrent->get_pprev();
    if (! pindexPrev) {
        fGeneratedStakeModifier = true;
        return true;  // genesis block's modifier is 0
    }

    //
    // First find current stake modifier and its generation block time
    // if it's not old enough, return the same stake modifier
    //
    int64_t nModifierTime = 0;
    if (! GetLastStakeModifier(pindexPrev, nStakeModifier, nModifierTime)) {
        return logging::error("bitkernel::ComputeNextStakeModifier: unable to get last modifier");
    }
    if (args_bool::fDebug) {
        logging::LogPrintf("bitkernel::ComputeNextStakeModifier: prev modifier=0x%016" PRIx64 " time=%s epoch=%u\n", nStakeModifier, util::DateTimeStrFormat(nModifierTime).c_str(), (unsigned int)nModifierTime);
    }
    if (nModifierTime / block_check::nModifierInterval >= pindexPrev->GetBlockTime() / block_check::nModifierInterval) {
        if (args_bool::fDebug) {
            logging::LogPrintf("bitkernel::ComputeNextStakeModifier: no new interval keep current modifier: pindexPrev nHeight=%d nTime=%u\n", pindexPrev->get_nHeight(), (unsigned int)pindexPrev->GetBlockTime());
        }
        return true;
    }
    if (nModifierTime / block_check::nModifierInterval >= pindexCurrent->GetBlockTime() / block_check::nModifierInterval) {
        //
        // fixed interval protocol requires current block timestamp also be in a different modifier interval
        //
        if (bitkernel<T>::IsFixedModifierInterval(pindexCurrent->get_nTime())) {
            if (args_bool::fDebug) {
                logging::LogPrintf("bitkernel::ComputeNextStakeModifier: no new interval keep current modifier: pindexCurrent nHeight=%d nTime=%u\n", pindexCurrent->get_nHeight(), (unsigned int)pindexCurrent->GetBlockTime());
            }
            return true;
        } else {
            if (args_bool::fDebug) {
                logging::LogPrintf("bitkernel::ComputeNextStakeModifier: old modifier at block %s not meeting fixed modifier interval: pindexCurrent nHeight=%d nTime=%u\n", pindexCurrent->GetBlockHash().ToString().c_str(), pindexCurrent->get_nHeight(), (unsigned int)pindexCurrent->GetBlockTime());
            }
        }
    }

    //
    // Sort candidate blocks by timestamp
    //
    std::vector<std::pair<int64_t, T> > vSortedByTimestamp;
    vSortedByTimestamp.reserve(64 * block_check::nModifierInterval / block_check::nStakeTargetSpacing);

    int64_t nSelectionInterval = GetStakeModifierSelectionInterval();
    int64_t nSelectionIntervalStart = (pindexPrev->GetBlockTime() / block_check::nModifierInterval) * block_check::nModifierInterval - nSelectionInterval;

    const CBlockIndex *pindex = pindexPrev;
    while (pindex && pindex->GetBlockTime() >= nSelectionIntervalStart)
    {
        vSortedByTimestamp.push_back(std::make_pair(pindex->GetBlockTime(), pindex->GetBlockHash()));
        pindex = pindex->get_pprev();
    }

    int nHeightFirstCandidate = pindex ? (pindex->get_nHeight() + 1) : 0;
    std::reverse(vSortedByTimestamp.begin(), vSortedByTimestamp.end());
    std::sort(vSortedByTimestamp.begin(), vSortedByTimestamp.end());

    //
    // Select 64 blocks from candidate blocks to generate stake modifier
    //
    uint64_t nStakeModifierNew = 0;
    int64_t nSelectionIntervalStop = nSelectionIntervalStart;
    std::map<T, const CBlockIndex *> mapSelectedBlocks;
    for (int nRound=0; nRound<std::min(64, (int)vSortedByTimestamp.size()); ++nRound)
    {
        //
        // add an interval section to the current selection round
        //
        nSelectionIntervalStop += GetStakeModifierSelectionIntervalSection(nRound);

        //
        // select a block from the candidates of current round
        //
        if (! SelectBlockFromCandidates(vSortedByTimestamp, mapSelectedBlocks, nSelectionIntervalStop, nStakeModifier, &pindex)) {
            return logging::error("bitkernel::ComputeNextStakeModifier: unable to select block at round %d", nRound);
        }

        //
        // write the entropy bit of the selected block
        //
        nStakeModifierNew |= (((uint64_t)pindex->GetStakeEntropyBit()) << nRound);

        //
        // add the selected block from candidates to selected list
        //
        mapSelectedBlocks.insert(std::make_pair(pindex->GetBlockHash(), pindex));
        if (args_bool::fDebug && map_arg::GetBoolArg("-printstakemodifier")) {
            logging::LogPrintf("bitkernel::ComputeNextStakeModifier: selected round %d stop=%s height=%d bit=%d\n", nRound, util::DateTimeStrFormat(nSelectionIntervalStop).c_str(), pindex->get_nHeight(), pindex->GetStakeEntropyBit());
        }
    }

    // Print selection map for visualization of the selected blocks
    if (args_bool::fDebug && map_arg::GetBoolArg("-printstakemodifier")) {
        std::string strSelectionMap = "";
        
        //
        // '-' indicates proof-of-work blocks not selected
        //
        strSelectionMap.insert(0, pindexPrev->get_nHeight() - nHeightFirstCandidate + 1, '-');
        pindex = pindexPrev;
        while (pindex && pindex->get_nHeight() >= nHeightFirstCandidate)
        {
            //
            // '=' indicates proof-of-stake blocks not selected
            //
            if (pindex->IsProofOfStake()) {
                strSelectionMap.replace(pindex->get_nHeight() - nHeightFirstCandidate, 1, "=");
            }
            pindex = pindex->get_pprev();
        }

        for(const std::pair<T, const CBlockIndex *> &item: mapSelectedBlocks)
        {
            //
            // 'S' indicates selected proof-of-stake blocks
            // 'W' indicates selected proof-of-work blocks
            //
            strSelectionMap.replace(item.second->get_nHeight() - nHeightFirstCandidate, 1, item.second->IsProofOfStake() ? "S" : "W");
        }
        logging::LogPrintf("bitkernel::ComputeNextStakeModifier: selection height [%d, %d] map %s\n", nHeightFirstCandidate, pindexPrev->get_nHeight(), strSelectionMap.c_str());
    }
    if (args_bool::fDebug) {
        logging::LogPrintf("bitkernel::ComputeNextStakeModifier: new modifier=0x%016" PRIx64 " time=%s\n", nStakeModifierNew, util::DateTimeStrFormat(pindexPrev->GetBlockTime()).c_str());
    }

    nStakeModifier = nStakeModifierNew;
    fGeneratedStakeModifier = true;
    return true;
}

//
// The stake modifier used to hash for a stake kernel is chosen as the stake
// modifier about a selection interval later than the coin generating the kernel
//
template <typename T>
bool bitkernel<T>::GetKernelStakeModifier(T hashBlockFrom, uint64_t &nStakeModifier, int &nStakeModifierHeight, int64_t &nStakeModifierTime, bool fPrintProofOfStake)
{
    nStakeModifier = 0;
    if (! block_info::mapBlockIndex.count(hashBlockFrom)) {
        return logging::error("bitkernel::GetKernelStakeModifier() : block not indexed");
    }

    const CBlockIndex *pindexFrom = block_info::mapBlockIndex[hashBlockFrom];
    nStakeModifierHeight = pindexFrom->get_nHeight();
    nStakeModifierTime = pindexFrom->GetBlockTime();
    int64_t nStakeModifierSelectionInterval = GetStakeModifierSelectionInterval();
    const CBlockIndex *pindex = pindexFrom;

    // loop to find the stake modifier later by a selection interval
    while (nStakeModifierTime < pindexFrom->GetBlockTime() + nStakeModifierSelectionInterval)
    {
        if (! pindex->get_pnext()) {
            // reached best block; may happen if node is behind on block chain
            if (fPrintProofOfStake || (pindex->GetBlockTime() + block_check::nStakeMinAge - nStakeModifierSelectionInterval > bitsystem::GetAdjustedTime())) {
                return logging::error("bitkernel::GetKernelStakeModifier() : reached best block %s at height %d from block %s", pindex->GetBlockHash().ToString().c_str(), pindex->get_nHeight(), hashBlockFrom.ToString().c_str());
            } else {
                // logging::LogPrintf("bitkernel::GetKernelStakeModifier nStakeModifierTime_%I64d pindexFrom->GetBlockTime()_%I64d Sum_%I64d\n", nStakeModifierTime, pindexFrom->GetBlockTime(), pindexFrom->GetBlockTime() + nStakeModifierSelectionInterval);
                // logging::LogPrintf("%I64d %I64d bitKernel::GetKernelStakeModifier %I64d\n", pindexFrom->GetBlockTime(), nStakeModifierTime, pindexFrom->GetBlockTime() + nStakeModifierSelectionInterval - nStakeModifierTime);
                // logging::LogPrintf("bitkernel::GetKernelStakeModifier() : reached best block %s at height %d from block %s\n", pindex->GetBlockHash().ToString().c_str(), pindex->nHeight, hashBlockFrom.ToString().c_str());
                return false;
            }
        }

        pindex = pindex->get_pnext();
        if (pindex->GeneratedStakeModifier()) {
            nStakeModifierHeight = pindex->get_nHeight();
            nStakeModifierTime = pindex->GetBlockTime();
        }
    }

    nStakeModifier = pindex->get_nStakeModifier();
    return true;
}

template <typename T>
bool bitkernel<T>::GetKernelStakeModifier(T hashBlockFrom, uint64_t &nStakeModifier)
{
    int nStakeModifierHeight;
    int64_t nStakeModifierTime;

    return GetKernelStakeModifier(hashBlockFrom, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, false);
}

//
// ppcoin kernel protocol
// coinstake must meet hash target according to the protocol:
// kernel (input 0) must meet the formula
//     hash(nStakeModifier + txPrev.block.nTime + txPrev.offset + txPrev.nTime + txPrev.vout.n + nTime) < bnTarget * nCoinDayWeight
// this ensures that the chance of getting a coinstake is proportional to the
// amount of coin age one owns.
// The reason this hash is chosen is the following:
//   nStakeModifier: scrambles computation to make it very difficult to precompute
//                  future proof-of-stake at the time of the coin's confirmation
//   txPrev.block.nTime: prevent nodes from guessing a good timestamp to
//                       generate transaction for future advantage
//   txPrev.offset: offset of txPrev inside block, to reduce the chance of 
//                  nodes generating coinstake at the same time
//   txPrev.nTime: reduce the chance of nodes generating coinstake at the same
//                 time
//   txPrev.vout.n: output number of txPrev, to reduce the chance of nodes
//                  generating coinstake at the same time
//   block/tx hash should not be used here as they can be generated in vast
//   quantities so as to generate blocks faster, degrading the system back into
//   a proof-of-work situation.
//
template <typename T>
bool bitkernel<T>::CheckStakeKernelHash(uint32_t nBits, const CBlock_impl<T> &blockFrom, uint32_t nTxPrevOffset, const CTransaction_impl<T> &txPrev, const COutPoint_impl<T> &prevout, uint32_t nTimeTx, T &hashProofOfStake, T &targetProofOfStake, bool fPrintProofOfStake)
{
    if (nTimeTx < txPrev.get_nTime()) { // Transaction timestamp violation
        return logging::error("bitkernel::CheckStakeKernelHash() : nTime violation");
    }

    uint32_t nTimeBlockFrom = blockFrom.GetBlockTime();
    if (nTimeBlockFrom + block_check::nStakeMinAge > nTimeTx) { // Min age requirement
        return logging::error("bitkernel::CheckStakeKernelHash() : min age violation");
    }

    CBigNum bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);
    int64_t nValueIn = txPrev.get_vout(prevout.get_n()).get_nValue();

    T hashBlockFrom = blockFrom.GetHash();

    CBigNum bnCoinDayWeight = CBigNum(nValueIn) * GetWeight((int64_t)txPrev.get_nTime(), (int64_t)nTimeTx) / util::COIN / util::nOneDay;
    //targetProofOfStake = (bnCoinDayWeight * bnTargetPerCoinDay).getuint256();
    targetProofOfStake = (bnCoinDayWeight * bnTargetPerCoinDay).getuint<T>();

    // Calculate hash
    CDataStream ss(SER_GETHASH, 0);
    uint64_t nStakeModifier = 0;
    int nStakeModifierHeight = 0;
    int64_t nStakeModifierTime = 0;

    if (! GetKernelStakeModifier(hashBlockFrom, nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake)) {
        return false;
    }
    ss << nStakeModifier;

    ss << nTimeBlockFrom << nTxPrevOffset << txPrev.get_nTime() << prevout.get_n() << nTimeTx;
    hashProofOfStake = hash_basis::Hash(ss.begin(), ss.end());
    if (fPrintProofOfStake) {
        logging::LogPrintf("bitkernel::CheckStakeKernelHash() : using modifier 0x%016" PRIx64 " at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
            nStakeModifier, nStakeModifierHeight,
            util::DateTimeStrFormat(nStakeModifierTime).c_str(),
            block_info::mapBlockIndex[hashBlockFrom]->get_nHeight(),
            util::DateTimeStrFormat(blockFrom.GetBlockTime()).c_str());
        logging::LogPrintf("bitkernel::CheckStakeKernelHash() : check modifier=0x%016" PRIx64 " nTimeBlockFrom=%u nTxPrevOffset=%u nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashTarget=%s hashProof=%s\n",
            nStakeModifier,
            nTimeBlockFrom, nTxPrevOffset, txPrev.get_nTime(), prevout.get_n(), nTimeTx,
            targetProofOfStake.ToString().c_str(), hashProofOfStake.ToString().c_str());
    }

    // Now check if proof-of-stake hash meets target protocol
    if (CBigNum(hashProofOfStake) > bnCoinDayWeight * bnTargetPerCoinDay) {
        return false;
    }
    if (args_bool::fDebug && !fPrintProofOfStake) {
        logging::LogPrintf("bitkernel::CheckStakeKernelHash() : using modifier 0x%016" PRIx64 " at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
            nStakeModifier, nStakeModifierHeight, 
            util::DateTimeStrFormat(nStakeModifierTime).c_str(),
            block_info::mapBlockIndex[hashBlockFrom]->get_nHeight(),
            util::DateTimeStrFormat(blockFrom.GetBlockTime()).c_str());
        logging::LogPrintf("bitkernel::CheckStakeKernelHash() : pass modifier=0x%016" PRIx64 " nTimeBlockFrom=%u nTxPrevOffset=%u nTimeTxPrev=%u nPrevout=%u nTimeTx=%u hashTarget=%s hashProof=%s\n",
            nStakeModifier,
            nTimeBlockFrom, nTxPrevOffset, txPrev.get_nTime(), prevout.get_n(), nTimeTx,
            targetProofOfStake.ToString().c_str(), hashProofOfStake.ToString().c_str());
    }

    return true;
}

// Scan given kernel for solution
template <typename T>
bool bitkernel<T>::ScanKernelForward(unsigned char *kernel, uint32_t nBits, uint32_t nInputTxTime, int64_t nValueIn, std::pair<uint32_t, uint32_t> &SearchInterval, std::vector<std::pair<T, uint32_t> > &solutions)
{
    try {
        //
        // TODO: custom threads amount
        //
        uint32_t nThreads = boost::thread::hardware_concurrency();
        if (nThreads == 0) {
           nThreads = 1;
           logging::LogPrintf("Warning: hardware_concurrency() failed in %s:%d\n", __FILE__, __LINE__);
        }
        uint32_t nPart = (SearchInterval.second - SearchInterval.first) / nThreads;

        KernelWorker **workers = new KernelWorker *[nThreads];
        boost::thread_group group;
        for(size_t i = 0; i < nThreads; ++i)
        {
            uint32_t nBegin = SearchInterval.first + nPart * i;
            uint32_t nEnd = SearchInterval.first + nPart * (i + 1);
            workers[i] = new KernelWorker(kernel, nBits, nInputTxTime, nValueIn, nBegin, nEnd);
            boost::function<void()> workerFnc = boost::bind(&KernelWorker::Do, workers[i]);
            group.create_thread(workerFnc);        // start thread
        }

        group.join_all();        // wali for all thread exit ...

        solutions.clear();
        for(size_t i = 0; i < nThreads; ++i)
        {
            std::vector<std::pair<T, uint32_t> > ws = workers[i]->GetSolutions();
            solutions.insert(solutions.end(), ws.begin(), ws.end());
        }

        for(size_t i = 0; i < nThreads; ++i)
        {
            delete workers[i];
        }
        delete [] workers;

        if (solutions.size() == 0) {
            // no solutions
            return false;
        }
        return true;
    } catch (const std::bad_alloc &e) {
        logging::LogPrintf("Warning %s: New allocate failed in %s:%d\n", e.what(), __FILE__, __LINE__);
        return false;
    }
}

// Check kernel hash target and coinstake signature
template <typename T>
bool bitkernel<T>::CheckProofOfStake(const CTransaction_impl<T> &tx, unsigned int nBits, T &hashProofOfStake, T &targetProofOfStake)
{
    if (! tx.IsCoinStake()) {
        return logging::error("bitkernel::CheckProofOfStake() : called on non-coinstake %s", tx.GetHash().ToString().c_str());
    }

    // Kernel (input 0) must match the stake hash target per coin age (nBits)
    const CTxIn& txin = tx.get_vin(0);

    // First try finding the previous transaction in database
    CTxDB txdb("r");
    CTransaction txPrev;
    CTxIndex txindex;
    if (! txPrev.ReadFromDisk(txdb, txin.get_prevout(), txindex)) {
        return tx.DoS(1, logging::error("bitkernel::CheckProofOfStake() : INFO: read txPrev failed"));  // previous transaction not in main chain, may occur during initial download
    }

    // Verify signature
    if (! block_check::manage<T>::VerifySignature(txPrev, tx, 0, Script_param::MANDATORY_SCRIPT_VERIFY_FLAGS, 0)) {
        return tx.DoS(100, logging::error("bitkernel::CheckProofOfStake() : block_check::manage::VerifySignature failed on coinstake %s", tx.GetHash().ToString().c_str()));
    }

    // Read block header
    CBlock block;
    if (! block.ReadFromDisk(txindex.get_pos().get_nFile(), txindex.get_pos().get_nBlockPos(), false)) {
        return args_bool::fDebug? logging::error("bitkernel::CheckProofOfStake() : read block failed") : false; // unable to read block of previous transaction
    }
    if (! bitkernel<T>::CheckStakeKernelHash(nBits, block, txindex.get_pos().get_nTxPos() - txindex.get_pos().get_nBlockPos(), txPrev, txin.get_prevout(), tx.get_nTime(), hashProofOfStake, targetProofOfStake, args_bool::fDebug)) {
        return tx.DoS(1, logging::error("bitkernel::CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s", tx.GetHash().ToString().c_str(), hashProofOfStake.ToString().c_str())); // may occur during initial download or if behind on block chain sync
    }

    return true;
}

// Get stake modifier checksum
template <typename T>
uint32_t bitkernel<T>::GetStakeModifierChecksum(const CBlockIndex_impl<T> *pindex)
{
    assert (pindex->get_pprev() || pindex->GetBlockHash() == (!args_bool::fTestNet ? block_params::hashGenesisBlock : block_params::hashGenesisBlockTestNet));

    //
    // Hash previous checksum with flags, hashProofOfStake and nStakeModifier
    //
    CDataStream ss(SER_GETHASH, 0);
    if (pindex->get_pprev()) {
        ss << pindex->get_pprev()->get_nStakeModifierChecksum();
    }

    ss << pindex->get_nFlags() << pindex->get_hashProofOfStake() << pindex->get_nStakeModifier();
    T hashChecksum = hash_basis::Hash(ss.begin(), ss.end());
    hashChecksum >>= (256 - 32);

    uint32_t ret = static_cast<uint32_t>(hashChecksum.Get64());
    //logging::LogPrintf("StakeModifierChecksum: nHeight_%d checksum_%x\n", pindex->nHeight, ret);
    return ret;
}

// Check stake modifier hard checkpoints
template <typename T>
bool bitkernel<T>::CheckStakeModifierCheckpoints(int nHeight, uint32_t nStakeModifierChecksum)
{
    MapModifierCheckpoints &checkpoints = (args_bool::fTestNet ? bitkernel<T>::mapStakeModifierCheckpointsTestNet : bitkernel<T>::mapStakeModifierCheckpoints);
    if (checkpoints.count(nHeight)) {
        bool ret = (nStakeModifierChecksum == checkpoints[nHeight]);
        if(! ret) {
            return logging::error("CheckStakeModifierCheckpoints error: checksum 0x%x", nStakeModifierChecksum);
        }
    }

    return true;
}

template class bitkernel<uint256>;
