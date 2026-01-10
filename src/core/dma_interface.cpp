#include "dma_interface.h"
#include "runtime_manager.h"

#include <cstring>
#include <iostream>
#include <algorithm>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
    #define VMM_LIBRARY_NAME "vmm.dll"
#else
    #include <dlfcn.h>
    // Windows compatibility types - must match Windows sizes exactly!
    // On 64-bit Linux, 'long' is 8 bytes but Windows DWORD is always 4 bytes
    typedef int BOOL;                       // 4 bytes (same as Windows)
    typedef unsigned int DWORD;             // 4 bytes (NOT unsigned long!)
    typedef DWORD* PDWORD;
    typedef unsigned char BYTE;
    typedef BYTE* PBYTE;
    typedef unsigned long long ULONG64;     // 8 bytes
    typedef ULONG64* PULONG64;
    typedef char* LPSTR;
    typedef const char* LPCSTR;
    typedef size_t SIZE_T;
    typedef SIZE_T* PSIZE_T;
    typedef unsigned short WORD;            // 2 bytes
    #define TRUE 1
    #define FALSE 0
    #define MAX_PATH 260
    #define VMM_LIBRARY_NAME "vmm.so"
#endif

namespace orpheus {

// ============================================================================
// VMM Types - Must match DLL exactly (NO packing - use natural alignment)
// ============================================================================

// Memory model enum (4 bytes)
enum VMM_MEMORYMODEL_TP {
    VMM_MEMORYMODEL_NA = 0,
    VMM_MEMORYMODEL_X86 = 1,
    VMM_MEMORYMODEL_X86PAE = 2,
    VMM_MEMORYMODEL_X64 = 3,
    VMM_MEMORYMODEL_ARM64 = 4,
};

// System type enum (4 bytes)
enum VMM_SYSTEM_TP {
    VMM_SYSTEM_UNKNOWN_PHYSICAL = 0,
    VMM_SYSTEM_UNKNOWN_64 = 1,
    VMM_SYSTEM_WINDOWS_64 = 2,
    VMM_SYSTEM_UNKNOWN_32 = 3,
    VMM_SYSTEM_WINDOWS_32 = 4,
};

// Process integrity level (4 bytes)
enum VMM_PROCESS_INTEGRITY_LEVEL {
    VMM_INTEGRITY_LEVEL_UNKNOWN = 0,
    VMM_INTEGRITY_LEVEL_UNTRUSTED = 1,
    VMM_INTEGRITY_LEVEL_LOW = 2,
    VMM_INTEGRITY_LEVEL_MEDIUM = 3,
    VMM_INTEGRITY_LEVEL_MEDIUMPLUS = 4,
    VMM_INTEGRITY_LEVEL_HIGH = 5,
    VMM_INTEGRITY_LEVEL_SYSTEM = 6,
    VMM_INTEGRITY_LEVEL_PROTECTED = 7,
};

// Process information struct - NATURAL ALIGNMENT (no packing!)
struct VMM_PROCESS_INFORMATION {
    ULONG64 magic;                      // 0
    WORD wVersion;                      // 8
    WORD wSize;                         // 10
    VMM_MEMORYMODEL_TP tpMemoryModel;   // 12 (4 bytes enum)
    VMM_SYSTEM_TP tpSystem;             // 16 (4 bytes enum)
    BOOL fUserOnly;                     // 20
    DWORD dwPID;                        // 24
    DWORD dwPPID;                       // 28
    DWORD dwState;                      // 32
    char szName[16];                    // 36
    char szNameLong[64];                // 52
    DWORD _pad1;                        // 116 (padding for 8-byte alignment)
    ULONG64 paDTB;                      // 120
    ULONG64 paDTB_UserOpt;              // 128
    struct {
        ULONG64 vaEPROCESS;             // 136
        ULONG64 vaPEB;                  // 144
        ULONG64 _Reserved1;             // 152
        BOOL fWow64;                    // 160
        DWORD vaPEB32;                  // 164
        DWORD dwSessionId;              // 168
        DWORD _pad2;                    // 172 (padding)
        ULONG64 qwLUID;                 // 176
        char szSID[MAX_PATH];           // 184
        VMM_PROCESS_INTEGRITY_LEVEL IntegrityLevel; // 444
    } win;
};

// Module entry struct
struct VMM_MAP_MODULEENTRY {
    ULONG64 vaBase;
    ULONG64 vaEntry;
    DWORD cbImageSize;
    BOOL fWoW64;
    LPSTR uszText;
    DWORD _Reserved3;
    DWORD _Reserved4;
    LPSTR uszFullName;
    DWORD tp;
    DWORD cbFileSizeRaw;
    DWORD cSection;
    DWORD cEAT;
    DWORD cIAT;
    DWORD _Reserved2;
    ULONG64 _Reserved1[3];
    void* pExDebugInfo;
    void* pExVersionInfo;
};

// Module map struct
struct VMM_MAP_MODULE {
    DWORD dwVersion;
    DWORD _Reserved1[5];
    ULONG64 pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMM_MAP_MODULEENTRY pMap[1];
};

// VAD (Virtual Address Descriptor) entry struct - memory regions
struct VMM_MAP_VADENTRY {
    ULONG64 vaStart;
    ULONG64 vaEnd;
    ULONG64 vaVad;
    // DWORD 0: bitfield for VadType(3), Protection(5), fImage(1), fFile(1), fPageFile(1), fPrivateMemory(1), etc.
    DWORD dw0;
    // DWORD 1: CommitCharge(31), MemCommit(1)
    DWORD dw1;
    DWORD u2;
    DWORD cbPrototypePte;
    ULONG64 vaPrototypePte;
    ULONG64 vaSubsection;
    LPSTR uszText;  // Description text (e.g., module name for image mappings)
    DWORD _FutureUse1;
    DWORD _Reserved1;
    ULONG64 vaFileObject;
    DWORD cVadExPages;
    DWORD cVadExPagesBase;
    ULONG64 _Reserved2;
};

// VAD map struct
struct VMM_MAP_VAD {
    DWORD dwVersion;
    DWORD _Reserved1[4];
    DWORD cPage;
    ULONG64 pbMultiText;
    DWORD cbMultiText;
    DWORD cMap;
    VMM_MAP_VADENTRY pMap[1];
};

// Constants
static constexpr ULONG64 PROCESS_INFO_MAGIC = 0xc0ffee663df9301eULL;
static constexpr WORD PROCESS_INFO_VERSION = 7;
static constexpr DWORD OPT_STRING_PATH_USER_IMAGE = 2;
static constexpr ULONG64 FLAG_ZEROPAD_ON_FAIL = 0x0002ULL;

// ============================================================================
// Function pointer types
// ============================================================================

using FN_Initialize = void*(*)(DWORD argc, LPCSTR argv[]);
using FN_Close = void(*)(void*);
using FN_MemFree = void(*)(void*);
using FN_PidList = BOOL(*)(void*, PDWORD, PSIZE_T);
using FN_ProcessGetInformation = BOOL(*)(void*, DWORD, VMM_PROCESS_INFORMATION*, PSIZE_T);
using FN_ProcessGetInformationAll = BOOL(*)(void*, VMM_PROCESS_INFORMATION**, PDWORD);
using FN_ProcessGetInformationString = LPSTR(*)(void*, DWORD, DWORD);
using FN_MapGetModuleU = BOOL(*)(void*, DWORD, VMM_MAP_MODULE**, DWORD);
using FN_MapGetVadU = BOOL(*)(void*, DWORD, BOOL, VMM_MAP_VAD**);
using FN_MemRead = BOOL(*)(void*, DWORD, ULONG64, PBYTE, DWORD);
using FN_MemReadEx = BOOL(*)(void*, DWORD, ULONG64, PBYTE, DWORD, PDWORD, ULONG64);
using FN_MemWrite = BOOL(*)(void*, DWORD, ULONG64, PBYTE, DWORD);
using FN_MemVirt2Phys = BOOL(*)(void*, DWORD, ULONG64, PULONG64);
using FN_ScatterInitialize = void*(*)(void*, DWORD, DWORD);
using FN_ScatterPrepare = BOOL(*)(void*, ULONG64, DWORD);
using FN_ScatterExecute = BOOL(*)(void*);
using FN_ScatterRead = BOOL(*)(void*, ULONG64, DWORD, PBYTE, PDWORD);
using FN_ScatterCloseHandle = void(*)(void*);
using FN_ConfigSet = BOOL(*)(void*, ULONG64, ULONG64);
using FN_ConfigGet = BOOL(*)(void*, ULONG64, PULONG64);
using FN_VfsReadU = DWORD(*)(void*, LPCSTR, PBYTE, DWORD, PDWORD, ULONG64);

// LeechCore FPGA option to get device ID
static constexpr ULONG64 LC_OPT_FPGA_FPGA_ID = 0x0300008100000000ULL;

// FPGA Device ID to friendly name mapping (from LeechCore device_fpga.c)
static const char* GetFPGADeviceName(uint64_t device_id) {
    switch (device_id) {
        case 0x00: return "SP605 / FT601";
        case 0x01: return "PCIeScreamer R1";
        case 0x02: return "AC701 / FT601";
        case 0x03: return "PCIeScreamer R2";
        case 0x04: return "ScreamerM2";
        case 0x05: return "NeTV2 RawUDP";
        case 0x08: return "FT2232H";
        case 0x09: return "Enigma X1";
        case 0x0A: return "Enigma X2";
        case 0x0B: return "ScreamerM2x4";
        case 0x0C: return "PCIeSquirrel";
        case 0x0D: return "Device #13N";
        case 0x0E: return "Device #14T";
        case 0x0F: return "Device #15N";
        case 0x10: return "Device #16T";
        default:   return nullptr;  // Unknown device
    }
}

// VMM config options for refresh
static constexpr ULONG64 OPT_REFRESH_ALL = 0x2001ffff;

// Static function pointers
static FN_Initialize fn_Initialize = nullptr;
static FN_Close fn_Close = nullptr;
static FN_MemFree fn_MemFree = nullptr;
static FN_PidList fn_PidList = nullptr;
static FN_ProcessGetInformation fn_ProcessGetInfo = nullptr;
static FN_ProcessGetInformationAll fn_ProcessGetInfoAll = nullptr;
static FN_ProcessGetInformationString fn_ProcessGetInfoString = nullptr;
static FN_MapGetModuleU fn_MapGetModuleU = nullptr;
static FN_MapGetVadU fn_MapGetVadU = nullptr;
static FN_MemRead fn_MemRead = nullptr;
static FN_MemReadEx fn_MemReadEx = nullptr;
static FN_MemWrite fn_MemWrite = nullptr;
static FN_MemVirt2Phys fn_Virt2Phys = nullptr;
static FN_ScatterInitialize fn_ScatterInit = nullptr;
static FN_ScatterPrepare fn_ScatterPrepare = nullptr;
static FN_ScatterExecute fn_ScatterExecute = nullptr;
static FN_ScatterRead fn_ScatterRead = nullptr;
static FN_ScatterCloseHandle fn_ScatterClose = nullptr;
static FN_ConfigSet fn_ConfigSet = nullptr;
static FN_ConfigGet fn_ConfigGet = nullptr;
static FN_VfsReadU fn_VfsReadU = nullptr;

static void* vmm_module = nullptr;

template<typename T>
static T LoadFunction(const char* name) {
#ifdef PLATFORM_WINDOWS
    return reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(vmm_module), name));
