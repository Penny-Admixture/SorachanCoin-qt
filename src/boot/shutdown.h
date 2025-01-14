// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2018-2021 The SorachanCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SHUTDOWN_H
#define BITCOIN_SHUTDOWN_H

#include <ui_interface.h>
#ifndef WIN32
# include <util/thread.h>
#endif

class boot : private no_instance
{
public:
    static void Shutdown(void *parg); // init.cpp
    static void StartShutdown() {
#ifdef QT_GUI
        // ensure we leave the Qt main loop for a clean GUI exit (Shutdown() is called in bitcoin.cpp afterwards)
        CClientUIInterface::uiInterface.QueueShutdown();
#else
        // Without UI, Shutdown() can simply be started in a new thread
        if(! bitthread::NewThread(Shutdown, nullptr))
            bitthread::thread_error(std::string(__func__) + " :Shutdown");
#endif
    }
};

#endif // BITCOIN_SHUTDOWN_H
