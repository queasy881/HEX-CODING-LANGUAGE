#pragma once
// HEX Language — Interpreter (parser + executor)
#include "token.h"
#include "value.h"
#include "environment.h"
#include "lexer.h"
#include "error.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <limits>
#include <random>
#include <future>
#include <mutex>
#include <regex>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>

class Interpreter {
    std::vector<Token> tokens_;
    size_t pos_;
    std::shared_ptr<Environment> env_;
    std::shared_ptr<Environment> global_env_;
    std::string source_;
    std::string filename_;

    // Async task storage
    std::map<std::string, std::shared_future<ValuePtr>> async_tasks_;
    std::mutex async_mutex_;

    // ── HTTP helper ─────────────────────────────────────────────────────
    static std::string http_request(const std::string& url, const std::string& method = "GET",
                                     const std::string& body = "", const std::string& content_type = "") {
        HINTERNET hNet = InternetOpenA("HEX/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (!hNet) return "";

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
        if (url.substr(0, 5) == "https") flags |= INTERNET_FLAG_SECURE;

        HINTERNET hUrl = nullptr;
        if (method == "GET") {
            hUrl = InternetOpenUrlA(hNet, url.c_str(), NULL, 0, flags, 0);
        } else {
            // Parse URL for host/path
            URL_COMPONENTSA uc = {};
            char host[256] = {}, path[2048] = {};
            uc.dwStructSize = sizeof(uc);
            uc.lpszHostName = host; uc.dwHostNameLength = sizeof(host);
            uc.lpszUrlPath = path; uc.dwUrlPathLength = sizeof(path);
            InternetCrackUrlA(url.c_str(), 0, 0, &uc);

            HINTERNET hConn = InternetConnectA(hNet, host, uc.nPort, NULL, NULL,
                                                INTERNET_SERVICE_HTTP, 0, 0);
            if (!hConn) { InternetCloseHandle(hNet); return ""; }

            DWORD req_flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0;
            HINTERNET hReq = HttpOpenRequestA(hConn, method.c_str(), path, NULL, NULL, NULL,
                                               req_flags | INTERNET_FLAG_RELOAD, 0);
            if (!hReq) { InternetCloseHandle(hConn); InternetCloseHandle(hNet); return ""; }

            std::string headers;
            if (!content_type.empty()) headers = "Content-Type: " + content_type + "\r\n";

            HttpSendRequestA(hReq, headers.empty() ? NULL : headers.c_str(),
                             headers.empty() ? 0 : (DWORD)headers.size(),
                             body.empty() ? NULL : (LPVOID)body.c_str(),
                             (DWORD)body.size());
            hUrl = hReq;
            // Note: hConn leaks here but it's fine for a scripting language
        }

        if (!hUrl) { InternetCloseHandle(hNet); return ""; }

        std::string result;
        char buf[4096];
        DWORD bytesRead;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead) && bytesRead > 0)
            result.append(buf, bytesRead);

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hNet);
        return result;
    }

    // ── Base64 helpers ──────────────────────────────────────────────────
    static std::string base64_encode(const std::string& input) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        int val = 0, bits = -6;
        for (uint8_t c : input) {
            val = (val << 8) + c;
            bits += 8;
            while (bits >= 0) {
                out += chars[(val >> bits) & 0x3F];
                bits -= 6;
            }
        }
        if (bits > -6) out += chars[((val << 8) >> (bits + 8)) & 0x3F];
        while (out.size() % 4) out += '=';
        return out;
    }

    static std::string base64_decode(const std::string& input) {
        static const int T[256] = {
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
            52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
            -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
            15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
            -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
            41,42,43,44,45,46,47,48,49,50,51
        };
        std::string out;
        int val = 0, bits = -8;
        for (uint8_t c : input) {
            if (c == '=' || c == '\n' || c == '\r') continue;
            if (T[c] == -1) continue;
            val = (val << 6) + T[c];
            bits += 6;
            if (bits >= 0) {
                out += (char)((val >> bits) & 0xFF);
                bits -= 8;
            }
        }
        return out;
    }

    // ── HEX Cipher — fully custom encoding + encryption ───────────────
    //
    // Nothing from base64 or any standard encoding.
    // Uses a custom alphabet and encoding scheme unique to HEX.
    //
    // HEX Encoding: each byte maps to 2 chars from a shuffled 16-char alphabet
    //   Alphabet: "hH3xX9eE1kK5rR7a" (HEX-themed, 16 chars)
    //   Byte 0xAB → alphabet[A] + alphabet[B]
    //   Prefix: ~HX{ ... }~  (the HEX signature)
    //
    // Encryption: XOR stream cipher with xorshift64 PRNG seeded from password + salt
    // Salt: 16 random bytes prepended to ciphertext
    // Format: ~HX{ encoded(salt + xor_encrypted_data) }~

    static const char* hex_alphabet() {
        return "hH3xX9eE1kK5rR7a";  // 16 chars, a is padding marker
    }

    // Encode raw bytes to HEX custom encoding
    static std::string hexcode_encode(const std::vector<uint8_t>& data) {
        const char* alpha = hex_alphabet();
        std::string out;
        out.reserve(data.size() * 2);
        for (uint8_t b : data) {
            out += alpha[(b >> 4) & 0x0F];
            out += alpha[b & 0x0F];
        }
        return out;
    }

    // Decode HEX custom encoding back to raw bytes
    static std::vector<uint8_t> hexcode_decode(const std::string& encoded) {
        const char* alpha = hex_alphabet();
        // Build reverse lookup
        int rev[256];
        for (int i = 0; i < 256; i++) rev[i] = -1;
        for (int i = 0; i < 16; i++) rev[(uint8_t)alpha[i]] = i;

        std::vector<uint8_t> out;
        out.reserve(encoded.size() / 2);
        for (size_t i = 0; i + 1 < encoded.size(); i += 2) {
            int hi = rev[(uint8_t)encoded[i]];
            int lo = rev[(uint8_t)encoded[i + 1]];
            if (hi < 0 || lo < 0) continue;
            out.push_back((uint8_t)((hi << 4) | lo));
        }
        return out;
    }

    static std::vector<uint8_t> derive_key_stream(const std::string& password,
                                                    const std::vector<uint8_t>& salt,
                                                    size_t length) {
        std::vector<uint8_t> key_stream;
        key_stream.reserve(length);

        // Seed from password + salt using FNV-like mixing
        uint64_t state = 0x48455843495048ULL; // "HEXCIPH"
        for (char c : password) state = (state ^ c) * 0x100000001B3ULL;
        for (uint8_t s : salt) state = (state ^ s) * 0x100000001B3ULL;

        // Secondary state for better mixing
        uint64_t state2 = state ^ 0xDEADBEEFCAFE;

        // Generate stream using double xorshift
        while (key_stream.size() < length) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            state2 ^= state2 << 5;
            state2 ^= state2 >> 3;
            state2 ^= state2 << 11;
            key_stream.push_back((uint8_t)((state ^ state2) & 0xFF));
        }
        return key_stream;
    }

    static std::string hex_encrypt(const std::string& plaintext, const std::string& password) {
        // 16-byte random salt
        std::vector<uint8_t> salt(16);
        std::random_device rd;
        for (auto& b : salt) b = (uint8_t)(rd() & 0xFF);

        // Derive key stream
        auto key = derive_key_stream(password, salt, plaintext.size());

        // XOR encrypt
        std::vector<uint8_t> encrypted(plaintext.size());
        for (size_t i = 0; i < plaintext.size(); i++)
            encrypted[i] = (uint8_t)plaintext[i] ^ key[i];

        // Combine salt + encrypted
        std::vector<uint8_t> combined;
        combined.insert(combined.end(), salt.begin(), salt.end());
        combined.insert(combined.end(), encrypted.begin(), encrypted.end());

        // Encode with HEX custom encoding and wrap in signature
        return "~HX{" + hexcode_encode(combined) + "}~";
    }

    static std::string hex_decrypt(const std::string& ciphertext, const std::string& password) {
        // Check signature
        if (ciphertext.size() < 7) return "";
        if (ciphertext.substr(0, 4) != "~HX{") return "";
        if (ciphertext.back() != '~' || ciphertext[ciphertext.size() - 2] != '}') return "";

        // Extract encoded body
        std::string body = ciphertext.substr(4, ciphertext.size() - 6);

        // Decode with HEX custom encoding
        auto combined = hexcode_decode(body);
        if (combined.size() < 17) return "";

        // Split salt + encrypted
        std::vector<uint8_t> salt(combined.begin(), combined.begin() + 16);
        std::vector<uint8_t> encrypted(combined.begin() + 16, combined.end());

        // Derive same key stream
        auto key = derive_key_stream(password, salt, encrypted.size());

        // XOR decrypt
        std::string decrypted(encrypted.size(), 0);
        for (size_t i = 0; i < encrypted.size(); i++)
            decrypted[i] = (char)(encrypted[i] ^ key[i]);

        return decrypted;
    }

    // ── JSON parser — converts JSON string to HEX Value ─────────────────
    static ValuePtr json_parse(const std::string& json, size_t& pos) {
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
               json[pos] == '\n' || json[pos] == '\r')) pos++;
        if (pos >= json.size()) return Value::make_null();

        char c = json[pos];

        // String
        if (c == '"') {
            pos++; // skip opening "
            std::string str;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                    switch (json[pos]) {
                        case 'n': str += '\n'; break;
                        case 't': str += '\t'; break;
                        case '"': str += '"'; break;
                        case '\\': str += '\\'; break;
                        case '/': str += '/'; break;
                        case 'b': str += '\b'; break;
                        case 'f': str += '\f'; break;
                        case 'r': str += '\r'; break;
                        default: str += json[pos]; break;
                    }
                } else {
                    str += json[pos];
                }
                pos++;
            }
            if (pos < json.size()) pos++; // skip closing "
            return Value::make_str(str);
        }

        // Number
        if (c == '-' || isdigit(c)) {
            std::string num;
            if (c == '-') { num += c; pos++; }
            while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '.' ||
                   json[pos] == 'e' || json[pos] == 'E' || json[pos] == '+' || json[pos] == '-')) {
                if ((json[pos] == '+' || json[pos] == '-') && num.back() != 'e' && num.back() != 'E') break;
                num += json[pos++];
            }
            return Value::make_num(std::stod(num));
        }

        // Boolean / null
        if (json.substr(pos, 4) == "true")  { pos += 4; return Value::make_bool(true); }
        if (json.substr(pos, 5) == "false") { pos += 5; return Value::make_bool(false); }
        if (json.substr(pos, 4) == "null")  { pos += 4; return Value::make_null(); }

        // Array
        if (c == '[') {
            pos++; // skip [
            ValueList items;
            while (pos < json.size()) {
                while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
                       json[pos] == '\r' || json[pos] == '\t')) pos++;
                if (pos < json.size() && json[pos] == ']') { pos++; break; }
                items.push_back(json_parse(json, pos));
                while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
                       json[pos] == '\r' || json[pos] == '\t')) pos++;
                if (pos < json.size() && json[pos] == ',') pos++;
            }
            return Value::make_list(items);
        }

        // Object
        if (c == '{') {
            pos++; // skip {
            ValueMap map;
            while (pos < json.size()) {
                while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
                       json[pos] == '\r' || json[pos] == '\t')) pos++;
                if (pos < json.size() && json[pos] == '}') { pos++; break; }
                // Parse key (must be string)
                auto key = json_parse(json, pos);
                while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':')) pos++;
                auto val = json_parse(json, pos);
                map[key->as_str()] = val;
                while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
                       json[pos] == '\r' || json[pos] == '\t')) pos++;
                if (pos < json.size() && json[pos] == ',') pos++;
            }
            return Value::make_map(map);
        }

        pos++; // skip unknown
        return Value::make_null();
    }

    static ValuePtr json_parse(const std::string& json) {
        size_t pos = 0;
        return json_parse(json, pos);
    }

    // ── JSON serializer — converts HEX Value to JSON string ─────────────
    static std::string json_stringify(ValuePtr val, int indent = 0, int depth = 0) {
        if (!val) return "null";
        std::string pad(depth * indent, ' ');
        std::string pad_inner((depth + 1) * indent, ' ');
        std::string nl = indent > 0 ? "\n" : "";
        std::string sp = indent > 0 ? " " : "";

        switch (val->type) {
            case Value::NUM: {
                if (val->num == (int64_t)val->num)
                    return std::to_string((int64_t)val->num);
                std::ostringstream ss; ss << val->num; return ss.str();
            }
            case Value::STR: {
                std::string escaped;
                for (char c : val->str) {
                    switch (c) {
                        case '"': escaped += "\\\""; break;
                        case '\\': escaped += "\\\\"; break;
                        case '\n': escaped += "\\n"; break;
                        case '\t': escaped += "\\t"; break;
                        case '\r': escaped += "\\r"; break;
                        default: escaped += c; break;
                    }
                }
                return "\"" + escaped + "\"";
            }
            case Value::BOOL: return val->boolean ? "true" : "false";
            case Value::TNULL: return "null";
            case Value::LIST: {
                if (val->list.empty()) return "[]";
                std::string s = "[" + nl;
                for (size_t i = 0; i < val->list.size(); i++) {
                    s += pad_inner + json_stringify(val->list[i], indent, depth + 1);
                    if (i + 1 < val->list.size()) s += ",";
                    s += nl;
                }
                return s + pad + "]";
            }
            case Value::MAP: {
                if (val->map_val.empty()) return "{}";
                std::string s = "{" + nl;
                size_t i = 0;
                for (auto& [k, v] : val->map_val) {
                    s += pad_inner + "\"" + k + "\":" + sp + json_stringify(v, indent, depth + 1);
                    if (++i < val->map_val.size()) s += ",";
                    s += nl;
                }
                return s + pad + "}";
            }
            default: return "null";
        }
    }

    Token& cur() { return tokens_[pos_]; }
    Token& peek(int off = 0) {
        size_t p = pos_ + off;
        return p < tokens_.size() ? tokens_[p] : tokens_.back();
    }
    Token advance() { return tokens_[pos_++]; }
    bool at_end() { return pos_ >= tokens_.size() || cur().type == Tok::EOF_TOK; }

    bool check(Tok t) { return !at_end() && cur().type == t; }
    bool match(Tok t) {
        if (check(t)) { advance(); return true; }
        return false;
    }

    void expect(Tok t, const std::string& msg) {
        if (!check(t)) throw_syntax(msg + " (got '" + cur().value + "')");
        advance();
    }

    void skip_newlines() {
        while (check(Tok::NEWLINE)) advance();
    }

    // ── Error throwers with context ─────────────────────────────────────

    [[noreturn]] void throw_syntax(const std::string& msg) {
        throw hex_error::syntax(msg, filename_, cur().line, cur().col);
    }

    [[noreturn]] void throw_name(const std::string& var) {
        throw hex_error::name(var, filename_, cur().line, cur().col);
    }

    [[noreturn]] void throw_type(const std::string& msg) {
        throw hex_error::type(msg, filename_, cur().line, cur().col);
    }

    [[noreturn]] void throw_value(const std::string& msg) {
        throw hex_error::value(msg, filename_, cur().line, cur().col);
    }

    [[noreturn]] void throw_runtime(const std::string& msg) {
        throw hex_error::runtime(msg, filename_, cur().line, cur().col);
    }

    // Legacy — maps old error() calls to SyntaxError
    [[noreturn]] void error(const std::string& msg) {
        throw hex_error::runtime(msg, filename_, cur().line, cur().col);
    }

    // ── Find matching ;; for a :: block ─────────────────────────────────
    int find_block_end(int start) {
        int depth = 1;
        int i = start;
        while (i < (int)tokens_.size()) {
            if (tokens_[i].type == Tok::DOUBLE_COLON) depth++;
            if (tokens_[i].type == Tok::DOUBLE_SEMI) {
                depth--;
                if (depth == 0) return i;
            }
            i++;
        }
        return (int)tokens_.size() - 1;
    }

    // ── Expression parsing (precedence climbing) ────────────────────────

    ValuePtr parse_expr() {
        return parse_or();
    }

    ValuePtr parse_or() {
        auto left = parse_and();
        while (check(Tok::OR)) {
            advance();
            auto right = parse_and();
            left = Value::make_bool(left->truthy() || right->truthy());
        }
        // Ternary: expr ? true_val : false_val
        if (check(Tok::QUESTION)) {
            advance();
            auto true_val = parse_expr();
            expect(Tok::COLON, "expected ':' in ternary");
            auto false_val = parse_expr();
            return left->truthy() ? true_val : false_val;
        }
        return left;
    }

    ValuePtr parse_and() {
        auto left = parse_not();
        while (check(Tok::AND)) {
            advance();
            auto right = parse_not();
            left = Value::make_bool(left->truthy() && right->truthy());
        }
        return left;
    }

    ValuePtr parse_not() {
        if (check(Tok::NOT)) {
            advance();
            auto val = parse_not();
            return Value::make_bool(!val->truthy());
        }
        return parse_comparison();
    }

    ValuePtr parse_comparison() {
        auto left = parse_addition();
        while (check(Tok::EQ) || check(Tok::NEQ) || check(Tok::LT) ||
               check(Tok::GT) || check(Tok::LTE) || check(Tok::GTE)) {
            auto op = advance();
            auto right = parse_addition();
            bool result = false;
            if (left->type == Value::NUM && right->type == Value::NUM) {
                if (op.type == Tok::EQ)  result = left->num == right->num;
                if (op.type == Tok::NEQ) result = left->num != right->num;
                if (op.type == Tok::LT)  result = left->num < right->num;
                if (op.type == Tok::GT)  result = left->num > right->num;
                if (op.type == Tok::LTE) result = left->num <= right->num;
                if (op.type == Tok::GTE) result = left->num >= right->num;
            } else {
                std::string ls = left->as_str(), rs = right->as_str();
                if (op.type == Tok::EQ)  result = ls == rs;
                if (op.type == Tok::NEQ) result = ls != rs;
                if (op.type == Tok::LT)  result = ls < rs;
                if (op.type == Tok::GT)  result = ls > rs;
                if (op.type == Tok::LTE) result = ls <= rs;
                if (op.type == Tok::GTE) result = ls >= rs;
            }
            left = Value::make_bool(result);
        }
        // "in" operator: $x in $list, "a" in "abc"
        if (check(Tok::KW_IN)) {
            advance();
            auto container = parse_addition();
            bool found = false;
            if (container->type == Value::LIST) {
                for (auto& item : container->list) {
                    if (item->as_str() == left->as_str()) { found = true; break; }
                }
            } else if (container->type == Value::STR) {
                found = container->str.find(left->as_str()) != std::string::npos;
            } else if (container->type == Value::MAP) {
                found = container->map_val.count(left->as_str()) > 0;
            }
            return Value::make_bool(found);
        }
        return left;
    }

    // Bitwise OR (lowest bitwise precedence)
    ValuePtr parse_addition() {
        auto left = parse_bitwise();
        while (check(Tok::PLUS) || check(Tok::MINUS)) {
            auto op = advance();
            auto right = parse_bitwise();
            if (op.type == Tok::PLUS) {
                // list + list = concatenation
                if (left->type == Value::LIST && right->type == Value::LIST) {
                    ValueList combined = left->list;
                    combined.insert(combined.end(), right->list.begin(), right->list.end());
                    left = Value::make_list(combined);
                }
                // string + anything = concat
                else if (left->type == Value::STR || right->type == Value::STR)
                    left = Value::make_str(left->as_str() + right->as_str());
                else
                    left = Value::make_num(left->as_num() + right->as_num());
            } else {
                left = Value::make_num(left->as_num() - right->as_num());
            }
        }
        return left;
    }

    // Bitwise: & | ^ ~ << >>
    ValuePtr parse_bitwise() {
        auto left = parse_multiplication();
        while (check(Tok::AMP) || check(Tok::IDENT)) {
            // & is already AMP token, but && is AND — AMP is single &
            // We use | for bitwise OR but || is logical OR
            // Check for bitwise keywords: band, bor, bxor, bshl, bshr
            if (check(Tok::AMP)) {
                // Single & is AMP — used for builtins in statement context
                // In expression context after a value, treat as bitwise AND
                break; // Don't consume — let statement handler deal with &
            }
            break;
        }
        return left;
    }

    ValuePtr parse_multiplication() {
        auto left = parse_power();
        while (check(Tok::STAR) || check(Tok::SLASH) || check(Tok::PERCENT)) {
            auto op = advance();
            auto right = parse_power();
            if (op.type == Tok::STAR) {
                // string * number = repeat
                if (left->type == Value::STR && right->type == Value::NUM) {
                    std::string result;
                    int n = (int)right->as_num();
                    for (int i = 0; i < n; i++) result += left->str;
                    left = Value::make_str(result);
                }
                // list * number = repeat
                else if (left->type == Value::LIST && right->type == Value::NUM) {
                    ValueList result;
                    int n = (int)right->as_num();
                    for (int i = 0; i < n; i++)
                        result.insert(result.end(), left->list.begin(), left->list.end());
                    left = Value::make_list(result);
                }
                // number * number
                else left = Value::make_num(left->as_num() * right->as_num());
            }
            if (op.type == Tok::SLASH) {
                double d = right->as_num();
                if (d == 0) throw hex_error::zero_division(filename_, cur().line, cur().col);
                // Check for // (integer division) — SLASH followed by SLASH
                left = Value::make_num(left->as_num() / d);
            }
            if (op.type == Tok::PERCENT) left = Value::make_num(fmod(left->as_num(), right->as_num()));
        }
        return left;
    }

    ValuePtr parse_power() {
        auto left = parse_unary();
        if (check(Tok::CARET)) {
            advance();
            auto right = parse_power(); // right-associative
            left = Value::make_num(pow(left->as_num(), right->as_num()));
        }
        return left;
    }

    ValuePtr parse_unary() {
        if (check(Tok::MINUS)) {
            advance();
            auto val = parse_unary();
            return Value::make_num(-val->as_num());
        }
        if (check(Tok::NOT)) {
            advance();
            auto val = parse_unary();
            return Value::make_bool(!val->truthy());
        }
        return parse_postfix();
    }

    ValuePtr parse_postfix() {
        auto val = parse_primary();

        // Handle indexing: $var[idx] or $var[start:end]
        while (check(Tok::LBRACKET)) {
            advance();

            // Check for slice: [start:end]
            auto idx = parse_expr();
            if (check(Tok::COLON)) {
                advance();
                auto end_idx = parse_expr();
                expect(Tok::RBRACKET, "expected ']'");

                if (val->type == Value::STR) {
                    int s = (int)idx->as_num();
                    int e = (int)end_idx->as_num();
                    if (s < 0) s += (int)val->str.size();
                    if (e < 0) e += (int)val->str.size();
                    if (s < 0) s = 0;
                    if (e > (int)val->str.size()) e = (int)val->str.size();
                    val = Value::make_str(val->str.substr(s, e - s));
                } else if (val->type == Value::LIST) {
                    int s = (int)idx->as_num();
                    int e = (int)end_idx->as_num();
                    if (s < 0) s += (int)val->list.size();
                    if (e < 0) e += (int)val->list.size();
                    ValueList sub(val->list.begin() + s, val->list.begin() + e);
                    val = Value::make_list(sub);
                }
            } else {
                expect(Tok::RBRACKET, "expected ']'");

                if (val->type == Value::LIST) {
                    int i = (int)idx->as_num();
                    if (i < 0) i += (int)val->list.size();
                    if (i >= 0 && i < (int)val->list.size())
                        val = val->list[i];
                    else
                        val = Value::make_null();
                } else if (val->type == Value::STR) {
                    int i = (int)idx->as_num();
                    if (i < 0) i += (int)val->str.size();
                    if (i >= 0 && i < (int)val->str.size())
                        val = Value::make_str(std::string(1, val->str[i]));
                    else
                        val = Value::make_null();
                } else if (val->type == Value::MAP) {
                    std::string key = idx->as_str();
                    auto it = val->map_val.find(key);
                    val = (it != val->map_val.end()) ? it->second : Value::make_null();
                }
            }
        }
        return val;
    }

    ValuePtr parse_primary() {
        // Number
        if (check(Tok::NUMBER)) {
            auto t = advance();
            return Value::make_num(std::stod(t.value));
        }

        // String — with {$var} interpolation
        if (check(Tok::STRING)) {
            auto t = advance();
            std::string raw = t.value;
            // Expand {$var} and {expr} patterns
            std::string result;
            for (size_t i = 0; i < raw.size(); i++) {
                if (raw[i] == '{' && i + 1 < raw.size() && raw[i+1] == '$') {
                    // Find closing }
                    size_t end = raw.find('}', i);
                    if (end != std::string::npos) {
                        std::string var_name = raw.substr(i + 2, end - i - 2);
                        auto val = env_->get(var_name);
                        result += val ? val->as_str() : "null";
                        i = end;
                        continue;
                    }
                }
                result += raw[i];
            }
            return Value::make_str(result);
        }

        // Booleans
        if (check(Tok::TRUE_LIT))  { advance(); return Value::make_bool(true); }
        if (check(Tok::FALSE_LIT)) { advance(); return Value::make_bool(false); }
        if (check(Tok::NULL_LIT))  { advance(); return Value::make_null(); }

        // $variable
        if (check(Tok::DOLLAR_IDENT)) {
            auto t = advance();
            auto val = env_->get(t.value);
            return val ? val : Value::make_null();
        }

        // ~read "file" — file read as expression
        if (check(Tok::TILDE)) {
            advance();
            if (check(Tok::IDENT) && cur().value == "read") {
                advance();
                auto path = parse_expr();
                std::ifstream f(path->as_str());
                if (!f.is_open()) return Value::make_null();
                std::string content((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
                return Value::make_str(content);
            }
            if (check(Tok::IDENT) && cur().value == "exists") {
                advance();
                auto path = parse_expr();
                return Value::make_bool(std::filesystem::exists(path->as_str()));
            }
            // ~get "url" — HTTP GET
            if (check(Tok::IDENT) && cur().value == "get") {
                advance();
                auto url = parse_expr();
                return Value::make_str(http_request(url->as_str(), "GET"));
            }
            // ~post "url" "body" — HTTP POST
            if (check(Tok::IDENT) && cur().value == "post") {
                advance();
                auto url = parse_expr();
                auto body = parse_expr();
                return Value::make_str(http_request(url->as_str(), "POST", body->as_str(),
                                       "application/x-www-form-urlencoded"));
            }
            // ~lines "file" — read file as list of lines (expression)
            if (check(Tok::IDENT) && cur().value == "lines") {
                advance();
                auto path = parse_expr();
                std::ifstream f(path->as_str());
                ValueList lines;
                if (f.is_open()) {
                    std::string line;
                    while (std::getline(f, line)) {
                        while (!line.empty() && line.back() == '\r') line.pop_back();
                        lines.push_back(Value::make_str(line));
                    }
                }
                return Value::make_list(lines);
            }
            // ~size "file" — file size as number (expression)
            if (check(Tok::IDENT) && cur().value == "size") {
                advance();
                auto path = parse_expr();
                if (std::filesystem::exists(path->as_str()))
                    return Value::make_num((double)std::filesystem::file_size(path->as_str()));
                return Value::make_num(-1);
            }
            // ~ls "dir" — list directory as list (expression)
            if (check(Tok::IDENT) && cur().value == "ls") {
                advance();
                auto path = parse_expr();
                ValueList items;
                try {
                    for (auto& e : std::filesystem::directory_iterator(path->as_str()))
                        items.push_back(Value::make_str(e.path().filename().string()));
                } catch (...) {}
                return Value::make_list(items);
            }
            // ~ext "file.txt" — get extension (expression)
            if (check(Tok::IDENT) && cur().value == "ext") {
                advance();
                auto path = parse_expr();
                auto ext = std::filesystem::path(path->as_str()).extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                return Value::make_str(ext);
            }
            // ~name "path/file.txt" — get filename (expression)
            if (check(Tok::IDENT) && cur().value == "name") {
                advance();
                auto path = parse_expr();
                return Value::make_str(std::filesystem::path(path->as_str()).filename().string());
            }
            // ~dir "path/file.txt" — get parent dir (expression)
            if (check(Tok::IDENT) && cur().value == "dir") {
                advance();
                auto path = parse_expr();
                return Value::make_str(std::filesystem::path(path->as_str()).parent_path().string());
            }
            // ~cwd — current directory (expression)
            if (check(Tok::IDENT) && cur().value == "cwd") {
                advance();
                return Value::make_str(std::filesystem::current_path().string());
            }
            error("unknown file operation after ~");
        }

        // #builtin — hash builtins as expressions
        if (check(Tok::HASH)) {
            advance();
            // Accept IDENT or keywords that are also builtin names
            std::string bname;
            if (check(Tok::IDENT)) bname = advance().value;
            else if (check(Tok::KW_MATCH)) bname = advance().value;
            else if (check(Tok::KW_DEFAULT)) bname = advance().value;
            else error("expected builtin name after #");
            return exec_hash_builtin(bname);
        }

        // Parenthesized expression
        if (check(Tok::LPAREN)) {
            advance();
            auto val = parse_expr();
            expect(Tok::RPAREN, "expected ')'");
            return val;
        }

        // List literal [1, 2, 3]
        if (check(Tok::LBRACKET)) {
            advance();
            ValueList items;
            skip_newlines();
            while (!check(Tok::RBRACKET) && !at_end()) {
                items.push_back(parse_expr());
                skip_newlines();
                if (!check(Tok::RBRACKET)) { expect(Tok::COMMA, "expected ',' in list"); }
                skip_newlines();
            }
            expect(Tok::RBRACKET, "expected ']'");
            return Value::make_list(items);
        }

        // Map literal {"key": val}
        if (check(Tok::LBRACE)) {
            advance();
            ValueMap m;
            skip_newlines();
            while (!check(Tok::RBRACE) && !at_end()) {
                auto key = parse_expr()->as_str();
                expect(Tok::COLON, "expected ':' in map");
                auto val = parse_expr();
                m[key] = val;
                skip_newlines();
                if (!check(Tok::RBRACE)) match(Tok::COMMA);
                skip_newlines();
            }
            expect(Tok::RBRACE, "expected '}'");
            return Value::make_map(m);
        }

        // Function call: funcname arg1 arg2
        if (check(Tok::IDENT)) {
            auto name = advance().value;
            auto func = env_->get(name);
            if (func && func->type == Value::FUNC) {
                return call_function(func);
            }
            // Just return as string if not found (could be identifier)
            return Value::make_str(name);
        }

        // ! shell as expression
        if (check(Tok::BANG)) {
            advance();
            auto cmd = parse_expr();
            std::string command = cmd->as_str();
            // Capture output
            FILE* pipe = _popen(command.c_str(), "r");
            if (!pipe) return Value::make_null();
            char buffer[256];
            std::string result;
            while (fgets(buffer, sizeof(buffer), pipe))
                result += buffer;
            _pclose(pipe);
            // Trim trailing newline
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            return Value::make_str(result);
        }

        // await funcname args — as expression, returns result
        if (check(Tok::KW_AWAIT)) {
            advance();
            if (!check(Tok::IDENT)) throw_syntax("expected function name after 'await'");
            auto fname = advance().value;
            auto func = env_->get(fname);
            if (!func || func->type != Value::FUNC) throw_name(fname);

            std::vector<ValuePtr> args;
            for (size_t i = 0; i < func->params.size(); i++)
                args.push_back(parse_expr());

            auto tokens_copy = tokens_;
            auto global = global_env_;
            int bstart = func->body_start;
            int bend = func->body_end;
            auto param_names = func->params;

            auto future = std::async(std::launch::async, [=]() -> ValuePtr {
                auto thread_env = std::make_shared<Environment>(global);
                for (size_t i = 0; i < param_names.size(); i++)
                    thread_env->set_local(param_names[i],
                        i < args.size() ? args[i] : Value::make_null());
                Interpreter thread_interp;
                thread_interp.tokens_ = tokens_copy;
                thread_interp.pos_ = bstart;
                thread_interp.env_ = thread_env;
                thread_interp.global_env_ = global;
                thread_interp.source_ = "";
                thread_interp.filename_ = "<async>";
                ValuePtr result = Value::make_null();
                try {
                    while ((int)thread_interp.pos_ < bend && !thread_interp.at_end())
                        thread_interp.exec_statement();
                } catch (ReturnSignal& ret) { result = ret.value; }
                catch (...) {}
                return result;
            });
            return future.get();
        }

        // new ClassName — create class instance
        if (check(Tok::KW_NEW)) {
            advance();
            if (!check(Tok::IDENT)) throw_syntax("expected class name after 'new'");
            auto class_name = advance().value;
            auto cls = env_->get("__class_" + class_name);
            if (!cls || cls->type != Value::MAP) throw_name(class_name);

            // Create instance (copy of class map)
            auto instance = Value::make_map(cls->map_val);
            // Call init if it exists
            auto init_func = instance->map_val.find("init");
            if (init_func != instance->map_val.end() && init_func->second->type == Value::FUNC) {
                // Parse constructor args
                auto func = init_func->second;
                std::vector<ValuePtr> args;
                for (size_t i = 0; i < func->params.size(); i++)
                    args.push_back(parse_expr());
                auto func_env = std::make_shared<Environment>(global_env_);
                func_env->set_local("this", instance);
                for (size_t i = 0; i < func->params.size(); i++)
                    func_env->set_local(func->params[i], i < args.size() ? args[i] : Value::make_null());
                auto old_env = env_; auto old_pos = pos_;
                env_ = func_env; pos_ = func->body_start;
                try { while ((int)pos_ < func->body_end && !at_end()) exec_statement(); }
                catch (ReturnSignal&) {}
                env_ = old_env; pos_ = old_pos;
            }
            return instance;
        }

        // this — access current object in class methods
        if (check(Tok::KW_THIS)) {
            advance();
            auto val = env_->get("this");
            return val ? val : Value::make_null();
        }

        // lambda: \$a $b => expr
        // Syntax: \$params => body_expr (single expression)
        if (check(Tok::IDENT) && cur().value == "fn") {
            advance();
            std::vector<std::string> params;
            while (check(Tok::DOLLAR_IDENT))
                params.push_back(advance().value);
            expect(Tok::FAT_ARROW, "expected '=>' in lambda");
            // Lambda body: insert a FAT_ARROW token before the expression
            // so call_function sees => expr and returns it
            int body_start = (int)pos_ - 1; // include the => token
            parse_expr(); // skip over body expression
            int body_end = (int)pos_;
            return Value::make_func("lambda", params, body_start, body_end);
        }

        throw_syntax("unexpected token: '" + cur().value + "'");
        return Value::make_null();
    }

    // ── Execute a #builtin ──────────────────────────────────────────────

    ValuePtr exec_hash_builtin(const std::string& name) {
        if (name == "int") {
            auto val = parse_expr();
            if (val->type == Value::NUM) return Value::make_num((int64_t)val->num);
            if (val->type == Value::BOOL) return Value::make_num(val->boolean ? 1 : 0);
            if (val->type == Value::STR) {
                try { size_t p; int64_t n = std::stoll(val->str, &p); if (p == val->str.size()) return Value::make_num((double)n); }
                catch (...) {}
                throw_value("cannot convert '" + val->str + "' to int");
            }
            throw_type("cannot convert " + std::string(val->type == Value::TNULL ? "null" : "value") + " to int");
        }
        if (name == "float") {
            auto val = parse_expr();
            if (val->type == Value::NUM) return val;
            if (val->type == Value::STR) {
                try { size_t p; double n = std::stod(val->str, &p); if (p == val->str.size()) return Value::make_num(n); }
                catch (...) {}
                throw_value("cannot convert '" + val->str + "' to float");
            }
            return Value::make_num(val->as_num());
        }
        if (name == "str") {
            auto val = parse_expr();
            return Value::make_str(val->as_str());
        }
        if (name == "rand") {
            auto lo = parse_expr();
            auto hi = parse_expr();
            int l = (int)lo->as_num(), h = (int)hi->as_num();
            return Value::make_num(l + rand() % (h - l + 1));
        }
        if (name == "abs") {
            auto val = parse_expr();
            return Value::make_num(fabs(val->as_num()));
        }
        if (name == "round") {
            auto val = parse_expr();
            return Value::make_num(round(val->as_num()));
        }
        if (name == "floor") {
            auto val = parse_expr();
            return Value::make_num(floor(val->as_num()));
        }
        if (name == "ceil") {
            auto val = parse_expr();
            return Value::make_num(ceil(val->as_num()));
        }
        if (name == "sqrt") {
            auto val = parse_expr();
            return Value::make_num(sqrt(val->as_num()));
        }
        if (name == "time") {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            return Value::make_num((double)ms);
        }
        if (name == "type") {
            auto val = parse_expr();
            const char* names[] = {"number", "string", "bool", "list", "map", "function", "null"};
            return Value::make_str(names[val->type]);
        }
        if (name == "len") {
            auto val = parse_expr();
            if (val->type == Value::STR) return Value::make_num((double)val->str.size());
            if (val->type == Value::LIST) return Value::make_num((double)val->list.size());
            if (val->type == Value::MAP) return Value::make_num((double)val->map_val.size());
            return Value::make_num(0);
        }

        // Type checks
        if (name == "isnum") {
            auto val = parse_expr();
            return Value::make_bool(val->type == Value::NUM);
        }
        if (name == "isstr") {
            auto val = parse_expr();
            return Value::make_bool(val->type == Value::STR);
        }
        if (name == "islist") {
            auto val = parse_expr();
            return Value::make_bool(val->type == Value::LIST);
        }
        if (name == "ismap") {
            auto val = parse_expr();
            return Value::make_bool(val->type == Value::MAP);
        }
        if (name == "isbool") {
            auto val = parse_expr();
            return Value::make_bool(val->type == Value::BOOL);
        }
        if (name == "isnull") {
            auto val = parse_expr();
            return Value::make_bool(val->type == Value::TNULL);
        }

        // Safe number cast (returns null on fail instead of crashing)
        if (name == "num") {
            auto val = parse_expr();
            if (val->type == Value::NUM) return val;
            if (val->type == Value::STR) {
                try { return Value::make_num(std::stod(val->str)); }
                catch (...) { return Value::make_null(); }
            }
            if (val->type == Value::BOOL) return Value::make_num(val->boolean ? 1 : 0);
            return Value::make_null();
        }
        if (name == "bool") {
            auto val = parse_expr();
            return Value::make_bool(val->truthy());
        }

        // Math
        if (name == "min") {
            auto a = parse_expr();
            auto b = parse_expr();
            return Value::make_num(std::min(a->as_num(), b->as_num()));
        }
        if (name == "max") {
            auto a = parse_expr();
            auto b = parse_expr();
            return Value::make_num(std::max(a->as_num(), b->as_num()));
        }
        if (name == "clamp") {
            auto val = parse_expr();
            auto lo = parse_expr();
            auto hi = parse_expr();
            double v = val->as_num();
            if (v < lo->as_num()) v = lo->as_num();
            if (v > hi->as_num()) v = hi->as_num();
            return Value::make_num(v);
        }
        if (name == "log") {
            auto val = parse_expr();
            return Value::make_num(log(val->as_num()));
        }
        if (name == "sin") {
            auto val = parse_expr();
            return Value::make_num(sin(val->as_num()));
        }
        if (name == "cos") {
            auto val = parse_expr();
            return Value::make_num(cos(val->as_num()));
        }
        if (name == "tan") {
            auto val = parse_expr();
            return Value::make_num(tan(val->as_num()));
        }
        if (name == "pow") {
            auto base = parse_expr();
            auto exp = parse_expr();
            return Value::make_num(pow(base->as_num(), exp->as_num()));
        }
        if (name == "sign") {
            auto val = parse_expr();
            double v = val->as_num();
            return Value::make_num(v > 0 ? 1 : (v < 0 ? -1 : 0));
        }

        // Base64
        if (name == "base64") {
            auto val = parse_expr();
            return Value::make_str(base64_encode(val->as_str()));
        }
        if (name == "decode64") {
            auto val = parse_expr();
            return Value::make_str(base64_decode(val->as_str()));
        }

        // HEX cipher — custom encryption
        if (name == "encrypt") {
            auto plaintext = parse_expr();
            auto password = parse_expr();
            return Value::make_str(hex_encrypt(plaintext->as_str(), password->as_str()));
        }
        if (name == "decrypt") {
            auto ciphertext = parse_expr();
            auto password = parse_expr();
            std::string result = hex_decrypt(ciphertext->as_str(), password->as_str());
            if (result.empty() && !ciphertext->as_str().empty())
                throw_value("decryption failed — wrong password or corrupted data");
            return Value::make_str(result);
        }

        // JSON
        if (name == "json") {
            auto val = parse_expr();
            return json_parse(val->as_str());
        }
        if (name == "tojson") {
            auto val = parse_expr();
            return Value::make_str(json_stringify(val, 2));
        }
        if (name == "jsonc") {
            // Compact JSON (no whitespace)
            auto val = parse_expr();
            return Value::make_str(json_stringify(val, 0));
        }

        // Regex
        if (name == "match") {
            auto text = parse_expr()->as_str();
            auto pattern = parse_expr()->as_str();
            try {
                std::regex re(pattern);
                std::smatch match;
                if (std::regex_search(text, match, re)) {
                    ValueList groups;
                    for (size_t i = 0; i < match.size(); i++)
                        groups.push_back(Value::make_str(match[i].str()));
                    return Value::make_list(groups);
                }
            } catch (...) {}
            return Value::make_null();
        }
        if (name == "matchall") {
            auto text = parse_expr()->as_str();
            auto pattern = parse_expr()->as_str();
            ValueList all;
            try {
                std::regex re(pattern);
                auto begin = std::sregex_iterator(text.begin(), text.end(), re);
                auto end = std::sregex_iterator();
                for (auto it = begin; it != end; ++it)
                    all.push_back(Value::make_str((*it)[0].str()));
            } catch (...) {}
            return Value::make_list(all);
        }
        if (name == "replacex") {
            auto text = parse_expr()->as_str();
            auto pattern = parse_expr()->as_str();
            auto replacement = parse_expr()->as_str();
            try {
                std::regex re(pattern);
                return Value::make_str(std::regex_replace(text, re, replacement));
            } catch (...) {}
            return Value::make_str(text);
        }
        if (name == "test") {
            // regex test — returns true/false
            auto text = parse_expr()->as_str();
            auto pattern = parse_expr()->as_str();
            try {
                std::regex re(pattern);
                return Value::make_bool(std::regex_search(text, re));
            } catch (...) {}
            return Value::make_bool(false);
        }

        // String padding
        if (name == "padl") {
            auto text = parse_expr()->as_str();
            auto width = (int)parse_expr()->as_num();
            auto fill = parse_expr()->as_str();
            char fc = fill.empty() ? ' ' : fill[0];
            while ((int)text.size() < width) text = fc + text;
            return Value::make_str(text);
        }
        if (name == "padr") {
            auto text = parse_expr()->as_str();
            auto width = (int)parse_expr()->as_num();
            auto fill = parse_expr()->as_str();
            char fc = fill.empty() ? ' ' : fill[0];
            while ((int)text.size() < width) text += fc;
            return Value::make_str(text);
        }
        if (name == "center") {
            auto text = parse_expr()->as_str();
            auto width = (int)parse_expr()->as_num();
            auto fill = parse_expr()->as_str();
            char fc = fill.empty() ? ' ' : fill[0];
            while ((int)text.size() < width) {
                text += fc;
                if ((int)text.size() < width) text = fc + text;
            }
            return Value::make_str(text);
        }

        // Range — with optional step: #range 1 10 or #range 1 10 2 or #range 10 0 (-2)
        if (name == "range") {
            auto start = parse_expr();
            auto end_v = parse_expr();
            int s = (int)start->as_num(), e = (int)end_v->as_num();
            int step = (s <= e) ? 1 : -1;
            // Check for optional step — must use parens for negative: #range 10 0 (-2)
            if (!check(Tok::NEWLINE) && !check(Tok::EOF_TOK) && !check(Tok::DOUBLE_SEMI)) {
                auto step_val = parse_expr();
                if (step_val->type == Value::NUM) step = (int)step_val->as_num();
            }
            ValueList items;
            if (step > 0) { for (int i = s; i <= e; i += step) items.push_back(Value::make_num(i)); }
            else if (step < 0) { for (int i = s; i >= e; i += step) items.push_back(Value::make_num(i)); }
            return Value::make_list(items);
        }

        // chr / ord — character code conversion
        if (name == "chr") {
            auto val = parse_expr();
            char c = (char)(int)val->as_num();
            return Value::make_str(std::string(1, c));
        }
        if (name == "ord") {
            auto val = parse_expr();
            if (val->type == Value::STR && !val->str.empty())
                return Value::make_num((double)(uint8_t)val->str[0]);
            return Value::make_num(0);
        }

        // hex / bin / oct — number format conversion
        if (name == "hex") {
            auto val = parse_expr();
            char buf[32]; snprintf(buf, sizeof(buf), "0x%X", (unsigned)(int)val->as_num());
            return Value::make_str(buf);
        }
        if (name == "bin") {
            auto val = parse_expr();
            unsigned n = (unsigned)(int)val->as_num();
            std::string bits = "0b";
            if (n == 0) bits += "0";
            else { std::string tmp; while (n) { tmp += (n & 1) ? '1' : '0'; n >>= 1; } std::reverse(tmp.begin(), tmp.end()); bits += tmp; }
            return Value::make_str(bits);
        }
        if (name == "oct") {
            auto val = parse_expr();
            char buf[32]; snprintf(buf, sizeof(buf), "0o%o", (unsigned)(int)val->as_num());
            return Value::make_str(buf);
        }

        // Math constants
        if (name == "pi") return Value::make_num(3.14159265358979323846);
        if (name == "e") return Value::make_num(2.71828182845904523536);
        if (name == "inf") return Value::make_num(std::numeric_limits<double>::infinity());

        // argv — command line args as list
        if (name == "argv") {
            // Stored at startup in __argv
            auto v = env_->get("__argv");
            return v ? v : Value::make_list();
        }

        // env — read environment variable
        if (name == "env") {
            auto key = parse_expr()->as_str();
            const char* val = getenv(key.c_str());
            return val ? Value::make_str(val) : Value::make_null();
        }

        // sorted — return NEW sorted list (non-mutating)
        if (name == "sorted") {
            auto val = parse_expr();
            if (val->type == Value::LIST) {
                ValueList copy = val->list;
                std::sort(copy.begin(), copy.end(),
                    [](const ValuePtr& a, const ValuePtr& b) { return a->as_num() < b->as_num(); });
                return Value::make_list(copy);
            }
            return val;
        }
        // reversed — return NEW reversed list (non-mutating)
        if (name == "reversed") {
            auto val = parse_expr();
            if (val->type == Value::LIST) {
                ValueList copy = val->list;
                std::reverse(copy.begin(), copy.end());
                return Value::make_list(copy);
            }
            if (val->type == Value::STR) {
                std::string s = val->str;
                std::reverse(s.begin(), s.end());
                return Value::make_str(s);
            }
            return val;
        }

        // set — create unique list (like Python set)
        if (name == "set") {
            auto val = parse_expr();
            if (val->type == Value::LIST) {
                ValueList result;
                std::set<std::string> seen;
                for (auto& item : val->list) {
                    if (seen.insert(item->as_str()).second)
                        result.push_back(item);
                }
                return Value::make_list(result);
            }
            return val;
        }

        // input — alternative to << (returns value)
        if (name == "input") {
            std::string prompt;
            if (check(Tok::STRING) || check(Tok::DOLLAR_IDENT)) {
                auto p = parse_expr();
                prompt = p->as_str();
            }
            if (!prompt.empty()) std::cout << prompt << std::flush;
            std::string line;
            std::getline(std::cin, line);
            return Value::make_str(line);
        }

        // Random
        if (name == "choice") {
            auto val = parse_expr();
            if (val->type == Value::LIST && !val->list.empty())
                return val->list[rand() % val->list.size()];
            if (val->type == Value::STR && !val->str.empty())
                return Value::make_str(std::string(1, val->str[rand() % val->str.size()]));
            return Value::make_null();
        }
        if (name == "sample") {
            auto val = parse_expr();
            auto count = (int)parse_expr()->as_num();
            if (val->type == Value::LIST) {
                ValueList copy = val->list;
                for (int i = (int)copy.size() - 1; i > 0; i--) {
                    int j = rand() % (i + 1);
                    std::swap(copy[i], copy[j]);
                }
                ValueList result(copy.begin(), copy.begin() + std::min(count, (int)copy.size()));
                return Value::make_list(result);
            }
            return Value::make_list();
        }
        if (name == "seed") {
            auto val = parse_expr();
            srand((unsigned)val->as_num());
            return Value::make_null();
        }
        if (name == "uniform") {
            auto lo = parse_expr()->as_num();
            auto hi = parse_expr()->as_num();
            double r = lo + ((double)rand() / RAND_MAX) * (hi - lo);
            return Value::make_num(r);
        }

        // Math extras
        if (name == "factorial") {
            auto val = (int)parse_expr()->as_num();
            int64_t result = 1;
            for (int i = 2; i <= val; i++) result *= i;
            return Value::make_num((double)result);
        }
        if (name == "gcd") {
            int a = (int)parse_expr()->as_num();
            int b = (int)parse_expr()->as_num();
            while (b) { int t = b; b = a % b; a = t; }
            return Value::make_num(std::abs(a));
        }
        if (name == "lcm") {
            int a = (int)parse_expr()->as_num();
            int b = (int)parse_expr()->as_num();
            int g = a; int tb = b;
            while (tb) { int t = tb; tb = g % tb; g = t; }
            return Value::make_num(std::abs(a / g * b));
        }
        if (name == "isnan") {
            auto val = parse_expr();
            return Value::make_bool(std::isnan(val->as_num()));
        }
        if (name == "isinf") {
            auto val = parse_expr();
            return Value::make_bool(std::isinf(val->as_num()));
        }
        if (name == "radians") {
            return Value::make_num(parse_expr()->as_num() * 3.14159265358979323846 / 180.0);
        }
        if (name == "degrees") {
            return Value::make_num(parse_expr()->as_num() * 180.0 / 3.14159265358979323846);
        }
        if (name == "hypot") {
            auto a = parse_expr()->as_num();
            auto b = parse_expr()->as_num();
            return Value::make_num(sqrt(a*a + b*b));
        }
        if (name == "atan2") {
            auto y = parse_expr()->as_num();
            auto x = parse_expr()->as_num();
            return Value::make_num(atan2(y, x));
        }

        // Statistics
        if (name == "mean") {
            auto val = parse_expr();
            if (val->type == Value::LIST && !val->list.empty()) {
                double sum = 0;
                for (auto& v : val->list) sum += v->as_num();
                return Value::make_num(sum / val->list.size());
            }
            return Value::make_num(0);
        }
        if (name == "median") {
            auto val = parse_expr();
            if (val->type == Value::LIST && !val->list.empty()) {
                std::vector<double> nums;
                for (auto& v : val->list) nums.push_back(v->as_num());
                std::sort(nums.begin(), nums.end());
                size_t n = nums.size();
                if (n % 2 == 0) return Value::make_num((nums[n/2-1] + nums[n/2]) / 2.0);
                return Value::make_num(nums[n/2]);
            }
            return Value::make_num(0);
        }
        if (name == "mode") {
            auto val = parse_expr();
            if (val->type == Value::LIST && !val->list.empty()) {
                std::map<std::string, int> freq;
                for (auto& v : val->list) freq[v->as_str()]++;
                std::string best; int best_count = 0;
                for (auto& [k, c] : freq) { if (c > best_count) { best = k; best_count = c; } }
                // Return as number if possible
                try { return Value::make_num(std::stod(best)); } catch (...) {}
                return Value::make_str(best);
            }
            return Value::make_null();
        }
        if (name == "stdev") {
            auto val = parse_expr();
            if (val->type == Value::LIST && val->list.size() > 1) {
                double sum = 0, n = (double)val->list.size();
                for (auto& v : val->list) sum += v->as_num();
                double mean = sum / n;
                double sq_sum = 0;
                for (auto& v : val->list) { double d = v->as_num() - mean; sq_sum += d * d; }
                return Value::make_num(sqrt(sq_sum / (n - 1)));
            }
            return Value::make_num(0);
        }
        if (name == "variance") {
            auto val = parse_expr();
            if (val->type == Value::LIST && val->list.size() > 1) {
                double sum = 0, n = (double)val->list.size();
                for (auto& v : val->list) sum += v->as_num();
                double mean = sum / n;
                double sq_sum = 0;
                for (auto& v : val->list) { double d = v->as_num() - mean; sq_sum += d * d; }
                return Value::make_num(sq_sum / (n - 1));
            }
            return Value::make_num(0);
        }

        // UUID
        if (name == "uuid") {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> hex_dist(0, 15);
            std::string uuid;
            const char* hex_chars = "0123456789abcdef";
            for (int i = 0; i < 32; i++) {
                if (i == 8 || i == 12 || i == 16 || i == 20) uuid += '-';
                uuid += hex_chars[hex_dist(gen)];
            }
            return Value::make_str(uuid);
        }

        // Hash (SHA-256 style simple hash)
        if (name == "hash") {
            auto val = parse_expr()->as_str();
            // Simple FNV-1a hash, output as hex string
            uint64_t h = 0xcbf29ce484222325ULL;
            for (char c : val) { h ^= (uint8_t)c; h *= 0x100000001b3ULL; }
            char buf[32]; snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
            return Value::make_str(buf);
        }

        // Counter — count occurrences, return map
        if (name == "counter") {
            auto val = parse_expr();
            ValueMap counts;
            if (val->type == Value::LIST) {
                for (auto& item : val->list) {
                    std::string key = item->as_str();
                    auto it = counts.find(key);
                    if (it != counts.end()) it->second = Value::make_num(it->second->as_num() + 1);
                    else counts[key] = Value::make_num(1);
                }
            } else if (val->type == Value::STR) {
                for (char c : val->str) {
                    std::string key(1, c);
                    auto it = counts.find(key);
                    if (it != counts.end()) it->second = Value::make_num(it->second->as_num() + 1);
                    else counts[key] = Value::make_num(1);
                }
            }
            return Value::make_map(counts);
        }

        // Copy / deepcopy
        if (name == "copy") {
            auto val = parse_expr();
            if (val->type == Value::LIST) return Value::make_list(val->list);
            if (val->type == Value::MAP) return Value::make_map(val->map_val);
            if (val->type == Value::STR) return Value::make_str(val->str);
            if (val->type == Value::NUM) return Value::make_num(val->num);
            if (val->type == Value::BOOL) return Value::make_bool(val->boolean);
            return Value::make_null();
        }
        if (name == "deepcopy") {
            // For now same as copy — deep copy nested structures
            std::function<ValuePtr(ValuePtr)> deep = [&](ValuePtr v) -> ValuePtr {
                if (!v) return Value::make_null();
                if (v->type == Value::LIST) {
                    ValueList copy;
                    for (auto& item : v->list) copy.push_back(deep(item));
                    return Value::make_list(copy);
                }
                if (v->type == Value::MAP) {
                    ValueMap copy;
                    for (auto& [k, val] : v->map_val) copy[k] = deep(val);
                    return Value::make_map(copy);
                }
                if (v->type == Value::STR) return Value::make_str(v->str);
                if (v->type == Value::NUM) return Value::make_num(v->num);
                if (v->type == Value::BOOL) return Value::make_bool(v->boolean);
                return Value::make_null();
            };
            return deep(parse_expr());
        }

        // Eval — evaluate HEX code string
        if (name == "eval") {
            auto code = parse_expr()->as_str();
            // Save state, run eval code, restore
            auto old_tokens = tokens_;
            auto old_pos = pos_;
            auto old_source = source_;
            auto old_file = filename_;
            Lexer lex(code);
            tokens_ = lex.tokenize();
            pos_ = 0;
            source_ = code;
            filename_ = "<eval>";
            while (!at_end()) {
                try { exec_statement(); }
                catch (HexError& e) { ErrorFormatter::print(e, source_); break; }
                catch (ReturnSignal& r) { tokens_ = old_tokens; pos_ = old_pos; source_ = old_source; filename_ = old_file; return r.value; }
            }
            tokens_ = old_tokens;
            pos_ = old_pos;
            source_ = old_source;
            filename_ = old_file;
            return Value::make_null();
        }

        // Combinatorics
        if (name == "combinations") {
            auto list = parse_expr();
            int r = (int)parse_expr()->as_num();
            if (list->type != Value::LIST) return Value::make_list();
            ValueList result;
            int n = (int)list->list.size();
            std::vector<int> indices(r);
            for (int i = 0; i < r; i++) indices[i] = i;
            while (true) {
                ValueList combo;
                for (int i : indices) combo.push_back(list->list[i]);
                result.push_back(Value::make_list(combo));
                int i = r - 1;
                while (i >= 0 && indices[i] == i + n - r) i--;
                if (i < 0) break;
                indices[i]++;
                for (int j = i + 1; j < r; j++) indices[j] = indices[j-1] + 1;
            }
            return Value::make_list(result);
        }
        if (name == "permutations") {
            auto list = parse_expr();
            if (list->type != Value::LIST) return Value::make_list();
            ValueList items = list->list;
            std::sort(items.begin(), items.end(),
                [](const ValuePtr& a, const ValuePtr& b) { return a->as_str() < b->as_str(); });
            ValueList result;
            do {
                result.push_back(Value::make_list(items));
            } while (std::next_permutation(items.begin(), items.end(),
                [](const ValuePtr& a, const ValuePtr& b) { return a->as_str() < b->as_str(); }));
            return Value::make_list(result);
        }
        if (name == "product") {
            // Cartesian product of two lists
            auto l1 = parse_expr();
            auto l2 = parse_expr();
            if (l1->type != Value::LIST || l2->type != Value::LIST) return Value::make_list();
            ValueList result;
            for (auto& a : l1->list)
                for (auto& b : l2->list)
                    result.push_back(Value::make_list({a, b}));
            return Value::make_list(result);
        }

        // Glob — find files by pattern
        if (name == "glob") {
            auto pattern = parse_expr()->as_str();
            ValueList found;
            // Extract directory and extension pattern
            std::string dir = ".";
            std::string ext_pattern = pattern;
            auto sep = pattern.find_last_of("/\\");
            if (sep != std::string::npos) {
                dir = pattern.substr(0, sep);
                ext_pattern = pattern.substr(sep + 1);
            }
            try {
                for (auto& entry : std::filesystem::directory_iterator(dir)) {
                    std::string name = entry.path().filename().string();
                    if (ext_pattern[0] == '*') {
                        std::string ext = ext_pattern.substr(1);
                        if (name.size() >= ext.size() && name.substr(name.size() - ext.size()) == ext)
                            found.push_back(Value::make_str(entry.path().string()));
                    } else if (name == ext_pattern) {
                        found.push_back(Value::make_str(entry.path().string()));
                    }
                }
            } catch (...) {}
            return Value::make_list(found);
        }

        // Tempfile — create temporary file, return path
        if (name == "tempfile") {
            char tmp[MAX_PATH];
            char tmpDir[MAX_PATH];
            GetTempPathA(MAX_PATH, tmpDir);
            GetTempFileNameA(tmpDir, "hex", 0, tmp);
            return Value::make_str(tmp);
        }

        // Sleep alias (seconds instead of ms)
        if (name == "sleep") {
            auto secs = parse_expr()->as_num();
            std::this_thread::sleep_for(std::chrono::milliseconds((int)(secs * 1000)));
            return Value::make_null();
        }

        // Query builtins as expressions (return values instead of printing)
        if (name == "has") {
            auto container = parse_expr();
            auto needle = parse_expr();
            if (container->type == Value::STR)
                return Value::make_bool(container->str.find(needle->as_str()) != std::string::npos);
            if (container->type == Value::LIST) {
                for (auto& item : container->list)
                    if (item->as_str() == needle->as_str()) return Value::make_bool(true);
                return Value::make_bool(false);
            }
            if (container->type == Value::MAP)
                return Value::make_bool(container->map_val.count(needle->as_str()) > 0);
            return Value::make_bool(false);
        }
        if (name == "contains") {
            auto container = parse_expr();
            auto needle = parse_expr();
            if (container->type == Value::LIST) {
                for (auto& item : container->list)
                    if (item->as_str() == needle->as_str()) return Value::make_bool(true);
            }
            return Value::make_bool(false);
        }
        if (name == "find") {
            auto container = parse_expr();
            auto needle = parse_expr();
            if (container->type == Value::STR) {
                size_t p = container->str.find(needle->as_str());
                return Value::make_num(p != std::string::npos ? (double)p : -1);
            }
            if (container->type == Value::LIST) {
                for (size_t i = 0; i < container->list.size(); i++)
                    if (container->list[i]->as_str() == needle->as_str()) return Value::make_num((double)i);
                return Value::make_num(-1);
            }
            return Value::make_num(-1);
        }
        if (name == "count") {
            auto container = parse_expr();
            auto needle = parse_expr();
            int c = 0;
            if (container->type == Value::STR) {
                size_t p = 0;
                while ((p = container->str.find(needle->as_str(), p)) != std::string::npos) { c++; p += needle->as_str().size(); }
            } else if (container->type == Value::LIST) {
                for (auto& item : container->list) if (item->as_str() == needle->as_str()) c++;
            }
            return Value::make_num(c);
        }
        if (name == "sum") {
            auto list = parse_expr();
            double total = 0;
            if (list->type == Value::LIST)
                for (auto& item : list->list) total += item->as_num();
            return Value::make_num(total);
        }
        if (name == "min_of") {
            auto list = parse_expr();
            if (list->type == Value::LIST && !list->list.empty()) {
                double m = list->list[0]->as_num();
                for (auto& item : list->list) if (item->as_num() < m) m = item->as_num();
                return Value::make_num(m);
            }
            return Value::make_null();
        }
        if (name == "max_of") {
            auto list = parse_expr();
            if (list->type == Value::LIST && !list->list.empty()) {
                double m = list->list[0]->as_num();
                for (auto& item : list->list) if (item->as_num() > m) m = item->as_num();
                return Value::make_num(m);
            }
            return Value::make_null();
        }

        // Date/time
        if (name == "date") {
            time_t now = ::time(NULL);
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&now));
            return Value::make_str(buf);
        }
        if (name == "clock") {
            time_t now = ::time(NULL);
            char buf[32];
            strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&now));
            return Value::make_str(buf);
        }

        error("unknown builtin: #" + name);
        return Value::make_null();
    }

    // ── Call a user-defined function ─────────────────────────────────────

    ValuePtr call_function(ValuePtr func) {
        // Parse arguments
        std::vector<ValuePtr> args;
        for (size_t i = 0; i < func->params.size(); i++) {
            args.push_back(parse_expr());
        }

        // Create new scope
        auto func_env = std::make_shared<Environment>(global_env_);
        for (size_t i = 0; i < func->params.size(); i++) {
            func_env->set_local(func->params[i], i < args.size() ? args[i] : Value::make_null());
        }

        // Execute body
        auto old_env = env_;
        auto old_pos = pos_;
        env_ = func_env;
        pos_ = func->body_start;

        ValuePtr result = Value::make_null();
        try {
            while ((int)pos_ < func->body_end && !at_end()) {
                exec_statement();
            }
        } catch (ReturnSignal& ret) {
            result = ret.value;
        }

        env_ = old_env;
        pos_ = old_pos;
        return result;
    }

    // ── Statement execution ─────────────────────────────────────────────

    void exec_statement() {
        skip_newlines();
        if (at_end()) return;

        // >> print (with optional color: >> red "error!")
        if (check(Tok::RSHIFT)) {
            advance();
            // Check for color keyword
            std::string color_code;
            if (check(Tok::IDENT)) {
                std::string c = cur().value;
                if (c == "red")     { color_code = "\033[31m"; advance(); }
                else if (c == "green")  { color_code = "\033[32m"; advance(); }
                else if (c == "yellow") { color_code = "\033[33m"; advance(); }
                else if (c == "blue")   { color_code = "\033[34m"; advance(); }
                else if (c == "cyan")   { color_code = "\033[36m"; advance(); }
                else if (c == "magenta"){ color_code = "\033[35m"; advance(); }
                else if (c == "white")  { color_code = "\033[37m"; advance(); }
                else if (c == "bold")   { color_code = "\033[1m";  advance(); }
                else if (c == "dim")    { color_code = "\033[2m";  advance(); }
            }
            auto val = parse_expr();
            if (!color_code.empty())
                std::cout << color_code << val->as_str() << "\033[0m" << std::endl;
            else
                std::cout << val->as_str() << std::endl;
            return;
        }

        // >>> print without newline
        if (check(Tok::TRIPLE_RSHIFT)) {
            advance();
            auto val = parse_expr();
            std::cout << val->as_str() << std::flush;
            return;
        }

        // << input
        if (check(Tok::LSHIFT)) {
            advance();
            if (!check(Tok::DOLLAR_IDENT)) error("expected $variable after <<");
            auto var_name = advance().value;
            std::string prompt;
            if (check(Tok::STRING)) prompt = advance().value;
            if (!prompt.empty()) std::cout << prompt << " ";
            std::string input;
            std::getline(std::cin, input);
            // Auto-detect type
            try {
                size_t processed;
                double n = std::stod(input, &processed);
                if (processed == input.size())
                    env_->set(var_name, Value::make_num(n));
                else
                    env_->set(var_name, Value::make_str(input));
            } catch (...) {
                env_->set(var_name, Value::make_str(input));
            }
            return;
        }

        // $var -> expr (assignment)
        if (check(Tok::DOLLAR_IDENT)) {
            auto var_name = cur().value;
            size_t save = pos_;

            advance();

            // $var[idx] -> expr (indexed assignment)
            if (check(Tok::LBRACKET)) {
                advance();
                auto idx = parse_expr();
                expect(Tok::RBRACKET, "expected ']'");
                expect(Tok::ARROW, "expected '->'");
                auto val = parse_expr();

                auto container = env_->get(var_name);
                if (!container) throw_name(var_name);

                if (container->type == Value::LIST) {
                    int i = (int)idx->as_num();
                    if (i < 0) i += (int)container->list.size();
                    if (i >= 0 && i < (int)container->list.size())
                        container->list[i] = val;
                } else if (container->type == Value::MAP) {
                    container->map_val[idx->as_str()] = val;
                } else if (container->type == Value::STR) {
                    int i = (int)idx->as_num();
                    if (i >= 0 && i < (int)container->str.size())
                        container->str[i] = val->as_str()[0];
                }
                return;
            }

            if (check(Tok::ARROW)) {
                advance();
                // Check if var is locked (constant)
                auto locked = env_->get("__lock_" + var_name);
                if (locked && locked->truthy())
                    throw_runtime("cannot reassign constant '$" + var_name + "'");
                auto val = parse_expr();
                env_->set(var_name, val);
                return;
            }

            // += -= *= /= compound assignment
            if (check(Tok::PLUS_EQ) || check(Tok::MINUS_EQ) ||
                check(Tok::STAR_EQ) || check(Tok::SLASH_EQ)) {
                auto op = advance();
                auto rhs = parse_expr();
                auto current = env_->get(var_name);
                if (!current) current = Value::make_num(0);
                auto locked = env_->get("__lock_" + var_name);
                if (locked && locked->truthy())
                    throw_runtime("cannot reassign constant '$" + var_name + "'");
                if (op.type == Tok::PLUS_EQ) {
                    if (current->type == Value::STR || rhs->type == Value::STR)
                        env_->set(var_name, Value::make_str(current->as_str() + rhs->as_str()));
                    else
                        env_->set(var_name, Value::make_num(current->as_num() + rhs->as_num()));
                } else if (op.type == Tok::MINUS_EQ) {
                    env_->set(var_name, Value::make_num(current->as_num() - rhs->as_num()));
                } else if (op.type == Tok::STAR_EQ) {
                    env_->set(var_name, Value::make_num(current->as_num() * rhs->as_num()));
                } else if (op.type == Tok::SLASH_EQ) {
                    double d = rhs->as_num();
                    if (d == 0) throw hex_error::zero_division(filename_, cur().line, cur().col);
                    env_->set(var_name, Value::make_num(current->as_num() / d));
                }
                return;
            }

            // $var.method args — method call on object
            if (check(Tok::DOT)) {
                advance();
                if (!check(Tok::IDENT)) throw_syntax("expected method name after '.'");
                auto method_name = advance().value;
                auto obj = env_->get(var_name);
                if (!obj || obj->type != Value::MAP) throw_type("$" + var_name + " is not an object");
                auto func_it = obj->map_val.find(method_name);
                if (func_it == obj->map_val.end() || func_it->second->type != Value::FUNC)
                    throw_name(var_name + "." + method_name);
                auto func = func_it->second;
                std::vector<ValuePtr> args;
                for (size_t i = 0; i < func->params.size(); i++)
                    args.push_back(parse_expr());
                auto func_env = std::make_shared<Environment>(global_env_);
                func_env->set_local("this", obj);
                for (size_t i = 0; i < func->params.size(); i++)
                    func_env->set_local(func->params[i], i < args.size() ? args[i] : Value::make_null());
                auto old_env = env_; auto old_pos = pos_;
                env_ = func_env; pos_ = func->body_start;
                try { while ((int)pos_ < func->body_end && !at_end()) exec_statement(); }
                catch (ReturnSignal&) {}
                env_ = old_env; pos_ = old_pos;
                return;
            }

            // Not an assignment, backtrack
            pos_ = save;
        }

        // ? condition :: ... ;; (if/else)
        if (check(Tok::QUESTION)) {
            advance();
            auto cond = parse_expr();
            expect(Tok::DOUBLE_COLON, "expected '::' after condition");
            skip_newlines();

            bool executed = false;
            if (cond->truthy()) {
                exec_if_body();
                executed = true;
            } else {
                skip_if_body();
            }

            // ?? else-if chains
            while (check(Tok::DOUBLE_Q)) {
                advance();
                auto c2 = parse_expr();
                expect(Tok::DOUBLE_COLON, "expected '::'");
                skip_newlines();
                if (!executed && c2->truthy()) {
                    exec_if_body();
                    executed = true;
                } else {
                    skip_if_body();
                }
            }

            // :: else ::
            skip_newlines();
            if (check(Tok::DOUBLE_COLON)) {
                size_t sv = pos_;
                advance();
                skip_newlines();
                if (check(Tok::KW_ELSE)) {
                    advance();
                    skip_newlines();
                    if (check(Tok::DOUBLE_COLON)) advance();
                    skip_newlines();
                    if (!executed) {
                        exec_if_body();
                    } else {
                        skip_if_body();
                    }
                } else {
                    pos_ = sv;
                }
            }

            skip_newlines();
            if (check(Tok::DOUBLE_SEMI)) advance();
            return;
        }

        // @ while cond :: ... ;;
        // @ for $i in start..end :: ... ;;
        // @ rep N :: ... ;;
        // @ funcname $params :: ... ;; (function def)
        if (check(Tok::IDENT) && cur().value == "@" ||
            (check(Tok::HASH) && false)) {
            // nope, @ is not an ident.
        }

        // Handle @ for loops, while, rep, func
        // We need to check for IDENT that equals special words after @
        // Actually @ doesn't tokenize as IDENT. Let me check...
        // @ is not in our token list as a symbol, it appears as part of
        // identifiers. Let me handle it differently.

        // Check for keyword-based constructs
        if (check(Tok::IDENT) && cur().value == "@") {
            advance();
            // This shouldn't happen since @ isn't tokenized. Let's handle via keywords.
        }

        // @ while / @ for / @ rep / @ func — these start with keywords after newlines
        // Actually, looking at the syntax: "@ while", "@ for", "@ rep", "@ greet"
        // The @ is consumed... but we didn't add @ as a token.
        // Let me handle it: if we see KW_WHILE, KW_FOR, KW_REP directly, treat as loops

        // while $x < 10 :: ... ;;
        if (check(Tok::KW_WHILE)) {
            advance();
            size_t cond_start = pos_;

            while (true) {
                pos_ = cond_start;
                auto cond = parse_expr();
                expect(Tok::DOUBLE_COLON, "expected '::'");
                skip_newlines();

                if (!cond->truthy()) {
                    skip_block();
                    if (check(Tok::DOUBLE_SEMI)) advance();
                    break;
                }

                size_t body_start = pos_;
                try {
                    exec_block();
                } catch (BreakSignal&) {
                    // skip rest of block
                    pos_ = body_start;
                    skip_block();
                    if (check(Tok::DOUBLE_SEMI)) advance();
                    break;
                } catch (ContinueSignal&) {
                    // re-evaluate condition
                    pos_ = body_start;
                    skip_block();
                }
                // The ;; after block — we need to go back to cond
                if (check(Tok::DOUBLE_SEMI)) {
                    advance(); // consume ;; but loop back
                }
            }
            return;
        }

        // for $i in start..end :: ... ;;
        if (check(Tok::KW_FOR)) {
            advance();
            if (!check(Tok::DOLLAR_IDENT)) error("expected $variable after for");
            auto var = advance().value;
            expect(Tok::KW_IN, "expected 'in' in for loop");

            auto start_val = parse_expr();

            if (check(Tok::DOTDOT)) {
                // Numeric range: for $i in 1..10
                advance();
                auto end_val = parse_expr();
                expect(Tok::DOUBLE_COLON, "expected '::'");
                skip_newlines();

                int s = (int)start_val->as_num();
                int e = (int)end_val->as_num();
                size_t body_start = pos_;

                for (int i = s; i <= e; i++) {
                    pos_ = body_start;
                    env_->set(var, Value::make_num(i));
                    try {
                        exec_block_no_semi();
                    } catch (BreakSignal&) { break; }
                    catch (ContinueSignal&) { continue; }
                }

                // Skip to ;;
                pos_ = body_start;
                skip_block();
                if (check(Tok::DOUBLE_SEMI)) advance();
            } else {
                // List iteration: for $item in $list
                expect(Tok::DOUBLE_COLON, "expected '::'");
                skip_newlines();

                size_t body_start = pos_;

                if (start_val->type == Value::LIST) {
                    for (auto& item : start_val->list) {
                        pos_ = body_start;
                        env_->set(var, item);
                        try {
                            exec_block_no_semi();
                        } catch (BreakSignal&) { break; }
                        catch (ContinueSignal&) { continue; }
                    }
                }
                // Map iteration: iterate over keys
                else if (start_val->type == Value::MAP) {
                    for (auto& [key, val] : start_val->map_val) {
                        pos_ = body_start;
                        env_->set(var, Value::make_str(key));
                        try {
                            exec_block_no_semi();
                        } catch (BreakSignal&) { break; }
                        catch (ContinueSignal&) { continue; }
                    }
                }
                // String iteration: iterate over characters
                else if (start_val->type == Value::STR) {
                    for (char c : start_val->str) {
                        pos_ = body_start;
                        env_->set(var, Value::make_str(std::string(1, c)));
                        try {
                            exec_block_no_semi();
                        } catch (BreakSignal&) { break; }
                        catch (ContinueSignal&) { continue; }
                    }
                }

                pos_ = body_start;
                skip_block();
                if (check(Tok::DOUBLE_SEMI)) advance();
            }
            return;
        }

        // rep N :: ... ;;
        if (check(Tok::KW_REP)) {
            advance();
            auto count = parse_expr();
            expect(Tok::DOUBLE_COLON, "expected '::'");
            skip_newlines();

            int n = (int)count->as_num();
            size_t body_start = pos_;

            for (int i = 0; i < n; i++) {
                pos_ = body_start;
                try {
                    exec_block_no_semi();
                } catch (BreakSignal&) { break; }
                catch (ContinueSignal&) { continue; }
            }

            pos_ = body_start;
            skip_block();
            if (check(Tok::DOUBLE_SEMI)) advance();
            return;
        }

        // forever :: ... ;; — infinite loop
        if (check(Tok::KW_FOREVER)) {
            advance();
            expect(Tok::DOUBLE_COLON, "expected '::' after forever");
            skip_newlines();
            size_t body_start = pos_;
            while (true) {
                pos_ = body_start;
                try { exec_block_no_semi(); }
                catch (BreakSignal&) { pos_ = body_start; skip_block(); if (check(Tok::DOUBLE_SEMI)) advance(); return; }
                catch (ContinueSignal&) { continue; }
            }
        }

        // do :: ... ;; while cond — runs at least once
        if (check(Tok::KW_DO)) {
            advance();
            expect(Tok::DOUBLE_COLON, "expected '::' after do");
            skip_newlines();
            size_t body_start = pos_;
            bool first = true;
            while (true) {
                pos_ = body_start;
                try { exec_block_no_semi(); }
                catch (BreakSignal&) { pos_ = body_start; skip_block(); break; }
                catch (ContinueSignal&) {}
                if (check(Tok::DOUBLE_SEMI)) advance();
                skip_newlines();
                if (first || true) {
                    if (check(Tok::KW_WHILE)) {
                        advance();
                        auto cond = parse_expr();
                        if (!cond->truthy()) break;
                    } else break;
                }
                first = false;
            }
            return;
        }

        // match $val :: case X :: ... ;; case Y :: ... ;; default :: ... ;; ;;
        if (check(Tok::KW_MATCH)) {
            advance();
            auto match_val = parse_expr();
            expect(Tok::DOUBLE_COLON, "expected '::' after match");
            skip_newlines();

            bool matched = false;
            while (!at_end() && !check(Tok::DOUBLE_SEMI)) {
                skip_newlines();
                if (check(Tok::DOUBLE_SEMI)) break;

                if (check(Tok::KW_CASE)) {
                    advance();
                    auto case_val = parse_expr();
                    expect(Tok::DOUBLE_COLON, "expected '::' after case value");
                    skip_newlines();
                    if (!matched && match_val->as_str() == case_val->as_str()) {
                        exec_if_body();
                        matched = true;
                    } else {
                        skip_if_body();
                    }
                    if (check(Tok::DOUBLE_SEMI)) advance();
                } else if (check(Tok::KW_DEFAULT)) {
                    advance();
                    expect(Tok::DOUBLE_COLON, "expected '::' after default");
                    skip_newlines();
                    if (!matched) {
                        exec_if_body();
                        matched = true;
                    } else {
                        skip_if_body();
                    }
                    if (check(Tok::DOUBLE_SEMI)) advance();
                } else {
                    advance(); // skip unknown
                }
            }
            if (check(Tok::DOUBLE_SEMI)) advance();
            return;
        }

        // lock $var -> expr — constant assignment (can't be changed after)
        if (check(Tok::KW_LOCK)) {
            advance();
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("expected $variable after lock");
            auto var_name = advance().value;
            expect(Tok::ARROW, "expected '->' after lock $var");
            auto val = parse_expr();
            env_->set(var_name, val);
            env_->set("__lock_" + var_name, Value::make_bool(true));
            return;
        }

        // import "file.hex" — execute another hex file in current scope
        if (check(Tok::KW_IMPORT)) {
            advance();
            auto path = parse_expr();
            std::string fpath = path->as_str();
            std::ifstream f(fpath);
            if (!f.is_open()) throw hex_error::import_err(fpath, filename_, cur().line, cur().col);
            std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();
            // Save state, run imported file, restore
            auto old_tokens = tokens_;
            auto old_pos = pos_;
            auto old_source = source_;
            auto old_file = filename_;
            Lexer lex(src);
            tokens_ = lex.tokenize();
            pos_ = 0;
            source_ = src;
            filename_ = fpath;
            while (!at_end()) {
                try { exec_statement(); }
                catch (HexError& e) { ErrorFormatter::print(e, source_); break; }
            }
            tokens_ = old_tokens;
            pos_ = old_pos;
            source_ = old_source;
            filename_ = old_file;
            return;
        }

        // class ClassName :: ... ;; — define a class
        // enum Color :: red green blue ;;
        if (check(Tok::KW_ENUM)) {
            advance();
            if (!check(Tok::IDENT)) throw_syntax("expected enum name");
            auto enum_name = advance().value;
            expect(Tok::DOUBLE_COLON, "expected '::' after enum name");
            skip_newlines();

            auto enum_map = Value::make_map();
            int idx = 0;
            while (!at_end() && !check(Tok::DOUBLE_SEMI)) {
                skip_newlines();
                if (check(Tok::DOUBLE_SEMI)) break;
                if (check(Tok::IDENT)) {
                    auto val_name = advance().value;
                    enum_map->map_val[val_name] = Value::make_num(idx++);
                } else {
                    advance();
                }
            }
            if (check(Tok::DOUBLE_SEMI)) advance();
            env_->set(enum_name, enum_map);
            return;
        }

        if (check(Tok::KW_CLASS)) {
            advance();
            if (!check(Tok::IDENT)) throw_syntax("expected class name after 'class'");
            auto class_name = advance().value;

            // Check for extends
            std::string parent_name;
            if (check(Tok::KW_EXTENDS)) {
                advance();
                if (!check(Tok::IDENT)) throw_syntax("expected parent class name after 'extends'");
                parent_name = advance().value;
            }

            expect(Tok::DOUBLE_COLON, "expected '::' after class name");
            skip_newlines();

            // Start with parent class members if extending
            auto class_map = Value::make_map();
            if (!parent_name.empty()) {
                auto parent = env_->get("__class_" + parent_name);
                if (!parent || parent->type != Value::MAP)
                    throw_name(parent_name);
                // Copy parent members
                for (auto& [k, v] : parent->map_val)
                    class_map->map_val[k] = v;
            }
            while (!at_end() && !check(Tok::DOUBLE_SEMI)) {
                skip_newlines();
                if (check(Tok::DOUBLE_SEMI)) break;
                // Method: name $params :: body ;;
                if (check(Tok::IDENT)) {
                    auto method_name = advance().value;
                    std::vector<std::string> params;
                    while (check(Tok::DOLLAR_IDENT))
                        params.push_back(advance().value);
                    expect(Tok::DOUBLE_COLON, "expected '::' in class method");
                    skip_newlines();
                    int body_start = (int)pos_;
                    skip_block();
                    int body_end = (int)pos_;
                    if (check(Tok::DOUBLE_SEMI)) advance();
                    class_map->map_val[method_name] = Value::make_func(method_name, params, body_start, body_end);
                } else if (check(Tok::DOLLAR_IDENT)) {
                    // Property: $name -> value
                    auto prop = advance().value;
                    expect(Tok::ARROW, "expected '->' for class property");
                    auto val = parse_expr();
                    class_map->map_val[prop] = val;
                    skip_newlines();
                } else {
                    advance();
                }
            }
            if (check(Tok::DOUBLE_SEMI)) advance();
            env_->set("__class_" + class_name, class_map);
            return;
        }

        // Destructuring: [$a, $b, $c] -> $list or [$a, $b] -> [1, 2]
        if (check(Tok::LBRACKET)) {
            size_t save = pos_;
            advance();
            std::vector<std::string> vars;
            bool is_destructure = true;
            while (!check(Tok::RBRACKET) && !at_end()) {
                if (check(Tok::DOLLAR_IDENT)) {
                    vars.push_back(advance().value);
                    if (check(Tok::COMMA)) advance();
                } else {
                    is_destructure = false;
                    break;
                }
            }
            if (is_destructure && check(Tok::RBRACKET)) {
                advance();
                if (check(Tok::ARROW)) {
                    advance();
                    auto val = parse_expr();
                    if (val->type == Value::LIST) {
                        for (size_t i = 0; i < vars.size(); i++) {
                            env_->set(vars[i], i < val->list.size() ? val->list[i] : Value::make_null());
                        }
                    }
                    return;
                }
            }
            pos_ = save; // not destructuring, backtrack
        }

        // pass — do nothing (placeholder)
        if (check(Tok::IDENT) && cur().value == "pass") {
            advance();
            return;
        }

        // del $var — delete variable from scope
        if (check(Tok::IDENT) && cur().value == "del") {
            advance();
            if (check(Tok::DOLLAR_IDENT)) {
                auto var = advance().value;
                env_->set(var, Value::make_null());
            }
            return;
        }

        // raise "message" — throw custom error
        if (check(Tok::IDENT) && cur().value == "raise") {
            advance();
            auto msg = parse_expr();
            throw hex_error::runtime(msg->as_str(), filename_, cur().line, cur().col);
        }

        // setenv "KEY" "VALUE" — set environment variable
        if (check(Tok::IDENT) && cur().value == "setenv") {
            advance();
            auto key = parse_expr()->as_str();
            auto val = parse_expr()->as_str();
            #ifdef _WIN32
            _putenv_s(key.c_str(), val.c_str());
            #else
            setenv(key.c_str(), val.c_str(), 1);
            #endif
            return;
        }

        // return / =>
        if (check(Tok::FAT_ARROW) || check(Tok::KW_RETURN)) {
            advance();
            auto val = parse_expr();
            throw ReturnSignal{val};
        }

        // break
        if (check(Tok::KW_BREAK)) {
            advance();
            throw BreakSignal{};
        }

        // continue
        if (check(Tok::KW_CONTINUE)) {
            advance();
            throw ContinueSignal{};
        }

        // ~write / ~append / ~del — file operations
        if (check(Tok::TILDE)) {
            advance();
            if (!check(Tok::IDENT)) error("expected file operation after ~");
            auto op = advance().value;

            if (op == "write") {
                auto path = parse_expr();
                auto content = parse_expr();
                std::ofstream f(path->as_str());
                if (f.is_open()) f << content->as_str();
                return;
            }
            if (op == "append") {
                auto path = parse_expr();
                auto content = parse_expr();
                std::ofstream f(path->as_str(), std::ios::app);
                if (f.is_open()) f << content->as_str() << "\n";
                return;
            }
            if (op == "del") {
                auto path = parse_expr();
                std::filesystem::remove(path->as_str());
                return;
            }
            // ~get "url" as statement (prints response)
            if (op == "get") {
                auto url = parse_expr();
                std::cout << http_request(url->as_str(), "GET") << std::endl;
                return;
            }
            // ~post "url" "body" as statement
            if (op == "post") {
                auto url = parse_expr();
                auto body = parse_expr();
                std::cout << http_request(url->as_str(), "POST", body->as_str(),
                                          "application/x-www-form-urlencoded") << std::endl;
                return;
            }
            // ~download "url" "file.txt"
            if (op == "download") {
                auto url = parse_expr();
                auto dest = parse_expr();
                std::string data = http_request(url->as_str(), "GET");
                if (data.empty()) {
                    throw hex_error::file_error(url->as_str(), "download", filename_, cur().line, cur().col);
                }
                std::ofstream f(dest->as_str(), std::ios::binary);
                if (!f.is_open()) {
                    throw hex_error::file_error(dest->as_str(), "write", filename_, cur().line, cur().col);
                }
                f.write(data.data(), data.size());
                f.close();
                return;
            }
            // ~lines "file" — read file as list of lines
            if (op == "lines") {
                auto path = parse_expr();
                std::ifstream f(path->as_str());
                if (!f.is_open()) throw hex_error::file_error(path->as_str(), "read", filename_, cur().line, cur().col);
                ValueList lines;
                std::string line;
                while (std::getline(f, line)) {
                    while (!line.empty() && line.back() == '\r') line.pop_back();
                    lines.push_back(Value::make_str(line));
                }
                // Store in next $var if assignment follows, otherwise just print
                // Actually this is a statement context, so we need a var
                // Let's make it work: ~lines "file" $var
                if (check(Tok::DOLLAR_IDENT)) {
                    auto var = advance().value;
                    env_->set(var, Value::make_list(lines));
                } else {
                    for (auto& l : lines) std::cout << l->as_str() << std::endl;
                }
                return;
            }
            // ~mkdir "path" — create directory (and parents)
            if (op == "mkdir") {
                auto path = parse_expr();
                std::filesystem::create_directories(path->as_str());
                return;
            }
            // ~rmdir "path" — remove directory
            if (op == "rmdir") {
                auto path = parse_expr();
                std::filesystem::remove_all(path->as_str());
                return;
            }
            // ~rename "old" "new" — rename or move file/folder
            if (op == "rename") {
                auto old_path = parse_expr();
                auto new_path = parse_expr();
                std::filesystem::rename(old_path->as_str(), new_path->as_str());
                return;
            }
            // ~copy "src" "dst" — copy file
            if (op == "copy") {
                auto src = parse_expr();
                auto dst = parse_expr();
                std::filesystem::copy_file(src->as_str(), dst->as_str(),
                    std::filesystem::copy_options::overwrite_existing);
                return;
            }
            // ~move "src" "dst" — move file (rename alias)
            if (op == "move") {
                auto src = parse_expr();
                auto dst = parse_expr();
                std::filesystem::rename(src->as_str(), dst->as_str());
                return;
            }
            // ~size "file" — print file size in bytes
            if (op == "size") {
                auto path = parse_expr();
                if (std::filesystem::exists(path->as_str())) {
                    auto sz = std::filesystem::file_size(path->as_str());
                    std::cout << sz << std::endl;
                } else {
                    std::cout << -1 << std::endl;
                }
                return;
            }
            // ~ls "dir" — list files in directory, stores in $var or prints
            if (op == "ls") {
                auto path = parse_expr();
                ValueList items;
                try {
                    for (auto& entry : std::filesystem::directory_iterator(path->as_str())) {
                        items.push_back(Value::make_str(entry.path().filename().string()));
                    }
                } catch (...) {}
                if (check(Tok::DOLLAR_IDENT)) {
                    auto var = advance().value;
                    env_->set(var, Value::make_list(items));
                } else {
                    for (auto& item : items) std::cout << item->as_str() << std::endl;
                }
                return;
            }
            // ~ext "file.txt" — get file extension
            if (op == "ext") {
                auto path = parse_expr();
                auto ext = std::filesystem::path(path->as_str()).extension().string();
                if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
                std::cout << ext << std::endl;
                return;
            }
            // ~name "path/to/file.txt" — get just the filename
            if (op == "name") {
                auto path = parse_expr();
                std::cout << std::filesystem::path(path->as_str()).filename().string() << std::endl;
                return;
            }
            // ~dir "path/to/file.txt" — get parent directory
            if (op == "dir") {
                auto path = parse_expr();
                std::cout << std::filesystem::path(path->as_str()).parent_path().string() << std::endl;
                return;
            }
            // ~cwd — print current working directory
            if (op == "cwd") {
                std::cout << std::filesystem::current_path().string() << std::endl;
                return;
            }
            // ~cd "path" — change working directory
            if (op == "cd") {
                auto path = parse_expr();
                std::filesystem::current_path(path->as_str());
                return;
            }
            // ~touch "file" — create empty file if it doesn't exist
            if (op == "touch") {
                auto path = parse_expr();
                if (!std::filesystem::exists(path->as_str())) {
                    std::ofstream f(path->as_str());
                }
                return;
            }
            // ~find "dir" "pattern" $var — find files matching pattern
            if (op == "find") {
                auto dir = parse_expr()->as_str();
                auto pattern = parse_expr()->as_str();
                ValueList found;
                try {
                    for (auto& entry : std::filesystem::recursive_directory_iterator(dir,
                            std::filesystem::directory_options::skip_permission_denied)) {
                        std::string fname = entry.path().filename().string();
                        // Skip hidden directories (starting with .)
                        if (entry.is_directory() && !fname.empty() && fname[0] == '.') continue;
                        if (!entry.is_regular_file()) continue;
                        // Simple wildcard: *.txt or exact match
                        if (pattern[0] == '*') {
                            std::string ext = pattern.substr(1);
                            if (fname.size() >= ext.size() &&
                                fname.substr(fname.size() - ext.size()) == ext) {
                                found.push_back(Value::make_str(entry.path().string()));
                            }
                        } else if (fname == pattern) {
                            found.push_back(Value::make_str(entry.path().string()));
                        }
                    }
                } catch (...) {}
                if (check(Tok::DOLLAR_IDENT)) {
                    env_->set(advance().value, Value::make_list(found));
                } else {
                    for (auto& f : found) std::cout << f->as_str() << std::endl;
                }
                return;
            }
            // ~replace "file" "old" "new" — replace string in file
            if (op == "replace") {
                auto path = parse_expr()->as_str();
                auto old_str = parse_expr()->as_str();
                auto new_str = parse_expr()->as_str();
                std::ifstream in(path);
                if (!in.is_open()) throw hex_error::file_error(path, "read", filename_, cur().line, cur().col);
                std::string content((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
                in.close();
                size_t p = 0;
                while ((p = content.find(old_str, p)) != std::string::npos) {
                    content.replace(p, old_str.length(), new_str);
                    p += new_str.length();
                }
                std::ofstream out(path);
                out << content;
                return;
            }
            error("unknown file op: ~" + op);
        }

        // ! shell command
        if (check(Tok::BANG)) {
            advance();
            auto cmd = parse_expr();
            system(cmd->as_str().c_str());
            return;
        }

        // &push, &pop, &rm, &len, &upper, &lower, etc.
        if (check(Tok::AMP)) {
            advance();
            if (!check(Tok::IDENT)) error("expected operation after &");
            auto op = advance().value;
            exec_amp_builtin(op);
            return;
        }

        // #wait, #exit, #clear, #swap, #try, #catch, #dump, #assert
        if (check(Tok::HASH)) {
            advance();
            // Accept IDENT or keywords like try/catch after #
            std::string name;
            if (check(Tok::IDENT))       name = advance().value;
            else if (check(Tok::KW_TRY)) name = advance().value;
            else if (check(Tok::KW_CATCH)) name = advance().value;
            else if (check(Tok::KW_BREAK)) name = advance().value;
            else error("expected builtin after #");
            exec_hash_statement(name);
            return;
        }

        // async funcname $params :: body ;; — define async function
        if (check(Tok::KW_ASYNC)) {
            advance();
            if (!check(Tok::IDENT)) throw_syntax("expected function name after 'async'");
            auto name = advance().value;
            std::vector<std::string> params;
            while (check(Tok::DOLLAR_IDENT))
                params.push_back(advance().value);
            expect(Tok::DOUBLE_COLON, "expected '::' in async function");
            skip_newlines();
            int body_start = (int)pos_;
            skip_block();
            int body_end = (int)pos_;
            if (check(Tok::DOUBLE_SEMI)) advance();
            // Mark as async by prefixing name
            auto func = Value::make_func(name, params, body_start, body_end);
            func->func_name = "__async_" + name;
            env_->set(name, func);
            return;
        }

        // await funcname args — call async function, run in thread, block until done
        // await $taskname — wait for a previously launched async task
        if (check(Tok::KW_AWAIT)) {
            advance();

            // Check if awaiting a task variable
            if (check(Tok::DOLLAR_IDENT)) {
                auto task_name = cur().value;
                std::lock_guard<std::mutex> lock(async_mutex_);
                auto it = async_tasks_.find(task_name);
                if (it != async_tasks_.end()) {
                    advance();
                    auto result = it->second.get();
                    env_->set(task_name, result);
                    async_tasks_.erase(it);
                    return;
                }
                // Not a task, fall through to function call
            }

            // await funcname args — call function in background thread
            if (check(Tok::IDENT)) {
                auto fname = advance().value;
                auto func = env_->get(fname);
                if (!func || func->type != Value::FUNC)
                    throw_name(fname);

                // Parse arguments
                std::vector<ValuePtr> args;
                for (size_t i = 0; i < func->params.size(); i++)
                    args.push_back(parse_expr());

                // Capture what we need for the thread
                auto tokens_copy = tokens_;
                auto global = global_env_;
                int bstart = func->body_start;
                int bend = func->body_end;
                auto param_names = func->params;

                // Launch in background thread
                auto future = std::async(std::launch::async, [=]() -> ValuePtr {
                    // Create isolated interpreter state for this thread
                    auto thread_env = std::make_shared<Environment>(global);
                    for (size_t i = 0; i < param_names.size(); i++)
                        thread_env->set_local(param_names[i],
                            i < args.size() ? args[i] : Value::make_null());

                    // Mini interpreter loop for the function body
                    Interpreter thread_interp;
                    thread_interp.tokens_ = tokens_copy;
                    thread_interp.pos_ = bstart;
                    thread_interp.env_ = thread_env;
                    thread_interp.global_env_ = global;
                    thread_interp.source_ = "";
                    thread_interp.filename_ = "<async>";

                    ValuePtr result = Value::make_null();
                    try {
                        while ((int)thread_interp.pos_ < bend && !thread_interp.at_end())
                            thread_interp.exec_statement();
                    } catch (ReturnSignal& ret) {
                        result = ret.value;
                    } catch (...) {}
                    return result;
                });

                // If there's a -> $var assignment after, store as named task
                // Otherwise block and wait
                if (check(Tok::ARROW)) {
                    advance();
                    // nope, the result of await IS the function call
                    // Let's just block and return
                }
                // Block until done and store result
                auto result = future.get();

                // Check if this was part of an assignment ($var -> await func)
                // The caller handles assignment, we just need to make this
                // work as an expression too. For now as statement, print nothing.
                // Store result in __await_result for the caller
                env_->set("__await_result", result);
                return;
            }
            throw_syntax("expected function name or $task after 'await'");
        }

        // Function definition: check for IDENT followed by params and ::
        // e.g. greet $name ::
        if (check(Tok::IDENT)) {
            auto name = cur().value;
            auto func = env_->get(name);

            // Check if this is a function definition (IDENT $params... ::)
            // Look ahead for pattern: IDENT ($DOLLAR_IDENT)* ::
            size_t save = pos_;
            advance();
            std::vector<std::string> params;
            bool is_funcdef = false;

            while (check(Tok::DOLLAR_IDENT)) {
                params.push_back(cur().value);
                advance();
            }

            if (check(Tok::DOUBLE_COLON) && !params.empty()) {
                // This is a function definition
                advance(); // ::
                skip_newlines();
                int body_start = (int)pos_;
                skip_block();
                int body_end = (int)pos_;
                if (check(Tok::DOUBLE_SEMI)) advance();

                env_->set(name, Value::make_func(name, params, body_start, body_end));
                return;
            }

            // Check if it's a no-arg function definition: name :: ... ;;
            if (check(Tok::DOUBLE_COLON) && params.empty()) {
                advance();
                skip_newlines();
                int body_start = (int)pos_;
                skip_block();
                int body_end = (int)pos_;
                if (check(Tok::DOUBLE_SEMI)) advance();

                env_->set(name, Value::make_func(name, {}, body_start, body_end));
                return;
            }

            // Backtrack — it's a function call
            pos_ = save;
            advance(); // eat name

            if (func && func->type == Value::FUNC) {
                call_function(func);
                return;
            }

            // Unknown identifier
            throw_name(name);
        }

        // Skip unknown tokens
        if (!at_end()) advance();
    }

    // ── If-body helpers: run/skip until ;;, ??, or :: else ─────────────

    // Check if :: at current pos is followed by 'else' (possibly with newlines between)
    bool is_else_marker() {
        if (!check(Tok::DOUBLE_COLON)) return false;
        size_t sv = pos_;
        advance();
        while (check(Tok::NEWLINE)) advance();
        bool result = check(Tok::KW_ELSE);
        pos_ = sv;
        return result;
    }

    // Check if ;; at depth 0 is a true block end
    // On single-line blocks: ;; is only a block end if next token is on a new line, EOF, or another ;;
    bool is_block_end_semi() {
        if (!check(Tok::DOUBLE_SEMI)) return false;
        int semi_line = cur().line;
        // Look at token AFTER the ;;
        size_t next = pos_ + 1;
        if (next >= tokens_.size()) return true; // EOF
        Token& nxt = tokens_[next];
        if (nxt.type == Tok::EOF_TOK) return true;
        if (nxt.type == Tok::NEWLINE) return true;
        if (nxt.type == Tok::DOUBLE_SEMI) return true;
        if (nxt.line != semi_line) return true;
        // Same line + more tokens = ;; is a separator, not block end
        return false;
    }

    void exec_if_body() {
        int depth = 0;
        while (!at_end()) {
            skip_newlines();
            if (at_end()) return;
            if (depth == 0) {
                if (is_block_end_semi()) return;
                if (check(Tok::DOUBLE_Q)) return;
                if (is_else_marker()) return;
            }
            // Track nested :: / ;; but NOT :: else ::
            if (check(Tok::DOUBLE_COLON) && !is_else_marker()) depth++;
            if (check(Tok::DOUBLE_SEMI)) {
                depth--;
                if (depth < 0) return;
                advance();
                continue;
            }
            exec_statement();
        }
    }

    void skip_if_body() {
        int depth = 0;
        while (!at_end()) {
            if (depth == 0) {
                if (is_block_end_semi()) return;
                if (check(Tok::DOUBLE_Q)) return;
                if (is_else_marker()) return;
            }
            if (check(Tok::DOUBLE_COLON) && !is_else_marker()) { depth++; advance(); continue; }
            if (check(Tok::DOUBLE_SEMI)) {
                depth--;
                advance();
                continue;
            }
            advance();
        }
    }

    // ── Execute block until ;; ──────────────────────────────────────────

    void exec_block() {
        int depth = 0;
        while (!at_end()) {
            skip_newlines();
            if (check(Tok::DOUBLE_SEMI) && depth == 0) {
                advance();
                return;
            }
            if (check(Tok::DOUBLE_Q) && depth == 0) return;
            if (check(Tok::DOUBLE_COLON) && peek(1).type == Tok::KW_ELSE && depth == 0) return;

            if (check(Tok::DOUBLE_COLON)) depth++;
            if (check(Tok::DOUBLE_SEMI)) { depth--; advance(); continue; }

            exec_statement();
        }
    }

    void exec_block_no_semi() {
        int depth = 0;
        while (!at_end()) {
            skip_newlines();
            if (check(Tok::DOUBLE_SEMI) && depth == 0) return;
            if (check(Tok::DOUBLE_COLON)) depth++;
            if (check(Tok::DOUBLE_SEMI)) { depth--; advance(); continue; }
            exec_statement();
        }
    }

    // ── Skip block (don't execute) ──────────────────────────────────────

    void skip_block() {
        int depth = 0;
        while (!at_end()) {
            if (check(Tok::DOUBLE_COLON)) { depth++; advance(); continue; }
            if (check(Tok::DOUBLE_SEMI)) {
                if (depth == 0) return;
                depth--;
                advance();
                continue;
            }
            if (depth == 0 && (check(Tok::DOUBLE_Q) ||
                (check(Tok::DOUBLE_COLON) && peek(1).type == Tok::KW_ELSE)))
                return;
            advance();
        }
    }

    void skip_else_branches() {
        skip_newlines();
        // Skip ?? and :: else :: branches
        while (!at_end()) {
            if (check(Tok::DOUBLE_Q)) {
                advance(); // ??
                // Skip condition
                while (!check(Tok::DOUBLE_COLON) && !at_end()) advance();
                if (check(Tok::DOUBLE_COLON)) advance();
                skip_block();
                continue;
            }
            if (check(Tok::DOUBLE_COLON) && peek(1).type == Tok::KW_ELSE) {
                advance(); advance(); // :: else
                if (check(Tok::DOUBLE_COLON)) advance();
                skip_block();
                continue;
            }
            break;
        }
        if (check(Tok::DOUBLE_SEMI)) advance();
    }

    // ── & builtins (list/string operations) ─────────────────────────────

    void exec_amp_builtin(const std::string& op) {
        if (op == "push") {
            if (!check(Tok::DOLLAR_IDENT)) error("&push expects $list");
            auto var = advance().value;
            auto val = parse_expr();
            auto list = env_->get(var);
            if (!list || list->type != Value::LIST) throw_type("$" + var + " is not a list");
            list->list.push_back(val);
            return;
        }
        if (op == "pop") {
            if (!check(Tok::DOLLAR_IDENT)) error("&pop expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            if (!list || list->type != Value::LIST || list->list.empty())
                throw_type("$" + var + " is not a list or is empty");
            list->list.pop_back();
            return;
        }
        if (op == "rm") {
            if (!check(Tok::DOLLAR_IDENT)) error("&rm expects $list");
            auto var = advance().value;
            auto idx = parse_expr();
            auto list = env_->get(var);
            if (!list || list->type != Value::LIST) throw_type("$" + var + " is not a list");
            int i = (int)idx->as_num();
            if (i >= 0 && i < (int)list->list.size())
                list->list.erase(list->list.begin() + i);
            return;
        }
        if (op == "len") {
            if (!check(Tok::DOLLAR_IDENT)) error("&len expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (!val) { std::cout << 0 << std::endl; return; }
            if (val->type == Value::LIST) std::cout << val->list.size() << std::endl;
            else if (val->type == Value::STR) std::cout << val->str.size() << std::endl;
            else if (val->type == Value::MAP) std::cout << val->map_val.size() << std::endl;
            else std::cout << 0 << std::endl;
            return;
        }
        if (op == "upper") {
            if (!check(Tok::DOLLAR_IDENT)) error("&upper expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                std::transform(val->str.begin(), val->str.end(), val->str.begin(), ::toupper);
            }
            return;
        }
        if (op == "lower") {
            if (!check(Tok::DOLLAR_IDENT)) error("&lower expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                std::transform(val->str.begin(), val->str.end(), val->str.begin(), ::tolower);
            }
            return;
        }
        if (op == "has") {
            if (!check(Tok::DOLLAR_IDENT)) error("&has expects $var");
            auto var = advance().value;
            auto needle = parse_expr();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                bool found = val->str.find(needle->as_str()) != std::string::npos;
                std::cout << (found ? "true" : "false") << std::endl;
            }
            return;
        }
        if (op == "replace") {
            if (!check(Tok::DOLLAR_IDENT)) error("&replace expects $var");
            auto var = advance().value;
            auto old_s = parse_expr()->as_str();
            auto new_s = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                size_t p = 0;
                while ((p = val->str.find(old_s, p)) != std::string::npos) {
                    val->str.replace(p, old_s.length(), new_s);
                    p += new_s.length();
                }
            }
            return;
        }
        if (op == "split") {
            if (!check(Tok::DOLLAR_IDENT)) error("&split expects $var");
            auto var = advance().value;
            auto delim = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                ValueList parts;
                std::string s = val->str;
                size_t p = 0;
                while ((p = s.find(delim)) != std::string::npos) {
                    parts.push_back(Value::make_str(s.substr(0, p)));
                    s = s.substr(p + delim.size());
                }
                parts.push_back(Value::make_str(s));
                env_->set(var, Value::make_list(parts));
            }
            return;
        }
        if (op == "sort") {
            if (!check(Tok::DOLLAR_IDENT)) error("&sort expects $list");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::LIST) {
                std::sort(val->list.begin(), val->list.end(),
                    [](const ValuePtr& a, const ValuePtr& b) {
                        return a->as_num() < b->as_num();
                    });
            }
            return;
        }
        if (op == "reverse") {
            if (!check(Tok::DOLLAR_IDENT)) error("&reverse expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::LIST)
                std::reverse(val->list.begin(), val->list.end());
            else if (val && val->type == Value::STR)
                std::reverse(val->str.begin(), val->str.end());
            return;
        }
        if (op == "join") {
            if (!check(Tok::DOLLAR_IDENT)) error("&join expects $list");
            auto var = advance().value;
            auto delim = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::LIST) {
                std::string result;
                for (size_t i = 0; i < val->list.size(); i++) {
                    if (i > 0) result += delim;
                    result += val->list[i]->as_str();
                }
                env_->set(var, Value::make_str(result));
            }
            return;
        }

        // ── String builtins ──────────────────────────────────────────────
        if (op == "trim") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&trim expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                size_t s = val->str.find_first_not_of(" \t\n\r");
                size_t e = val->str.find_last_not_of(" \t\n\r");
                if (s == std::string::npos) val->str = "";
                else val->str = val->str.substr(s, e - s + 1);
            }
            return;
        }
        if (op == "startswith") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&startswith expects $var");
            auto var = advance().value;
            auto prefix = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR)
                std::cout << (val->str.substr(0, prefix.size()) == prefix ? "true" : "false") << std::endl;
            else std::cout << "false" << std::endl;
            return;
        }
        if (op == "endswith") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&endswith expects $var");
            auto var = advance().value;
            auto suffix = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR && val->str.size() >= suffix.size())
                std::cout << (val->str.substr(val->str.size() - suffix.size()) == suffix ? "true" : "false") << std::endl;
            else std::cout << "false" << std::endl;
            return;
        }
        if (op == "repeat") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&repeat expects $var");
            auto var = advance().value;
            auto times = (int)parse_expr()->as_num();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                std::string orig = val->str;
                val->str = "";
                for (int i = 0; i < times; i++) val->str += orig;
            }
            return;
        }
        if (op == "index") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&index expects $var");
            auto var = advance().value;
            auto needle = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                size_t pos = val->str.find(needle);
                std::cout << (pos != std::string::npos ? (int)pos : -1) << std::endl;
            } else if (val && val->type == Value::LIST) {
                int found = -1;
                for (size_t i = 0; i < val->list.size(); i++) {
                    if (val->list[i]->as_str() == needle) { found = (int)i; break; }
                }
                std::cout << found << std::endl;
            } else std::cout << -1 << std::endl;
            return;
        }
        if (op == "count") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&count expects $var");
            auto var = advance().value;
            auto needle = parse_expr()->as_str();
            auto val = env_->get(var);
            int c = 0;
            if (val && val->type == Value::STR) {
                size_t pos = 0;
                while ((pos = val->str.find(needle, pos)) != std::string::npos) { c++; pos += needle.size(); }
            } else if (val && val->type == Value::LIST) {
                for (auto& item : val->list) if (item->as_str() == needle) c++;
            }
            std::cout << c << std::endl;
            return;
        }
        if (op == "chars") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&chars expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                ValueList chars;
                for (char ch : val->str) chars.push_back(Value::make_str(std::string(1, ch)));
                env_->set(var, Value::make_list(chars));
            }
            return;
        }
        if (op == "title") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&title expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                bool cap_next = true;
                for (auto& c : val->str) {
                    if (cap_next && isalpha(c)) { c = toupper(c); cap_next = false; }
                    else if (!isalpha(c)) cap_next = true;
                    else c = tolower(c);
                }
            }
            return;
        }
        if (op == "capitalize") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&capitalize expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR && !val->str.empty()) {
                std::transform(val->str.begin(), val->str.end(), val->str.begin(), ::tolower);
                val->str[0] = toupper(val->str[0]);
            }
            return;
        }
        if (op == "swapcase") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&swapcase expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                for (auto& c : val->str) {
                    if (isupper(c)) c = tolower(c);
                    else if (islower(c)) c = toupper(c);
                }
            }
            return;
        }
        if (op == "isdigit") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&isdigit expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            bool result = false;
            if (val && val->type == Value::STR && !val->str.empty()) {
                result = true;
                for (char c : val->str) if (!isdigit(c)) { result = false; break; }
            }
            std::cout << (result ? "true" : "false") << std::endl;
            return;
        }
        if (op == "isalpha") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&isalpha expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            bool result = false;
            if (val && val->type == Value::STR && !val->str.empty()) {
                result = true;
                for (char c : val->str) if (!isalpha(c)) { result = false; break; }
            }
            std::cout << (result ? "true" : "false") << std::endl;
            return;
        }

        // lstrip / rstrip
        if (op == "lstrip") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&lstrip expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                size_t s = val->str.find_first_not_of(" \t\n\r");
                val->str = (s == std::string::npos) ? "" : val->str.substr(s);
            }
            return;
        }
        if (op == "rstrip") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&rstrip expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                size_t e = val->str.find_last_not_of(" \t\n\r");
                val->str = (e == std::string::npos) ? "" : val->str.substr(0, e + 1);
            }
            return;
        }
        // zfill — pad number string with zeros
        if (op == "zfill") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&zfill expects $var");
            auto var = advance().value;
            auto width = (int)parse_expr()->as_num();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                while ((int)val->str.size() < width) val->str = "0" + val->str;
            }
            return;
        }
        // partition — split into [before, sep, after]
        if (op == "partition") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&partition expects $var");
            auto var = advance().value;
            auto sep = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                size_t p = val->str.find(sep);
                ValueList parts;
                if (p != std::string::npos) {
                    parts.push_back(Value::make_str(val->str.substr(0, p)));
                    parts.push_back(Value::make_str(sep));
                    parts.push_back(Value::make_str(val->str.substr(p + sep.size())));
                } else {
                    parts.push_back(Value::make_str(val->str));
                    parts.push_back(Value::make_str(""));
                    parts.push_back(Value::make_str(""));
                }
                env_->set(var, Value::make_list(parts));
            }
            return;
        }
        // ljust / rjust — left/right justify
        if (op == "ljust") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&ljust expects $var");
            auto var = advance().value;
            auto width = (int)parse_expr()->as_num();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                while ((int)val->str.size() < width) val->str += " ";
            }
            return;
        }
        if (op == "rjust") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&rjust expects $var");
            auto var = advance().value;
            auto width = (int)parse_expr()->as_num();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                while ((int)val->str.size() < width) val->str = " " + val->str;
            }
            return;
        }

        // splitlines
        if (op == "splitlines") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&splitlines expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                ValueList lines;
                std::istringstream ss(val->str);
                std::string line;
                while (std::getline(ss, line)) {
                    while (!line.empty() && line.back() == '\r') line.pop_back();
                    lines.push_back(Value::make_str(line));
                }
                env_->set(var, Value::make_list(lines));
            }
            return;
        }
        // removeprefix / removesuffix
        if (op == "removeprefix") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&removeprefix expects $var");
            auto var = advance().value;
            auto prefix = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR && val->str.substr(0, prefix.size()) == prefix)
                val->str = val->str.substr(prefix.size());
            return;
        }
        if (op == "removesuffix") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&removesuffix expects $var");
            auto var = advance().value;
            auto suffix = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR && val->str.size() >= suffix.size() &&
                val->str.substr(val->str.size() - suffix.size()) == suffix)
                val->str = val->str.substr(0, val->str.size() - suffix.size());
            return;
        }
        // rfind — reverse find
        if (op == "rfind") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&rfind expects $var");
            auto var = advance().value;
            auto needle = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::STR) {
                size_t p = val->str.rfind(needle);
                std::cout << (p != std::string::npos ? (int)p : -1) << std::endl;
            } else std::cout << -1 << std::endl;
            return;
        }

        // ── List/Map shared builtins ────────────────────────────────────
        // clear — empty a list or map
        if (op == "clear") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&clear expects $var");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::LIST) val->list.clear();
            else if (val && val->type == Value::MAP) val->map_val.clear();
            else if (val && val->type == Value::STR) val->str.clear();
            return;
        }
        // copy — shallow copy
        if (op == "copy") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&copy expects $src");
            auto src = advance().value;
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&copy expects $dst");
            auto dst = advance().value;
            auto val = env_->get(src);
            if (val && val->type == Value::LIST) env_->set(dst, Value::make_list(val->list));
            else if (val && val->type == Value::MAP) env_->set(dst, Value::make_map(val->map_val));
            else if (val) env_->set(dst, val);
            return;
        }
        // extend — append all items from one list to another
        if (op == "extend") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&extend expects $dst");
            auto dst_var = advance().value;
            auto src = parse_expr();
            auto dst = env_->get(dst_var);
            if (dst && dst->type == Value::LIST && src->type == Value::LIST)
                dst->list.insert(dst->list.end(), src->list.begin(), src->list.end());
            return;
        }
        // remove — remove first occurrence by VALUE (not index)
        if (op == "remove") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&remove expects $list");
            auto var = advance().value;
            auto target = parse_expr();
            auto list = env_->get(var);
            if (list && list->type == Value::LIST) {
                for (auto it = list->list.begin(); it != list->list.end(); ++it) {
                    if ((*it)->as_str() == target->as_str()) { list->list.erase(it); break; }
                }
            }
            return;
        }

        // ── Map builtins ────────────────────────────────────────────────
        // get — safe access with default value
        if (op == "get") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&get expects $map");
            auto var = advance().value;
            auto key = parse_expr()->as_str();
            auto def = parse_expr();
            auto map = env_->get(var);
            if (map && map->type == Value::MAP) {
                auto it = map->map_val.find(key);
                std::cout << (it != map->map_val.end() ? it->second->as_str() : def->as_str()) << std::endl;
            } else std::cout << def->as_str() << std::endl;
            return;
        }
        // items — get list of [key, value] pairs
        if (op == "items") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&items expects $map");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::MAP) {
                ValueList items;
                for (auto& [k, v] : val->map_val)
                    items.push_back(Value::make_list({Value::make_str(k), v}));
                env_->set(var, Value::make_list(items));
            }
            return;
        }
        // fromkeys — create map from list of keys with default value
        if (op == "fromkeys") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&fromkeys expects $var");
            auto var = advance().value;
            auto keys = parse_expr();
            auto def = parse_expr();
            if (keys->type == Value::LIST) {
                ValueMap m;
                for (auto& k : keys->list) m[k->as_str()] = def;
                env_->set(var, Value::make_map(m));
            }
            return;
        }
        // setdefault — get value, set default if missing
        if (op == "setdefault") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&setdefault expects $map");
            auto var = advance().value;
            auto key = parse_expr()->as_str();
            auto def = parse_expr();
            auto map = env_->get(var);
            if (map && map->type == Value::MAP) {
                auto it = map->map_val.find(key);
                if (it == map->map_val.end()) map->map_val[key] = def;
            }
            return;
        }
        // update — merge another map (like Python dict.update)
        if (op == "update") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&update expects $map");
            auto var = advance().value;
            auto src = parse_expr();
            auto map = env_->get(var);
            if (map && map->type == Value::MAP && src->type == Value::MAP)
                for (auto& [k, v] : src->map_val) map->map_val[k] = v;
            return;
        }

        // ── List builtins ───────────────────────────────────────────────
        if (op == "insert") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&insert expects $list");
            auto var = advance().value;
            auto idx = (int)parse_expr()->as_num();
            auto val_to_insert = parse_expr();
            auto list = env_->get(var);
            if (!list || list->type != Value::LIST) throw_type("$" + var + " is not a list");
            if (idx < 0) idx += (int)list->list.size();
            if (idx < 0) idx = 0;
            if (idx > (int)list->list.size()) idx = (int)list->list.size();
            list->list.insert(list->list.begin() + idx, val_to_insert);
            return;
        }
        if (op == "find") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&find expects $list");
            auto var = advance().value;
            auto needle = parse_expr();
            auto list = env_->get(var);
            int found = -1;
            if (list && list->type == Value::LIST) {
                for (size_t i = 0; i < list->list.size(); i++) {
                    if (list->list[i]->as_str() == needle->as_str()) { found = (int)i; break; }
                }
            }
            std::cout << found << std::endl;
            return;
        }
        if (op == "sum") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&sum expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            double total = 0;
            if (list && list->type == Value::LIST) {
                for (auto& item : list->list) total += item->as_num();
            }
            std::cout << total << std::endl;
            return;
        }
        if (op == "min") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&min expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            if (list && list->type == Value::LIST && !list->list.empty()) {
                double m = list->list[0]->as_num();
                for (auto& item : list->list) if (item->as_num() < m) m = item->as_num();
                std::cout << m << std::endl;
            }
            return;
        }
        if (op == "max") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&max expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            if (list && list->type == Value::LIST && !list->list.empty()) {
                double m = list->list[0]->as_num();
                for (auto& item : list->list) if (item->as_num() > m) m = item->as_num();
                std::cout << m << std::endl;
            }
            return;
        }
        if (op == "unique") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&unique expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            if (list && list->type == Value::LIST) {
                ValueList result;
                std::set<std::string> seen;
                for (auto& item : list->list) {
                    std::string key = item->as_str();
                    if (seen.find(key) == seen.end()) {
                        seen.insert(key);
                        result.push_back(item);
                    }
                }
                list->list = result;
            }
            return;
        }
        if (op == "shuffle") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&shuffle expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            if (list && list->type == Value::LIST) {
                for (int i = (int)list->list.size() - 1; i > 0; i--) {
                    int j = rand() % (i + 1);
                    std::swap(list->list[i], list->list[j]);
                }
            }
            return;
        }
        if (op == "contains") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&contains expects $list");
            auto var = advance().value;
            auto needle = parse_expr();
            auto list = env_->get(var);
            bool found = false;
            if (list && list->type == Value::LIST) {
                for (auto& item : list->list) {
                    if (item->as_str() == needle->as_str()) { found = true; break; }
                }
            }
            std::cout << (found ? "true" : "false") << std::endl;
            return;
        }
        if (op == "flat") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&flat expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            if (list && list->type == Value::LIST) {
                ValueList result;
                for (auto& item : list->list) {
                    if (item->type == Value::LIST) {
                        for (auto& sub : item->list) result.push_back(sub);
                    } else {
                        result.push_back(item);
                    }
                }
                list->list = result;
            }
            return;
        }
        if (op == "fill") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&fill expects $var");
            auto var = advance().value;
            auto fill_val = parse_expr();
            auto count = (int)parse_expr()->as_num();
            ValueList result;
            for (int i = 0; i < count; i++) {
                if (fill_val->type == Value::NUM) result.push_back(Value::make_num(fill_val->num));
                else if (fill_val->type == Value::STR) result.push_back(Value::make_str(fill_val->str));
                else result.push_back(Value::make_null());
            }
            env_->set(var, Value::make_list(result));
            return;
        }

        // ── Map builtins ────────────────────────────────────────────────
        if (op == "keys") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&keys expects $map");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::MAP) {
                ValueList keys;
                for (auto& [k, v] : val->map_val) keys.push_back(Value::make_str(k));
                env_->set(var, Value::make_list(keys));
            }
            return;
        }
        if (op == "vals") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&vals expects $map");
            auto var = advance().value;
            auto val = env_->get(var);
            if (val && val->type == Value::MAP) {
                ValueList vals;
                for (auto& [k, v] : val->map_val) vals.push_back(v);
                env_->set(var, Value::make_list(vals));
            }
            return;
        }
        if (op == "del") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&del expects $map");
            auto var = advance().value;
            auto key = parse_expr()->as_str();
            auto val = env_->get(var);
            if (val && val->type == Value::MAP) val->map_val.erase(key);
            return;
        }
        if (op == "merge") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&merge expects $map1");
            auto var1 = advance().value;
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&merge expects $map2");
            auto var2 = advance().value;
            auto m1 = env_->get(var1);
            auto m2 = env_->get(var2);
            if (m1 && m1->type == Value::MAP && m2 && m2->type == Value::MAP) {
                for (auto& [k, v] : m2->map_val) m1->map_val[k] = v;
            }
            return;
        }
        if (op == "haskey") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&haskey expects $map");
            auto var = advance().value;
            auto key = parse_expr()->as_str();
            auto val = env_->get(var);
            bool found = false;
            if (val && val->type == Value::MAP) found = val->map_val.count(key) > 0;
            std::cout << (found ? "true" : "false") << std::endl;
            return;
        }

        // &map $list funcname — apply function to each element, store result
        if (op == "map") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&map expects $list");
            auto var = advance().value;
            if (!check(Tok::IDENT)) throw_syntax("&map expects function name");
            auto fname = advance().value;
            auto list = env_->get(var);
            auto func = env_->get(fname);
            if (!list || list->type != Value::LIST) throw_type("$" + var + " is not a list");
            if (!func || func->type != Value::FUNC) throw_name(fname);
            ValueList result;
            for (auto& item : list->list) {
                auto fenv = std::make_shared<Environment>(global_env_);
                if (!func->params.empty()) fenv->set_local(func->params[0], item);
                auto old_env = env_; auto old_pos = pos_;
                env_ = fenv; pos_ = func->body_start;
                ValuePtr ret = Value::make_null();
                try { while ((int)pos_ < func->body_end && !at_end()) exec_statement(); }
                catch (ReturnSignal& r) { ret = r.value; }
                env_ = old_env; pos_ = old_pos;
                result.push_back(ret);
            }
            list->list = result;
            return;
        }
        // &filter $list funcname — keep elements where func returns true
        if (op == "filter") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&filter expects $list");
            auto var = advance().value;
            if (!check(Tok::IDENT)) throw_syntax("&filter expects function name");
            auto fname = advance().value;
            auto list = env_->get(var);
            auto func = env_->get(fname);
            if (!list || list->type != Value::LIST) throw_type("$" + var + " is not a list");
            if (!func || func->type != Value::FUNC) throw_name(fname);
            ValueList result;
            for (auto& item : list->list) {
                auto fenv = std::make_shared<Environment>(global_env_);
                if (!func->params.empty()) fenv->set_local(func->params[0], item);
                auto old_env = env_; auto old_pos = pos_;
                env_ = fenv; pos_ = func->body_start;
                ValuePtr ret = Value::make_bool(false);
                try { while ((int)pos_ < func->body_end && !at_end()) exec_statement(); }
                catch (ReturnSignal& r) { ret = r.value; }
                env_ = old_env; pos_ = old_pos;
                if (ret->truthy()) result.push_back(item);
            }
            list->list = result;
            return;
        }
        // &reduce $list funcname — reduce list to single value using func($acc, $item)
        if (op == "reduce") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&reduce expects $list");
            auto var = advance().value;
            if (!check(Tok::IDENT)) throw_syntax("&reduce expects function name");
            auto fname = advance().value;
            auto list = env_->get(var);
            auto func = env_->get(fname);
            if (!list || list->type != Value::LIST || list->list.empty()) throw_type("$" + var + " is not a non-empty list");
            if (!func || func->type != Value::FUNC) throw_name(fname);
            ValuePtr acc = list->list[0];
            for (size_t i = 1; i < list->list.size(); i++) {
                auto fenv = std::make_shared<Environment>(global_env_);
                if (func->params.size() >= 1) fenv->set_local(func->params[0], acc);
                if (func->params.size() >= 2) fenv->set_local(func->params[1], list->list[i]);
                auto old_env = env_; auto old_pos = pos_;
                env_ = fenv; pos_ = func->body_start;
                try { while ((int)pos_ < func->body_end && !at_end()) exec_statement(); }
                catch (ReturnSignal& r) { acc = r.value; }
                env_ = old_env; pos_ = old_pos;
            }
            env_->set(var, acc);
            return;
        }
        // &any $list — true if any element is truthy
        if (op == "any") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&any expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            bool result = false;
            if (list && list->type == Value::LIST) {
                for (auto& item : list->list) if (item->truthy()) { result = true; break; }
            }
            std::cout << (result ? "true" : "false") << std::endl;
            return;
        }
        // &all $list — true if all elements are truthy
        if (op == "all") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&all expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            bool result = true;
            if (list && list->type == Value::LIST) {
                for (auto& item : list->list) if (!item->truthy()) { result = false; break; }
            }
            std::cout << (result ? "true" : "false") << std::endl;
            return;
        }
        // &zip $list1 $list2 — combine two lists into list of pairs
        if (op == "zip") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&zip expects $list1");
            auto var1 = advance().value;
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&zip expects $list2");
            auto var2 = advance().value;
            auto l1 = env_->get(var1);
            auto l2 = env_->get(var2);
            if (!l1 || l1->type != Value::LIST || !l2 || l2->type != Value::LIST)
                throw_type("&zip expects two lists");
            ValueList result;
            size_t len = l1->list.size() < l2->list.size() ? l1->list.size() : l2->list.size();
            for (size_t i = 0; i < len; i++)
                result.push_back(Value::make_list({l1->list[i], l2->list[i]}));
            env_->set(var1, Value::make_list(result));
            return;
        }
        // &enumerate $list — turn into [[0,item],[1,item],...]
        if (op == "enumerate") {
            if (!check(Tok::DOLLAR_IDENT)) throw_syntax("&enumerate expects $list");
            auto var = advance().value;
            auto list = env_->get(var);
            if (!list || list->type != Value::LIST) throw_type("$" + var + " is not a list");
            ValueList result;
            for (size_t i = 0; i < list->list.size(); i++)
                result.push_back(Value::make_list({Value::make_num((double)i), list->list[i]}));
            list->list = result;
            return;
        }

        throw_syntax("unknown & operation: &" + op);
    }

    // ── # statement builtins ────────────────────────────────────────────

    void exec_hash_statement(const std::string& name) {
        if (name == "wait") {
            auto ms = parse_expr();
            std::this_thread::sleep_for(std::chrono::milliseconds((int)ms->as_num()));
            return;
        }
        if (name == "exit") {
            int code = 0;
            if (!check(Tok::NEWLINE) && !check(Tok::EOF_TOK) && !check(Tok::DOUBLE_SEMI)) {
                code = (int)parse_expr()->as_num();
            }
            std::exit(code);
        }
        if (name == "clear") {
            #ifdef _WIN32
            system("cls");
            #else
            system("clear");
            #endif
            return;
        }
        if (name == "swap") {
            if (!check(Tok::DOLLAR_IDENT)) error("#swap expects $var1 $var2");
            auto a = advance().value;
            if (!check(Tok::DOLLAR_IDENT)) error("#swap expects $var1 $var2");
            auto b = advance().value;
            auto va = env_->get(a);
            auto vb = env_->get(b);
            env_->set(a, vb ? vb : Value::make_null());
            env_->set(b, va ? va : Value::make_null());
            return;
        }
        if (name == "try") {
            expect(Tok::DOUBLE_COLON, "expected '::' after #try");
            skip_newlines();

            bool caught = false;
            std::string caught_type;
            std::string caught_msg;

            try {
                exec_block_no_semi();
            } catch (HexError& e) {
                caught = true;
                caught_type = e.type_name();
                caught_msg = e.message;
            } catch (std::runtime_error& e) {
                caught = true;
                caught_type = "RuntimeError";
                caught_msg = e.what();
            }

            // Skip remaining try body if error occurred midway
            if (caught) {
                int depth = 0;
                while (!at_end()) {
                    if (check(Tok::DOUBLE_COLON)) { depth++; advance(); continue; }
                    if (check(Tok::DOUBLE_SEMI)) {
                        if (depth == 0) break;
                        depth--; advance(); continue;
                    }
                    if (depth == 0 && check(Tok::HASH)) {
                        size_t sv = pos_;
                        advance();
                        if (check(Tok::KW_CATCH) || (check(Tok::IDENT) && cur().value == "catch") ||
                            (check(Tok::IDENT) && cur().value == "finally")) {
                            pos_ = sv; break;
                        }
                    }
                    advance();
                }
            }

            // Set error info variables for catch block
            if (caught) {
                env_->set("__error_type", Value::make_str(caught_type));
                env_->set("__error_msg", Value::make_str(caught_msg));
            }

            // Find #catch — optionally with type: #catch TypeError ::
            skip_newlines();
            bool catch_executed = false;
            if (check(Tok::HASH)) {
                size_t hash_pos = pos_;
                advance();
                if (check(Tok::KW_CATCH) || (check(Tok::IDENT) && cur().value == "catch")) {
                    advance();
                    // Check for specific error type
                    std::string catch_filter;
                    if (check(Tok::IDENT)) {
                        catch_filter = advance().value;
                    }
                    expect(Tok::DOUBLE_COLON, "expected '::' after #catch");
                    skip_newlines();

                    bool should_run = caught;
                    if (should_run && !catch_filter.empty()) {
                        should_run = (caught_type == catch_filter);
                    }

                    if (should_run) {
                        exec_if_body();
                        catch_executed = true;
                    } else {
                        skip_if_body();
                    }
                    if (check(Tok::DOUBLE_SEMI)) advance();
                } else {
                    pos_ = hash_pos;
                }
            }

            // Find #finally — always runs
            skip_newlines();
            if (check(Tok::HASH)) {
                size_t hash_pos = pos_;
                advance();
                if (check(Tok::IDENT) && cur().value == "finally") {
                    advance();
                    expect(Tok::DOUBLE_COLON, "expected '::' after #finally");
                    skip_newlines();
                    exec_if_body();
                    if (check(Tok::DOUBLE_SEMI)) advance();
                } else {
                    pos_ = hash_pos;
                }
            }

            return;
        }
        if (name == "catch") {
            // Standalone #catch without #try — skip its block
            // May have type filter
            if (check(Tok::IDENT)) advance(); // skip type filter
            expect(Tok::DOUBLE_COLON, "expected '::' after #catch");
            skip_if_body();
            if (check(Tok::DOUBLE_SEMI)) advance();
            return;
        }
        if (name == "finally") {
            // Standalone #finally without #try — still execute it
            expect(Tok::DOUBLE_COLON, "expected '::' after #finally");
            skip_newlines();
            exec_if_body();
            if (check(Tok::DOUBLE_SEMI)) advance();
            return;
        }

        // #eval — execute HEX code from string (statement context)
        if (name == "eval") {
            auto code = parse_expr()->as_str();
            auto old_tokens = tokens_;
            auto old_pos = pos_;
            auto old_source = source_;
            auto old_file = filename_;
            Lexer lex(code);
            tokens_ = lex.tokenize();
            pos_ = 0;
            source_ = code;
            filename_ = "<eval>";
            while (!at_end()) {
                try { exec_statement(); }
                catch (HexError& e) { ErrorFormatter::print(e, source_); break; }
            }
            tokens_ = old_tokens;
            pos_ = old_pos;
            source_ = old_source;
            filename_ = old_file;
            return;
        }

        // #dump — debug print with type info
        if (name == "dump") {
            auto val = parse_expr();
            const char* tnames[] = {"number", "string", "bool", "list", "map", "function", "null"};
            std::cout << "[dump] type=" << tnames[val->type] << " value=" << val->as_str() << std::endl;
            return;
        }

        // #assert — crash if condition is false
        if (name == "assert") {
            auto cond = parse_expr();
            std::string msg = "assertion failed";
            if (check(Tok::STRING)) msg = advance().value;
            if (!cond->truthy()) {
                throw hex_error::runtime(msg, filename_, cur().line, cur().col);
            }
            return;
        }

        throw_syntax("unknown builtin: #" + name);
    }