#else
    return reinterpret_cast<T>(dlsym(vmm_module, name));
#endif
}

static bool LoadVMMFunctions() {
    if (vmm_module == nullptr) return false;

    fn_Initialize = LoadFunction<FN_Initialize>("VMMDLL_Initialize");
    fn_Close = LoadFunction<FN_Close>("VMMDLL_Close");
    fn_MemFree = LoadFunction<FN_MemFree>("VMMDLL_MemFree");
    fn_PidList = LoadFunction<FN_PidList>("VMMDLL_PidList");
    fn_ProcessGetInfo = LoadFunction<FN_ProcessGetInformation>("VMMDLL_ProcessGetInformation");
    fn_ProcessGetInfoAll = LoadFunction<FN_ProcessGetInformationAll>("VMMDLL_ProcessGetInformationAll");
    fn_ProcessGetInfoString = LoadFunction<FN_ProcessGetInformationString>("VMMDLL_ProcessGetInformationString");
    fn_MapGetModuleU = LoadFunction<FN_MapGetModuleU>("VMMDLL_Map_GetModuleU");
    fn_MapGetVadU = LoadFunction<FN_MapGetVadU>("VMMDLL_Map_GetVadU");
    fn_MemRead = LoadFunction<FN_MemRead>("VMMDLL_MemRead");
    fn_MemReadEx = LoadFunction<FN_MemReadEx>("VMMDLL_MemReadEx");
    fn_MemWrite = LoadFunction<FN_MemWrite>("VMMDLL_MemWrite");
    fn_Virt2Phys = LoadFunction<FN_MemVirt2Phys>("VMMDLL_MemVirt2Phys");
    fn_ScatterInit = LoadFunction<FN_ScatterInitialize>("VMMDLL_Scatter_Initialize");
    fn_ScatterPrepare = LoadFunction<FN_ScatterPrepare>("VMMDLL_Scatter_Prepare");
    fn_ScatterExecute = LoadFunction<FN_ScatterExecute>("VMMDLL_Scatter_Execute");
    fn_ScatterRead = LoadFunction<FN_ScatterRead>("VMMDLL_Scatter_Read");
    fn_ScatterClose = LoadFunction<FN_ScatterCloseHandle>("VMMDLL_Scatter_CloseHandle");
    fn_ConfigSet = LoadFunction<FN_ConfigSet>("VMMDLL_ConfigSet");
    fn_ConfigGet = LoadFunction<FN_ConfigGet>("VMMDLL_ConfigGet");
    fn_VfsReadU = LoadFunction<FN_VfsReadU>("VMMDLL_VfsReadU");

    return fn_Initialize && fn_Close && fn_MemFree && fn_PidList && fn_ProcessGetInfo && fn_MemRead;
}

