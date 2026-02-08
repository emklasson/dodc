#include "string_utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>
using namespace std;

string trim(string_view s) {
    auto start = s.find_first_not_of(" \t\r\n");
    auto end = s.find_last_not_of(" \t\r\n");
    if (start == s.npos) {
        return "";
    }

    return string(s.substr(start, end - start + 1));
}

string toupper(const string &in) {
    string s = in;
    for_each(s.begin(), s.end(), [](char &c) { c = toupper((unsigned char)c); });
    return s;
}

string toyesno(bool b) {
    return b ? "yes" : "no";
}

string tostring(long long n) {
    stringstream ss;
    ss << n;
    return ss.str();
}

int toint(string s) {
    stringstream ss(s);
    int n;
    ss >> n;
    return n;
}

uint_fast64_t touint64(string s) {
    stringstream ss(s);
    uint_fast64_t n;
    ss >> n;
    return n;
}

bool isnumber(string_view s) {
    for (auto j = 0; j < s.size(); ++j) {
        if (!isdigit(s[j])) {
            return false;
        }
    }

    return true;
}

char tohex(int n) {
    return "0123456789abcdef"[n % 16];
}

string urlencode(string_view in) {
    string s;
    for (auto j = 0; j < in.size(); ++j) {
        if (isalnum(in[j])) {
            s += in[j];
        } else if (in[j] == ' ') {
            s += '+';
        } else {
            s += '%';
            s += tohex(in[j] / 16);
            s += tohex(in[j] % 16);
        }
    }

    return s;
}

// "11000000" -> "11e6"
string scientify(string n) {
    int e = 0;
    while (n.size() > 1 && n[n.size() - 1] == '0') {
        ++e;
        n.resize(n.size() - 1);
    }

    if (e < 4) {
        return n + string(e, '0');
    }

    return n + "e" + tostring(e);
}

string pluralise(const string &singular, int count) {
    return singular + (count != 1 ? "s" : "");
}
