#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pthread.h>

#include <iostream>
#include <thread>

int main() {
    HANDLE started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!started || !release) {
        std::cerr << "PROCESS_THREAD_HANDLE_TEST=FAIL reason=create_event error=" << GetLastError() << "\n";
        if (started) CloseHandle(started);
        if (release) CloseHandle(release);
        return 1;
    }

    std::thread worker([started, release] {
        SetEvent(started);
        WaitForSingleObject(release, INFINITE);
    });

    HANDLE pthreadHandle = reinterpret_cast<HANDLE>(pthread_gethandle(worker.native_handle()));
    HANDLE workerHandle = nullptr;
    if (!pthreadHandle ||
        !DuplicateHandle(GetCurrentProcess(), pthreadHandle, GetCurrentProcess(), &workerHandle,
            0, FALSE, DUPLICATE_SAME_ACCESS)) {
        const DWORD error = GetLastError();
        SetEvent(release);
        worker.join();
        CloseHandle(started);
        CloseHandle(release);
        std::cerr << "PROCESS_THREAD_HANDLE_TEST=FAIL reason=duplicate_native_handle error=" << error << "\n";
        return 2;
    }

    if (WaitForSingleObject(started, 5000) != WAIT_OBJECT_0) {
        SetEvent(release);
        worker.join();
        CloseHandle(workerHandle);
        CloseHandle(started);
        CloseHandle(release);
        std::cerr << "PROCESS_THREAD_HANDLE_TEST=FAIL reason=worker_start_timeout\n";
        return 3;
    }

    worker.detach();
    if (WaitForSingleObject(workerHandle, 0) != WAIT_TIMEOUT) {
        SetEvent(release);
        WaitForSingleObject(workerHandle, 5000);
        CloseHandle(workerHandle);
        CloseHandle(started);
        CloseHandle(release);
        std::cerr << "PROCESS_THREAD_HANDLE_TEST=FAIL reason=worker_signaled_before_release\n";
        return 4;
    }

    SetEvent(release);
    const DWORD exitWait = WaitForSingleObject(workerHandle, 5000);
    CloseHandle(workerHandle);
    CloseHandle(started);
    CloseHandle(release);
    if (exitWait != WAIT_OBJECT_0) {
        std::cerr << "PROCESS_THREAD_HANDLE_TEST=FAIL reason=worker_exit_wait result=" << exitWait
            << " error=" << GetLastError() << "\n";
        return 5;
    }

    std::cout << "PROCESS_THREAD_HANDLE_TEST=PASS backend=mingw_pthread native_handle=windows_handle detached_worker_exit=observed\n";
    return 0;
}