public:
    Interpreter() {
        srand((unsigned)time(NULL));
        env_ = std::make_shared<Environment>();
        global_env_ = env_;
        // Enable ANSI colors on Windows 10+ (only for real console)
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(hOut, &mode)) {
                SetConsoleMode(hOut, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
            }
        }
    }

    void set_argv(int argc, char* argv[]) {
        ValueList args;
        for (int i = 0; i < argc; i++)
            args.push_back(Value::make_str(argv[i]));
        env_->set("__argv", Value::make_list(args));
    }

    void run(const std::string& source, const std::string& filename = "<stdin>") {
        source_ = source;
        filename_ = filename;

        Lexer lexer(source);
        tokens_ = lexer.tokenize();
        pos_ = 0;

        while (!at_end()) {
            try {
                exec_statement();
            } catch (ReturnSignal&) {
                break;
            } catch (BreakSignal&) {
                auto err = hex_error::syntax("'break' outside loop", filename_, cur().line, cur().col);
                ErrorFormatter::print(err, source_);
                break;
            } catch (ContinueSignal&) {
                auto err = hex_error::syntax("'continue' outside loop", filename_, cur().line, cur().col);
                ErrorFormatter::print(err, source_);
                break;
            } catch (HexError& e) {
                ErrorFormatter::print(e, source_);
                while (!at_end() && !check(Tok::NEWLINE)) advance();
            } catch (std::runtime_error& e) {
                // Fallback for any untyped errors
                std::cerr << "\n  RuntimeError: " << e.what() << std::endl;
                while (!at_end() && !check(Tok::NEWLINE)) advance();
            }
        }
    }

    void repl() {
        std::cout << R"(
  ██╗  ██╗███████╗██╗  ██╗
  ██║  ██║██╔════╝╚██╗██╔╝
  ███████║█████╗   ╚███╔╝
  ██╔══██║██╔══╝   ██╔██╗
  ██║  ██║███████╗██╔╝ ██╗
  ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝
  v1.0 — type #exit to quit
)" << std::endl;

        std::string line;
        std::string buffer;

        while (true) {
            if (buffer.empty())
                std::cout << "hex> ";
            else
                std::cout << " ... ";

            if (!std::getline(std::cin, line)) break;

            buffer += line + "\n";

            // Count :: and ;; to detect incomplete blocks
            int open = 0;
            for (size_t i = 0; i + 1 < buffer.size(); i++) {
                if (buffer[i] == ':' && buffer[i + 1] == ':') open++;
                if (buffer[i] == ';' && buffer[i + 1] == ';') open--;
            }

            if (open <= 0) {
                run(buffer);
                buffer.clear();
            }
        }
    }
};
