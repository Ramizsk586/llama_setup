/*
 * OpenWebUI Setup Center
 * Modern dark Win32 UI - Redesigned
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <wininet.h>

#define IDI_ICON1 101
#define VALORA_APP_DIR "Valora"
#define VALORA_CONFIG_NAME "config.ini"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

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
#define APP_W       820
#define APP_H       620
#define CARD_MAX_W  660
#define HEADER_H    88
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
static int  nModels = 0;
static char sCommand[MAX_CMD] = "";
static char sConfigPath[MAX_PATH] = "";
static int  nServerType = 0;

/* GPU auto-detection state */
static BOOL bGpuFieldEdited = FALSE;     // Track if user manually edited GPU field
static int  nLastDetectedGpu = -1;       // Store last auto-detected value

/* ──────────────────────────────────────────────
   Handles
   ────────────────────────────────────────────── */
static HWND hServerEdit, hFolderEdit, hModelCombo;
static HWND hCtxEdit, hGpuEdit, hPortEdit, hThreadsEdit;
static HWND hServerTypeCombo, hCommandTypeCombo;
static HWND hOutputEdit;
static HWND hBtnServer, hBtnFolder, hBtnPrev, hBtnNext, hBtnGenerate, hBtnCopy;
// NEW: Static controls for labels so they align perfectly
static HWND hLblServer, hLblFolder, hLblModel, hLblCtx, hLblGpu, hLblPort, hLblThreads, hLblServerType, hLblCommandType;

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
static BOOL NormalizeHuggingFaceRepo(const char *input, char *repo, int repoLen);
static BOOL WriteHuggingFaceDownloaderScript(char *scriptPath, int scriptPathLen);
static int EnsureModelsFolderReady(void);

/* HuggingFace model download */
static int DownloadFromHuggingFace(const char *hfUrl);

/* ──────────────────────────────────────────────
   Utility
   ────────────────────────────────────────────── */
static void SetCtlFont(HWND hWnd, HFONT hFont)
{
    if (hWnd) SendMessageA(hWnd, WM_SETFONT, (WPARAM)hFont, FALSE);
}

static void AttachConsoleStreams(void)
{
    FILE *dummy;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!AllocConsole())
            return;
    }

    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    freopen_s(&dummy, "CONIN$", "r", stdin);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}

static void OpenOwnedConsole(const char *title)
{
    FreeConsole();
    if (!AllocConsole())
        return;

    if (title && *title)
        SetConsoleTitleA(title);

    AttachConsoleStreams();
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

    WritePrivateProfileStringA("paths", "server", sServer, sConfigPath);
    WritePrivateProfileStringA("paths", "models_folder", sFolder, sConfigPath);
    WritePrivateProfileStringA("paths", "default_model", sSelectedModel, sConfigPath);
    WritePrivateProfileStringA("settings", "ctx", ctx, sConfigPath);
    WritePrivateProfileStringA("settings", "gpu", gpu, sConfigPath);
    WritePrivateProfileStringA("settings", "port", port, sConfigPath);
    WritePrivateProfileStringA("settings", "threads", threads, sConfigPath);
    WritePrivateProfileStringA("settings", "server_type", serverType, sConfigPath);

    return TRUE;
}

