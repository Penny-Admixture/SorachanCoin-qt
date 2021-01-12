// Copyright (c) 2019-2021 The SorachanCoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// NVM Express Mar-2020 1.4a, thanks.
// https://nvmexpress.org/wp-content/uploads/NVM-Express-1_4a-2020.03.09-Ratified.pdf

#ifndef SORACHAIN_DRIVEBASE_H
#define SORACHAIN_DRIVEBASE_H

#include <windows.h>
#include <winioctl.h>
#include <new>
#include <string>
#include <sstream>
#include <cstring>
#include <vector>
#include <random/random.h>
#include <debugcs/debugcs.h>
#ifndef WIN32 // port to lsync.h
# include <condition_variable>
# include <mutex>
#endif
#define PREDICTION_UNDER_DEVELOPMENT

#define ARRAY_SIZE(X) (sizeof(X)/(sizeof(X[0])))
typedef int64_t sector_t;
#ifndef WIN32
# ifndef wchar_t
using wchar_t = unsigned short;
# endif
using LPWSTR = unsigned wchar_t *;
using LPCWSTR = const unsigned wchar_t *;
using LPSTR = char *;
using LPCSTR = const char *;
#endif
constexpr int SECTOR_SIZE_DEFAULT = 512;
constexpr sector_t TOTAL_SECTORS_DEFAULT = (sector_t)32 * 1024 * 1024 * SECTOR_SIZE_DEFAULT; // 16GB
constexpr int SECTORS_STEP_DEFAULT = 8;
constexpr int NO_DRIVELETTER = -1;
constexpr sector_t SECTORS_STEP = (sector_t)-1;
constexpr int MAX_DRIVELETTER = 26;
constexpr int MAX_PARTITION = 128;
constexpr int CMD_BUFFER_SIZE = 4096;
constexpr int CMD_SEND_LIMIT = 10;
constexpr int DRIVE_TARGET_UNUSED = -1;
constexpr sector_t SCAN_BEGIN_MIN_SECTOR = 1024;
static const wchar_t *DRIVENAME_GET_FAILURE = L"DRIVE_DEFAULT";
static const wchar_t *DRIVEVENDOR_GET_FAILURE = L"VENDOR_DEFAULT";
static const wchar_t *TEMPFILE_DIR = L"__TEMP__";
static const wchar_t *TEMPFILE_NAME = L"__sorachain.dat";

class sync // port to lsync.h
{
private:
    sync(const sync &)=delete;
    sync &operator=(const sync &)=delete;
    sync(sync &&)=delete;
    sync &operator=(sync &&)=delete;
#ifdef WIN32
    mutable CRITICAL_SECTION cs;
#else
    mutable std::recursive_mutex cs;
#endif
public:
#ifdef WIN32
    sync() {::InitializeCriticalSection(&cs);}
    ~sync() {::DeleteCriticalSection(&cs);}
    void enter() const {::EnterCriticalSection(&cs);}
    void leave() const {::LeaveCriticalSection(&cs);}
#else
    sync() {}
    ~sync() {}
    void enter() const {cs.lock();}
    void leave() const {cs.unlock();}
#endif
};

class cevent // port to lsync.h
{
private:
    cevent(const cevent &)=delete;
    cevent &operator=(const cevent &)=delete;
    cevent(cevent &&)=delete;
    cevent &operator=(cevent &&)=delete;
#ifdef WIN32
    HANDLE hEvent;
#else
    std::mutex *mutex;
    std::condition_variable *ev;
#endif
public:
#ifdef WIN32
    cevent() {
        hEvent = ::CreateEventW(nullptr, TRUE, FALSE, L"");
        if(! hEvent)
            throw std::runtime_error("Event object create failure.");
    }
    ~cevent() {
        if(hEvent)
            ::CloseHandle(hEvent);
    }

    void set() const {
        ::SetEvent(hEvent);
    }
    void reset() const {
        ::ResetEvent(hEvent);
    }
    void wait() const {
        ::WaitForSingleObject(hEvent, INFINITE);
    }
#else
    cevent() {reset();}
    ~cevent() {
        if(ev) {
            delete ev;
        }
        if(mutex) {
            mutex->unlock();
            delete mutex;
        }
    }

