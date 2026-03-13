#if !defined(__dodc_h_included)
#define __dodc_h_included

#include "multiprocessing.h"
#include <mutex>
#include <print>
#include <string>
using namespace std;

const string version = "v1.52";

extern bool log_prefix_newline;
extern int log_pad_width;
extern mutex log_mutex;

struct factor_t {
    string factorline, method, args;
    factor_t(string _factorline, string _method, string _args) {
        factorline = _factorline;
        method = _method;
        args = _args;
    }
};

struct workunit_result_t {
    string factor, method, args;
};

struct workunit_t {
    string inputnumber;
    string tempfile;
    string cmdline;
    string method;
    string b1;
    bool schedule_bg; // Schedule work in Background process? (macOS)
    workunit_result_t result;
    bool (*handler)(workunit_t &wu);

    bool enhanced;
    string expr;

    int threadnumber;
};

/// @brief Prints a formatted message to stdout.
/// If the last message printed ended in '\r' then a newline is printed first
/// unless this message also ends in '\r'.
template<typename... Args>
void log_save_r(string_view format, Args&&... args)
{
    lock_guard lock(log_mutex);
    string s = vformat(format, make_format_args(args...));
    if (s.empty()) {
        return;
    }

    print("{}{}",
        log_prefix_newline && (s.back() != '\r') ? "\n" : "",
        s);
    log_prefix_newline = s.back() == '\r';

    if (log_prefix_newline) {
        fflush(stdout);
    }
}

/// @brief Prints a formatted message to stdout.
/// Pads output to overwrite any previous message ending in '\r'.
/// If the message contains neither '\r' nor '\n' then it's printed as is.
template<typename... Args>
void log(string_view format, Args&&... args)
{
    lock_guard lock(log_mutex);
    string s = vformat(format, make_format_args(args...));
    if (s.empty()) {
        return;
    }

    auto i = s.find_first_of("\r\n");
    if (i == string::npos) {
        print("{}", s);
    } else {
        print("{:{}}{}",
            s.substr(0, i),
            log_pad_width,
            s.substr(i));
    }

    if (s.back() != '\n') {
        fflush(stdout);
        log_pad_width = s.size();
    } else {
        log_pad_width = 0;
    }
}

#endif
