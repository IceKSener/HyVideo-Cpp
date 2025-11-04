#include "Task.hpp"

using namespace std;

Task::Task(Task &&t){
    type = t.type;
    inputs = std::move(t.inputs);
    args = std::move(t.args);
}

InputVideo& Task::addInput(string path){
    return inputs.emplace_back(path);
}

string Task::getStr(TaskArgs args, const string key, const string& def){
    return args[key].value_or(def);
}
int Task::getInt(TaskArgs args, const string key, int def){
    if(args[key].has_value()) return atoi(args[key].value().c_str());
    return def;
}
double Task::getReal(TaskArgs args, const string key, double def){
    if(args[key].has_value()) return atof(args[key].value().c_str());
    return def;
}

bool Task::Run(){
    if(type==CALC_SCORES){
        return _taskCalcScores();
    }else if(type==TRANSCODE){
        return _taskTranscode();
    }
    return true;
}