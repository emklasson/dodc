/*
dodc (C) Mikael Klasson 2004-2026
fluff@mklasson.com
http://mklasson.com
*/

#include "cfg_t.h"
#include "dodc.h"
#include "dodc_cado_nfs.h"
#include "dodc_gmp_ecm.h"
#include "dodc_msieve.h"
#include "multiprocessing.h"
#include "string_utils.h"
#include <algorithm>
#include <cctype>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
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
mutex log_mutex; // Controls access to log.

queue<workunit_t> wu_result_queue;
set<int> running_worker_threads;        // Thread numbers used by running workers.
atomic<int> running_helper_threads = 0; // Number of running helper threads.
atomic_flag quit = false;               // Set to true by SIGINT handler to exit program.

cfg_t cfg;  // Configuration data from .cfg file and cmdline.

string okmethods[] = {"ECM", "P-1", "P+1", "MSIEVE_QS", "CADO_SNFS", "CADO_GNFS"}; // Supported methods.

int total_factors = 0; // Total number of found factors.

bool log_prefix_newline = false; // Used by log().

extern char **environ; // For posix_spawnp.

int process_wu_results();
void check_quit();
void cleanup_and_exit();
void block_sigint();

struct auto_method_t {
	string method;
	int minsize;
	int maxsize;
};

vector<auto_method_t> get_auto_methods() {
	vector<auto_method_t> methods;
	if (cfg.auto_method != "") {
		stringstream ss(cfg.auto_method);
		auto_method_t am;
		while (getline(ss, am.method, ',')) {
			char c;
			ss >> am.minsize >> c >> am.maxsize >> c;	// Skip ';'.
			methods.push_back(am);
		}
	}

	return methods;
}

/// @brief Blocks SIGINT in the current thread.
void block_sigint() {
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGINT);
    pthread_sigmask(SIG_BLOCK, &sigset, nullptr);
}

bool dump_factor(factor_t f) {
    ofstream fout(cfg.submit_failure_file, ios::app);
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

	string result_file = cfg.wget_result_file + "_submit";
    string postdata = "name=" + urlencode(cfg.name)
		+ "&method=unknown"
		+ "&factors=" + urlencode(factorlines)
		+ "&args=unknown"
		+ "&submitter=" + urlencode("dodc " + version);
    remove(result_file.c_str());

    log("Sending factors to server...\n");
    int r = system((cfg.wget_cmd
		+ " -T " + to_string(cfg.internet_timeout * 60)
		+ " -q --cache=off --output-document=\"" + result_file
		+ "\" --post-data=\"" + postdata + "\" "
		+ cfg.submit_url).c_str());
    if (r != 0) {
        log("WARNING: wget returned {} while submitting factors. There was probably an error.\n", r);
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
                                log("{} : {}\n", f.first.factorline, result);
                            }
                        }
                    }
                }
            }
        }
    }

    if (!successes) {
        log("ERROR! Couldn't parse submission result.\n");
    } else {
        log("{} new {}.\n", new_count, pluralise("factor", new_count));
    }

    return successes;
}

//  Returns true if submit_interval has passed since last time that happened.
bool submit_interval_passed() {
    static time_t lastattempt = 0;
    auto now = time(0);
    if (now - cfg.submit_interval * 60 >= lastattempt) {
        lastattempt = now;
        return true;
    }

    return false;
}

bool report_work_thread(string cmd) {
    block_sigint();
    ++running_helper_threads;
    log("Reporting completed work... Thanks!\n");
    auto [success, exit_code] = spawn_and_wait(cmd);

    if (success && exit_code != 0) {
        success = false;
        log("WARNING: Wget returned {} while reporting work. There was probably an error.\n", exit_code);
    } else if (!success) {
        log("ERROR: Couldn't report work.\n");
    }

    --running_helper_threads;
    return success;
}