    void set() const {
        ev.notify_one();
    }
    void reset() const {
        try {
            mutex = new std::mutex;
            ev = new std::condition_variable;
        } catch (const std::bad_alloc &) {
            throw std::runtime_error("Event object create failure.");
        }
    }
    void wait() const {
        ev.wait(*mutex);
    }
#endif
};

template <typename C, typename T>
class randrangebuffer final
{
private:
    randrangebuffer()=delete;
    randrangebuffer(const randrangebuffer &)=delete;
    randrangebuffer &operator=(const randrangebuffer &)=delete;
    randrangebuffer(randrangebuffer &&)=delete;
    randrangebuffer &operator=(randrangebuffer &&)=delete;

    int (C::*__RAND_bytes)(unsigned char *&buf, int num);
    C *self;
    T buf;
public:
    T getrand(T nMax = std::numeric_limits<T>::max()) {
        // referrence: the bitcoin util.cpp ////////////////////////////////
        // The range of the random source must be a multiple of the modulus
        // to give every possible output value an equal possibility.
        ////////////////////////////////////////////////////////////////////
        const T nRange = (std::numeric_limits<T>::max() / nMax) * nMax;
        T nRand = 0;
        T *pnRand = &nRand;
        do
        {
            (void)((self->*(__RAND_bytes))((unsigned char *&)pnRand, sizeof(nRand)));
        } while (*pnRand >= nRange);

        buf = (*pnRand) % nMax;
        buf = (buf != 0) ? buf: nRange;
        return buf;
    }
    T getbuf() const {
        return buf;
    }

    explicit randrangebuffer(int (C::*__RAND_bytes_func)(unsigned char *&buf, int num), C *__self) {
        buf = std::numeric_limits<T>::max();
        __RAND_bytes = __RAND_bytes_func;
        self = __self;
    }
    ~randrangebuffer() {}
};

template <typename T>
class mcrypto final
{
private:
    mcrypto(const mcrypto &)=delete;
    mcrypto &operator=(const mcrypto &)=delete;
    mcrypto(mcrypto &&)=delete;
    mcrypto &operator=(mcrypto &&)=delete;
    randrangebuffer<mcrypto<T>, T> randrange;

    int __RAND_bytes(unsigned char *&buf, int num) {
        //return ::RAND_bytes(buf, num);
        latest_crypto::random::GetStrongRandBytes(buf, num);
        return 1; // 1 on success.
    }
public:
    mcrypto() : randrange(&mcrypto<T>::__RAND_bytes, this) {}
    ~mcrypto() {}

    mcrypto &operator>>=(const mcrypto &obj) {
        randrange.getrand(obj.randrange.getbuf());
        return *this;
    }
    operator T() const {
        return randrange.getbuf();
    }
};

class drive_util
{
private:
    drive_util(const drive_util &)=delete;
    drive_util &operator=(const drive_util &)=delete;
    drive_util(drive_util &&)=delete;
    drive_util &operator=(drive_util &&)=delete;
protected:
    bool chartowchar(const char *source, std::wstring &dest) const;
    bool wchartochar(LPCWSTR source, std::string &dest) const;
    drive_util() {}
    virtual ~drive_util() {}
};

class drive_handle
{
private:
    drive_handle()=delete;
    drive_handle(const drive_handle &)=delete;
    drive_handle &operator=(const drive_handle &)=delete;
    drive_handle(drive_handle &&)=delete;
    drive_handle &operator=(drive_handle &&)=delete;

    int nDrive;
#ifdef WIN32
    HANDLE hDrive;
#else
    int hDrive;
#endif
    bool lock;
    static sync cs;

    bool tempfile;
    std::wstring tempfiledir;

    bool createdir(LPCWSTR path) const;

protected:
    bool openread(bool _lock = false);
    bool openwrite(bool _lock = false);
    bool openwritefile(char letter, LPCWSTR path, bool _lock = false);
    void close();

protected:
    drive_handle(int drive_target) : nDrive(drive_target), hDrive(nullptr), lock(false), tempfile(false) {}
    virtual ~drive_handle() {close();}

    int getdrive() const {return nDrive;}
#ifdef WIN32
    HANDLE gethandle() const {return hDrive;}
#else
    int gethandle() const {return hDrive;}
#endif
    bool getlock() const {return lock;}
};

