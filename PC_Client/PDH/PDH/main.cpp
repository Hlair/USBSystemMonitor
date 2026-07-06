// Бібліотеки графічного інтерфейсу
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Стандартні бібліотеки
#include <stdio.h>
#include <cstdlib>
#include <chrono>
#include <string>

// Модулі бізнес-логіки
#include "SystemMonitor.h"
#include "SerialManager.h"

// Заголовки Windows API
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

// Макрос доступу до нативних функцій
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// Файл ресурсів
#include "resource.h"

// Лінкування системних бібліотек
#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")
#pragma comment(lib, "shell32.lib")

// Сумісність стандартного вводу/виводу для MSVC
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

// Константа темної теми вікна
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// =====================================================================
// СТАН ПРОГРАМИ ТА ГЛОБАЛЬНІ ЗМІННІ
// =====================================================================

// Змінні стану застосунку
struct AppContext {
    bool isRunning = true;                  // Прапорець життєвого циклу
    GLFWwindow* window = nullptr;           // Вказівник вікна
    NOTIFYICONDATAA trayIcon = {};          // Структура іконки трею
    WNDPROC originalWndProc = nullptr;      // Вказівник процедури вікна

    ImFont* fontPlay = nullptr;             // Шрифт Play
    ImFont* fontPause = nullptr;            // Шрифт Pause

    int mainMode = 0;                       // Поточний режим екрану
    int selectedGraphIdx = 0;               // Індекс обраного графіка
    bool pendingAutoSync = false;           // Прапорець очікування синхронізації
    bool showHelp = false;                  // Прапорець вікна допомоги
    bool showAbout = false;                 // Прапорець вікна інформації
};

// Глобальний екземпляр контексту
AppContext g_App;

// Константи подій трею
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001

// =====================================================================
// ОБРОБНИКИ ПОДІЙ WINDOWS
// =====================================================================

// Процедура перехоплення повідомлень вікна
LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TRAYICON) {
        if (lParam == WM_LBUTTONUP) {
            // Відновлення вікна
            glfwShowWindow(g_App.window);
            glfwRestoreWindow(g_App.window);
        }
        else if (lParam == WM_RBUTTONUP) {
            // Контекстне меню
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit Program");
            SetForegroundWindow(hwnd);

            // Результат вибору меню
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);

            // Обробка команди виходу
            if (cmd == ID_TRAY_EXIT) g_App.isRunning = false;
        }
        return 0;
    }
    // Передача повідомлення стандартному обробнику
    return CallWindowProc(g_App.originalWndProc, hwnd, msg, wParam, lParam);
}

// Обробник події закриття вікна
void WindowCloseCallback(GLFWwindow* window) {
    // Логіка згортання в трей при закритті
    glfwSetWindowShouldClose(window, GLFW_FALSE);
    glfwHideWindow(window);
}

// =====================================================================
// ДОПОМІЖНІ ФУНКЦІЇ ІНТЕРФЕЙСУ
// =====================================================================

// Палітра кольорів та стилі ImGui
void ApplyModernDarkTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Кольори
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.33f, 1.00f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.27f, 0.29f, 1.00f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.32f, 0.32f, 0.34f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.45f, 0.65f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.52f, 0.72f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.38f, 0.58f, 1.00f);

    // Геометрія
    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(12, 10);
    style.WindowPadding = ImVec2(12, 12);
}

// Утиліта малювання індикаторів
void DrawSmartProgressBar(float fraction, const char* overlayText, ImVec4 defaultColor = ImVec4(0.20f, 0.75f, 0.35f, 1.0f)) {
    ImVec4 color = defaultColor;

    // Порогові значення кольорів
    if (fraction >= 0.85f) color = ImVec4(0.85f, 0.25f, 0.25f, 1.00f);
    else if (fraction >= 0.65f) color = ImVec4(0.85f, 0.65f, 0.15f, 1.00f);

    // Колір гістограми
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(fraction, ImVec2(-1, 20), overlayText);
    ImGui::PopStyleColor();
}

// =====================================================================
// БЛОКИ ІНТЕРФЕЙСУ
// =====================================================================