/// @brief Reserves a number on the server in a separate thread.
/// @param number Number in the format k*2^n&plusmn;1;
/// @param cmd Commandline to run.
/// @param wget_cmd Name of wget command.
/// @param result_file Name of temp file to store result in.
void reserve_number_thread(string number, string cmd, string wget_cmd, string result_file) {
    block_sigint();
    ++running_helper_threads;
	log("Trying to reserve {}...\n", number);
	auto [success, exit_code] = spawn_and_wait(cmd);
	if (!success || exit_code != 0) {
		log("WARNING: Problem running {} while reserving number.\n", wget_cmd);
	}

	ifstream f(result_file);
	string line;
	bool handled = false;
	while (getline(f, line)) {
		if (line.find("I reserved the following number") != string::npos) {
			log("{} reserved successfully.\n", number);
			handled = true;
			break;
		}

		string tag = "already reserved by ";
		auto pos = line.find(tag);
		if (pos != string::npos) {
			pos += tag.length();
			auto pos2 = line.find(".<br>", pos);
			string who = pos2 != string::npos ? line.substr(pos, pos2 - pos) : line.substr(pos);
			log("{} is already reserved by {}.\n", number, who);
			handled = true;
			break;
		}
	}

	if (!handled) {
		log("Couldn't reserve {}.\n", number);
	}

	f.close();
	remove(result_file.c_str());
    --running_helper_threads;
}

/// @brief Tries to reserve a number on the server.
/// @param number Number in the format k*2^n&plusmn;1;
void reserve_number(string number) {
	static int cnt = 0;
	string url = cfg.reserve_url
		+ "?name=" + urlencode(cfg.name)
		+ "&numbers=" + urlencode(number);

	string result_file = cfg.wget_result_file + "_reserve" + tostring(cnt++);
	remove(result_file.c_str());

	string cmd = cfg.wget_cmd
		+ " -T " + to_string(cfg.internet_timeout * 60)
		+ " -q --cache=off --output-document=\"" + result_file + "\" \""
		+ url + "\"";

	thread t(reserve_number_thread, number, cmd, cfg.wget_cmd, result_file);
	t.detach();
}

bool download_composites() {
    string url = cfg.composite_url
		+ "?enhanced=true&sort=" + urlencode(cfg.sort)
		+ "&nmin=" + tostring(cfg.nmin)
		+ "&nmax=" + tostring(cfg.nmax)
		+ "&order=" + urlencode(cfg.order)
		+ "&gzip=" + toyesno(cfg.use_gzip)
		+ "&recommendedwork=" + toyesno(cfg.recommended_work)
		+ (cfg.exclude_reservations ? "&name=" + urlencode(cfg.name) : "");
    stringstream ss(cfg.numbers);
    int k;
    char plusminus, comma;
    int filenr = 1;
    while (ss >> k >> plusminus) {
        url += "&file" + tostring(filenr++) + "=" + tostring(k) + "t2rn" + (plusminus == '+' ? "p" : "m") + "1.txt";
        ss >> comma;
    }

    log("Downloading composites...\n");
    int r = system((cfg.wget_cmd
		+ " -T " + to_string(cfg.internet_timeout * 60)
		+ " -q --cache=off --output-document=\"" + cfg.composite_file
		+ (cfg.use_gzip ? ".gz" : "") + "\" \""
		+ url + "\"").c_str());

    bool probablyerror = false;
    if (r != 0) {
        log("WARNING: wget returned {} while downloading composites. There was probably an error.\n", r);
        probablyerror = true;
    }

    if (cfg.use_gzip) {
        r = system((cfg.gzip_cmd + " -df " + cfg.composite_file + ".gz").c_str());
        if (r != 0) {
            log("WARNING: gzip returned {} while unpacking composites. There was probably an error.\n", r);
            probablyerror = true;
        }
    }

    return probablyerror;
}

