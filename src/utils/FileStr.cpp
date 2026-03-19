#include "utils/FileStr.hpp"

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
std::string getTimeStr(int64_t pts, AVRational timebase) {
    if (timebase.den == 0) return "<None>";
    double time = pts * timebase.num / (double) timebase.den;
    char buf[50];
    if (time < 0) {
        buf[0] = '-';
        time = -time;
    }
    int hour, min;
    hour=time/3600; time-=hour*3600;
    min=time/60; time-=min*60;
    sprintf(buf, "%02d:%02d:%06.3lf", hour, min, time);
    return string(buf);
}