// Панель статистики процесора
void DrawPanelCPU(SystemMonitor& monitor, const ImVec2& size) {
    ImGui::BeginChild("Panel_CPU", size, true);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "PROCESSOR (CPU)");
    ImGui::Separator(); ImGui::Spacing();

    // Загальне навантаження
    float cpu_total = (float)monitor.GetCpuTotal() / 100.0f;
    char cpu_buf[32]; snprintf(cpu_buf, sizeof(cpu_buf), "CPU Total: %.1f %%", monitor.GetCpuTotal());
    DrawSmartProgressBar(cpu_total, cpu_buf);

    ImGui::Spacing();
    ImGui::TextDisabled("Logical Cores:");

    // Скролл-регіон ядер
    ImGui::BeginChild("Cores_Scroll");
    for (int i = 0; i < monitor.GetNumCores(); i++) {
        float core_val = (float)monitor.GetCoreLoad(i) / 100.0f;
        char core_buf[32]; snprintf(core_buf, sizeof(core_buf), "Core %d: %.1f%%", i, monitor.GetCoreLoad(i));
        DrawSmartProgressBar(core_val, core_buf);
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

// Панель пам'яті та графічних адаптерів
void DrawPanelMemoryGPU(SystemMonitor& monitor, const ImVec2& size) {
    ImGui::BeginChild("Panel_GPU", size, true);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "MEMORY & GRAPHICS");
    ImGui::Separator(); ImGui::Spacing();

    // Статистика оперативної пам'яті
    ImGui::TextDisabled("System RAM");
    double usedRam = monitor.GetRamUsedGB();
    double totalRam = monitor.GetRamTotalGB();
    float ram_p = totalRam > 0 ? (float)(usedRam / totalRam) : 0.0f;

    char ram_buf[64]; snprintf(ram_buf, sizeof(ram_buf), "RAM: %.1f GB / %.1f GB", usedRam, totalRam);
    DrawSmartProgressBar(ram_p, ram_buf);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Video Adapters");

    // Перелік відеокарт
    for (int i = 0; i < (int)monitor.GetGpuCount(); i++) {
        ImGui::Text("GPU %d: %s", i, monitor.GetGpuName(i).c_str());

        // Навантаження ядра
        float load = (float)monitor.GetGpuLoad(i) / 100.0f;
        char buf[32]; snprintf(buf, sizeof(buf), "Core Load: %.1f %%", monitor.GetGpuLoad(i));
        DrawSmartProgressBar(load, buf);
        ImGui::Spacing();

        // Використання відеопам'яті
        double usedVram = monitor.GetGpuUsedVramMB(i);
        double totalVram = monitor.GetGpuTotalVramMB(i);
        float vramFraction = totalVram > 0 ? (float)(usedVram / totalVram) : 0.0f;

        char vramBuf[64]; snprintf(vramBuf, sizeof(vramBuf), "VRAM: %.0f / %.0f MB", usedVram, totalVram);

        // Колір індикатора VRAM
        DrawSmartProgressBar(vramFraction, vramBuf, ImVec4(0.6f, 0.3f, 0.8f, 1.0f));
        ImGui::Spacing(); ImGui::Spacing();
    }
    ImGui::EndChild();
}

// Панель мережі та накопичувачів
void DrawPanelNetworkStorage(SystemMonitor& monitor, const ImVec2& size) {
    ImGui::BeginChild("Panel_NET", size, true);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "NETWORK & STORAGE");
    ImGui::Separator(); ImGui::Spacing();

    // Статистика мережі
    ImGui::TextDisabled("Network Traffic");
    float dl = (float)monitor.GetNetDownload();
    float ul = (float)monitor.GetNetUpload();

    char dl_buf[64]; snprintf(dl_buf, sizeof(dl_buf), "Download: %.2f MB/s", dl);
    DrawSmartProgressBar(dl > 100.0f ? 1.0f : dl / 100.0f, dl_buf, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));

    char ul_buf[64]; snprintf(ul_buf, sizeof(ul_buf), "Upload: %.2f MB/s", ul);
    DrawSmartProgressBar(ul > 100.0f ? 1.0f : ul / 100.0f, ul_buf, ImVec4(0.8f, 0.5f, 0.2f, 1.0f));

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    ImGui::TextDisabled("Physical Drives");

    // Перелік накопичувачів
    if (monitor.GetDiskCount() == 0) ImGui::TextDisabled("No disks detected.");
    for (int i = 0; i < (int)monitor.GetDiskCount(); i++) {
        ImGui::Text("Drive %d", i);
        float r_spd = (float)monitor.GetDiskRead(i);
        float w_spd = (float)monitor.GetDiskWrite(i);

        // Швидкість читання
        char r_buf[64]; snprintf(r_buf, sizeof(r_buf), "Read: %.2f MB/s", r_spd);
        DrawSmartProgressBar(r_spd > 500.0f ? 1.0f : r_spd / 500.0f, r_buf, ImVec4(0.3f, 0.6f, 0.9f, 1.0f));
        ImGui::Spacing();

        // Швидкість запису
        char w_buf[64]; snprintf(w_buf, sizeof(w_buf), "Write: %.2f MB/s", w_spd);
        DrawSmartProgressBar(w_spd > 500.0f ? 1.0f : w_spd / 500.0f, w_buf, ImVec4(0.8f, 0.4f, 0.8f, 1.0f));
        ImGui::Spacing(); ImGui::Spacing();
    }
    ImGui::EndChild();
}