// Returns true if all previously unsubmitted factors were submitted ok now.
void process_unsubmitted_factors(bool forceattempt) {
    static time_t lastattempt = 0;
    if (!forceattempt && (time(0) - cfg.submit_retry_interval * 60 < lastattempt)) {
        return;
    }

    lastattempt = time(0);
    ifstream fin(cfg.submit_failure_file);
    if (!fin.is_open()) {
        // Assume file doesn't exist.
        return;
    }

    map<string, string> info;
    info["method"] = cfg.method; // Use this if method isn't stored in file.
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

	auto print_fail = []() {
		log("Please submit the factors in {} manually\n"
			"or wait and see if dodc manages to submit them automatically later.\n"
			"Manual submit: {}\n",
			cfg.submit_failure_file,
			cfg.manual_submit_url);
	};

    if (!succeeded) {
        print_fail();
        return;
    }

    // Remove all old failures.
    fin.close();
    remove(cfg.submit_failure_file.c_str());
    if (succeeded == unsubmitted.size()) {
        log("Submitted all {} of your unsubmitted factors!\n", succeeded);
        return;
    } else {
        log("Submitted {} of your {} unsubmitted factors!\n", succeeded, unsubmitted.size());
        print_fail();
    }

    // Dump remaining unsubmitted to file.
    for (auto i = unsubmitted.begin(); i != unsubmitted.end(); ++i) {
        if (i->second) {
            dump_factor(i->first);
        }
    }
}

bool parse_cmdline(int argc, char **argv) {
    for (int j = 1; j < argc;) {
        if (argv[j][0] == '-') {
            string arg = string(argv[j]).substr(1);
            if (arg == "h" || arg == "-help" || arg == "?") {
                return false; // Just display help info, then exit.
            }

            if (j + 1 >= argc) {
                log("ERROR: cmdline switch without argument: {}\n", argv[j]);
                return false;
            }

            if (!cfg.set("cmdline", arg, argv[j + 1])) {
				return false;
			}
            j += 2;
        } else {
            log("WARNING: unrecognized cmdline argument: {}\n", argv[j]);
            ++j;
        }
    }

    return true;
}

void adjust_worker_threads(int from, int to) {
    if (from > to) {
        for (int j = from; j > to; --j) {
            log("Waiting for {} worker {} to finish...\n",
				j - to,
				pluralise("thread", j - to));
            hsem_wu.acquire();
        }
        if (!to) {
            // Process results before idling in case user aborts.
            total_factors += process_wu_results();
            log("No workers left. Idling...\n");
        }
    } else {
        int count = to - from;
        log("Adding {} worker {}.\n", count, pluralise("thread", count));
        hsem_wu.release(count);
    }
}

/// @brief Reads a live configuration file and applies any changes.
/// Also checks if quit has been set and if so shuts down cleanly.
void read_live_config() {
    string live_filename = "dodc_live.cfg";

    check_quit();

    auto old_threads = cfg.workers;
    if (!cfg.read(live_filename, true)) {
        return;
    }

    if (cfg.workers != old_threads) {
        adjust_worker_threads(old_threads, cfg.workers);
    }

    remove(live_filename.c_str());
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
    if (cfg.name == "") {
        log("ERROR: you must specify a name. Use Anonymous if you don't want to be credited.\n");
        ok = false;
    }

    if (cfg.nmin > cfg.nmax) {
        log("ERROR: nmin={} > nmax={} doesn't make sense.\n", cfg.nmin, cfg.nmax);
        ok = false;
    }

    cfg.method = toupper(cfg.method);
    if (!verify_method(cfg.method)) {
        log("ERROR: unrecognized method: {}\n", cfg.method);
        ok = false;
    }

	for (auto& am : get_auto_methods()) {
		if (!verify_method(am.method)) {
			log("ERROR: unrecognized automethod: {}\n", am.method);
			ok = false;
		}
	}

    if (cfg.method == "P-1") {
        cfg.ecm_args += " -pm1";
        if (cfg.curves != 1) {
            log("WARNING: running {} P-1 tests doesn't make sense. Forcing <curves> to 1.\n", cfg.curves);
            cfg.curves = 1;
        }
    } else if (cfg.method == "P+1") {
        cfg.ecm_args += " -pp1";
        if (cfg.curves > 3) {
            log("WARNING: running {} P+1 tests is probably a waste. Try 3 instead.\n", cfg.curves);
        }
    }

    return ok;
}

