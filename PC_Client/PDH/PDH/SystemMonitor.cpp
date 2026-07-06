#include "SystemMonitor.h"

// Приховані структури ядра ОС
#define SystemProcessorPerformanceInformation 8

typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
} SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

typedef NTSTATUS(WINAPI* PROCNTQUERYSYSTEMINFORMATION)(
    UINT SystemInformationClass, PVOID SystemInformation,
    ULONG SystemInformationLength, PULONG ReturnLength);

// Конвертація системного часу
static uint64_t ConvertFileTime(const FILETIME& ft) {
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart;
}

// API NVIDIA (NVML)
namespace {
    typedef struct { unsigned int gpu; unsigned int memory; } nvmlUtilization_t;
    typedef int (*nvmlInit_t)();
    typedef int (*nvmlShutdown_t)();
    typedef int (*nvmlDeviceGetCount_t)(unsigned int*);
    typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);
    typedef int (*nvmlDeviceGetUtilizationRates_t)(void*, nvmlUtilization_t*);

    nvmlInit_t p_nvmlInit = nullptr;
    nvmlShutdown_t p_nvmlShutdown = nullptr;
    nvmlDeviceGetCount_t p_nvmlDeviceGetCount = nullptr;
    nvmlDeviceGetHandleByIndex_t p_nvmlDeviceGetHandleByIndex = nullptr;
    nvmlDeviceGetUtilizationRates_t p_nvmlDeviceGetUtilizationRates = nullptr;
}

SystemMonitor::SystemMonitor() {
    // Лічильник продуктивності відеопам'яті
    PdhOpenQuery(nullptr, 0, &vramQuery);

    // Конфігурація процесора
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    numLogicalCores = sysInfo.dwNumberOfProcessors;
    lastCoreLoads.resize(numLogicalCores, 0.0);
    prevCoreIdle.resize(numLogicalCores, 0);
    prevCoreKernel.resize(numLogicalCores, 0);
    prevCoreUser.resize(numLogicalCores, 0);

    // Початковий стан процесора
    FILETIME idle, kernel, user;
    GetSystemTimes(&idle, &kernel, &user);
    prevIdleTime = ConvertFileTime(idle);
    prevKernelTime = ConvertFileTime(kernel);
    prevUserTime = ConvertFileTime(user);

    // Ініціалізація GPU
    hNvmlDll = LoadLibraryW(L"nvml.dll");
    if (!hNvmlDll) hNvmlDll = LoadLibraryW(L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");

    bool nvmlInitialized = false;
    if (hNvmlDll) {
        p_nvmlInit = (nvmlInit_t)GetProcAddress(hNvmlDll, "nvmlInit_v2");
        p_nvmlShutdown = (nvmlShutdown_t)GetProcAddress(hNvmlDll, "nvmlShutdown");
        p_nvmlDeviceGetCount = (nvmlDeviceGetCount_t)GetProcAddress(hNvmlDll, "nvmlDeviceGetCount_v2");
        p_nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(hNvmlDll, "nvmlDeviceGetHandleByIndex_v2");
        p_nvmlDeviceGetUtilizationRates = (nvmlDeviceGetUtilizationRates_t)GetProcAddress(hNvmlDll, "nvmlDeviceGetUtilizationRates");

        if (p_nvmlInit && p_nvmlInit() == 0) nvmlInitialized = true;
    }

    IDXGIFactory* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory))) {
        IDXGIAdapter* pAdapter;
        UINT i = 0;
        int nvidiaDeviceIndex = 0;

        while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC desc;
            pAdapter->GetDesc(&desc);
            std::wstring ws(desc.Description);

            if (desc.DedicatedVideoMemory > 0 && ws.find(L"Microsoft Basic") == std::wstring::npos) {
                GpuInfo info;
                info.name = std::string(ws.begin(), ws.end());
                info.luid = desc.AdapterLuid;
                info.dedicatedVramBytes = desc.DedicatedVideoMemory;

                if (vramQuery) {
                    wchar_t counterPath[256];
                    swprintf_s(counterPath, L"\\GPU Adapter Memory(luid_0x%08x_0x%08x_phys_0)\\Dedicated Usage",
                        (uint32_t)desc.AdapterLuid.HighPart, desc.AdapterLuid.LowPart);
                    PdhAddEnglishCounterW(vramQuery, counterPath, 0, &info.hCounterVram);
                }

                info.isNvidia = (ws.find(L"NVIDIA") != std::wstring::npos);
                if (info.isNvidia && nvmlInitialized) {
                    unsigned int count = 0;
                    p_nvmlDeviceGetCount(&count);
                    if (nvidiaDeviceIndex < count) {
                        p_nvmlDeviceGetHandleByIndex(nvidiaDeviceIndex, &info.nvmlDeviceHandle);
                        nvidiaDeviceIndex++;
                    }
                }

                pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&info.pAdapter3);
                gpus.push_back(info);
            }
            pAdapter->Release();
            i++;
        }
        pFactory->Release();
    }

    // Ініціалізація дисків
    for (int i = 0; i < 4; i++) {
        std::wstring drivePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);
        HANDLE hDevice = CreateFileW(drivePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hDevice != INVALID_HANDLE_VALUE) {
            DiskInfo di;
            di.name = "PhysicalDrive " + std::to_string(i);
            disks.push_back(di);
            CloseHandle(hDevice);
        }
    }

    // Початковий час циклу
    lastUpdateTime = std::chrono::steady_clock::now();
}

