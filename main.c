/*
 * OpenWebUI Setup Center
 * Modern dark Win32 UI - Redesigned
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <time.h>

static int RunDaemonCommand(int argc, char **argv);
static int RunDaemonLoop(int port);

#define IDI_ICON1 101
#define VALORA_APP_DIR "Valora"
#define VALORA_CONFIG_NAME "config.ini"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ws2_32.lib")

/* ──────────────────────────────────────────────
   Modern Dark Theme Colors (OpenWebUI Style)
   ────────────────────────────────────────────── */
#define C_BG        RGB(15, 18, 24)
#define C_PANEL     RGB(24, 28, 36)
#define C_PANEL2    RGB(38, 44, 54)
#define C_BORDER    RGB(58, 66, 79)
#define C_ACCENT    RGB(35, 184, 190)
#define C_ACCENT2   RGB(67, 208, 214)
#define C_ACCENT_SOFT RGB(24, 86, 98)
#define C_TEXT      RGB(244, 247, 251)
#define C_MUTED     RGB(160, 171, 188)
#define C_INPUT     RGB(19, 23, 31)
#define C_INPUT_BORDER RGB(83, 94, 110)
#define C_SUCCESS   RGB(34, 197, 94)

/* ──────────────────────────────────────────────
   Layout Constants (Compact Style)
   ────────────────────────────────────────────── */
#define APP_W       700
#define APP_H       500
#define CARD_MAX_W  660
#define HEADER_H    50
#define FOOTER_H    20
#define PAD         12

#define MAX_MODELS  256
#define MAX_CMD     4096

/* ──────────────────────────────────────────────
   Control IDs
   ────────────────────────────────────────────── */
enum {
    ID_BTN_SERVER = 1001,
    ID_BTN_FOLDER,
    ID_BTN_PREV,
    ID_BTN_NEXT,
    ID_BTN_GENERATE,
    ID_BTN_COPY
};

/* ──────────────────────────────────────────────
   App state
   ────────────────────────────────────────────── */
static char sServer[MAX_PATH] = "";
static char sFolder[MAX_PATH] = "";
static char sSelectedModel[MAX_PATH] = "";
static char sModels[MAX_MODELS][MAX_PATH];
static char sModelTypes[MAX_MODELS][16];
static char sModelProjectors[MAX_MODELS][MAX_PATH];
static int  nModels = 0;
static char sCommand[MAX_CMD] = "";
static char sConfigPath[MAX_PATH] = "";
static int  nServerType = 0;
static char sCustomCtx[32] = "";

/* llama.cpp setup state */
static char sLlamaCppPath[MAX_PATH] = "";

/* GPU auto-detection state */
static BOOL bGpuFieldEdited = FALSE;
static int  nLastDetectedGpu = -1;

/* Safety Controller State */
static char sLastSafetyReason[64] = "";
static HANDLE sActiveProcessHandle = NULL;
static DWORD sActiveProcessId = 0;
static BOOL sModelLoaded = FALSE;

/* Safety Thresholds */
#define SAFE_RAM_BUFFER_MB 2048
#define SAFE_VRAM_BUFFER_MB 512
#define WARNING_RAM_MB 4096
#define WARNING_COMMIT_PERCENT 92
#define DANGER_COMMIT_PERCENT 97

/* ──────────────────────────────────────────────
   Handles
   ────────────────────────────────────────────── */
static HWND hServerEdit, hFolderEdit, hModelCombo;
static HWND hCtxEdit, hGpuEdit, hPortEdit, hThreadsEdit;
static HWND hKvCacheKCombo;
static HWND hKvCacheVCombo;
static HWND hServerTypeCombo;
static HWND hOutputEdit;
static HWND hBtnServer, hBtnFolder, hBtnPrev, hBtnNext, hBtnGenerate, hBtnCopy;
// NEW: Static controls for labels so they align perfectly
static HWND hLblServer, hLblFolder, hLblModel, hLblCtx, hLblGpu, hLblPort, hLblThreads, hLblServerType, hLblCommandType, hLblKvCacheK, hLblKvCacheV;

/* ──────────────────────────────────────────────
   GDI resources
   ────────────────────────────────────────────── */
static HBRUSH brBg, brPanel, brPanel2, brInput;
static HFONT hFontTitle, hFontBody, hFontSmall, hFontBold, hFontLabel;

static void ScanModels(void);
static void GetCliPathFromServerPath(const char *serverPath, char *cliPath, int cliPathLen);
static int RunInteractiveModelSelector(char *selectedModel, int selectedModelLen);
static void GetModelConfig(const char *modelName, char *ctx, int ctxLen, char *gpu, int gpuLen, char *threads, int threadsLen);
static BOOL HasModelConfig(const char *modelName);
static const char *GetModelTypeLabel(const char *modelName);
static BOOL FindMatchingProjector(const char *modelName, char *projectorName, int projectorNameLen);
static BOOL NormalizeHuggingFaceRepo(const char *input, char *repo, int repoLen);
static BOOL WriteHuggingFaceDownloaderScript(char *scriptPath, int scriptPathLen);
static int EnsureModelsFolderReady(void);

/* HuggingFace model download */
static int DownloadFromHuggingFace(const char *hfUrl);

/* Chat Mode */
static int RunChatMode(void);
static BOOL sDebugMode = FALSE;
static BOOL sOllamaMode = FALSE;
static char sOllamaUrl[256] = "http://127.0.0.1:11434";
static char sSelectedOllamaModel[256] = "";

/* ──────────────────────────────────────────────
   llama.cpp Setup
   ────────────────────────────────────────────── */
typedef enum {
    OS_UNKNOWN = 0,
    OS_WINDOWS,
    OS_LINUX,
    OS_MAC
} OSType;

typedef struct {
    char name[256];
    char downloadUrl[512];
    int size;
    BOOL isVulkan;
    BOOL isCpu;
    OSType os;
} LlamaCppBuild;

static OSType DetectOS(void);
static int FetchLlamaCppReleases(LlamaCppBuild **builds, int *count, const char *buildPattern);
static void FreeLlamaCppBuilds(LlamaCppBuild *builds, int count);
static int SelectBuildMenu(LlamaCppBuild *builds, int count);
static int ExtractZip(const char *zipPath, const char *destDir);
static BOOL VerifyLlamaCppInstall(const char *installPath);
static int SetupLlamaCpp(void);
static int UpdateLlamaCpp(void);

/* ──────────────────────────────────────────────
   Safety Controller
   ────────────────────────────────────────────── */
static const char* DetectQuantizationType(const char *modelName);

typedef enum {
    SAFETY_ALLOW,
    SAFETY_ALLOW_WITH_WARNINGS,
    SAFETY_REFUSE,
    SAFETY_ABORT,
    SAFETY_KILL
} SafetyDecision;

static void SafetyInit(void);
static void SafetyShutdown(void);
static BOOL KillProcessTree(DWORD pid);
static BOOL TerminateActiveProcess(void);
static ULONGLONG GetSystemRamMB(void);
static ULONGLONG GetAvailableRamMB(void);
static ULONGLONG GetCommitChargeMB(void);
static ULONGLONG GetTotalCommitLimitMB(void);
static ULONGLONG GetAvailableVRAMMB(void);
static int GetModelSizeMB(const char *modelName);
static int GetProjectorSizeMB(const char *projectorName);
static int EstimateContextMemoryMB(int ctxLen);
static SafetyDecision CheckLoadSafety(const char *modelName, const char *projectorName, int ctxLen, int gpuLayers);
static void PrintSafetyRefusal(const char *reason, const char *model, const char *suggestion);

/* ──────────────────────────────────────────────
   Safety Controller Implementation
   ────────────────────────────────────────────── */
static void SafetyInit(void)
{
    sActiveProcessHandle = NULL;
    sActiveProcessId = 0;
    sModelLoaded = FALSE;
    sLastSafetyReason[0] = '\0';
}

static void SafetyShutdown(void)
{
    TerminateActiveProcess();
    sModelLoaded = FALSE;
}

static BOOL KillProcessTree(DWORD pid)
{
    HANDLE hSnapshot;
    PROCESSENTRY32 pe;
    BOOL found;

    if (pid == 0)
        return TRUE;

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE)
        return FALSE;

    pe.dwSize = sizeof(pe);
    found = Process32First(hSnapshot, &pe);

    while (found) {
        if (pe.th32ParentProcessID == pid) {
            KillProcessTree(pe.th32ProcessID);
        }
        found = Process32Next(hSnapshot, &pe);
    }

    CloseHandle(hSnapshot);

    if (pid == GetCurrentProcessId())
        return TRUE;

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE) {
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnapshot, &pe)) {
            do {
                if (pe.th32ProcessID == pid) {
                    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                    if (hProcess) {
                        TerminateProcess(hProcess, 1);
                        CloseHandle(hProcess);
                    }
                    break;
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }

    return TRUE;
}

static BOOL TerminateActiveProcess(void)
{
    if (sActiveProcessId != 0) {
        KillProcessTree(sActiveProcessId);
        if (sActiveProcessHandle) {
            CloseHandle(sActiveProcessHandle);
            sActiveProcessHandle = NULL;
        }
        sActiveProcessId = 0;
    }
    return TRUE;
}

static ULONGLONG GetSystemRamMB(void)
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex))
        return statex.ullTotalPhys / (1024 * 1024);
    return 0;
}

static ULONGLONG GetAvailableRamMB(void)
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex))
        return statex.ullAvailPhys / (1024 * 1024);
    return 0;
}

static ULONGLONG GetCommitChargeMB(void)
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex))
        return statex.ullTotalPageFile / (1024 * 1024) - statex.ullAvailPageFile / (1024 * 1024);
    return 0;
}

static ULONGLONG GetTotalCommitLimitMB(void)
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex))
        return statex.ullTotalPageFile / (1024 * 1024);
    return 0;
}

static const IID IID_IDXGIFactory = {0x7b7166ec,0x21c7,0x4ae1,{0x9c,0xa1,0x23,0x78,0x69,0x3a,0x1e,0x5f}};

typedef struct IDXGIFactory IDXGIFactory;
typedef struct IDXGIAdapter IDXGIAdapter;

typedef struct DXGI_ADAPTER_DESC {
    UINT VendorId;
    UINT DeviceId;
    UINT SubSysId;
    UINT Revision;
    UINT AdapterLuid;
    SIZE_T DedicatedVideoMemory;
    SIZE_T DedicatedSystemMemory;
    SIZE_T SharedSystemMemory;
    DWORD Flags;
} DXGI_ADAPTER_DESC;

typedef struct IDXGIAdapterVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDXGIAdapter*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDXGIAdapter*);
    ULONG (STDMETHODCALLTYPE *Release)(IDXGIAdapter*);
    HRESULT (STDMETHODCALLTYPE *GetParent)(IDXGIAdapter*, REFIID, void**);
    HRESULT (STDMETHODCALLTYPE *GetDesc)(IDXGIAdapter*, DXGI_ADAPTER_DESC*);
} IDXGIAdapterVtbl;

struct IDXGIAdapter {
    IDXGIAdapterVtbl *lpVtbl;
};

typedef struct IDXGIFactoryVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDXGIFactory*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDXGIFactory*);
    ULONG (STDMETHODCALLTYPE *Release)(IDXGIFactory*);
    HRESULT (STDMETHODCALLTYPE *EnumAdapters)(IDXGIFactory*, UINT, IDXGIAdapter**);
} IDXGIFactoryVtbl;

struct IDXGIFactory {
    IDXGIFactoryVtbl *lpVtbl;
};

static ULONGLONG GetAvailableVRAMMB(void)
{
    ULONGLONG vramMB = 0;
    
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    if (dxgi) {
        typedef HRESULT(WINAPI *PFN_CREATE_DXGIFACTORY)(REFIID, void**);
        PFN_CREATE_DXGIFACTORY pfnCreateDXGIFactory = (PFN_CREATE_DXGIFACTORY)GetProcAddress(dxgi, "CreateDXGIFactory");
        if (pfnCreateDXGIFactory) {
            IDXGIFactory *pFactory = NULL;
            if (SUCCEEDED(pfnCreateDXGIFactory(&IID_IDXGIFactory, (void**)&pFactory))) {
                IDXGIAdapter *pAdapter = NULL;
                if (SUCCEEDED(pFactory->lpVtbl->EnumAdapters(pFactory, 0, &pAdapter))) {
                    DXGI_ADAPTER_DESC desc;
                    if (SUCCEEDED(pAdapter->lpVtbl->GetDesc(pAdapter, &desc))) {
                        vramMB = desc.DedicatedVideoMemory / (1024 * 1024);
                    }
                    pAdapter->lpVtbl->Release(pAdapter);
                }
                pFactory->lpVtbl->Release(pFactory);
            }
        }
        FreeLibrary(dxgi);
    }

    if (vramMB == 0) {
        ULONGLONG avail = GetAvailableRamMB();
        if (avail > 8192) return 8192;
        if (avail > 4096) return 4096;
        if (avail > 2048) return 2048;
        return 1024;
    }

    return vramMB;
}

static int GetModelSizeMB(const char *modelName)
{
    if (!modelName || !*modelName || !sFolder[0])
        return 0;

    char fullPath[MAX_PATH * 2];
    snprintf(fullPath, sizeof(fullPath), "%s\\%s", sFolder, modelName);

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(fullPath, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER size;
        size.LowPart = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        return (int)(size.QuadPart / (1024 * 1024));
    }
    return 0;
}

static int GetProjectorSizeMB(const char *projectorName)
{
    return GetModelSizeMB(projectorName);
}

static int EstimateContextMemoryMB(int ctxLen)
{
    int modelSizeMB = 0;
    if (sSelectedModel[0])
        modelSizeMB = GetModelSizeMB(sSelectedModel);

    double quantFactor = 0.45;
    const char *quant = DetectQuantizationType(sSelectedModel);
    if (strstr(quant, "Q8")) quantFactor = 0.95;
    else if (strstr(quant, "Q6")) quantFactor = 0.70;
    else if (strstr(quant, "Q5")) quantFactor = 0.55;
    else if (strstr(quant, "Q3")) quantFactor = 0.30;
    else if (strstr(quant, "F16")) quantFactor = 0.50;

    int estimatedWeightSize = (int)(modelSizeMB * quantFactor);
    int bytesPerToken = (int)(estimatedWeightSize * 0.001);

    return (ctxLen * bytesPerToken) / 2;
}

static SafetyDecision CheckLoadSafety(const char *modelName, const char *projectorName, int ctxLen, int gpuLayers)
{
    ULONGLONG totalRAM = GetSystemRamMB();
    ULONGLONG availVRAM = GetAvailableVRAMMB();

    int modelSizeMB = GetModelSizeMB(modelName);
    int projectorSizeMB = projectorName && projectorName[0] ? GetProjectorSizeMB(projectorName) : 0;
    int ctxMemoryMB = EstimateContextMemoryMB(ctxLen);

    int totalModelRAM = modelSizeMB + projectorSizeMB + ctxMemoryMB;

    if (projectorSizeMB > 0 && projectorSizeMB > 1000) {
        lstrcpynA(sLastSafetyReason, "PROJECTOR_TOO_HEAVY", sizeof(sLastSafetyReason));
        return SAFETY_REFUSE;
    }

    if (totalModelRAM > (int)(totalRAM * 0.95)) {
        lstrcpynA(sLastSafetyReason, "MODEL_TOO_LARGE", sizeof(sLastSafetyReason));
        return SAFETY_REFUSE;
    }

    return SAFETY_ALLOW;
}

static void PrintSafetyRefusal(const char *reason, const char *model, const char *suggestion)
{
    printf("\n");
    printf("\x1b[31m========================================\x1b[0m\n");
    printf("\x1b[31m  SAFETY CONTROLLER: LOAD REFUSED\x1b[0m\n");
    printf("\x1b[31m========================================\x1b[0m\n\n");
    printf("\x1b[33mReason: %s\x1b[0m\n\n", reason);
    printf("Model: %s\n\n", model ? model : "N/A");
    printf("%s\n\n", suggestion);
    printf("\x1b[90mThe device was protected from potential instability.\x1b[0m\n");
    printf("\n");
}

/* ──────────────────────────────────────────────
   Utility
   ────────────────────────────────────────────── */
static void SetCtlFont(HWND hWnd, HFONT hFont)
{
    if (hWnd) SendMessageA(hWnd, WM_SETFONT, (WPARAM)hFont, FALSE);
}

static BOOL HasConsoleHandle(HANDLE hConsole)
{
    DWORD mode = 0;
    return hConsole != NULL &&
           hConsole != INVALID_HANDLE_VALUE &&
           GetConsoleMode(hConsole, &mode) != 0;
}

static void EnableAnsiConsole(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;

    if (!HasConsoleHandle(hOut))
        return;

    mode |= ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;
    if (GetConsoleMode(hOut, &mode))
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

static void AttachConsoleStreams(BOOL allowAlloc)
{
    FILE *dummy;

    if (!HasConsoleHandle(GetStdHandle(STD_OUTPUT_HANDLE)) ||
        !HasConsoleHandle(GetStdHandle(STD_INPUT_HANDLE))) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            DWORD error = GetLastError();
            if (error != ERROR_ACCESS_DENIED) {
                if (!allowAlloc || !AllocConsole())
                    return;
            }
        }
    }

    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    freopen_s(&dummy, "CONIN$", "r", stdin);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    EnableAnsiConsole();
}

static void OpenOwnedConsole(const char *title)
{
    DWORD consoleMode;
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    BOOL hasConsole = (hInput != INVALID_HANDLE_VALUE) && 
                      (GetConsoleMode(hInput, &consoleMode) != 0);
    
    if (!hasConsole) {
        if (!AllocConsole()) {
            return;
        }
        if (title && *title) {
            SetConsoleTitleA(title);
        }
    }
    
    AttachConsoleStreams(TRUE);
}

static void HideStandaloneConsoleWindow(void)
{
    DWORD processList[4];
    DWORD processCount;
    HWND consoleHwnd;

    consoleHwnd = GetConsoleWindow();
    if (!consoleHwnd)
        return;

    processCount = GetConsoleProcessList(processList, (DWORD)(sizeof(processList) / sizeof(processList[0])));
    if (processCount <= 1)
        ShowWindow(consoleHwnd, SW_HIDE);
}

static BOOL FileExistsA_(const char *path)
{
    DWORD attrs;

    if (!path || !*path)
        return FALSE;

    attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL DirectoryExistsA_(const char *path)
{
    DWORD attrs;

    if (!path || !*path)
        return FALSE;

    attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static BOOL BuildConfigPath(char *configPath, int configPathLen)
{
    char appData[MAX_PATH];
    char dirPath[MAX_PATH];

    if (!configPath || configPathLen <= 0)
        return FALSE;

    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appData) != S_OK)
        return FALSE;

    snprintf(dirPath, sizeof(dirPath), "%s\\%s", appData, VALORA_APP_DIR);
    CreateDirectoryA(dirPath, NULL);
    snprintf(configPath, configPathLen, "%s\\%s", dirPath, VALORA_CONFIG_NAME);
    return TRUE;
}

static void SaveUiStateToGlobals(void)
{
    int idx;

    if (!hServerEdit || !hFolderEdit || !hModelCombo)
        return;

    GetWindowTextA(hServerEdit, sServer, sizeof(sServer));
    GetWindowTextA(hFolderEdit, sFolder, sizeof(sFolder));

    idx = (int)SendMessageA(hModelCombo, CB_GETCURSEL, 0, 0);
    if (idx != CB_ERR) {
        SendMessageA(hModelCombo, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)sSelectedModel);
    } else {
        sSelectedModel[0] = '\0';
    }
}

static BOOL SaveConfigToDisk(void)
{
    char ctx[32] = "2048";
    char gpu[32] = "-1";
    char port[32] = "8000";
    char threads[32] = "4";
    char serverType[16];
    char kvCacheBoth[32];

    if (!BuildConfigPath(sConfigPath, sizeof(sConfigPath)))
        return FALSE;

    SaveUiStateToGlobals();

    if (hCtxEdit) GetWindowTextA(hCtxEdit, ctx, sizeof(ctx));
    if (hGpuEdit) GetWindowTextA(hGpuEdit, gpu, sizeof(gpu));
    if (hPortEdit) GetWindowTextA(hPortEdit, port, sizeof(port));
    if (hThreadsEdit) GetWindowTextA(hThreadsEdit, threads, sizeof(threads));
    if (hServerTypeCombo)
        nServerType = (int)SendMessageA(hServerTypeCombo, CB_GETCURSEL, 0, 0);

    snprintf(serverType, sizeof(serverType), "%d", nServerType);

    /* Get KV cache K type from combo */
    char kvCacheK[16] = "f16";
    if (hKvCacheKCombo) {
        int sel = (int)SendMessageA(hKvCacheKCombo, CB_GETCURSEL, 0, 0);
        switch (sel) {
            case 0: lstrcpynA(kvCacheK, "f16", sizeof(kvCacheK)); break;
            case 1: lstrcpynA(kvCacheK, "q8_0", sizeof(kvCacheK)); break;
            case 2: lstrcpynA(kvCacheK, "q4_0", sizeof(kvCacheK)); break;
            case 3: lstrcpynA(kvCacheK, "q4_1", sizeof(kvCacheK)); break;
            default: lstrcpynA(kvCacheK, "f16", sizeof(kvCacheK)); break;
        }
    }

    /* Get KV cache V type from combo */
    char kvCacheV[16] = "f16";
    if (hKvCacheVCombo) {
        int sel = (int)SendMessageA(hKvCacheVCombo, CB_GETCURSEL, 0, 0);
        switch (sel) {
            case 0: lstrcpynA(kvCacheV, "f16", sizeof(kvCacheV)); break;
            case 1: lstrcpynA(kvCacheV, "q8_0", sizeof(kvCacheV)); break;
            case 2: lstrcpynA(kvCacheV, "q4_0", sizeof(kvCacheV)); break;
            case 3: lstrcpynA(kvCacheV, "q4_1", sizeof(kvCacheV)); break;
            default: lstrcpynA(kvCacheV, "f16", sizeof(kvCacheV)); break;
        }
    }

    snprintf(kvCacheBoth, sizeof(kvCacheBoth), "%s,%s", kvCacheK, kvCacheV);

    WritePrivateProfileStringA("paths", "server", sServer, sConfigPath);
    WritePrivateProfileStringA("paths", "models_folder", sFolder, sConfigPath);
    WritePrivateProfileStringA("paths", "default_model", sSelectedModel, sConfigPath);
    WritePrivateProfileStringA("paths", "llama_cpp_path", sLlamaCppPath, sConfigPath);
    WritePrivateProfileStringA("settings", "ctx", ctx, sConfigPath);
    WritePrivateProfileStringA("settings", "gpu", gpu, sConfigPath);
    WritePrivateProfileStringA("settings", "port", port, sConfigPath);
    WritePrivateProfileStringA("settings", "threads", threads, sConfigPath);
    WritePrivateProfileStringA("settings", "server_type", serverType, sConfigPath);
    WritePrivateProfileStringA("settings", "kv_cache_type", kvCacheBoth, sConfigPath);

    return TRUE;
}

static BOOL LoadConfigFromDisk(void)
{
    if (!BuildConfigPath(sConfigPath, sizeof(sConfigPath)))
        return FALSE;

    GetPrivateProfileStringA("paths", "server", "", sServer, sizeof(sServer), sConfigPath);
    GetPrivateProfileStringA("paths", "models_folder", "", sFolder, sizeof(sFolder), sConfigPath);
    GetPrivateProfileStringA("paths", "default_model", "", sSelectedModel, sizeof(sSelectedModel), sConfigPath);
    GetPrivateProfileStringA("paths", "llama_cpp_path", "", sLlamaCppPath, sizeof(sLlamaCppPath), sConfigPath);
    nServerType = GetPrivateProfileIntA("settings", "server_type", 0, sConfigPath);

    return sServer[0] != '\0' && sFolder[0] != '\0';
}

static void ApplyLoadedConfigToControls(void)
{
    char value[32];

    if (!hServerEdit || !hFolderEdit)
        return;

    SetWindowTextA(hServerEdit, sServer);
    SetWindowTextA(hFolderEdit, sFolder);

    GetPrivateProfileStringA("settings", "ctx", "2048", value, sizeof(value), sConfigPath);
    SetWindowTextA(hCtxEdit, value);

    GetPrivateProfileStringA("settings", "gpu", "-1", value, sizeof(value), sConfigPath);
    SetWindowTextA(hGpuEdit, value);

    GetPrivateProfileStringA("settings", "port", "8000", value, sizeof(value), sConfigPath);
    SetWindowTextA(hPortEdit, value);

    GetPrivateProfileStringA("settings", "threads", "4", value, sizeof(value), sConfigPath);
    SetWindowTextA(hThreadsEdit, value);
    
    /* Load KV cache type - format: "k_type,v_type" */
    GetPrivateProfileStringA("settings", "kv_cache_type", "f16,f16", value, sizeof(value), sConfigPath);
    
    char kvK[16] = "f16";
    char kvV[16] = "f16";
    char *comma = strchr(value, ',');
    if (comma) {
        *comma = '\0';
        lstrcpynA(kvK, value, sizeof(kvK));
        lstrcpynA(kvV, comma + 1, sizeof(kvV));
    } else {
        lstrcpynA(kvK, value, sizeof(kvK));
        lstrcpynA(kvV, value, sizeof(kvV));
    }
    
    /* Set KV cache K combo */
    if (lstrcmpiA(kvK, "q8_0") == 0) {
        SendMessageA(hKvCacheKCombo, CB_SETCURSEL, 1, 0);
    } else if (lstrcmpiA(kvK, "q4_0") == 0) {
        SendMessageA(hKvCacheKCombo, CB_SETCURSEL, 2, 0);
    } else if (lstrcmpiA(kvK, "q4_1") == 0) {
        SendMessageA(hKvCacheKCombo, CB_SETCURSEL, 3, 0);
    } else {
        SendMessageA(hKvCacheKCombo, CB_SETCURSEL, 0, 0);
    }
    
    /* Set KV cache V combo */
    if (lstrcmpiA(kvV, "q8_0") == 0) {
        SendMessageA(hKvCacheVCombo, CB_SETCURSEL, 1, 0);
    } else if (lstrcmpiA(kvV, "q4_0") == 0) {
        SendMessageA(hKvCacheVCombo, CB_SETCURSEL, 2, 0);
    } else if (lstrcmpiA(kvV, "q4_1") == 0) {
        SendMessageA(hKvCacheVCombo, CB_SETCURSEL, 3, 0);
    } else {
        SendMessageA(hKvCacheVCombo, CB_SETCURSEL, 0, 0);
    }

    SendMessageA(hServerTypeCombo, CB_SETCURSEL, (WPARAM)nServerType, 0);
}

static BOOL EnsureSelectedModelExists(void)
{
    int i;

    if (nModels <= 0)
        return FALSE;

    if (sSelectedModel[0]) {
        for (i = 0; i < nModels; ++i) {
            if (lstrcmpiA(sModels[i], sSelectedModel) == 0)
                return TRUE;
        }
    }

    lstrcpynA(sSelectedModel, sModels[0], sizeof(sSelectedModel));
    return TRUE;
}

static void BuildModelPath(char *dst, int dstLen, const char *modelName)
{
    if (!dst || dstLen <= 0) return;
    snprintf(dst, dstLen, "%s\\%s", sFolder, modelName);
}

static BOOL StringContainsI(const char *text, const char *needle)
{
    size_t needleLen;
    const char *p;

    if (!text || !needle || !*needle)
        return FALSE;

    needleLen = strlen(needle);
    for (p = text; *p; ++p) {
        if (_strnicmp(p, needle, needleLen) == 0)
            return TRUE;
    }

    return FALSE;
}

static BOOL NameContainsAnyI(const char *text, const char *const *needles, int count)
{
    int i;

    if (!text || !*text || !needles || count <= 0)
        return FALSE;

    for (i = 0; i < count; ++i) {
        if (needles[i] && StringContainsI(text, needles[i]))
            return TRUE;
    }

    return FALSE;
}

static BOOL IsProjectorFileName(const char *name)
{
    static const char *const strongKeywords[] = {
        "mmproj", "mm-proj", "projector", "vision_proj", "vision-proj", "image_proj"
    };

    return NameContainsAnyI(name, strongKeywords, (int)(sizeof(strongKeywords) / sizeof(strongKeywords[0])));
}

static BOOL IsProjectorCandidateFileName(const char *name)
{
    static const char *const candidateKeywords[] = {
        "mmproj", "mm-proj", "projector", "vision_proj", "vision-proj",
        "vision", "encoder", "clip", "vit"
    };

    return NameContainsAnyI(name, candidateKeywords, (int)(sizeof(candidateKeywords) / sizeof(candidateKeywords[0])));
}

static BOOL IsVisionSignalName(const char *modelName)
{
    static const char *const visionKeywords[] = {
        "llava", "vision", "multimodal", "qwen2-vl", "internvl",
        "minicpm-v", "minicpmv", "pixtral", "smolvlm",
        "-vl-", "_vl_", "-vl.", "_vl.", "-vl-", "vlm"
    };

    return NameContainsAnyI(modelName, visionKeywords, (int)(sizeof(visionKeywords) / sizeof(visionKeywords[0])));
}

static BOOL ExtractQuantizationTag(const char *name, char *quant, int quantLen)
{
    static const char *const tags[] = {
        "IQ1_M", "IQ1_S", "IQ2_M", "IQ2_S", "IQ3_M", "IQ3_S",
        "IQ4_NL", "IQ4_XS", "IQ4_M", "IQ4_S",
        "Q2_K", "Q3_K_S", "Q3_K_M", "Q3_K_L",
        "Q4_K_S", "Q4_K_M", "Q4_0", "Q4_1",
        "Q5_K_S", "Q5_K_M", "Q5_0", "Q5_1",
        "Q6_K", "Q6_0", "Q8_0", "BF16", "F16", "F32"
    };
    int i;

    if (!quant || quantLen <= 0)
        return FALSE;

    quant[0] = '\0';
    if (!name || !*name)
        return FALSE;

    for (i = 0; i < (int)(sizeof(tags) / sizeof(tags[0])); ++i) {
        if (StringContainsI(name, tags[i])) {
            lstrcpynA(quant, tags[i], quantLen);
            return TRUE;
        }
    }

    return FALSE;
}

static int GetKnownFamilyScore(const char *modelName, const char *candidateName)
{
    static const char *const families[] = {
        "llava", "qwen2-vl", "qwen", "internvl", "minicpm",
        "pixtral", "smolvlm", "gemma-3", "gemma", "phi", "vl"
    };
    int i;

    for (i = 0; i < (int)(sizeof(families) / sizeof(families[0])); ++i) {
        if (StringContainsI(modelName, families[i]) && StringContainsI(candidateName, families[i]))
            return 10;
    }

    return 0;
}

static ULONGLONG GetFileSize64(const WIN32_FIND_DATAA *fd)
{
    ULARGE_INTEGER value;

    if (!fd)
        return 0;

    value.LowPart = fd->nFileSizeLow;
    value.HighPart = fd->nFileSizeHigh;
    return value.QuadPart;
}

static const char *GetModelTypeLabel(const char *modelName)
{
    if (!modelName || !*modelName)
        return "Text";

    if (IsVisionSignalName(modelName)) {
        return "Vision";
    }

    if (StringContainsI(modelName, "audio") ||
        StringContainsI(modelName, "speech") ||
        StringContainsI(modelName, "whisper") ||
        StringContainsI(modelName, "asr") ||
        StringContainsI(modelName, "wav")) {
        return "Audio";
    }

    if (StringContainsI(modelName, "embed") ||
        StringContainsI(modelName, "embedding") ||
        StringContainsI(modelName, "bge") ||
        StringContainsI(modelName, "e5") ||
        StringContainsI(modelName, "gte") ||
        StringContainsI(modelName, "nomic-embed")) {
        return "Embedding";
    }

    if (StringContainsI(modelName, "rerank") ||
        StringContainsI(modelName, "reranker") ||
        StringContainsI(modelName, "ranker")) {
        return "Reranker";
    }

    return "Text";
}

static int ScoreProjectorCandidate(const char *modelName, const char *candidateName)
{
    char modelQuant[32];
    char candidateQuant[32];
    int score = 0;

    if (!candidateName || !*candidateName || !IsProjectorCandidateFileName(candidateName))
        return -1;

    score += GetKnownFamilyScore(modelName, candidateName);
    if (StringContainsI(candidateName, "mmproj")) score += 5;
    if (StringContainsI(candidateName, "projector")) score += 4;
    if (StringContainsI(candidateName, "clip") || StringContainsI(candidateName, "encoder")) score += 3;
    if (StringContainsI(candidateName, "vision")) score += 2;
    if (ExtractQuantizationTag(modelName, modelQuant, sizeof(modelQuant)) &&
        ExtractQuantizationTag(candidateName, candidateQuant, sizeof(candidateQuant)) &&
        lstrcmpiA(modelQuant, candidateQuant) == 0) score += 2;

    return score;
}