// Панель керування дисплеєм
void DrawPanelControlCenter(SystemMonitor& monitor, SerialManager& serial, const ImVec2& size) {
    ImGui::BeginChild("Panel_DisplayControl", size, true, ImGuiWindowFlags_NoScrollbar);

    // Блокування панелі за відсутності з'єднання
    bool canSend = serial.IsConnected() && !serial.IsPaused();
    if (!canSend) ImGui::BeginDisabled();

    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "DISPLAY CONTROL");
    ImGui::Separator();

    // Список метрик
    std::vector<std::string> graphNames = { "CPU Total", "RAM Usage", "Network Traffic" };
    std::vector<uint8_t> baseCmds = { 10, 20, 30 };

    for (size_t i = 0; i < monitor.GetGpuCount(); i++) {
        graphNames.push_back("GPU " + std::to_string(i) + " Load"); baseCmds.push_back(40 + i);
        graphNames.push_back("GPU " + std::to_string(i) + " VRAM"); baseCmds.push_back(50 + i);
    }
    for (size_t i = 0; i < monitor.GetDiskCount(); i++) {
        graphNames.push_back("Disk " + std::to_string(i) + " Activity"); baseCmds.push_back(60 + i);
    }

    // Захист меж масиву
    if (g_App.selectedGraphIdx >= graphNames.size()) g_App.selectedGraphIdx = 0;

    // Функція обчислення коду команди
    auto get_final_cmd = [&](int mode, int graph_idx) -> uint8_t {
        uint8_t base = baseCmds[graph_idx];

        // Зсув коду
        if (mode == 5) return base + 60;
        if (mode == 6) return base + 120;
        return base;
        };

    // Функція примусової синхронізації
    auto trigger_force_sync = [&]() {
        uint8_t cmdToSync = (g_App.mainMode == 0) ? 0 :
            (g_App.mainMode == 2) ? 1 :
            (g_App.mainMode == 3) ? 2 :
            (g_App.mainMode == 4) ? 3 :
            (g_App.mainMode == 7) ? 5 : get_final_cmd(g_App.mainMode, g_App.selectedGraphIdx);
        serial.SendDisplayCommand(cmdToSync);
        };

    // Відкладена синхронізація
    if (g_App.pendingAutoSync && serial.IsConnected()) {
        trigger_force_sync();
        g_App.pendingAutoSync = false;
    }

    // Статус підтримки вибору метрики
    bool is_dynamic = (g_App.mainMode == 1 || g_App.mainMode == 5 || g_App.mainMode == 6);
    float inner_left_w = ImGui::GetContentRegionAvail().x * 0.40f;

    // Випадаючий список
    ImGui::BeginChild("ComboBlock", ImVec2(inner_left_w, 0), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("Target Metric:");

    // Блокування списку
    if (!is_dynamic) ImGui::BeginDisabled();

    // Стилізація списку
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.18f, 0.18f, 0.20f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.14f, 0.14f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.60f, 0.25f, 0.45f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.65f, 0.35f, 0.55f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.55f, 0.20f, 0.40f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##GraphSelect", graphNames[g_App.selectedGraphIdx].c_str())) {
        for (int i = 0; i < graphNames.size(); i++) {
            bool is_selected = (g_App.selectedGraphIdx == i);
            if (ImGui::Selectable(graphNames[i].c_str(), is_selected)) {
                g_App.selectedGraphIdx = i;
                if (is_dynamic) serial.SendDisplayCommand(get_final_cmd(g_App.mainMode, g_App.selectedGraphIdx));
            }
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);

    if (!is_dynamic) ImGui::EndDisabled();

    ImGui::Spacing(); ImGui::Spacing();

    // Кнопка синхронізації
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.45f, 0.65f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.52f, 0.72f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    if (ImGui::Button("FORCE SYNC", ImVec2(-1, 30))) trigger_force_sync();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::EndChild();

    ImGui::SameLine();

    // Панель вибору режиму
    ImGui::BeginChild("TilesBlock", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("Screen Modes:");

    // Конфігурація режимів
    struct TileData { const char* name; int mode; bool isDynamic; };
    TileData tiles[] = {
        {"Radial Rings", 0, false}, {"Double Net", 2, false}, {"Core Grid", 3, false},
        {"Detailed Graph", 1, true}, {"Big Digit", 5, true}, {"Speedometer", 6, true},
        {"GraphM", 4, false}, {"Tile Screen", 7, false}
    };

    // Заокруглення кнопок
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    // Ширина плиток
    float tile_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
    for (int i = 0; i < 8; i++) {
        bool is_selected = (g_App.mainMode == tiles[i].mode);
        ImVec4 col_normal, col_hover, col_active;

        // Кольори кнопок
        if (tiles[i].isDynamic) {
            col_normal = is_selected ? ImVec4(0.60f, 0.25f, 0.45f, 1.0f) : ImVec4(0.24f, 0.18f, 0.22f, 1.0f);
            col_hover = is_selected ? ImVec4(0.65f, 0.35f, 0.55f, 1.0f) : ImVec4(0.32f, 0.24f, 0.28f, 1.0f);
            col_active = is_selected ? ImVec4(0.55f, 0.20f, 0.40f, 1.0f) : ImVec4(0.40f, 0.25f, 0.35f, 1.0f);
        }
        else {
            col_normal = is_selected ? ImVec4(0.22f, 0.45f, 0.65f, 1.0f) : ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
            col_hover = is_selected ? ImVec4(0.28f, 0.52f, 0.72f, 1.0f) : ImVec4(0.26f, 0.26f, 0.28f, 1.0f);
            col_active = is_selected ? ImVec4(0.18f, 0.38f, 0.58f, 1.0f) : ImVec4(0.30f, 0.30f, 0.35f, 1.0f);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, col_normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col_active);

        // Кнопка та обробка кліку
        if (ImGui::Button(tiles[i].name, ImVec2(tile_w, 32.0f))) {
            g_App.mainMode = tiles[i].mode;

            // Відправка команди
            serial.SendDisplayCommand(tiles[i].isDynamic ? get_final_cmd(g_App.mainMode, g_App.selectedGraphIdx) :
                (g_App.mainMode == 0 ? 0 : (g_App.mainMode == 2 ? 1 : (g_App.mainMode == 3 ? 2 : (g_App.mainMode == 4 ? 3 : 5)))));
        }
        ImGui::PopStyleColor(3);

        // Перенесення рядка
        if ((i + 1) % 3 != 0 && i != 7) ImGui::SameLine();
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();

    if (!canSend) ImGui::EndDisabled();
    ImGui::EndChild();
}

// Панель стану з'єднання
void DrawPanelHardwareStatus(SerialManager& serial, const ImVec2& size) {
    ImGui::BeginChild("Panel_HardwareStatus", size, true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "HARDWARE CONNECTION");
    ImGui::Separator(); ImGui::Spacing();

    bool paused = serial.IsPaused();

    if (paused) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.4f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.7f, 0.5f, 1.0f));
    }

    ImGui::PushFont(paused ? g_App.fontPlay : g_App.fontPause);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));

    // Зміна стану передачі
    if (ImGui::Button(paused ? "\xE2\x96\xB6" : "\xE2\x8F\xB8", ImVec2(32, 32))) {
        serial.SetPaused(!paused);
        g_App.pendingAutoSync = paused; // Планування синхронізації
    }

    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::PopStyleColor(2);

    ImGui::SameLine(0, 10.0f);

    // Текстовий статус
    ImGui::BeginGroup();
    if (serial.IsConnected()) {
        ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "[+] Connected");
        ImGui::TextDisabled("Port: %s", serial.GetCurrentPort().c_str());
        ImGui::TextDisabled(paused ? "Status: Paused" : "Status: Active Sync");
    }
    else {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "[-] Disconnected");
        ImGui::TextDisabled("Scanning COM...");
    }
    ImGui::EndGroup();
    ImGui::EndChild();
}