SystemMonitor::~SystemMonitor() {
    // Очищення ресурсів
    if (vramQuery) PdhCloseQuery(vramQuery);
    for (auto& gpu : gpus) {
        if (gpu.pAdapter3) gpu.pAdapter3->Release();
    }
    if (p_nvmlShutdown) p_nvmlShutdown();
    if (hNvmlDll) FreeLibrary(hNvmlDll);
}

void SystemMonitor::Update() {
    auto currentTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = currentTime - lastUpdateTime;

    // Обмеження частоти оновлення (0.5с)
    if (elapsed.count() < 0.5) return;

    // Синхронізація потоків
    std::lock_guard<std::mutex> lock(dataMutex);

    // Опитування лічильників
    if (vramQuery) PdhCollectQueryData(vramQuery);

    // Статистика CPU (Загальна)
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        uint64_t currentIdle = ConvertFileTime(idle);
        uint64_t currentKernel = ConvertFileTime(kernel);
        uint64_t currentUser = ConvertFileTime(user);

        uint64_t sysIdle = currentIdle - prevIdleTime;
        uint64_t sysKernel = currentKernel - prevKernelTime;
        uint64_t sysUser = currentUser - prevUserTime;

        uint64_t sysTotal = sysKernel + sysUser;
        if (sysTotal > 0) lastCpuTotal = ((sysTotal - sysIdle) * 100.0) / sysTotal;

        prevIdleTime = currentIdle;
        prevKernelTime = currentKernel;
        prevUserTime = currentUser;
    }

    // Статистика CPU (Поядерна)
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto NtQuerySystemInformation = (PROCNTQUERYSYSTEMINFORMATION)GetProcAddress(ntdll, "NtQuerySystemInformation");
        if (NtQuerySystemInformation) {
            std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> coreInfo(numLogicalCores);
            ULONG returnLength = 0;

            if (NtQuerySystemInformation(SystemProcessorPerformanceInformation, coreInfo.data(), (ULONG)(coreInfo.size() * sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)), &returnLength) == 0) {
                for (int i = 0; i < numLogicalCores; i++) {
                    uint64_t cIdle = coreInfo[i].IdleTime.QuadPart;
                    uint64_t cKernel = coreInfo[i].KernelTime.QuadPart;
                    uint64_t cUser = coreInfo[i].UserTime.QuadPart;

                    uint64_t diffIdle = cIdle - prevCoreIdle[i];
                    uint64_t total = (cKernel - prevCoreKernel[i]) + (cUser - prevCoreUser[i]);

                    if (total > 0) lastCoreLoads[i] = ((total - diffIdle) * 100.0) / total;

                    prevCoreIdle[i] = cIdle; prevCoreKernel[i] = cKernel; prevCoreUser[i] = cUser;
                }
            }
        }
    }

    // Статистика RAM
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        lastRamPercent = memInfo.dwMemoryLoad;
        ramTotalGB = (double)memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
        ramUsedGB = (double)(memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }

    // Статистика GPU
    for (auto& gpu : gpus) {
        bool loadFound = false;

        if (gpu.isNvidia && gpu.nvmlDeviceHandle && p_nvmlDeviceGetUtilizationRates) {
            nvmlUtilization_t util;
            if (p_nvmlDeviceGetUtilizationRates(gpu.nvmlDeviceHandle, &util) == 0) {
                gpu.lastLoad = (double)util.gpu;
                loadFound = true;
            }
        }

        if (!loadFound) {
            D3DKMT_QUERYSTATISTICS queryStats = {};
            queryStats.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
            queryStats.AdapterLuid = gpu.luid;

            if (D3DKMTQueryStatistics(&queryStats) == 0) {
                UINT nodeCount = queryStats.QueryResult.AdapterInformation.NodeCount;
                if (gpu.prevNodeRunningTime.size() != nodeCount) gpu.prevNodeRunningTime.resize(nodeCount, 0);

                double maxNodeLoad = 0.0;
                for (UINT i = 0; i < nodeCount; i++) {
                    D3DKMT_QUERYSTATISTICS nodeStats = {};
                    nodeStats.Type = D3DKMT_QUERYSTATISTICS_NODE;
                    nodeStats.AdapterLuid = gpu.luid;
                    nodeStats.QueryNode.NodeId = i;

                    if (D3DKMTQueryStatistics(&nodeStats) == 0) {
                        uint64_t currentRunningTime = nodeStats.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
                        if (gpu.prevNodeRunningTime[i] > 0) {
                            double nodeLoad = (double)(currentRunningTime - gpu.prevNodeRunningTime[i]) / (elapsed.count() * 10000000.0);
                            if (nodeLoad > maxNodeLoad) maxNodeLoad = nodeLoad;
                        }
                        gpu.prevNodeRunningTime[i] = currentRunningTime;
                    }
                }
                gpu.lastLoad = (maxNodeLoad > 1.0) ? 100.0 : maxNodeLoad * 100.0;
            }
        }

        if (gpu.hCounterVram) {
            PDH_FMT_COUNTERVALUE counterVal;
            if (PdhGetFormattedCounterValue(gpu.hCounterVram, PDH_FMT_LARGE, nullptr, &counterVal) == ERROR_SUCCESS) {
                gpu.usedVramBytes = counterVal.largeValue;
            }
        }
        else if (gpu.pAdapter3) {
            DXGI_QUERY_VIDEO_MEMORY_INFO memInfo;
            if (SUCCEEDED(gpu.pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
                gpu.usedVramBytes = memInfo.CurrentUsage;
            }
        }
    }

    // Мережева статистика
    ULONG outBufLen = 0;
    GetIfTable(nullptr, &outBufLen, FALSE);
    if (outBufLen > 0) {
        std::vector<uint8_t> buffer(outBufLen);
        MIB_IFTABLE* pIfTable = reinterpret_cast<MIB_IFTABLE*>(buffer.data());

        if (GetIfTable(pIfTable, &outBufLen, FALSE) == NO_ERROR) {
            double maxDownload = 0.0, maxUpload = 0.0;
            for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
                if (pIfTable->table[i].dwPhysAddrLen > 0) {
                    DWORD id = pIfTable->table[i].dwIndex;
                    uint32_t inBytes = pIfTable->table[i].dwInOctets;
                    uint32_t outBytes = pIfTable->table[i].dwOutOctets;

                    if (prevAdapterIn.count(id)) {
                        double currentDl = (inBytes - prevAdapterIn[id]) / (elapsed.count() * 1024.0 * 1024.0);
                        double currentUl = (outBytes - prevAdapterOut[id]) / (elapsed.count() * 1024.0 * 1024.0);
                        if (currentDl > maxDownload) maxDownload = currentDl;
                        if (currentUl > maxUpload) maxUpload = currentUl;
                    }
                    prevAdapterIn[id] = inBytes;
                    prevAdapterOut[id] = outBytes;
                }
            }
            netDownloadMBps = maxDownload;
            netUploadMBps = maxUpload;
        }
    }

    // Дискова статистика
    for (size_t i = 0; i < disks.size(); i++) {
        std::wstring drivePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);
        HANDLE hDevice = CreateFileW(drivePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hDevice != INVALID_HANDLE_VALUE) {
            DISK_PERFORMANCE diskPerf;
            DWORD bytesReturned;
            if (DeviceIoControl(hDevice, IOCTL_DISK_PERFORMANCE, nullptr, 0, &diskPerf, sizeof(diskPerf), &bytesReturned, nullptr)) {
                if (disks[i].prevRead > 0) {
                    disks[i].readMBps = (diskPerf.BytesRead.QuadPart - disks[i].prevRead) / (elapsed.count() * 1024.0 * 1024.0);
                    disks[i].writeMBps = (diskPerf.BytesWritten.QuadPart - disks[i].prevWrite) / (elapsed.count() * 1024.0 * 1024.0);
                }
                disks[i].prevRead = diskPerf.BytesRead.QuadPart;
                disks[i].prevWrite = diskPerf.BytesWritten.QuadPart;
            }
            CloseHandle(hDevice);
        }
    }

    // Оновлення таймера
    lastUpdateTime = currentTime;
}

