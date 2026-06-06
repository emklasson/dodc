#include "cfg_base_t.h"
#include "string_utils.h"
#include <fstream>
#include <sstream>
using namespace std;

cfg_base_t::cfg_base_t(string filename) {
    this->filename = filename;
}

void cfg_base_t::add(string name, bool &var, bool val) {
    var = val;
    bools[name] = &var;
}

void cfg_base_t::add(string name, long long &var, long long val) {
    var = val;
    ints[name] = &var;
}

void cfg_base_t::add(string name, double &var, double val) {
    var = val;
    floats[name] = &var;
}

void cfg_base_t::add(string name, string &var, string val) {
    var = val;
    strings[name] = &var;
}

bool cfg_base_t::read(string filename, bool silent_fail) {
    if (filename == "") {
        filename = this->filename;
    }

    ifstream f(filename);
    if (!f.is_open()) {
        if (!silent_fail) {
            log("ERROR: couldn't open config file: {}\n", filename);
        }

        return false;
    }

    string line;
    while (getline(f, line)) {
		string arg, val;
        stringstream ss(line);
        if (!getline(ss, arg, '=')) {
            continue;
        }

        arg = trim(arg);
        if (arg.starts_with("//") || arg.starts_with("#") || arg.starts_with(";")) {
            continue;
        }

        ss >> ws;
        getline(ss, val);
        val = trim(val);

        if (!set("cfg", arg, val)) {
            return false;
        }
    }

    return true;
}

double parse_size(string s) {
    auto suffixMap = map<char, double>({
        {'K', 1e3},
        {'M', 1e6},
        {'G', 1e9},
        {'T', 1e12},
        {'P', 1e15},
        {'E', 1e18},
        {'Z', 1e21},
        {'Y', 1e24},
        {'R', 1e27},
        {'Q', 1e30},
    });
    double multiplier = s.empty() ? 1 : suffixMap[toupper(s.back())];
    if (!multiplier) {
        multiplier = 1;
    } else {
        s.pop_back();
    }

    return stod(s) * multiplier;
}

bool cfg_base_t::set(string source, string name, string value) {
    try {
        if (bools.count(name)) {
            value = toupper(value);
            *bools[name] = (value == "TRUE" || value == "1" || value == "YES" || value == "ON");
        } else if (ints.count(name)) {
            // Use stod for scientific notation support. Lose precision over
            // 2^53-1 but that's fine.
            *ints[name] = parse_size(value);
        } else if (floats.count(name)) {
            *floats[name] = parse_size(value);
        } else if (strings.count(name)) {
            *strings[name] = value;
        } else {
            log("ERROR: unrecognized option in {}: '{}'\n", source, name);
            return false;
        }
    } catch (...) {
        log("ERROR: error converting config value: {} = {}\n", name, value);
        return false;
    }

    log("{}: {} = {}\n", source, name, value);
    return true;
}