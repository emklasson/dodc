/*
dodc (C) Mikael Klasson 2004-2026
fluff@mklasson.com
http://mklasson.com
*/

#include "dodc.h"
#include "dodc_cado_nfs.h"
// #include "dodc_ggnfs.h"
#include "dodc_gmp_ecm.h"
#include "dodc_msieve.h"
// #include "dodc_yafu.h"
#include "multiprocessing.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <queue>
#include <random>
#include <semaphore>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace std;

// #include <windows.h>
// #include <process.h>

counting_semaphore hsem_wu{0};
mutex hmutex_wu; // Controls access to running_worker_threads.
mutex hmutex_wu_result;

queue<workunit_t> wu_result_queue;
set<int> running_worker_threads;   // Thread numbers used by running workers.
atomic<int> reporters_running = 0; // # report_work_thread threads running.

map<string, string> cfg;  // Configuration data from .ini file and cmdline.
map<string, bool> okargs; // Allowed configuration arguments. <name,required>

string okmethods[] = {"ECM", "P-1", "P+1", "MSIEVE_QS", "CADO_SNFS", "CADO_GNFS"}; // Supported methods.

extern char **environ; // For posix_spawnp.

int process_wu_results();

string toupper(string in) {
    string s = in;
    for (auto j = 0; j < s.size(); ++j) {
        s[j] = toupper(s[j]);
    }

    return s;
}

