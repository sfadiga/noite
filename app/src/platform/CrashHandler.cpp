#include "platform/CrashHandler.h"

#include "log/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <crtdbg.h>
#include <csignal>
#include <cstdlib>
#include <exception>

namespace crash {

namespace {

constexpr DWORD kCodeFatal = 0xE0000001; // terminate, abort, qFatal
constexpr DWORD kCodeCrt = 0xE0000002;   // pure call, invalid parameter
constexpr int kMaxFrames = 64;
constexpr int kPathChars = 1024;
constexpr int kLockTries = 50; // 1 ms apart
constexpr ULONG kStackReserve = 64 * 1024; // left for the report after a stack overflow

// Prepared by install(); the crash path only reads them. Empty when the path did not fit.
wchar_t g_logPath[kPathChars];
wchar_t g_dumpDir[kPathChars]; // ends with a backslash
// The thread writing the report; 0 while there is none.
std::atomic<DWORD> g_reportingThread{0};

// Text built in a fixed buffer: nothing on the crash path may allocate.
struct Line {
    char text[640];
    int len = 0;
    static constexpr int kRoom = sizeof(text) - 3; // keeps CRLF or a NUL

    void put(char c)
    {
        if (len < kRoom)
            text[len++] = c;
    }
    void put(const char* s)
    {
        while (*s)
            put(*s++);
    }
    void hex(quint64 v)
    {
        char digits[16];
        int n = 0;
        do {
            digits[n++] = "0123456789abcdef"[v & 15];
            v >>= 4;
        } while (v);
        put("0x");
        while (n)
            put(digits[--n]);
    }
    void number(unsigned v, int width)
    {
        char digits[10];
        int n = 0;
        do {
            digits[n++] = char('0' + v % 10);
            v /= 10;
        } while (v || n < width);
        while (n)
            put(digits[--n]);
    }
};

void putFrame(Line& line, const char* module, quint64 offset)
{
    line.put(module);
    line.put('+');
    line.hex(offset);
}

// "2026-10-18 06:00:01.123 F [crash] ", the shape of applog's lines.
void putPrefix(Line& line, const SYSTEMTIME& t)
{
    line.number(t.wYear, 4);
    line.put('-');
    line.number(t.wMonth, 2);
    line.put('-');
    line.number(t.wDay, 2);
    line.put(' ');
    line.number(t.wHour, 2);
    line.put(':');
    line.number(t.wMinute, 2);
    line.put(':');
    line.number(t.wSecond, 2);
    line.put('.');
    line.number(t.wMilliseconds, 3);
    line.put(" F [crash] ");
}

HMODULE moduleAt(DWORD64 address)
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(address), &module);
    return module;
}

// module+offset, or ?+address outside any module.
void putAddress(Line& line, DWORD64 address)
{
    const HMODULE module = moduleAt(address);
    wchar_t path[MAX_PATH];
    if (!module || GetModuleFileNameW(module, path, MAX_PATH) == 0) {
        putFrame(line, "?", address);
        return;
    }
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p)
        if (*p == L'\\' || *p == L'/')
            name = p + 1;
    char utf8[MAX_PATH * 3];
    if (WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof utf8, nullptr, nullptr) == 0)
        lstrcpyA(utf8, "?");
    putFrame(line, utf8, address - reinterpret_cast<DWORD64>(module));
}

void writeLine(HANDLE file, Line& line)
{
    line.text[line.len++] = '\r';
    line.text[line.len++] = '\n';
    DWORD written = 0;
    WriteFile(file, line.text, DWORD(line.len), &written, nullptr);
    line.len = 0;
}

// StackWalk64's lookups, served from the loaded images' own unwind tables: dbghelp's Sym*
// versions load module data on the heap and miss modules loaded after install().
PVOID CALLBACK functionTable(HANDLE, DWORD64 address)
{
    DWORD64 base = 0;
    return RtlLookupFunctionEntry(address, &base, nullptr);
}

DWORD64 CALLBACK moduleBase(HANDLE, DWORD64 address)
{
    return reinterpret_cast<DWORD64>(moduleAt(address));
}

const char* describe(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case 0xE06D7363: return "C++ exception";
    default: return nullptr;
    }
}

