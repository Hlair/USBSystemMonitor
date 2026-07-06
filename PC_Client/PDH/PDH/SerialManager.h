#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include "SystemMonitor.h"

// Заборонена компілятору додавати пусті байти для вирівнювання структур у пам'яті.
#pragma pack(push, 1)

// Перелік команд для взаємодії з мікроконтролером.
enum class CommandType : uint8_t {
    HANDSHAKE = 0x01,
    METRICS_BASIC = 0x02,
    SET_DISPLAY_MODE = 0x03
};

// Структура корисного навантаження базових метрик.
struct BasicMetricsPayload {
    uint8_t cpu_total;
    uint8_t ram_percent;
    uint8_t total_ram_gb;
    uint16_t used_ram_mb;
    uint32_t net_rx_kbps;
    uint32_t net_tx_kbps;
    uint8_t num_cores;
    uint8_t core_loads[32];
    uint8_t num_gpus;
    uint8_t gpu_loads[4];
    uint8_t gpu_vram_percents[4];
    uint8_t num_disks;
    uint32_t disk_read_kbps[4];
    uint32_t disk_write_kbps[4];
};

// Структура заголовка пакету даних.
struct PacketHeader {
    uint8_t magic = 0xAA;
    CommandType command;
    uint16_t payload_length;
};

#pragma pack(pop)

// Клас управління послідовним портом.
class SerialManager {
public:
    SerialManager();
    ~SerialManager();

    void SetPaused(bool paused);
    bool IsPaused() const;
    bool IsConnected() const;
    std::string GetCurrentPort() const;

    void SendMetrics(const SystemMonitor& monitor);
    void SendDisplayCommand(uint8_t mode);

private:
    // Системні дескриптори.
    HANDLE hSerial;
    std::string currentPort;

    // Прапорці стану з'єднання та життєвого циклу.
    std::atomic<bool> isConnected{ false };
    std::atomic<bool> keepRunning{ true };
    std::atomic<bool> isPaused{ false };

    // Об'єкти синхронізації та паралельного виконання.
    std::thread connectionThread;
    std::mutex serialMutex;

    void ConnectionTask();
    bool TryConnect(const std::string& portName);
    void Disconnect();
    bool SendPacket(CommandType cmd, const void* payload, uint16_t length);
    uint8_t CalculateChecksum(const uint8_t* data, size_t length);
};