static BOOL FindMatchingProjector(const char *modelName, char *projectorName, int projectorNameLen)
{
    char pattern[MAX_PATH * 2];
    char modelBasePath[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE hFind;
    int bestScore = -1;
    ULONGLONG bestSize = 0;
    ULONGLONG fallbackSize = 0;
    char fallbackName[MAX_PATH] = "";
    const char *basePath;

    if (!projectorName || projectorNameLen <= 0) return FALSE;
    projectorName[0] = '\0';

    if (!sFolder[0] || !modelName || !*modelName)
        return FALSE;

    basePath = sFolder;
    if (strchr(modelName, '\\')) {
        const char *lastBackslash = strrchr(modelName, '\\');
        size_t baseLen = (size_t)(lastBackslash - modelName);
        if (baseLen > 0 && baseLen < sizeof(modelBasePath) - 1) {
            snprintf(modelBasePath, sizeof(modelBasePath), "%s\\%.*s", sFolder, (int)baseLen, modelName);
            basePath = modelBasePath;
        }
    }

    snprintf(pattern, sizeof(pattern), "%s\\*.gguf", basePath);
    hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return FALSE;

    do {
        int score;
        ULONGLONG fileSize;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (_stricmp(fd.cFileName, modelName) == 0)
            continue;

        fileSize = GetFileSize64(&fd);
        score = ScoreProjectorCandidate(modelName, fd.cFileName);
        if (score > bestScore || (score == bestScore && fileSize > bestSize)) {
            bestScore = score;
            bestSize = fileSize;
            lstrcpynA(projectorName, fd.cFileName, projectorNameLen);
        }

        if (fileSize > fallbackSize) {
            fallbackSize = fileSize;
            lstrcpynA(fallbackName, fd.cFileName, sizeof(fallbackName));
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

    if (bestScore >= 0 && projectorName[0] != '\0')
        return TRUE;

    if (fallbackName[0]) {
        lstrcpynA(projectorName, fallbackName, projectorNameLen);
        return TRUE;
    }

    return FALSE;
}

static int FindModelIndexByName(const char *modelName)
{
    int i;

    if (!modelName || !*modelName)
        return -1;

    for (i = 0; i < nModels; ++i) {
        if (lstrcmpiA(sModels[i], modelName) == 0)
            return i;
    }

    return -1;
}

static void GetConfiguredServerValues(char *ctx, int ctxLen, char *gpu, int gpuLen, char *port, int portLen, char *threads, int threadsLen, char *kvCache, int kvCacheLen)
{
    if (ctx && ctxLen > 0) GetPrivateProfileStringA("settings", "ctx", "2048", ctx, ctxLen, sConfigPath);
    if (gpu && gpuLen > 0) GetPrivateProfileStringA("settings", "gpu", "-1", gpu, gpuLen, sConfigPath);
    if (port && portLen > 0) GetPrivateProfileStringA("settings", "port", "8000", port, portLen, sConfigPath);
    if (threads && threadsLen > 0) GetPrivateProfileStringA("settings", "threads", "4", threads, threadsLen, sConfigPath);
    if (kvCache && kvCacheLen > 0) {
        char stored[64];
        GetPrivateProfileStringA("settings", "kv_cache_type", "f16,f16", stored, sizeof(stored), sConfigPath);
        char *comma = strchr(stored, ',');
        if (comma) {
            *comma = '\0';
            lstrcpynA(kvCache, stored, kvCacheLen);
        } else {
            lstrcpynA(kvCache, stored, kvCacheLen);
        }
    }
}

static void GetConfiguredServerValuesBoth(char *kvCacheK, int kvCacheKLen, char *kvCacheV, int kvCacheVLen)
{
    char stored[64];
    GetPrivateProfileStringA("settings", "kv_cache_type", "f16,f16", stored, sizeof(stored), sConfigPath);
    char *comma = strchr(stored, ',');
    if (comma) {
        *comma = '\0';
        if (kvCacheK && kvCacheKLen > 0) lstrcpynA(kvCacheK, stored, kvCacheKLen);
        if (kvCacheV && kvCacheVLen > 0) lstrcpynA(kvCacheV, comma + 1, kvCacheVLen);
    } else {
        if (kvCacheK && kvCacheKLen > 0) lstrcpynA(kvCacheK, stored, kvCacheKLen);
        if (kvCacheV && kvCacheVLen > 0) lstrcpynA(kvCacheV, stored, kvCacheVLen);
    }
}

/* Check if a model has saved configuration */
static BOOL HasModelConfig(const char *modelName)
{
    char section[MAX_PATH + 8];
    char value[32];

    if (!modelName || !*modelName)
        return FALSE;

    /* Create section name like [model_modelname.gguf] */
    snprintf(section, sizeof(section), "model_%s", modelName);

    /* Check if ctx value exists in model section */
    GetPrivateProfileStringA(section, "ctx", "", value, sizeof(value), sConfigPath);
    return value[0] != '\0';
}

/* Get per-model configuration, fall back to defaults if not set */
static void GetModelConfig(const char *modelName, char *ctx, int ctxLen, char *gpu, int gpuLen, char *threads, int threadsLen)
{
    char section[MAX_PATH + 8];

    if (!modelName || !*modelName) {
        /* Use global defaults */
        if (ctx && ctxLen > 0) snprintf(ctx, ctxLen, "2048");
        if (gpu && gpuLen > 0) snprintf(gpu, gpuLen, "-1");
        if (threads && threadsLen > 0) snprintf(threads, threadsLen, "4");
        return;
    }

    /* Create section name like [model_modelname.gguf] */
    snprintf(section, sizeof(section), "model_%s", modelName);

    /* Try to get from model-specific section */
    if (ctx && ctxLen > 0) GetPrivateProfileStringA(section, "ctx", "2048", ctx, ctxLen, sConfigPath);
    if (gpu && gpuLen > 0) GetPrivateProfileStringA(section, "gpu", "-1", gpu, gpuLen, sConfigPath);
    if (threads && threadsLen > 0) GetPrivateProfileStringA(section, "threads", "4", threads, threadsLen, sConfigPath);
}

/* Interactive model selector using console API */

/* Animation states for realtime feedback */
static const char* animationFrames[] = {"-", "\\", "|", "/"};
static int animFrame = 0;

/* Get GPU VRAM info using WMI - returns total bytes */
static ULONGLONG GetGpuMemoryBytes(void)
{
    FILE *fp;
    char output[512] = "";
    ULONGLONG bytes = 0;

    fp = _popen("powershell.exe -NoProfile -c \"(Get-CimInstance Win32_VideoController | Select -First 1).AdapterRAM\"", "r");
    if (fp) {
        if (fgets(output, sizeof(output), fp)) {
            char *p, *end;
            for (p = output; *p && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t'); p++);
            for (end = p; *end && (*end >= '0' && *end <= '9'); end++);
            *end = '\0';
            bytes = (ULONGLONG)atoll(p);
        }
        _pclose(fp);
    }

    return bytes;
}

static int GetTotalVRAM(void)
{
    ULONGLONG bytes = GetGpuMemoryBytes();
    return (int)(bytes / (1024 * 1024));
}

/* Get GPU name using WMI */
static void GetGpuName(char *buffer, int bufferLen)
{
    FILE *fp;
    char output[256] = "";

    if (!buffer || bufferLen <= 0) return;

    fp = _popen("powershell.exe -NoProfile -c \"(Get-CimInstance Win32_VideoController | Select -First 1).Name\"", "r");
    if (fp) {
        if (fgets(output, sizeof(output), fp)) {
            char *p, *end;
            for (p = output; *p && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t'); p++);
            for (end = p; *end && *end != '\r' && *end != '\n'; end++);
            *end = '\0';
            lstrcpynA(buffer, p, bufferLen);
        } else {
            lstrcpynA(buffer, "Unknown", bufferLen);
        }
        _pclose(fp);
    } else {
        lstrcpynA(buffer, "Unknown", bufferLen);
    }
}

/* Get model file size in MB */
static int GetModelFileSizeMB(const char *modelName)
{
    if (!modelName || !*modelName || !sFolder[0])
        return 0;

    char fullPath[MAX_PATH * 2];
    snprintf(fullPath, sizeof(fullPath), "%s\\%s", sFolder, modelName);

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(fullPath, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER fileSize;
        fileSize.LowPart = fad.nFileSizeLow;
        fileSize.HighPart = fad.nFileSizeHigh;
        return (int)(fileSize.QuadPart / (1024 * 1024));
    }
    return 0;
}

/* Detect quantization type from filename */
static const char* DetectQuantizationType(const char *modelName)
{
    if (!modelName) return "Unknown";
    
    /* Check for various quantization patterns */
    if (strstr(modelName, "IQ2_XXS") || strstr(modelName, "iq2_xxs")) return "IQ2-XXS";
    if (strstr(modelName, "IQ2_XS") || strstr(modelName, "iq2_xs")) return "IQ2-XS";
    if (strstr(modelName, "IQ2_S") || strstr(modelName, "iq2_s")) return "IQ2-S";
    if (strstr(modelName, "IQ2_M") || strstr(modelName, "iq2_m")) return "IQ2-M";
    if (strstr(modelName, "IQ3_XXS") || strstr(modelName, "iq3_xxs")) return "IQ3-XXS";
    if (strstr(modelName, "IQ1_S") || strstr(modelName, "iq1_s")) return "IQ1-S";
    if (strstr(modelName, "Q5_K_S") || strstr(modelName, "q5_k_s")) return "Q5-K_S";
    if (strstr(modelName, "Q5_K_M") || strstr(modelName, "q5_k_m")) return "Q5-K_M";
    if (strstr(modelName, "Q4_K_S") || strstr(modelName, "q4_k_s")) return "Q4-K_S";
    if (strstr(modelName, "Q4_K_M") || strstr(modelName, "q4_k_m")) return "Q4-K_M";
    if (strstr(modelName, "Q4_0") || strstr(modelName, "q4_0")) return "Q4_0";
    if (strstr(modelName, "Q4_1") || strstr(modelName, "q4_1")) return "Q4_1";
    if (strstr(modelName, "Q5_0") || strstr(modelName, "q5_0")) return "Q5_0";
    if (strstr(modelName, "Q5_1") || strstr(modelName, "q5_1")) return "Q5_1";
    if (strstr(modelName, "Q6_K") || strstr(modelName, "q6_k")) return "Q6_K";
    if (strstr(modelName, "Q8_0") || strstr(modelName, "q8_0")) return "Q8_0";
    if (strstr(modelName, "F16") || strstr(modelName, "f16")) return "F16";
    if (strstr(modelName, "F32") || strstr(modelName, "f32")) return "F32";
    if (strstr(modelName, "BF16") || strstr(modelName, "bf16")) return "BF16";
    
    return "Unknown";
}

/* Format file size for display */
static void FormatFileSize(int sizeMB, char *buffer, int bufferLen)
{
    if (!buffer || bufferLen <= 0) return;
    
    if (sizeMB >= 1024) {
        snprintf(buffer, bufferLen, "%.1f GB", sizeMB / 1024.0);
    } else if (sizeMB > 0) {
        snprintf(buffer, bufferLen, "%d MB", sizeMB);
    } else {
        snprintf(buffer, bufferLen, "Unknown");
    }
}

/* Check if model fits in available VRAM - returns TRUE if usable */
static BOOL IsModelUsable(const char *modelName, int vramMB)
{
    int modelSizeMB = GetModelFileSizeMB(modelName);
    if (modelSizeMB == 0) return FALSE;  /* Can't determine, assume not usable */
    
    /* Rough estimate: model needs ~35% of its size in VRAM for loading */
    int requiredVRAM = (int)(modelSizeMB * 0.35);
    return (vramMB >= requiredVRAM);
}

/* Print animation frame */
static void ClearConsole(void) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD written, size;
    COORD origin = {0, 0};
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) return;
    size = csbi.dwSize.X * csbi.dwSize.Y;
    FillConsoleOutputCharacterA(hConsole, ' ', size, origin, &written);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes, size, origin, &written);
    SetConsoleCursorPosition(hConsole, origin);
}

static void PrintAnimation(const char *message)
{
    printf("\r%s %s", animationFrames[animFrame], message);
    fflush(stdout);
    animFrame = (animFrame + 1) % 4;
}

static int GetSelectorPriority(const char *modelName, BOOL usable, int sizeMB)
{
    const char *quant;

    if (!usable)
        return 1000 + (sizeMB > 0 ? sizeMB : 999999);

    quant = DetectQuantizationType(modelName);
    if (strcmp(quant, "Q4-K_M") == 0) return 0;
    if (strcmp(quant, "Q4_0") == 0) return 1;
    if (strcmp(quant, "Q4_1") == 0) return 2;
    if (strcmp(quant, "Q5-K_M") == 0) return 3;
    if (strcmp(quant, "Q5-K_S") == 0) return 4;
    if (strcmp(quant, "Q5_0") == 0) return 5;
    if (strcmp(quant, "Q5_1") == 0) return 6;
    if (strcmp(quant, "Q6_K") == 0) return 7;
    if (strcmp(quant, "Q8_0") == 0) return 8;
    if (strcmp(quant, "F16") == 0) return 9;
    if (strcmp(quant, "BF16") == 0) return 10;
    if (strcmp(quant, "F32") == 0) return 11;
    return 12;
}

static int PickBestSelectorIndex(const int *modelSizes, const BOOL *usableFlags)
{
    int i;
    int bestIndex = 0;
    int bestPriority = 9999999;
    int bestSize = 9999999;

    for (i = 0; i < nModels; ++i) {
        int priority;
        int sizeValue;

        if (sSelectedModel[0] && lstrcmpiA(sModels[i], sSelectedModel) == 0)
            return i;

        priority = GetSelectorPriority(sModels[i], usableFlags[i], modelSizes[i]);
        sizeValue = (modelSizes[i] > 0) ? modelSizes[i] : 9999999;

        if (priority < bestPriority || (priority == bestPriority && sizeValue < bestSize)) {
            bestPriority = priority;
            bestSize = sizeValue;
            bestIndex = i;
        }
    }

    return bestIndex;
}

static void MoveCursorUp(int lines)
{
    if (lines > 0)
        printf("\x1b[%dA", lines);
}

static void MoveCursorDown(int lines)
{
    if (lines > 0)
        printf("\x1b[%dB", lines);
}

static void PrintTableHeader(void)
{
    printf("\n+----+-----------------------------------------------+------------+----------+------------+----------+\n");
    printf("| %2s | %-45s | %10s | %8s | %10s | %8s |\n", "#", "Model", "Size", "Quant", "Type", "Status");
    printf("+----+-----------------------------------------------+------------+----------+------------+----------+\n");
}

static void PrintTableRowAt(int rowIndex, int selectedIndex, const char *modelName, int sizeMB, const char *quant, const char *typeLabel, BOOL usable)
{
    char sizeStr[32];
    const char *status = usable ? "Ready" : "Low VRAM";
    BOOL isSelected = (rowIndex == selectedIndex);

    FormatFileSize(sizeMB, sizeStr, sizeof(sizeStr));
    printf("\x1b[2K\r");
    printf("| %2d | ", rowIndex + 1);
    if (isSelected) {
        printf("\x1b[46;30m%-45.45s\x1b[0m", modelName);
    } else {
        printf("%-45.45s", modelName);
    }
    printf(" | %10s | %8.8s | %10.10s | ", sizeStr, quant, typeLabel);
    if (usable) {
        printf("\x1b[32m%8s\x1b[0m", status);
    } else {
        printf("\x1b[33m%8s\x1b[0m", status);
    }
    printf(" |\n");
}

static void PrintTableFooter(void)
{
    printf("+----+-----------------------------------------------+------------+----------+------------+----------+\n");
}

static void RefreshSelectorRow(int rowIndex, int selectedIndex, const int *modelSizes, const BOOL *usableFlags, int totalRows)
{
    int linesUp;
    const char *quant;

    linesUp = totalRows - rowIndex + 1;
    MoveCursorUp(linesUp);
    quant = DetectQuantizationType(sModels[rowIndex]);
    PrintTableRowAt(rowIndex, selectedIndex, sModels[rowIndex], modelSizes[rowIndex], quant, sModelTypes[rowIndex], usableFlags[rowIndex]);
    MoveCursorDown(linesUp - 1);
    fflush(stdout);
}

static int RunInteractiveModelSelector(char *selectedModel, int selectedModelLen)
{
    int selectedIndex = 0;
    int previousIndex = 0;
    int i;
    int ch;
    DWORD mode;
    HANDLE hConsole;
    int availableVRAM;
    
    /* Array to store model sizes */
    static int modelSizes[MAX_MODELS];
    static BOOL usableFlags[MAX_MODELS];
    
    if (!selectedModel || selectedModelLen <= 0)
        return -1;

    /* Scan models first with animation */
    printf("\n");
    PrintAnimation("Scanning models...");
    ScanModels();
    
    /* Get VRAM info */
    availableVRAM = GetTotalVRAM();
    
    /* Calculate sizes for all models */
    for (i = 0; i < nModels; i++) {
        modelSizes[i] = GetModelFileSizeMB(sModels[i]);
        usableFlags[i] = IsModelUsable(sModels[i], availableVRAM);
    }
    
    /* Clear animation line */
    printf("\r                     \r");
    
    if (nModels == 0) {
        fprintf(stderr, "No .gguf models found in %s\n", sFolder);
        return -1;
    }

    /* Get console handle */
    hConsole = GetStdHandle(STD_INPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE)
        return -1;

    /* Save current console mode */
    GetConsoleMode(hConsole, &mode);
    /* Disable mouse input and window resizing */
    SetConsoleMode(hConsole, mode & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));

    /* Initial display */
    printf("\n=== \x1b[36mValora Model Selector\x1b[0m ===\n\n");
    printf("\x1b[90mUse UP/DOWN arrow keys to navigate, ENTER to select, ESC to cancel\x1b[0m\n");
    printf("\x1b[90mAvailable VRAM: %d MB\x1b[0m\n\n", availableVRAM);

    selectedIndex = PickBestSelectorIndex(modelSizes, usableFlags);
    previousIndex = selectedIndex;

    /* Draw initial table */
    PrintTableHeader();
    for (i = 0; i < nModels; ++i) {
        const char *quant = DetectQuantizationType(sModels[i]);
        PrintTableRowAt(i, selectedIndex, sModels[i], modelSizes[i], quant, sModelTypes[i], usableFlags[i]);
    }
    PrintTableFooter();
    
    /* Input loop */
    while (1) {
        ch = _getch();
        if (ch == 224) { /* Arrow key prefix */
            ch = _getch();
            if (ch == 72) { /* Up arrow */
                previousIndex = selectedIndex;
                selectedIndex = (selectedIndex > 0) ? (selectedIndex - 1) : (nModels - 1);
                RefreshSelectorRow(previousIndex, selectedIndex, modelSizes, usableFlags, nModels);
                RefreshSelectorRow(selectedIndex, selectedIndex, modelSizes, usableFlags, nModels);
            } else if (ch == 80) { /* Down arrow */
                previousIndex = selectedIndex;
                selectedIndex = (selectedIndex < nModels - 1) ? (selectedIndex + 1) : 0;
                RefreshSelectorRow(previousIndex, selectedIndex, modelSizes, usableFlags, nModels);
                RefreshSelectorRow(selectedIndex, selectedIndex, modelSizes, usableFlags, nModels);
            }
        } else if (ch == 13) { /* Enter */
            lstrcpynA(selectedModel, sModels[selectedIndex], selectedModelLen);
            /* Restore console mode */
            SetConsoleMode(hConsole, mode);
            printf("\n\x1b[32mSelected: %s\x1b[0m\n", selectedModel);
            return selectedIndex;
        } else if (ch == 27) { /* Escape */
            /* Restore console mode */
            SetConsoleMode(hConsole, mode);
            return -1;
        }
    }
}

static void BuildServeCommand(char *dst, int dstLen, const char *customCtx, int customGpu, BOOL includeLocalIp)
{
    char modelPath[MAX_PATH * 2];
    char ctx[32], gpu[32], port[32], threads[32], kvCacheK[16], kvCacheV[16];
    char host[128];

    BuildModelPath(modelPath, sizeof(modelPath), sSelectedModel);
    GetConfiguredServerValues(ctx, sizeof(ctx), gpu, sizeof(gpu), port, sizeof(port), threads, sizeof(threads), kvCacheK, sizeof(kvCacheK));
    GetConfiguredServerValuesBoth(kvCacheK, sizeof(kvCacheK), kvCacheV, sizeof(kvCacheV));

    if (customCtx && customCtx[0])
        lstrcpynA(ctx, customCtx, sizeof(ctx));
    if (customGpu >= 0)
        snprintf(gpu, sizeof(gpu), "%d", customGpu);

    if (includeLocalIp) {
        lstrcpynA(host, "0.0.0.0", sizeof(host));
    } else {
        lstrcpynA(host, (nServerType == 1) ? "0.0.0.0" : "127.0.0.1", sizeof(host));
    }

    snprintf(
        dst, dstLen,
        "\"%s\" -m \"%s\" -c %s -ngl %s --port %s -t %s --host %s --cache-type-k %s --cache-type-v %s",
        sServer, modelPath, ctx, gpu, port, threads, host, kvCacheK, kvCacheV
    );
}

static void BuildRunCommand(char *dst, int dstLen, const char *modelName)
{
    char cliPath[MAX_PATH];
    char modelPath[MAX_PATH * 2];
    char projectorPath[MAX_PATH * 2];
    char ctx[32], gpu[32], threads[32], kvCacheK[16] = "f16", kvCacheV[16] = "f16";
    int modelIndex;
    const char *targetModel;

    GetCliPathFromServerPath(sServer, cliPath, sizeof(cliPath));
    targetModel = modelName ? modelName : sSelectedModel;
    BuildModelPath(modelPath, sizeof(modelPath), targetModel);
    GetModelConfig(targetModel, ctx, sizeof(ctx), gpu, sizeof(gpu), threads, sizeof(threads));
    
    GetConfiguredServerValuesBoth(kvCacheK, sizeof(kvCacheK), kvCacheV, sizeof(kvCacheV));
    
    if (sCustomCtx[0])
        lstrcpynA(ctx, sCustomCtx, sizeof(ctx));

    modelIndex = FindModelIndexByName(targetModel);

    if (modelIndex >= 0 &&
        _stricmp(sModelTypes[modelIndex], "Vision") == 0 &&
        sModelProjectors[modelIndex][0]) {
        if (strchr(targetModel, '\\')) {
            const char *lastBackslash = strrchr(targetModel, '\\');
            char fullProjectorPath[MAX_PATH];
            snprintf(fullProjectorPath, sizeof(fullProjectorPath), "%.*s\\%s",
                (int)(lastBackslash - targetModel), targetModel, sModelProjectors[modelIndex]);
            BuildModelPath(projectorPath, sizeof(projectorPath), fullProjectorPath);
        } else {
            BuildModelPath(projectorPath, sizeof(projectorPath), sModelProjectors[modelIndex]);
        }
        snprintf(
            dst, dstLen,
            "\"%s\" -m \"%s\" --mmproj \"%s\" -c %s -ngl %s -t %s --cache-type-k %s --cache-type-v %s",
            cliPath, modelPath, projectorPath, ctx, gpu, threads, kvCacheK, kvCacheV
        );
    } else {
        snprintf(
            dst, dstLen,
            "\"%s\" -m \"%s\" -c %s -ngl %s -t %s --cache-type-k %s --cache-type-v %s",
            cliPath, modelPath, ctx, gpu, threads, kvCacheK, kvCacheV
        );
    }
}

static int RunChildProcess(const char *commandLine)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char mutableCmd[MAX_CMD];
    DWORD exitCode = 1;

    if (!commandLine || !*commandLine)
        return 1;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    lstrcpynA(mutableCmd, commandLine, sizeof(mutableCmd));

    if (!CreateProcessA(NULL, mutableCmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Failed to start command.\n");
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exitCode;
}

static BOOL NormalizeHuggingFaceRepo(const char *input, char *repo, int repoLen)
{
    const char *start;
    char temp[512];
    char *slash1, *slash2, *end;

    if (!input || !*input || !repo || repoLen <= 0)
        return FALSE;

    while (*input == ' ' || *input == '\t')
        input++;

    start = strstr(input, "huggingface.co/");
    if (start) {
        start += lstrlenA("huggingface.co/");
    } else {
        start = input;
    }

    while (*start == '/')
        start++;

    lstrcpynA(temp, start, sizeof(temp));

    end = temp;
    while (*end) {
        if (*end == '?' || *end == '#') {
            *end = '\0';
            break;
        }
        end++;
    }

    slash1 = strchr(temp, '/');
    if (!slash1)
        return FALSE;

    slash2 = strchr(slash1 + 1, '/');
    if (slash2)
        *slash2 = '\0';

    if (!temp[0] || !slash1[1])
        return FALSE;

    lstrcpynA(repo, temp, repoLen);
    return TRUE;
}

static BOOL WriteHuggingFaceDownloaderScript(char *scriptPath, int scriptPathLen)
{
    char tempPath[MAX_PATH];
    FILE *fp;

    if (!scriptPath || scriptPathLen <= 0)
        return FALSE;

    if (!GetTempPathA(sizeof(tempPath), tempPath))
        return FALSE;

    snprintf(scriptPath, scriptPathLen, "%svalora_hf_get.ps1", tempPath);

    fp = fopen(scriptPath, "w");
    if (!fp)
        return FALSE;

    fputs("param([string]$Repo,[string]$ModelsFolder)\n", fp);
    fputs("$ErrorActionPreference = 'Stop'\n", fp);
    fputs("$ProgressPreference = 'SilentlyContinue'\n", fp);
    fputs("\n", fp);
    fputs("function Format-Bytes([Int64]$Bytes) {\n", fp);
    fputs("  if ($Bytes -le 0) { return 'Unknown size' }\n", fp);
    fputs("  if ($Bytes -ge 1GB) { return ('{0:N2} GB' -f ($Bytes / 1GB)) }\n", fp);
    fputs("  if ($Bytes -ge 1MB) { return ('{0:N0} MB' -f ($Bytes / 1MB)) }\n", fp);
    fputs("  return ('{0:N0} KB' -f ($Bytes / 1KB))\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Get-Quant([string]$Name) {\n", fp);
    fputs("  $patterns = @('IQ\\d+_[A-Z]+','Q\\d+_K_[A-Z]+','Q\\d+_[A-Z0-9]+','Q\\d+','BF16','F16','F32')\n", fp);
    fputs("  foreach ($pattern in $patterns) {\n", fp);
    fputs("    $match = [regex]::Match($Name.ToUpperInvariant(), $pattern)\n", fp);
    fputs("    if ($match.Success) { return $match.Value }\n", fp);
    fputs("  }\n", fp);
    fputs("  return 'Unknown'\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Test-Contains([string]$Text, [string]$Needle) {\n", fp);
    fputs("  if ([string]::IsNullOrWhiteSpace($Text) -or [string]::IsNullOrWhiteSpace($Needle)) { return $false }\n", fp);
    fputs("  return $Text.IndexOf($Needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Is-ProjectorFile([string]$Name) {\n", fp);
    fputs("  return (Test-Contains $Name 'mmproj') -or (Test-Contains $Name 'mm-proj') -or (Test-Contains $Name 'projector') -or (Test-Contains $Name 'vision_proj') -or (Test-Contains $Name 'vision-proj') -or (Test-Contains $Name 'image_proj')\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Is-ProjectorCandidate([string]$Name) {\n", fp);
    fputs("  return (Is-ProjectorFile $Name) -or (Test-Contains $Name 'vision') -or (Test-Contains $Name 'encoder') -or (Test-Contains $Name 'clip') -or (Test-Contains $Name 'vit')\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Test-VisionName([string]$Name) {\n", fp);
    fputs("  return (Test-Contains $Name 'vision') -or (Test-Contains $Name 'llava') -or (Test-Contains $Name 'vlm') -or (Test-Contains $Name '-vl-') -or (Test-Contains $Name '_vl_') -or (Test-Contains $Name '-vl.') -or (Test-Contains $Name '_vl.') -or (Test-Contains $Name 'minicpm-v') -or (Test-Contains $Name 'minicpmv') -or (Test-Contains $Name 'smolvlm') -or (Test-Contains $Name 'qwen2-vl') -or (Test-Contains $Name 'internvl') -or (Test-Contains $Name 'pixtral') -or (Test-Contains $Name 'gemma-3') -or (Test-Contains $Name 'multimodal')\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Get-ModelType([string]$Name) {\n", fp);
    fputs("  if (Test-VisionName $Name) { return 'Vision' }\n", fp);
    fputs("  if ((Test-Contains $Name 'audio') -or (Test-Contains $Name 'speech') -or (Test-Contains $Name 'whisper') -or (Test-Contains $Name 'asr') -or (Test-Contains $Name 'wav')) { return 'Audio' }\n", fp);
    fputs("  if ((Test-Contains $Name 'embed') -or (Test-Contains $Name 'embedding') -or (Test-Contains $Name 'bge') -or (Test-Contains $Name 'e5') -or (Test-Contains $Name 'gte') -or (Test-Contains $Name 'nomic-embed')) { return 'Embedding' }\n", fp);
    fputs("  if ((Test-Contains $Name 'rerank') -or (Test-Contains $Name 'reranker') -or (Test-Contains $Name 'ranker')) { return 'Reranker' }\n", fp);
    fputs("  return 'Text'\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Get-RepoSignalFlags($RepoInfo, [string]$Repo) {\n", fp);
    fputs("  $parts = @()\n", fp);
    fputs("  if ($RepoInfo.tags) { $parts += ($RepoInfo.tags -join ' ') }\n", fp);
    fputs("  if ($RepoInfo.pipeline_tag) { $parts += [string]$RepoInfo.pipeline_tag }\n", fp);
    fputs("  if ($RepoInfo.cardData) { try { $parts += ($RepoInfo.cardData | ConvertTo-Json -Depth 10 -Compress) } catch {} }\n", fp);
    fputs("  if ($RepoInfo.config) { try { $parts += ($RepoInfo.config | ConvertTo-Json -Depth 10 -Compress) } catch {} }\n", fp);
    fputs("  try { $parts += (Invoke-WebRequest -Uri ('https://huggingface.co/' + $Repo + '/resolve/main/config.json') -UseBasicParsing -TimeoutSec 20).Content } catch {}\n", fp);
    fputs("  try { $parts += (Invoke-WebRequest -Uri ('https://huggingface.co/' + $Repo + '/resolve/main/README.md') -UseBasicParsing -TimeoutSec 20).Content } catch {}\n", fp);
    fputs("  $text = ($parts | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join \"`n\"\n", fp);
    fputs("  return [PSCustomObject]@{ Vision = (Test-Contains $text 'vision'); Image = (Test-Contains $text 'image'); Multimodal = (Test-Contains $text 'multimodal'); Clip = (Test-Contains $text 'clip') }\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Get-DetectedModelType([string]$Name, $RepoSignals, [bool]$HasStructureSignal) {\n", fp);
    fputs("  $baseType = Get-ModelType $Name\n", fp);
    fputs("  if ($baseType -ne 'Text') { return $baseType }\n", fp);
    fputs("  if ($RepoSignals.Vision -or $RepoSignals.Image -or $RepoSignals.Multimodal -or $RepoSignals.Clip -or $HasStructureSignal) { return 'Vision' }\n", fp);
    fputs("  return 'Text'\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Get-ProjectorScore([string]$ModelName, [string]$CandidateName) {\n", fp);
    fputs("  if (-not (Is-ProjectorCandidate $CandidateName)) { return -1 }\n", fp);
    fputs("  $score = 0\n", fp);
    fputs("  if ((Test-Contains $ModelName 'llava') -and (Test-Contains $CandidateName 'llava')) { $score += 10 }\n", fp);
    fputs("  if ((Test-Contains $ModelName 'minicpm') -and (Test-Contains $CandidateName 'minicpm')) { $score += 10 }\n", fp);
    fputs("  if ((Test-Contains $ModelName 'smolvlm') -and (Test-Contains $CandidateName 'smolvlm')) { $score += 10 }\n", fp);
    fputs("  if ((Test-Contains $ModelName 'qwen') -and (Test-Contains $CandidateName 'qwen')) { $score += 10 }\n", fp);
    fputs("  if ((Test-Contains $ModelName 'internvl') -and (Test-Contains $CandidateName 'internvl')) { $score += 10 }\n", fp);
    fputs("  if (((Test-Contains $ModelName '-vl-') -or (Test-Contains $ModelName '_vl_')) -and ((Test-Contains $CandidateName '-vl-') -or (Test-Contains $CandidateName '_vl_'))) { $score += 10 }\n", fp);
    fputs("  if ((Test-Contains $ModelName 'pixtral') -and (Test-Contains $CandidateName 'pixtral')) { $score += 10 }\n", fp);
    fputs("  if ((Test-Contains $ModelName 'gemma') -and (Test-Contains $CandidateName 'gemma')) { $score += 10 }\n", fp);
    fputs("  if (Test-Contains $CandidateName 'mmproj') { $score += 5 }\n", fp);
    fputs("  if (Test-Contains $CandidateName 'projector') { $score += 4 }\n", fp);
    fputs("  if ((Test-Contains $CandidateName 'clip') -or (Test-Contains $CandidateName 'encoder')) { $score += 3 }\n", fp);
    fputs("  if (((Get-Quant $ModelName) -ne 'Unknown') -and ((Get-Quant $ModelName) -eq (Get-Quant $CandidateName))) { $score += 2 }\n", fp);
    fputs("  return $score\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Find-MatchingProjector($Selected, $Projectors, $AllGgufFiles) {\n", fp);
    fputs("  $best = $null\n", fp);
    fputs("  $bestScore = -1\n", fp);
    fputs("  foreach ($candidate in $Projectors) {\n", fp);
    fputs("    $candidateName = if ($candidate.PSObject.Properties['rfilename']) { $candidate.rfilename } else { $candidate.Name }\n", fp);
    fputs("    $score = Get-ProjectorScore $Selected.Name $candidateName\n", fp);
    fputs("    if ($score -gt $bestScore) { $bestScore = $score; $best = $candidate }\n", fp);
    fputs("  }\n", fp);
    fputs("  if ($best) { return $best }\n", fp);
    fputs("  $fallback = $AllGgufFiles | Where-Object { $_.Name -ine $Selected.Name } | Sort-Object Size -Descending | Select-Object -First 1\n", fp);
    fputs("  return $fallback\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Trim-Cell([string]$Text, [int]$Width) {\n", fp);
    fputs("  if (-not $Text) { return ''.PadRight($Width) }\n", fp);
    fputs("  if ($Text.Length -le $Width) { return $Text.PadRight($Width) }\n", fp);
    fputs("  if ($Width -le 3) { return $Text.Substring(0, $Width) }\n", fp);
    fputs("  return ($Text.Substring(0, $Width - 3) + '...')\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Show-Table($Items, [string]$RecommendedName) {\n", fp);
    fputs("  $line = '+' + ('-' * 4) + '+' + ('-' * 40) + '+' + ('-' * 12) + '+' + ('-' * 12) + '+' + ('-' * 12) + '+' + ('-' * 12) + '+' + ('-' * 15) + '+'\n", fp);
    fputs("  Write-Host $line\n", fp);
    fputs("  Write-Host ('| ' + (Trim-Cell 'No' 2) + ' | ' + (Trim-Cell 'Model' 38) + ' | ' + (Trim-Cell 'Size' 10) + ' | ' + (Trim-Cell 'Quant' 10) + ' | ' + (Trim-Cell 'Type' 10) + ' | ' + (Trim-Cell 'GPU Fit' 10) + ' | ' + (Trim-Cell 'Recommended' 13) + ' |')\n", fp);
    fputs("  Write-Host $line\n", fp);
    fputs("  for ($i = 0; $i -lt $Items.Count; $i++) {\n", fp);
    fputs("    $item = $Items[$i]\n", fp);
    fputs("    $recommended = if ($item.Name -eq $RecommendedName) { 'YES' } else { '' }\n", fp);
    fputs("    $row = '| ' + (Trim-Cell ([string]($i + 1)) 2) + ' | ' + (Trim-Cell $item.Name 38) + ' | ' + (Trim-Cell (Format-Bytes $item.Size) 10) + ' | ' + (Trim-Cell $item.Quant 10) + ' | ' + (Trim-Cell $item.Type 10) + ' | ' + (Trim-Cell $item.Gpu 10) + ' | ' + (Trim-Cell $recommended 13) + ' |'\n", fp);
    fputs("    if ($item.Name -eq $RecommendedName) { Write-Host $row -ForegroundColor Green } else { Write-Host $row }\n", fp);
    fputs("  }\n", fp);
    fputs("  Write-Host $line\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Render-DownloadBar([string]$Label, [Int64]$Done, [Int64]$Total, [datetime]$Started) {\n", fp);
    fputs("  $width = 42\n", fp);
    fputs("  $elapsed = [Math]::Max(([datetime]::UtcNow - $Started).TotalSeconds, 0.1)\n", fp);
    fputs("  $speed = $Done / $elapsed\n", fp);
    fputs("  if ($Total -gt 0) {\n", fp);
    fputs("    $percent = [Math]::Min(100, [Math]::Max(0, [int](($Done * 100) / $Total)))\n", fp);
    fputs("    $filled = [Math]::Min($width, [int](($Done * $width) / $Total))\n", fp);
    fputs("    $eta = [TimeSpan]::FromSeconds([int](($Total - $Done) / [Math]::Max($speed, 1)))\n", fp);
    fputs("  } else {\n", fp);
    fputs("    $percent = 0\n", fp);
    fputs("    $filled = 0\n", fp);
    fputs("    $eta = [TimeSpan]::FromSeconds(0)\n", fp);
    fputs("  }\n", fp);
    fputs("  $bar = ('=' * $filled) + (' ' * ($width - $filled))\n", fp);
    fputs("  $left = (Trim-Cell $Label 18)\n", fp);
    fputs("  $etaText = $eta.ToString('hh\\:mm\\:ss')\n", fp);
    fputs("  $line = ('{0} {1,3}% [{2}] {3,8} {4,8}/s ETA {5}' -f $left, $percent, $bar, (Format-Bytes $Total), (Format-Bytes ([Int64]$speed)), $etaText)\n", fp);
    fputs("  [Console]::Write(\"`r\" + $line.PadRight(120))\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Download-WithWebRequest([string]$Url, [string]$Path, [string]$Label) {\n", fp);
    fputs("  $request = [System.Net.HttpWebRequest]::Create($Url)\n", fp);
    fputs("  $request.AllowAutoRedirect = $true\n", fp);
    fputs("  $request.Timeout = [int][TimeSpan]::FromMinutes(30).TotalMilliseconds\n", fp);
    fputs("  $request.ReadWriteTimeout = [int][TimeSpan]::FromHours(12).TotalMilliseconds\n", fp);
    fputs("  $request.UserAgent = 'valora/1.0'\n", fp);
    fputs("  $response = $request.GetResponse()\n", fp);
    fputs("  $total = [Int64]$response.ContentLength\n", fp);
    fputs("  $stream = $response.GetResponseStream()\n", fp);
    fputs("  $file = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)\n", fp);
    fputs("  $buffer = New-Object byte[] 262144\n", fp);
    fputs("  $done = [Int64]0\n", fp);
    fputs("  $started = [datetime]::UtcNow\n", fp);
    fputs("  try {\n", fp);
    fputs("    while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {\n", fp);
    fputs("      $file.Write($buffer, 0, $read)\n", fp);
    fputs("      $done += $read\n", fp);
    fputs("      Render-DownloadBar $Label $done $total $started\n", fp);
    fputs("    }\n", fp);
    fputs("    if ($total -gt 0) { Render-DownloadBar $Label $total $total $started }\n", fp);
    fputs("    [Console]::WriteLine('')\n", fp);
    fputs("  } finally {\n", fp);
    fputs("    $file.Dispose()\n", fp);
    fputs("    $stream.Dispose()\n", fp);
    fputs("    $response.Dispose()\n", fp);
    fputs("  }\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Download-WithProgress([string]$Url, [string]$Path) {\n", fp);
    fputs("  try {\n", fp);
    fputs("    $label = 'pulling ' + [System.IO.Path]::GetFileName($Path)\n", fp);
    fputs("    try { Add-Type -AssemblyName System.Net.Http -ErrorAction Stop | Out-Null } catch {}\n", fp);
    fputs("    try {\n", fp);
    fputs("      $handler = New-Object System.Net.Http.HttpClientHandler\n", fp);
    fputs("      $handler.AllowAutoRedirect = $true\n", fp);
    fputs("      $client = New-Object System.Net.Http.HttpClient($handler)\n", fp);
    fputs("      $client.Timeout = [TimeSpan]::FromHours(12)\n", fp);
    fputs("      $response = $client.GetAsync($Url, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()\n", fp);
    fputs("      [void]$response.EnsureSuccessStatusCode()\n", fp);
    fputs("      $total = 0\n", fp);
    fputs("      if ($response.Content.Headers.ContentLength) { $total = [Int64]$response.Content.Headers.ContentLength }\n", fp);
    fputs("      $stream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()\n", fp);
    fputs("      $file = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)\n", fp);
    fputs("      $buffer = New-Object byte[] 262144\n", fp);
    fputs("      $done = [Int64]0\n", fp);
    fputs("      $started = [datetime]::UtcNow\n", fp);
    fputs("      try {\n", fp);
    fputs("        while (($read = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {\n", fp);
    fputs("          $file.Write($buffer, 0, $read)\n", fp);
    fputs("          $done += $read\n", fp);
    fputs("          Render-DownloadBar $label $done $total $started\n", fp);
    fputs("        }\n", fp);
    fputs("        if ($total -gt 0) { Render-DownloadBar $label $total $total $started }\n", fp);
    fputs("        [Console]::WriteLine('')\n", fp);
    fputs("      } finally {\n", fp);
    fputs("        $file.Dispose()\n", fp);
    fputs("        $stream.Dispose()\n", fp);
    fputs("        $response.Dispose()\n", fp);
    fputs("        $client.Dispose()\n", fp);
    fputs("        $handler.Dispose()\n", fp);
    fputs("      }\n", fp);
    fputs("    } catch [System.Management.Automation.RuntimeException] {\n", fp);
    fputs("      Download-WithWebRequest $Url $Path $label\n", fp);
    fputs("    }\n", fp);
    fputs("  } catch {\n", fp);
    fputs("    Write-Host \"Download failed: $_\" -ForegroundColor Red\n", fp);
    fputs("    if (Test-Path $Path) { Remove-Item $Path -Force }\n", fp);
    fputs("    exit 1\n", fp);
    fputs("  }\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Get-GpuOffload([Int64]$Bytes) {\n", fp);
    fputs("  try {\n", fp);
    fputs("    $gpuRam = (Get-CimInstance Win32_VideoController | Where-Object { $_.AdapterRAM -gt 0 } | Measure-Object -Maximum AdapterRAM).Maximum\n", fp);
    fputs("    if (-not $gpuRam) { return 'Unknown' }\n", fp);
    fputs("    $threshold = [Math]::Max(2GB, [Int64]($Bytes * 0.35))\n", fp);
    fputs("    if ($gpuRam -ge $threshold) { return 'Yes' }\n", fp);
    fputs("    return 'No'\n", fp);
    fputs("  } catch { return 'Unknown' }\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Get-DefaultModel($Files) {\n", fp);
    fputs("  $preferred = $Files | Sort-Object @{ Expression = {\n", fp);
    fputs("    $name = $_.Name.ToLowerInvariant()\n", fp);
    fputs("    if ($name -match 'q4_k_m') { return 0 }\n", fp);
    fputs("    if ($name -match 'q4_0') { return 1 }\n", fp);
    fputs("    if ($name -match 'q5') { return 2 }\n", fp);
    fputs("    if ($name -match 'q6') { return 3 }\n", fp);
    fputs("    if ($name -match 'q8') { return 4 }\n", fp);
    fputs("    if ($name -match 'f16|bf16') { return 5 }\n", fp);
    fputs("    return 6\n", fp);
    fputs("  } }, @{ Expression = { $_.Size }; Descending = $false }, Name\n", fp);
    fputs("  return $preferred[0]\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("if (-not (Test-Path -LiteralPath $ModelsFolder)) { New-Item -ItemType Directory -Path $ModelsFolder | Out-Null }\n", fp);
    fputs("Write-Host ''\n", fp);
    fputs("Write-Host ('Checking Hugging Face repo: ' + $Repo) -ForegroundColor Cyan\n", fp);
    fputs("Write-Host 'Fetching repository metadata...' -ForegroundColor DarkGray\n", fp);
    fputs("$repoUrl = 'https://huggingface.co/api/models/' + $Repo\n", fp);
    fputs("$repoInfo = Invoke-RestMethod -Uri $repoUrl\n", fp);
    fputs("$repoSignals = Get-RepoSignalFlags $repoInfo $Repo\n", fp);
    fputs("$allGgufFiles = @($repoInfo.siblings | Where-Object { $_.rfilename -match '\\.gguf$' })\n", fp);
    fputs("if ($allGgufFiles.Count -eq 0) { Write-Host 'No GGUF files found in this repository.' -ForegroundColor Yellow; exit 1 }\n", fp);
    fputs("$projectorFiles = @($allGgufFiles | Where-Object { Is-ProjectorCandidate $_.rfilename })\n", fp);
    fputs("$ggufFiles = @($allGgufFiles | Where-Object { -not (Is-ProjectorFile $_.rfilename) })\n", fp);
    fputs("if ($ggufFiles.Count -eq 0) { $ggufFiles = $allGgufFiles }\n", fp);
    fputs("$hasStructureSignal = ($ggufFiles.Count -gt 0 -and $projectorFiles.Count -gt 0)\n", fp);
    fputs("$files = @()\n", fp);
    fputs("foreach ($entry in $ggufFiles) {\n", fp);
    fputs("  $name = $entry.rfilename\n", fp);
    fputs("  $escapedName = [System.Uri]::EscapeDataString($name) -replace '%2F', '/'\n", fp);
    fputs("  $downloadUrl = 'https://huggingface.co/' + $Repo + '/resolve/main/' + $escapedName + '?download=true'\n", fp);
    fputs("  Write-Host ('Inspecting ' + $name + ' ...') -ForegroundColor DarkGray\n", fp);
    fputs("  $size = 0\n", fp);
    fputs("  try {\n", fp);
    fputs("    $head = Invoke-WebRequest -Uri $downloadUrl -Method Head -UseBasicParsing\n", fp);
    fputs("    if ($head.Headers['Content-Length']) { $size = [Int64]$head.Headers['Content-Length'] }\n", fp);
    fputs("  } catch {}\n", fp);
    fputs("  $files += [PSCustomObject]@{ Name = $name; Url = $downloadUrl; Size = $size; Quant = (Get-Quant $name); Type = (Get-DetectedModelType $name $repoSignals $hasStructureSignal); Gpu = (Get-GpuOffload $size) }\n", fp);
    fputs("}\n", fp);
    fputs("$files = @($files | Sort-Object Name)\n", fp);
    fputs("$defaultModel = Get-DefaultModel $files\n", fp);
    fputs("$selected = $defaultModel\n", fp);
    fputs("$selectedProjector = $null\n", fp);
    fputs("\n", fp);
    fputs("if ($files.Count -gt 1) {\n", fp);
    fputs("  Write-Host ('Found ' + $files.Count + ' GGUF models in this repo.') -ForegroundColor Green\n", fp);
    fputs("  Write-Host ('Recommended: ' + $defaultModel.Name + ' | ' + (Format-Bytes $defaultModel.Size) + ' | ' + $defaultModel.Quant + ' | ' + $defaultModel.Type + ' | GPU fit: ' + $defaultModel.Gpu)\n", fp);
    fputs("  Write-Host ''\n", fp);
    fputs("  Write-Host '1. Continue with default model'\n", fp);
    fputs("  Write-Host '2. Change model'\n", fp);
    fputs("  Write-Host '3. Cancel'\n", fp);
    fputs("  do { $choice = Read-Host 'Choose 1, 2 or 3' } until ($choice -match '^[123]$')\n", fp);
    fputs("  if ($choice -eq '3') { Write-Host 'Cancelled.'; exit 0 }\n", fp);
    fputs("  if ($choice -eq '2') {\n", fp);
    fputs("    Write-Host ''\n", fp);
    fputs("    Show-Table $files $defaultModel.Name\n", fp);
    fputs("    do { $pick = Read-Host 'Choose model number' } until ($pick -match '^\\d+$' -and [int]$pick -ge 1 -and [int]$pick -le $files.Count)\n", fp);
    fputs("    $selected = $files[[int]$pick - 1]\n", fp);
    fputs("  }\n", fp);
    fputs("} else {\n", fp);
    fputs("  Write-Host ('One GGUF model found: ' + $selected.Name + ' | ' + (Format-Bytes $selected.Size) + ' | ' + $selected.Quant + ' | ' + $selected.Type + ' | GPU fit: ' + $selected.Gpu) -ForegroundColor Green\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("Write-Host ('Detection summary: metadata=' + ($repoSignals.Vision -or $repoSignals.Image -or $repoSignals.Multimodal -or $repoSignals.Clip) + ', structure=' + $hasStructureSignal + ', selected-type=' + $selected.Type) -ForegroundColor DarkCyan\n", fp);
    fputs("if ($selected.Type -eq 'Vision' -and $projectorFiles.Count -gt 0) {\n", fp);
    fputs("  $selectedProjector = Find-MatchingProjector $selected $projectorFiles $files\n", fp);
    fputs("  if ($selectedProjector) {\n", fp);
    fputs("    $matchedProjectorName = if ($selectedProjector.PSObject.Properties['rfilename']) { $selectedProjector.rfilename } else { $selectedProjector.Name }\n", fp);
    fputs("    Write-Host ('Matched projector: ' + $matchedProjectorName) -ForegroundColor DarkCyan\n", fp);
    fputs("  }\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("$targetPath = Join-Path $ModelsFolder ([System.IO.Path]::GetFileName($selected.Name))\n", fp);
    fputs("# Create org/model subfolder structure\n", fp);
    fputs("$repoParts = $Repo -split '/'\n", fp);
    fputs("$orgName = $repoParts[0]\n", fp);
    fputs("$modelName = $repoParts[1]\n", fp);
    fputs("$orgFolder = Join-Path $ModelsFolder $orgName\n", fp);
    fputs("$modelFolder = Join-Path $orgFolder $modelName\n", fp);
    fputs("if (-not (Test-Path -LiteralPath $orgFolder)) { New-Item -ItemType Directory -Path $orgFolder | Out-Null }\n", fp);
    fputs("if (-not (Test-Path -LiteralPath $modelFolder)) { New-Item -ItemType Directory -Path $modelFolder | Out-Null }\n", fp);
    fputs("$targetPath = Join-Path $modelFolder ([System.IO.Path]::GetFileName($selected.Name))\n", fp);
    fputs("if (Test-Path -LiteralPath $targetPath) {\n", fp);
    fputs("  Write-Host 'File already exists, overwriting...' -ForegroundColor Yellow\n", fp);
    fputs("}\n", fp);
    fputs("$projectorTargetPath = $null\n", fp);
    fputs("$overwriteProjector = 'Y'\n", fp);
    fputs("if ($selected.Type -eq 'Vision' -and -not $selectedProjector) {\n", fp);
    fputs("  Write-Host 'WARNING: Vision model detected but no projector match was found.' -ForegroundColor Yellow\n", fp);
    fputs("  Write-Host '1. Continue without projector'\n", fp);
    fputs("  Write-Host '2. Try fallback selection'\n", fp);
    fputs("  Write-Host '3. Abort'\n", fp);
    fputs("  do { $missingProjectorChoice = Read-Host 'Choose 1, 2 or 3' } until ($missingProjectorChoice -match '^[123]$')\n", fp);
    fputs("  if ($missingProjectorChoice -eq '3') { Write-Host 'Aborted.' -ForegroundColor Yellow; exit 1 }\n", fp);
    fputs("  if ($missingProjectorChoice -eq '2') {\n", fp);
    fputs("    $selectedProjector = ($files | Where-Object { $_.Name -ine $selected.Name } | Sort-Object Size -Descending | Select-Object -First 1)\n", fp);
    fputs("    if ($selectedProjector) { Write-Host ('Fallback projector: ' + $selectedProjector.Name) -ForegroundColor Yellow }\n", fp);
    fputs("  }\n", fp);
    fputs("}\n", fp);
    fputs("if ($selectedProjector) {\n", fp);
    fputs("  $projectorName = if ($selectedProjector.PSObject.Properties['rfilename']) { $selectedProjector.rfilename } else { $selectedProjector.Name }\n", fp);
    fputs("  $projectorTargetPath = Join-Path $modelFolder ([System.IO.Path]::GetFileName($projectorName))\n", fp);
    fputs("  if (Test-Path -LiteralPath $projectorTargetPath) {\n", fp);
    fputs("    Write-Host 'Projector file already exists, overwriting...' -ForegroundColor Yellow\n", fp);
    fputs("  }\n", fp);
    fputs("}\n", fp);
    fputs("Write-Host ''\n", fp);
    fputs("Write-Host ('Downloading ' + $selected.Name) -ForegroundColor Cyan\n", fp);
    fputs("Write-Host ('Saving to ' + $targetPath)\n", fp);
    fputs("Write-Host 'Download progress:' -ForegroundColor DarkGray\n", fp);
    fputs("Download-WithProgress $selected.Url $targetPath\n", fp);
    fputs("if ($selectedProjector -and $projectorTargetPath) {\n", fp);
    fputs("  $projectorName = if ($selectedProjector.PSObject.Properties['rfilename']) { $selectedProjector.rfilename } else { $selectedProjector.Name }\n", fp);
    fputs("  $escapedProjectorName = [System.Uri]::EscapeDataString($projectorName) -replace '%2F', '/'\n", fp);
    fputs("  $projectorUrl = 'https://huggingface.co/' + $Repo + '/resolve/main/' + $escapedProjectorName + '?download=true'\n", fp);
    fputs("  Write-Host ''\n", fp);
    fputs("  Write-Host ('Downloading companion projector ' + $projectorName) -ForegroundColor Cyan\n", fp);
    fputs("  Write-Host ('Saving to ' + $projectorTargetPath)\n", fp);
    fputs("  Write-Host 'Download progress:' -ForegroundColor DarkGray\n", fp);
    fputs("  Download-WithProgress $projectorUrl $projectorTargetPath\n", fp);
    fputs("}\n", fp);
    fputs("Write-Host ''\n", fp);
    fputs("Write-Host ('Installed model to ' + $targetPath) -ForegroundColor Green\n", fp);
    fputs("if ($selectedProjector -and $projectorTargetPath) { Write-Host ('Installed projector to ' + $projectorTargetPath) -ForegroundColor Green }\n", fp);
    fputs("$suggestedThreads = if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 4 }\n", fp);
    fputs("$suggestedCtx = 4096\n", fp);
    fputs("$suggestedGpuLayers = if ($selected.Gpu -eq 'No') { 0 } elseif ($selected.Gpu -eq 'Yes') { 999 } else { 40 }\n", fp);
    fputs("$readyCommand = 'llama-cli -m \"' + $targetPath + '\"'\n", fp);
    fputs("if ($selectedProjector -and $projectorTargetPath) { $readyCommand += ' --mmproj \"' + $projectorTargetPath + '\"' }\n", fp);
    fputs("$readyCommand += ' -c ' + $suggestedCtx + ' -ngl ' + $suggestedGpuLayers + ' -t ' + $suggestedThreads\n", fp);
    fputs("Write-Host ('Ready command: ' + $readyCommand) -ForegroundColor Green\n", fp);

    fclose(fp);
    return TRUE;
}

static int DownloadFromHuggingFaceLegacy(const char *hfUrl)
{
    char repo[256];
    char scriptPath[MAX_PATH];
    char command[MAX_CMD];
    int startTime;
    
    /* Animation state */
    int animCounter = 0;
    
    if (!EnsureModelsFolderReady())
        return 1;

    if (!NormalizeHuggingFaceRepo(hfUrl, repo, sizeof(repo))) {
        fprintf(stderr, "Invalid Hugging Face repo. Use owner/repo or a huggingface.co URL.\n");
        return 1;
    }

    if (!WriteHuggingFaceDownloaderScript(scriptPath, sizeof(scriptPath))) {
        fprintf(stderr, "Failed to prepare the Hugging Face downloader script.\n");
        return 1;
    }

    printf("\n\x1b[36m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\x1b[0m\n");
    printf("\x1b[36m  Downloading model from Hugging Face\x1b[0m\n");
    printf("\x1b[36m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\x1b[0m\n\n");
    printf("\x1b[90mRepository:\x1b[0m %s\n", repo);
    printf("\x1b[90mDestination:\x1b[0m %s\n\n", sFolder);
    
    /* Show animation while preparing */
    printf("\x1b[33m");
    for (int i = 0; i < 3; i++) {
        printf("\r%s Preparing download...", animationFrames[animCounter]);
        fflush(stdout);
        Sleep(100);
        animCounter = (animCounter + 1) % 10;
    }
    printf("\x1b[0m\n\n");
    
    printf("\x1b[90mStarting download (this may take a while depending on model size)...\x1b[0m\n\n");
    
    startTime = GetTickCount();
    
    snprintf(
        command, sizeof(command),
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -Repo \"%s\" -ModelsFolder \"%s\"",
        scriptPath, repo, sFolder
    );

    /* Run with animation showing progress */
    printf("\x1b[90m[");
    fflush(stdout);
    
    int result = RunChildProcess(command);
    
    /* Complete animation */
    printf("\x1b[90m]\x1b[0m\n");
    
    int elapsed = (GetTickCount() - startTime) / 1000;
    if (result == 0) {
        printf("\n\x1b[32m✓ Download completed successfully!\x1b[0m\n");
        printf("\x1b[90mTime taken: %d seconds\x1b[0m\n", elapsed);
        
        /* Rescan models after download */
        printf("\n\x1b[90mScanning new models...");
        ScanModels();
        printf(" \x1b[32mFound %d model(s)\x1b[0m\n", nModels);
    } else {
        printf("\n\x1b[31m✗ Download failed with exit code %d\x1b[0m\n", result);
        printf("\x1b[90mTime taken: %d seconds\x1b[0m\n", elapsed);
    }
    
    return result;
}

static int DownloadFromHuggingFace(const char *hfUrl)
{
    char repo[256];
    char scriptPath[MAX_PATH];
    char command[MAX_CMD];
    int startTime;
    int animCounter = 0;
    int i;
    int result;
    int elapsed;

    if (!EnsureModelsFolderReady())
        return 1;

    if (!NormalizeHuggingFaceRepo(hfUrl, repo, sizeof(repo))) {
        fprintf(stderr, "Invalid Hugging Face repo. Use owner/repo or a huggingface.co URL.\n");
        return 1;
    }

    if (!WriteHuggingFaceDownloaderScript(scriptPath, sizeof(scriptPath))) {
        fprintf(stderr, "Failed to prepare the Hugging Face downloader script.\n");
        return 1;
    }

    printf("\n================================================================\n");
    printf("  Downloading model from Hugging Face\n");
    printf("================================================================\n\n");
    printf("Repository:  %s\n", repo);
    printf("Destination: %s\n\n", sFolder);

    for (i = 0; i < 3; i++) {
        printf("\r%s Preparing download...", animationFrames[animCounter]);
        fflush(stdout);
        Sleep(100);
        animCounter = (animCounter + 1) % 10;
    }
    printf("\r[ok] Preparing download...          \n\n");
    printf("Starting download (this may take a while depending on model size)...\n\n");

    startTime = GetTickCount();

    snprintf(
        command, sizeof(command),
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\" -Repo \"%s\" -ModelsFolder \"%s\"",
        scriptPath, repo, sFolder
    );

    result = RunChildProcess(command);
    elapsed = (GetTickCount() - startTime) / 1000;

    if (result == 0) {
        printf("\n[ok] Download completed successfully!\n");
        printf("Time taken: %d seconds\n", elapsed);

        printf("\nScanning new models...");
        ScanModels();
        printf(" Found %d model(s)\n", nModels);
    } else {
        printf("\n[error] Download failed with exit code %d\n", result);
        printf("Time taken: %d seconds\n", elapsed);
    }

    return result;
}

static int PrintModels(void)
{
    int i;

    if (!DirectoryExistsA_(sFolder)) {
        fprintf(stderr, "Configured models folder does not exist: %s\n", sFolder);
        return 1;
    }

    ScanModels();
    if (nModels == 0) {
        fprintf(stderr, "No .gguf models found in %s\n", sFolder);
        return 1;
    }

    EnsureSelectedModelExists();
    printf("Configured models in %s\n", sFolder);
    for (i = 0; i < nModels; ++i) {
        printf("%s %-45s  [%s]", lstrcmpiA(sModels[i], sSelectedModel) == 0 ? "*" : "-", sModels[i], sModelTypes[i]);
        if (_stricmp(sModelTypes[i], "Vision") == 0 && sModelProjectors[i][0]) {
            printf("  projector=%s", sModelProjectors[i]);
        }
        printf("\n");
    }
    fflush(stdout);
    return 0;
}

/* ──────────────────────────────────────────────
   llama.cpp Setup Implementation
   ────────────────────────────────────────────── */

static OSType DetectOS(void)
{
#ifdef _WIN32
    return OS_WINDOWS;
#elif __linux__
    return OS_LINUX;
#elif __APPLE__
    return OS_MAC;
#else
    return OS_UNKNOWN;
#endif
}

static const char* GetOSName(OSType os)
{
    switch (os) {
        case OS_WINDOWS: return "Windows";
        case OS_LINUX:   return "Linux";
        case OS_MAC:    return "macOS";
        default:       return "Unknown";
    }
}

static const char* GetOSFilterPattern(OSType os)
{
    switch (os) {
        case OS_WINDOWS: return "win";
        case OS_LINUX:   return "linux";
        case OS_MAC:    return "macos";
        default:       return "";
    }
}

static int FetchLlamaCppReleases(LlamaCppBuild **builds, int *count, const char *buildPattern)
{
    char command[MAX_CMD];
    char tempFile[MAX_PATH];
    char *jsonBuffer = NULL;
    DWORD jsonSize = 0;
    HANDLE hFile;
    DWORD bytesRead;
    int buildIdx = 0;
    char *nameStart, *nameEnd;
    char *browserUrlStart, *browserUrlEnd;
    char *sizeStart, *sizeEnd;
    char *p;
    LlamaCppBuild *tempBuilds = NULL;
    int capacity = 16;
    OSType detectedOS;
    const char *osFilter;
    
    *builds = NULL;
    *count = 0;
    
    detectedOS = DetectOS();
    osFilter = GetOSFilterPattern(detectedOS);
    
    if (!osFilter[0]) {
        fprintf(stderr, "Unsupported operating system.\n");
        return -1;
    }
    
    tempBuilds = (LlamaCppBuild*)malloc(sizeof(LlamaCppBuild) * capacity);
    if (!tempBuilds) {
        fprintf(stderr, "Out of memory.\n");
        return -1;
    }
    
    /* Create temp file for JSON response */
    GetTempFileNameA(".", "valora_releases_", 0, tempFile);
    DeleteFileA(tempFile);
    lstrcatA(tempFile, ".json");
    
    /* Fetch JSON from GitHub API */
    snprintf(command, sizeof(command),
        "powershell.exe -NoProfile -Command \"$r = Invoke-RestMethod -Uri 'https://api.github.com/repos/ggerganov/llama.cpp/releases/latest' -Headers @{'Accept'='application/vnd.github+json'}; $r | ConvertTo-Json -Depth 10 | Out-File -FilePath '%s' -Encoding utf8\"",
        tempFile);
    
    if (RunChildProcess(command) != 0) {
        fprintf(stderr, "Failed to fetch releases from GitHub.\n");
        free(tempBuilds);
        DeleteFileA(tempFile);
        return -1;
    }
    
    /* Read JSON file */
    hFile = CreateFileA(tempFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to read release data.\n");
        free(tempBuilds);
        DeleteFileA(tempFile);
        return -1;
    }
    
    jsonSize = GetFileSize(hFile, NULL);
    if (jsonSize == 0 || jsonSize > 1024*1024) {
        CloseHandle(hFile);
        free(tempBuilds);
        DeleteFileA(tempFile);
        fprintf(stderr, "Invalid release data.\n");
        return -1;
    }
    
    jsonBuffer = (char*)malloc(jsonSize + 1);
    if (!jsonBuffer) {
        CloseHandle(hFile);
        free(tempBuilds);
        DeleteFileA(tempFile);
        fprintf(stderr, "Out of memory.\n");
        return -1;
    }
    
    ReadFile(hFile, jsonBuffer, jsonSize, &bytesRead, NULL);
    jsonBuffer[bytesRead] = '\0';
    CloseHandle(hFile);
    DeleteFileA(tempFile);
    
    /* Parse JSON - find assets array and extract relevant builds */
    p = jsonBuffer;
    
    while (*p) {
        /* Look for \"name\" field in an asset - handle both formats */
        nameStart = strstr(p, "\"name\":");
        if (!nameStart) break;
        nameStart = strchr(nameStart + 7, '\"');
        if (!nameStart) break;
        nameStart++;
        nameEnd = strstr(nameStart, "\"");
        if (!nameEnd) break;
        
        sizeStart = strstr(nameEnd, "\"size\":");
        if (!sizeStart) { p = nameEnd + 1; continue; }
        sizeStart += 6;
        while (*sizeStart == ' ' || *sizeStart == '\t') sizeStart++;
        if (*sizeStart == ':') sizeStart++;
        while (*sizeStart == ' ' || *sizeStart == '\t') sizeStart++;
        sizeEnd = sizeStart;
        while (*sizeEnd != ',' && *sizeEnd != '}' && *sizeEnd != '\0') sizeEnd++;
        
        browserUrlStart = strstr(sizeStart, "\"browser_download_url\":");
        if (!browserUrlStart) break;
        browserUrlStart = strchr(browserUrlStart + 22, '\"');
        if (!browserUrlStart) break;
        browserUrlStart++;
        browserUrlEnd = strstr(browserUrlStart, "\"");
        if (!browserUrlEnd) break;
        
        /* Extract and validate build name */
        {
            int nameLen = (int)(nameEnd - nameStart);
            char buildName[256];
            if (nameLen >= sizeof(buildName)) nameLen = sizeof(buildName) - 1;
            memcpy(buildName, nameStart, nameLen);
            buildName[nameLen] = '\0';
            
            /* Check if this build matches our OS */
            if (strstr(buildName, osFilter) != NULL) {
                
                /* If buildPattern specified, filter by it (for updates) */
                if (buildPattern && !strstr(buildName, buildPattern)) {
                    p = browserUrlEnd + 1;
                    continue;
                }
                
                /* Check if it's a binary archive (not source) */
                BOOL isValid = (
                    strstr(buildName, "bin-") != NULL ||
                    strstr(buildName, "-bin") != NULL
                ) && (
                    strstr(buildName, ".zip") != NULL ||
                    strstr(buildName, ".tar") != NULL ||
                    strstr(buildName, ".tgz") != NULL
                );
                
                if (isValid && buildIdx < capacity) {
                    /* Extract URL */
                    int urlLen = (int)(browserUrlEnd - browserUrlStart);
                    if (urlLen >= sizeof(tempBuilds[buildIdx].downloadUrl)) {
                        urlLen = sizeof(tempBuilds[buildIdx].downloadUrl) - 1;
                    }
                    memcpy(tempBuilds[buildIdx].downloadUrl, browserUrlStart, urlLen);
                    tempBuilds[buildIdx].downloadUrl[urlLen] = '\0';
                    
                    /* Extract name */
                    lstrcpynA(tempBuilds[buildIdx].name, buildName, sizeof(tempBuilds[buildIdx].name));
                    
                    /* Extract size */
                    {
                        int sizeLen = (int)(sizeEnd - sizeStart);
                        char sizeStr[32];
                        if (sizeLen >= sizeof(sizeStr)) sizeLen = sizeof(sizeStr) - 1;
                        memcpy(sizeStr, sizeStart, sizeLen);
                        sizeStr[sizeLen] = '\0';
                        long long sizeBytes = (long long)strtod(sizeStr, NULL);
                        tempBuilds[buildIdx].size = (int)(sizeBytes / (1024*1024));
                    }
                    
                    /* Determine if CPU or Vulkan */
                    tempBuilds[buildIdx].isVulkan = (
                        strstr(buildName, "vulkan") != NULL ||
                        strstr(buildName, "cuda") != NULL
                    );
                    tempBuilds[buildIdx].isCpu = !tempBuilds[buildIdx].isVulkan;
                    tempBuilds[buildIdx].os = detectedOS;
                    
                    buildIdx++;
                }
            }
        }
        
        p = browserUrlEnd + 1;
    }
    
    free(jsonBuffer);
    
    if (buildIdx == 0) {
        free(tempBuilds);
        fprintf(stderr, "No builds found for %s.\n", GetOSName(detectedOS));
        return -1;
    }
    
    *builds = tempBuilds;
    *count = buildIdx;
    
    return 0;
}

static void FreeLlamaCppBuilds(LlamaCppBuild *builds, int count)
{
    if (builds) free(builds);
}

static void PrintBuildMenu(int selectedIndex, const LlamaCppBuild *builds, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        const char *type = builds[i].isVulkan ? "GPU" : "CPU";
        const char *marker = (i == selectedIndex) ? ">" : " ";
        
        printf("%s [%-4s] %-50s  (%d MB)\n",
            marker, type, builds[i].name, builds[i].size);
    }
}

static int SelectBuildMenu(LlamaCppBuild *builds, int count)
{
    int selectedIndex = 0;
    int previousIndex = 0;
    int ch;
    DWORD mode;
    HANDLE hConsole;
    COORD menuOrigin;
    
    if (count <= 0) return -1;
    
    /* Get console handles */
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hInput == INVALID_HANDLE_VALUE || hOutput == INVALID_HANDLE_VALUE) return -1;
    
    /* Save and modify console mode */
    GetConsoleMode(hInput, &mode);
    SetConsoleMode(hInput, mode & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
    
    printf("\n=== Select llama.cpp Build ===\n\n");
    printf("\x1b[90mUse UP/DOWN arrow keys to navigate, ENTER to select, ESC to cancel\x1b[0m\n\n");
    
    PrintBuildMenu(selectedIndex, builds, count);
    
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hOutput, &csbi);
        menuOrigin = csbi.dwCursorPosition;
        menuOrigin.Y -= count;
    }
    
    /* Input loop */
    while (1) {
        ch = _getch();
        if (ch == 224) {  /* Arrow key prefix */
            ch = _getch();
            if (ch == 72) {  /* Up arrow */
                previousIndex = selectedIndex;
                selectedIndex = (selectedIndex > 0) ? selectedIndex - 1 : count - 1;
            } else if (ch == 80) {  /* Down arrow */
                previousIndex = selectedIndex;
                selectedIndex = (selectedIndex < count - 1) ? selectedIndex + 1 : 0;
            }
            
            if (previousIndex != selectedIndex) {
                SetConsoleCursorPosition(hOutput, menuOrigin);
                PrintBuildMenu(selectedIndex, builds, count);
            }
        } else if (ch == 13) {  /* Enter */
            SetConsoleMode(hInput, mode);
            return selectedIndex;
        } else if (ch == 27) {  /* Escape */
            SetConsoleMode(hInput, mode);
            return -1;
        }
    }
}

static int DownloadBuildWithProgress(const char *url, const char *destPath)
{
    char scriptPath[MAX_PATH];
    char tempPath[MAX_PATH];
    FILE *fp;
    char command[MAX_CMD];
    int result;

    if (!GetTempPathA(sizeof(tempPath), tempPath)) {
        fprintf(stderr, "Failed to get temp path.\n");
        return 1;
    }

    snprintf(scriptPath, sizeof(scriptPath), "%svalora_llama_dl.ps1", tempPath);

    fp = fopen(scriptPath, "w");
    if (!fp) {
        fprintf(stderr, "Failed to create download script.\n");
        return 1;
    }

    fputs("$ErrorActionPreference = 'Stop'\n", fp);
    fputs("$ProgressPreference = 'SilentlyContinue'\n", fp);
    fprintf(fp, "$url  = '%s'\n", url);
    fprintf(fp, "$dest = '%s'\n", destPath);
    fputs("\n", fp);
    fputs("try {\n", fp);
    fputs("  if (Test-Path $dest) { Remove-Item $dest -Force }\n", fp);
    fputs("\n", fp);
    fputs("  $head = Invoke-WebRequest -Uri $url -Method Head -UseBasicParsing\n", fp);
    fputs("  $totalBytes = [Int64]$head.Headers['Content-Length']\n", fp);
    fputs("  if ($totalBytes -le 0) { throw 'Could not determine file size' }\n", fp);
    fputs("  $totalMB = [Math]::Floor($totalBytes / 1MB)\n", fp);
    fputs("  Write-Host \"Downloading $totalMB MB...\"\n", fp);
    fputs("\n", fp);
    fputs("  Add-Type -AssemblyName System.Net.Http\n", fp);
    fputs("  $client = [System.Net.Http.HttpClient]::new()\n", fp);
    fputs("  $client.DefaultRequestHeaders.Add('User-Agent', 'Valora/1.0')\n", fp);
    fputs("  $resp = $client.GetAsync($url, [System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()\n", fp);
    fputs("  $resp.EnsureSuccessStatusCode()\n", fp);
    fputs("  $netStream  = $resp.Content.ReadAsStreamAsync().GetAwaiter().GetResult()\n", fp);
    fputs("  $fileStream = [System.IO.File]::OpenWrite($dest)\n", fp);
    fputs("  $buffer     = New-Object byte[] 65536\n", fp);
    fputs("  $downloaded = [Int64]0\n", fp);
    fputs("\n", fp);
    fputs("  while (($read = $netStream.Read($buffer, 0, $buffer.Length)) -gt 0) {\n", fp);
    fputs("    $fileStream.Write($buffer, 0, $read)\n", fp);
    fputs("    $downloaded += $read\n", fp);
    fputs("    $pct    = [Math]::Min(100, [int](($downloaded * 100) / $totalBytes))\n", fp);
    fputs("    $filled = [Math]::Floor(40 * $pct / 100)\n", fp);
    fputs("    $bar    = ('#' * $filled).PadRight(40, '-')\n", fp);
    fputs("    $dlMB   = [Math]::Floor($downloaded / 1MB)\n", fp);
    fputs("    Write-Host -NoNewline \"`r[$bar] $($pct.ToString('00'))% ($dlMB / $totalMB MB)\"\n", fp);
    fputs("  }\n", fp);
    fputs("\n", fp);
    fputs("  $fileStream.Close()\n", fp);
    fputs("  $netStream.Close()\n", fp);
    fputs("  $client.Dispose()\n", fp);
    fputs("\n", fp);
    fputs("  $fileSize = (Get-Item $dest).Length\n", fp);
    fputs("  if ($fileSize -lt $totalBytes) { throw \"Incomplete download: got $fileSize of $totalBytes bytes\" }\n", fp);
    fputs("\n", fp);
    fputs("  Write-Host ''\n", fp);
    fputs("  Write-Host 'Download complete!' -ForegroundColor Green\n", fp);
    fputs("  exit 0\n", fp);
    fputs("} catch {\n", fp);
    fputs("  Write-Host ('ERROR: ' + $_.Exception.Message) -ForegroundColor Red\n", fp);
    fputs("  try { if ($fileStream) { $fileStream.Close() } } catch {}\n", fp);
    fputs("  if (Test-Path $dest) { Remove-Item $dest -Force -ErrorAction SilentlyContinue }\n", fp);
    fputs("  exit 1\n", fp);
    fputs("}\n", fp);

    fclose(fp);

    snprintf(command, sizeof(command),
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"%s\"",
        scriptPath);

    result = RunChildProcess(command);

    DeleteFileA(scriptPath);

    return result;
}

static int ExtractZip(const char *zipPath, const char *destDir)
{
    char command[MAX_CMD];
    
    printf("Extracting to: %s\n", destDir);
    
    /* Ensure destination directory exists */
    CreateDirectoryA(destDir, NULL);
    
    if (strstr(zipPath, ".zip") != NULL) {
        snprintf(command, sizeof(command),
            "powershell.exe -NoProfile -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"",
            zipPath, destDir);
    } else {
        /* Handle tar.gz or tar.zst */
        char destZip[MAX_PATH];
        lstrcpynA(destZip, zipPath, sizeof(destZip));
        snprintf(command, sizeof(command),
            "powershell.exe -NoProfile -Command \"tar -xf '%s' -C '%s'\"",
            zipPath, destDir);
    }
    
    return RunChildProcess(command);
}

static BOOL VerifyLlamaCppInstall(const char *installPath)
{
    char exePath[MAX_PATH];
    BOOL hasServer = FALSE;
    BOOL hasCli = FALSE;
    
    /* Check for llama-server */
    snprintf(exePath, sizeof(exePath), "%s\\llama-server.exe", installPath);
    hasServer = FileExistsA_(exePath);
    
    /* Also check without .exe extension for Unix-like systems */
    if (!hasServer) {
        snprintf(exePath, sizeof(exePath), "%s/llama-server", installPath);
        hasServer = FileExistsA_(exePath);
    }
    
    /* Check for llama-cli */
    snprintf(exePath, sizeof(exePath), "%s\\llama-cli.exe", installPath);
    hasCli = FileExistsA_(exePath);
    
    if (!hasCli) {
        snprintf(exePath, sizeof(exePath), "%s/llama-cli", installPath);
        hasCli = FileExistsA_(exePath);
    }
    
    if (!hasServer) {
        fprintf(stderr, "llama-server not found at: %s\n", installPath);
    }
    if (!hasCli) {
        fprintf(stderr, "llama-cli not found at: %s\n", installPath);
    }
    
    return hasServer && hasCli;
}

static int SetupLlamaCpp(void)
{
    LlamaCppBuild *builds = NULL;
    int buildCount = 0;
    int selectedIdx = -1;
    char installDir[MAX_PATH];
    char zipPath[MAX_PATH];
    char modelDir[MAX_PATH];
    OSType os;
    int result = 0;
    
    printf("\n================================================================\n");
    printf("  llama.cpp Setup\n");
    printf("================================================================\n\n");
    
    /* Detect OS */
    os = DetectOS();
    printf("Detected OS: %s\n\n", GetOSName(os));
    
    /* Fetch releases */
    printf("Fetching latest releases from GitHub...\n");
    
    if (FetchLlamaCppReleases(&builds, &buildCount, NULL) != 0) {
        fprintf(stderr, "Failed to fetch releases.\n");
        return 1;
    }
    
    if (buildCount == 0) {
        fprintf(stderr, "No builds available for your OS.\n");
        return 1;
    }
    
    printf("Found %d build(s)\n\n", buildCount);
    
    /* Show interactive menu */
    selectedIdx = SelectBuildMenu(builds, buildCount);
    if (selectedIdx < 0) {
        printf("\nSelection cancelled.\n");
        FreeLlamaCppBuilds(builds, buildCount);
        return 0;
    }
    
    printf("\nSelected: %s\n", builds[selectedIdx].name);
    
    /* Prepare install path */
    if (!LoadConfigFromDisk()) {
        /* Create default paths */
        if (!BuildConfigPath(sConfigPath, sizeof(sConfigPath))) {
            fprintf(stderr, "Failed to build config path.\n");
            FreeLlamaCppBuilds(builds, buildCount);
            return 1;
        }
    }
    
    /* Build install directory path - use local directory */
    GetCurrentDirectoryA(sizeof(installDir), installDir);
    lstrcatA(installDir, "\\llama-cpp\\");
    
    /* Build model directory path */
    lstrcpynA(modelDir, sFolder, sizeof(modelDir));
    if (!modelDir[0]) {
        GetCurrentDirectoryA(sizeof(modelDir), modelDir);
        lstrcatA(modelDir, "\\models");
    }
    
    /* Ensure install directory exists */
    CreateDirectoryA(installDir, NULL);
    
    /* Download the build */
    GetTempFileNameA(".", "valora_llama_", 0, zipPath);
    lstrcatA(zipPath, ".zip");
    
    printf("\n================================================================\n");
    printf("  Downloading...\n");
    printf("================================================================\n\n");
    
    result = DownloadBuildWithProgress(builds[selectedIdx].downloadUrl, zipPath);
    if (result != 0) {
        fprintf(stderr, "Download failed with exit code %d\n", result);
        DeleteFileA(zipPath);
        FreeLlamaCppBuilds(builds, buildCount);
        return 1;
    }
    
    /* Extract the build */
    printf("\n================================================================\n");
    printf("  Extracting...\n");
    printf("================================================================\n\n");
    
    result = ExtractZip(zipPath, installDir);
    DeleteFileA(zipPath);
    
    if (result != 0) {
        fprintf(stderr, "Extraction failed with exit code %d\n", result);
        FreeLlamaCppBuilds(builds, buildCount);
        return 1;
    }
    
    /* Verify installation */
    printf("\n================================================================\n");
    printf("  Verifying...\n");
    printf("================================================================\n\n");
    
    if (!VerifyLlamaCppInstall(installDir)) {
        fprintf(stderr, "Installation verification failed.\n");
        FreeLlamaCppBuilds(builds, buildCount);
        return 1;
    }
    
    /* Save path to config */
    lstrcpynA(sLlamaCppPath, installDir, sizeof(sLlamaCppPath));
    
    /* Save the build name pattern for later updates */
    {
        char buildPattern[64];
        const char *p = strstr(builds[selectedIdx].name, "-bin-");
        if (p) {
            const char *osStart = p + 5;
            const char *osEnd = strchr(osStart, '-');
            if (osEnd) {
                int len = (int)(osEnd - osStart);
                if (len < sizeof(buildPattern)) {
                    memcpy(buildPattern, osStart, len);
                    buildPattern[len] = '\0';
                    
                    /* Save build pattern for later updates */
                    if (sConfigPath[0]) {
                        WritePrivateProfileStringA("paths", "llama_cpp_build", buildPattern, sConfigPath);
                    }
                }
            }
        }
    }
    
    /* Check if we need to update config */
    if (!BuildConfigPath(sConfigPath, sizeof(sConfigPath))) {
        fprintf(stderr, "Warning: Could not build config path. Path saved in memory only.\n");
    } else {
        WritePrivateProfileStringA("paths", "llama_cpp_path", sLlamaCppPath, sConfigPath);
    }
    
    /* Update server path to new installation */
    {
        WIN32_FIND_DATAA findData;
        HANDLE hFind;
        char searchPath[MAX_PATH];
        
        snprintf(searchPath, sizeof(searchPath), "%s\\*llama-server.exe", installDir);
        hFind = FindFirstFileA(searchPath, &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            FindClose(hFind);
            snprintf(sServer, sizeof(sServer), "%s\\%s", installDir, findData.cFileName);
            
            /* Save to config */
            if (sConfigPath[0]) {
                WritePrivateProfileStringA("paths", "server", sServer, sConfigPath);
            }
        }
    }
    
    FreeLlamaCppBuilds(builds, buildCount);
    
    printf("\n================================================================\n");
    printf("  \x1b[32mSetup completed successfully!\x1b[0m\n");
    printf("================================================================\n\n");
    
    printf("llama.cpp installed to: %s\n", sLlamaCppPath);
    printf("You can now use 'valora run' or 'valora serve' with the configured server.\n\n");
    
    return 0;
}

static int UpdateLlamaCpp(void)
{
    LlamaCppBuild *builds = NULL;
    int buildCount = 0;
    int selectedIdx = -1;
    char installDir[MAX_PATH];
    char zipPath[MAX_PATH];
    int result = 0;
    
    printf("\n================================================================\n");
    printf("  llama.cpp Update\n");
    printf("================================================================\n\n");
    
    /* Load config to get current llama.cpp path */
    if (!LoadConfigFromDisk()) {
        fprintf(stderr, "Valora is not configured. Run 'valora setup --llama.cpp' first.\n");
        return 1;
    }
    
    if (!sLlamaCppPath[0]) {
        fprintf(stderr, "llama.cpp path not found in config. Run 'valora setup --llama.cpp' first.\n");
        return 1;
    }
    
    printf("Fetching latest releases from GitHub...\n\n");
    
    /* Fetch all releases - let user choose any build */
    if (FetchLlamaCppReleases(&builds, &buildCount, NULL) != 0) {
        fprintf(stderr, "Failed to fetch releases.\n");
        return 1;
    }
    
    if (buildCount == 0) {
        fprintf(stderr, "No builds found for Windows.\n");
        return 1;
    }
    
    printf("Found %d matching build(s)\n\n", buildCount);
    
    /* Let user select the build */
    selectedIdx = SelectBuildMenu(builds, buildCount);
    if (selectedIdx < 0) {
        printf("\nSelection cancelled.\n");
        FreeLlamaCppBuilds(builds, buildCount);
        return 0;
    }
    
    printf("\nSelected: %s\n", builds[selectedIdx].name);
    
    /* Use existing install directory */
    lstrcpynA(installDir, sLlamaCppPath, sizeof(installDir));
    
    /* Delete existing installation */
    printf("\n================================================================\n");
    printf("  Removing old installation...\n");
    printf("================================================================\n\n");
    
    {
        char command[MAX_PATH * 2];
        snprintf(command, sizeof(command),
            "powershell.exe -NoProfile -Command \"Remove-Item -Path '%s\\*' -Recurse -Force\"",
            installDir);
        result = RunChildProcess(command);
    }
    
    /* Download the new build */
    GetTempFileNameA(".", "valora_llama_", 0, zipPath);
    lstrcatA(zipPath, ".zip");
    
    printf("\n================================================================\n");
    printf("  Downloading...\n");
    printf("================================================================\n\n");
    
    result = DownloadBuildWithProgress(builds[selectedIdx].downloadUrl, zipPath);
    if (result != 0) {
        fprintf(stderr, "Download failed with exit code %d\n", result);
        DeleteFileA(zipPath);
        FreeLlamaCppBuilds(builds, buildCount);
        return 1;
    }
    
    /* Extract the build */
    printf("\n================================================================\n");
    printf("  Extracting...\n");
    printf("================================================================\n\n");
    
    result = ExtractZip(zipPath, installDir);
    DeleteFileA(zipPath);
    
    if (result != 0) {
        fprintf(stderr, "Extraction failed with exit code %d\n", result);
        FreeLlamaCppBuilds(builds, buildCount);
        return 1;
    }
    
    /* Verify installation */
    printf("\n================================================================\n");
    printf("  Verifying...\n");
    printf("================================================================\n\n");
    
    if (!VerifyLlamaCppInstall(installDir)) {
        fprintf(stderr, "Installation verification failed.\n");
        FreeLlamaCppBuilds(builds, buildCount);
        return 1;
    }
    
    /* Update server path to new installation */
    {
        WIN32_FIND_DATAA findData;
        HANDLE hFind;
        char searchPath[MAX_PATH];
        
        snprintf(searchPath, sizeof(searchPath), "%s\\*llama-server.exe", installDir);
        hFind = FindFirstFileA(searchPath, &findData);
        if (hFind != INVALID_HANDLE_VALUE) {
            FindClose(hFind);
            snprintf(sServer, sizeof(sServer), "%s\\%s", installDir, findData.cFileName);
            
            if (sConfigPath[0]) {
                WritePrivateProfileStringA("paths", "server", sServer, sConfigPath);
            }
        }
    }
    
    FreeLlamaCppBuilds(builds, buildCount);
    
    printf("\n================================================================\n");
    printf("  \x1b[32mUpdate completed successfully!\x1b[0m\n");
    printf("================================================================\n\n");
    
    printf("llama.cpp updated at: %s\n\n", sLlamaCppPath);
    
    return 0;
}

static int PrintUsage(void)
{
    printf("\x1b[36mValora CLI - Local AI Model Manager\x1b[0m\n\n");
    
    printf("\x1b[33mGeneral:\x1b[0m\n");
    printf("  valora setup           Open the GUI setup window\n");
    printf("  valora help           Show this help message\n");
    printf("  valora version        Show version information\n");
    
    printf("\n\x1b[33mSetup:\x1b[0m\n");
    printf("  valora setup --llama.cpp  Setup llama.cpp from GitHub releases\n");
    printf("  valora setup --models     Download models from Hugging Face\n");
    printf("  valora update --llama.cpp Update installed llama.cpp to latest\n");
    
    printf("\n\x1b[33mModel Management:\x1b[0m\n");
    printf("  valora list           List configured models (alias: valora models)\n");
    printf("  valora list --dir <path>  Change models folder path\n");
    printf("  valora models         List configured models\n");
    printf("  valora run [model]    Run model in llama-cli\n");
    printf("    Options (all optional):\n");
    printf("      --context <n>     Context length (default: 2048)\n");
    printf("      --gpu-layers <n>  GPU layers (default: -1 for auto)\n");
    printf("      --cache-type-k <t>  K cache type (default: f16)\n");
    printf("      --cache-type-v <t>  V cache type (default: f16)\n");
    printf("        Types: f16, q8_0, q4_0, q4_1\n");
    printf("      --debug           Show command before running\n");
    printf("  valora get <repo>     Download GGUF model from Hugging Face\n");
    printf("    Example: valora get TheBloke/Mistral-7B-v0.1-GGUF\n");
    
    printf("\n\x1b[33mllama.cpp:\x1b[0m\n");
    printf("  valora llama          Show current llama.cpp path\n");
    printf("  valora llama --dir <path>  Change llama.cpp folder path\n");
    
    printf("\n\x1b[33mServer:\x1b[0m\n");
    printf("  valora serve [model]  Start model in llama-server\n");
    printf("    Options (all optional):\n");
    printf("      --context <n>     Context length (default: 2048)\n");
    printf("      --gpu-layers <n>  GPU layers (default: -1 for auto)\n");
    printf("      --cache-type-k <t>  K cache type (default: f16)\n");
    printf("      --cache-type-v <t>  V cache type (default: f16)\n");
    printf("        Types: f16, q8_0, q4_0, q4_1\n");
    printf("      --ip              Show local IP in server URL\n");
    printf("      --port <n>        Server port (default: 8080)\n");
    printf("  valora chat           Start interactive chat (requires server running)\n");
    printf("    Options (all optional):\n");
    printf("      --debug           Enable debug mode\n");
    printf("      --ollama          Use Ollama-compatible API\n");

    printf("\n\x1b[33mDaemon (Background Server):\x1b[0m\n");
    printf("  valora daemon start           Start daemon in background (default port: 11435)\n");
    printf("    --port <n>                  Use custom port\n");
    printf("    --fg, --foreground          Run in foreground (for debugging)\n");
    printf("  valora daemon stop            Stop running daemon\n");
    printf("  valora daemon status          Show daemon status and current model\n");
    printf("  valora daemon restart         Restart the daemon\n");
    printf("  valora daemon log [N]         Show last N lines of daemon log (default: 50)\n");
    printf("\n  Daemon API Endpoints:\n");
    printf("    GET  /api/tags              List available models (Ollama-compatible)\n");
    printf("    GET  /v1/models             List models (OpenAI-compatible)\n");
    printf("    POST /api/generate           Generate completion\n");
    printf("    POST /api/chat              Chat completion\n");
    printf("    POST /v1/chat/completions   OpenAI-style chat\n");
    printf("    DELETE /api/delete          Unload current model\n");
    printf("    GET  /api/version           Daemon version\n");

    printf("\n\x1b[33mUtilities:\x1b[0m\n");
    printf("  valora info           Show system information (RAM, GPU)\n");
    printf("  valora kill           Terminate running model server\n");
    printf("  valora status         Check if server is running\n");
    
    printf("\n\x1b[90mQuick Start:\x1b[0m\n");
    printf("  1. Run 'valora setup' to configure your models folder\n");
    printf("  2. Run 'valora list' to see available models\n");
    printf("  3. Run 'valora run' to run a model in CLI mode (will prompt for model selection)\n");
    printf("  4. Run 'valora serve' to start the server (will prompt for model selection)\n");
    printf("  5. Run 'valora chat' to start chatting\n");
    printf("\n\x1b[90mNote: Commands work without any flags. Use flags only to customize behavior.\x1b[0m\n");
    
    fflush(stdout);
    return 1;
}

/* ──────────────────────────────────────────────
   GPU Layer Auto-Detection Helpers
   ────────────────────────────────────────────── */

/* Calculate quantization scale factor (relative to fp32) */
static double GetQuantizationScaleFactor(const char *quantType)
{
    if (!quantType) return 1.0;
    
    if (strcmp(quantType, "Q8") == 0) return 0.95;
    if (strcmp(quantType, "Q6") == 0) return 0.70;
    if (strcmp(quantType, "Q5") == 0) return 0.55;
    if (strcmp(quantType, "Q4") == 0) return 0.40;
    if (strcmp(quantType, "Q3") == 0) return 0.30;
    if (strcmp(quantType, "F16") == 0) return 0.50;
    if (strcmp(quantType, "F32") == 0) return 1.00;
    
    return 0.50; /* Conservative default */
}

/*
 * Estimate maximum safe GPU layers based on:
 *   - Available VRAM
 *   - Model size and quantization
 *   - Reserve memory for context and overhead
 * Returns recommended GPU layer count, or -1 for auto-detection fallback
 */
static int EstimateGpuLayers(const char *folderPath, const char *modelName)
{
    if (!folderPath || !modelName || !*folderPath || !*modelName)
        return -1;

    /* Build full path */
    char fullPath[MAX_PATH * 2];
    snprintf(fullPath, sizeof(fullPath), "%s\\%s", folderPath, modelName);
    
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(fullPath, GetFileExInfoStandard, &fad)) {
        ULARGE_INTEGER fileSize;
        fileSize.LowPart = fad.nFileSizeLow;
        fileSize.HighPart = fad.nFileSizeHigh;
        int modelSizeMB = (int)(fileSize.QuadPart / (1024 * 1024));
        
        if (modelSizeMB <= 0)
            return -1;

        /* Estimate actual size after context overhead (20% reserve for KV cache, etc) */
        int reservedMB = (int)(modelSizeMB * 0.20);
        
        const char *quantType = DetectQuantizationType(modelName);
        double scaleFactor = GetQuantizationScaleFactor(quantType);
        
        /* Get VRAM */
        int vramMB = GetTotalVRAM();
        
        /* Available for layers = total VRAM - model size - reserves */
        int availForLayers = vramMB - modelSizeMB - reservedMB;
        
        if (availForLayers <= 0)
            return -1;
        
        /* Rough heuristic */
        int estimatedLayers = (availForLayers * modelSizeMB) / (1024 * 256);
        
        if (estimatedLayers <= 0)
            estimatedLayers = 1;
        
        /* Cap at 128 layers (very conservative upper bound) */
        if (estimatedLayers > 128)
            estimatedLayers = 128;
        
        return estimatedLayers;
    }
    
    return -1;  /* File not found */
}

/*
 * Auto-detect and populate GPU layers field when model is selected
 * Only updates if user hasn't manually edited the field
 */
static void AutoDetectAndSetGpuLayers(void)
{
    int modelIdx = (int)SendMessageA(hModelCombo, CB_GETCURSEL, 0, 0);
    
    /* If no model selected or folder not set, reset to defaults */
    if (modelIdx == CB_ERR || !sFolder[0]) {
        SetWindowTextA(hGpuEdit, "-1");
        nLastDetectedGpu = -1;
        bGpuFieldEdited = FALSE;
        return;
    }

    /* Get selected model name */
    char modelName[MAX_PATH] = {0};
    SendMessageA(hModelCombo, CB_GETLBTEXT, (WPARAM)modelIdx, (LPARAM)modelName);

    /* Try auto-detection */
    int detectedLayers = EstimateGpuLayers(sFolder, modelName);
    
    /* If detection succeeded, update the field */
    if (detectedLayers > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", detectedLayers);
        SetWindowTextA(hGpuEdit, buf);
        nLastDetectedGpu = detectedLayers;
        bGpuFieldEdited = FALSE;
    } else {
        /* Detection failed; keep safe fallback (-1 = auto) */
        SetWindowTextA(hGpuEdit, "-1");
        nLastDetectedGpu = -1;
        bGpuFieldEdited = FALSE;
    }
}

static void CopyToClipboardA(HWND hwnd, const char *text)
{
    if (!text || !*text) return;
    if (!OpenClipboard(hwnd)) return;

    EmptyClipboard();

    size_t len = strlen(text) + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hMem) {
        void *p = GlobalLock(hMem);
        if (p) {
            memcpy(p, text, len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }

    CloseClipboard();
}

static void ShowOnlyOnPage(HWND hwnd)
{
    ShowWindow(hServerEdit,    SW_SHOW);
    ShowWindow(hFolderEdit,    SW_SHOW);
    ShowWindow(hModelCombo,    SW_SHOW);
    ShowWindow(hBtnServer,     SW_SHOW);
    ShowWindow(hBtnFolder,     SW_SHOW);
    ShowWindow(hCtxEdit,       SW_SHOW);
    ShowWindow(hGpuEdit,       SW_SHOW);
    ShowWindow(hPortEdit,      SW_SHOW);
    ShowWindow(hThreadsEdit,   SW_SHOW);
    ShowWindow(hServerTypeCombo, SW_SHOW);
    ShowWindow(hLblServer,     SW_SHOW);
    ShowWindow(hLblFolder,     SW_SHOW);
    ShowWindow(hLblModel,      SW_SHOW);
    ShowWindow(hLblCtx,        SW_SHOW);
    ShowWindow(hLblGpu,        SW_SHOW);
    ShowWindow(hLblPort,       SW_SHOW);
    ShowWindow(hLblThreads,    SW_SHOW);
    ShowWindow(hLblServerType, SW_SHOW);
    ShowWindow(hLblCommandType, SW_SHOW);
    
    ShowWindow(hOutputEdit,    SW_HIDE);
    ShowWindow(hBtnGenerate,   SW_HIDE);
    ShowWindow(hBtnCopy,       SW_HIDE);
    ShowWindow(hBtnPrev,       SW_HIDE);
    SetWindowTextA(hBtnNext, "Save Setup");
    InvalidateRect(hwnd, NULL, TRUE);
}

/* ──────────────────────────────────────────────
   Model scanning (with recursive subfolder support)
   ────────────────────────────────────────────── */
static void ScanModelsRecursively(const char *basePath, int *count);

static void ScanModels(void)
{
    int selectedIndex = 0;
    int i;

    nModels = 0;
    if (hModelCombo)
        SendMessageA(hModelCombo, CB_RESETCONTENT, 0, 0);
    
    /* Reset GPU auto-detection state when rescanning models */
    bGpuFieldEdited = FALSE;
    nLastDetectedGpu = -1;

    if (!sFolder[0]) return;

    /* Scan models recursively from all subfolders */
    ScanModelsRecursively(sFolder, &nModels);

    if (!EnsureSelectedModelExists())
        return;

    for (i = 0; i < nModels; ++i) {
        if (lstrcmpiA(sModels[i], sSelectedModel) == 0) {
            selectedIndex = i;
            break;
        }
    }

    if (hModelCombo && nModels > 0) {
        SendMessageA(hModelCombo, CB_SETCURSEL, (WPARAM)selectedIndex, 0);
        AutoDetectAndSetGpuLayers();
    }
}

/* Recursive function to scan for GGUF files in subfolders */
static void ScanModelsRecursively(const char *basePath, int *count)
{
    char searchPattern[MAX_PATH * 2];
    char subfolderPath[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE hFind;

    /* First, scan GGUF files in current directory */
    snprintf(searchPattern, sizeof(searchPattern), "%s\\*.gguf", basePath);
    hFind = FindFirstFileA(searchPattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (*count < MAX_MODELS && !IsProjectorFileName(fd.cFileName)) {
                    /* Store full relative path from models folder */
                    const char *relativePath = basePath + lstrlenA(sFolder);
                    if (*relativePath == '\\') relativePath++;
                    
                    char fullRelPath[MAX_PATH];
                    if (*relativePath) {
                        snprintf(fullRelPath, sizeof(fullRelPath), "%s\\%s", relativePath, fd.cFileName);
                    } else {
                        lstrcpynA(fullRelPath, fd.cFileName, MAX_PATH);
                    }
                    
                    lstrcpynA(sModels[*count], fullRelPath, MAX_PATH);
                    
                    lstrcpynA(sModelTypes[*count], GetModelTypeLabel(fd.cFileName), (int)sizeof(sModelTypes[*count]));
                    if (_stricmp(sModelTypes[*count], "Vision") == 0) {
                        FindMatchingProjector(fullRelPath, sModelProjectors[*count], MAX_PATH);
                    } else {
                        sModelProjectors[*count][0] = '\0';
                    }
                    if (hModelCombo)
                        SendMessageA(hModelCombo, CB_ADDSTRING, 0, (LPARAM)sModels[*count]);
                    (*count)++;
                }
            }
        } while (FindNextFileA(hFind, &fd) && *count < MAX_MODELS);
        FindClose(hFind);
    }

    /* Then, recurse into subdirectories */
    snprintf(searchPattern, sizeof(searchPattern), "%s\\*", basePath);
    hFind = FindFirstFileA(searchPattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                /* Skip . and .. */
                if (lstrcmpA(fd.cFileName, ".") != 0 && lstrcmpA(fd.cFileName, "..") != 0) {
                    snprintf(subfolderPath, sizeof(subfolderPath), "%s\\%s", basePath, fd.cFileName);
                    ScanModelsRecursively(subfolderPath, count);
                }
            }
        } while (FindNextFileA(hFind, &fd) && *count < MAX_MODELS);
        FindClose(hFind);
    }
}