// The CRASH line and the stack go to the log first, so they survive a dump that fails or hangs.
void writeReport(EXCEPTION_POINTERS* info, const char* reason)
{
    // applog's QFile writes at its own cached offset, so another thread logging now would overwrite
    // the report: hold the log mutex for good (the process ends). Waits a little, never blocks; a
    // holder that never lets go (this thread crashing inside the log) only costs the wait.
    for (int i = 0; i < kLockTries && !applog::tryLockForCrash(); ++i)
        Sleep(1);
    const HANDLE log = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    SYSTEMTIME now;
    GetLocalTime(&now);
    Line line;
    putPrefix(line, now);
    line.put("CRASH ");
    line.hex(info->ExceptionRecord->ExceptionCode);
    line.put(" at ");
    putAddress(line, reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress));
    if (reason) {
        line.put(" (");
        line.put(reason);
        line.put(')');
    }
    writeLine(log, line);

    CONTEXT context = *info->ContextRecord; // StackWalk64 advances its copy
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    for (int i = 0; i < kMaxFrames; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &context, nullptr,
                         functionTable, moduleBase, nullptr))
            break;
        // Only the first frame may be at 0 (a call through a null pointer); the walk goes on from it.
        if (frame.AddrPC.Offset == 0 && i > 0)
            break;
        line.put("  ");
        putAddress(line, frame.AddrPC.Offset);
        writeLine(log, line);
    }

    Line name;
    name.put("crash-");
    name.number(now.wYear, 4);
    name.number(now.wMonth, 2);
    name.number(now.wDay, 2);
    name.put('-');
    name.number(now.wHour, 2);
    name.number(now.wMinute, 2);
    name.number(now.wSecond, 2);
    name.put(".dmp");
    name.text[name.len] = '\0'; // 26 characters
    wchar_t path[kPathChars + 32];
    int n = 0;
    for (const wchar_t* p = g_dumpDir; *p; ++p)
        path[n++] = *p;
    for (const char* p = name.text; *p; ++p)
        path[n++] = wchar_t(*p);
    path[n] = L'\0';

    const HANDLE dump = g_dumpDir[0] ? CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                                     : INVALID_HANDLE_VALUE;
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    const bool dumped = dump != INVALID_HANDLE_VALUE
        && MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                             MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo), &exception, nullptr, nullptr);
    if (dump != INVALID_HANDLE_VALUE)
        CloseHandle(dump);
    putPrefix(line, now);
    line.put(dumped ? "dump: " : "dump failed: ");
    line.put(name.text);
    writeLine(log, line);
    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);
}

// Reports once, then ends the process with the exception code.
void report(EXCEPTION_POINTERS* info, const char* reason)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const DWORD self = GetCurrentThreadId();
    DWORD reporting = 0;
    if (!g_reportingThread.compare_exchange_strong(reporting, self)) {
        if (reporting == self) // crashed while reporting
            TerminateProcess(GetCurrentProcess(), code);
        Sleep(INFINITE); // another thread is reporting and ends the process
    }
    writeReport(info, reason);
    TerminateProcess(GetCurrentProcess(), code);
}

// For the hooks that get no context: context comes from RtlCaptureContext in the hook itself,
// so the walk starts in a frame that is still live.
void reportCaptured(CONTEXT* context, DWORD code, const char* reason)
{
    EXCEPTION_RECORD record{};
    record.ExceptionCode = code;
    record.ExceptionAddress = reinterpret_cast<PVOID>(context->Rip);
    EXCEPTION_POINTERS info{&record, context};
    report(&info, reason);
}

LONG WINAPI onException(EXCEPTION_POINTERS* info)
{
    report(info, describe(info->ExceptionRecord->ExceptionCode));
    return EXCEPTION_EXECUTE_HANDLER;
}

// std::set_terminate is per thread in the MSVC runtime: on other threads terminate() calls
// abort(), which lands in onAbort.
void onTerminate()
{
    CONTEXT context;
    RtlCaptureContext(&context);
    reportCaptured(&context, kCodeFatal, "terminate");
}

void onAbort(int)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    reportCaptured(&context, kCodeFatal, "abort");
}

void onFatal(const char*)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    reportCaptured(&context, kCodeFatal, "qFatal");
}

void onPurecall()
{
    CONTEXT context;
    RtlCaptureContext(&context);
    reportCaptured(&context, kCodeCrt, "pure virtual call");
}

void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    reportCaptured(&context, kCodeCrt, "invalid parameter");
}

void copyPath(wchar_t (&out)[kPathChars], const QString& path)
{
    out[0] = L'\0';
    if (path.size() >= kPathChars)
        return;
    memcpy(out, path.utf16(), path.size() * sizeof(wchar_t));
    out[path.size()] = L'\0';
}

} // namespace

QString formatFrame(const QString& module, quint64 offset)
{
    Line line;
    putFrame(line, module.toUtf8().constData(), offset);
    return QString::fromUtf8(line.text, line.len);
}

void pruneDumps(const QString& logsDir, int keep)
{
    keep = qMax(keep, 0);
    const QDir dir(logsDir);
    const QStringList dumps = dir.entryList({"crash-*.dmp"}, QDir::Files, QDir::Name);
    for (qsizetype i = 0; i < dumps.size() - keep; ++i)
        QFile::remove(dir.filePath(dumps[i]));
}

void install(const QString& logsDir)
{
    QDir().mkpath(logsDir);
    pruneDumps(logsDir);
    QString log = applog::currentLogPath();
    if (log.isEmpty())
        log = QDir(logsDir).filePath("noite.log");
    copyPath(g_logPath, QDir::toNativeSeparators(QFileInfo(log).absoluteFilePath()));
    copyPath(g_dumpDir, QDir::toNativeSeparators(QDir(logsDir).absolutePath()) + '\\');

    // Nothing may open a window: the report replaces Windows Error Reporting and the CRT's boxes.
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_error_mode(_OUT_TO_STDERR); // a failed C assert() prints instead of opening a box
#ifdef _DEBUG
    // The debug CRT's assert and error reports are message boxes; the debugger gets them instead,
    // and the failed check then goes on to the invalid parameter handler.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif

    // The filter runs on the overflowed stack; this thread (the main one) keeps room for it.
    ULONG reserve = kStackReserve;
    SetThreadStackGuarantee(&reserve);

    SetUnhandledExceptionFilter(onException);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPurecall);
    _set_invalid_parameter_handler(onInvalidParameter);
    signal(SIGABRT, onAbort);
    applog::setFatalHook(onFatal);
}

} // namespace crash