void found_factor(string foundfactor, bool enhanced, string expr, string inputnumber, string method, string args) {
    ofstream fout(cfg.factor_file, ios::app);
    stringstream factorline;
    if (enhanced) {
        factorline << foundfactor << " | " << expr;
    } else {
        factorline << foundfactor << " | " << inputnumber;
    }

    log("{}\t({} digits)\n", factorline.str(), foundfactor.size());
    fout << factorline.str() << endl;
    if (cfg.auto_submit) {
        dump_factor(factor_t(factorline.str(), method, args));
    }
}

/// @brief Initialises the composites file. Shuffling if "order = random".
/// Processes recommended work settings from server if "recommended_work = yes".
/// @return Number of composites in the file.
int init_composites() {
    int cnt = 0;
    string line;
    if (cfg.recommended_work) {
        ifstream f(cfg.composite_file);
        getline(f, line);
        if (line.substr(0, 1) != "#") {
            log("WARNING: trying to do recommended work, but didn't get any settings from server.\n");
        } else {
            stringstream ss(line.substr(1));
            ss >> ws;
            string fn, val;
            while (getline(ss, fn, '(')) {
                getline(ss, val, ')');
                cfg.set("recommended work", fn, val);
            }
        }
    }

    ifstream f(cfg.composite_file);
    if (cfg.order == "random") {
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
        ofstream fout(cfg.composite_file);
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

/// @brief Gets a new workunit when a thread is available.
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
            log("{} saved in {} for now.\n",
				pluralise("Factor", found),
				cfg.submit_failure_file);
        }
    }

    return found;
}

void process_workunit_thread(void *p) {
    block_sigint();
    workunit_t *pwu = (workunit_t *)p;

    if (pwu->handler(*pwu)) {
		add_wu_result(pwu);
	}

    free_workunit(pwu);
}

void do_workunit(string inputnumber, bool enhanced, string expr) {
    workunit_t *pwu = get_workunit();
    workunit_t &wu = *pwu;
    wu.inputnumber = inputnumber;
    wu.enhanced = enhanced;
    wu.expr = expr;

    string method = cfg.method;
	for (auto& am : get_auto_methods()) {
		if (am.minsize <= inputnumber.size() && am.maxsize >= inputnumber.size()) {
			method = am.method;
			break;
		}
	}

    method = toupper(method);
    string msg;
    if (enhanced) {
        msg = format("Factoring {} c{}", expr, inputnumber.size());
    } else {
        msg = format("Factoring {}", inputnumber);
    }

    string tab = string(8 - (msg.size() % 8), ' ');
    log("[{}] {}{}[{}]    {}",
		wu.threadnumber,
		msg,
		tab,
		method,
		cfg.less_spam ? '\r' : '\n');

    if (method == "MSIEVE_QS") {
        wu.tempfile = "msieve" + tostring(wu.threadnumber);
        wu.method = method;
        wu.handler = do_workunit_msieve;
    } else if (method == "CADO_SNFS" || method == "CADO_GNFS") {
        wu.tempfile = "dodc_cado_nfs_" + tostring(wu.threadnumber);
        wu.method = method;
        wu.handler = do_workunit_cado_nfs;
    } else {
        wu.tempfile = cfg.ecm_result_file + tostring(wu.threadnumber);
        wu.cmdline = "echo " + wu.inputnumber + " | " + cfg.ecm_cmd + " -c " + tostring(cfg.curves) + " " + cfg.ecm_args + " " + tostring(cfg.b1) + " > " + wu.tempfile;
        wu.method = method;
        wu.b1 = tostring(cfg.b1);
        wu.handler = do_workunit_gmp_ecm;
    }

    wu.schedule_bg = wu.threadnumber > cfg.pcore_workers;

	if (cfg.auto_reserve > 0
		&& pwu->inputnumber.size() >= cfg.auto_reserve
		&& (pwu->method.contains("NFS")
			|| pwu->method.contains("QS"))) {
		reserve_number(pwu->expr);
	}

    // _beginthread( process_workunit_thread, 0, pwu );
    thread t(process_workunit_thread, pwu);
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

#if defined(__APPLE__)
    // setpriority( PRIO_DARWIN_PROCESS, 0, PRIO_DARWIN_BG ); // Only schedules on efficiency cores.
    // pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif
#endif
}

