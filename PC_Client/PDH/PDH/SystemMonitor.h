#pragma once
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <map>

#ifndef NTSTATUS
#define NTSTATUS LONG
#endif

#include <d3dkmthk.h>
#include <pdh.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")

// Оголошено клас для збору апаратних метрик системи.
class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    void Update();

    // Забезпечено потокобезпечний доступ до даних для інтерфейсу та менеджера передачі.
    int GetNumCores() const;
    double GetCpuTotal() const;
    double GetCoreLoad(int index) const;

    double GetRamTotalGB() const;
    double GetRamUsedGB() const;
    int GetRamPercent() const;

    size_t GetGpuCount() const;
    std::string GetGpuName(int index) const;
    double GetGpuLoad(int index) const;
    double GetGpuTotalVramMB(int index) const;
    double GetGpuUsedVramMB(int index) const;

    double GetNetDownload() const;
    double GetNetUpload() const;

    size_t GetDiskCount() const;
    double GetDiskRead(int i) const;
    double GetDiskWrite(int i) const;

private:
    // М'ютекс для синхронізації доступу до даних.
    mutable std::mutex dataMutex;

    // Блок параметрів процесора та оперативної пам'яті.
    int numLogicalCores = 0;
    double lastCpuTotal = 0.0;
    std::vector<double> lastCoreLoads;

    double ramTotalGB = 0.0;
    double ramUsedGB = 0.0;
    int lastRamPercent = 0;

    uint64_t prevIdleTime = 0, prevKernelTime = 0, prevUserTime = 0;
    std::vector<uint64_t> prevCoreIdle, prevCoreKernel, prevCoreUser;

    // Блок параметрів мережевих адаптерів.
    std::map<DWORD, uint32_t> prevAdapterIn;
    std::map<DWORD, uint32_t> prevAdapterOut;
    double netDownloadMBps = 0.0;
    double netUploadMBps = 0.0;

    // Структури даних апаратних компонентів.
    struct GpuInfo {
        std::string name;
        bool isNvidia = false;
        void* nvmlDeviceHandle = nullptr;
        LUID luid = { 0 };
        double lastLoad = 0.0;
        std::vector<uint64_t> prevNodeRunningTime;

        IDXGIAdapter3* pAdapter3 = nullptr;
        uint64_t usedVramBytes = 0;
        uint64_t dedicatedVramBytes = 0;
        PDH_HCOUNTER hCounterVram = nullptr;
    };
    std::vector<GpuInfo> gpus;

    struct DiskInfo {
        std::string name;
        uint64_t prevRead = 0;
        uint64_t prevWrite = 0;
        double readMBps = 0.0;
        double writeMBps = 0.0;
    };
    std::vector<DiskInfo> disks;

    std::chrono::steady_clock::time_point lastUpdateTime;

    // Системні дескриптори.
    PDH_HQUERY vramQuery = nullptr;
    HMODULE hNvmlDll = nullptr;
};