static void SelectServerFile(HWND hwnd)
{
    OPENFILENAMEA ofn;
    char selectedFile[MAX_PATH] = "";

    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = selectedFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Executable (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = "Select Server Executable";

    if (GetOpenFileNameA(&ofn)) {
        lstrcpynA(sServer, selectedFile, sizeof(sServer));
        SetWindowTextA(hServerEdit, sServer);
    }
}

static void SelectFolder(HWND hwnd)
{
    BROWSEINFOA bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = "Select Model Folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return;

    char path[MAX_PATH] = {0};
    if (SHGetPathFromIDListA(pidl, path)) {
        lstrcpynA(sFolder, path, MAX_PATH);
        SetWindowTextA(hFolderEdit, sFolder);
        ScanModels();
    }

    CoTaskMemFree(pidl);
}

/* Derive CLI executable path from server path */
static void GetCliPathFromServerPath(const char *serverPath, char *cliPath, int cliPathLen)
{
    if (!serverPath || !cliPath || cliPathLen <= 0) {
        if (cliPath && cliPathLen > 0) cliPath[0] = '\0';
        return;
    }

    /* Copy server path */
    lstrcpynA(cliPath, serverPath, cliPathLen);
    
    /* Find the last backslash */
    char *lastSlash = strrchr(cliPath, '\\');
    if (lastSlash) {
        /* Replace filename with llama-cli.exe */
        lstrcpynA(lastSlash + 1, "llama-cli.exe", cliPathLen - (int)(lastSlash - cliPath + 1));
    } else {
        /* No path, just replace with llama-cli.exe */
        lstrcpynA(cliPath, "llama-cli.exe", cliPathLen);
    }
}

static void GenerateCommand(HWND hwnd)
{
    int idx = (int)SendMessageA(hModelCombo, CB_GETCURSEL, 0, 0);

    if (!sServer[0] && hServerEdit)
        GetWindowTextA(hServerEdit, sServer, sizeof(sServer));
    if (!sFolder[0] && hFolderEdit)
        GetWindowTextA(hFolderEdit, sFolder, sizeof(sFolder));

    if (!sServer[0]) {
        MessageBoxA(hwnd, "Select the server executable first.", "Setup incomplete", MB_ICONERROR);
        return;
    }

    if (!sFolder[0]) {
        MessageBoxA(hwnd, "Select the model folder first.", "Setup incomplete", MB_ICONERROR);
        return;
    }

    /* Allow saving even with no models - user may add models later */
    if (idx != CB_ERR) {
        SendMessageA(hModelCombo, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)sSelectedModel);
    } else {
        sSelectedModel[0] = '\0';  /* No model selected - that's okay for empty folder */
    }

    if (!SaveConfigToDisk()) {
        MessageBoxA(hwnd, "Could not save the Valora configuration.", "Save failed", MB_ICONERROR);
        return;
    }

    snprintf(
        sCommand, sizeof(sCommand),
        "valora models\r\nvalora run\r\nvalora serve\r\n\r\nCurrent quick command: %s",
        "valora serve"
    );
    CopyToClipboardA(hwnd, sCommand);
    MessageBoxA(
        hwnd,
        "Setup saved.\n\nYou can now use:\nvalora models\nvalora run\nvalora serve\n\nThe command list has been copied to your clipboard.",
        "Valora ready",
        MB_OK | MB_ICONINFORMATION
    );
}