void check_quit() {
    if (quit.test()) {
        log("Caught SIGINT. Finishing current work units before exiting...\n");
        cleanup_and_exit();
    }
}

void cleanup_and_exit() {
    adjust_worker_threads(cfg.workers, 0);

    total_factors += process_wu_results();
    log("#factors found: {}\n", total_factors);

    // Do one last valiant attempt to submit any unsubmitted factors.
    process_unsubmitted_factors(true);

    while (running_helper_threads > 0) {
		int n = running_helper_threads;
		log("Waiting for {} helper {} to finish...\n",
			n,
			pluralise("thread", n));
        this_thread::sleep_for(chrono::seconds(5));
    }

    remove(cfg.wget_result_file.c_str());
    remove(cfg.ecm_result_file.c_str());

    log("All done! Exiting.\n");
    exit(0);
}

void signal_handler(int signum) {
    if (signum == SIGINT) {
        quit.test_and_set();
    }
}

int main(int argc, char **argv) {
    log("dodc {} by Mikael Klasson\n", version);
    log("usage: dodc [<settings>]\n");
    log("  Make sure you're using the right username.\n");
    log("  Look in dodc.cfg for available options.\n");
    signal(SIGINT, signal_handler);
    srand((uint)time(0));

    // No config file is fine. A bad one is not.
    if (filesystem::exists(cfg.filename) && !cfg.read()) {
		return 1;
	}

    if (!parse_cmdline(argc, argv)) {
        return 1;
    }

    if (!verify_args()) {
        log("Fix your settings and try again. Exiting.\n");
        return 1;
    }

    log("Using factorization method {}\n", cfg.method);
    set_priority();
    adjust_worker_threads(0, cfg.workers);

    do {
        process_unsubmitted_factors(true);

        if (cfg.auto_download) {
            download_composites();
        }

        int ccnt = init_composites();
        log("Found {} composites in {}.\n", ccnt, cfg.composite_file);
        if (!ccnt) {
            // Composite file is empty.
            if (cfg.fallback && !cfg.recommended_work) {
                log("Switching to fallback mode.\n");
                cfg.recommended_work = true;
                cfg.auto_download = true;
                continue;
            } else {
                log("Work complete. Exiting.\n");
                break;
            }
        }

        ifstream fin(cfg.composite_file);
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
            total_factors += process_wu_results();
            process_unsubmitted_factors(false);
        }

        log("#factors found: {}\n", total_factors);
        if (cfg.report_work) {
            string url = cfg.report_url
				+ "?method=" + urlencode(cfg.method)
				+ "&numbers=" + urlencode(cfg.numbers)
				+ "&nmin=" + tostring(cfg.nmin)
				+ "&nmax=" + tostring(cfg.nmax)
				+ "&b1=" + tostring(cfg.b1)
				+ "&curves=" + tostring(cfg.curves);
            string cmd = cfg.wget_cmd
				+ " -T " + to_string(cfg.internet_timeout * 60)
				+ " -q --cache=off"
				+ " --output-document=\"" + cfg.wget_result_file + "_report\""
				+ " \"" + url + "\"";
            thread t(report_work_thread, cmd);
            t.detach();
        }

        cfg.b1 += cfg.b1_increase;
        log("Increasing B1 to {}\n", cfg.b1);
    } while (cfg.loop);

    cleanup_and_exit();
}
