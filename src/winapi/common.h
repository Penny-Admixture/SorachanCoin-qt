// Copyright (c) 2018-2021 The SorachanCoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SORACHANCOIN_WINAPI_COMMON_H
#define SORACHANCOIN_WINAPI_COMMON_H

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <sstream>
#include <time.h>
#include <shlobj.h>
#include <util/logging.h>
#include <libstr/cmstring.h>

#ifdef WIN32
# define IDS_ERROR_CREATEWINDOW               L"To Process failed in CreateWindowEx."
# define IDS_ERROR_CLASSREGISTER              L"To Process failed in RegisterClassEx."
# define IDS_ERROR_FONT                       L"To Create fonts were failure."

# define TRANS_STRING(str)                    (_(str)).c_str()

class font
{
private:
    font()=delete;
    font(const font &)=delete;
    font &operator=(const font &)=delete;
    font(font &&)=delete;
    font &operator=(font &&)=delete;

    HFONT hFont;
    font(int cHeight) {
        hFont = ::CreateFontW(cHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_DONTCARE, nullptr);
        if(! hFont)
            throw std::runtime_error(CMString(IDS_ERROR_FONT));
    }
    ~font() {
        if(hFont) {
            ::DeleteObject(hFont);
            hFont = nullptr;
        }
    }
public:
    static const font &instance(int cHeight) {
        static font fobj(cHeight);
        return fobj;
    }
    const font &operator()(HDC hDC, RECT rc, const std::string &obj) const {
        std::ostringstream stream;
        stream << obj;
        HFONT prev = (HFONT)::SelectObject(hDC, hFont);
        ::DrawTextA(hDC, stream.str().c_str(), -1, &rc, DT_WORDBREAK);
        ::SelectObject(hDC, prev);
        return *this;
    }
    template <typename T> const font &operator()(HDC hDC, RECT rc, const T &obj) const {
        std::wostringstream stream;
        stream << obj;
        HFONT prev = (HFONT)::SelectObject(hDC, hFont);
        ::DrawTextW(hDC, stream.str().c_str(), -1, &rc, DT_WORDBREAK);
        ::SelectObject(hDC, prev);
        return *this;
    }
};
#endif

//
// Prediction System
// Note: At first, Win32 supported
// Note: load independent the MessageLoop below.
//
namespace predsystem {

    enum ret_code {
        success = 0,
        error_createwindow,
        error_initddk,
        error_createobject,
        error_outofmemory,
    };

    struct result {
        intptr_t window_ret;
        ret_code ret;
        std::string e;
        std::vector<uint8_t> vch;
        result() {
            window_ret = 0;
            ret = success;
        }
    };

    extern result CreateBenchmark() noexcept;
    extern bool CreateMiniwindow(bool *restart) noexcept;
    extern bool CreateSorara() noexcept;

} // namespace predsystem

#endif
