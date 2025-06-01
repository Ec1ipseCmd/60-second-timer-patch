#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <vector>
#include <string>

using namespace std;

class TimerPatcher {
private:
    HANDLE processHandle;
    DWORD processId;
    LPVOID currentTimerAddress;

public:
    TimerPatcher() : processHandle(nullptr), processId(0), currentTimerAddress(nullptr) {}

    bool findGame() {
        PROCESSENTRY32 entry;
        entry.dwSize = sizeof(PROCESSENTRY32);

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return false;
        }

        bool found = false;

        if (Process32First(snapshot, &entry)) {
            do {
                if (strcmp(entry.szExeFile, "60Seconds.exe") == 0 ||
                    strcmp(entry.szExeFile, "60SecondsReatomized.exe") == 0 ||
                    strcmp(entry.szExeFile, "60 Seconds! Reatomized.exe") == 0) {

                    processId = entry.th32ProcessID;

                    if (processHandle) {
                        CloseHandle(processHandle);
                    }

                    processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);

                    if (processHandle != nullptr) {
                        found = true;
                        break;
                    }
                }
            } while (Process32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return found;
    }

    LPVOID getModuleBaseAddress(const char* moduleName) {
        MODULEENTRY32 entry;
        entry.dwSize = sizeof(MODULEENTRY32);

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, processId);
        if (snapshot == INVALID_HANDLE_VALUE) {
            return nullptr;
        }