class drive_cmd : protected drive_handle, protected drive_util
{
private:
    drive_cmd()=delete;
    drive_cmd(const drive_cmd &)=delete;
    drive_cmd &operator=(const drive_cmd &)=delete;
    drive_cmd(drive_cmd &&)=delete;
    drive_cmd &operator=(drive_cmd &&)=delete;

    DWORD sector_size;
    sector_t total_sectors;
    std::wstring drive_vendor;
    std::wstring drive_name;
    char cDriveLetter[MAX_DRIVELETTER];

    //static const unsigned int IOCTL_STORAGE_QUERY_PROPERTY = CTL_CODE(IOCTL_STORAGE_BASE, 0x500, METHOD_BUFFERED, FILE_ANY_ACCESS);
    typedef enum _STORAGE_PROPERTY_ID
    {
        StorageDeviceProperty                   = 0,
        StorageAdapterProperty,
        StorageDeviceIdProperty,
        StorageDeviceUniqueIdProperty,
        StorageDeviceWriteCacheProperty,
        StorageMiniportProperty,
        StorageAccessAlignmentProperty,
        StorageDeviceSeekPenaltyProperty,
        StorageDeviceTrimProperty,
        StorageDeviceWriteAggregationProperty,
        StorageDeviceDeviceTelemetryProperty,
        StorageDeviceLBProvisioningProperty,
        StorageDevicePowerProperty,
        StorageDeviceCopyOffloadProperty,
        StorageDeviceResiliencyProperty,
        StorageDeviceMediumProductType,
        StorageAdapterCryptoProperty,
        StorageDeviceIoCapabilityProperty       = 48,
        StorageAdapterProtocolSpecificProperty,
        StorageDeviceProtocolSpecificProperty,
        StorageAdapterTemperatureProperty,
        StorageDeviceTemperatureProperty,
        StorageAdapterPhysicalTopologyProperty,
        StorageDevicePhysicalTopologyProperty,
        StorageDeviceAttributesProperty
    } STORAGE_PROPERTY_ID, *PSTORAGE_PROPERTY_ID; // https://msdn.microsoft.com/en-us/windows/ff566996(v=vs.80)

    typedef enum _STORAGE_QUERY_TYPE
    {
        PropertyStandardQuery    = 0,
        PropertyExistsQuery,
        PropertyMaskQuery,
        PropertyQueryMaxDefined
    } STORAGE_QUERY_TYPE, *PSTORAGE_QUERY_TYPE; // https://msdn.microsoft.com/en-us/windows/ff566998(v=vs.80)

    typedef struct _STORAGE_PROPERTY_QUERY
    {
        STORAGE_PROPERTY_ID PropertyId;
        STORAGE_QUERY_TYPE QueryType;
        UCHAR AdditionalParameters[1];
    } STORAGE_PROPERTY_QUERY, *PSTORAGE_PROPERTY_QUERY; // https://msdn.microsoft.com/en-us/windows/ff566997(v=vs.80)

# pragma pack(push, 1)
    typedef struct _STORAGE_DEVICE_DESCRIPTOR {
        DWORD            Version;
        DWORD            Size;
        BYTE             DeviceType;
        BYTE             DeviceTypeModifier;
        BOOLEAN          RemovableMedia;
        BOOLEAN          CommandQueueing;
        DWORD            VendorIdOffset;
        DWORD            ProductIdOffset;
        DWORD            ProductRevisionOffset;
        DWORD            SerialNumberOffset;
        STORAGE_BUS_TYPE BusType;
        DWORD            RawPropertiesLength;
        BYTE             RawDeviceProperties[1];
    } STORAGE_DEVICE_DESCRIPTOR, *PSTORAGE_DEVICE_DESCRIPTOR; // https://docs.microsoft.com/en-us/windows/desktop/api/winioctl/ns-winioctl-_storage_device_descriptor
# pragma pack(pop)