/* ──────────────────────────────────────────────
   Drawing helpers
   ────────────────────────────────────────────── */
static void DrawLabel(HDC hdc, const char *text, int x, int y, int w, int h, HFONT font, COLORREF color, UINT fmt)
{
    RECT r = {x, y, x + w, y + h};
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextA(hdc, text, -1, &r, fmt);
    SelectObject(hdc, old);
}

static void DrawHeader(HDC hdc, RECT rc)
{
    RECT accent = {0, 0, rc.right, 4};
    HBRUSH accentBrush = CreateSolidBrush(C_ACCENT);

    FillRect(hdc, &accent, accentBrush);
    DeleteObject(accentBrush);

    SetBkMode(hdc, TRANSPARENT);
    DrawLabel(hdc, "Valora Setup", 0, 22, rc.right, 40, hFontTitle, C_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawLabel(hdc, "Set your paths once, then launch models from the terminal without reopening setup.", 0, 58, rc.right, 20, hFontSmall, C_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawCardFrame(HDC hdc, RECT rc)
{
    int cardW = (CARD_MAX_W < (rc.right - 2 * PAD)) ? CARD_MAX_W : (rc.right - 2 * PAD);
    if (cardW < 440) cardW = rc.right - 2 * PAD;
    int cardH = rc.bottom - HEADER_H - FOOTER_H - 12;
    if (cardH < 300) cardH = 300;

    int cardX = (rc.right - cardW) / 2;
    int cardY = HEADER_H + 12;

    RECT glow;
    HBRUSH glowBrush = CreateSolidBrush(C_ACCENT_SOFT);
    HBRUSH b = CreateSolidBrush(C_PANEL);
    HPEN p = CreatePen(PS_SOLID, 1, C_BORDER);
    
    glow.left = cardX - 2;
    glow.top = cardY - 2;
    glow.right = cardX + cardW + 2;
    glow.bottom = cardY + cardH + 2;
    FillRect(hdc, &glow, glowBrush);

    HBRUSH oldB = (HBRUSH)SelectObject(hdc, b);
    HPEN oldP = (HPEN)SelectObject(hdc, p);

    RoundRect(hdc, cardX, cardY, cardX + cardW, cardY + cardH, 10, 10);

    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);

    DeleteObject(p);
    DeleteObject(b);
    DeleteObject(glowBrush);
}

/* ──────────────────────────────────────────────
   Layout
   ────────────────────────────────────────────── */
static void LayoutControls(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    int W = rc.right;
    int H = rc.bottom;
    int padding = 16;
    int rowH = 26;
    int labelH = 14;
    int gapY = 8;
    int colGap = 12;
    int y = padding + 10;

    int labelW = 90;
    int editW = 140;
    int btnW = 70;
    int editH = 24;
    int comboH = 100;

    // Server Type
    MoveWindow(hLblServerType, padding, y, 100, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hServerTypeCombo, padding, y, 180, comboH, TRUE);
    y += rowH + gapY;

    // Server Path
    MoveWindow(hLblServer, padding, y, 80, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hServerEdit, padding, y, W - padding * 2 - btnW - 8, editH, TRUE);
    MoveWindow(hBtnServer, W - padding - btnW, y - 1, btnW, editH + 2, TRUE);
    y += rowH + gapY;

    // Model Folder
    MoveWindow(hLblFolder, padding, y, 80, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hFolderEdit, padding, y, W - padding * 2 - btnW - 8, editH, TRUE);
    MoveWindow(hBtnFolder, W - padding - btnW, y - 1, btnW, editH + 2, TRUE);
    y += rowH + gapY;

    // Model Combo
    MoveWindow(hLblModel, padding, y, 80, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hModelCombo, padding, y, W - padding * 2, comboH, TRUE);
    y += rowH + gapY;

    // Context & GPU in same row
    MoveWindow(hLblCtx, padding, y, labelW, labelH, TRUE);
    MoveWindow(hCtxEdit, padding + labelW + 4, y, 100, editH, TRUE);
    
    MoveWindow(hLblGpu, padding + labelW + 110, y, 70, labelH, TRUE);
    MoveWindow(hGpuEdit, padding + labelW + 110 + 75, y, 80, editH, TRUE);
    y += rowH + gapY;

    // Port & Threads in same row
    MoveWindow(hLblPort, padding, y, labelW, labelH, TRUE);
    MoveWindow(hPortEdit, padding + labelW + 4, y, 80, editH, TRUE);
    
    MoveWindow(hLblThreads, padding + labelW + 110, y, 70, labelH, TRUE);
    MoveWindow(hThreadsEdit, padding + labelW + 110 + 75, y, 80, editH, TRUE);
    y += rowH + gapY;

    // KV Cache K and V (side by side)
    MoveWindow(hLblKvCacheK, padding, y, 80, labelH, TRUE);
    MoveWindow(hKvCacheKCombo, padding + 85, y, 130, comboH, TRUE);
    MoveWindow(hLblKvCacheV, padding + 225, y, 80, labelH, TRUE);
    MoveWindow(hKvCacheVCombo, padding + 225 + 85, y, 130, comboH, TRUE);
    y += rowH + gapY + 20;

    // Buttons
    MoveWindow(hBtnPrev, padding, y, 100, 32, TRUE);
    MoveWindow(hBtnNext, W - padding - 100, y, 100, 32, TRUE);
}

/* ──────────────────────────────────────────────
   Painting
   ────────────────────────────────────────────── */
static void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);

    FillRect(hdc, &rc, brBg);

    EndPaint(hwnd, &ps);
}