// ============================================================================
// DMAInterface Implementation
// ============================================================================

DMAInterface::DMAInterface() = default;
DMAInterface::~DMAInterface() { Close(); }

DMAInterface::DMAInterface(DMAInterface&& other) noexcept
    : vmm_handle_(other.vmm_handle_)
    , last_error_(std::move(other.last_error_))
    , error_callback_(std::move(other.error_callback_)) {
    other.vmm_handle_ = nullptr;
}

DMAInterface& DMAInterface::operator=(DMAInterface&& other) noexcept {
    if (this != &other) {
        Close();
        vmm_handle_ = other.vmm_handle_;
        last_error_ = std::move(other.last_error_);
        error_callback_ = std::move(other.error_callback_);
        other.vmm_handle_ = nullptr;
    }
    return *this;
}

bool DMAInterface::Initialize(const std::string& device) {
    if (vmm_handle_ != nullptr) return true;

    auto& runtime = RuntimeManager::Instance();
    if (!runtime.IsInitialized()) {
        ReportError("RuntimeManager not initialized");
        return false;
    }

    if (vmm_module == nullptr) {
        vmm_module = runtime.LoadExtractedDLL(VMM_LIBRARY_NAME);
        if (vmm_module == nullptr) {
            ReportError("Failed to load " VMM_LIBRARY_NAME);
            return false;
        }
        if (!LoadVMMFunctions()) {
            ReportError("Failed to load VMM functions");
            return false;
        }
    }

    std::vector<const char*> argv;
    argv.push_back("");
    argv.push_back("-device");
    argv.push_back(device.c_str());
    // Disable symbol server to prevent popup dialogs on refresh
    argv.push_back("-disable-symbolserver");

    vmm_handle_ = fn_Initialize(static_cast<DWORD>(argv.size()), const_cast<LPCSTR*>(argv.data()));
    if (vmm_handle_ == nullptr) {
        ReportError("VMMDLL_Initialize failed for device: " + device);
        return false;
    }

    // Try to get the actual FPGA device name via ConfigGet
    device_type_ = device;  // Fallback to device string (e.g., "fpga")
    if (fn_ConfigGet && device == "fpga") {
        ULONG64 fpga_id = 0;
        if (fn_ConfigGet(vmm_handle_, LC_OPT_FPGA_FPGA_ID, &fpga_id)) {
            const char* name = GetFPGADeviceName(fpga_id);
            if (name) {
                device_type_ = name;
            }
        }
    }

    return true;
}

