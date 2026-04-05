// ============================================================================
//  HEX Programming Language
//  A symbol-heavy, hacker-aesthetic interpreted language
//
//  Usage:
//    hex.exe script.hex        Run a script
//    hex.exe                   Interactive REPL
//
//  Compile:
//    g++ -std=c++17 -O2 -static -o hex.exe hex.cpp
// ============================================================================

#include "interpreter.h"
#include <iostream>
#include <fstream>
#include <string>

int main(int argc, char* argv[]) {
    Interpreter hex;
    hex.set_argv(argc, argv);

    if (argc < 2) {
        hex.repl();
    } else {
        std::string path = argv[1];
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "[hex] cannot open: " << path << std::endl;
            return 1;
        }
        std::string source((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        f.close();
        hex.run(source, path);
    }

    return 0;
}