// =====================================================================
// ГОЛОВНИЙ ЦИКЛ МАЛЮВАННЯ UI
// =====================================================================

// Головна функція компонування інтерфейсу
void DrawMainUI(SystemMonitor& monitor, SerialManager& serial) {
    // Параметри головного вікна
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    // Фонове вікно
    ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // Системне меню
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Hide to Tray")) glfwHideWindow(g_App.window);
            if (ImGui::MenuItem("Exit Completely")) g_App.isRunning = false;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("How to use")) g_App.showHelp = true;
            if (ImGui::MenuItem("About")) g_App.showAbout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Спливаючі вікна Help та About
    if (g_App.showHelp || g_App.showAbout) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));

        if (g_App.showHelp) {
            ImGui::Begin("Help & Instructions", &g_App.showHelp, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "How it works:");
            ImGui::Separator(); ImGui::Spacing();
            ImGui::BulletText("Connect your ESP32 board via USB.");
            ImGui::BulletText("The program will automatically detect the COM port.");
            ImGui::BulletText("Metrics are sent automatically every 500ms.");
            ImGui::BulletText("Use the bottom panel to switch views on the ESP32 screen.");
            ImGui::BulletText("You can safely minimize the app to the System Tray.");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 120) * 0.5f);
            if (ImGui::Button("Close", ImVec2(120, 0))) g_App.showHelp = false;
            ImGui::End();
        }

        if (g_App.showAbout) {
            ImGui::Begin("About Program", &g_App.showAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "PC Resource Monitor to UART");
            ImGui::Separator(); ImGui::Spacing();
            ImGui::Text("Created by: "); ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Simonov Nazar");
            ImGui::Text("Version: "); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Release 1.3");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetWindowSize().x - 120) * 0.5f);
            if (ImGui::Button("Close", ImVec2(120, 0))) g_App.showAbout = false;
            ImGui::End();
        }

        ImGui::PopStyleVar(2);
    }

    // Геометрія верхніх панелей
    float total_w = ImGui::GetContentRegionAvail().x;
    float top_h = ImGui::GetContentRegionAvail().y * 0.70f;

    // Пропорція ширини
    float block_w = (total_w - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;

    DrawPanelCPU(monitor, ImVec2(block_w, top_h)); ImGui::SameLine();
    DrawPanelMemoryGPU(monitor, ImVec2(block_w, top_h)); ImGui::SameLine();
    DrawPanelNetworkStorage(monitor, ImVec2(0, top_h));

    // Геометрія нижніх панелей
    float bottom_h = ImGui::GetContentRegionAvail().y;

    // Пропорція панелі стану
    float right_w = total_w * 0.28f;
    float left_w = total_w - right_w - ImGui::GetStyle().ItemSpacing.x;

    DrawPanelControlCenter(monitor, serial, ImVec2(left_w, bottom_h)); ImGui::SameLine();
    DrawPanelHardwareStatus(serial, ImVec2(0, bottom_h));

    ImGui::End();
}

