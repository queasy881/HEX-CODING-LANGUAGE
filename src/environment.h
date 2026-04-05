#pragma once
// HEX Language — Variable scope / environment
#include "value.h"
#include <string>
#include <unordered_map>
#include <memory>

class Environment {
    std::unordered_map<std::string, ValuePtr> vars_;
    std::shared_ptr<Environment> parent_;

public:
    Environment() : parent_(nullptr) {}
    Environment(std::shared_ptr<Environment> parent) : parent_(parent) {}

    ValuePtr get(const std::string& name) {
        auto it = vars_.find(name);
        if (it != vars_.end()) return it->second;
        if (parent_) return parent_->get(name);
        return nullptr;
    }

    void set(const std::string& name, ValuePtr val) {
        Environment* env = this;
        while (env) {
            auto it = env->vars_.find(name);
            if (it != env->vars_.end()) {
                it->second = val;
                return;
            }
            env = env->parent_.get();
        }
        vars_[name] = val;
    }

    void set_local(const std::string& name, ValuePtr val) {
        vars_[name] = val;
    }
};