    bool cmddrivename();
    bool cmdgeometry();
    bool cmddriveletter();

protected:
    bool getparam();
    void setparam(const drive_cmd *instanced);
    DWORD getsectorsize() const;
    sector_t gettotalsectors() const;
    std::vector<char> getdriveletter() const;
    char getdriveletter(int n) const;
    const std::wstring &getdrivevendor() const;
    const std::wstring &getdrivename() const;

protected:
    drive_cmd(int drive_target) : drive_handle(drive_target) {
        sector_size = SECTOR_SIZE_DEFAULT;
        total_sectors = TOTAL_SECTORS_DEFAULT;
        drive_vendor = DRIVEVENDOR_GET_FAILURE;
        drive_name = DRIVENAME_GET_FAILURE;
        std::memset(cDriveLetter, 0x00, sizeof(cDriveLetter));
    }
    virtual ~drive_cmd() {}
};

class drive_stream : protected drive_cmd
{
private:
    drive_stream()=delete;
    drive_stream(const drive_stream &)=delete;
    drive_stream &operator=(const drive_stream &)=delete;
    drive_stream(drive_stream &&)=delete;
    drive_stream &operator=(drive_stream &&)=delete;

    mutable std::vector<BYTE> buffer;
    mutable int64_t total_size;
    size_t getbuffersize() const {
        return buffer.size();
    }
    BYTE *getbuffer() const {
        return &buffer.at(0);
    }
    const BYTE *const_getbuffer() const {
        return &buffer.at(0);
    }

protected:
    std::vector<BYTE> *getbuffer_lock() {
        return getlock() ? &buffer: nullptr;
    }
    const std::vector<BYTE> *getbuffer_lock() const {
        return getlock() ? &buffer: nullptr;
    }

protected:
    bool readfile(sector_t offset, DWORD _size = 0) const;
    bool writefile(sector_t offset, DWORD _size = 0) const;

protected:
    drive_stream(int drive_target) : drive_cmd(drive_target) {bufclear();}
    virtual ~drive_stream() {}

    void alloc(size_t size) const;
    void allocrand(size_t size) const;
    int64_t gettotalsize() const;
    void bufclear() const;
};

#ifndef PREDICTION_UNDER_DEVELOPMENT

/////////////////////////////////////////////////////////////////////////
// BASE CLASS
/////////////////////////////////////////////////////////////////////////

class drive_base : protected drive_stream
{
private:
    drive_base(); // {}
    drive_base(const drive_base &); // {}
    drive_base &operator=(const drive_base &); // {}

    cla_thread<drive_base> thread;
    DWORD sectors_step;
    bool failure;
    drive_base *obj;

    virtual bool acc_thread(const bool &exit_flag) = 0;
    unsigned int _thread(cla_thread<drive_base>::thread_data *pdata) {
        if(! obj) {
            failure = true;
            return 1;
        }
        if(! obj->acc_thread(pdata->exit_flag)) {
            failure = true;
        }
        return 1;
    }

    bool accsectors(sector_t begin, sector_t end, const bool &exit_flag, bool readflag) const {
        if(! gethandle()) {return false;}
        if(exit_flag) {return true;}
        if(! getlock()) { // Note: When locked, the 'accsectors' is file mode.
            if(readflag) {
                alloc(getsectorsize() * sectors_step);
            } else {
                allocrand(getsectorsize() * sectors_step);
            }
        } else {
            if(readflag) {
                alloc(getsectorsize() * sectors_step);
            }
        }

        if(end == SECTORS_STEP) { // random access.
            end = begin + sectors_step - 1;
        }
        if(gettotalsectors() <= end) {
            end = gettotalsectors() - 1;
        }

        const sector_t range = end - begin + 1;
        const sector_t count = range / sectors_step;
        const int remain = (int)(range % sectors_step);
        const sector_t begin_offset = begin * getsectorsize();

        for(int i=0; i < count; ++i)
        {
            if(exit_flag) {return true;}
            if(readflag) {
                if (! readfile(begin_offset + (getsectorsize() * sectors_step * i))) {
                    return false;
                }
            } else {
                if (! writefile(begin_offset + (getsectorsize() * sectors_step * i))) {
                    return false;
                }
            }
        }

        if(0 < remain) {
            if(exit_flag) {return true;}
            if(readflag) {
                if (! readfile(begin_offset + (getsectorsize() * sectors_step * count), getsectorsize() * remain)) {
                    return false;
                }
            } else {
                if (! writefile(begin_offset + (getsectorsize() * sectors_step * count), getsectorsize() * remain)) {
                    return false;
                }
            }
        }

        return true;
    }

protected:
    // When the buffer integrity does not matter.
    bool readsectors(sector_t begin, sector_t end, const bool &exit_flag) const {
        return accsectors(begin, end, exit_flag, true);
    }
    bool writesectors(sector_t begin, sector_t end, const bool &exit_flag) const {
        return accsectors(begin, end, exit_flag, false);
    }