void DMAInterface::Close() {
    if (vmm_handle_ != nullptr && fn_Close != nullptr) {
        fn_Close(vmm_handle_);
        vmm_handle_ = nullptr;
        device_type_.clear();
    }
}

std::vector<ProcessInfo> DMAInterface::GetProcessList() {
    std::vector<ProcessInfo> result;
    if (!IsConnected() || fn_ProcessGetInfoAll == nullptr) return result;

    // Force refresh of process cache to get accurate, live data
    if (fn_ConfigSet != nullptr) {
        fn_ConfigSet(vmm_handle_, OPT_REFRESH_ALL, 1);
    }

    VMM_PROCESS_INFORMATION* pInfo = nullptr;
    DWORD count = 0;

    if (!fn_ProcessGetInfoAll(vmm_handle_, &pInfo, &count) || pInfo == nullptr || count == 0) {
        return result;
    }

    result.reserve(count);
    for (DWORD i = 0; i < count; i++) {
        // Skip terminated/exiting processes (dwState != 0 means not running)
        if (pInfo[i].dwState != 0) continue;

        // Skip processes with invalid DTB (another indicator of dead process)
        if (pInfo[i].paDTB == 0) continue;

        ProcessInfo info;
        info.pid = pInfo[i].dwPID;
        info.ppid = pInfo[i].dwPPID;
        info.name = pInfo[i].szName;
        info.base_address = pInfo[i].win.vaEPROCESS;
        info.peb_address = pInfo[i].win.vaPEB;
        info.dtb = pInfo[i].paDTB;
        info.is_64bit = (pInfo[i].tpMemoryModel == VMM_MEMORYMODEL_X64);
        info.is_wow64 = pInfo[i].win.fWow64 != 0;
        info.state = pInfo[i].dwState;

        if (fn_ProcessGetInfoString != nullptr) {
            LPSTR path = fn_ProcessGetInfoString(vmm_handle_, info.pid, OPT_STRING_PATH_USER_IMAGE);
            if (path != nullptr) {
                info.path = path;
                fn_MemFree(path);
            }
        }
        result.push_back(std::move(info));
    }

    fn_MemFree(pInfo);
    return result;
}

