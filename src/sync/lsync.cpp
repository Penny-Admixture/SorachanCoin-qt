// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2018-2021 The SorachanCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sync/lsync.h>
#include <util/logging.h>
#include <util/strencodings.h>
#include <util.h> // for itostr

#include <stdio.h>
#include <map>
#include <memory>
#include <set>

#ifdef LDEBUG_LOCKCONTENTION
#if !defined(HAVE_THREAD_LOCAL)
static_assert(false, "thread_local is not supported");
#endif
void LPrintLockContention(const char* pszName, const char* pszFile, int nLine)
{
    LogPrintf("LOCKCONTENTION: %s\n", pszName);
    LogPrintf("Locker: %s:%d\n", pszFile, nLine);
}
#endif /* LDEBUG_LOCKCONTENTION */

#ifdef LDEBUG_LOCKORDER
//
// Early deadlock detection.
// Problem being solved:
//    Thread 1 locks A, then B, then C
//    Thread 2 locks D, then C, then A
//     --> may result in deadlock between the two threads, depending on when they run.
// Solution implemented here:
// Keep track of pairs of locks: (A before B), (A before C), etc.
// Complain if any thread tries to lock in a different order.
//

struct LCLockLocation {
    LCLockLocation(const char* pszName, const char* pszFile, int nLine, bool fTryIn)
    {
        mutexName = pszName;
        sourceFile = pszFile;
        sourceLine = nLine;
        fTry = fTryIn;
    }

    std::string ToString() const
    {
        return mutexName + "  " + sourceFile + ":" + itostr(sourceLine) + (fTry ? " (TRY)" : "");
    }

private:
    bool fTry;
    std::string mutexName;
    std::string sourceFile;
    int sourceLine;
};

typedef std::vector<std::pair<void*, LCLockLocation> > LLockStack;
typedef std::map<std::pair<void*, void*>, LLockStack> LLockOrders;
typedef std::set<std::pair<void*, void*> > LInvLockOrders;

struct LLockData {
    // Very ugly hack: as the global constructs and destructors run single
    // threaded, we use this boolean to know whether LockData still exists,
    // as DeleteLock can get called by global CCriticalSection destructors
    // after LockData disappears.
    bool available;
    LLockData() : available(true) {}
    ~LLockData() { available = false; }

    LLockOrders lockorders;
    LInvLockOrders invlockorders;
    std::mutex dd_mutex;
};
LLockData& GetLockData() {
    static LLockData lockdata;
    return lockdata;
}

static thread_local LLockStack g_lockstack;

static void potential_deadlock_detected(const std::pair<void*, void*>& mismatch, const LLockStack& s1, const LLockStack& s2)
{
    LogPrintf("POTENTIAL DEADLOCK DETECTED\n");
    LogPrintf("Previous lock order was:\n");
    for (const std::pair<void*, LCLockLocation> & i : s2) {
        if (i.first == mismatch.first) {
            LogPrintf(" (1)"); /* Continued */
        }
        if (i.first == mismatch.second) {
            LogPrintf(" (2)"); /* Continued */
        }
        LogPrintf(" %s\n", i.second.ToString());
    }
    LogPrintf("Current lock order is:\n");
    for (const std::pair<void*, LCLockLocation> & i : s1) {
        if (i.first == mismatch.first) {
            LogPrintf(" (1)"); /* Continued */
        }
        if (i.first == mismatch.second) {
            LogPrintf(" (2)"); /* Continued */
        }
        LogPrintf(" %s\n", i.second.ToString());
    }
    if (g_debug_lockorder_abort) {
        tfm::format(std::cerr, "Assertion failed: detected inconsistent lock order at %s:%i, details in debug log.\n", __FILE__, __LINE__);
        abort();
    }
    throw std::logic_error("potential deadlock detected");
}

static void push_lock(void* c, const LCLockLocation& locklocation)
{
    LLockData& lockdata = GetLockData();
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);

    g_lockstack.push_back(std::make_pair(c, locklocation));

    for (const std::pair<void*, LCLockLocation>& i : g_lockstack) {
        if (i.first == c)
            break;

        std::pair<void*, void*> p1 = std::make_pair(i.first, c);
        if (lockdata.lockorders.count(p1))
            continue;
        lockdata.lockorders[p1] = g_lockstack;

        std::pair<void*, void*> p2 = std::make_pair(c, i.first);
        lockdata.invlockorders.insert(p2);
        if (lockdata.lockorders.count(p2))
            potential_deadlock_detected(p1, lockdata.lockorders[p2], lockdata.lockorders[p1]);
    }
}

static void pop_lock()
{
    g_lockstack.pop_back();
}

void LEnterCritical(const char* pszName, const char* pszFile, int nLine, void* cs, bool fTry)
{
    push_lock(cs, LCLockLocation(pszName, pszFile, nLine, fTry));
}

void LLeaveCritical()
{
    pop_lock();
}

std::string LLocksHeld()
{
    std::string result;
    for (const std::pair<void*, LCLockLocation>& i : g_lockstack)
        result += i.second.ToString() + std::string("\n");
    return result;
}

void LAssertLockHeldInternal(const char* pszName, const char* pszFile, int nLine, void* cs)
{
    for (const std::pair<void*, LCLockLocation>& i : g_lockstack)
        if (i.first == cs)
            return;
    tfm::format(std::cerr, "Assertion failed: lock %s not held in %s:%i; locks held:\n%s", pszName, pszFile, nLine, LLocksHeld().c_str());
    abort();
}

void LAssertLockNotHeldInternal(const char* pszName, const char* pszFile, int nLine, void* cs)
{
    for (const std::pair<void*, LCLockLocation>& i : g_lockstack) {
        if (i.first == cs) {
            tfm::format(std::cerr, "Assertion failed: lock %s held in %s:%i; locks held:\n%s", pszName, pszFile, nLine, LLocksHeld().c_str());
            abort();
        }
    }
}

void LDeleteLock(void* cs)
{
    LLockData& lockdata = GetLockData();
    if (!lockdata.available) {
        // We're already shutting down.
        return;
    }
    std::lock_guard<std::mutex> lock(lockdata.dd_mutex);
    std::pair<void*, void*> item = std::make_pair(cs, nullptr);
    LLockOrders::iterator it = lockdata.lockorders.lower_bound(item);
    while (it != lockdata.lockorders.end() && it->first.first == cs) {
        std::pair<void*, void*> invitem = std::make_pair(it->first.second, it->first.first);
        lockdata.invlockorders.erase(invitem);
        lockdata.lockorders.erase(it++);
    }
    LInvLockOrders::iterator invit = lockdata.invlockorders.lower_bound(item);
    while (invit != lockdata.invlockorders.end() && invit->first == cs) {
        std::pair<void*, void*> invinvitem = std::make_pair(invit->second, invit->first);
        lockdata.lockorders.erase(invinvitem);
        lockdata.invlockorders.erase(invit++);
    }
}

bool g_debug_lockorder_abort = true; // extern

#endif /* LDEBUG_LOCKORDER */