string stripws(string in) {
    string s;
    for (auto j = 0; j < in.size(); ++j) {
        if (!isspace(in[j])) {
            s = in.substr(j);
            break;
        }
    }

    while (s.size() && isspace(s[s.size() - 1])) {
        s.resize(s.size() - 1);
    }

    return s;
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

bool isnumber(string s) {
    for (auto j = 0; j < s.size(); ++j) {
        if (!isdigit(s[j])) {
            return false;
        }
    }

    return true;
}

char tohex(int n) {
    return string("0123456789abcdef")[n % 16];
}

string urlencode(string in) {
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

string pluralise(string singular, int count) {
    return singular + (count > 1 ? "s" : "");
}

struct auto_method_t {
	string method;
	int minsize;
	int maxsize;
};

vector<auto_method_t> get_auto_methods() {
	vector<auto_method_t> methods;
	if (cfg["automethod"] != "") {
		stringstream ss(cfg["automethod"]);
		auto_method_t am;
		while (getline(ss, am.method, ',')) {
			char c;
			ss >> am.minsize >> c >> am.maxsize >> c;	// Skip ';'.
			methods.push_back(am);
		}
	}

	return methods;
}

bool dump_factor(factor_t f) {
    ofstream fout(cfg["submitfailurefile"], ios::app);
    fout << "#method(" << f.method << ")" << "args(" << f.args << ")" << endl;
    fout << f.factorline << endl;
    return true;
}

// Submit multiple factors, one per line, with a comment line above each in the
// format: "#method(methodname)args(args)".
// Failures must be handled by caller as factors will not be saved in here.
// Sets factors[].second to false for each submitted factor.
// Returns number of submitted factors.
int submit_factors(vector<pair<factor_t, bool>> &factors) {
    int successes = 0;
    string factorlines;
    for (auto i = 0; i < factors.size(); ++i) {
        factorlines += (i ? "\n" : "")
			+ format("#method({})args({})\n", factors[i].first.method, factors[i].first.args)
			+ factors[i].first.factorline;
    }

	string result_file = cfg["wgetresultfile"] + "_submit";
    string postdata = "name=" + urlencode(cfg["name"])
		+ "&method=unknown"
		+ "&factors=" + urlencode(factorlines)
		+ "&args=unknown"
		+ "&submitter=" + urlencode("dodc " + version);
    remove(result_file.c_str());

    cout << "Sending factors to server..." << endl;
    int r = system((cfg["wgetcmd"]
		+ " -T " + to_string(toint(cfg["internet_timeout"]) * 60)
		+ " -q --cache=off --output-document=\"" + result_file
		+ "\" --post-data=\"" + postdata + "\" "
		+ cfg["submiturl"]).c_str());
    if (r != 0) {
        cout << "WARNING: wget returned " << r << ". There was probably an error." << endl;
    }

    ifstream f(result_file);
    string line;
    string prefix = "JSONResults:";
    int new_count = 0;
    while (getline(f, line)) {
        if (line.substr(0, prefix.size()) == prefix) {
            for (auto f : factors) {
                auto pos = line.find(f.first.factorline);
                f.second = pos == line.npos;
                successes += f.second ? 0 : 1;
                if (pos != line.npos) {
                    string result = "";
                    string tag = "\"result\":\"";
                    pos = line.find(tag, pos);
                    if (pos != line.npos) {
                        pos += tag.size();
                        auto pos2 = line.find("\"", pos);
                        if (pos2 != line.npos) {
                            result = line.substr(pos, pos2 - pos);
                            if (result == "new") {
                                ++new_count;
                            } else if (result != "old" && result != "") {
                                cout << f.first.factorline << " : " << result << endl;
                            }
                        }
                    }
                }
            }
        }
    }

    if (!successes) {
        cout << "ERROR! Couldn't parse submission result." << endl;
    } else {
        cout << new_count << " new " << pluralise("factor", new_count) << "." << endl;
    }

    return successes;
}

//  Returns true if submitinterval has passed since last time that happened.
bool submit_interval_passed() {
    static time_t lastattempt = 0;
    auto now = time(0);
    if (now - toint(cfg["submitinterval"]) * 60 >= lastattempt) {
        lastattempt = now;
        return true;
    }

    return false;
}

bool report_work_thread(string cmd) {
    ++reporters_running;
    cout << "Reporting completed work... Thanks!\n";
    auto [success, exit_code] = spawn_and_wait(cmd);

    if (success && exit_code != 0) {
        success = false;
        cout << format("WARNING: Wget returned {} while reporting work. There was probably an error.\n", exit_code);
    } else if (!success) {
        cout << "ERROR: Couldn't report work.\n";
    }

    --reporters_running;
    return success;
}

/// @brief Reserves a number on the server in a separate thread.
/// @param number Number in the format k*2^n&plusmn;1;
/// @param cmd Commandline to run.
/// @param wgetcmd Name of wget command.
/// @param result_file Name of temp file to store result in.
void reserve_number_thread(string number, string cmd, string wgetcmd, string result_file) {
	cout << format("Trying to reserve {}...\n", number);
	auto [success, exit_code] = spawn_and_wait(cmd);
	if (!success || exit_code != 0) {
		cout << format("WARNING: Problem running {} while reserving number.\n", wgetcmd);
	}

	ifstream f(result_file);
	string line;
	string msg = format("Couldn't reserve {}.\n", number);
	while (getline(f, line)) {
		if (line.find("I reserved the following number") != string::npos) {
			msg = format("{} reserved successfully.\n", number);
			break;
		}

		string tag = "already reserved by ";
		auto pos = line.find(tag);
		if (pos != string::npos) {
			pos += tag.length();
			auto pos2 = line.find(".<br>", pos);
			string who = pos2 != string::npos ? line.substr(pos, pos2 - pos) : line.substr(pos);
			msg = format("{} is already reserved by {}.\n", number, who);
			break;
		}
	}

	cout << msg;
	f.close();
	remove(result_file.c_str());
}

/// @brief Tries to reserve a number on the server.
/// @param number Number in the format k*2^n&plusmn;1;
void reserve_number(string number) {
	static int cnt = 0;
	string url = cfg["reserve_url"]
		+ "?name=" + urlencode(cfg["name"])
		+ "&numbers=" + urlencode(number);

	string result_file = cfg["wgetresultfile"] + "_reserve" + tostring(cnt++);
	remove(result_file.c_str());

	string cmd = cfg["wgetcmd"]
		+ " -T " + to_string(toint(cfg["internet_timeout"]) * 60)
		+ " -q --cache=off --output-document=\"" + result_file + "\" \""
		+ url + "\"";

	thread t(reserve_number_thread, number, cmd, cfg["wgetcmd"], result_file);
	t.detach();
}

bool download_composites() {
    string url = cfg["compositeurl"]
		+ "?enhanced=true&sort=" + urlencode(cfg["sort"])
		+ "&nmin=" + urlencode(cfg["nmin"])
		+ "&nmax=" + urlencode(cfg["nmax"])
		+ "&order=" + urlencode(cfg["order"])
		+ "&gzip=" + urlencode(cfg["use_gzip"])
		+ "&recommendedwork=" + urlencode(cfg["recommendedwork"])
		+ (cfg["exclude_reservations"] == "yes" ? "&name=" + urlencode(cfg["name"]) : "");
    stringstream ss(cfg["numbers"]);
    int k;
    char plusminus, comma;
    int filenr = 1;
    while (ss >> k >> plusminus) {
        url += "&file" + tostring(filenr++) + "=" + tostring(k) + "t2rn" + (plusminus == '+' ? "p" : "m") + "1.txt";
        ss >> comma;
    }

    cout << "Downloading composites..." << endl;
    int r = system((cfg["wgetcmd"]
		+ " -T " + to_string(toint(cfg["internet_timeout"]) * 60)
		+ " -q --cache=off --output-document=\"" + cfg["compositefile"]
		+ (cfg["use_gzip"] == "yes" ? ".gz" : "") + "\" \""
		+ url + "\"").c_str());

    bool probablyerror = false;
    if (r != 0) {
        cout << "WARNING: wget returned " << r << ". There was probably an error." << endl;
        probablyerror = true;
    }

    if (cfg["use_gzip"] == "yes") {
        r = system((cfg["gzipcmd"] + " -df " + cfg["compositefile"] + ".gz").c_str());
        if (r != 0) {
            cout << "WARNING: gzip returned " << r << ". There was probably an error unpacking composites." << endl;
            probablyerror = true;
        }
    }

    return probablyerror;
}

// Returns true if all previously unsubmitted factors were submitted ok now.
void process_unsubmitted_factors(bool forceattempt) {
    static time_t lastattempt = 0;
    if (!forceattempt && (time(0) - toint(cfg["submitretryinterval"]) * 60 < lastattempt)) {
        return;
    }

    lastattempt = time(0);
    ifstream fin(cfg["submitfailurefile"]);
    if (!fin.is_open()) {
        // Assume file doesn't exist.
        return;
    }

    map<string, string> info;
    info["method"] = cfg["method"]; // Use this if method isn't stored in file.
    string line;
    vector<pair<factor_t, bool>> unsubmitted;
    while (getline(fin, line)) {
        if (!line.size()) {
            continue;
        }

        // Is there extra info present?
        if (line[0] == '#') {
            stringstream ss(line.substr(1));
            string fn, val;
            while (getline(ss, fn, '(')) {
                getline(ss, val, ')');
                info[fn] = val;
            }
            continue;
        }
        unsubmitted.push_back(make_pair(factor_t(line, info["method"], info["args"]), true));
    }

    if (!unsubmitted.size()) {
        return;
    }

    int succeeded = submit_factors(unsubmitted);

    string failstring = "Please submit the factors in " + cfg["submitfailurefile"] + " manually\n"
		+ "or wait and see if dodc manages to submit them automatically later.\n"
		+ "Manual submit: " + cfg["manualsubmiturl"] + "\n";

    if (!succeeded) {
        cout << failstring;
        return;
    }

    // Remove all old failures.
    fin.close();
    remove(cfg["submitfailurefile"].c_str());
    if (succeeded == unsubmitted.size()) {
        cout << "Submitted all " << tostring(succeeded) << " of your unsubmitted factors!" << endl;
        return;
    } else {
        cout << "Submitted " << tostring(succeeded) << " of your "
             << (uint)unsubmitted.size() << " unsubmitted factors!" << endl;
        cout << failstring;
    }

    // Dump remaining unsubmitted to file.
    for (auto i = unsubmitted.begin(); i != unsubmitted.end(); ++i) {
        if (i->second) {
            dump_factor(i->first);
        }
    }

    return;
}

/// @brief Sets a configuration value.
/// @param src Source of the configuration value (e.g. "cmdline", "ini").
/// @param arg Argument name.
/// @param val Value.
/// @return True if the argument is valid and was set, false otherwise.
bool cfg_set(string src, string arg, string val) {
    if (!okargs.count(arg)) {
        cout << "ERROR: unrecognized option: '" << arg << "'" << endl;
        return false;
    }

    cout << src << ": " << arg << " = " << val << "    " << endl;
    cfg[arg] = val;
    return true;
}

bool parse_cmdline(int argc, char **argv) {
    for (int j = 1; j < argc;) {
        if (argv[j][0] == '-') {
            string arg = string(argv[j]).substr(1);
            if (arg == "h" || arg == "-help" || arg == "?") {
                return false; // Just display help info, then exit.
            }

            if (j + 1 >= argc) {
                cout << "ERROR: cmdline switch without argument: " << argv[j] << endl;
                return false;
            }

            if (!cfg_set("cmdline", arg, argv[j + 1])) {
				return false;
			}
            j += 2;
        } else {
            cout << "WARNING: unrecognized cmdline argument: " << argv[j] << endl;
            ++j;
        }
    }

    return true;
}

void adjust_worker_threads(int from, int to) {
    if (from > to) {
        for (int j = from; j > to; --j) {
            cout << format("Waiting for {} worker {} to finish...\n",
                           j - to, pluralise("thread", j - to));
            hsem_wu.acquire();
        }
        if (!to) {
            // Process results before idling in case user aborts.
            process_wu_results();
            cout << "No workers left. Idling...\n";
        }
    } else {
        int count = to - from;
        cout << "Adding " << count << " worker " << pluralise("thread", count) << "." << endl;
        hsem_wu.release(count);
    }
}

/// @brief Reads a .ini file and sets configuration values.
/// @param fname Name of the .ini file to read.
/// @param silent_fail If true, don't print error messages if the file doesn't exist.
/// @return True if the file was read successfully, false otherwise.
bool read_inifile(string fname, bool silent_fail = false) {
    ifstream f(fname);

    if (!f.is_open()) {
        if (!silent_fail) {
            cout << "ERROR: couldn't open .ini file: " << fname << endl;
            cout << "Look in the dodc archive for an example of a proper .ini file." << endl;
        }

        return false;
    }

    string line;
    while (getline(f, line)) {
		string arg, val;
        stringstream ss(line);
        getline(ss, arg, '=');
        ss >> ws;
        arg = stripws(arg);
        if (!getline(ss, val)) {
            continue;
        }

        val = stripws(val);
        if (arg.substr(0, 2) == "//" || arg.substr(0, 1) == "#" || arg.substr(0, 1) == ";") {
            continue;
        }

        if (!cfg_set("ini", arg, val)) {
            return false;
        }
    }

    return true;
}

void read_live_config() {
    string live_filename = "dodc_live.ini";
    int threads = toint(cfg["worker_threads"]);

    if (!read_inifile(live_filename, true)) {
        return;
    }

    int new_threads = toint(cfg["worker_threads"]);
    if (new_threads != threads) {
        adjust_worker_threads(threads, new_threads);
    }

    remove(live_filename.c_str());
}

bool init_args() {
    string reqargs[] = {
		"name", "b1", "b1increase", "curves", "nmin", "nmax", "loop", "numbers",
		"autosubmit", "autodownload", "reportwork", "wgetcmd", "ecmcmd", "gzipcmd",
		"use_gzip", "sort", "order", "compositefile", "compositeurl", "submiturl",
		"manualsubmiturl", "reporturl", "factorfile", "submitfailurefile", "sigmafile",
		"wgetresultfile", "ecmresultfile", "recommendedwork", "method", "submitretryinterval",
		"worker_threads", "submitinterval", "internet_timeout", "pcore_workers",
		"exclude_reservations", "reserve_url", "auto_reserve"};
    string optargs[] = {"ecmargs", "fallback", "automethod", "less_spam"};
    for (auto j = 0; j < sizeof(reqargs) / sizeof(string); ++j) {
        okargs[reqargs[j]] = true;
    }

    for (auto j = 0; j < sizeof(optargs) / sizeof(string); ++j) {
        okargs[optargs[j]] = false;
    }

    return true;
}

/// @brief Verifies that a method is known and supported.
/// @param method Method to check.
/// @return True if OK, otherwise false.
bool verify_method(string method) {
    for (auto j = 0; j < sizeof(okmethods) / sizeof(string); ++j) {
        if (method == okmethods[j]) {
            return true;
        }
    }

    return false;
}

bool verify_args() {
    bool ok = true;
    for (auto i = okargs.begin(); i != okargs.end(); ++i) {
        if (i->second && cfg[i->first] == "") {
            cout << "ERROR: missing required setting: " << i->first << endl;
            ok = false;
        }
    }

    if (toint(cfg["nmin"]) > toint(cfg["nmax"])) {
        cout << "ERROR: nmin=" << cfg["nmin"] << " > nmax=" << cfg["nmax"] << " doesn't make sense." << endl;
        ok = false;
    }

    cfg["method"] = toupper(cfg["method"]);
    if (!verify_method(cfg["method"])) {
        cout << "ERROR: unrecognized method: " << cfg["method"] << endl;
        ok = false;
    }

	for (auto& am : get_auto_methods()) {
		if (!verify_method(am.method)) {
			cout << "ERROR: unrecognized automethod: " << am.method << endl;
			ok = false;
		}
	}

    if (cfg["method"] == "P-1") {
        cfg["ecmargs"] += " -pm1";
        if (toint(cfg["curves"]) != 1) {
            cout << "WARNING: running " << cfg["curves"] << " P-1 tests doesn't make sense. Forcing <curves> to 1." << endl;
            cfg["curves"] = 1;
        }
    } else if (cfg["method"] == "P+1") {
        cfg["ecmargs"] += " -pp1";
        if (toint(cfg["curves"]) > 3) {
            cout << "WARNING: running " << cfg["curves"] << " P+1 tests is probably a waste. Try 3 instead." << endl;
        }
    }

    return ok;
}

void found_factor(string foundfactor, bool enhanced, string expr, string inputnumber, string method, string args) {
    ofstream fout(cfg["factorfile"], ios::app);
    stringstream factorline;
    if (enhanced) {
        factorline << foundfactor << " | " << expr;
    } else {
        factorline << foundfactor << " | " << inputnumber;
    }

    cout << factorline.str() << "\t" << "(" << tostring(foundfactor.size()) << " digits)" << endl;
    fout << factorline.str() << endl;
    if (cfg["autosubmit"] == "yes") {
        dump_factor(factor_t(factorline.str(), method, args));
    }
}

/// @brief Initialises the composites file. Shuffling if "order = random".
/// Processes recommended work settings from server if "recommendedwork = yes".
/// @return Number of composites in the file.
int init_composites() {
    int cnt = 0;
    string line;
    if (cfg["recommendedwork"] == "yes") {
        ifstream f(cfg["compositefile"]);
        getline(f, line);
        if (line.substr(0, 1) != "#") {
            cout << "WARNING: trying to do recommended work, but didn't get any settings from server." << endl;
        } else {
            stringstream ss(line.substr(1));
            ss >> ws;
            string fn, val;
            while (getline(ss, fn, '(')) {
                getline(ss, val, ')');
                cfg_set("recommended work", fn, val);
            }
        }
    }

    ifstream f(cfg["compositefile"]);
    if (cfg["order"] == "random") {
        vector<pair<string, string>> v;
        string comment;
        while (getline(f, line)) {
            if (line.substr(0, 1) != "#") {
                ++cnt;
                v.push_back(make_pair(line, comment));
                comment = "";
            } else {
                comment = line;
            }
        }

        f.close();
        random_device rd;
        mt19937 g(rd());
        shuffle(v.begin(), v.end(), g);
        ofstream fout(cfg["compositefile"]);
        for (auto j = 0; j < v.size(); ++j) {
            if (v[j].second != "") {
                fout << v[j].second << endl;
            }

            fout << v[j].first << endl;
        }
    } else {
        while (getline(f, line)) {
            if (line.substr(0, 1) != "#") {
                ++cnt;
            }
        }
    }

    return cnt;
}

void free_workunit(workunit_t *pwu) {
    lock_guard lock(hmutex_wu);

    running_worker_threads.erase(pwu->threadnumber);
    delete pwu;
    hsem_wu.release();
}

workunit_t *get_workunit() {
    read_live_config();
    while (!hsem_wu.try_acquire_for(chrono::seconds(5))) {
        read_live_config();
    }

    lock_guard lock(hmutex_wu);
    workunit_t *pwu = new workunit_t;

    // Use the lowest free thread number.
    for (int n = 1;; ++n) {
        if (!running_worker_threads.contains(n)) {
            pwu->threadnumber = n;
            running_worker_threads.insert(n);
            break;
        }
    }

    return pwu;
}

void add_wu_result(workunit_t *pwu) {
    lock_guard lock(hmutex_wu_result);

    wu_result_queue.push(*pwu);
}

/// @brief Processes results from work units.
/// @return Number of found factorisations.
int process_wu_results() {
    lock_guard lock(hmutex_wu_result);

    int found = 0;
    while (wu_result_queue.size()) {
        workunit_t &wu = wu_result_queue.front();
        stringstream ss(wu.result.factor);
        string factor;
        while (ss >> factor) {
            found_factor(factor, wu.enhanced, wu.expr, wu.inputnumber, wu.result.method, wu.result.args);

            // Trial factor found factor if it's small.
            if (factor.size() <= 10) {
                auto n = touint64(factor);
                for (uint_fast64_t f = 3; f * f <= n; f += 2) {
                    if (!(n % f)) {
                        found_factor(tostring(f), wu.enhanced, wu.expr, wu.inputnumber, wu.result.method, wu.result.args);
                        do {
                            n /= f;
                        } while (!(n % f));
                    }
                }

                if (n > 1 && n != touint64(factor)) {
                    found_factor(tostring(n), wu.enhanced, wu.expr, wu.inputnumber, wu.result.method, wu.result.args);
                }
            }
        }

        wu_result_queue.pop();
        ++found;
    }

    if (found > 0) {
        if (submit_interval_passed()) {
            process_unsubmitted_factors(true);
        } else {
            cout << "Factor(s) saved in " << cfg["submitfailurefile"] << " for now." << endl;
        }
    }

    return found;
}

void process_workunit(void *p) {
    workunit_t *pwu = (workunit_t *)p;

    if (pwu->handler(*pwu)) {
		add_wu_result(pwu);
	}

    free_workunit(pwu);
}

// Returns true if a factor was found.
void do_workunit(string inputnumber, bool enhanced, string expr) {
    workunit_t *pwu = get_workunit();
    workunit_t &wu = *pwu;

    wu.inputnumber = inputnumber;
    wu.enhanced = enhanced;
    wu.expr = expr;

    string method = cfg["method"];
	for (auto& am : get_auto_methods()) {
		if (am.minsize <= inputnumber.size() && am.maxsize >= inputnumber.size()) {
			method = am.method;
			break;
		}
	}

    cout << "[" << wu.threadnumber << "] ";
    method = toupper(method);
    string msg;
    if (enhanced) {
        msg = format("Factoring {} c{}", expr, inputnumber.size());
    } else {
        msg = format("Factoring {}", inputnumber);
    }

    string tab = string(8 - (msg.size() % 8), ' ');
    msg = format("{}{}[{}]    ", msg, tab, method);
    if (cfg["less_spam"] == "yes") {
        cout << msg << "\r" << flush;
    } else {
        cout << msg << endl;
    }

    if (method == "MSIEVE_QS") {
        wu.tempfile = "msieve" + tostring(wu.threadnumber);
        wu.method = method;
        wu.handler = do_workunit_msieve;
	// } else if( method == "GGNFS_SNFS" ) {
	// 	wu.tempfile = "dodc_ggnfs_snfs_" + tostring( wu.threadnumber );
	// 	wu.method = method;
	// 	wu.handler = do_workunit_ggnfs_snfs;
	// } else if( method == "YAFU_QS" ) {
	// 	wu.tempfile = "dodc_yafu_qs_" + tostring( wu.threadnumber );
	// 	wu.method = method;
	// 	wu.handler = do_workunit_yafu;
    } else if (method == "CADO_SNFS" || method == "CADO_GNFS") {
        wu.tempfile = "dodc_cado_nfs_" + tostring(wu.threadnumber);
        wu.method = method;
        wu.handler = do_workunit_cado_nfs;
    } else {
        wu.tempfile = cfg["ecmresultfile"] + tostring(wu.threadnumber);
        wu.cmdline = "echo " + wu.inputnumber + " | " + cfg["ecmcmd"] + " -c " + cfg["curves"] + " " + cfg["ecmargs"] + " " + cfg["b1"] + " > " + wu.tempfile;
        wu.method = method;
        wu.b1 = cfg["b1"];
        wu.handler = do_workunit_gmp_ecm;
    }

    wu.schedule_bg = wu.threadnumber > toint(cfg["pcore_workers"]);

	int auto_reserve = toint(cfg["auto_reserve"]);
	if (auto_reserve > 0
		&& pwu->inputnumber.size() >= auto_reserve
		&& (pwu->method.contains("NFS")
			|| pwu->method.contains("QS"))) {
		reserve_number(pwu->expr);
	}

    // _beginthread( process_workunit, 0, pwu );
    thread t(process_workunit, pwu);
    t.detach();
}

/// @brief Sets the process priority.
/// @param priority The priority level (0-4 with 0 being idle and 4 being high).
void set_priority(int priority = 0) {
#if defined(WIN32)
    unsigned int aprio[] = {IDLE_PRIORITY_CLASS, BELOW_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS,
                            ABOVE_NORMAL_PRIORITY_CLASS, HIGH_PRIORITY_CLASS};
    unsigned int atprio[] = {THREAD_PRIORITY_IDLE, THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
                             THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL, THREAD_PRIORITY_HIGHEST};
    SetPriorityClass(GetCurrentProcess(), aprio[priority]);
    SetThreadPriority(GetCurrentThread(), atprio[0]);
#else // linux or macOS
    setpriority(PRIO_PROCESS, 0, 20 - 10 * priority);

    // macOS:
    // setpriority( PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG ); // Only schedules on efficiency cores.
    // pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
}

int main(int argc, char **argv) {
    cout << "dodc " << version << " by Mikael Klasson" << endl;
    cout << "usage: dodc [<settings>]" << endl;
    cout << "  Make sure you're using the right username." << endl;
    cout << "  Look in dodc.ini for available options." << endl;
    srand((uint)time(0));
    init_args();
    if (!read_inifile("dodc.ini")) {
		return 1;
	}

    if (!parse_cmdline(argc, argv)) {
        return 1;
    }

    if (!verify_args()) {
        cout << "Fix your settings and try again. Exiting." << endl;
        return 1;
    }

    cout << "Using factorization method " << cfg["method"] << endl;

    set_priority();
    adjust_worker_threads(0, toint(cfg["worker_threads"]));

    int totalfactors = 0;
    do {
        process_unsubmitted_factors(true);

        if (cfg["autodownload"] == "yes") {
            download_composites();
        }

        int ccnt = init_composites();
        cout << "Found " << ccnt << " composites in " << cfg["compositefile"] << "." << endl;
        if (!ccnt) {
            // Composite file is empty.
            if (cfg["fallback"] == "yes" && cfg["recommendedwork"] != "yes") {
                cout << "Switching to fallback mode." << endl;
                cfg["recommendedwork"] = "yes";
                cfg["autodownload"] = "yes";
                continue;
            } else {
                cout << "Work complete. Exiting." << endl;
                break;
            }
        }

        ifstream fin(cfg["compositefile"]);
        bool enhanced = false;
        string line, expr;
        while (getline(fin, line)) {
            if (line.substr(0, 1) == "#") {
                stringstream ss(line.substr(1));
                ss >> ws;
                string fn, val;
                while (getline(ss, fn, '(')) {
                    getline(ss, val, ')');
                    if (fn == "expr") {
                        expr = val;
                        enhanced = true;
                    }
                }

                continue;
            }

            do_workunit(line, enhanced, expr);
            totalfactors += process_wu_results();
            process_unsubmitted_factors(false);
        }

        cout << "#factors found: " << totalfactors << "    " << endl;
        if (cfg["reportwork"] == "yes") {
            string url = cfg["reporturl"]
				+ "?method=" + urlencode(cfg["method"])
				+ "&numbers=" + urlencode(cfg["numbers"])
				+ "&nmin=" + urlencode(cfg["nmin"])
				+ "&nmax=" + urlencode(cfg["nmax"])
				+ "&b1=" + urlencode(cfg["b1"])
				+ "&curves=" + urlencode(cfg["curves"]);
            string cmd = cfg["wgetcmd"]
				+ " -T " + to_string(toint(cfg["internet_timeout"]) * 60)
				+ " -q --cache=off"
				+ " --output-document=\"" + cfg["wgetresultfile"] + "_report\""
				+ " \"" + url + "\"";
            thread t(report_work_thread, cmd);
            t.detach();
        }

        cfg["b1"] = tostring(touint64(cfg["b1"]) + touint64(cfg["b1increase"]));
        cout << "Increasing B1 to " << cfg["b1"] << endl;
    } while (cfg["loop"] == "yes");

    adjust_worker_threads(toint(cfg["worker_threads"]), 0);

    totalfactors += process_wu_results();
    cout << "#factors found: " << totalfactors << endl;

    // Do one last valiant attempt to submit any unsubmitted factors.
    process_unsubmitted_factors(true);

    while (reporters_running > 0) {
        cout << "Waiting for " << reporters_running << " work reporting "
             << pluralise("thread", reporters_running) << " to finish..." << endl;
        this_thread::sleep_for(chrono::seconds(5));
    }

    remove(cfg["wgetresultfile"].c_str());
    remove(cfg["ecmresultfile"].c_str());

    cout << "All done! Exiting." << endl;
}