/* ──────────────────────────────────────────────
   Control creation
   ────────────────────────────────────────────── */
static void CreateControls(HWND hwnd)
{
    HINSTANCE hi = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

    // Labels - short and compact
    hLblServerType = CreateWindowA("STATIC", "Server Type", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblServer = CreateWindowA("STATIC", "Server", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblFolder = CreateWindowA("STATIC", "Models Folder", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblModel  = CreateWindowA("STATIC", "Model", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblCtx    = CreateWindowA("STATIC", "Context", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblGpu    = CreateWindowA("STATIC", "GPU Layers", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblPort   = CreateWindowA("STATIC", "Port", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblThreads = CreateWindowA("STATIC", "Threads", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    // KV Cache K and V (side by side)
    hLblKvCacheK = CreateWindowA("STATIC", "KV Cache K", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblKvCacheV = CreateWindowA("STATIC", "KV Cache V", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);

    // Server Type Combo
    hServerTypeCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);
    SendMessageA(hServerTypeCombo, CB_ADDSTRING, 0, (LPARAM)"Local");
    SendMessageA(hServerTypeCombo, CB_ADDSTRING, 0, (LPARAM)"LAN");
    SendMessageA(hServerTypeCombo, CB_SETCURSEL, 0, 0);

    // Inputs
    hServerEdit = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hBtnServer = CreateWindowA("BUTTON", "Browse",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_SERVER, hi, NULL);

    hBtnFolder = CreateWindowA("BUTTON", "Browse",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_FOLDER, hi, NULL);

    hModelCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hCtxEdit = CreateWindowA("EDIT", "2048",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hGpuEdit = CreateWindowA("EDIT", "-1",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hPortEdit = CreateWindowA("EDIT", "8000",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hThreadsEdit = CreateWindowA("EDIT", "4",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    /* KV Cache K Type Combo */
    hKvCacheKCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);
    SendMessageA(hKvCacheKCombo, CB_ADDSTRING, 0, (LPARAM)"F16");
    SendMessageA(hKvCacheKCombo, CB_ADDSTRING, 0, (LPARAM)"Q8_0");
    SendMessageA(hKvCacheKCombo, CB_ADDSTRING, 0, (LPARAM)"Q4_0");
    SendMessageA(hKvCacheKCombo, CB_ADDSTRING, 0, (LPARAM)"Q4_1");
    SendMessageA(hKvCacheKCombo, CB_SETCURSEL, 0, 0);

    /* KV Cache V Type Combo */
    hKvCacheVCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);
    SendMessageA(hKvCacheVCombo, CB_ADDSTRING, 0, (LPARAM)"F16");
    SendMessageA(hKvCacheVCombo, CB_ADDSTRING, 0, (LPARAM)"Q8_0");
    SendMessageA(hKvCacheVCombo, CB_ADDSTRING, 0, (LPARAM)"Q4_0");
    SendMessageA(hKvCacheVCombo, CB_ADDSTRING, 0, (LPARAM)"Q4_1");
    SendMessageA(hKvCacheVCombo, CB_SETCURSEL, 0, 0);

    /* Output controls */
    hOutputEdit = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hBtnPrev = CreateWindowA("BUTTON", "Prev",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_PREV, hi, NULL);

    hBtnNext = CreateWindowA("BUTTON", "Next",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_NEXT, hi, NULL);
}

/* ──────────────────────────────────────────────
   Owner-draw button rendering
   ────────────────────────────────────────────── */
static BOOL OnDrawItem(LPARAM lp)
{
    DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
    if (!di || di->CtlType != ODT_BUTTON) return FALSE;

    int id = di->CtlID;
    BOOL pressed = (di->itemState & ODS_SELECTED) ? TRUE : FALSE;
    BOOL primary = (id == ID_BTN_NEXT);

    RECT r = di->rcItem;
    HDC hdc = di->hDC;

    COLORREF bg, border, txt;

    if (primary) {
        // Teal Primary Button
        bg = pressed ? C_ACCENT2 : C_ACCENT;
        border = bg;
        txt = RGB(255, 255, 255);
    } else {
        // Secondary Buttons - more compact
        bg = pressed ? RGB(55, 55, 55) : RGB(50, 50, 50);
        border = C_INPUT_BORDER;
        txt = C_TEXT;
    }

    HBRUSH b = CreateSolidBrush(bg);
    FillRect(hdc, &r, b);
    DeleteObject(b);

    HPEN p = CreatePen(PS_SOLID, 1, border);
    HPEN oldP = (HPEN)SelectObject(hdc, p);
    HBRUSH oldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, 5, 5);
    
    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(p);

    char label[64] = {0};
    GetWindowTextA(di->hwndItem, label, sizeof(label));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, txt);
    HFONT oldF = (HFONT)SelectObject(hdc, primary ? hFontBold : hFontBody);
    DrawTextA(hdc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);

    return TRUE;
}

/* ──────────────────────────────────────────────
   Window procedure
   ────────────────────────────────────────────── */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        {
            brBg     = CreateSolidBrush(C_BG);
            brPanel  = CreateSolidBrush(C_PANEL);
            brPanel2 = CreateSolidBrush(C_PANEL2);
            brInput  = CreateSolidBrush(C_INPUT);

            hFontTitle = CreateFontA(30, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI Semibold");
            hFontBody  = CreateFontA(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontSmall = CreateFontA(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            hFontBold  = CreateFontA(17, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI Semibold");
            hFontLabel = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI Semibold");

            CreateControls(hwnd);
            if (LoadConfigFromDisk()) {
                ApplyLoadedConfigToControls();
                ScanModels();
                ApplyLoadedConfigToControls();
            }
        }
        return 0;

    case WM_SIZE:
        LayoutControls(hwnd);
        ShowOnlyOnPage(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_COMMAND:
        {
            int cmdId = LOWORD(wp);
            int notifyCode = HIWORD(wp);
            HWND hCtl = (HWND)lp;
            
            switch (cmdId) {
            case ID_BTN_SERVER:   SelectServerFile(hwnd); break;
            case ID_BTN_FOLDER:   SelectFolder(hwnd); break;
            case ID_BTN_NEXT:     GenerateCommand(hwnd); break;
            }
            
            /* Handle combo box model selection change */
            if (hCtl == hModelCombo && notifyCode == CBN_SELCHANGE) {
                int sel = (int)SendMessageA(hModelCombo, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR)
                    SendMessageA(hModelCombo, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)sSelectedModel);
                AutoDetectAndSetGpuLayers();
            }
            
            /* Track if user manually edits GPU layers field */
            if (hCtl == hGpuEdit && (notifyCode == EN_CHANGE || notifyCode == EN_UPDATE)) {
                char buf[32] = {0};
                GetWindowTextA(hGpuEdit, buf, sizeof(buf));
                
                /* User is editing - track that this is a manual edit */
                /* unless the value matches our last auto-detected value */
                if (buf[0]) {
                    int currentVal = atoi(buf);
                    if (currentVal != nLastDetectedGpu) {
                        bGpuFieldEdited = TRUE;  /* User is manually editing */
                    }
                }
            }
        }
        return 0;

    case WM_DRAWITEM:
        return OnDrawItem(lp);

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, C_TEXT);
            SetBkColor(hdc, C_INPUT);
            
            // Draw a subtle accent border effect for inputs
            HWND hCtl = (HWND)lp;
            if (hCtl == hServerEdit || hCtl == hFolderEdit || hCtl == hCtxEdit || hCtl == hModelCombo) {
                // Input field border hint
            }
            return (LRESULT)brInput;
        }

    case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wp;
            HWND hCtl = (HWND)lp;

            // Labels inside the card get the Panel background color
            if (hCtl == hLblServerType || hCtl == hLblCommandType || hCtl == hLblServer || hCtl == hLblFolder || hCtl == hLblModel || hCtl == hLblCtx || 
                hCtl == hLblGpu || hCtl == hLblPort || hCtl == hLblThreads) {
                SetTextColor(hdc, C_MUTED);
                SetBkColor(hdc, C_PANEL);
                return (LRESULT)brPanel;
            }

            // Other generic statics get the App background
            SetTextColor(hdc, C_MUTED);
            SetBkColor(hdc, C_BG);
            return (LRESULT)brBg;
        }

    case WM_DESTROY:
        if (brBg) DeleteObject(brBg);
        if (brPanel) DeleteObject(brPanel);
        if (brPanel2) DeleteObject(brPanel2);
        if (brInput) DeleteObject(brInput);

        if (hFontTitle) DeleteObject(hFontTitle);
        if (hFontBody) DeleteObject(hFontBody);
        if (hFontSmall) DeleteObject(hFontSmall);
        if (hFontBold) DeleteObject(hFontBold);
        if (hFontLabel) DeleteObject(hFontLabel);

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ──────────────────────────────────────────────
   Entry point
   ────────────────────────────────────────────── */
static int RunGui(HINSTANCE hInst, int nShow)
{
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_ICON1));
    wc.hIconSm = LoadIconA(hInst, MAKEINTRESOURCEA(IDI_ICON1));
    wc.lpszClassName = "LlamaSetupRedesign";
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

    if (!RegisterClassExA(&wc))
        return 0;

    RECT wr = {0, 0, APP_W, APP_H};
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);

    HWND hwnd = CreateWindowExA(
        0, "LlamaSetupRedesign", "Valora Setup",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        (sx - winW) / 2, (sy - winH) / 2, winW, winH,
        NULL, NULL, hInst, NULL
    );

    if (!hwnd) return 0;

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}

static int EnsureCliConfigReady(BOOL requireCliExecutable)
{
    char cliPath[MAX_PATH];

    if (!LoadConfigFromDisk()) {
        fprintf(stderr, "Valora is not configured yet. Run `valora setup` first.\n");
        return 1;
    }

    if (!FileExistsA_(sServer)) {
        fprintf(stderr, "Configured server executable was not found: %s\n", sServer);
        return 1;
    }

    if (!DirectoryExistsA_(sFolder)) {
        fprintf(stderr, "Configured models folder was not found: %s\n", sFolder);
        return 1;
    }

    ScanModels();
    if (!EnsureSelectedModelExists()) {
        fprintf(stderr, "No models were found in %s\n", sFolder);
        return 1;
    }

    if (requireCliExecutable) {
        GetCliPathFromServerPath(sServer, cliPath, sizeof(cliPath));
        if (!FileExistsA_(cliPath)) {
            fprintf(stderr, "Expected CLI executable was not found: %s\n", cliPath);
            return 1;
        }
    }

    return 0;
}

static int EnsureModelsFolderReady(void)
{
    if (!LoadConfigFromDisk()) {
        fprintf(stderr, "Valora is not configured yet. Run `valora setup` first.\n");
        return 0;
    }

    if (!sFolder[0]) {
        fprintf(stderr, "No models folder is saved yet. Run `valora setup` first.\n");
        return 0;
    }

    if (!DirectoryExistsA_(sFolder)) {
        fprintf(stderr, "Configured models folder was not found: %s\n", sFolder);
        return 0;
    }

    return 1;
}

static int RunCli(int argc, char **argv)
{
    /* When no arguments, show help (user can run 'valora setup' to open GUI) */
    if (argc <= 1) {
        AttachConsoleStreams(FALSE);
        return PrintUsage();
    }

    if (lstrcmpiA(argv[1], "setup") == 0) {
        if (argc > 2 && lstrcmpiA(argv[2], "--llama.cpp") == 0) {
            AttachConsoleStreams(TRUE);
            return SetupLlamaCpp();
        }
        HideStandaloneConsoleWindow();
        return RunGui(GetModuleHandleA(NULL), SW_SHOWDEFAULT);
    }
    
    if (lstrcmpiA(argv[1], "update") == 0) {
        if (argc > 2 && lstrcmpiA(argv[2], "--llama.cpp") == 0) {
            AttachConsoleStreams(TRUE);
            return UpdateLlamaCpp();
        }
        fprintf(stderr, "Usage: valora update --llama.cpp\n");
        return 1;
    }

    if (lstrcmpiA(argv[1], "llama") == 0) {
        AttachConsoleStreams(TRUE);
        int i;
        for (i = 2; i < argc; i++) {
            if (lstrcmpiA(argv[i], "--dir") == 0 && i + 1 < argc) {
                lstrcpynA(sLlamaCppPath, argv[i + 1], sizeof(sLlamaCppPath));
                if (sConfigPath[0]) {
                    WritePrivateProfileStringA("paths", "llama_cpp_path", sLlamaCppPath, sConfigPath);
                    
                    /* Also update server path */
                    {
                        WIN32_FIND_DATAA findData;
                        HANDLE hFind;
                        char searchPath[MAX_PATH];
                        snprintf(searchPath, sizeof(searchPath), "%s\\*llama-server.exe", sLlamaCppPath);
                        hFind = FindFirstFileA(searchPath, &findData);
                        if (hFind != INVALID_HANDLE_VALUE) {
                            FindClose(hFind);
                            snprintf(sServer, sizeof(sServer), "%s\\%s", sLlamaCppPath, findData.cFileName);
                            WritePrivateProfileStringA("paths", "server", sServer, sConfigPath);
                        }
                    }
                }
                printf("llama.cpp folder updated to: %s\n", sLlamaCppPath);
                return 0;
            }
        }
        /* Show current llama.cpp path */
        if (!LoadConfigFromDisk()) {
            fprintf(stderr, "Valora is not configured. Run 'valora setup' first.\n");
            return 1;
        }
        if (sLlamaCppPath[0]) {
            printf("Current llama.cpp folder: %s\n", sLlamaCppPath);
            printf("Usage: valora llama --dir <path> to change\n");
        } else {
            printf("llama.cpp not configured. Run 'valora setup --llama.cpp' first.\n");
        }
        return 0;
    }

    AttachConsoleStreams(TRUE);

    if (lstrcmpiA(argv[1], "models") == 0 || lstrcmpiA(argv[1], "list") == 0) {
        int i;
        for (i = 2; i < argc; i++) {
            if (lstrcmpiA(argv[i], "--dir") == 0 && i + 1 < argc) {
                lstrcpynA(sFolder, argv[i + 1], sizeof(sFolder));
                if (sConfigPath[0]) {
                    WritePrivateProfileStringA("paths", "models_folder", sFolder, sConfigPath);
                }
                printf("Models folder updated to: %s\n", sFolder);
                return 0;
            }
        }
        if (EnsureCliConfigReady(FALSE) != 0)
            return 1;
        return PrintModels();
    }

    if (lstrcmpiA(argv[1], "get") == 0) {
        if (argc <= 2 || !argv[2][0]) {
            fprintf(stderr, "Usage: valora get <owner/repo|hf_url>\n");
            return 1;
        }
        return DownloadFromHuggingFace(argv[2]);
    }

    if (lstrcmpiA(argv[1], "run") == 0) {
        int i;
        int customGpu = -1;
        char ctxBuf[32] = "2048";
        char gpuBuf[32] = "-1";
        BOOL modelProvidedAsArg = FALSE;
        BOOL enableDebug = FALSE;
        SafetyDecision safety;

        SafetyInit();

        if (EnsureCliConfigReady(TRUE) != 0)
            return 1;

        sCustomCtx[0] = '\0';

        for (i = 2; i < argc; i++) {
            if (lstrcmpiA(argv[i], "--context") == 0 && i + 1 < argc) {
                lstrcpynA(sCustomCtx, argv[i + 1], sizeof(sCustomCtx));
                i++;
            } else if (lstrcmpiA(argv[i], "--gpu-layers") == 0 && i + 1 < argc) {
                customGpu = atoi(argv[i + 1]);
                i++;
            } else if (lstrcmpiA(argv[i], "--debug") == 0) {
                enableDebug = TRUE;
            } else if (argv[i][0] != '-') {
                lstrcpynA(sSelectedModel, argv[i], sizeof(sSelectedModel));
                modelProvidedAsArg = TRUE;
            }
        }

        if (!modelProvidedAsArg) {
            sSelectedModel[0] = '\0';
        }

        if (!sSelectedModel[0]) {
            char selectedModel[MAX_PATH];
            if (RunInteractiveModelSelector(selectedModel, sizeof(selectedModel)) < 0) {
                printf("Model selection cancelled.\n");
                return 0;
            }
            lstrcpynA(sSelectedModel, selectedModel, sizeof(sSelectedModel));
        }

        if (sCustomCtx[0])
            lstrcpynA(ctxBuf, sCustomCtx, sizeof(ctxBuf));
        else
            GetModelConfig(sSelectedModel, ctxBuf, sizeof(ctxBuf), gpuBuf, sizeof(gpuBuf), NULL, 0);

        if (customGpu >= 0)
            snprintf(gpuBuf, sizeof(gpuBuf), "%d", customGpu);

        {
            char projector[MAX_PATH] = "";
            int ctxLen = atoi(ctxBuf);
            int gpuLayers = atoi(gpuBuf);
            if (strcmp(GetModelTypeLabel(sSelectedModel), "Vision") == 0)
                FindMatchingProjector(sSelectedModel, projector, sizeof(projector));
            safety = CheckLoadSafety(sSelectedModel, projector[0] ? projector : NULL, ctxLen, gpuLayers);

            if (safety == SAFETY_REFUSE) {
                PrintSafetyRefusal(sLastSafetyReason, sSelectedModel,
                    "Suggestion: Try a smaller model, lower quantization, reduced context, or fewer GPU layers.");
                return 1;
            }

            if (safety == SAFETY_KILL) {
                TerminateActiveProcess();
                PrintSafetyRefusal(sLastSafetyReason, sSelectedModel,
                    "Process terminated. Suggestion: Close other applications and retry.");
                return 1;
            }

            if (safety == SAFETY_ALLOW_WITH_WARNINGS) {
                printf("\n\x1b[33m[SAFETY WARNING] %s - proceeding with caution\x1b[0m\n\n", sLastSafetyReason);
            }
        }

        BuildRunCommand(sCommand, sizeof(sCommand), NULL);
        if (sCustomCtx[0])
            printf("Using custom context length: %s\n", sCustomCtx);

        if (enableDebug) {
            printf("\n--- DEBUG: Command being executed ---\n%s\n--- END DEBUG ---\n\n", sCommand);
        }

        {
            ULONGLONG availRAM = GetAvailableRamMB();
            ULONGLONG totalRAM = GetSystemRamMB();
            int modelMB = GetModelSizeMB(sSelectedModel);
            printf("\x1b[90m[Safety Check] RAM: %llu/%llu MB | Model: %d MB\x1b[0m\n\n", 
                   availRAM, totalRAM, modelMB);
        }

        sModelLoaded = TRUE;
        return RunChildProcess(sCommand);
    }

    if (lstrcmpiA(argv[1], "serve") == 0 || lstrcmpiA(argv[1], "server") == 0) {
        int i;
        int customGpu = -1;
        char customCtx[32] = "";
        BOOL includeLocalIp = FALSE;
        BOOL modelProvidedAsArg = FALSE;
        int customPort = 0;
        SafetyDecision safety;

        SafetyInit();

        if (EnsureCliConfigReady(FALSE) != 0)
            return 1;

        for (i = 2; i < argc; i++) {
            if (lstrcmpiA(argv[i], "--context") == 0 && i + 1 < argc) {
                lstrcpynA(customCtx, argv[i + 1], sizeof(customCtx));
                i++;
            } else if (lstrcmpiA(argv[i], "--gpu-layers") == 0 && i + 1 < argc) {
                customGpu = atoi(argv[i + 1]);
                i++;
            } else if (lstrcmpiA(argv[i], "--ip") == 0) {
                includeLocalIp = TRUE;
            } else if (lstrcmpiA(argv[i], "--port") == 0 && i + 1 < argc) {
                customPort = atoi(argv[i + 1]);
                i++;
            } else if (argv[i][0] != '-') {
                lstrcpynA(sSelectedModel, argv[i], sizeof(sSelectedModel));
                modelProvidedAsArg = TRUE;
            }
        }

        if (!modelProvidedAsArg) {
            sSelectedModel[0] = '\0';
        }

        if (!sSelectedModel[0]) {
            char selectedModel[MAX_PATH];
            printf("\x1b[36m=== Select Model ===\x1b[0m\n\n");
            if (RunInteractiveModelSelector(selectedModel, sizeof(selectedModel)) < 0) {
                printf("Model selection cancelled.\n");
                return 0;
            }
            lstrcpynA(sSelectedModel, selectedModel, sizeof(sSelectedModel));
        }

        {
            char projector[MAX_PATH] = "";
            int ctxLen = customCtx[0] ? atoi(customCtx) : 2048;
            if (strcmp(GetModelTypeLabel(sSelectedModel), "Vision") == 0)
                FindMatchingProjector(sSelectedModel, projector, sizeof(projector));
            safety = CheckLoadSafety(sSelectedModel, projector[0] ? projector : NULL, ctxLen, customGpu >= 0 ? customGpu : -1);

            if (safety == SAFETY_REFUSE) {
                PrintSafetyRefusal(sLastSafetyReason, sSelectedModel,
                    "Suggestion: Try a smaller model, lower quantization, reduced context, or fewer GPU layers.");
                return 1;
            }

            if (safety == SAFETY_KILL) {
                TerminateActiveProcess();
                PrintSafetyRefusal(sLastSafetyReason, sSelectedModel,
                    "Process terminated. Suggestion: Close other applications and retry.");
                return 1;
            }

            if (safety == SAFETY_ALLOW_WITH_WARNINGS) {
                printf("\n\x1b[33m[SAFETY WARNING] %s - proceeding with caution\x1b[0m\n\n", sLastSafetyReason);
            }
        }

        BuildServeCommand(sCommand, sizeof(sCommand), customCtx[0] ? customCtx : NULL, customGpu, includeLocalIp);
        if (customCtx[0])
            printf("Using custom context length: %s\n", customCtx);
        if (includeLocalIp)
            printf("Server will listen on localhost and local IP\n");

        {
            ULONGLONG availRAM = GetAvailableRamMB();
            ULONGLONG totalRAM = GetSystemRamMB();
            int modelMB = GetModelSizeMB(sSelectedModel);
            printf("\x1b[90m[Safety Check] RAM: %llu/%llu MB | Model: %d MB\x1b[0m\n\n", 
                   availRAM, totalRAM, modelMB);
        }

        sModelLoaded = TRUE;
        return RunChildProcess(sCommand);
    }

    if (lstrcmpiA(argv[1], "chat") == 0 || lstrcmpiA(argv[1], "cli") == 0) {
        int argIdx = 2;
        while (argc > argIdx) {
            if (lstrcmpiA(argv[argIdx], "--debug") == 0) {
                sDebugMode = TRUE;
            } else if (lstrcmpiA(argv[argIdx], "--ollama") == 0) {
                sOllamaMode = TRUE;
            }
            argIdx++;
        }
        return RunChatMode();
    }

    /* Version command */
    if (lstrcmpiA(argv[1], "version") == 0 || lstrcmpiA(argv[1], "ver") == 0) {
        printf("\x1b[36mValora CLI v1.0.0\x1b[0m\n");
        printf("Local AI Model Manager\n");
        printf("Built with llama.cpp\n");
        fflush(stdout);
        return 0;
    }

    /* Info command - Show system information */
    if (lstrcmpiA(argv[1], "info") == 0 || lstrcmpiA(argv[1], "sysinfo") == 0) {
        ULONGLONG availRAM = GetAvailableRamMB();
        ULONGLONG totalRAM = GetSystemRamMB();
        int vramMB = GetTotalVRAM();
        char gpuName[256] = "";
        
        GetGpuName(gpuName, sizeof(gpuName));
        
        printf("\x1b[36m=== System Information ===\x1b[0m\n\n");
        printf("\x1b[33mMemory:\x1b[0m\n");
        printf("  Available RAM: %llu MB\n", availRAM);
        printf("  Total RAM:   %llu MB\n", totalRAM);
        printf("  Used RAM:   %llu MB\n", totalRAM - availRAM);
        
        printf("\n\x1b[33mGPU:\x1b[0m\n");
        printf("  Name:    %s\n", gpuName);
        printf("  VRAM:    %d MB\n", vramMB);
        
        /* Check for running server */
        if (sModelLoaded) {
            printf("\n\x1b[32m[Server Running]\x1b[0m Model is currently loaded\n");
        } else {
            printf("\n\x1b[90m[No Server] No model server is currently running\x1b[0m\n");
        }
        
        /* Show configured models folder */
        if (LoadConfigFromDisk() && sFolder[0]) {
            printf("\n\x1b[33mConfig:\x1b[0m\n");
            printf("  Models folder: %s\n", sFolder);
        }
        
        fflush(stdout);
        return 0;
    }

    /* Kill command - Terminate running server */
    if (lstrcmpiA(argv[1], "kill") == 0 || lstrcmpiA(argv[1], "stop") == 0) {
        if (sModelLoaded) {
            TerminateActiveProcess();
            sModelLoaded = FALSE;
            printf("\x1b[32mServer terminated successfully.\x1b[0m\n");
        } else {
            printf("\x1b[90mNo server is currently running.\x1b[0m\n");
        }
        fflush(stdout);
        return 0;
    }

    /* Status command - Check if server is running */
    if (lstrcmpiA(argv[1], "status") == 0) {
        if (sModelLoaded) {
            printf("\x1b[32m[Running]\x1b[0m Model server is active\n");
            if (sSelectedModel[0]) {
                printf("  Model: %s\n", sSelectedModel);
            }
        } else {
            printf("\x1b[90m[Stopped] No model server is running\x1b[0m\n");
        }
        fflush(stdout);
        return 0;
    }

    /* Daemon command - Background server mode */
    if (lstrcmpiA(argv[1], "daemon") == 0) {
        return RunDaemonCommand(argc, argv);
    }

    return PrintUsage();
}

/* ──────────────────────────────────────────────
   Chat Mode Implementation
   ────────────────────────────────────────────── */

#define MAX_HISTORY 50
#define MAX_MESSAGE_LEN 4096

typedef struct {
    char role[32];
    char content[MAX_MESSAGE_LEN];
} ChatMessage;

typedef struct {
    ChatMessage messages[MAX_HISTORY];
    int count;
    int max_tokens;
} ChatHistory;

static ChatHistory sChatHistory;
static char sServerUrl[512] = "http://127.0.0.1:8000";
static char sApiKey[256] = "";
static BOOL sWebSearchEnabled = FALSE;

static void ChatInit(void) {
    memset(&sChatHistory, 0, sizeof(sChatHistory));
    sChatHistory.max_tokens = 4096;
}

static char** sOllamaModels = NULL;
static int sOllamaModelCount = 0;

static char* HttpPost(const char *url, const char *json_data);
static char* HttpGetUrl(const char *full_url);

static void FreeOllamaModels(void) {
    if (sOllamaModels) {
        for (int i = 0; i < sOllamaModelCount; i++) {
            if (sOllamaModels[i]) free(sOllamaModels[i]);
        }
        free(sOllamaModels);
        sOllamaModels = NULL;
    }
    sOllamaModelCount = 0;
}

static int FetchOllamaModels(void) {
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/models", sOllamaUrl);
    
    printf("\x1b[90mFetching models from: %s\x1b[0m\n", url);
    
    char *response = HttpGetUrl(url);
    if (!response) {
        printf("\x1b[31mFailed to connect to Ollama server\x1b[0m\n");
        return -1;
    }
    
    printf("\x1b[90m[DEBUG] Response: %.200s...\x1b[0m\n", response);
    
    FreeOllamaModels();
    sOllamaModels = malloc(sizeof(char*) * 50);
    if (!sOllamaModels) {
        free(response);
        return -1;
    }
    
    const char *data_start = strstr(response, "\"data\":");
    if (data_start) {
        data_start += 7;
        const char *p = data_start;
        int in_array = 0;
        int brace_depth = 0;
        
        while (*p) {
            if (*p == '[' && !in_array) {
                in_array = 1;
            } else if (*p == '{' && in_array) {
                brace_depth++;
            } else if (*p == '}' && in_array) {
                brace_depth--;
                if (brace_depth == 0 && in_array) {
                }
            } else if (*p == ']' && in_array) {
                break;
            } else if (in_array && brace_depth >= 1 && strncmp(p, "\"id\":\"", 6) == 0) {
                p += 6;
                const char *end = strchr(p, '"');
                if (end && end > p) {
                    int len = (int)(end - p);
                    if (len > 0 && len < 256) {
                        sOllamaModels[sOllamaModelCount] = malloc(len + 1);
                        if (sOllamaModels[sOllamaModelCount]) {
                            strncpy(sOllamaModels[sOllamaModelCount], p, len);
                            sOllamaModels[sOllamaModelCount][len] = '\0';
                            sOllamaModelCount++;
                        }
                    }
                }
            }
            p++;
        }
    }
    
    free(response);
    return sOllamaModelCount;
}

static int SelectOllamaModel(char *selected, int selSize) {
    int count = FetchOllamaModels();
    if (count <= 0) {
        printf("\x1b[31mFailed to fetch models from Ollama. Is Ollama running?\x1b[0m\n");
        printf("\x1b[33mStart Ollama with: ollama serve\x1b[0m\n");
        return -1;
    }
    
    while (1) {
        ClearConsole();
        printf("\x1b[36m+===+-------------------------------------------+--------------------+\x1b[0m\n");
        printf("\x1b[36m|\x1b[0m   \x1b[36m|\x1b[0m              SELECT A MODEL               \x1b[36m|\x1b[0m      Server       \x1b[36m|\x1b[0m\n");
        printf("\x1b[36m+===+-------------------------------------------+--------------------+\x1b[0m\n\n");

        printf("\x1b[90mServer:\x1b[0m %s\n\n", sOllamaUrl);

        printf("\x1b[36m+===+-------------------------------------------+--------------------+\x1b[0m\n");
        printf("\x1b[36m|\x1b[0m \x1b[33m#\x1b[0m \x1b[36m|\x1b[0m Model Name                                  \x1b[36m|\x1b[0m Status         \x1b[36m|\x1b[0m\n");
        printf("\x1b[36m+===+-------------------------------------------+--------------------+\x1b[0m\n");
        
        for (int i = 0; i < count; i++) {
            int name_len = strlen(sOllamaModels[i]);
            printf("\x1b[36m|\x1b[0m \x1b[33m%2d\x1b[0m \x1b[36m|\x1b[0m %s", i + 1, sOllamaModels[i]);
            int pad = 43 - name_len;
            for (int j = 0; j < pad; j++) putchar(' ');
            printf("\x1b[36m|\x1b[0m Ready          \x1b[36m|\x1b[0m\n");
        }
        printf("\x1b[36m+===+-------------------------------------------+--------------------+\x1b[0m\n\n");
        printf("\x1b[90mEnter number (1-%d) or 0 to cancel: \x1b[0m", count);
        
        char input[32];
        if (!fgets(input, sizeof(input), stdin)) {
            return -1;
        }
        input[strcspn(input, "\n")] = '\0';
        
        int choice = atoi(input);
        if (choice >= 1 && choice <= count) {
            strncpy(selected, sOllamaModels[choice - 1], selSize - 1);
            selected[selSize - 1] = '\0';
            return 0;
        }
        if (choice == 0) {
            return -1;
        }
        printf("\x1b[31mInvalid choice. Press Enter to try again...\x1b[0m");
        getchar();
    }
}

static char* BuildOllamaPayload(const char *user_message) {
    static char payload[32768];
    int offset;
    int i;
    
    offset = snprintf(payload, sizeof(payload),
        "{\"model\":\"%s\",\"stream\":false,\"messages\":[", sSelectedOllamaModel);
    
    for (i = 0; i < sChatHistory.count && offset < (int)sizeof(payload) - 100; i++) {
        if (i > 0) {
            offset += snprintf(payload + offset, sizeof(payload) - offset, ",");
        }
        
        char escaped[8192] = "";
        const char *src = sChatHistory.messages[i].content;
        char *dst = escaped;
        while (*src && dst < escaped + sizeof(escaped) - 4) {
            if (*src == '"' || *src == '\\') {
                *dst++ = '\\';
            } else if (*src == '\n') {
                *dst++ = '\\';
                *dst++ = 'n';
                src++;
                continue;
            } else if (*src == '\r') {
                src++;
                continue;
            }
            *dst++ = *src++;
        }
        
        offset += snprintf(payload + offset, sizeof(payload) - offset,
            "{\"role\":\"%s\",\"content\":\"%s\"}",
            sChatHistory.messages[i].role, escaped);
    }
    
    if (user_message) {
        char escaped[8192] = "";
        const char *src = user_message;
        char *dst = escaped;
        while (*src && dst < escaped + sizeof(escaped) - 4) {
            if (*src == '"' || *src == '\\') {
                *dst++ = '\\';
            } else if (*src == '\n') {
                *dst++ = '\\';
                *dst++ = 'n';
                src++;
                continue;
            } else if (*src == '\r') {
                src++;
                continue;
            }
            *dst++ = *src++;
        }
        offset += snprintf(payload + offset, sizeof(payload) - offset,
            "],\"content\":\"%s\"}", escaped);
    } else {
        offset += snprintf(payload + offset, sizeof(payload) - offset, "]}");
    }
    
    return payload;
}

static char* ParseOllamaResponse(const char *json_response) {
    static char result[8192];
    const char *content_start;
    const char *content_end;
    
    if (!json_response) return NULL;
    if (strstr(json_response, "\"error\"")) return NULL;
    
    content_start = strstr(json_response, "\"content\":\"");
    if (!content_start) return NULL;
    
    content_start += 11;
    content_end = content_start;
    while (*content_end) {
        if (*content_end == '\\' && *(content_end+1) == '"') {
            content_end += 2;
        } else if (*content_end == '"') {
            break;
        } else {
            content_end++;
        }
    }
    
    if (!content_end || content_end <= content_start) return NULL;
    
    int len = (int)(content_end - content_start);
    if (len >= sizeof(result)) len = sizeof(result) - 1;
    
    strncpy(result, content_start, len);
    result[len] = '\0';
    
    char *dst = result;
    const char *src = result;
    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            if (*(src + 1) == 'n') {
                *dst++ = '\n';
                src += 2;
            } else if (*(src + 1) == 't') {
                *dst++ = '\t';
                src += 2;
            } else if (*(src + 1) == 'r') {
                *dst++ = '\r';
                src += 2;
            } else if (*(src + 1) == '\\') {
                *dst++ = '\\';
                src += 2;
            } else if (*(src + 1) == '"') {
                *dst++ = '"';
                src += 2;
            } else {
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    
    return result;
}

static void PrintWebSearchReport(const char *query, int resultCount) {
    printf("\n");
    printf("\x1b[36m\x1b[1m+========================================+\x1b[0m\n");
    printf("\x1b[36m\x1b[1m|\x1b[0m        \x1b[1mWEB SEARCH REPORT\x1b[0m             \x1b[36m\x1b[1m|\x1b[0m\n");
    printf("\x1b[36m\x1b[1m+========================================+\x1b[0m\n");
    printf("\n");
    
    printf("\x1b[33m[*]\x1b[0m Query: \x1b[37m%s\x1b[0m\n", query);
    printf("\x1b[32m[+]\x1b[0m Found: \x1b[37m%d result(s)\x1b[0m\n", resultCount);
    printf("\x1b[90m+========================================+\x1b[0m\n\n");
}

static void ChatAddMessage(const char *role, const char *content) {
    if (sChatHistory.count >= MAX_HISTORY) {
        int i;
        for (i = 0; i < sChatHistory.count - 1; i++) {
            sChatHistory.messages[i] = sChatHistory.messages[i + 1];
        }
        sChatHistory.count--;
    }
    
    ChatMessage *msg = &sChatHistory.messages[sChatHistory.count];
    strncpy(msg->role, role, sizeof(msg->role) - 1);
    strncpy(msg->content, content, sizeof(msg->content) - 1);
    sChatHistory.count++;
}

static void ChatClear(void) {
    sChatHistory.count = 0;
    memset(sChatHistory.messages, 0, sizeof(sChatHistory.messages));
}

static const char* GetSystemPrompt(void) {
    return "You are Valora, a helpful AI assistant running locally. "
           "Keep responses short and helpful. "
           "You can use web search when needed for current information.";
}

static size_t HttpWriteCallback(void *contents, size_t size, size_t nmemb, char **output) {
    size_t realsize = size * nmemb;
    size_t len = *output ? strlen(*output) : 0;
    char *ptr = realloc(*output, len + realsize + 1);
    
    if (!ptr)
        return 0;
    
    *output = ptr;
    memcpy(*output + len, contents, realsize);
    (*output)[len + realsize] = '\0';
    
    return realsize;
}

static DWORD WINAPI ThinkingAnimationThread(LPVOID param) {
    int *running = (int*)param;
    const char *frames = "|/-\\";
    int frame = 0;
    while (*running) {
        printf("\r\x1b[36m[%c]\x1b[0m \x1b[33mProcessing...\x1b[0m ", frames[frame % 4]);
        fflush(stdout);
        Sleep(100);
        frame++;
    }
    printf("\r                                 \r");
    fflush(stdout);
    return 0;
}

static char* HttpPost(const char *url, const char *json_data) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char *result = NULL;
    DWORD dwSize, dwDownloaded;
    BOOL bResults;
    char host[512] = "", path[1024] = "";
    INTERNET_PORT port = 80;
    const char *p, *host_start, *path_start;
    
    if (!url || !json_data)
        return NULL;
    
    p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7, port = 80;
    else if (strncmp(p, "https://", 8) == 0) p += 8, port = 443;
    
    host_start = p;
    while (*p && *p != '/' && *p != ':') p++;
    
    strncpy(host, host_start, p - host_start);
    host[p - host_start] = '\0';
    
    if (*p == ':') {
        p++;
        port = (INTERNET_PORT)atoi(p);
        while (*p && *p >= '0' && *p <= '9') p++;
    }
    
    path_start = (*p == '/') ? p : "/";
    strncpy(path, path_start, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    
    hSession = InternetOpen("Valora/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession)
        return NULL;
    
    hConnect = InternetConnect(hSession, host, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hSession); return NULL; }
    
    hRequest = HttpOpenRequest(hConnect, "POST", path, NULL, NULL, NULL, 0, 0);
    
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hSession); return NULL; }
    
    {
        DWORD timeout = 30000;
        InternetSetOption(hRequest, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOption(hRequest, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOption(hRequest, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    }
    
    {
        bResults = HttpSendRequest(hRequest, "Content-Type: application/json", 
            (DWORD)strlen("Content-Type: application/json"), (LPVOID)json_data, (DWORD)strlen(json_data));
    }
    
    if (bResults) {
        char *response = NULL;
        char buffer[8192];
        DWORD bytesRead;
        
        while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            HttpWriteCallback(buffer, 1, bytesRead, &response);
        }
        result = response;
    }
    
    if (hRequest) InternetCloseHandle(hRequest);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hSession) InternetCloseHandle(hSession);
    
    return result;
}

static BOOL sToolsEnabled = FALSE;
static BOOL sToolsForcedDisabled = FALSE;

static BOOL ShouldUseTools(void) {
    /* Disable tools for small models that don't support function calling */
    /* Models below 1B parameters typically can't handle tool calls */
    if (sToolsForcedDisabled) return FALSE;
    return sToolsEnabled;
}

static const char* GetToolsDefinition(void) {
    if (!ShouldUseTools()) return "";
    
    static const char *tools = 
        "["
        "{"
        "\"type\": \"function\","
        "\"function\": {"
        "\"name\": \"wikipedia\","
        "\"description\": \"Get a summary from Wikipedia for a given topic. Use when you need factual information about people, places, events, or topics.\","
        "\"parameters\": {"
        "\"type\": \"object\","
        "\"properties\": {"
        "\"topic\": {"
        "\"type\": \"string\","
        "\"description\": \"The Wikipedia article topic to look up (e.g., 'World_War_II', 'Artificial_intelligence')\""
        "},"
        "\"required\": [\"topic\"]"
        "}"
        "}"
        "}"
        "}"
        "]";
    return tools;
}

static char* BuildChatPayload(const char *user_message) {
    static char payload[24576];
    int offset;
    int i;
    
    offset = snprintf(payload, sizeof(payload),
        "{\"model\":\"auto\",\"messages\":["
        "{\"role\":\"system\",\"content\":\"%s. Available tools: wikipedia(topic) - lookup Wikipedia articles.\"},",
        GetSystemPrompt());
    
    for (i = 0; i < sChatHistory.count && offset < (int)sizeof(payload) - 100; i++) {
        if (i > 0)
            offset += snprintf(payload + offset, sizeof(payload) - offset, ",");
        
        char escaped[8192] = "";
        const char *src = sChatHistory.messages[i].content;
        char *dst = escaped;
        while (*src && dst < escaped + sizeof(escaped) - 4) {
            if (*src == '"' || *src == '\\') {
                *dst++ = '\\';
            } else if (*src == '\n') {
                *dst++ = '\\';
                *dst++ = 'n';
                src++;
                continue;
            } else if (*src == '\r') {
                *dst++ = '\\';
                *dst++ = 'r';
                src++;
                continue;
            } else if (*src == '\t') {
                *dst++ = '\\';
                *dst++ = 't';
                src++;
                continue;
            }
            *dst++ = *src++;
        }
        
        offset += snprintf(payload + offset, sizeof(payload) - offset,
            "{\"role\":\"%s\",\"content\":\"%s\"}",
            sChatHistory.messages[i].role,
            escaped);
    }
    
    if (user_message) {
        const char *tools = GetToolsDefinition();
        if (tools && tools[0]) {
            offset += snprintf(payload + offset, sizeof(payload) - offset,
                "],\"tools\":%s,\"max_tokens\":%d}", tools, sChatHistory.max_tokens);
        } else {
            offset += snprintf(payload + offset, sizeof(payload) - offset,
                "],\"max_tokens\":%d}", sChatHistory.max_tokens);
        }
    } else {
        offset += snprintf(payload + offset, sizeof(payload) - offset,
            "],\"max_tokens\":%d}", sChatHistory.max_tokens);
    }
    
    return payload;
}

static char* ParseChatResponse(const char *json_response) {
    static char result[8192];
    const char *content_start, *content_end;
    int len;
    
    if (!json_response)
        return NULL;
    
    if (strstr(json_response, "\"error\"")) {
        const char *err = strstr(json_response, "\"error\"");
        if (err) {
            char *colon = strchr(err, ':');
            if (colon) {
                char *start = colon + 1;
                while (*start == ' ' || *start == '"') start++;
                char *end = strchr(start, '}');
                if (!end) end = strchr(start, '"');
                if (end && end > start) {
                    len = (int)(end - start);
                    if (len > 0 && len < sizeof(result)) {
                        strncpy(result, start, len);
                        result[len] = '\0';
                        fprintf(stderr, "\x1b[31mServer error: %s\x1b[0m\n", result);
                        return NULL;
                    }
                }
            }
        }
        return NULL;
    }
    
    const char *msg_start = strstr(json_response, "\"message\":");
    if (msg_start) {
        content_start = strstr(msg_start, "\"content\"");
    } else {
        content_start = strstr(json_response, "\"content\"");
    }
    
    if (!content_start) {
        content_start = strstr(json_response, "\"text\"");
    }
    if (!content_start)
        return NULL;
    
    while (*content_start && *content_start != ':') content_start++;
    if (!*content_start) return NULL;
    content_start++;
    while (*content_start && (*content_start == ' ' || *content_start == '"')) content_start++;
    if (!*content_start) return NULL;
    
    content_end = content_start;
    while (*content_end && *content_end != '"') {
        if (*content_end == '\\')
            content_end++;
        content_end++;
    }
    
    len = (int)(content_end - content_start);
    if (len <= 0 || len >= (int)sizeof(result))
        return NULL;
    
    strncpy(result, content_start, len);
    result[len] = '\0';
    
    {
        char *p = result, *q = result;
        while (*p) {
            if (*p == '\\' && *(p + 1) == 'n') { *q++ = '\n'; p += 2; }
            else if (*p == '\\' && *(p + 1) == 't') { *q++ = '\t'; p += 2; }
            else if (*p == '\\' && *(p + 1) == 'r') { *q++ = '\r'; p += 2; }
            else *q++ = *p++;
        }
        *q = '\0';
    }
    
    {
        char *p = result, *q = result;
        while (*p) {
            if (strncmp(p, "<|im_end|>", 10) == 0) { p += 10; continue; }
            if (strncmp(p, "<|im_start|>", 11) == 0) { p += 11; continue; }
            if (strncmp(p, "<|endoftext|>", 12) == 0) { p += 12; continue; }
            *q++ = *p++;
        }
        *q = '\0';
    }
    
    while (*result && (*result == '\n' || *result == '\r' || *result == ' ')) memmove(result, result+1, strlen(result));
    
    return result;
}

static BOOL HasToolCall(const char *json_response) {
    if (!json_response) return FALSE;
    return (strstr(json_response, "\"tool_calls\":") != NULL || 
            strstr(json_response, "wikipedia") != NULL);
}

static char* ExtractToolCall(const char *json_response) {
    static char tool_call[4096];
    const char *start, *end;
    
    tool_call[0] = '\0';
    if (!json_response) return tool_call;
    
    start = strstr(json_response, "\"name\":\"");
    if (!start) start = strstr(json_response, "\"name\": \"");
    if (!start) return tool_call;
    
    start += 8;
    end = strstr(start, "\"");
    if (!end || end - start >= (int)sizeof(tool_call)) return tool_call;
    
    strncpy(tool_call, start, end - start);
    tool_call[end - start] = '\0';
    
    return tool_call;
}

static char* ExtractToolArgs(const char *json_response) {
    static char args[4096];
    const char *start, *end;
    
    args[0] = '\0';
    if (!json_response) return args;
    
    start = strstr(json_response, "\"arguments\":\"");
    if (!start) start = strstr(json_response, "\"arguments\": \"");
    if (!start) {
        start = strstr(json_response, "{\"topic\":\"");
        if (!start) return args;
    } else {
        start += 14;
    }
    
    end = strstr(start, "\"");
    if (!end) return args;
    
    int len = (int)(end - start);
    if (len >= (int)sizeof(args)) len = sizeof(args) - 1;
    
    strncpy(args, start, len);
    args[len] = '\0';
    
    return args;
}

static char* DoWikiSearch(const char *searchQuery);

static BOOL ExecuteToolCall(const char *tool_name, const char *tool_args, char *result, int resultLen) {
    if (!tool_name || !result || resultLen <= 0) return FALSE;
    
    result[0] = '\0';
    
    if (strcmp(tool_name, "wikipedia") == 0) {
        char topic[512] = "";
        const char *topic_start = strstr(tool_args, "\"topic\":\"");
        if (!topic_start) topic_start = strstr(tool_args, "topic:");
        if (topic_start) {
            if (strstr(tool_args, "\"topic\":\"")) {
                topic_start += 9;
            } else {
                topic_start += 7;
            }
            const char *topic_end = strstr(topic_start, "\"");
            if (!topic_end) topic_end = topic_start + strlen(topic_start);
            int len = (int)(topic_end - topic_start);
            if (len > 500) len = 500;
            strncpy(topic, topic_start, len);
            topic[len] = '\0';
        }
        
        if (topic[0]) {
            char *wiki_result = DoWikiSearch(topic);
            if (wiki_result && wiki_result[0]) {
                strncpy(result, wiki_result, resultLen - 1);
                result[resultLen - 1] = '\0';
                return TRUE;
            }
        }
    }
    
    return FALSE;
}

static void PrintWrapped(const char *text, int width) {
    if (!text || !*text) return;
    
    const char *p = text;
    while (*p) {
        if (*p == '\\' && *(p+1) == 'n') {
            putchar('\n');
            p += 2;
            if (*p == '\n') putchar('\n');
            continue;
        }
        if (*p == '\n') {
            putchar('\n');
            p++;
            continue;
        }
        putchar(*p);
        p++;
    }
    fflush(stdout);
}

static void EnsureServerRunning(void) {
    SOCKET s;
    struct sockaddr_in addr;
    WSADATA wsd;
    
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
        return;
    
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { WSACleanup(); return; }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        closesocket(s);
        WSACleanup();
        return;
    }
    closesocket(s);
    WSACleanup();
    
    printf("\n\x1b[33mNo server running. Please start one first:\x1b[0m\n");
    printf("  \x1b[36mvalora serve\x1b[0m\n\n");
}

static HANDLE sChatServerProcess = NULL;
static DWORD sChatServerProcessId = 0;
static BOOL sServerStartedByUs = FALSE;

static void StopChatServer(void) {
    if (sChatServerProcess) {
        TerminateProcess(sChatServerProcess, 0);
        CloseHandle(sChatServerProcess);
        sChatServerProcess = NULL;
    }
    if (sChatServerProcessId)
        KillProcessTree(sChatServerProcessId);
    sChatServerProcessId = 0;
}

static char* HttpGet(const char *query);

static DWORD WINAPI SearchAnimationThread(LPVOID param) {
    int *running = (int*)param;
    const char *frames = "|/-\\";
    int frame = 0;
    while (*running) {
        printf("\r\x1b[36m[%c]\x1b[0m \x1b[33mSearching...\x1b[0m ", frames[frame % 4]);
        fflush(stdout);
        Sleep(100);
        frame++;
    }
    printf("\r                                    \r");
    fflush(stdout);
    return 0;
}

static char* DoWikiSearch(const char *searchQuery) {
    char *result = NULL;
    char url[2048];
    
    if (!searchQuery || !*searchQuery) return NULL;
    
    int running = 1;
    HANDLE hThread = CreateThread(NULL, 0, SearchAnimationThread, &running, 0, NULL);
    
    printf("\x1b[90mWiki: \x1b[33m%s\x1b[0m\n", searchQuery);
    fflush(stdout);
    
    snprintf(url, sizeof(url), "https://en.wikipedia.org/api/rest_v1/page/summary/%s", searchQuery);
    result = HttpGetUrl(url);
    
    running = 0;
    if (hThread) {
        Sleep(200);
        CloseHandle(hThread);
    }
    
    if (result) {
        static char formatted[8192] = "";
        char *desc_start, *desc_end;
        
        formatted[0] = '\0';
        
        char *extract = strstr(result, "\"extract\":\"");
        if (extract) {
            extract += 11;
            desc_end = strstr(extract, "\"");
            if (desc_end && desc_end > extract) {
                int len = (int)(desc_end - extract);
                if (len > 4000) len = 4000;
                strncpy(formatted, extract, len);
                formatted[len] = '\0';
                
                char *p = formatted, *q = formatted;
                while (*p) {
                    if (*p == '\\' && *(p+1) == 'n') { *q++ = ' '; p += 2; }
                    else if (*p == '\\' && *(p+1) == 't') { *q++ = '\t'; p += 2; }
                    else if (*p == '\\' && *(p+1) == 'r') { p += 2; }
                    else if (*p == '"') { p++; }
                    else *q++ = *p++;
                }
                *q = '\0';
            }
        }
        
        if (!formatted[0]) {
            char *title = strstr(result, "\"title\":\"");
            if (title) {
                title += 9;
                char *title_end = strstr(title, "\"");
                if (title_end && title_end > title) {
                    int len = (int)(title_end - title);
                    if (len > 200) len = 200;
                    strncpy(formatted, "No summary available for: ", 26);
                    strncat(formatted, title, len);
                }
            }
        }
        
        free(result);
        return formatted[0] ? formatted : NULL;
    }
    
    return NULL;
}

static char* DoWebSearch(const char *searchQuery) {
    char *result = NULL;
    
    if (!searchQuery || !*searchQuery) return NULL;
    
    int running = 1;
    HANDLE hThread = CreateThread(NULL, 0, SearchAnimationThread, &running, 0, NULL);
    
    printf("\x1b[90mQuery: \x1b[33m%s\x1b[0m\n", searchQuery);
    fflush(stdout);
    
    result = HttpGet(searchQuery);
    
    running = 0;
    if (hThread) {
        Sleep(200);
        CloseHandle(hThread);
    }
    
    if (result) {
        static char formatted[16384] = "";
        char *titleStart, *titleEnd;
        char *snippetStart, *snippetEnd;
        int count = 0;
        
        formatted[0] = '\0';
        
        char *p = result;
        char *jsonStart = strstr(p, "\"results\":[");
        if (!jsonStart) jsonStart = p;
        
        while (count < 3) {
            titleStart = strstr(jsonStart, "\"title\":\"");
            if (!titleStart) break;
            titleStart += 9;
            titleEnd = strstr(titleStart, "\"");
            if (!titleEnd) break;
            
            snippetStart = strstr(titleEnd, "\"snippet\":\"");
            if (snippetStart) {
                snippetStart += 11;
                snippetEnd = strstr(snippetStart, "\"");
            }
            
            char entry[512] = "";
            
            if (titleEnd > titleStart) {
                int titleLen = (int)(titleEnd - titleStart);
                if (titleLen > 80) titleLen = 80;
                strncat(entry, titleStart, titleLen);
            }
            
            if (snippetStart && snippetEnd && snippetEnd > snippetStart) {
                int snippetLen = (int)(snippetEnd - snippetStart);
                if (snippetLen > 200) snippetLen = 200;
                strcat(entry, " - ");
                strncat(entry, snippetStart, snippetLen);
            }
            
            if (count > 0) strcat(formatted, "|||");
            strcat(formatted, entry);
            
            count++;
            jsonStart = titleEnd;
        }
        
        if (count == 0) {
            strncpy(formatted, result, sizeof(formatted) - 1);
            formatted[sizeof(formatted) - 1] = '\0';
        }
        
        free(result);
        return formatted;
    }
    
    return NULL;
}

static char* HttpGet(const char *query) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char *result = NULL;
    char url[2048] = {0};
    char host[256] = {0}, path[1024] = {0};
    const char *p;
    BOOL bResults;
    DWORD bytesRead;
    
    if (!query || !*query) return NULL;
    
    snprintf(url, sizeof(url), "https://duckduckgo.com/?q=%s&format=json", query);
    p = url + 8;
    while (*p && *p != '/') p++;
    strncpy(host, url + 8, p - url - 8);
    strcpy(path, *p ? p : "/");
    
    hSession = InternetOpen("Valora/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) return NULL;
    
    hConnect = InternetConnect(hSession, host, 443, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hSession); return NULL; }
    
    hRequest = HttpOpenRequest(hConnect, "GET", path, NULL, NULL, NULL, INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_CERT_CN_INVALID, 0);
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hSession); return NULL; }
    
    bResults = HttpSendRequest(hRequest, NULL, 0, NULL, 0);
    if (bResults) {
        char buffer[16384];
        while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            size_t len = result ? strlen(result) : 0;
            char *newresult = realloc(result, len + bytesRead + 1);
            if (newresult) {
                result = newresult;
                memcpy(result + len, buffer, bytesRead + 1);
            } else {
                free(result);
                result = NULL;
                break;
            }
        }
    }
    
    if (hRequest) InternetCloseHandle(hRequest);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hSession) InternetCloseHandle(hSession);
    
    return result;
}

