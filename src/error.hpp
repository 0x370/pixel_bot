// src/error.hpp — fatal error model with std::stacktrace.
//
// Every failure is fatal: throw FatalError{msg}; there are no error codes,
// fallbacks, or retries. The stacktrace is captured at the throw site so it
// points at the failing call, not main. SIGINT/SIGTERM are the graceful exit
// path (they release the shutdown semaphore); the five fatal signals dump a
// trace and exit. std::stacktrace::current() is not async-signal-safe — an
// accepted tradeoff (see plan step 1); the warmup call primes the lazy
// backend so the common segfault path still produces a readable trace.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <print>
#include <csignal>
#include <exception>
#include <stdexcept>
#include <stacktrace>
#include <string>
#include <semaphore>

namespace pixelbot {

// Fatal exception carrying the throw-site stacktrace. std::runtime_error owns
// its message string, so what() stays valid for the exception's lifetime.
class FatalError : public std::runtime_error {
public:
    explicit FatalError(std::string msg)
        : std::runtime_error{std::move(msg)}, trace_{std::stacktrace::current()} {}
    [[nodiscard]] const std::stacktrace& trace() const noexcept { return trace_; }
private:
    std::stacktrace trace_;
};

// Normal exception path: message + trace + exit.
[[noreturn]] inline void die(const FatalError& err) {
    std::println(stderr, "pixelbot: {}", err.what());
    std::println(stderr, "{}", std::to_string(err.trace()));
    std::fflush(stderr);
    std::quick_exit(1);
}

// Fatal-signal handler: name + trace + _exit. Not async-signal-safe; accepted.
[[noreturn]] inline void die_signal(int signo) {
    const char* name = "unknown";
    switch (signo) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGILL:  name = "SIGILL";  break;
        case SIGBUS:  name = "SIGBUS";  break;
    }
    std::println(stderr, "pixelbot: fatal signal {}", name);
    std::println(stderr, "{}", std::to_string(std::stacktrace::current()));
    std::fflush(stderr);
    _exit(1);
}

// Graceful-shutdown semaphore, set once by main and released by SIGINT/SIGTERM.
// An inline variable (C++17) so every TU observes the same pointer.
inline std::binary_semaphore* g_shutdown_sem = nullptr;
inline void set_shutdown_semaphore(std::binary_semaphore* s) noexcept {
    g_shutdown_sem = s;
}
inline void shutdown_signal(int /*signo*/) noexcept {
    if (g_shutdown_sem) g_shutdown_sem->release();
}

// set_terminate backstop: a stray throw with no handler still traces + aborts.
inline void terminate_handler() {
    std::println(stderr, "pixelbot: unhandled exception");
    try { std::rethrow_exception(std::current_exception()); }
    catch (const FatalError& e) { std::println(stderr, "{}", e.what()); }
    catch (const std::exception& e) { std::println(stderr, "{}", e.what()); }
    catch (...) { std::println(stderr, "unknown exception type"); }
    std::println(stderr, "{}", std::to_string(std::stacktrace::current()));
    std::fflush(stderr);
    std::abort();
}

// First thing main() calls: warm up the stacktrace backend, install the
// terminate handler, the five fatal-signal handlers (die_signal, no SA_RESTART),
// and the two graceful-shutdown handlers (SIGINT/SIGTERM -> semaphore release).
inline void install_crash_handlers() {
    // Prime libstdc++'s lazy backtrace backend before any signal can fire.
    { [[maybe_unused]] auto _ = std::stacktrace::current(); }
    std::set_terminate(terminate_handler);

    auto install = [](int sig, void(*h)(int), int flags) {
        struct sigaction sa{};
        sa.sa_flags = flags;
        sa.sa_handler = h;
        sigemptyset(&sa.sa_mask);
        sigaction(sig, &sa, nullptr);
    };
    // Fatal: dump trace, _exit. No SA_RESTART.
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
        install(sig, &die_signal, 0);
    // Graceful: release shutdown semaphore.
    for (int sig : {SIGINT, SIGTERM})
        install(sig, &shutdown_signal, SA_RESTART);
}

} // namespace pixelbot