    // When the buffer integrity is required.
    bool readsectors_lock(sector_t begin, sector_t end, const bool &exit_flag) {
        return getlock() ? accsectors(begin, end, exit_flag, true): false;
    }
    bool writesectors_lock(sector_t begin, sector_t end, const bool &exit_flag) {
        return getlock() ? accsectors(begin, end, exit_flag, false): false;
    }

    bool start(drive_base *_obj) {
        clearfailure();
        obj = _obj;
        return thread.start(nullptr, this);
    }

protected:
    explicit drive_base(int drive_target) : drive_stream(drive_target), thread(&drive_base::_thread) {
        sectors_step = SECTORS_STEP_DEFAULT;
        failure = false;
        obj = nullptr;
    }

    //
    // READ or WRITE openhandle
    //
    bool base_openhandle(char mode, const drive_base *instanced = nullptr, bool lock = false, LPCWSTR path = nullptr) {
        setparam(instanced);
        if(mode == 'r') {
            if(! openread(lock)) {
                return false;
            }
        } else if (mode == 'w') {
            if(! openwrite(lock)) {
                return false;
            }
        } else if (mode == 'f') {
            if(! openwritefile('\0', path, lock)) {
                return false;
            }
            return true; // Note: unused getparam().
        } else if (mode == 'b') {
            if(! openwritefile(getdriveletter(0), nullptr, false)) {
                return false;
            }
        } else {
            return false;
        }
        return ((instanced == nullptr) ? getparam(): true);
    }

public:
    virtual ~drive_base() {}

    //
    // Method
    //
    void stop() {thread.stop();}
    void waitclose() {thread.waitclose();}
    bool signal() const {return thread.signal();}
    bool getfailure() const {return failure;}
    void clearfailure() {failure = false;}
    void setstep(DWORD sectors_size) { // Note: size
        if(0 < sectors_size) {
            sectors_step = sectors_size / getsectorsize();
            if(sectors_step == 0) { sectors_step = 1; }
        }
    }
    DWORD getstep() const {return sectors_step;}
    std::wstring getdriveinfo() const {
        std::wostringstream letter;
        letter << L"DriveLetter: ";
        std::vector<char> vcl = getdriveletter();
        for(std::vector<char>::const_iterator ite = vcl.begin(); ite != vcl.end(); ++ite)
        {
            letter << *ite << L":\\ ";
        }

        DWORD capacity = (DWORD)(gettotalsectors() * getsectorsize() / 1024 / 1024 / 1024);
        std::wostringstream stream;
        stream << getdrivevendor().c_str() << L"\n" << getdrivename().c_str() << L"\n" << L"Capacity: " << capacity << L" GB" << L"\n" << letter.str().c_str();
        return stream.str();
    }
    bool checkdriveletter() const {
        return (getdriveletter(0) != '\0');
    }
    std::wstring getdriveinfo(int) const {
        std::wostringstream stream;
        stream << getdrivevendor().c_str() << L" " << getdrivename().c_str();
        return stream.str();
    }
    double getspeed(double ti) const {
        return (double)gettotalsize() / ti;
    }
    const std::vector<BYTE> *getbufferread() const { // Read => getbuffer
        return getbuffer_lock();
    }
    std::vector<BYTE> *setbufferwrite() { // setbuffer => buffered => Write
        return getbuffer_lock();
    }
    virtual double getprog() const = 0;
    virtual void setaccpoint(sector_t _begin = 0, sector_t _end = 0) = 0;
    virtual void clearaccpoint() = 0;
    virtual bool openhandle(const drive_base *instanced = nullptr) = 0;
    virtual void set(const std::vector<sector_t> &_sectors_addr) = 0;
    virtual void setrand(const std::vector<unsigned __int64> &_rand_addr) = 0;
    virtual bool scan() = 0;
};

