#include "SerialManager.h"
#include <iostream>

SerialManager::SerialManager() : hSerial(INVALID_HANDLE_VALUE) {
    // Робочий потік для управління з'єднанням.
    connectionThread = std::thread(&SerialManager::ConnectionTask, this);
}

SerialManager::~SerialManager() {
    // Завершення робочиого потоку та закриття порту.
    keepRunning = false;
    if (connectionThread.joinable()) {
        connectionThread.join();
    }
    Disconnect();
}

void SerialManager::SetPaused(bool paused) {
    // Зміна стану паузи передачі даних.
    isPaused = paused;
    if (isPaused) {
        isConnected = false;
        Disconnect();
    }
}

bool SerialManager::IsPaused() const { return isPaused; }
bool SerialManager::IsConnected() const { return isConnected; }
std::string SerialManager::GetCurrentPort() const { return currentPort; }

uint8_t SerialManager::CalculateChecksum(const uint8_t* data, size_t length) {
    // Розрахунок контрольної суми алгоритмом XOR.
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; ++i) {
        checksum ^= data[i];
    }
    return checksum;
}

bool SerialManager::SendPacket(CommandType cmd, const void* payload, uint16_t length) {
    // Блокування доступу до порту для інших потоків.
    std::lock_guard<std::mutex> lock(serialMutex);
    if (!isConnected || hSerial == INVALID_HANDLE_VALUE) return false;

    // Заголовок пакету даних.
    PacketHeader header;
    header.command = cmd;
    header.payload_length = length;

    std::vector<uint8_t> buffer;
    uint8_t* headerPtr = reinterpret_cast<uint8_t*>(&header);
    buffer.insert(buffer.end(), headerPtr, headerPtr + sizeof(PacketHeader));

    if (payload && length > 0) {
        const uint8_t* payloadPtr = reinterpret_cast<const uint8_t*>(payload);
        buffer.insert(buffer.end(), payloadPtr, payloadPtr + length);
    }

    buffer.push_back(CalculateChecksum(buffer.data(), buffer.size()));

    // Відправка пакету в послідовний порт.
    DWORD bytesWritten;
    if (!WriteFile(hSerial, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesWritten, nullptr)) {
        isConnected = false;
        return false;
    }
    return true;
}

void SerialManager::SendMetrics(const SystemMonitor& monitor) {
    if (!isConnected || isPaused) return;

    // Ініціалізовано нулями.
    BasicMetricsPayload payload = { 0 };

    // Формування корисного навантаження з метриками системи.
    payload.cpu_total = static_cast<uint8_t>(monitor.GetCpuTotal());
    payload.ram_percent = static_cast<uint8_t>(monitor.GetRamPercent());
    payload.total_ram_gb = static_cast<uint8_t>(monitor.GetRamTotalGB());
    payload.used_ram_mb = static_cast<uint16_t>(monitor.GetRamUsedGB() * 1024.0);
    payload.net_rx_kbps = static_cast<uint32_t>(monitor.GetNetDownload() * 1024);
    payload.net_tx_kbps = static_cast<uint32_t>(monitor.GetNetUpload() * 1024);

    payload.num_cores = static_cast<uint8_t>(min(32, monitor.GetNumCores()));
    for (int i = 0; i < payload.num_cores; i++) {
        payload.core_loads[i] = static_cast<uint8_t>(monitor.GetCoreLoad(i));
    }

    payload.num_gpus = static_cast<uint8_t>(min(4, (int)monitor.GetGpuCount()));
    for (int i = 0; i < payload.num_gpus; i++) {
        payload.gpu_loads[i] = static_cast<uint8_t>(monitor.GetGpuLoad(i));
        double totalVram = monitor.GetGpuTotalVramMB(i);
        payload.gpu_vram_percents[i] = totalVram > 0 ? static_cast<uint8_t>((monitor.GetGpuUsedVramMB(i) / totalVram) * 100.0) : 0;
    }

    payload.num_disks = static_cast<uint8_t>(min(4, (int)monitor.GetDiskCount()));
    for (int i = 0; i < payload.num_disks; i++) {
        payload.disk_read_kbps[i] = static_cast<uint32_t>(monitor.GetDiskRead(i) * 1024);
        payload.disk_write_kbps[i] = static_cast<uint32_t>(monitor.GetDiskWrite(i) * 1024);
    }

    SendPacket(CommandType::METRICS_BASIC, &payload, sizeof(BasicMetricsPayload));
}

void SerialManager::SendDisplayCommand(uint8_t mode) {
    // Передача команди керування дисплеєм.
    SendPacket(CommandType::SET_DISPLAY_MODE, &mode, sizeof(mode));
}

void SerialManager::ConnectionTask() {
    // Реалізація циклу автоматичного підключення до послідовного порту.
    while (keepRunning) {
        if (!isConnected && !isPaused) {
            Disconnect();
            for (int i = 1; i <= 20 && !isConnected && keepRunning && !isPaused; ++i) {
                std::string port = "COM" + std::to_string(i);
                if (TryConnect(port)) {
                    currentPort = port;
                    isConnected = true;
                    break;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

bool SerialManager::TryConnect(const std::string& portName) {
    std::string path = "\\\\.\\" + portName;

    // Відкриття дескриптора послідовного порту.
    std::lock_guard<std::mutex> lock(serialMutex);
    hSerial = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);

    if (hSerial == INVALID_HANDLE_VALUE) return false;

    // Налаштування параметриів передачі даних.
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) { Disconnect(); return false; }

    dcbSerialParams.BaudRate = CBR_115200;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) { Disconnect(); return false; }

    // Налаштування таймаутиів операцій читання та запису.
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    // Процедуру рукостискання.
    PacketHeader hsHeader = { 0xAA, CommandType::HANDSHAKE, 0 };
    std::vector<uint8_t> hsBuf;
    uint8_t* ptr = reinterpret_cast<uint8_t*>(&hsHeader);
    hsBuf.insert(hsBuf.end(), ptr, ptr + sizeof(PacketHeader));
    hsBuf.push_back(CalculateChecksum(hsBuf.data(), hsBuf.size()));

    DWORD bytesWritten, bytesRead;
    WriteFile(hSerial, hsBuf.data(), static_cast<DWORD>(hsBuf.size()), &bytesWritten, nullptr);

    // Перевірка відповіді від пристрою.
    uint8_t response = 0;
    ReadFile(hSerial, &response, 1, &bytesRead, nullptr);

    if (bytesRead == 1 && response == 0xBB) return true;

    Disconnect();
    return false;
}

void SerialManager::Disconnect() {
    // Закриття дескриптора послідовного порту.
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
}