        LPVOID baseAddress = nullptr;
        if (Module32First(snapshot, &entry)) {
            do {
                if (strcmp(entry.szModule, moduleName) == 0) {
                    baseAddress = entry.modBaseAddr;
                    break;
                }
            } while (Module32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return baseAddress;
    }

    LPVOID followPointerChain(DWORD baseOffset) {
        LPVOID baseAddress = getModuleBaseAddress("mono-2.0-bdwgc.dll");
        if (baseAddress == nullptr) {
            return nullptr;
        }

        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(baseAddress);
        LPVOID firstAddress = reinterpret_cast<LPVOID>(baseAddr + baseOffset);

        uintptr_t firstValue;
        SIZE_T bytesRead;
        if (!ReadProcessMemory(processHandle, firstAddress, &firstValue, sizeof(uintptr_t), &bytesRead)) {
            return nullptr;
        }

        LPVOID secondAddress = reinterpret_cast<LPVOID>(firstValue + 0x28);

        uintptr_t secondValue;
        if (!ReadProcessMemory(processHandle, secondAddress, &secondValue, sizeof(uintptr_t), &bytesRead)) {
            return nullptr;
        }

        LPVOID timerAddress = reinterpret_cast<LPVOID>(secondValue + 0x88);
        return timerAddress;
    }

    LPVOID findWorkingTimer() {
        LPVOID timer1 = followPointerChain(0x498DE0);
        if (timer1 && testTimerAddress(timer1)) {
            return timer1;
        }

        LPVOID timer2 = followPointerChain(0x4A0C80);
        if (timer2 && testTimerAddress(timer2)) {
            return timer2;
        }

        return nullptr;
    }

    bool testTimerAddress(LPVOID address) {
        DWORD value;
        SIZE_T bytesRead;

        if (!ReadProcessMemory(processHandle, address, &value, sizeof(DWORD), &bytesRead)) {
            return false;
        }

        return (value >= 30 && value <= 62);
    }

    bool isValidTimerValue(DWORD value) {
        return (value >= 30 && value <= 62);
    }

    bool isRoundReset(DWORD currentValue, DWORD lastValue) {
        if ((lastValue < 40 && currentValue >= 55) ||
            (abs((int)currentValue - (int)lastValue) > 20 && currentValue > 50)) {
            return true;
        }
        return false;
    }

    void backgroundMonitor() {
        cout << "60 Seconds Background Timer Patcher v3.0" << endl;
        cout << "========================================" << endl;
        cout << "Monitoring for 60 Seconds! Reatomized..." << endl;
        cout << "Will patch scav timer to 3 seconds when it reaches 58." << endl;
        cout << "Simply close this window to end the patch." << endl << endl;

        int patchCount = 0;
        bool gameWasFound = false;
        int invalidValueCount = 0;
        bool waitingForTrigger = false;
        DWORD lastValue = 0;
        int stableValueCount = 0;
        bool timerHasBeenPatched = false;
        int scanFailCount = 0;

        while (true) {
            // Check for game
            if (!findGame()) {
                if (gameWasFound) {
                    cout << "Game closed. Waiting for restart..." << endl;
                    gameWasFound = false;
                }
                currentTimerAddress = nullptr;
                waitingForTrigger = false;
                timerHasBeenPatched = false;
                scanFailCount = 0;
                Sleep(5000);
                continue;
            } else {
                if (!gameWasFound) {
                    cout << "60 Seconds! Reatomized detected!" << endl;
                    gameWasFound = true;
                    currentTimerAddress = nullptr;
                    invalidValueCount = 0;
                    waitingForTrigger = false;
                    timerHasBeenPatched = false;
                    scanFailCount = 0;
                }
            }

            // Try to find timer if we don't have one
            if (!currentTimerAddress || invalidValueCount > 10) {
                currentTimerAddress = findWorkingTimer();
                invalidValueCount = 0;
                waitingForTrigger = false;
                timerHasBeenPatched = false;

                if (!currentTimerAddress) {
                    scanFailCount++;
                    // Only show message every 10 failed scans to reduce spam
                    if (scanFailCount % 10 == 1) {
                        cout << "Waiting for active timer..." << endl;
                    }
                    Sleep(3000);
                    continue;
                } else {
                    if (scanFailCount > 0) {
                        cout << "Timer found! Monitoring active." << endl;
                        scanFailCount = 0;
                    }
                }
            }

            // Check and patch timer if we have one
            if (currentTimerAddress) {
                DWORD currentValue;
                SIZE_T bytesRead;

                if (ReadProcessMemory(processHandle, currentTimerAddress, &currentValue, sizeof(DWORD), &bytesRead)) {

                    if (isValidTimerValue(currentValue)) {
                        invalidValueCount = 0;

                        // Check for round reset
                        if (isRoundReset(currentValue, lastValue)) {
                            cout << "New round started!" << endl;
                            waitingForTrigger = false;
                            timerHasBeenPatched = false;
                            stableValueCount = 0;
                        }

                        // Count stable values
                        if (currentValue == lastValue) {
                            stableValueCount++;
                        } else {
                            stableValueCount = 0;
                        }

                        // Show important timer changes only
                        if (currentValue != lastValue && stableValueCount == 0) {
                            // Only show when waiting for trigger or when timer gets patched
                            if (currentValue >= 59 && !timerHasBeenPatched) {
                                if (!waitingForTrigger) {
                                    cout << "Timer at " << currentValue << " - waiting for 58..." << endl;
                                    waitingForTrigger = true;
                                }
                            }
                        }

                        // Patch when timer hits exactly 58 seconds
                        if (currentValue == 58 && waitingForTrigger && !timerHasBeenPatched && stableValueCount >= 2) {
                            DWORD newValue = 3;
                            SIZE_T bytesWritten;

                            if (WriteProcessMemory(processHandle, currentTimerAddress, &newValue, sizeof(DWORD), &bytesWritten)) {
                                patchCount++;
                                cout << "PATCHED! Timer: 58 -> 3 seconds (#" << patchCount << ")" << endl;
                                waitingForTrigger = false;
                                timerHasBeenPatched = true;
                            }
                        }

                        lastValue = currentValue;
                    } else {
                        invalidValueCount++;
                        if (invalidValueCount > 10) {
                            currentTimerAddress = nullptr;
                        }
                    }
                } else {
                    currentTimerAddress = nullptr;
                }
            }

            Sleep(200);
        }
    }
};

int main() {
    TimerPatcher patcher;
    patcher.backgroundMonitor();
    return 0;
}