/////////////////////////////////////////////////////////////////////////
// SCAN START METHOD
/////////////////////////////////////////////////////////////////////////

class drive_method : public drive_base
{
private:
    drive_method(); // {}
    drive_method(const drive_method &); // {}
    drive_method &operator=(const drive_method &); // {}

    sector_t begin, end;
    virtual bool acc_thread(const bool &exit_flag) = 0;

protected:
    explicit drive_method(int drive_target) : drive_base(drive_target) {
        clearaccpoint();
    }
    virtual ~drive_method() {}

    sector_t getbegin() const {return begin;}
    sector_t getend() const {return end;}

public:
    //
    // Method
    //
    void setaccpoint(sector_t _begin = 0, sector_t _end = 0) final override {
        begin = _begin;
        end = _end;
    }
    void clearaccpoint() final override {
        begin = 0;
        end = 0;
    }
    virtual double getprog() const = 0;
    virtual void set(const std::vector<sector_t> &_sectors_addr) = 0;
    virtual void setrand(const std::vector<unsigned __int64> &_rand_addr) = 0;
    virtual bool openhandle(const drive_base *instanced = nullptr) = 0;
    bool scan() final override {
        return this->start(this);
    }
};

/////////////////////////////////////////////////////////////////////////
// SEQUENTIAL ACCESS
/////////////////////////////////////////////////////////////////////////

class drive_accseq : public drive_method
{
private:
    drive_accseq(); // {}
    drive_accseq(const drive_accseq &); // {}
    drive_accseq &operator=(const drive_accseq &); // {}

    static const __int64 unit_size = (__int64)100 * 1024 * 1024; // 100MB.
    static const __int64 inspect_size = (__int64)1 * 1024 * 1024 * 1024; // 1GB.

    sector_t seqbegin, seqend;
    int current;
    sector_t total;
    virtual bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const = 0;
    bool acc_thread(const bool &exit_flag) final override {
        if(seqbegin < SCAN_BEGIN_MIN_SECTOR) {seqbegin = SCAN_BEGIN_MIN_SECTOR;}
        const sector_t range = seqend - seqbegin;
        const sector_t unit_sectors = unit_size / getsectorsize();
        const sector_t count = total = range / unit_sectors;
        const int remain = (int)(range % unit_sectors);
        if(0 < remain) {++total;}

        for(int i=0; i < count; ++i)
        {
            if(exit_flag) {return true;}
            if(! _rwfunc(seqbegin + (i * unit_sectors), seqbegin + ((i + 1) * unit_sectors), exit_flag)) {
                return false;
            }
            ++current;
        }
        if(0 < remain) {
            if(exit_flag) {return true;}
            if(! _rwfunc(seqbegin + (count * unit_sectors), seqbegin + remain, exit_flag)) {
                return false;
            }
            ++current;
        }
        return true;
    }

protected:
    explicit drive_accseq(int drive_target) : drive_method(drive_target) {
        current = 0;
        total = 0;
        seqbegin = seqend = 0;
    }
    virtual ~drive_accseq() {}

public:
    //
    // Method
    //
    void set(const std::vector<sector_t> &_sectors_addr) final override {
        seqbegin = _sectors_addr[0];
        seqend = seqbegin + (inspect_size / getsectorsize());
        //if(gettotalsectors() <= seqend) {
        //    seqend = gettotalsectors() - 1;
        //}
    }
    void setrand(const std::vector<unsigned __int64> &_rand_addr) final override {
        seqbegin = _rand_addr[0] % gettotalsectors();
        seqend = seqbegin + (inspect_size / getsectorsize());
        //if(gettotalsectors() <= seqend) {
        //    seqend = gettotalsectors() - 1;
        //}
    }
    double getprog() const final override {
        if(0 < total) {
            double prog = (double)((double)current / (double)total);
            // OK debugcs::instance() << prog;
            return prog;
        } else {
            return 0.0;
        }
    }
    virtual bool openhandle(const drive_base *instanced = nullptr) = 0;
};

/*
** drive_seqread, drive_seqwrite, drive_randomread, drive_randomwrite
** Because it is for benchmarking, buffer exclusivity is not a problem.
*/
class drive_seqread final : public drive_accseq
{
private:
    drive_seqread(); // {}
    drive_seqread(const drive_seqread &); // {}
    drive_seqread &operator=(const drive_seqread &); // {}

    bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const final override {
        return readsectors(begin, end, exit_flag);
    }
public:
    explicit drive_seqread(int drive_target) : drive_accseq(drive_target) {}
    ~drive_seqread() {}

    //
    // Method
    //
    bool openhandle(const drive_base *instanced = nullptr) final override {
        return base_openhandle('r', instanced);
    }
};

class drive_seqwrite final : public drive_accseq
{
private:
    drive_seqwrite(); // {}
    drive_seqwrite(const drive_seqwrite &); // {}
    drive_seqwrite &operator=(const drive_seqwrite &); // {}

    bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const final override {
        bool ret = writesectors(begin, end, exit_flag);
        //::FlushFileBuffers(gethandle());
        return ret;
    }
public:
    explicit drive_seqwrite(int drive_target) : drive_accseq(drive_target) {}
    ~drive_seqwrite() {}

    //
    // Method
    //
    bool openhandle(const drive_base *instanced = nullptr) final override {
        return base_openhandle('w', instanced);
    }
};

/////////////////////////////////////////////////////////////////////////
// RANDOM ACCESS
/////////////////////////////////////////////////////////////////////////

class drive_accrandom : public drive_method
{
private:
    drive_accrandom(); // {}
    drive_accrandom(const drive_accrandom &); // {}
    drive_accrandom &operator=(const drive_accrandom &); // {}

    std::vector<sector_t> sectors_addr;
    int current;
    virtual bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const = 0;

protected:
    bool acc_thread(const bool &exit_flag) final override {
        if(sectors_addr.size() == 0) {return false;}
        current = 0;
        for(std::vector<sector_t>::const_iterator ite = sectors_addr.begin(); ite != sectors_addr.end(); ++ite)
        {
            if(exit_flag) {return true;}
            if(SCAN_BEGIN_MIN_SECTOR <= getbegin()) {
                if(! _rwfunc(getbegin(), SECTORS_STEP, exit_flag)) {
                    return false;
                }
            }

            sector_t randombegin = *ite;
            if(! getlock()) {
                if(randombegin < SCAN_BEGIN_MIN_SECTOR) {
                    randombegin = SCAN_BEGIN_MIN_SECTOR;
                }
            }
            if(! _rwfunc(randombegin, SECTORS_STEP, exit_flag)) {
                return false;
            }

            if(SCAN_BEGIN_MIN_SECTOR <= getend()) {
                if(! _rwfunc(getend(), SECTORS_STEP, exit_flag)) {
                    return false;
                }
            }
            ++current;
        }
        return true;
    }

protected:
    explicit drive_accrandom(int drive_target) : drive_method(drive_target) {
        current = 0;
    }
    virtual ~drive_accrandom() {}

public:
    //
    // Method
    //
    void set(const std::vector<sector_t> &_sectors_addr) final override {
        sectors_addr = _sectors_addr;
    }
    void setrand(const std::vector<unsigned __int64> &_rand_addr) final override {
        sectors_addr.clear();
        sectors_addr.reserve(_rand_addr.size());
        sector_t total_sectors = gettotalsectors();
        for(std::vector<unsigned __int64>::const_iterator ite = _rand_addr.begin(); ite != _rand_addr.end(); ++ite)
        {
            sector_t sector = (sector_t)(*ite % total_sectors);
            sectors_addr.push_back(sector);
        }
    }
    double getprog() const final override {
        if(0 < sectors_addr.size()) {
            double prog = (double)((double)current / sectors_addr.size());
            // OK debugcs::instance() << prog;
            return prog;
        } else {
            return 0.0;
        }
    }
    virtual bool openhandle(const drive_base *instanced = nullptr) = 0;
};

class drive_randomread final : public drive_accrandom
{
private:
    drive_randomread(); // {}
    drive_randomread(const drive_randomread &); // {}
    drive_randomread &operator=(const drive_randomread &); // {}

    bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const final override {
        return readsectors(begin, end, exit_flag);
    }
public:
    explicit drive_randomread(int drive_target) : drive_accrandom(drive_target) {}
    ~drive_randomread() {}

