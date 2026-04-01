#if !defined(__cfg_base_t_h_included)
#define __cfg_base_t_h_included

#include <map>
#include <print>
#include <string>
using namespace std;
using LogFunc = void(*)(std::string_view);

class cfg_base_t {
private:
    map<string, bool *> bools;
    map<string, long long *> ints;
    map<string, double *> floats;
    map<string, string *> strings;

    LogFunc logger;

    template<typename... Args>
    void log(string_view format, Args&&... args) {
        string s = vformat(format, make_format_args(args...));
        if (logger) {
            logger(s);
        } else {
            print("{}", s);
        }
    }

public:
    string filename;
    cfg_base_t(string filename);

    /// @brief Sets a custom logging function to use instead of print.
    /// @param func Logging function, taking a single string_view argument.
    void set_logger(LogFunc func) {
        logger = func;
    }

    void add(string name, bool &var, bool val = false);
    void add(string name, long long &var, long long val = 0);
    void add(string name, double &var, double val = 0);
    void add(string name, string &var, string val = "");

    /// @brief Sets a configuration value.
    /// @param source Source of the value (e.g. "cmdline", "cfg").
    /// @param name Setting name.
    /// @param value Value.
    /// @return True if the setting exists and was set, otherwise false.
    bool set(string source, string name, string value);

    /// @brief Reads a configuration file and sets configuration values.
    /// @param filename Name of the configuration file to read.
    /// @param silent_fail If true, don't print an error message if the file doesn't exist.
    /// @return True if the file was read successfully, otherwise false.
    bool read(string filename = "", bool silent_fail = false);
};

#endif