std::optional<ProcessInfo> DMAInterface::GetProcessInfo(uint32_t pid) {
    if (!IsConnected() || fn_ProcessGetInfo == nullptr) return std::nullopt;

    VMM_PROCESS_INFORMATION info = {};
    info.magic = PROCESS_INFO_MAGIC;
    info.wVersion = PROCESS_INFO_VERSION;
    SIZE_T cbInfo = sizeof(info);

    if (!fn_ProcessGetInfo(vmm_handle_, pid, &info, &cbInfo)) return std::nullopt;

    ProcessInfo result;
    result.pid = info.dwPID;
    result.ppid = info.dwPPID;
    result.name = info.szName;
    result.base_address = info.win.vaEPROCESS;
    result.peb_address = info.win.vaPEB;
    result.dtb = info.paDTB;
    result.is_64bit = (info.tpMemoryModel == VMM_MEMORYMODEL_X64);
    result.is_wow64 = info.win.fWow64 != 0;
    result.state = info.dwState;

    if (fn_ProcessGetInfoString != nullptr) {
        LPSTR path = fn_ProcessGetInfoString(vmm_handle_, pid, OPT_STRING_PATH_USER_IMAGE);
        if (path != nullptr) {
            result.path = path;
            fn_MemFree(path);
        }
    }
    return result;
}

