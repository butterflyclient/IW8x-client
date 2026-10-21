#include "connection.h"

#define WIN32_LEAN_AND_MEAN
#define NOMCX
#define NOSERVICE
#define NOIME
#include <assert.h>
#include <windows.h>

int GetProcessId()
{
    return (int)::GetCurrentProcessId();
}

struct BaseConnectionWin : public BaseConnection {
    HANDLE pipe{INVALID_HANDLE_VALUE};
};

static BaseConnectionWin Connection;

/*static*/ BaseConnection* BaseConnection::Create()
{
    return &Connection;
}

/*static*/ void BaseConnection::Destroy(BaseConnection*& c)
{
    auto self = reinterpret_cast<BaseConnectionWin*>(c);
    self->Close();
    c = nullptr;
}

bool BaseConnection::Open()
{
#ifdef IW8MU_DISCORD_RPC_TEST
    wchar_t pipeName[]{L"\\\\?\\pipe\\magma120-discord-rpc-fixture-0"};
#else
    wchar_t pipeName[]{L"\\\\?\\pipe\\discord-ipc-0"};
#endif
    const size_t pipeDigit = sizeof(pipeName) / sizeof(wchar_t) - 2;
    pipeName[pipeDigit] = L'0';
    auto self = reinterpret_cast<BaseConnectionWin*>(this);
    for (;;) {
        self->pipe = ::CreateFileW(
          pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (self->pipe != INVALID_HANDLE_VALUE) {
            self->isOpen = true;
            return true;
        }

        auto lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND) {
#ifndef IW8MU_DISCORD_RPC_TEST
            if (pipeName[pipeDigit] < L'9') {
                pipeName[pipeDigit]++;
                continue;
            }
#endif
        }
        else if (lastError == ERROR_PIPE_BUSY) {
            return false; // IW8: SDK backoff retries; never block shutdown here.
        }
        return false;
    }
}

bool BaseConnection::Close()
{
    auto self = reinterpret_cast<BaseConnectionWin*>(this);
    ::CloseHandle(self->pipe);
    self->pipe = INVALID_HANDLE_VALUE;
    self->isOpen = false;
    return true;
}

// IW8: bound optional RPC I/O. Discord exiting or a stalled pipe must not
// prevent the SDK worker from stopping with the game.
static bool Transfer(HANDLE pipe, void* data, DWORD size, bool write)
{
    OVERLAPPED operation{};
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) return false;
    DWORD count = 0;
    bool success = (write ? WriteFile(pipe, data, size, &count, &operation)
                         : ReadFile(pipe, data, size, &count, &operation)) == TRUE;
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(operation.hEvent, 100) == WAIT_OBJECT_0)
            success = GetOverlappedResult(pipe, &operation, &count, FALSE) == TRUE;
        else {
            CancelIoEx(pipe, &operation);
            // Cancellation completion owns the stack OVERLAPPED until retired.
            GetOverlappedResult(pipe, &operation, &count, TRUE);
        }
    }
    CloseHandle(operation.hEvent);
    return success && count == size;
}

bool BaseConnection::Write(const void* data, size_t length)
{
    if (length == 0) {
        return true;
    }
    auto self = reinterpret_cast<BaseConnectionWin*>(this);
    assert(self);
    if (!self) {
        return false;
    }
    if (self->pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    assert(data);
    if (!data) {
        return false;
    }
    const DWORD bytesLength = (DWORD)length;
    return Transfer(self->pipe, const_cast<void*>(data), bytesLength, true);
}

bool BaseConnection::Read(void* data, size_t length)
{
    assert(data);
    if (!data) {
        return false;
    }
    auto self = reinterpret_cast<BaseConnectionWin*>(this);
    assert(self);
    if (!self) {
        return false;
    }
    if (self->pipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD bytesAvailable = 0;
    if (::PeekNamedPipe(self->pipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
        if (bytesAvailable >= length) {
            DWORD bytesToRead = (DWORD)length;
            if (Transfer(self->pipe, data, bytesToRead, false)) {
                return true;
            }
            else {
                Close();
            }
        }
    }
    else {
        Close();
    }
    return false;
}
