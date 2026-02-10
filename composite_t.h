#if !defined(__composite_t_h_included)
#define __composite_t_h_included

#include <string>
#include <vector>
#include <sstream>
using namespace std;

class composite_t {
public:
    string inputnumber;
    string expr;

    composite_t(string inputnumber, string enhanced_line) {
        this->inputnumber = inputnumber;
        this->expr = get_argument(enhanced_line, "expr");
    }

    static string get_argument(string line, string argument) {
        if (!line.starts_with("#")) {
            return "";
        }

        vector<pair<string, string>> arguments;
        stringstream ss(line.substr(1));
        ss >> ws;
        string fn, val;
        while (getline(ss, fn, '(')) {
            getline(ss, val, ')');
            if (fn == argument) {
                return val;
            }
        }

        return "";
    }

    static vector<pair<string, string>> get_all_arguments(string line) {
        if (!line.starts_with("#")) {
            return {};
        }

        vector<pair<string, string>> arguments;
        stringstream ss(line.substr(1));
        ss >> ws;
        string fn, val;
        while (getline(ss, fn, '(')) {
            getline(ss, val, ')');
            arguments.push_back(pair(fn, val));
        }

        return arguments;
    }
};

#endif