static BOOL LoadConfigFromDisk(void)
{
    if (!BuildConfigPath(sConfigPath, sizeof(sConfigPath)))
        return FALSE;

    GetPrivateProfileStringA("paths", "server", "", sServer, sizeof(sServer), sConfigPath);
    GetPrivateProfileStringA("paths", "models_folder", "", sFolder, sizeof(sFolder), sConfigPath);
    GetPrivateProfileStringA("paths", "default_model", "", sSelectedModel, sizeof(sSelectedModel), sConfigPath);
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

static void GetConfiguredServerValues(char *ctx, int ctxLen, char *gpu, int gpuLen, char *port, int portLen, char *threads, int threadsLen)
{
    if (ctx && ctxLen > 0) GetPrivateProfileStringA("settings", "ctx", "2048", ctx, ctxLen, sConfigPath);
    if (gpu && gpuLen > 0) GetPrivateProfileStringA("settings", "gpu", "-1", gpu, gpuLen, sConfigPath);
    if (port && portLen > 0) GetPrivateProfileStringA("settings", "port", "8000", port, portLen, sConfigPath);
    if (threads && threadsLen > 0) GetPrivateProfileStringA("settings", "threads", "4", threads, threadsLen, sConfigPath);
}

/* Check if a model has saved configuration */
static BOOL HasModelConfig(const char *modelName)
{
    char section[64];
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
    char section[64];

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

/* Get available VRAM in MB */
static int GetAvailableVRAM(void)
{
    /* Try to get GPU memory from WMI */
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    
    if (GlobalMemoryStatusEx(&statex)) {
        ULONGLONG availMB = statex.ullAvailPhys / (1024 * 1024);
        if (availMB > 8192) return 16384;
        if (availMB > 4096) return 8192;
        if (availMB > 2048) return 4096;
        if (availMB > 1024) return 2048;
        return 1024;
    }
    return 2048;
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
static void PrintAnimation(const char *message)
{
    printf("\r%s %s", animationFrames[animFrame], message);
    fflush(stdout);
    animFrame = (animFrame + 1) % 10;
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
    printf("\n+----+-----------------------------------------------+------------+----------+----------+\n");
    printf("| %2s | %-45s | %10s | %8s | %8s |\n", "#", "Model", "Size", "Quant", "Status");
    printf("+----+-----------------------------------------------+------------+----------+----------+\n");
}

static void PrintTableRowAt(int rowIndex, int selectedIndex, const char *modelName, int sizeMB, const char *quant, BOOL usable)
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
    printf(" | %10s | %8.8s | ", sizeStr, quant);
    if (usable) {
        printf("\x1b[32m%8s\x1b[0m", status);
    } else {
        printf("\x1b[33m%8s\x1b[0m", status);
    }
    printf(" |\n");
}

static void PrintTableFooter(void)
{
    printf("+----+-----------------------------------------------+------------+----------+----------+\n");
}

static void RefreshSelectorRow(int rowIndex, int selectedIndex, const int *modelSizes, const BOOL *usableFlags, int totalRows)
{
    int linesUp;
    const char *quant;

    linesUp = totalRows - rowIndex + 1;
    MoveCursorUp(linesUp);
    quant = DetectQuantizationType(sModels[rowIndex]);
    PrintTableRowAt(rowIndex, selectedIndex, sModels[rowIndex], modelSizes[rowIndex], quant, usableFlags[rowIndex]);
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
    availableVRAM = GetAvailableVRAM();
    
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
        PrintTableRowAt(i, selectedIndex, sModels[i], modelSizes[i], quant, usableFlags[i]);
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

static void BuildServeCommand(char *dst, int dstLen)
{
    char modelPath[MAX_PATH * 2];
    char ctx[32], gpu[32], port[32], threads[32];
    const char *host = (nServerType == 1) ? "0.0.0.0" : "127.0.0.1";

    BuildModelPath(modelPath, sizeof(modelPath), sSelectedModel);
    GetConfiguredServerValues(ctx, sizeof(ctx), gpu, sizeof(gpu), port, sizeof(port), threads, sizeof(threads));

    snprintf(
        dst, dstLen,
        "\"%s\" -m \"%s\" -c %s -ngl %s --port %s -t %s --host %s",
        sServer, modelPath, ctx, gpu, port, threads, host
    );
}

static void BuildRunCommand(char *dst, int dstLen, const char *modelName)
{
    char cliPath[MAX_PATH];
    char modelPath[MAX_PATH * 2];
    char ctx[32], gpu[32], threads[32];

    GetCliPathFromServerPath(sServer, cliPath, sizeof(cliPath));
    BuildModelPath(modelPath, sizeof(modelPath), modelName ? modelName : sSelectedModel);
    GetModelConfig(modelName, ctx, sizeof(ctx), gpu, sizeof(gpu), threads, sizeof(threads));

    snprintf(
        dst, dstLen,
        "\"%s\" -m \"%s\" -c %s -ngl %s -t %s",
        cliPath, modelPath, ctx, gpu, threads
    );
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
    fputs("function Trim-Cell([string]$Text, [int]$Width) {\n", fp);
    fputs("  if (-not $Text) { return ''.PadRight($Width) }\n", fp);
    fputs("  if ($Text.Length -le $Width) { return $Text.PadRight($Width) }\n", fp);
    fputs("  if ($Width -le 3) { return $Text.Substring(0, $Width) }\n", fp);
    fputs("  return ($Text.Substring(0, $Width - 3) + '...')\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("function Show-Table($Items) {\n", fp);
    fputs("  $line = '+' + ('-' * 4) + '+' + ('-' * 44) + '+' + ('-' * 12) + '+' + ('-' * 12) + '+' + ('-' * 15) + '+'\n", fp);
    fputs("  Write-Host $line\n", fp);
    fputs("  Write-Host ('| ' + (Trim-Cell 'No' 2) + ' | ' + (Trim-Cell 'Model' 42) + ' | ' + (Trim-Cell 'Size' 10) + ' | ' + (Trim-Cell 'Quant' 10) + ' | ' + (Trim-Cell 'GPU Offload' 13) + ' |')\n", fp);
    fputs("  Write-Host $line\n", fp);
    fputs("  for ($i = 0; $i -lt $Items.Count; $i++) {\n", fp);
    fputs("    $item = $Items[$i]\n", fp);
    fputs("    $row = '| ' + (Trim-Cell ([string]($i + 1)) 2) + ' | ' + (Trim-Cell $item.Name 42) + ' | ' + (Trim-Cell (Format-Bytes $item.Size) 10) + ' | ' + (Trim-Cell $item.Quant 10) + ' | ' + (Trim-Cell $item.Gpu 13) + ' |'\n", fp);
    fputs("    Write-Host $row\n", fp);
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
    fputs("  } else {\n", fp);
    fputs("    $percent = 0\n", fp);
    fputs("    $filled = 0\n", fp);
    fputs("  }\n", fp);
    fputs("  $bar = ('=' * $filled) + (' ' * ($width - $filled))\n", fp);
    fputs("  $left = (Trim-Cell $Label 18)\n", fp);
    fputs("  $elapsedText = [TimeSpan]::FromSeconds([int]$elapsed).ToString('hh\\:mm\\:ss')\n", fp);
    fputs("  $line = ('{0} {1,3}% [{2}] {3,8} {4,8}/s {5}' -f $left, $percent, $bar, (Format-Bytes $Total), (Format-Bytes ([Int64]$speed)), $elapsedText)\n", fp);
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
    fputs("$ggufFiles = @($repoInfo.siblings | Where-Object { $_.rfilename -match '\\.gguf$' })\n", fp);
    fputs("if ($ggufFiles.Count -eq 0) { Write-Host 'No GGUF files found in this repository.' -ForegroundColor Yellow; exit 1 }\n", fp);
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
    fputs("  $files += [PSCustomObject]@{ Name = $name; Url = $downloadUrl; Size = $size; Quant = (Get-Quant $name); Gpu = (Get-GpuOffload $size) }\n", fp);
    fputs("}\n", fp);
    fputs("$files = @($files | Sort-Object Name)\n", fp);
    fputs("$defaultModel = Get-DefaultModel $files\n", fp);
    fputs("$selected = $defaultModel\n", fp);
    fputs("\n", fp);
    fputs("if ($files.Count -gt 1) {\n", fp);
    fputs("  Write-Host ('Found ' + $files.Count + ' GGUF models in this repo.') -ForegroundColor Green\n", fp);
    fputs("  Write-Host ('Default: ' + $defaultModel.Name + ' | ' + (Format-Bytes $defaultModel.Size) + ' | ' + $defaultModel.Quant + ' | GPU offload: ' + $defaultModel.Gpu)\n", fp);
    fputs("  Write-Host ''\n", fp);
    fputs("  Write-Host '1. Continue with default model'\n", fp);
    fputs("  Write-Host '2. Change model'\n", fp);
    fputs("  Write-Host '3. Cancel'\n", fp);
    fputs("  do { $choice = Read-Host 'Choose 1, 2 or 3' } until ($choice -match '^[123]$')\n", fp);
    fputs("  if ($choice -eq '3') { Write-Host 'Cancelled.'; exit 0 }\n", fp);
    fputs("  if ($choice -eq '2') {\n", fp);
    fputs("    Write-Host ''\n", fp);
    fputs("    Show-Table $files\n", fp);
    fputs("    do { $pick = Read-Host 'Choose model number' } until ($pick -match '^\\d+$' -and [int]$pick -ge 1 -and [int]$pick -le $files.Count)\n", fp);
    fputs("    $selected = $files[[int]$pick - 1]\n", fp);
    fputs("  }\n", fp);
    fputs("} else {\n", fp);
    fputs("  Write-Host ('One GGUF model found: ' + $selected.Name + ' | ' + (Format-Bytes $selected.Size) + ' | ' + $selected.Quant + ' | GPU offload: ' + $selected.Gpu) -ForegroundColor Green\n", fp);
    fputs("}\n", fp);
    fputs("\n", fp);
    fputs("$targetPath = Join-Path $ModelsFolder ([System.IO.Path]::GetFileName($selected.Name))\n", fp);
    fputs("if (Test-Path -LiteralPath $targetPath) {\n", fp);
    fputs("  do { $overwrite = Read-Host 'File already exists. Overwrite? (y/n)' } until ($overwrite -match '^[YyNn]$')\n", fp);
    fputs("  if ($overwrite -match '^[Nn]$') { Write-Host 'Cancelled.'; exit 0 }\n", fp);
    fputs("}\n", fp);
    fputs("Write-Host ''\n", fp);
    fputs("Write-Host ('Downloading ' + $selected.Name) -ForegroundColor Cyan\n", fp);
    fputs("Write-Host ('Saving to ' + $targetPath)\n", fp);
    fputs("Write-Host 'Download progress:' -ForegroundColor DarkGray\n", fp);
    fputs("Download-WithProgress $selected.Url $targetPath\n", fp);
    fputs("Write-Host ''\n", fp);
    fputs("Write-Host ('Installed model to ' + $targetPath) -ForegroundColor Green\n", fp);

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
        printf("%s %s\n", lstrcmpiA(sModels[i], sSelectedModel) == 0 ? "*" : "-", sModels[i]);
    }
    fflush(stdout);
    return 0;
}

static int PrintUsage(void)
{
    printf("Valora commands:\n");
    printf("  valora setup   Open the GUI setup window\n");
    printf("  valora list    Alias for valora models\n");
    printf("  valora models  List configured models\n");
    printf("  valora run     Run the saved model in llama-cli\n");
    printf("  valora serve [model]  Start a model in llama-server (optional model name)\n");
    printf("  valora get <owner/repo|hf_url>  Download a GGUF model from Hugging Face\n");
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
        
        /* Get available VRAM */
        int vramMB = GetAvailableVRAM();
        
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
    ShowWindow(hCommandTypeCombo, SW_SHOW);
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
   Model scanning
   ────────────────────────────────────────────── */
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

    char pattern[MAX_PATH * 2];
    snprintf(pattern, sizeof(pattern), "%s\\*.gguf", sFolder);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (nModels < MAX_MODELS) {
                lstrcpynA(sModels[nModels], fd.cFileName, MAX_PATH);
                if (hModelCombo)
                    SendMessageA(hModelCombo, CB_ADDSTRING, 0, (LPARAM)sModels[nModels]);
                nModels++;
            }
        }
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);

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
    int cmdTypeIdx = (int)SendMessageA(hCommandTypeCombo, CB_GETCURSEL, 0, 0);
    int isCliMode = (cmdTypeIdx == 1);

    if (!sServer[0] && hServerEdit)
        GetWindowTextA(hServerEdit, sServer, sizeof(sServer));
    if (!sFolder[0] && hFolderEdit)
        GetWindowTextA(hFolderEdit, sFolder, sizeof(sFolder));

    if (idx == CB_ERR) {
        MessageBoxA(hwnd, "No model selected. Browse a model folder first.", "Setup incomplete", MB_ICONERROR);
        return;
    }

    if (!sServer[0]) {
        MessageBoxA(hwnd, "Select the server executable first.", "Setup incomplete", MB_ICONERROR);
        return;
    }

    if (!sFolder[0]) {
        MessageBoxA(hwnd, "Select the model folder first.", "Setup incomplete", MB_ICONERROR);
        return;
    }

    SendMessageA(hModelCombo, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)sSelectedModel);
    if (!SaveConfigToDisk()) {
        MessageBoxA(hwnd, "Could not save the Valora configuration.", "Save failed", MB_ICONERROR);
        return;
    }

    snprintf(
        sCommand, sizeof(sCommand),
        "valora models\r\nvalora run\r\nvalora serve\r\n\r\nCurrent quick command: %s",
        isCliMode ? "valora run" : "valora serve"
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

    int cardW = (CARD_MAX_W < (W - 2 * PAD)) ? CARD_MAX_W : (W - 2 * PAD);
    if (cardW < 440) cardW = W - 2 * PAD;
    int cardH = H - HEADER_H - FOOTER_H - 12;

    int cardX = (W - cardW) / 2;
    int cardY = HEADER_H + 12;

    int innerX = cardX + 28;
    int innerW = cardW - 56;
    int rowH = 28;
    int labelH = 16;    
    int gapY = 11;
    int colW = (innerW - 12) / 2;
    int y = cardY + 22;

    // Server Type (new option)
    MoveWindow(hLblServerType, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hServerTypeCombo, innerX, y, innerW, 100, TRUE);
    y += rowH + gapY + 4;

    // Command Type (Server vs CLI)
    MoveWindow(hLblCommandType, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hCommandTypeCombo, innerX, y, innerW, 100, TRUE);
    y += rowH + gapY + 4;

    // Server Executable
    MoveWindow(hLblServer, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hServerEdit, innerX, y, innerW - 98, rowH, TRUE);
    MoveWindow(hBtnServer,  innerX + innerW - 88, y - 1, 88, rowH + 2, TRUE);
    y += rowH + gapY;

    // Model Folder
    MoveWindow(hLblFolder, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hFolderEdit, innerX, y, innerW - 98, rowH, TRUE);
    MoveWindow(hBtnFolder,  innerX + innerW - 88, y - 1, 88, rowH + 2, TRUE);
    y += rowH + gapY;

    // Model Combo (Height is dropdown height)
    MoveWindow(hLblModel, innerX, y, innerW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hModelCombo, innerX, y, innerW, 100, TRUE);
    y += rowH + gapY + 4;

    // Parameters in 2-column grid
    // Left column: Context Size
    MoveWindow(hLblCtx, innerX, y, colW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hCtxEdit, innerX, y, colW, rowH, TRUE);
    
    // Right column: GPU Layers (at same y level)
    int rightColX = innerX + colW + 12;
    MoveWindow(hLblGpu, rightColX, y - labelH - 2, colW, labelH, TRUE);
    MoveWindow(hGpuEdit, rightColX, y, colW, rowH, TRUE);
    y += rowH + gapY;

    // Left column: Port
    MoveWindow(hLblPort, innerX, y, colW, labelH, TRUE);
    y += labelH + 2;
    MoveWindow(hPortEdit, innerX, y, colW, rowH, TRUE);
    
    // Right column: Threads (at same y level)
    MoveWindow(hLblThreads, rightColX, y - labelH - 2, colW, labelH, TRUE);
    MoveWindow(hThreadsEdit, rightColX, y, colW, rowH, TRUE);
    y += rowH + 20;

    // Generate button - positioned with more spacing
    MoveWindow(hBtnNext, innerX, y, innerW, 38, TRUE);
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

    DrawHeader(hdc, rc);
    DrawCardFrame(hdc, rc);
    
    // Labels are now drawn via standard STATIC controls to ensure 
    // mathematically perfect alignment with the layout engine.

    EndPaint(hwnd, &ps);
}

/* ──────────────────────────────────────────────
   Control creation
   ────────────────────────────────────────────── */
static void CreateControls(HWND hwnd)
{
    HINSTANCE hi = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);

    // Labels
    hLblServerType = CreateWindowA("STATIC", "Server Type", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblCommandType = CreateWindowA("STATIC", "Command Type", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblServer = CreateWindowA("STATIC", "Server Executable File", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblFolder = CreateWindowA("STATIC", "Models Directory", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblModel  = CreateWindowA("STATIC", "Target Model", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblCtx    = CreateWindowA("STATIC", "Context Size", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblGpu    = CreateWindowA("STATIC", "GPU Layers", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblPort   = CreateWindowA("STATIC", "Port", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);
    hLblThreads = CreateWindowA("STATIC", "Threads", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, NULL, hi, NULL);

    // Server Type Combo
    hServerTypeCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);
    SendMessageA(hServerTypeCombo, CB_ADDSTRING, 0, (LPARAM)"Local - same device only");
    SendMessageA(hServerTypeCombo, CB_ADDSTRING, 0, (LPARAM)"LAN/IP - same Wi-Fi network");
    SendMessageA(hServerTypeCombo, CB_SETCURSEL, 0, 0);

    // Command Type Combo
    hCommandTypeCombo = CreateWindowA("COMBOBOX", "",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);
    SendMessageA(hCommandTypeCombo, CB_ADDSTRING, 0, (LPARAM)"Llama Server");
    SendMessageA(hCommandTypeCombo, CB_ADDSTRING, 0, (LPARAM)"Llama CLI");
    SendMessageA(hCommandTypeCombo, CB_SETCURSEL, 0, 0);

    // Inputs
    hServerEdit = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hBtnServer = CreateWindowA("BUTTON", "Browse",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_SERVER, hi, NULL);

    hFolderEdit = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL | WS_BORDER,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

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

    /* Output controls */
    hOutputEdit = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL | WS_HSCROLL,
        0, 0, 0, 0, hwnd, NULL, hi, NULL);

    hBtnPrev = CreateWindowA("BUTTON", "Previous",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_PREV, hi, NULL);

    hBtnNext = CreateWindowA("BUTTON", "Save Setup",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_NEXT, hi, NULL);

    hBtnGenerate = CreateWindowA("BUTTON", "Generate",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_GENERATE, hi, NULL);

    hBtnCopy = CreateWindowA("BUTTON", "Copy",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 0, 0, hwnd, (HMENU)ID_BTN_COPY, hi, NULL);

    // Apply Fonts
    HWND ctrls[] = { hServerTypeCombo, hCommandTypeCombo, hServerEdit, hFolderEdit, hModelCombo, hCtxEdit, hGpuEdit, hPortEdit, hThreadsEdit, hOutputEdit };
    for (int i = 0; i < (int)(sizeof(ctrls) / sizeof(ctrls[0])); i++) SetCtlFont(ctrls[i], hFontBody);

    HWND labels[] = { hLblServerType, hLblCommandType, hLblServer, hLblFolder, hLblModel, hLblCtx, hLblGpu, hLblPort, hLblThreads };
    for (int i = 0; i < (int)(sizeof(labels) / sizeof(labels[0])); i++) SetCtlFont(labels[i], hFontLabel);

    SetCtlFont(hBtnServer, hFontBody);
    SetCtlFont(hBtnFolder, hFontBody);
    SetCtlFont(hBtnNext, hFontBold);

    ShowOnlyOnPage(hwnd);
    LayoutControls(hwnd);
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
    if (argc <= 1) {
        HideStandaloneConsoleWindow();
        return RunGui(GetModuleHandleA(NULL), SW_SHOWDEFAULT);
    }

    if (lstrcmpiA(argv[1], "setup") == 0) {
        HideStandaloneConsoleWindow();
        return RunGui(GetModuleHandleA(NULL), SW_SHOWDEFAULT);
    }

    AttachConsoleStreams();

    if (lstrcmpiA(argv[1], "models") == 0 || lstrcmpiA(argv[1], "list") == 0) {
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
        if (EnsureCliConfigReady(TRUE) != 0)
            return 1;

        /* Check if model name provided as argument */
        if (argc > 2 && argv[2][0]) {
            /* Use provided model name */
            lstrcpynA(sSelectedModel, argv[2], sizeof(sSelectedModel));
        } else {
            /* Show interactive model selector */
            char selectedModel[MAX_PATH];
            if (RunInteractiveModelSelector(selectedModel, sizeof(selectedModel)) < 0) {
                printf("Model selection cancelled.\n");
                return 0;
            }
            lstrcpynA(sSelectedModel, selectedModel, sizeof(sSelectedModel));
        }

        BuildRunCommand(sCommand, sizeof(sCommand), NULL);
        return RunChildProcess(sCommand);
    }

    if (lstrcmpiA(argv[1], "serve") == 0 || lstrcmpiA(argv[1], "server") == 0) {
        if (EnsureCliConfigReady(FALSE) != 0)
            return 1;

        /* Check if model name provided as argument */
        if (argc > 2 && argv[2][0]) {
            /* Use provided model name */
            lstrcpynA(sSelectedModel, argv[2], sizeof(sSelectedModel));
        } else {
            /* Show interactive model selector */
            char selectedModel[MAX_PATH];
            if (RunInteractiveModelSelector(selectedModel, sizeof(selectedModel)) < 0) {
                printf("Model selection cancelled.\n");
                return 0;
            }
            lstrcpynA(sSelectedModel, selectedModel, sizeof(sSelectedModel));
        }

        BuildServeCommand(sCommand, sizeof(sCommand));
        return RunChildProcess(sCommand);
    }

    if (lstrcmpiA(argv[1], "get") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: valora get <huggingface_url>\n");
            fprintf(stderr, "Example: valora get LiquidAI/LFM2.5-1.2B-Instruct-GGUF\n");
            return 1;
        }
        return DownloadFromHuggingFace(argv[2]);
    }

    return PrintUsage();
}

int main(int argc, char **argv)
{
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
    return RunCli(argc, argv);
}
