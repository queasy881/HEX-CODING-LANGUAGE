#pragma once
// HEX Language — Error system
// Modeled after Python's exception hierarchy with file/line/col context
// Pretty-prints errors with source line highlighting

#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <iostream>

// ═══════════════════════════════════════════════════════════════════════════════
//  ERROR TYPES — mirrors Python's hierarchy
// ═══════════════════════════════════════════════════════════════════════════════

enum class ErrorType {
    SYNTAX_ERROR,        // bad syntax, unexpected token, unterminated string
    NAME_ERROR,          // undefined variable
    TYPE_ERROR,          // wrong type for operation (e.g. "hello" - 5)
    VALUE_ERROR,         // right type but wrong value (e.g. #int "abc")
    INDEX_ERROR,         // list/string index out of range
    KEY_ERROR,           // map key not found
    ZERO_DIVISION_ERROR, // division by zero
    FILE_ERROR,          // file not found, can't read/write
    RUNTIME_ERROR,       // generic runtime error
    ARGUMENT_ERROR,      // wrong number of arguments to function
    OVERFLOW_ERROR,      // recursion too deep, number too large
    IMPORT_ERROR,        // failed to import/include file
};

// ═══════════════════════════════════════════════════════════════════════════════
//  HEX ERROR — the main exception class
// ═══════════════════════════════════════════════════════════════════════════════

class HexError : public std::exception {
public:
    ErrorType   type;
    std::string message;
    std::string file;
    int         line;
    int         col;
    std::string source_line;   // the actual line of code that caused it
    std::string hint;          // optional "did you mean?" hint

    HexError(ErrorType type, const std::string& msg, const std::string& file = "",
             int line = 0, int col = 0)
        : type(type), message(msg), file(file), line(line), col(col) {}

    const char* what() const noexcept override {
        return message.c_str();
    }

    // Get the error type name as string (like Python's "SyntaxError", "TypeError")
    std::string type_name() const {
        switch (type) {
            case ErrorType::SYNTAX_ERROR:        return "SyntaxError";
            case ErrorType::NAME_ERROR:          return "NameError";
            case ErrorType::TYPE_ERROR:          return "TypeError";
            case ErrorType::VALUE_ERROR:         return "ValueError";
            case ErrorType::INDEX_ERROR:         return "IndexError";
            case ErrorType::KEY_ERROR:           return "KeyError";
            case ErrorType::ZERO_DIVISION_ERROR: return "ZeroDivisionError";
            case ErrorType::FILE_ERROR:          return "FileError";
            case ErrorType::RUNTIME_ERROR:       return "RuntimeError";
            case ErrorType::ARGUMENT_ERROR:      return "ArgumentError";
            case ErrorType::OVERFLOW_ERROR:      return "OverflowError";
            case ErrorType::IMPORT_ERROR:        return "ImportError";
        }
        return "Error";
    }

    // Set the source line for context display
    HexError& with_source(const std::string& src) {
        source_line = src;
        return *this;
    }

