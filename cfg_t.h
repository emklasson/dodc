#if !defined(__cfg_t_h_included)
#define __cfg_t_h_included

#include "cfg_base_t.h"
using namespace std;

class cfg_t : public cfg_base_t {
public:
    string name;
    long long workers;
    bool recommended_work;
    string ecm_args;
    bool less_spam;

	string method;
	string auto_method;
	long long b1;
	long long b1_increase;
	long long curves;
	long long nmin;
	long long nmax;
	bool loop;
	string numbers;
	bool fallback;
	string sort;
	string order;

	bool auto_submit;
	long long submit_retry_interval;
	long long submit_interval;
	long long internet_timeout;
	bool auto_download;
	bool exclude_reservations;
	long long auto_reserve;
	bool report_work;

	long long pcore_workers;

	string wget_cmd;
	string ecm_cmd;
	string gzip_cmd;
	bool use_gzip;

	string composite_url;
	string submit_url;
	string manual_submit_url;
	string report_url;
	string reserve_url;

	string composite_file;
	string factor_file;
	string submit_failure_file;
	string wget_result_file;
	string ecm_result_file;

    cfg_t() : cfg_base_t("dodc.cfg") {
        // Add configuration options with default values.
        add("name", name);
        add("workers", workers, 4);
        add("recommended_work", recommended_work, false);
        add("ecm_args", ecm_args);
        add("less_spam", less_spam, true);

        add("method", method, "ECM");
        add("auto_method", auto_method);
        add("b1", b1, 1000000);
        add("b1_increase", b1_increase, 1000);
        add("curves", curves, 1);
        add("nmin", nmin, 1);
        add("nmax", nmax, 1000);
        add("loop", loop, true);
        add("numbers", numbers, "3-,3+,5-,5+,7-,7+,9-,9+,11-,11+,13-,13+,15-,15+,17-,17+");
        add("fallback", fallback, true);
        add("sort", sort, "n");
        add("order", order, "random");

        add("auto_submit", auto_submit, true);
        add("submit_retry_interval", submit_retry_interval, 60);
        add("submit_interval", submit_interval, 1);
        add("internet_timeout", internet_timeout, 2);
        add("auto_download", auto_download, true);
        add("exclude_reservations", exclude_reservations, true);
        add("auto_reserve", auto_reserve, 90);
        add("report_work", report_work, true);

        add("pcore_workers", pcore_workers, 4);

        add("wget_cmd", wget_cmd, "wget");
        add("ecm_cmd", ecm_cmd, "ecm");
        add("gzip_cmd", gzip_cmd, "gzip");
        add("use_gzip", use_gzip, true);

        add("composite_url", composite_url, "http://mklasson.com/factors/listcomposites.php");
        add("submit_url", submit_url, "http://mklasson.com/factors/process.php");
        add("manual_submit_url", manual_submit_url, "http://mklasson.com/factors/submit.php");
        add("report_url", report_url, "http://mklasson.com/factors/report_work.php");
        add("reserve_url", reserve_url, "http://mklasson.com/factors/reserve_result.php");

        add("composite_file", composite_file, "dodc_composites");
        add("factor_file", factor_file, "dodc_factors.txt");
        add("submit_failure_file", submit_failure_file, "dodc_unsubmitted_factors.txt");
        add("wget_result_file", wget_result_file, "dodc_wget_result");
        add("ecm_result_file", ecm_result_file, "dodc_ecm_result");
    }
};

#endif