// =====================================================================
// ТОЧКА ВХОДУ (ОСНОВНА ФУНКЦІЯ)
// =====================================================================
int main() {
    // Об'єкти бізнес-логіки
    SystemMonitor monitor;
    SerialManager serial;

    // Графічний фреймворк
    if (!glfwInit()) return 1;

    // Розмір вікна
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    g_App.window = glfwCreateWindow(1100, 800, "PC Resource Monitor", nullptr, nullptr);
    if (!g_App.window) return 1;

    // Контекст та VSync
    glfwMakeContextCurrent(g_App.window);
    glfwSwapInterval(1);

    // --- Налаштування вікна засобами Windows API ---
    HWND hwnd = glfwGetWin32Window(g_App.window);

    // Системна темна тема
    BOOL isDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &isDarkMode, sizeof(isDarkMode));

    // Обробник повідомлень
    g_App.originalWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)TrayWndProc);
    glfwSetWindowCloseCallback(g_App.window, WindowCloseCallback);

    // Іконка вікна
    HICON myIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    if (myIcon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)myIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)myIcon);
    }

    // --- Налаштування іконки системного трею ---
    g_App.trayIcon.cbSize = sizeof(NOTIFYICONDATAA);
    g_App.trayIcon.hWnd = hwnd;
    g_App.trayIcon.uID = 1;
    g_App.trayIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_App.trayIcon.uCallbackMessage = WM_TRAYICON;
    g_App.trayIcon.hIcon = myIcon ? myIcon : LoadIcon(NULL, IDI_APPLICATION);
    strcpy_s(g_App.trayIcon.szTip, sizeof(g_App.trayIcon.szTip), "ESP32 System Monitor");

    // Іконка системного трею
    Shell_NotifyIconA(NIM_ADD, &g_App.trayIcon);

    // --- Ініціалізація ImGui ---
    // Бібліотека інтерфейсу
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Шлях до шрифтів
    char winDir[MAX_PATH]; GetWindowsDirectoryA(winDir, MAX_PATH);
    std::string fontPath = std::string(winDir) + "\\Fonts\\segoeui.ttf";
    std::string symbolFontPath = std::string(winDir) + "\\Fonts\\seguisym.ttf";

    // Основний шрифт
    ImFont* mainFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    if (!mainFont) io.Fonts->AddFontDefault();

    // Іконочні символи
    static const ImWchar icon_pause_range[] = { 0x23F8, 0x23F8, 0 };
    static const ImWchar icon_play_range[] = { 0x25B6, 0x25B6, 0 };

    ImFontConfig configIcon;

    // Зміщення символів
    configIcon.GlyphOffset.y = -2.0f;
    g_App.fontPause = io.Fonts->AddFontFromFileTTF(symbolFontPath.c_str(), 28.0f, &configIcon, icon_pause_range);

    configIcon.GlyphOffset.y = -1.0f;
    g_App.fontPlay = io.Fonts->AddFontFromFileTTF(symbolFontPath.c_str(), 24.0f, &configIcon, icon_play_range);

    // Резервні шрифти
    if (!g_App.fontPause) g_App.fontPause = mainFont;
    if (!g_App.fontPlay) g_App.fontPlay = mainFont;

    // Стилі інтерфейсу
    ApplyModernDarkTheme();

    // Бекенд рендерингу
    ImGui_ImplGlfw_InitForOpenGL(g_App.window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // --- Створено фоновий потік апаратного моніторингу ---
    // Потік моніторингу
    std::thread hardwareThread([&]() {
        while (g_App.isRunning) {
            // Оновлення метрик
            monitor.Update();

            // Відправка даних
            serial.SendMetrics(monitor);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        });

    // --- ГОЛОВНИЙ ЦИКЛ РЕНДЕРИНГУ ---
    while (g_App.isRunning) {
        // Обробка подій
        glfwPollEvents();

        // Перевірка видимості вікна
        if (!glfwGetWindowAttrib(g_App.window, GLFW_VISIBLE) || glfwGetWindowAttrib(g_App.window, GLFW_ICONIFIED)) {
            // Затримка потоку
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // Новий кадр
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Інтерфейс
        DrawMainUI(monitor, serial);

        // Рендеринг
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(g_App.window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        // Колір фону
        glClearColor(0.08f, 0.08f, 0.09f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Оновлення буфера
        glfwSwapBuffers(g_App.window);
    }

    // --- ПРОЦЕДУРА ЗАВЕРШЕННЯ ПРОГРАМИ ---
    // Сигнал зупинки
    g_App.isRunning = false;

    // Синхронізація фонового потоку
    if (hardwareThread.joinable()) hardwareThread.join();

    // Звільнення ресурсів
    // Видалення іконки трею
    Shell_NotifyIconA(NIM_DELETE, &g_App.trayIcon);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(g_App.window);
    glfwTerminate();

    // Завершення роботи
    return 0;
}