static char* HttpGetUrl(const char *full_url) {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    char *result = NULL;
    char host[256] = {0}, path[1024] = {0};
    const char *p;
    BOOL bResults;
    DWORD bytesRead;
    int use_ssl = 0;
    int port = 80;
    
    if (!full_url || !*full_url) return NULL;
    
    if (strncmp(full_url, "https://", 8) == 0) {
        use_ssl = 1;
        port = 443;
        p = full_url + 8;
    } else if (strncmp(full_url, "http://", 7) == 0) {
        p = full_url + 7;
    } else {
        return NULL;
    }
    
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    
    if (colon && (!slash || colon < slash)) {
        strncpy(host, p, colon - p);
        host[colon - p] = '\0';
        port = atoi(colon + 1);
        if (slash) {
            strncpy(path, slash, sizeof(path) - 1);
        } else {
            strcpy(path, "/");
        }
    } else if (slash) {
        strncpy(host, p, slash - p);
        host[slash - p] = '\0';
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, p, sizeof(host) - 1);
        strcpy(path, "/");
    }
    
    hSession = InternetOpen("Valora/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) return NULL;
    
    hConnect = InternetConnect(hSession, host, port, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) { InternetCloseHandle(hSession); return NULL; }
    
    hRequest = HttpOpenRequest(hConnect, "GET", path, NULL, NULL, NULL, 
        use_ssl ? INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_CERT_CN_INVALID : 0, 0);
    if (!hRequest) { InternetCloseHandle(hConnect); InternetCloseHandle(hSession); return NULL; }
    
    bResults = HttpSendRequest(hRequest, NULL, 0, NULL, 0);
    if (bResults) {
        char buffer[16384];
        while (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            size_t len = result ? strlen(result) : 0;
            char *newresult = realloc(result, len + bytesRead + 1);
            if (newresult) {
                result = newresult;
                memcpy(result + len, buffer, bytesRead + 1);
            } else {
                free(result);
                result = NULL;
                break;
            }
        }
    }
    
    if (hRequest) InternetCloseHandle(hRequest);
    if (hConnect) InternetCloseHandle(hConnect);
    if (hSession) InternetCloseHandle(hSession);
    
    return result;
}

static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (sServerStartedByUs && sChatServerProcess) {
        StopChatServer();
    }
    return FALSE;
}

static BOOL IsServerRunning(void) {
    SOCKET s;
    struct sockaddr_in addr;
    WSADATA wsd;
    BOOL running = FALSE;
    
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0)
        return FALSE;
    
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { WSACleanup(); return FALSE; }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0)
        running = TRUE;
    
    closesocket(s);
    WSACleanup();
    return running;
}

static int StartServerForChat(const char *customCtx) {
    char modelPath[MAX_PATH * 2];
    char projectorPath[MAX_PATH * 2];
    char ctx[32], gpu[32], threads[32];
    char selectedModel[256];
    int selResult;
    ULONGLONG availRAM;
    ULONGLONG totalRAM;
    int modelMB;
    int safety;
    char command[MAX_CMD];
    char kvCacheK[16], kvCacheV[16];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    GetConfiguredServerValues(ctx, sizeof(ctx), gpu, sizeof(gpu), NULL, 0, threads, sizeof(threads), kvCacheK, sizeof(kvCacheK));
    GetConfiguredServerValuesBoth(kvCacheK, sizeof(kvCacheK), kvCacheV, sizeof(kvCacheV));
    
    printf("\x1b[32mContext:\x1b[0m %s\n", ctx);
    printf("\x1b[32mGPU Layers:\x1b[0m %s\n", gpu);
    printf("\x1b[32mThreads:\x1b[0m %s\n", threads);
    printf("\x1b[32mKV Cache K:\x1b[0m %s\n", kvCacheK);
    printf("\x1b[32mKV Cache V:\x1b[0m %s\n\n", kvCacheV);
    
    if (EnsureCliConfigReady(FALSE) != 0)
        return -1;
    
    SafetyInit();
    
    printf("\x1b[36m=== Select Model for Chat ===\x1b[0m\n\n");
    selResult = RunInteractiveModelSelector(selectedModel, sizeof(selectedModel));
    
    if (selResult < 0) {
        printf("Model selection cancelled.\n");
        return -1;
    }
    
    lstrcpynA(sSelectedModel, selectedModel, sizeof(sSelectedModel));
    
    {
        char projector[MAX_PATH] = "";
        if (strcmp(GetModelTypeLabel(sSelectedModel), "Vision") == 0) {
            FindMatchingProjector(sSelectedModel, projector, sizeof(projector));
            if (projector[0])
                lstrcpynA(projectorPath, projector, sizeof(projectorPath));
            else
                projectorPath[0] = '\0';
        } else {
            projectorPath[0] = '\0';
        }
    }
    
    safety = CheckLoadSafety(sSelectedModel, projectorPath[0] ? projectorPath : NULL, 2048, -1);
    
    if (safety == SAFETY_REFUSE) {
        PrintSafetyRefusal(sLastSafetyReason, sSelectedModel,
            "Suggestion: Try a smaller model or close other applications.");
        return -1;
    }
    
    if (safety == SAFETY_KILL) {
        TerminateActiveProcess();
        PrintSafetyRefusal(sLastSafetyReason, sSelectedModel,
            "Process terminated. Try again after closing other apps.");
        return -1;
    }
    
    if (safety == SAFETY_ALLOW_WITH_WARNINGS) {
        printf("\n\x1b[33m[SAFETY WARNING] %s - proceeding with caution\x1b[0m\n\n", sLastSafetyReason);
    }
    
    BuildModelPath(modelPath, sizeof(modelPath), sSelectedModel);
    GetConfiguredServerValues(ctx, sizeof(ctx), gpu, sizeof(gpu), NULL, 0, threads, sizeof(threads), kvCacheK, sizeof(kvCacheK));
    GetConfiguredServerValuesBoth(kvCacheK, sizeof(kvCacheK), kvCacheV, sizeof(kvCacheV));
    
    if (customCtx && customCtx[0])
        lstrcpynA(ctx, customCtx, sizeof(ctx));
    
    availRAM = GetAvailableRamMB();
    totalRAM = GetSystemRamMB();
    modelMB = GetModelSizeMB(sSelectedModel);

    /* Auto-disable tools for small models (< 500MB ~= 350M-500M params) that don't support function calling */
    if (modelMB > 0 && modelMB < 500) {
        sToolsForcedDisabled = TRUE;
        printf("\n\x1b[90m[Auto-disabled tools for small model]\x1b[0m\n\n");
    } else {
        sToolsForcedDisabled = FALSE;
    }
    
    ClearConsole();
    
    printf("\x1b[36m========================================\x1b[0m\n");
    printf("\x1b[36m       VALORA SERVER STARTING\x1b[0m\n");
    printf("\x1b[36m========================================\x1b[0m\n\n");
    printf("\x1b[32mModel:\x1b[0m %s\n", sSelectedModel);
    printf("\x1b[32mContext:\x1b[0m %s\n", ctx);
    printf("\x1b[32mGPU Layers:\x1b[0m %s\n", gpu);
    printf("\x1b[32mThreads:\x1b[0m %s\n", threads);
    printf("\x1b[32mKV Cache K:\x1b[0m %s\n", kvCacheK);
    printf("\x1b[32mKV Cache V:\x1b[0m %s\n\n", kvCacheV);
    printf("\x1b[90mRAM: %llu/%llu MB | Model: %d MB\x1b[0m\n", availRAM, totalRAM, modelMB);
    printf("\x1b[90mServer: http://127.0.0.1:8000\x1b[0m\n\n");
    
    if (projectorPath[0]) {
        snprintf(command, sizeof(command),
            "\"%s\" -m \"%s\" --mmproj \"%s\" -c %s -ngl %s -t %s --port 8000 --host 127.0.0.1 --cache-type-k %s --cache-type-v %s",
            sServer, modelPath, projectorPath, ctx, gpu, threads, kvCacheK, kvCacheV);
    } else {
        snprintf(command, sizeof(command),
            "\"%s\" -m \"%s\" -c %s -ngl %s -t %s --port 8000 --host 127.0.0.1 --cache-type-k %s --cache-type-v %s",
            sServer, modelPath, ctx, gpu, threads, kvCacheK, kvCacheV);
    }
    
    if (EnsureCliConfigReady(FALSE) != 0)
        return -1;
    
    SafetyInit();
    
    printf("\x1b[36m=== Select Model for Chat ===\x1b[0m\n\n");
    selResult = RunInteractiveModelSelector(selectedModel, sizeof(selectedModel));
    
    if (selResult < 0) {
        printf("Model selection cancelled.\n");
        return -1;
    }
    
    lstrcpynA(sSelectedModel, selectedModel, sizeof(sSelectedModel));
    
    {
        char projector[MAX_PATH] = "";
        if (strcmp(GetModelTypeLabel(sSelectedModel), "Vision") == 0) {
            FindMatchingProjector(sSelectedModel, projector, sizeof(projector));
            if (projector[0])
                lstrcpynA(projectorPath, projector, sizeof(projectorPath));
            else
                projectorPath[0] = '\0';
        } else {
            projectorPath[0] = '\0';
        }
    }
    
    safety = CheckLoadSafety(sSelectedModel, projectorPath[0] ? projectorPath : NULL, 2048, -1);
    
    if (safety == SAFETY_REFUSE) {
        PrintSafetyRefusal(sLastSafetyReason, sSelectedModel,
            "Suggestion: Try a smaller model or close other applications.");
        return -1;
    }
    
    if (safety == SAFETY_KILL) {
        TerminateActiveProcess();
        PrintSafetyRefusal(sLastSafetyReason, sSelectedModel,
            "Process terminated. Try again after closing other apps.");
        return -1;
    }
    
    if (safety == SAFETY_ALLOW_WITH_WARNINGS) {
        printf("\n\x1b[33m[SAFETY WARNING] %s - proceeding with caution\x1b[0m\n\n", sLastSafetyReason);
    }
    
    BuildModelPath(modelPath, sizeof(modelPath), sSelectedModel);
    GetConfiguredServerValues(ctx, sizeof(ctx), gpu, sizeof(gpu), NULL, 0, threads, sizeof(threads), kvCacheK, sizeof(kvCacheK));
    GetConfiguredServerValuesBoth(kvCacheK, sizeof(kvCacheK), kvCacheV, sizeof(kvCacheV));
        lstrcpynA(ctx, customCtx, sizeof(ctx));
    
        availRAM = GetAvailableRamMB();
    totalRAM = GetSystemRamMB();
    modelMB = GetModelSizeMB(sSelectedModel);

    /* Auto-disable tools for small models (< 500MB ~= 350M-500M params) that don't support function calling */
    if (modelMB > 0 && modelMB < 500) {
        sToolsForcedDisabled = TRUE;
        printf("\n\x1b[90m[Auto-disabled tools for small model]\x1b[0m\n\n");
    } else {
        sToolsForcedDisabled = FALSE;
    }
    
    ClearConsole();
    
    printf("\x1b[36m========================================\x1b[0m\n");
    printf("\x1b[36m       VALORA SERVER STARTING\x1b[0m\n");
    printf("\x1b[36m========================================\x1b[0m\n\n");
    printf("\x1b[32mModel:\x1b[0m %s\n", sSelectedModel);
    printf("\x1b[32mContext:\x1b[0m %s\n", ctx);
    printf("\x1b[32mGPU Layers:\x1b[0m %s\n", gpu);
    printf("\x1b[32mThreads:\x1b[0m %s\n\n", threads);
    printf("\x1b[90mRAM: %llu/%llu MB | Model: %d MB\x1b[0m\n", availRAM, totalRAM, modelMB);
    printf("\x1b[90mServer: http://127.0.0.1:8000\x1b[0m\n\n");
    
    if (projectorPath[0]) {
        snprintf(command, sizeof(command),
            "\"%s\" -m \"%s\" --mmproj \"%s\" -c %s -ngl %s -t %s --port 8000 --host 127.0.0.1",
            sServer, modelPath, projectorPath, ctx, gpu, threads);
    } else {
        snprintf(command, sizeof(command),
            "\"%s\" -m \"%s\" -c %s -ngl %s -t %s --port 8000 --host 127.0.0.1",
            sServer, modelPath, ctx, gpu, threads);
    }
    
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    printf("\x1b[90mStarting server: %s\x1b[0m\n", command);
    fflush(stdout);
    
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "\n\x1b[31mFailed to start server. Error: %lu\x1b[0m\n", GetLastError());
        return -1;
    }
    
    sChatServerProcess = pi.hProcess;
    sChatServerProcessId = pi.dwProcessId;
    CloseHandle(pi.hThread);
    
    printf("\x1b[90mWaiting for server");
    fflush(stdout);
    for (int i = 0; i < 30; i++) {
        Sleep(1000);
        DWORD exitCode;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            printf("\n\x1b[31mServer process exited early (code %lu). Check model and path.\x1b[0m\n", exitCode);
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            sChatServerProcess = NULL;
            return -1;
        }
        if (IsServerRunning()) {
            printf("\x1b[90m OK\x1b[0m\n\n");
            return 0;
        }
        printf("\x1b[90m.\x1b[0m");
        fflush(stdout);
    }
    printf("\n\x1b[31mServer did not become ready in 30s.\x1b[0m\n");
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    sChatServerProcess = NULL;
    return -1;
}

