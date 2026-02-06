#if !defined(__string_utils_h_included)
#define __string_utils_h_included

#include <string>
using namespace std;

string trim(const string &s);
string toupper(const string &in);
string tostring(long long n);
string toyesno(bool b);
int toint(string s);
uint_fast64_t touint64(string s);
bool isnumber(string s);
char tohex(int n);
string urlencode(string in);
string scientify(string n);
string pluralise(string singular, int count);

#endif