// Методи доступу до метрик
int SystemMonitor::GetNumCores() const { std::lock_guard<std::mutex> lock(dataMutex); return numLogicalCores; }
double SystemMonitor::GetCpuTotal() const { std::lock_guard<std::mutex> lock(dataMutex); return lastCpuTotal; }
double SystemMonitor::GetCoreLoad(int index) const { std::lock_guard<std::mutex> lock(dataMutex); return lastCoreLoads[index]; }
double SystemMonitor::GetRamTotalGB() const { std::lock_guard<std::mutex> lock(dataMutex); return ramTotalGB; }
double SystemMonitor::GetRamUsedGB() const { std::lock_guard<std::mutex> lock(dataMutex); return ramUsedGB; }
int SystemMonitor::GetRamPercent() const { std::lock_guard<std::mutex> lock(dataMutex); return lastRamPercent; }
size_t SystemMonitor::GetGpuCount() const { std::lock_guard<std::mutex> lock(dataMutex); return gpus.size(); }
std::string SystemMonitor::GetGpuName(int index) const { std::lock_guard<std::mutex> lock(dataMutex); return gpus[index].name; }
double SystemMonitor::GetGpuLoad(int index) const { std::lock_guard<std::mutex> lock(dataMutex); return gpus[index].lastLoad; }
double SystemMonitor::GetGpuTotalVramMB(int index) const { std::lock_guard<std::mutex> lock(dataMutex); return gpus[index].dedicatedVramBytes / (1024.0 * 1024.0); }
double SystemMonitor::GetGpuUsedVramMB(int index) const { std::lock_guard<std::mutex> lock(dataMutex); return gpus[index].usedVramBytes / (1024.0 * 1024.0); }
double SystemMonitor::GetNetDownload() const { std::lock_guard<std::mutex> lock(dataMutex); return netDownloadMBps; }
double SystemMonitor::GetNetUpload() const { std::lock_guard<std::mutex> lock(dataMutex); return netUploadMBps; }
size_t SystemMonitor::GetDiskCount() const { std::lock_guard<std::mutex> lock(dataMutex); return disks.size(); }
double SystemMonitor::GetDiskRead(int i) const { std::lock_guard<std::mutex> lock(dataMutex); return disks[i].readMBps; }
double SystemMonitor::GetDiskWrite(int i) const { std::lock_guard<std::mutex> lock(dataMutex); return disks[i].writeMBps; }