static int RunChatMode(void) {
    char input[MAX_MESSAGE_LEN];
    char *response;
    char *assistant_msg;
    char url[512];
    BOOL serverStartedByUs = FALSE;
    int exitCode = 0;
    
    ChatInit();
    
    if (sOllamaMode) {
        ClearConsole();
        printf("\x1b[36m\x1b[1m========================================\x1b[0m\n");
        printf("\x1b[36m\x1b[1m         VALORA CHAT\x1b[0m\n");
        printf("\x1b[36m\x1b[1m        (Ollama Mode)\x1b[0m\n");
        printf("\x1b[36m\x1b[1m========================================\x1b[0m\n\n");
        
        if (SelectOllamaModel(sSelectedOllamaModel, sizeof(sSelectedOllamaModel)) != 0) {
            printf("\x1b[90mModel selection cancelled.\x1b[0m\n");
            return 1;
        }
        
        ClearConsole();
        printf("\x1b[36m\x1b[1m========================================\x1b[0m\n");
        printf("\x1b[36m\x1b[1m         VALORA CHAT\x1b[0m\n");
        printf("\x1b[36m\x1b[1m        (Ollama Mode)\x1b[0m\n");
        printf("\x1b[36m\x1b[1m========================================\x1b[0m\n\n");
        printf("\x1b[32mModel:\x1b[0m %s\n", sSelectedOllamaModel);
        printf("\x1b[32mServer:\x1b[0m %s\n\n", sOllamaUrl);
        
        snprintf(url, sizeof(url), "%s/api/chat", sOllamaUrl);
    } else if (IsServerRunning()) {
        ClearConsole();
        printf("\x1b[36m\x1b[1m========================================\x1b[0m\n");
        printf("\x1b[36m\x1b[1m         VALORA CHAT\x1b[0m\n");
        printf("\x1b[36m\x1b[1m========================================\x1b[0m\n\n");
        printf("\x1b[90mUsing existing server.\x1b[0m\n\n");
        snprintf(url, sizeof(url), "%s/v1/chat/completions", sServerUrl);
    } else {
        if (StartServerForChat(NULL) != 0) {
            return 1;
        }
        sServerStartedByUs = TRUE;
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
        ClearConsole();
        printf("\x1b[36m\x1b[1m========================================\x1b[0m\n");
        printf("\x1b[36m\x1b[1m         VALORA CHAT\x1b[0m\n");
        printf("\x1b[36m\x1b[1m========================================\x1b[0m\n\n");
        printf("\x1b[32mModel:\x1b[0m %s\n", sSelectedModel);
        printf("\x1b[32mServer:\x1b[0m http://127.0.0.1:8000\n\n");
        snprintf(url, sizeof(url), "%s/v1/chat/completions", sServerUrl);
    }
    
    printf("\x1b[90mType '\x1b[33m/exit\x1b[90m' to quit | '\x1b[33m/clear\x1b[90m' for history | '\x1b[33m/cls\x1b[90m' for screen | '\x1b[33m/\x1b[90m' for commands\n\n");
    
    while (1) {
        printf("\x1b[32mYou \x1b[0m> ");
        if (!fgets(input, sizeof(input), stdin)) {
            exitCode = 0;
            break;
        }
        
input[strcspn(input, "\n")] = '\0';
        
        char *p = input;
        while (*p == ' ') p++;
        if (p != input) memmove(input, p, strlen(p) + 1);
        
        if (input[0] == '/' && input[1] == '\0') {
            printf("\n\x1b[36m=== Commands ===\x1b[0m\n");
            printf("  /exit              Exit and stop server\n");
            printf("  /clear             Clear chat history\n");
            printf("  /cls               Clear terminal\n");
            printf("  /model             Change model\n");
            printf("  /restart           Restart llama server\n");
            printf("  /restart --context N  Restart with context N\n");
            printf("  /websearch <query> Search web (optional query)\n");
            printf("  /search <query>    Same as /websearch\n");
            printf("  /web <query>       Same as /websearch\n");
            printf("  /wiki <topic>      Get Wikipedia summary\n");
            printf("  /help              Show help\n\n");
            printf("\x1b[32mYou \x1b[0m> ");
            if (!fgets(input, sizeof(input), stdin)) { exitCode = 0; break; }
            input[strcspn(input, "\n")] = '\0';
        }
        
        if (strcmp(input, "/exit") == 0 || strcmp(input, "exit") == 0) {
            exitCode = 0;
            break;
        }
        
        if (strcmp(input, "/cls") == 0) {
            ClearConsole();
            printf("\x1b[36m\x1b[1m========================================\x1b[0m\n");
            printf("\x1b[36m\x1b[1m         VALORA CHAT\x1b[0m\n");
            if (sOllamaMode) {
                printf("\x1b[36m\x1b[1m        (Ollama Mode)\x1b[0m\n");
            }
            printf("\x1b[36m\x1b[1m========================================\x1b[0m\n\n");
            if (sOllamaMode) {
                printf("\x1b[32mModel:\x1b[0m %s\n", sSelectedOllamaModel);
                printf("\x1b[32mServer:\x1b[0m %s\n\n", sOllamaUrl);
            } else {
                printf("\x1b[32mModel:\x1b[0m %s\n", sSelectedModel);
                printf("\x1b[32mServer:\x1b[0m http://127.0.0.1:8000\n\n");
            }
            printf("\x1b[90mType '\x1b[33m/exit\x1b[90m' to quit | '\x1b[33m/clear\x1b[90m' for history | '\x1b[33m/cls\x1b[90m' for screen\n\n");
            continue;
        }
        
        if (strcmp(input, "/clear") == 0 || strcmp(input, "clear") == 0) {
            ChatClear();
            printf("\x1b[90mChat history cleared.\x1b[0m\n");
            continue;
        }
        
        if (strcmp(input, "/model") == 0) {
            char selectedModel[MAX_PATH];
            if (RunInteractiveModelSelector(selectedModel, sizeof(selectedModel)) >= 0) {
                printf("\x1b[33mModel change requires restart. Please exit and run 'valora chat' again.\x1b[0m\n");
            }
            continue;
        }
        
        if (strncmp(input, "/restart", 8) == 0) {
            char customCtx[32] = "";
            char *ctxArg = strstr(input, "--context");
            if (ctxArg) {
                ctxArg += 9;
                while (*ctxArg == ' ') ctxArg++;
                int ctxLen = 0;
                while (ctxArg[ctxLen] && ctxArg[ctxLen] != ' ' && ctxLen < 31) {
                    customCtx[ctxLen] = ctxArg[ctxLen];
                    ctxLen++;
                }
                customCtx[ctxLen] = '\0';
            }
            if (customCtx[0]) {
                printf("\x1b[90mRestarting server with context %s...\x1b[0m\n", customCtx);
            } else {
                printf("\x1b[90mRestarting server...\x1b[0m\n");
            }
            if (sServerStartedByUs && sChatServerProcess) {
                StopChatServer();
            }
            Sleep(1000);
            if (StartServerForChat(customCtx[0] ? customCtx : NULL) != 0) {
                printf("\x1b[31mFailed to restart server\x1b[0m\n");
            } else {
                if (customCtx[0]) {
                    printf("\x1b[32mServer restarted with context %s\x1b[0m\n", customCtx);
                } else {
                    printf("\x1b[32mServer restarted\x1b[0m\n");
                }
            }
            continue;
        }
        
        if (strncmp(input, "/websearch", 10) == 0 || strncmp(input, "/search", 7) == 0 || strncmp(input, "/web", 4) == 0) {
            char searchQuery[512] = "";
            char *queryStart = strchr(input, ' ');
            if (queryStart) {
                queryStart++;
                while (*queryStart == ' ') queryStart++;
                lstrcpynA(searchQuery, queryStart, sizeof(searchQuery));
            }
            
            if (!searchQuery[0]) {
                printf("\x1b[90mSearch query: \x1b[0m");
                if (fgets(searchQuery, sizeof(searchQuery), stdin)) {
                    searchQuery[strcspn(searchQuery, "\n")] = 0;
                }
            }
            
            if (searchQuery[0]) {
                char *result = DoWebSearch(searchQuery);
                if (result && result[0]) {
                    char searchResults[3][4096];
                    int resultCount = 0;
                    
                    char *token = strtok(result, "|||");
                    while (token && resultCount < 3) {
                        strncpy(searchResults[resultCount], token, sizeof(searchResults[resultCount]) - 1);
                        searchResults[resultCount][sizeof(searchResults[resultCount]) - 1] = '\0';
                        resultCount++;
                        token = strtok(NULL, "|||");
                    }
                    
                    if (resultCount == 0) {
                        strncpy(searchResults[0], result, sizeof(searchResults[0]) - 1);
                        searchResults[0][sizeof(searchResults[0]) - 1] = '\0';
                        resultCount = 1;
                    }
                    
                    ChatClear();
                    
                    char searchHeader[512];
                    snprintf(searchHeader, sizeof(searchHeader), "[Web Search: %s]", searchQuery);
                    ChatAddMessage("system", searchHeader);
                    
                    for (int i = 0; i < resultCount; i++) {
                        char contextMsg[8192];
                        snprintf(contextMsg, sizeof(contextMsg), "[Search result %d]\n%s", i + 1, searchResults[i]);
                        ChatAddMessage("user", contextMsg);
                    }
                    
                    PrintWebSearchReport(searchQuery, resultCount);
                    
                    printf("\x1b[90mSearch results added to chat history. Ask me anything about this topic!\x1b[0m\n\n");
                    continue;
                } else {
                    printf("\x1b[31m[✗] Search failed\x1b[0m\n\n");
                }
            } else {
                printf("\x1b[90mSearch cancelled.\x1b[0m\n");
            }
            continue;
        }
        
        if (strncmp(input, "/wiki", 5) == 0) {
            char searchQuery[512] = "";
            char *queryStart = strchr(input, ' ');
            if (queryStart) {
                queryStart++;
                while (*queryStart == ' ') queryStart++;
                lstrcpynA(searchQuery, queryStart, sizeof(searchQuery));
            }
            
            if (!searchQuery[0]) {
                printf("\x1b[90mWikipedia article: \x1b[0m");
                if (fgets(searchQuery, sizeof(searchQuery), stdin)) {
                    searchQuery[strcspn(searchQuery, "\n")] = 0;
                }
            }
            
            if (searchQuery[0]) {
                char *result = DoWikiSearch(searchQuery);
                if (result && result[0]) {
                    ChatClear();
                    
                    char wikiHeader[512];
                    snprintf(wikiHeader, sizeof(wikiHeader), "[Wikipedia: %s]", searchQuery);
                    ChatAddMessage("system", wikiHeader);
                    
                    char contextMsg[8192];
                    snprintf(contextMsg, sizeof(contextMsg), "%s", result);
                    ChatAddMessage("user", contextMsg);
                    
                    printf("\x1b[90mWikipedia article added to chat history. Ask me anything about this topic!\x1b[0m\n\n");
                } else {
                    printf("\x1b[31mFailed to fetch Wikipedia article\x1b[0m\n");
                }
            } else {
                printf("\x1b[90mSearch cancelled.\x1b[0m\n");
            }
            continue;
        }
        
        if (strcmp(input, "/help") == 0 || strcmp(input, "/?") == 0) {
            printf("\n\x1b[36mAvailable Commands:\x1b[0m\n");
            printf("  /exit     - Exit chat and stop server\n");
            printf("  /clear    - Clear chat history\n");
            printf("  /cls      - Clear terminal screen\n");
            printf("  /model    - Change model (requires restart)\n");
            printf("  /restart  - Restart llama server\n");
            printf("  /websearch <query> - Search web and get AI response\n");
            printf("  /search   - Same as /websearch\n");
            printf("  /web      - Same as /websearch\n");
            printf("  /wiki     - Get Wikipedia summary\n");
            printf("  /help     - Show this help\n\n");
            continue;
        }
        
        if (strlen(input) == 0)
            continue;
        
        ChatAddMessage("user", input);
        
        if (sDebugMode) {
            printf("\x1b[90m[DEBUG] Sending to URL: %s\x1b[0m\n", url);
        }
        
        response = NULL;
        for (int retry = 0; retry < 3 && !response; retry++) {
            if (retry > 0) {
                printf("\x1b[90m[Retry %d/3]...\x1b[0m\n", retry);
                Sleep(1000);
            }
            
            BOOL thinking = TRUE;
            HANDLE hThread = CreateThread(NULL, 0, ThinkingAnimationThread, &thinking, 0, NULL);
            
            if (sOllamaMode) {
                response = HttpPost(url, BuildOllamaPayload(input));
            } else {
                response = HttpPost(url, BuildChatPayload(input));
            }
            
            thinking = FALSE;
            if (hThread) { WaitForSingleObject(hThread, INFINITE); CloseHandle(hThread); }
        }
        
        if (!response) {
            DWORD err = GetLastError();
            if (sDebugMode) {
                if (err == 0) {
                    printf("\x1b[31m[DEBUG] HttpPost returned NULL, no Win32 error set - possible connection reset or timeout\x1b[0m\n");
                } else {
                    char buffer[256];
                    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, buffer, sizeof(buffer), NULL);
                    printf("\x1b[31m[DEBUG] HTTP POST failed. LastError: %lu - %s\x1b[0m\n", err, buffer);
                }
            }
            printf("\x1b[31mFailed to get response. Is the server running?\x1b[0m\n");
            ChatAddMessage("assistant", "");
            continue;
        }
        
        if (sDebugMode) {
            int dbgLen = strlen(response);
            if (dbgLen > 500) {
                printf("\x1b[90m[DEBUG] Response (first 500 chars): %.500s...\x1b[0m\n\n", response);
            } else {
                printf("\x1b[90m[DEBUG] Response: %.500s\x1b[0m\n\n", response);
            }
        }
        
        if (!sOllamaMode && HasToolCall(response)) {
            char tool_name[256] = "";
            char tool_args[4096] = "";
            char tool_result[8192] = "";
            
            strncpy(tool_name, ExtractToolCall(response), sizeof(tool_name) - 1);
            tool_name[sizeof(tool_name) - 1] = '\0';
            strncpy(tool_args, ExtractToolArgs(response), sizeof(tool_args) - 1);
            tool_args[sizeof(tool_args) - 1] = '\0';
            
            if (tool_name[0] && ExecuteToolCall(tool_name, tool_args, tool_result, sizeof(tool_result))) {
                printf("\x1b[90m[Tool] Executed %s: %.100s...\x1b[0m\n", tool_name, tool_result);
                
                char tool_msg[16384];
                snprintf(tool_msg, sizeof(tool_msg),
                    "{\"role\":\"tool\",\"content\":\"%s\",\"name\":\"%s\"}",
                    tool_result, tool_name);
                ChatAddMessage("user", tool_msg);
                
                free(response);
                if (sOllamaMode) {
                    response = HttpPost(url, BuildOllamaPayload(NULL));
                } else {
                    response = HttpPost(url, BuildChatPayload(NULL));
                }
                
                if (!response) {
                    printf("\x1b[31mFailed to get follow-up response\x1b[0m\n");
                    continue;
                }
            }
        }
        
        if (sOllamaMode) {
            assistant_msg = ParseOllamaResponse(response);
        } else {
            assistant_msg = ParseChatResponse(response);
        }
        
        if (!assistant_msg || !assistant_msg[0]) {
            if (sOllamaMode) {
                printf("\x1b[31mFailed to parse Ollama response.\x1b[0m\n");
            } else {
                printf("\x1b[31mServer error. Restarting...\x1b[0m\n");
                if (sServerStartedByUs && sChatServerProcess) {
                    StopChatServer();
                }
                Sleep(1000);
                if (StartServerForChat(NULL) != 0) {
                    printf("\x1b[31mFailed to restart server\x1b[0m\n");
                } else {
                    printf("\x1b[32mServer restarted\x1b[0m\n");
                }
            }
            free(response);
            ChatAddMessage("assistant", "");
            continue;
        }
        
        ChatAddMessage("assistant", assistant_msg);
        
        printf("\n\x1b[35mAI >\x1b[0m ");
        PrintWrapped(assistant_msg, 80);
        printf("\n\n");
        fflush(stdout);
        
        free(response);
    }
    
    if (serverStartedByUs) {
        StopChatServer();
    }
    
    printf("\n\x1b[90mGoodbye!\x1b[0m\n");
    return exitCode;
}

/* ──────────────────────────────────────────────
   DAEMON MODE IMPLEMENTATION
   ────────────────────────────────────────────── */

#define DAEMON_DEFAULT_PORT 11435
#define DAEMON_INTERNAL_PORT 11436
#define DAEMON_LOG_MAX_SIZE (10 * 1024 * 1024)
#define DAEMON_THREAD_POOL_SIZE 4

typedef struct {
    char method[16];
    char path[512];
    char query[256];
    char body[65536];
    int  bodyLen;
    char contentType[64];
} HttpRequest;

typedef struct {
    char model[256];
    char prompt[32768];
    BOOL stream;
    char messages[50][2048];
    int messageCount;
} OllamaRequest;

static char    sCurrentDaemonModel[MAX_PATH] = "";
static HANDLE  sDaemonChildHandle = NULL;
static DWORD   sDaemonChildPid = 0;
static HANDLE  sDaemonModelMutex = NULL;
static BOOL    sDaemonRunning = FALSE;
static int     sDaemonPort = DAEMON_DEFAULT_PORT;
static int     sDaemonInternalPort = DAEMON_INTERNAL_PORT;
static HANDLE  sDaemonLogFile = INVALID_HANDLE_VALUE;
static ULONGLONG sDaemonStartTime = 0;
static char    sDaemonLogPath[MAX_PATH] = "";
static SOCKET  sDaemonListenSocket = INVALID_SOCKET;

static CRITICAL_SECTION sDaemonLogCS;

static void DaemonLogClose(void);

static BOOL BuildPidFilePath(char *path, int len)
{
    char appData[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appData) != S_OK)
        return FALSE;
    snprintf(path, len, "%s\\%s\\daemon.pid", appData, VALORA_APP_DIR);
    return TRUE;
}

static void WritePidFile(DWORD pid)
{
    char path[MAX_PATH];
    FILE *fp;
    if (!BuildPidFilePath(path, sizeof(path)))
        return;
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%lu", pid);
        fclose(fp);
    }
}

static DWORD ReadPidFile(void)
{
    char path[MAX_PATH];
    FILE *fp;
    DWORD pid = 0;
    if (!BuildPidFilePath(path, sizeof(path)))
        return 0;
    fp = fopen(path, "r");
    if (fp) {
        fscanf(fp, "%lu", &pid);
        fclose(fp);
    }
    return pid;
}

static void DeletePidFile(void)
{
    char path[MAX_PATH];
    if (BuildPidFilePath(path, sizeof(path)))
        DeleteFileA(path);
}

static BOOL IsDaemonRunning(void)
{
    DWORD pid = ReadPidFile();
    if (pid == 0)
        return FALSE;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        CloseHandle(hProc);
        return TRUE;
    }
    return FALSE;
}

static void DaemonLogOpen(void)
{
    char appData[MAX_PATH];
    char dirPath[MAX_PATH];
    
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appData) != S_OK)
        return;
    
    snprintf(dirPath, sizeof(dirPath), "%s\\%s", appData, VALORA_APP_DIR);
    CreateDirectoryA(dirPath, NULL);
    snprintf(sDaemonLogPath, sizeof(sDaemonLogPath), "%s\\daemon.log", dirPath);
    
    InitializeCriticalSection(&sDaemonLogCS);
    
    sDaemonLogFile = CreateFileA(sDaemonLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void DaemonLogClose(void)
{
    EnterCriticalSection(&sDaemonLogCS);
    if (sDaemonLogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(sDaemonLogFile);
        sDaemonLogFile = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&sDaemonLogCS);
    DeleteCriticalSection(&sDaemonLogCS);
}

static void DaemonLog(const char *fmt, ...)
{
    char timestamp[64];
    char buffer[4096];
    SYSTEMTIME st;
    va_list args;
    
    GetLocalTime(&st);
    snprintf(timestamp, sizeof(timestamp), "[%04d-%02d-%02d %02d:%02d:%02d]",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    EnterCriticalSection(&sDaemonLogCS);
    
    if (sDaemonLogFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        char line[4600];
        snprintf(line, sizeof(line), "%s %s\n", timestamp, buffer);
        WriteFile(sDaemonLogFile, line, (DWORD)strlen(line), &written, NULL);
        
        LARGE_INTEGER fileSize;
        if (GetFileSizeEx(sDaemonLogFile, &fileSize) && fileSize.QuadPart > DAEMON_LOG_MAX_SIZE) {
            LeaveCriticalSection(&sDaemonLogCS);
            
            char backupPath[MAX_PATH];
            snprintf(backupPath, sizeof(backupPath), "%s.bak", sDaemonLogPath);
            
            EnterCriticalSection(&sDaemonLogCS);
            CloseHandle(sDaemonLogFile);
            
            DeleteFileA(backupPath);
            MoveFileExA(sDaemonLogPath, backupPath, MOVEFILE_REPLACE_EXISTING);
            
            sDaemonLogFile = CreateFileA(sDaemonLogPath, CREATE_NEW, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
    }
    
    LeaveCriticalSection(&sDaemonLogCS);
}

static int DaemonLogPrint(int nLines)
{
    char path[MAX_PATH];
    FILE *fp;
    char **lines = NULL;
    int count = 0;
    int i;
    
    if (!BuildPidFilePath(path, sizeof(path)))
        return 1;
    
    snprintf(path, sizeof(path), "%s", sDaemonLogPath);
    
    fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Could not open daemon log file.\n");
        fflush(stderr);
        return 1;
    }
    
    lines = malloc(sizeof(char*) * 1000);
    if (!lines) {
        fclose(fp);
        return 1;
    }
    
    while (count < 1000) {
        char line[4096];
        if (fgets(line, sizeof(line), fp)) {
            lines[count] = _strdup(line);
            if (lines[count])
                count++;
        } else {
            break;
        }
    }
    fclose(fp);
    
    if (nLines <= 0 || nLines > count)
        nLines = count;
    
    for (i = count - nLines; i < count; i++) {
        printf("%s", lines[i]);
        free(lines[i]);
    }
    
    free(lines);
    fflush(stdout);
    return 0;
}

static void SendHttpResponse(SOCKET s, int status, const char *contentType, const char *body, int bodyLen)
{
    char header[1024];
    int headerLen;
    
    const char *statusText = "OK";
    if (status == 400) statusText = "Bad Request";
    else if (status == 404) statusText = "Not Found";
    else if (status == 503) statusText = "Service Unavailable";
    
    headerLen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, statusText, contentType, bodyLen);
    
    send(s, header, headerLen, 0);
    if (bodyLen > 0 && body)
        send(s, body, bodyLen, 0);
}

static void SendStreamChunk(SOCKET s, const char *data, int len)
{
    char header[32];
    int headerLen;
    
    if (len <= 0) return;
    
    headerLen = snprintf(header, sizeof(header), "%x\r\n", len);
    send(s, header, headerLen, 0);
    send(s, data, len, 0);
    send(s, "\r\n", 2, 0);
}

static void SendStreamEnd(SOCKET s)
{
    send(s, "0\r\n\r\n", 5, 0);
}

static void SendErrorResponse(SOCKET s, int status, const char *message)
{
    char body[1024];
    int bodyLen = snprintf(body, sizeof(body), "{\"error\":{\"message\":\"%s\",\"code\":%d}}", message, status);
    SendHttpResponse(s, status, "application/json", body, bodyLen);
}

static void BuildServeCommandForDaemon(char *dst, int dstLen, const char *modelName, int port)
{
    char modelPath[MAX_PATH * 2];
    char ctx[32], gpu[32], threads[32], kvCacheK[16], kvCacheV[16];
    char projectorPath[MAX_PATH] = "";
    
    BuildModelPath(modelPath, sizeof(modelPath), modelName);
    GetConfiguredServerValues(ctx, sizeof(ctx), gpu, sizeof(gpu), NULL, 0, threads, sizeof(threads), kvCacheK, sizeof(kvCacheK));
    GetConfiguredServerValuesBoth(kvCacheK, sizeof(kvCacheK), kvCacheV, sizeof(kvCacheV));
    
    GetModelConfig(modelName, ctx, sizeof(ctx), gpu, sizeof(gpu), threads, sizeof(threads));
    
    if (_stricmp(GetModelTypeLabel(modelName), "Vision") == 0) {
        char projectorName[MAX_PATH];
        if (FindMatchingProjector(modelName, projectorName, sizeof(projectorName))) {
            if (strchr(modelName, '\\')) {
                const char *lastBackslash = strrchr(modelName, '\\');
                snprintf(projectorPath, sizeof(projectorPath), "%.*s\\%s",
                    (int)(lastBackslash - modelName), modelName, projectorName);
            } else {
                lstrcpynA(projectorPath, projectorName, sizeof(projectorPath));
            }
        }
    }
    
    if (projectorPath[0]) {
        snprintf(dst, dstLen,
            "\"%s\" -m \"%s\" --mmproj \"%s\" -c %s -ngl %s -t %s --port %d --host 127.0.0.1 --cache-type-k %s --cache-type-v %s",
            sServer, modelPath, projectorPath, ctx, gpu, threads, port, kvCacheK, kvCacheV);
    } else {
        snprintf(dst, dstLen,
            "\"%s\" -m \"%s\" -c %s -ngl %s -t %s --port %d --host 127.0.0.1 --cache-type-k %s --cache-type-v %s",
            sServer, modelPath, ctx, gpu, threads, port, kvCacheK, kvCacheV);
    }
}

static BOOL DaemonWaitForServer(int port, int timeoutMs)
{
    int waited = 0;
    SOCKET testSock;
    struct sockaddr_in addr;
    WSADATA wsd;
    
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
        return FALSE;
    
    while (waited < timeoutMs) {
        testSock = socket(AF_INET, SOCK_STREAM, 0);
        if (testSock == INVALID_SOCKET) {
            WSACleanup();
            return FALSE;
        }
        
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        
        if (connect(testSock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            closesocket(testSock);
            WSACleanup();
            return TRUE;
        }
        
        closesocket(testSock);
        Sleep(200);
        waited += 200;
    }
    
    WSACleanup();
    return FALSE;
}

static void DaemonUnloadModel(void)
{
    if (sDaemonChildPid == 0 && sDaemonChildHandle == NULL)
        return;
    
    if (sDaemonChildPid != 0) {
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, sDaemonChildPid);
        if (hProc) {
            TerminateProcess(hProc, 0);
            CloseHandle(hProc);
        }
        KillProcessTree(sDaemonChildPid);
    }
    
    if (sDaemonChildHandle) {
        WaitForSingleObject(sDaemonChildHandle, 3000);
        CloseHandle(sDaemonChildHandle);
    }
    
    sDaemonChildHandle = NULL;
    sDaemonChildPid = 0;
    sCurrentDaemonModel[0] = '\0';
    
    DaemonLog("[INFO] Model unloaded");
}

static int DaemonLaunchModel(const char *modelName)
{
    char command[MAX_CMD];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD startTime = GetTickCount();
    
    BuildServeCommandForDaemon(command, sizeof(command), modelName, sDaemonInternalPort);
    
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    DaemonLog("[INFO] Launching model: %s", modelName);
    DaemonLog("[DEBUG] Command: %s", command);
    
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
        DaemonLog("[ERROR] Failed to launch model: %lu", GetLastError());
        return -1;
    }
    
    sDaemonChildHandle = pi.hProcess;
    sDaemonChildPid = pi.dwProcessId;
    lstrcpynA(sCurrentDaemonModel, modelName, sizeof(sCurrentDaemonModel));
    
    CloseHandle(pi.hThread);
    
    if (!DaemonWaitForServer(sDaemonInternalPort, 30000)) {
        DaemonLog("[ERROR] Model server failed to start within 30s");
        DaemonUnloadModel();
        return -1;
    }
    
    DWORD elapsed = GetTickCount() - startTime;
    DaemonLog("[INFO] Model loaded: %s (%lums)", modelName, elapsed);
    
    return 0;
}

static int DaemonSwapModel(const char *modelName)
{
    int result;
    SafetyDecision safety;
    char ctx[32], gpu[32], threads[32];
    ULONGLONG startTime;
    
    WaitForSingleObject(sDaemonModelMutex, INFINITE);
    
    if (sCurrentDaemonModel[0] && lstrcmpiA(sCurrentDaemonModel, modelName) == 0) {
        ReleaseMutex(sDaemonModelMutex);
        return 0;
    }
    
    if (sCurrentDaemonModel[0]) {
        DaemonLog("[INFO] Unloading model: %s", sCurrentDaemonModel);
        DaemonUnloadModel();
    }
    
    GetModelConfig(modelName, ctx, sizeof(ctx), gpu, sizeof(gpu), threads, sizeof(threads));
    
    safety = CheckLoadSafety(modelName, NULL, atoi(ctx), atoi(gpu));
    
    if (safety == SAFETY_REFUSE) {
        DaemonLog("[ERROR] Safety check failed for %s: %s", modelName, sLastSafetyReason);
        ReleaseMutex(sDaemonModelMutex);
        return -1;
    }
    
    startTime = GetTickCount();
    result = DaemonLaunchModel(modelName);
    
    ReleaseMutex(sDaemonModelMutex);
    
    return result;
}

static char* ExtractJsonString(const char *json, const char *key, char *out, int outLen)
{
    char pattern[256];
    const char *p;
    
    out[0] = '\0';
    
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    p = strstr(json, pattern);
    if (!p)
        return NULL;
    
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') p++;
    
    if (*p == '"') {
        p++;
        const char *end = strchr(p, '"');
        if (end && end > p) {
            int len = (int)(end - p);
            if (len >= outLen) len = outLen - 1;
            memcpy(out, p, len);
            out[len] = '\0';
            return out;
        }
    } else if (*p == '{' || *p == '[') {
        return NULL;
    } else {
        const char *end = p;
        while (*end && *end != ',' && *end != '}' && *end != ']' && *end != '\n')
            end++;
        int len = (int)(end - p);
        if (len > 0 && len < outLen) {
            memcpy(out, p, len);
            out[len] = '\0';
            return out;
        }
    }
    
    return NULL;
}

static void ExtractOllamaGenerateRequest(const char *body, OllamaRequest *req)
{
    memset(req, 0, sizeof(OllamaRequest));
    
    ExtractJsonString(body, "model", req->model, sizeof(req->model));
    ExtractJsonString(body, "prompt", req->prompt, sizeof(req->prompt));
    
    const char *streamStr = strstr(body, "\"stream\":");
    if (streamStr) {
        streamStr += 8;
        while (*streamStr == ' ' || *streamStr == '\t') streamStr++;
        req->stream = (strncmp(streamStr, "true", 4) == 0);
    }
}

static void ExtractOllamaChatRequest(const char *body, OllamaRequest *req)
{
    const char *p, *contentStart, *contentEnd;
    char roleBuf[64];
    int msgIdx = 0;
    
    memset(req, 0, sizeof(OllamaRequest));
    
    ExtractJsonString(body, "model", req->model, sizeof(req->model));
    
    const char *streamStr = strstr(body, "\"stream\":");
    if (streamStr) {
        streamStr += 8;
        while (*streamStr == ' ' || *streamStr == '\t') streamStr++;
        req->stream = (strncmp(streamStr, "true", 4) == 0);
    }
    
    p = body;
    while ((p = strstr(p, "\"role\":")) && msgIdx < 50) {
        p += 7;
        while (*p == ' ' || *p == '\t' || *p == '"') p++;
        
        const char *roleEnd = strchr(p, '"');
        if (!roleEnd) break;
        
        int roleLen = (int)(roleEnd - p);
        if (roleLen >= sizeof(roleBuf)) roleLen = sizeof(roleBuf) - 1;
        memcpy(roleBuf, p, roleLen);
        roleBuf[roleLen] = '\0';
        
        contentStart = strstr(roleEnd, "\"content\":");
        if (!contentStart) break;
        contentStart += 11;
        while (*contentStart == ' ' || *contentStart == '\t') contentStart++;
        
        if (*contentStart == '"') {
            contentStart++;
            contentEnd = contentStart;
            while (*contentEnd && *contentEnd != '"' && contentEnd - contentStart < 2000) {
                if (*contentEnd == '\\' && contentEnd[1])
                    contentEnd++;
                contentEnd++;
            }
        } else {
            contentEnd = contentStart;
            while (*contentEnd && *contentEnd != ',' && *contentEnd != '}' && contentEnd - contentStart < 2000)
                contentEnd++;
        }
        
        int contentLen = (int)(contentEnd - contentStart);
        if (contentLen > 0 && contentLen < 2000) {
            char contentBuf[2048];
            memcpy(contentBuf, contentStart, contentLen);
            contentBuf[contentLen] = '\0';
            
            snprintf(req->messages[msgIdx], sizeof(req->messages[0]),
                "{\"role\":\"%s\",\"content\":\"%s\"}", roleBuf, contentBuf);
            msgIdx++;
        }
        
        p = contentEnd;
    }
    
    req->messageCount = msgIdx;
}

static void BuildTagsResponse(SOCKET s)
{
    char *response;
    int totalLen;
    int capacity = 65536;
    int offset = 0;
    int i;
    
    response = malloc(capacity);
    if (!response) {
        SendErrorResponse(s, 500, "Out of memory");
        return;
    }
    
    offset = snprintf(response, capacity, "{\"models\":[");
    
    ScanModelsRecursively(sFolder, &nModels);
    
    for (i = 0; i < nModels && i < MAX_MODELS; i++) {
        char fullPath[MAX_PATH * 2];
        WIN32_FILE_ATTRIBUTE_DATA fad;
        ULARGE_INTEGER fileSize;
        const char *quant;
        const char *typeLabel;
        SYSTEMTIME st;
        FILETIME ft;
        char timeBuf[64];
        
        BuildModelPath(fullPath, sizeof(fullPath), sModels[i]);
        
        fileSize.QuadPart = 0;
        if (GetFileAttributesExA(fullPath, GetFileExInfoStandard, &fad)) {
            fileSize.LowPart = fad.nFileSizeLow;
            fileSize.HighPart = fad.nFileSizeHigh;
            ft = fad.ftLastWriteTime;
            FileTimeToSystemTime(&ft, &st);
            snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        } else {
            timeBuf[0] = '\0';
        }
        
        quant = DetectQuantizationType(sModels[i]);
        typeLabel = GetModelTypeLabel(sModels[i]);
        
        char entry[2048];
        int entryLen = snprintf(entry, sizeof(entry),
            "%s{\"name\":\"%s\",\"model\":\"%s\",\"modified_at\":\"%s\",\"size\":%llu,"
            "\"digest\":\"\",\"details\":{\"format\":\"gguf\",\"family\":\"llama\","
            "\"parameter_size\":\"7B\",\"quantization_level\":\"%s\"}}",
            i > 0 ? "," : "",
            sModels[i], sModels[i], timeBuf, fileSize.QuadPart, quant);
        
        if (offset + entryLen >= capacity) {
            capacity *= 2;
            char *newResp = realloc(response, capacity);
            if (!newResp) {
                free(response);
                SendErrorResponse(s, 500, "Out of memory");
                return;
            }
            response = newResp;
        }
        
        memcpy(response + offset, entry, entryLen);
        offset += entryLen;
    }
    
    offset += snprintf(response + offset, capacity - offset, "]}");
    
    SendHttpResponse(s, 200, "application/json", response, offset);
    free(response);
}

static void BuildV1ModelsResponse(SOCKET s)
{
    char *response;
    int capacity = 65536;
    int offset = 0;
    int i;
    time_t now = time(NULL);
    
    response = malloc(capacity);
    if (!response) {
        SendErrorResponse(s, 500, "Out of memory");
        return;
    }
    
    offset = snprintf(response, capacity, "{\"object\":\"list\",\"data\":[");
    
    ScanModelsRecursively(sFolder, &nModels);
    
    for (i = 0; i < nModels && i < MAX_MODELS; i++) {
        char entry[1024];
        int entryLen = snprintf(entry, sizeof(entry),
            "%s{\"id\":\"%s\",\"object\":\"model\",\"created\":%ld,\"owned_by\":\"valora\"}",
            i > 0 ? "," : "",
            sModels[i], (long)now);
        
        if (offset + entryLen >= capacity) {
            capacity *= 2;
            char *newResp = realloc(response, capacity);
            if (!newResp) {
                free(response);
                SendErrorResponse(s, 500, "Out of memory");
                return;
            }
            response = newResp;
        }
        
        memcpy(response + offset, entry, entryLen);
        offset += entryLen;
    }
    
    offset += snprintf(response + offset, capacity - offset, "]}");
    
    SendHttpResponse(s, 200, "application/json", response, offset);
    free(response);
}

static void HandleVersion(SOCKET s)
{
    char body[256];
    int bodyLen = snprintf(body, sizeof(body), "{\"version\":\"0.1.0\"}");
    SendHttpResponse(s, 200, "application/json", body, bodyLen);
}

static void HandleNotFound(SOCKET s)
{
    SendErrorResponse(s, 404, "Endpoint not found");
}

static void HandleGetTags(SOCKET s)
{
    BuildTagsResponse(s);
}

static void HandleGetV1Models(SOCKET s)
{
    BuildV1ModelsResponse(s);
}

static void HandleDeleteModel(SOCKET s)
{
    WaitForSingleObject(sDaemonModelMutex, INFINITE);
    
    if (sCurrentDaemonModel[0]) {
        DaemonLog("[INFO] Deleting model: %s", sCurrentDaemonModel);
        DaemonUnloadModel();
    }
    
    ReleaseMutex(sDaemonModelMutex);
    
    char body[256];
    int bodyLen = snprintf(body, sizeof(body), "{\"status\":\"success\"}");
    SendHttpResponse(s, 200, "application/json", body, bodyLen);
}

static void HandleShutdown(SOCKET s)
{
    DaemonLog("[INFO] Shutdown request received");
    
    char body[256];
    int bodyLen = snprintf(body, sizeof(body), "{\"status\":\"shutting_down\"}");
    SendHttpResponse(s, 200, "application/json", body, bodyLen);
    
    sDaemonRunning = FALSE;
}

static void TranslateOpenAiChunkToOllamaGenerate(const char *chunk, const char *modelName, char *out, int outLen, BOOL *done)
{
    const char *content = strstr(chunk, "\"content\":\"");
    const char *done_marker = strstr(chunk, "\"done\":true");
    
    *done = (done_marker != NULL);
    
    if (content) {
        content += 11;
        const char *end = content;
        while (*end && *end != '"') {
            if (*end == '\\' && end[1])
                end++;
            end++;
        }
        
        int contentLen = (int)(end - content);
        if (contentLen > 0 && contentLen < outLen - 100) {
            char decoded[4096];
            int decodedLen = 0;
            
            const char *src = content;
            char *dst = decoded;
            while (src < end && decodedLen < (int)sizeof(decoded) - 1) {
                if (*src == '\\' && src[1]) {
                    src++;
                    if (*src == 'n') { *dst++ = '\n'; src++; decodedLen++; }
                    else if (*src == 't') { *dst++ = '\t'; src++; decodedLen++; }
                    else if (*src == '"') { *dst++ = '"'; src++; decodedLen++; }
                    else if (*src == '\\') { *dst++ = '\\'; src++; decodedLen++; }
                    else { *dst++ = *src++; decodedLen++; }
                } else {
                    *dst++ = *src++;
                    decodedLen++;
                }
            }
            *dst = '\0';
            
            SYSTEMTIME st;
            FILETIME ft;
            GetSystemTime(&st);
            SystemTimeToFileTime(&st, &ft);
            
            snprintf(out, outLen,
                "{\"model\":\"%s\",\"created_at\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\","
                "\"response\":\"%s\",\"done\":false}\n",
                modelName, st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, decoded);
            return;
        }
    }
    
    out[0] = '\0';
}

static void TranslateOpenAiChunkToOllamaChat(const char *chunk, const char *modelName, char *out, int outLen, BOOL *done)
{
    const char *content = strstr(chunk, "\"content\":\"");
    const char *done_marker = strstr(chunk, "\"done\":true");
    
    *done = (done_marker != NULL);
    
    if (content) {
        content += 11;
        const char *end = content;
        while (*end && *end != '"') {
            if (*end == '\\' && end[1])
                end++;
            end++;
        }
        
        int contentLen = (int)(end - content);
        if (contentLen > 0 && contentLen < outLen - 150) {
            char decoded[4096];
            int decodedLen = 0;
            
            const char *src = content;
            char *dst = decoded;
            while (src < end && decodedLen < (int)sizeof(decoded) - 1) {
                if (*src == '\\' && src[1]) {
                    src++;
                    if (*src == 'n') { *dst++ = '\n'; src++; decodedLen++; }
                    else if (*src == 't') { *dst++ = '\t'; src++; decodedLen++; }
                    else if (*src == '"') { *dst++ = '"'; src++; decodedLen++; }
                    else if (*src == '\\') { *dst++ = '\\'; src++; decodedLen++; }
                    else { *dst++ = *src++; decodedLen++; }
                } else {
                    *dst++ = *src++;
                    decodedLen++;
                }
            }
            *dst = '\0';
            
            SYSTEMTIME st;
            FILETIME ft;
            GetSystemTime(&st);
            SystemTimeToFileTime(&st, &ft);
            
            snprintf(out, outLen,
                "{\"model\":\"%s\",\"created_at\":\"%04d-%02d-%02dT%02d:%02d:%02dZ\","
                "\"message\":{\"role\":\"assistant\",\"content\":\"%s\"},\"done\":false}\n",
                modelName, st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, decoded);
            return;
        }
    }
    
    out[0] = '\0';
}

static void StreamProxyRequest(SOCKET clientSock, const char *llamaUrl, const char *requestBody, BOOL toOllamaFormat, BOOL isChat)
{
    SOCKET proxySock;
    struct sockaddr_in addr;
    WSADATA wsd;
    char buffer[8192];
    char ollamaLine[8192];
    
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0)
        return;
    
    proxySock = socket(AF_INET, SOCK_STREAM, 0);
    if (proxySock == INVALID_SOCKET) {
        WSACleanup();
        return;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)sDaemonInternalPort);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(proxySock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(proxySock);
        WSACleanup();
        return;
    }
    
    char httpRequest[32768 + 1024];
    int reqLen = snprintf(httpRequest, sizeof(httpRequest),
        "POST /v1/completions HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        sDaemonInternalPort, (int)strlen(requestBody), requestBody);
    
    send(proxySock, httpRequest, reqLen, 0);
    
    char header[4096];
    int headerLen = 0;
    int contentLength = -1;
    int bodyReceived = 0;
    BOOL inBody = FALSE;
    
    while (1) {
        int bytes = recv(proxySock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0)
            break;
        
        buffer[bytes] = '\0';
        
        if (!inBody) {
            char *headerEnd = strstr(buffer, "\r\n\r\n");
            if (headerEnd) {
                inBody = TRUE;
                
                char *headerPortion = buffer;
                int headerPortionLen = (int)(headerEnd - buffer);
                char save = buffer[headerPortionLen];
                buffer[headerPortionLen] = '\0';
                
                const char *cl = strstr(buffer, "Content-Length:");
                if (cl) {
                    cl += 15;
                    while (*cl == ' ' || *cl == '\t') cl++;
                    contentLength = atoi(cl);
                }
                
                buffer[headerPortionLen] = save;
                
                int bodyStart = (int)(headerEnd - buffer) + 4;
                int initialBody = bytes - bodyStart;
                if (initialBody > 0) {
                    bodyReceived += initialBody;
                    
                    if (toOllamaFormat) {
                        TranslateOpenAiChunkToOllamaGenerate(headerEnd + 4, sCurrentDaemonModel, ollamaLine, sizeof(ollamaLine), &(BOOL){FALSE});
                        if (ollamaLine[0])
                            send(clientSock, ollamaLine, (int)strlen(ollamaLine), 0);
                    } else {
                        send(clientSock, headerEnd + 4, initialBody, 0);
                    }
                }
            }
        } else {
            bodyReceived += bytes;
            
            if (toOllamaFormat) {
                const char *lineStart = buffer;
                for (int i = 0; i < bytes; i++) {
                    if (buffer[i] == '\n') {
                        int lineLen = (int)(lineStart - buffer);
                        char line[4096];
                        if (lineLen < sizeof(line)) {
                            memcpy(line, lineStart, lineLen);
                            line[lineLen] = '\0';
                            
                            BOOL done = FALSE;
                            if (isChat)
                                TranslateOpenAiChunkToOllamaChat(line, sCurrentDaemonModel, ollamaLine, sizeof(ollamaLine), &done);
                            else
                                TranslateOpenAiChunkToOllamaGenerate(line, sCurrentDaemonModel, ollamaLine, sizeof(ollamaLine), &done);
                            
                            if (ollamaLine[0])
                                send(clientSock, ollamaLine, (int)strlen(ollamaLine), 0);
                            
                            if (done) {
                                snprintf(ollamaLine, sizeof(ollamaLine),
                                    "{\"model\":\"%s\",\"created_at\":\"\",\"response\":\"\",\"done\":true}\n",
                                    sCurrentDaemonModel);
                                send(clientSock, ollamaLine, (int)strlen(ollamaLine), 0);
                            }
                        }
                        lineStart = buffer + i + 1;
                    }
                }
            } else {
                send(clientSock, buffer, bytes, 0);
            }
            
            if (contentLength > 0 && bodyReceived >= contentLength)
                break;
        }
    }
    
    closesocket(proxySock);
    WSACleanup();
}