std::optional<ProcessInfo> DMAInterface::GetProcessByName(const std::string& name) {
    auto processes = GetProcessList();
    for (const auto& proc : processes) {
#ifdef PLATFORM_WINDOWS
        if (_stricmp(proc.name.c_str(), name.c_str()) == 0) return proc;
#else
        if (strcasecmp(proc.name.c_str(), name.c_str()) == 0) return proc;
#endif
    }
    return std::nullopt;
}

std::vector<ModuleInfo> DMAInterface::GetModuleList(uint32_t pid) {
    std::vector<ModuleInfo> result;
    if (!IsConnected() || fn_MapGetModuleU == nullptr) return result;

    VMM_MAP_MODULE* pModuleMap = nullptr;
    if (!fn_MapGetModuleU(vmm_handle_, pid, &pModuleMap, 0) || pModuleMap == nullptr) {
        return result;
    }

    result.reserve(pModuleMap->cMap);
    for (DWORD i = 0; i < pModuleMap->cMap; i++) {
        const auto& entry = pModuleMap->pMap[i];
        ModuleInfo info;
        info.name = entry.uszText ? entry.uszText : "";
        info.path = entry.uszFullName ? entry.uszFullName : "";
        info.base_address = entry.vaBase;
        info.entry_point = entry.vaEntry;
        info.size = entry.cbImageSize;
        info.is_64bit = entry.fWoW64 == 0;
        result.push_back(std::move(info));
    }

    fn_MemFree(pModuleMap);
    return result;
}

std::optional<ModuleInfo> DMAInterface::GetModuleByName(uint32_t pid, const std::string& name) {
    auto modules = GetModuleList(pid);
    for (const auto& mod : modules) {
#ifdef PLATFORM_WINDOWS
        if (_stricmp(mod.name.c_str(), name.c_str()) == 0) return mod;
#else
        if (strcasecmp(mod.name.c_str(), name.c_str()) == 0) return mod;
#endif
    }
    return std::nullopt;
}

std::vector<MemoryRegion> DMAInterface::GetMemoryRegions(uint32_t pid) {
    std::vector<MemoryRegion> result;
    if (!IsConnected() || fn_MapGetVadU == nullptr) return result;

    VMM_MAP_VAD* pVadMap = nullptr;
    if (!fn_MapGetVadU(vmm_handle_, pid, TRUE, &pVadMap) || pVadMap == nullptr) {
        return result;
    }

    result.reserve(pVadMap->cMap);
    for (DWORD i = 0; i < pVadMap->cMap; i++) {
        const auto& entry = pVadMap->pMap[i];
        MemoryRegion region;
        region.base_address = entry.vaStart;
        region.size = entry.vaEnd - entry.vaStart + 1;
        
        // Extract protection from dw0 bitfield (bits 3-7)
        DWORD protection = (entry.dw0 >> 3) & 0x1F;
        // Convert to string representation
        static const char* prot_strings[] = {
            "---", "--R", "-W-", "-WR", "E--", "E-R", "EW-", "EWR",
            "---", "--R", "-WC", "-WCR", "E--", "E-R", "EWC", "EWCR",
            "---", "--R", "-W-", "-WR", "E--", "E-R", "EW-", "EWR",
            "---", "--R", "-WC", "-WCR", "E--", "E-R", "EWC", "EWCR"
        };
        region.protection = prot_strings[protection & 0x1F];
        
        // Determine type from flags in dw0
        bool fImage = (entry.dw0 >> 8) & 1;
        bool fFile = (entry.dw0 >> 9) & 1;
        bool fPrivate = (entry.dw0 >> 11) & 1;
        bool fStack = (entry.dw0 >> 13) & 1;
        bool fHeap = (entry.dw0 >> 23) & 1;
        
        if (fImage) region.type = "Image";
        else if (fStack) region.type = "Stack";
        else if (fHeap) region.type = "Heap";
        else if (fFile) region.type = "Mapped";
        else if (fPrivate) region.type = "Private";
        else region.type = "Unknown";
        
        // Info text from VMM
        region.info = entry.uszText ? entry.uszText : "";
        
        result.push_back(std::move(region));
    }

    if (fn_MemFree && pVadMap) {
        fn_MemFree(pVadMap);
    }

    return result;
}

