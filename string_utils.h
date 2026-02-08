#if !defined(__string_utils_h_included)
#define __string_utils_h_included

#include <string>
#include <string_view>
using namespace std;

string trim(string_view s);
string toupper(const string &in);
string tostring(long long n);
string toyesno(bool b);
int toint(string s);
uint_fast64_t touint64(string s);
bool isnumber(string_view s);
char tohex(int n);
string urlencode(string_view in);
string scientify(string n);
string pluralise(const string &singular, int count);

#endif
