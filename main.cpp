#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
using namespace std;
class TimerPatcher {
    HANDLE processHandle;
    DWORD processId;
    LPVOID currentTimerAddress;
public:
    TimerPatcher() : processHandle(nullptr), processId(0), currentTimerAddress(nullptr) {}
    bool findGame() {
        PROCESSENTRY32 entry = {sizeof(PROCESSENTRY32)};
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (snapshot == INVALID_HANDLE_VALUE) return false;
        bool found = false;
        if (Process32First(snapshot, &entry)) {
            do {
                if (strstr(entry.szExeFile, "60Seconds")) {
                    processId = entry.th32ProcessID;
                    if (processHandle) CloseHandle(processHandle);
                    processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
                    if (processHandle) { found = true; break; }
                }
            } while (Process32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }
    LPVOID getModuleBase(const char* name) {
        MODULEENTRY32 entry = {sizeof(MODULEENTRY32)};
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
        if (snapshot == INVALID_HANDLE_VALUE) return nullptr;
        LPVOID base = nullptr;
        if (Module32First(snapshot, &entry)) {
            do { if (!strcmp(entry.szModule, name)) { base = entry.modBaseAddr; break; }
            } while (Module32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return base;
    }
    LPVOID followChain(DWORD offset) {
        LPVOID base = getModuleBase("mono-2.0-bdwgc.dll");
        if (!base) return nullptr;
        uintptr_t addr1, addr2;
        SIZE_T read;
        if (!ReadProcessMemory(processHandle, (LPVOID)((uintptr_t)base + offset), &addr1, sizeof(uintptr_t), &read)) return nullptr;
        if (!ReadProcessMemory(processHandle, (LPVOID)(addr1 + 0x28), &addr2, sizeof(uintptr_t), &read)) return nullptr;
        return (LPVOID)(addr2 + 0x88);
    }
    LPVOID findTimer() {
        for (DWORD offset : {0x498DE0, 0x4A0C80}) {
            LPVOID timer = followChain(offset);
            if (timer) {
                DWORD val; SIZE_T read;
                if (ReadProcessMemory(processHandle, timer, &val, sizeof(DWORD), &read) && val >= 30 && val <= 62) return timer;
            }
        }
        return nullptr;
    }
    void run() {
        HWND console = GetConsoleWindow(); RECT r; GetWindowRect(console, &r);
        MoveWindow(console, r.left, r.top, (r.right - r.left) / 2, r.bottom - r.top, TRUE);
        cout << "60 Seconds Timer Patcher v1.0\n==============================\nMonitoring...\n\n";

        int patches = 0, fails = 0, stable = 0;
        bool found = false, waiting = false, patched = false;
        DWORD last = 0;

        while (true) {
            if (!findGame()) {
                if (found) { cout << "Game closed.\n"; found = false; }
                currentTimerAddress = nullptr; waiting = patched = false; fails = 0;
                Sleep(5000); continue;
            }
            if (!found) { cout << "Game detected!\n"; found = true; fails = 0; waiting = patched = false; }

            if (!currentTimerAddress) {
                currentTimerAddress = findTimer();
                if (!currentTimerAddress) {
                    if (++fails % 10 == 1) cout << "Waiting for timer...\n";
                    Sleep(3000); continue;
                }
                if (fails) { cout << "Timer found!\n"; fails = 0; }
            }
            DWORD val; SIZE_T read;
            if (ReadProcessMemory(processHandle, currentTimerAddress, &val, sizeof(DWORD), &read) && val >= 30 && val <= 62) {
                if ((last < 40 && val >= 55) || (abs((int)val - (int)last) > 20 && val > 50)) {
                    cout << "New round!\n"; waiting = patched = false; stable = 0;
                }
                stable = (val == last) ? stable + 1 : 0;
                if (val != last && !stable && val >= 59 && !patched && !waiting) {
                    cout << "Timer at " << val << " - waiting for 58...\n"; waiting = true;
                }
                if (val == 58 && waiting && !patched && stable >= 2) {
                    DWORD newVal = 3; SIZE_T written;
                    if (WriteProcessMemory(processHandle, currentTimerAddress, &newVal, sizeof(DWORD), &written)) {
                        cout << "PATCHED! 58 -> 3 seconds (#" << ++patches << ")\n";
                        waiting = false; patched = true;
                    }
                }
                last = val;
            } else currentTimerAddress = nullptr;
            Sleep(200);
        }
    }
};
int main() { TimerPatcher().run(); return 0; }
