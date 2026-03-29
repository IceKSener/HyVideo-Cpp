#include "utils/FileStr.hpp"

#ifdef WIN32
#include <windows.h>
#endif // WIN32

using namespace std;
using namespace filesystem;

string tolower(string str) {
    for(int i=0 ; i<str.length() ; ++i) str[i]=tolower(str[i]);
    return str;
}

string replacechar(string str, char from, char to) {
    for(int i=0 ; i<str.length() ; ++i) if(str[i]==from) str[i]=to;
    return str;
}

vector<string> strsplit(string str, const string& c) {
    vector<string> results;
    if(str.empty()) return results;
    int index;
    while((index=str.find(c))>=0){
        results.push_back(str.substr(0,index));
        str=str.substr(index+c.length());
    }
    results.push_back(str);
    return results;
}

string withsuffix(const string& file, const string& suffix) {
    int index=replacechar(file,'\\','/').rfind('/')+1;
    string path=file.substr(0,index), filename=file.substr(index);
    if((index=filename.rfind('.'))>=0) filename=filename.substr(0,index+1);
    return path.append(filename).append(suffix);
}

bool findConf(path& out, const string &name, const path& dir) {
    if (!is_directory(dir)) return false;
    if (is_regular_file(dir/name)) {
        out = dir/name;
        return true;
    } else if (is_regular_file(dir/(name+".json"))) {
        out = dir/(name+".json");
        return true;
    }
    for (const auto& entry: recursive_directory_iterator(dir)) {
        if (!entry.is_directory()) continue;
        const auto& dir = entry.path();
        if (is_regular_file(dir/name)) {
            out = dir/name;
            return true;
        } else if (is_regular_file(dir/(name+".json"))) {
            out = dir/(name+".json");
            return true;
        }
    }
    return false;
}

string getTimeStr(double time, bool simple) {
    char buf[50];
    if (time < 0) {
        buf[0] = '-';
        time = -time;
    }
    int hour, min;
    hour=(int)time/3600; time-=hour*3600;
    min=(int)time/60; time-=min*60;
    if (simple && hour==0) {
        if (min == 0) sprintf(buf, "%06.3lf", time);
        else sprintf(buf, "%02d:%06.3lf", min, time);
    } else {
        sprintf(buf, "%02d:%02d:%06.3lf", hour, min, time);
    }
    return string(buf);
}

string getTimeStr(int64_t pts, AVRational timebase, bool simple) {
    if (timebase.den == 0) return "<None>";
    double time = pts * timebase.num / (double) timebase.den;
    return getTimeStr(time, simple);
}

string LocaltoUTF8(const string& str) {
    // local to unicode
    int wide_size = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
    if (wide_size <= 0) return string();
    vector<wchar_t> v_wide_buf(wide_size);
    wchar_t *wide_buf = v_wide_buf.data();
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, wide_buf, wide_size);

    // unicode to utf8
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide_buf, -1, NULL, 0, NULL, NULL);
    if (utf8_size <= 0) return string();
    vector<char> v_utf8_buf(utf8_size);
    char *utf8_buf = v_utf8_buf.data();
    WideCharToMultiByte(CP_UTF8, 0, wide_buf, -1, utf8_buf, utf8_size, NULL, NULL);
    return utf8_buf;
}

string UTF8toLocal(const string& str) {
    // utf8 to unicode
    int wide_size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (wide_size <= 0) return string();
    vector<wchar_t> v_wide_buf(wide_size);
    wchar_t *wide_buf = v_wide_buf.data();
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wide_buf, wide_size);

    // unicode to local
    int local_size = WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, NULL, 0, NULL, NULL);
    if (local_size <= 0) return string();
    vector<char> v_local_buf(local_size);
    char *local_buf = v_local_buf.data();
    WideCharToMultiByte(CP_ACP, 0, wide_buf, -1, local_buf, local_size, NULL, NULL);
    return local_buf;
}