static void BuildLlamaCompletionRequest(const OllamaRequest *req, char *out, int outLen, BOOL isChat)
{
    if (isChat && req->messageCount > 0) {
        int offset = snprintf(out, outLen,
            "{\"model\":\"%s\",\"messages\":[", sCurrentDaemonModel);
        
        for (int i = 0; i < req->messageCount && offset < outLen - 100; i++) {
            offset += snprintf(out + offset, outLen - offset, "%s%s",
                i > 0 ? "," : "", req->messages[i]);
        }
        
        snprintf(out + offset, outLen - offset,
            "],\"stream\":false,\"max_tokens\":2048}");
    } else {
        snprintf(out, outLen,
            "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false,\"max_tokens\":2048}",
            sCurrentDaemonModel, req->prompt);
    }
}

static void HandlePostGenerate(SOCKET s, HttpRequest *req)
{
    OllamaRequest ollamaReq;
    char llamaRequest[32768];
    
    ExtractOllamaGenerateRequest(req->body, &ollamaReq);
    
    if (!ollamaReq.model[0]) {
        SendErrorResponse(s, 400, "Missing model field");
        return;
    }
    
    if (strcmp(ollamaReq.model, sCurrentDaemonModel) != 0) {
        int swapResult = DaemonSwapModel(ollamaReq.model);
        if (swapResult != 0) {
            SendErrorResponse(s, 503, "Failed to load model - safety check failed or model not found");
            return;
        }
    }
    
    snprintf(llamaRequest, sizeof(llamaRequest),
        "{\"model\":\"%s\",\"prompt\":\"%s\",\"stream\":false,\"max_tokens\":2048}",
        sCurrentDaemonModel, ollamaReq.prompt);
    
    SOCKET proxySock;
    struct sockaddr_in addr;
    WSADATA wsd;
    char buffer[16384];
    
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0) {
        SendErrorResponse(s, 500, "Internal error");
        return;
    }
    
    proxySock = socket(AF_INET, SOCK_STREAM, 0);
    if (proxySock == INVALID_SOCKET) {
        WSACleanup();
        SendErrorResponse(s, 500, "Internal error");
        return;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)sDaemonInternalPort);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(proxySock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(proxySock);
        WSACleanup();
        SendErrorResponse(s, 503, "Model server not responding");
        return;
    }
    
    char httpRequest[32768 + 1024];
    int reqLen = snprintf(httpRequest, sizeof(httpRequest),
        "POST /v1/completions HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        sDaemonInternalPort, (int)strlen(llamaRequest), llamaRequest);
    
    send(proxySock, httpRequest, reqLen, 0);
    
    char *response = NULL;
    size_t responseLen = 0;
    
    while (1) {
        int bytes = recv(proxySock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0)
            break;
        
        buffer[bytes] = '\0';
        
        char *bodyStart = strstr(buffer, "\r\n\r\n");
        if (bodyStart) {
            bodyStart += 4;
            int bodyLen = bytes - (int)(bodyStart - buffer);
            
            char *newResp = realloc(response, responseLen + bodyLen + 1);
            if (newResp) {
                response = newResp;
                memcpy(response + responseLen, bodyStart, bodyLen);
                responseLen += bodyLen;
                response[responseLen] = '\0';
            }
            
            const char *cl = strstr(buffer, "Content-Length:");
            if (cl) {
                cl += 15;
                while (*cl == ' ' || *cl == '\t') cl++;
                int contentLen = atoi(cl);
                
                char *headerEnd = strstr(buffer, "\r\n\r\n");
                if (headerEnd) {
                    int totalHeaderLen = (int)(headerEnd - buffer) + 4;
                    int receivedLen = bytes;
                    
                    if (receivedLen >= totalHeaderLen + contentLen)
                        break;
                }
            }
            
            if (strstr(buffer, "data:") == NULL && bodyLen > 0)
                break;
        }
    }
    
    closesocket(proxySock);
    WSACleanup();
    
    if (response && responseLen > 0) {
        SendHttpResponse(s, 200, "application/json", response, (int)responseLen);
        free(response);
    } else {
        SendErrorResponse(s, 500, "Failed to get response from model");
    }
}

static void HandlePostChat(SOCKET s, HttpRequest *req)
{
    OllamaRequest ollamaReq;
    char llamaRequest[32768];
    
    ExtractOllamaChatRequest(req->body, &ollamaReq);
    
    if (!ollamaReq.model[0]) {
        SendErrorResponse(s, 400, "Missing model field");
        return;
    }
    
    if (strcmp(ollamaReq.model, sCurrentDaemonModel) != 0) {
        int swapResult = DaemonSwapModel(ollamaReq.model);
        if (swapResult != 0) {
            SendErrorResponse(s, 503, "Failed to load model - safety check failed or model not found");
            return;
        }
    }
    
    int offset = snprintf(llamaRequest, sizeof(llamaRequest),
        "{\"model\":\"%s\",\"messages\":[", sCurrentDaemonModel);
    
    for (int i = 0; i < ollamaReq.messageCount && offset < (int)sizeof(llamaRequest) - 100; i++) {
        offset += snprintf(llamaRequest + offset, sizeof(llamaRequest) - offset, "%s%s",
            i > 0 ? "," : "", ollamaReq.messages[i]);
    }
    
    snprintf(llamaRequest + offset, sizeof(llamaRequest) - offset,
        "],\"stream\":false,\"max_tokens\":2048}");
    
    SOCKET proxySock;
    struct sockaddr_in addr;
    WSADATA wsd;
    char buffer[16384];
    
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0) {
        SendErrorResponse(s, 500, "Internal error");
        return;
    }
    
    proxySock = socket(AF_INET, SOCK_STREAM, 0);
    if (proxySock == INVALID_SOCKET) {
        WSACleanup();
        SendErrorResponse(s, 500, "Internal error");
        return;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)sDaemonInternalPort);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(proxySock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(proxySock);
        WSACleanup();
        SendErrorResponse(s, 503, "Model server not responding");
        return;
    }
    
    char httpRequest[32768 + 1024];
    int reqLen = snprintf(httpRequest, sizeof(httpRequest),
        "POST /v1/chat/completions HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        sDaemonInternalPort, (int)strlen(llamaRequest), llamaRequest);
    
    send(proxySock, httpRequest, reqLen, 0);
    
    char *response = NULL;
    size_t responseLen = 0;
    
    while (1) {
        int bytes = recv(proxySock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0)
            break;
        
        buffer[bytes] = '\0';
        
        char *bodyStart = strstr(buffer, "\r\n\r\n");
        if (bodyStart) {
            bodyStart += 4;
            int bodyLen = bytes - (int)(bodyStart - buffer);
            
            char *newResp = realloc(response, responseLen + bodyLen + 1);
            if (newResp) {
                response = newResp;
                memcpy(response + responseLen, bodyStart, bodyLen);
                responseLen += bodyLen;
                response[responseLen] = '\0';
            }
            
            const char *cl = strstr(buffer, "Content-Length:");
            if (cl) {
                cl += 15;
                while (*cl == ' ' || *cl == '\t') cl++;
                int contentLen = atoi(cl);
                
                char *headerEnd = strstr(buffer, "\r\n\r\n");
                if (headerEnd) {
                    int totalHeaderLen = (int)(headerEnd - buffer) + 4;
                    int receivedLen = bytes;
                    
                    if (receivedLen >= totalHeaderLen + contentLen)
                        break;
                }
            }
            
            if (strstr(buffer, "data:") == NULL && bodyLen > 0)
                break;
        }
    }
    
    closesocket(proxySock);
    WSACleanup();
    
    if (response && responseLen > 0) {
        SendHttpResponse(s, 200, "application/json", response, (int)responseLen);
        free(response);
    } else {
        SendErrorResponse(s, 500, "Failed to get response from model");
    }
}

static void HandlePostV1Chat(SOCKET s, HttpRequest *req)
{
    if (!sCurrentDaemonModel[0]) {
        SendErrorResponse(s, 400, "No model loaded");
        return;
    }
    
    SOCKET proxySock;
    struct sockaddr_in addr;
    WSADATA wsd;
    char buffer[16384];
    
    if (WSAStartup(MAKEWORD(2,2), &wsd) != 0) {
        SendErrorResponse(s, 500, "Internal error");
        return;
    }
    
    proxySock = socket(AF_INET, SOCK_STREAM, 0);
    if (proxySock == INVALID_SOCKET) {
        WSACleanup();
        SendErrorResponse(s, 500, "Internal error");
        return;
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)sDaemonInternalPort);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(proxySock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(proxySock);
        WSACleanup();
        SendErrorResponse(s, 503, "Model server not responding");
        return;
    }
    
    char httpRequest[32768 + 1024];
    int reqLen = snprintf(httpRequest, sizeof(httpRequest),
        "POST /v1/chat/completions HTTP/1.1\r\n"
        "Host: 127.0.0.1:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        sDaemonInternalPort, req->bodyLen, req->body);
    
    send(proxySock, httpRequest, reqLen, 0);
    
    char *response = NULL;
    size_t responseLen = 0;
    
    while (1) {
        int bytes = recv(proxySock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0)
            break;
        
        buffer[bytes] = '\0';
        
        char *bodyStart = strstr(buffer, "\r\n\r\n");
        if (bodyStart) {
            bodyStart += 4;
            int bodyLen = bytes - (int)(bodyStart - buffer);
            
            char *newResp = realloc(response, responseLen + bodyLen + 1);
            if (newResp) {
                response = newResp;
                memcpy(response + responseLen, bodyStart, bodyLen);
                responseLen += bodyLen;
                response[responseLen] = '\0';
            }
            
            const char *cl = strstr(buffer, "Content-Length:");
            if (cl) {
                cl += 15;
                while (*cl == ' ' || *cl == '\t') cl++;
                int contentLen = atoi(cl);
                
                char *headerEnd = strstr(buffer, "\r\n\r\n");
                if (headerEnd) {
                    int totalHeaderLen = (int)(headerEnd - buffer) + 4;
                    int receivedLen = bytes;
                    
                    if (receivedLen >= totalHeaderLen + contentLen)
                        break;
                }
            }
            
            if (strstr(buffer, "data:") == NULL && bodyLen > 0)
                break;
        }
    }
    
    closesocket(proxySock);
    WSACleanup();
    
    if (response && responseLen > 0) {
        SendHttpResponse(s, 200, "application/json", response, (int)responseLen);
        free(response);
    } else {
        SendErrorResponse(s, 500, "Failed to get response from model");
    }
}

static int ParseHttpRequest(SOCKET s, HttpRequest *req)
{
    char buffer[16384];
    int bytes;
    
    memset(req, 0, sizeof(HttpRequest));
    
    bytes = recv(s, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0)
        return -1;
    
    buffer[bytes] = '\0';
    
    char *lineEnd = strstr(buffer, "\r\n");
    if (!lineEnd)
        return -1;
    
    int reqLineLen = (int)(lineEnd - buffer);
    if (reqLineLen >= sizeof(req->method) + sizeof(req->path) + 4)
        return -1;
    
    char reqLine[1024];
    lstrcpynA(reqLine, buffer, sizeof(reqLine));
    
    char *method = reqLine;
    char *path = strchr(method, ' ');
    if (!path)
        return -1;
    *path = '\0';
    path++;
    
    char *version = strchr(path, ' ');
    if (version) {
        *version = '\0';
    }
    
    lstrcpynA(req->method, method, sizeof(req->method));
    lstrcpynA(req->path, path, sizeof(req->path));
    
    char *query = strchr(req->path, '?');
    if (query) {
        *query = '\0';
        query++;
        lstrcpynA(req->query, query, sizeof(req->query));
    }
    
    char *bodyStart = strstr(buffer, "\r\n\r\n");
    if (bodyStart) {
        bodyStart += 4;
        int bodyLen = bytes - (int)(bodyStart - buffer);
        if (bodyLen > 0 && bodyLen < (int)sizeof(req->body)) {
            memcpy(req->body, bodyStart, bodyLen);
            req->body[bodyLen] = '\0';
            req->bodyLen = bodyLen;
        }
    }
    
    const char *cl = strstr(buffer, "Content-Length:");
    if (cl) {
        cl += 15;
        while (*cl == ' ' || *cl == '\t') cl++;
        const char *clEnd = strchr(cl, '\r');
        if (clEnd) {
            int len = (int)(clEnd - cl);
            if (len < (int)sizeof(req->contentType)) {
                memcpy(req->contentType, cl, len);
                req->contentType[len] = '\0';
            }
        }
    }
    
    return 0;
}

static void RouteRequest(SOCKET s, HttpRequest *req)
{
    if (strcmp(req->method, "GET") == 0) {
        if (strcmp(req->path, "/api/tags") == 0)
            HandleGetTags(s);
        else if (strcmp(req->path, "/v1/models") == 0)
            HandleGetV1Models(s);
        else if (strcmp(req->path, "/api/version") == 0)
            HandleVersion(s);
        else if (strcmp(req->path, "/") == 0)
            HandleVersion(s);
        else if (strcmp(req->path, "/api/daemon/shutdown") == 0)
            HandleShutdown(s);
        else
            HandleNotFound(s);
    }
    else if (strcmp(req->method, "POST") == 0) {
        if (strcmp(req->path, "/api/generate") == 0)
            HandlePostGenerate(s, req);
        else if (strcmp(req->path, "/api/chat") == 0)
            HandlePostChat(s, req);
        else if (strcmp(req->path, "/v1/chat/completions") == 0)
            HandlePostV1Chat(s, req);
        else
            HandleNotFound(s);
    }
    else if (strcmp(req->method, "DELETE") == 0) {
        if (strcmp(req->path, "/api/delete") == 0)
            HandleDeleteModel(s);
        else
            HandleNotFound(s);
    }
    else if (strcmp(req->method, "OPTIONS") == 0) {
        char header[512];
        int headerLen = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n");
        send(s, header, headerLen, 0);
    }
    else {
        HandleNotFound(s);
    }
}

static DWORD WINAPI DaemonHttpWorkerThread(LPVOID param)
{
    SOCKET clientSock = (SOCKET)param;
    HttpRequest req;
    
    if (ParseHttpRequest(clientSock, &req) == 0) {
        DaemonLog("[REQUEST] %s %s", req.method, req.path);
        RouteRequest(clientSock, &req);
    }
    
    closesocket(clientSock);
    return 0;
}

static DWORD WINAPI DaemonListenThread(LPVOID param)
{
    SOCKET listenSock = (SOCKET)param;
    HANDLE threads[DAEMON_THREAD_POOL_SIZE];
    int threadCount = 0;
    
    while (sDaemonRunning) {
        struct sockaddr_in clientAddr;
        int addrLen = sizeof(clientAddr);
        
        SOCKET clientSock = accept(listenSock, (struct sockaddr*)&clientAddr, &addrLen);
        
        if (clientSock == INVALID_SOCKET) {
            if (sDaemonRunning)
                Sleep(100);
            continue;
        }
        
        if (threadCount >= DAEMON_THREAD_POOL_SIZE) {
            WaitForMultipleObjects(threadCount, threads, FALSE, 100);
            threadCount = 0;
        }
        
        HANDLE hThread = CreateThread(NULL, 0, DaemonHttpWorkerThread, (LPVOID)clientSock, 0, NULL);
        if (hThread) {
            threads[threadCount++] = hThread;
            CloseHandle(hThread);
        } else {
            closesocket(clientSock);
        }
    }
    
    for (int i = 0; i < threadCount; i++) {
        if (threads[i])
            CloseHandle(threads[i]);
    }
    
    return 0;
}

static int StartDaemonHttpServer(int port)
{
    WSADATA wsa;
    struct sockaddr_in addr;
    int reuse = 1;
    
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
        return -1;
    
    sDaemonListenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (sDaemonListenSocket == INVALID_SOCKET) {
        WSACleanup();
        return -1;
    }
    
    setsockopt(sDaemonListenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sDaemonListenSocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sDaemonListenSocket);
        WSACleanup();
        return -1;
    }
    
    if (listen(sDaemonListenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(sDaemonListenSocket);
        WSACleanup();
        return -1;
    }
    
    sDaemonRunning = TRUE;
    
    HANDLE hListenThread = CreateThread(NULL, 0, DaemonListenThread, (LPVOID)sDaemonListenSocket, 0, NULL);
    if (!hListenThread) {
        closesocket(sDaemonListenSocket);
        WSACleanup();
        return -1;
    }
    
    CloseHandle(hListenThread);
    
    DaemonLog("[INFO] HTTP server started on port %d", port);
    
    return 0;
}

static void StopDaemonHttpServer(void)
{
    sDaemonRunning = FALSE;
    
    if (sDaemonListenSocket != INVALID_SOCKET) {
        shutdown(sDaemonListenSocket, SD_BOTH);
        closesocket(sDaemonListenSocket);
        sDaemonListenSocket = INVALID_SOCKET;
    }
    
    WSACleanup();
}

static int DaemonStatus(void)
{
    if (!IsDaemonRunning()) {
        printf("  Status  : Stopped\n");
        printf("  (Start with: valora daemon start)\n\n");
        fflush(stdout);
        return 1;
    }
    
    DWORD pid = ReadPidFile();
    ULONGLONG uptime = 0;
    
    if (sDaemonStartTime > 0) {
        uptime = (GetTickCount64() - sDaemonStartTime) / 1000;
    }
    
    printf("\n");
    printf("=== Valora Daemon ===\n");
    printf("\n");
    printf("  Status : Running\n");
    printf("  Port   : %d\n", sDaemonPort);
    printf("  PID    : %lu\n", pid);
    
    if (uptime > 0) {
        int hours = (int)(uptime / 3600);
        int mins = (int)((uptime % 3600) / 60);
        int secs = (int)(uptime % 60);
        printf("  Uptime : %dh %dm %ds\n", hours, mins, secs);
    }
    
    printf("\n");
    printf("  Current Model\n");
    printf("  --------------\n");
    
    if (sCurrentDaemonModel[0]) {
        int modelSizeMB = GetModelSizeMB(sCurrentDaemonModel);
        const char *quant = DetectQuantizationType(sCurrentDaemonModel);
        printf("  Name  : %s\n", sCurrentDaemonModel);
        printf("  Type  : Chat (%s)\n", quant);
        printf("  Size  : %d MB\n", modelSizeMB);
    } else {
        printf("  (No model loaded - will load on first request)\n");
    }
    
    printf("\n");
    printf("  System Resources\n");
    printf("  ----------------\n");
    ULONGLONG availRAM = GetAvailableRamMB();
    ULONGLONG totalRAM = GetSystemRamMB();
    ULONGLONG usedRAM = totalRAM - availRAM;
    printf("  RAM   : %llu / %llu MB used\n", usedRAM, totalRAM);
    
    printf("\n\n");
    fflush(stdout);
    
    return 0;
}

static int DaemonStart(int port, BOOL foreground)
{
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD consoleMode = 0;
    if (hConsole != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hConsole, &consoleMode);
        consoleMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT;
        SetConsoleMode(hConsole, consoleMode);
    }
    
    if (IsDaemonRunning()) {
        printf("Valora daemon is already running on port %d\n\n", port);
        fflush(stdout);
        return 0;
    }
    
    if (!foreground) {
        char selfPath[MAX_PATH];
        char args[256];
        
        GetModuleFileNameA(NULL, selfPath, sizeof(selfPath));
        snprintf(args, sizeof(args), "daemon --fg --port %d", port);
        
        SHELLEXECUTEINFOA sei = {0};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpFile = selfPath;
        sei.lpParameters = args;
        sei.nShow = SW_HIDE;
        
        if (!ShellExecuteExA(&sei)) {
            fprintf(stderr, "Failed to start daemon process\n\n");
            fflush(stderr);
            return 1;
        }
        
        DWORD newPid = GetProcessId(sei.hProcess);
        CloseHandle(sei.hProcess);
        
        WritePidFile(newPid);
        
        Sleep(1000);
        
        printf("\n[*] Valora daemon started\n");
        printf("    Port : %d\n", port);
        printf("    PID  : %lu\n", newPid);
        printf("    Logs : %%APPDATA%%\\Valora\\daemon.log\n");
        printf("\n\n");
        fflush(stdout);
        
        return 0;
    }
    
    return RunDaemonLoop(port);
}

static int DaemonStop(void)
{
    DWORD pid = ReadPidFile();
    int port = sDaemonPort;
    
    if (pid == 0) {
        printf("Daemon PID file not found. Is the daemon running?\n");
        fflush(stdout);
        return 1;
    }
    
    printf("\n[*] Stopping daemon (PID: %lu)...\n", pid);
    fflush(stdout);
    
    SOCKET stopSock;
    struct sockaddr_in addr;
    WSADATA wsd;
    char buffer[1024];
    
    if (WSAStartup(MAKEWORD(2,2), &wsd) == 0) {
        stopSock = socket(AF_INET, SOCK_STREAM, 0);
        if (stopSock != INVALID_SOCKET) {
            addr.sin_family = AF_INET;
            addr.sin_port = htons((u_short)port);
            addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            
            if (connect(stopSock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                char req[256];
                int reqLen = snprintf(req, sizeof(req),
                    "GET /api/daemon/shutdown HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n", port);
                send(stopSock, req, reqLen, 0);
                
                recv(stopSock, buffer, sizeof(buffer), 0);
            }
            
            closesocket(stopSock);
        }
        WSACleanup();
    }
    
    Sleep(500);
    
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        CloseHandle(hProc);
        
        printf("[*] Graceful stop failed, forcing termination...\n");
        fflush(stdout);
        KillProcessTree(pid);
    }
    
    DeletePidFile();
    
    printf("[*] Daemon stopped.\n\n\n");
    fflush(stdout);
    
    return 0;
}

static int DaemonRestart(int port)
{
    DaemonStop();
    Sleep(1000);
    return DaemonStart(port, FALSE);
}

static int RunDaemonLoop(int port)
{
    char ctx[32], gpu[32], threads[32];
    
    sDaemonPort = port;
    sDaemonStartTime = GetTickCount64();
    
    DaemonLogOpen();
    DaemonLog("[INFO] ===============================================");
    DaemonLog("[INFO] Valora Daemon starting on port %d", port);
    
    if (!LoadConfigFromDisk()) {
        DaemonLog("[ERROR] Failed to load config");
        fprintf(stderr, "Valora is not configured. Run 'valora setup' first.\n\n");
        fflush(stderr);
        DaemonLogClose();
        return 1;
    }
    
    if (!sServer[0] || !sFolder[0]) {
        DaemonLog("[ERROR] Server or models folder not configured");
        fprintf(stderr, "Server or models folder not configured.\n\n");
        fflush(stderr);
        DaemonLogClose();
        return 1;
    }
    
    sDaemonModelMutex = CreateMutex(NULL, FALSE, NULL);
    
    GetConfiguredServerValues(ctx, sizeof(ctx), gpu, sizeof(gpu), NULL, 0, threads, sizeof(threads), NULL, 0);
    
    DaemonLog("[INFO] Config loaded - Server: %s", sServer);
    DaemonLog("[INFO] Models folder: %s", sFolder);
    
    if (StartDaemonHttpServer(port) != 0) {
        DaemonLog("[ERROR] Failed to start HTTP server on port %d", port);
        fprintf(stderr, "Failed to start HTTP server on port %d\n\n", port);
        fflush(stderr);
        if (sDaemonModelMutex) CloseHandle(sDaemonModelMutex);
        DaemonLogClose();
        return 1;
    }
    
    DaemonLog("[INFO] Daemon ready and listening on 0.0.0.0:%d", port);
    
    printf("\n=== Valora Daemon ===\n");
    printf("\n");
    printf("  Port : %d\n", port);
    printf("  APIs :\n");
    printf("         GET  /api/tags            List models\n");
    printf("         POST /api/chat            Chat\n");
    printf("         POST /v1/chat/completions OpenAI compat\n");
    printf("\n");
    printf("  Logs : %%APPDATA%%\\Valora\\daemon.log\n");
    printf("\n");
    printf("Press Ctrl+C to stop.\n");
    printf("\n");
    fflush(stdout);
    
    while (sDaemonRunning) {
        Sleep(100);
    }
    
    DaemonLog("[INFO] Shutting down...");
    
    StopDaemonHttpServer();
    
    WaitForSingleObject(sDaemonModelMutex, INFINITE);
    DaemonUnloadModel();
    ReleaseMutex(sDaemonModelMutex);
    
    if (sDaemonModelMutex) CloseHandle(sDaemonModelMutex);
    
    DaemonLog("[INFO] Daemon stopped");
    DaemonLogClose();
    
    return 0;
}

static int RunDaemonCommand(int argc, char **argv)
{
    int i;
    int port = DAEMON_DEFAULT_PORT;
    BOOL foreground = FALSE;
    BOOL showStatus = FALSE;
    BOOL showLog = FALSE;
    int logLines = 50;
    
    AttachConsoleStreams(TRUE);
    
    for (i = 2; i < argc; i++) {
        if (lstrcmpiA(argv[i], "start") == 0) {
        }
        else if (lstrcmpiA(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
        else if (lstrcmpiA(argv[i], "--fg") == 0) {
            foreground = TRUE;
        }
        else if (lstrcmpiA(argv[i], "--foreground") == 0) {
            foreground = TRUE;
        }
        else if (lstrcmpiA(argv[i], "stop") == 0) {
            return DaemonStop();
        }
        else if (lstrcmpiA(argv[i], "status") == 0) {
            return DaemonStatus();
        }
        else if (lstrcmpiA(argv[i], "restart") == 0) {
            return DaemonRestart(port);
        }
        else if (lstrcmpiA(argv[i], "log") == 0) {
            showLog = TRUE;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                logLines = atoi(argv[++i]);
                if (logLines <= 0) logLines = 50;
            }
        }
        else if (lstrcmpiA(argv[i], "--log-lines") == 0 && i + 1 < argc) {
            logLines = atoi(argv[++i]);
            showLog = TRUE;
        }
        else {
            fprintf(stderr, "Unknown daemon command: %s\n", argv[i]);
            fprintf(stderr, "Usage: valora daemon [start|stop|status|restart|log]\n");
            fflush(stderr);
            return 1;
        }
    }
    
    if (showLog) {
        return DaemonLogPrint(logLines);
    }
    
    if (showStatus) {
        return DaemonStatus();
    }
    
    return DaemonStart(port, foreground);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    AttachConsoleStreams(argc > 1);
    
    if (argc > 1 && lstrcmpiA(argv[1], "daemon") == 0) {
        return RunDaemonCommand(argc, argv);
    }
    
    if (argc > 2 && lstrcmpiA(argv[1], "serve") == 0 && lstrcmpiA(argv[2], "--daemon") == 0) {
        return RunDaemonCommand(argc, argv);
    }
    
    return RunCli(argc, argv);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    int argc;
    char **argv;

    (void)hInst;
    (void)hPrev;
    (void)lpCmd;
    (void)nShow;

    argv = __argv;
    argc = __argc;
    AttachConsoleStreams(argc > 1);
    return RunCli(argc, argv);
}