    // Set a hint
    HexError& with_hint(const std::string& h) {
        hint = h;
        return *this;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
//  ERROR FORMATTER — pretty-prints errors like Python
// ═══════════════════════════════════════════════════════════════════════════════
//
//  Output looks like:
//
//    File "test.hex", line 12
//      $x -> 10 / 0
//                 ^
//    ZeroDivisionError: division by zero
//
// ═══════════════════════════════════════════════════════════════════════════════

class ErrorFormatter {
public:
    // Extract a specific line from source code (1-indexed)
    static std::string get_source_line(const std::string& source, int line_num) {
        if (line_num <= 0 || source.empty()) return "";

        int current = 1;
        size_t start = 0;

        for (size_t i = 0; i < source.size(); i++) {
            if (current == line_num) {
                start = i;
                size_t end = source.find('\n', i);
                if (end == std::string::npos) end = source.size();
                std::string result = source.substr(start, end - start);
                // Trim trailing \r
                while (!result.empty() && result.back() == '\r') result.pop_back();
                return result;
            }
            if (source[i] == '\n') current++;
        }
        return "";
    }

    // Format a HexError into a pretty string
    static std::string format(const HexError& err, const std::string& source = "") {
        std::ostringstream ss;

        ss << "\n";

        // Traceback header
        ss << "  Traceback:\n";

        // File + line
        if (!err.file.empty() || err.line > 0) {
            ss << "    File";
            if (!err.file.empty()) ss << " \"" << err.file << "\"";
            if (err.line > 0) ss << ", line " << err.line;
            ss << "\n";
        }

        // Source line + caret
        std::string src = err.source_line;
        if (src.empty() && !source.empty() && err.line > 0) {
            src = get_source_line(source, err.line);
        }

        if (!src.empty()) {
            // Trim leading whitespace for display but track indent
            size_t indent = 0;
            while (indent < src.size() && (src[indent] == ' ' || src[indent] == '\t'))
                indent++;
            std::string trimmed = src.substr(indent);

            ss << "      " << trimmed << "\n";

            // Caret pointer
            if (err.col > 0) {
                int caret_pos = err.col - 1 - (int)indent;
                if (caret_pos < 0) caret_pos = 0;
                if (caret_pos > (int)trimmed.size()) caret_pos = (int)trimmed.size();
                ss << "      " << std::string(caret_pos, ' ') << "^" << "\n";
            }
        }

        // Error type + message
        ss << "  " << err.type_name() << ": " << err.message << "\n";

        // Hint
        if (!err.hint.empty()) {
            ss << "  Hint: " << err.hint << "\n";
        }

        return ss.str();
    }

    // Print error to stderr
    static void print(const HexError& err, const std::string& source = "") {
        std::cerr << format(err, source);
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
//  ERROR BUILDER — convenient factory functions
// ═══════════════════════════════════════════════════════════════════════════════

namespace hex_error {

inline HexError syntax(const std::string& msg, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::SYNTAX_ERROR, msg, file, line, col);
}

inline HexError name(const std::string& var_name, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::NAME_ERROR, "name '$" + var_name + "' is not defined", file, line, col);
}

inline HexError type(const std::string& msg, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::TYPE_ERROR, msg, file, line, col);
}

inline HexError value(const std::string& msg, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::VALUE_ERROR, msg, file, line, col);
}

inline HexError index(int idx, int size, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::INDEX_ERROR,
        "index " + std::to_string(idx) + " out of range (size " + std::to_string(size) + ")",
        file, line, col);
}

inline HexError key(const std::string& key_name, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::KEY_ERROR, "key '" + key_name + "' not found", file, line, col);
}

inline HexError zero_division(const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::ZERO_DIVISION_ERROR, "division by zero", file, line, col);
}

inline HexError file_error(const std::string& path, const std::string& op, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::FILE_ERROR, "cannot " + op + " '" + path + "'", file, line, col);
}

inline HexError runtime(const std::string& msg, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::RUNTIME_ERROR, msg, file, line, col);
}

inline HexError argument(const std::string& func, int expected, int got, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::ARGUMENT_ERROR,
        func + "() takes " + std::to_string(expected) + " arguments, got " + std::to_string(got),
        file, line, col);
}

inline HexError overflow(const std::string& msg, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::OVERFLOW_ERROR, msg, file, line, col);
}

inline HexError import_err(const std::string& path, const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::IMPORT_ERROR, "cannot import '" + path + "'", file, line, col);
}

// Type name helper for error messages
inline std::string type_of(int vtype) {
    const char* names[] = {"number", "string", "bool", "list", "map", "function", "null"};
    if (vtype >= 0 && vtype <= 6) return names[vtype];
    return "unknown";
}

inline HexError type_mismatch(const std::string& op, int got_type, const std::string& expected,
                               const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::TYPE_ERROR,
        "unsupported type for '" + op + "': got " + type_of(got_type) + ", expected " + expected,
        file, line, col);
}

inline HexError type_binary(const std::string& op, int left_type, int right_type,
                             const std::string& file = "", int line = 0, int col = 0) {
    return HexError(ErrorType::TYPE_ERROR,
        "unsupported operand types for '" + op + "': " + type_of(left_type) + " and " + type_of(right_type),
        file, line, col);
}

} // namespace hex_error