std::vector<uint8_t> DMAInterface::ReadMemory(uint32_t pid, uint64_t address, size_t size) {
    std::vector<uint8_t> result;
    if (!IsConnected() || fn_MemReadEx == nullptr || size == 0) return result;

    result.resize(size);
    DWORD bytesRead = 0;

    BOOL success = fn_MemReadEx(vmm_handle_, pid, address, result.data(),
                                 static_cast<DWORD>(size), &bytesRead, FLAG_ZEROPAD_ON_FAIL);

    if (!success || bytesRead == 0) {
        result.clear();
    } else if (bytesRead < size) {
        result.resize(bytesRead);
    }
    return result;
}

bool DMAInterface::WriteMemory(uint32_t pid, uint64_t address, const std::vector<uint8_t>& data) {
    if (!IsConnected() || fn_MemWrite == nullptr || data.empty()) return false;
    return fn_MemWrite(vmm_handle_, pid, address, const_cast<PBYTE>(data.data()),
                        static_cast<DWORD>(data.size())) != 0;
}

size_t DMAInterface::ScatterRead(uint32_t pid, std::vector<ScatterRequest>& requests) {
    if (!IsConnected() || requests.empty()) return 0;

    // Fallback to individual reads if scatter API not available
    if (!fn_ScatterInit || !fn_ScatterPrepare || !fn_ScatterExecute || !fn_ScatterRead || !fn_ScatterClose) {
        size_t success_count = 0;
        for (auto& req : requests) {
            auto data = ReadMemory(pid, req.address, req.size);
            req.success = !data.empty();
            if (req.success) {
                req.data = std::move(data);
                success_count++;
            }
        }
        return success_count;
    }

    void* scatter = fn_ScatterInit(vmm_handle_, pid, 0);
    if (!scatter) return 0;

    for (const auto& req : requests) {
        fn_ScatterPrepare(scatter, req.address, req.size);
    }

    if (!fn_ScatterExecute(scatter)) {
        fn_ScatterClose(scatter);
        return 0;
    }

    size_t success_count = 0;
    for (auto& req : requests) {
        req.data.resize(req.size);
        DWORD bytesRead = 0;
        req.success = fn_ScatterRead(scatter, req.address, req.size, req.data.data(), &bytesRead) != 0;
        if (req.success) {
            req.data.resize(bytesRead);
            success_count++;
        } else {
            req.data.clear();
        }
    }

    fn_ScatterClose(scatter);
    return success_count;
}

std::string DMAInterface::ReadString(uint32_t pid, uint64_t address, size_t max_length) {
    auto data = ReadMemory(pid, address, max_length);
    if (data.empty()) return "";
    auto it = std::find(data.begin(), data.end(), '\0');
    return (it != data.end()) ? std::string(data.begin(), it) : std::string(data.begin(), data.end());
}

std::wstring DMAInterface::ReadWideString(uint32_t pid, uint64_t address, size_t max_length) {
    auto data = ReadMemory(pid, address, max_length * 2);
    if (data.empty()) return L"";
    const wchar_t* wstr = reinterpret_cast<const wchar_t*>(data.data());
    size_t wlen = data.size() / 2;
    for (size_t i = 0; i < wlen; i++) {
        if (wstr[i] == L'\0') return std::wstring(wstr, i);
    }
    return std::wstring(wstr, wlen);
}

std::optional<uint64_t> DMAInterface::VirtualToPhysical(uint32_t pid, uint64_t virtual_addr) {
    if (!IsConnected() || fn_Virt2Phys == nullptr) return std::nullopt;
    ULONG64 physical = 0;
    return fn_Virt2Phys(vmm_handle_, pid, virtual_addr, &physical) ? std::optional<uint64_t>(physical) : std::nullopt;
}

std::vector<uint8_t> DMAInterface::ReadPhysical(uint64_t physical_addr, size_t size) {
    return ReadMemory(static_cast<uint32_t>(-1), physical_addr, size);
}

void DMAInterface::ReportError(const std::string& message) {
    last_error_ = message;
    if (error_callback_) error_callback_(message);
    else std::cerr << "[DMAInterface ERROR] " << message << std::endl;
}

} // namespace orpheus
