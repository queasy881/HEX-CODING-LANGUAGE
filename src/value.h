#pragma once
// HEX Language — Value type system + control flow signals
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <cstdint>

struct Value;
using ValuePtr = std::shared_ptr<Value>;
using ValueList = std::vector<ValuePtr>;
using ValueMap = std::map<std::string, ValuePtr>;

struct Value {
    enum Type { NUM, STR, BOOL, LIST, MAP, FUNC, TNULL };
    Type type;

    double      num;
    std::string str;
    bool        boolean;
    ValueList   list;
    ValueMap    map_val;

    // Function data
    std::string                func_name;
    std::vector<std::string>   params;
    int                        body_start;
    int                        body_end;

    Value() : type(TNULL), num(0), boolean(false) {}

    static ValuePtr make_num(double n) {
        auto v = std::make_shared<Value>();
        v->type = NUM; v->num = n;
        return v;
    }
    static ValuePtr make_str(const std::string& s) {
        auto v = std::make_shared<Value>();
        v->type = STR; v->str = s;
        return v;
    }
    static ValuePtr make_bool(bool b) {
        auto v = std::make_shared<Value>();
        v->type = BOOL; v->boolean = b;
        return v;
    }
    static ValuePtr make_list(const ValueList& l = {}) {
        auto v = std::make_shared<Value>();
        v->type = LIST; v->list = l;
        return v;
    }
    static ValuePtr make_map(const ValueMap& m = {}) {
        auto v = std::make_shared<Value>();
        v->type = MAP; v->map_val = m;
        return v;
    }
    static ValuePtr make_null() {
        return std::make_shared<Value>();
    }
    static ValuePtr make_func(const std::string& name, const std::vector<std::string>& params,
                               int start, int end) {
        auto v = std::make_shared<Value>();
        v->type = FUNC;
        v->func_name = name;
        v->params = params;
        v->body_start = start;
        v->body_end = end;
        return v;
    }

    bool truthy() const {
        switch (type) {
            case NUM:  return num != 0;
            case STR:  return !str.empty();
            case BOOL: return boolean;
            case LIST: return !list.empty();
            case MAP:  return !map_val.empty();
            case TNULL: return false;
            default: return false;
        }
    }

    double as_num() const {
        if (type == NUM) return num;
        if (type == STR) { try { return std::stod(str); } catch (...) { return 0; } }
        if (type == BOOL) return boolean ? 1 : 0;
        return 0;
    }

    std::string as_str() const {
        switch (type) {
            case NUM: {
                if (num == (int64_t)num) return std::to_string((int64_t)num);
                std::ostringstream ss;
                ss << num;
                return ss.str();
            }
            case STR:  return str;
            case BOOL: return boolean ? "true" : "false";
            case LIST: {
                std::string s = "[";
                for (size_t i = 0; i < list.size(); i++) {
                    if (i > 0) s += ", ";
                    if (list[i]->type == STR) s += "\"" + list[i]->as_str() + "\"";
                    else s += list[i]->as_str();
                }
                return s + "]";
            }
            case MAP: {
                std::string s = "{";
                bool first = true;
                for (auto& [k, v] : map_val) {
                    if (!first) s += ", ";
                    s += "\"" + k + "\": ";
                    if (v->type == STR) s += "\"" + v->as_str() + "\"";
                    else s += v->as_str();
                    first = false;
                }
                return s + "}";
            }
            case FUNC: return "<func:" + func_name + ">";
            case TNULL: return "null";
        }
        return "???";
    }
};

// Control flow signals
struct BreakSignal {};
struct ContinueSignal {};
struct ReturnSignal { ValuePtr value; };