    //
    // Method
    //
    bool openhandle(const drive_base *instanced = nullptr) final override {
        return base_openhandle('r', instanced);
    }
};

class drive_randomwrite final : public drive_accrandom
{
private:
    drive_randomwrite(); // {}
    drive_randomwrite(const drive_randomwrite &); // {}
    drive_randomwrite &operator=(const drive_randomwrite &); // {}

    bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const final override {
        bool ret = writesectors(begin, end, exit_flag);
        //::FlushFileBuffers(gethandle());
        return ret;
    }
public:
    explicit drive_randomwrite(int drive_target) : drive_accrandom(drive_target) {}
    ~drive_randomwrite() {}

    //
    // Method
    //
    bool openhandle(const drive_base *instanced = nullptr) final override {
        return base_openhandle('w', instanced);
    }
};

/////////////////////////////////////////////////////////////////////////
// DATA
/////////////////////////////////////////////////////////////////////////

class drive_dataread final : public drive_accrandom
{
private:
    drive_dataread(); // {}
    drive_dataread(const drive_dataread &); // {}
    drive_dataread &operator=(const drive_dataread &); // {}

    bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const final override {
        return readsectors(begin, end, exit_flag);
    }
public:
    explicit drive_dataread(int drive_target) : drive_accrandom(drive_target) {}
    ~drive_dataread() {}

    //
    // Method
    //
    bool openhandle(const drive_base *instanced = nullptr) final override {
        return base_openhandle('r', instanced, true);
    }
};

class drive_datawritefull final : public drive_accrandom
{
private:
    // drive_datawritefull(); // {}
    drive_datawritefull(const drive_datawritefull &); // {}
    drive_datawritefull &operator=(const drive_datawritefull &); // {}

    std::wstring path;
    size_t padding;
    bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const final override {
        if(! writesectors(begin, end, exit_flag)) {
            return false;
        } else {
            const std::vector<BYTE> *p = getbufferread();
            LARGE_INTEGER li;
            li.QuadPart = p->size() - padding;
            return (::SetFilePointerEx(gethandle(), li, nullptr, FILE_BEGIN) && ::SetEndOfFile(gethandle())) ? true: false;
        }
    }
public:
    drive_datawritefull() : drive_accrandom(DRIVE_TARGET_UNUSED), padding(0) {}
    ~drive_datawritefull() {}

    //
    // Method
    //
    void setpath(LPCWSTR _path) {
        path = _path;
    }
    void setaddr() {
        std::vector<BYTE> *p = setbufferwrite();
        if(p->size() == 0) {return;}

        const size_t writestepsize = getstep() * getsectorsize();
        const sector_t wsectors = p->size() / writestepsize + 1;

        size_t orgsize = p->size();
        padding = writestepsize - orgsize % writestepsize;
        p->resize(writestepsize);

        std::vector<sector_t> sectors_addr;
        for(size_t i=0; i <= orgsize / (getsectorsize() * getstep()); i += wsectors)
        {
            sectors_addr.push_back(i);
        }
        set(sectors_addr);
    }
    bool openhandle(const drive_base *instanced = nullptr) final override {
        return base_openhandle('f', instanced, true, path.c_str());
    }
};

class drive_datawritelimit final : public drive_accrandom
{
private:
    drive_datawritelimit(); // {}
    drive_datawritelimit(const drive_datawritelimit &); // {}
    drive_datawritelimit &operator=(const drive_datawritelimit &); // {}

    static const __int64 limit_size = 100 * 1024 * 1024; // begin => under 100MB
    bool _rwfunc(sector_t begin, sector_t end, const bool &exit_flag) const final override {
        sector_t limit_sectors = limit_size / getsectorsize();
        // OK debugcs::instance() << L"[DriveDataWriteLimit]" << begin % limit_sectors << end;
        return writesectors(begin % limit_sectors, end, exit_flag);
    }
public:
    explicit drive_datawritelimit(int drive_target) : drive_accrandom(drive_target) {}
    ~drive_datawritelimit() {}

    //
    // Method
    //
    bool openhandle(const drive_base *instanced = nullptr) final override {
        return base_openhandle('b', instanced, false);
    }
};
#endif // PREDICTION_UNDER_DEVELOPMENT

#endif
