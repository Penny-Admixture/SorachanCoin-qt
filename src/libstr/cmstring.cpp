// Copyright (c) 2019-2021 The SorachanCoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <libstr/cmstring.h>

// test
class CMString_test {
public:
    CMString_test() {
        CMString str = CMString(L"cats") += 4;
        (str += "doge") + "doge";
        (str += std::string("mike")) + std::wstring(L"neko");
        (str += 2) + 5;
        assert(str=="cats4dogedogemikeneko25");
    }
};
CMString_test cmstring;
