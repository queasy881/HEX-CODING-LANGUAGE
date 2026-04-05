#pragma once
// HEX Language — Lexer / Tokenizer
#include "token.h"
#include <string>
#include <vector>
#include <cctype>

class Lexer {
    std::string src_;
    size_t pos_;
    int line_, col_;

    char peek(int off = 0) {
        size_t p = pos_ + off;
        return p < src_.size() ? src_[p] : '\0';
    }
    char eat() {
        char c = peek();
        pos_++;
        if (c == '\n') { line_++; col_ = 1; }
        else col_++;
        return c;
    }
    bool match(char c) {
        if (peek() == c) { eat(); return true; }
        return false;
    }

    void skip_whitespace() {
        while (pos_ < src_.size() && (peek() == ' ' || peek() == '\t' || peek() == '\r'))
            eat();
    }

    Token make(Tok t, const std::string& v = "") {
        return {t, v, line_, col_};
    }

public:
    Lexer(const std::string& src) : src_(src), pos_(0), line_(1), col_(1) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;

        while (pos_ < src_.size()) {
            skip_whitespace();
            if (pos_ >= src_.size()) break;

            char c = peek();
            int ln = line_, cl = col_;

            // Comments: ## ...
            if (c == '#' && peek(1) == '#') {
                while (pos_ < src_.size() && peek() != '\n') eat();
                continue;
            }

            // Newline
            if (c == '\n') {
                eat();
                if (!tokens.empty() && tokens.back().type != Tok::NEWLINE)
                    tokens.push_back({Tok::NEWLINE, "\\n", ln, cl});
                continue;
            }

            // Numbers
            if (isdigit(c) || (c == '.' && isdigit(peek(1)))) {
                std::string num;
                bool has_dot = false;
                while (pos_ < src_.size() && (isdigit(peek()) || peek() == '.')) {
                    if (peek() == '.') {
                        if (has_dot) break;
                        if (peek(1) == '.') break;
                        has_dot = true;
                    }
                    num += eat();
                }
                tokens.push_back({Tok::NUMBER, num, ln, cl});
                continue;
            }

            // Strings — including triple-quote multi-line """..."""
            if (c == '"' || c == '\'') {
                // Check for triple quote
                if (c == '"' && peek(1) == '"' && peek(2) == '"') {
                    eat(); eat(); eat(); // consume """
                    std::string str;
                    while (pos_ < src_.size()) {
                        if (peek() == '"' && peek(1) == '"' && peek(2) == '"') {
                            eat(); eat(); eat(); // consume closing """
                            break;
                        }
                        str += eat();
                    }
                    tokens.push_back({Tok::STRING, str, ln, cl});
                    continue;
                }

                char quote = eat();
                std::string str;
                while (pos_ < src_.size() && peek() != quote) {
                    if (peek() == '\\') {
                        eat();
                        char esc = eat();
                        switch (esc) {
                            case 'n': str += '\n'; break;
                            case 't': str += '\t'; break;
                            case '\\': str += '\\'; break;
                            case '"': str += '"'; break;
                            case '\'': str += '\''; break;
                            default: str += esc; break;
                        }
                    } else {
                        str += eat();
                    }
                }
                if (pos_ < src_.size()) eat();
                tokens.push_back({Tok::STRING, str, ln, cl});
                continue;
            }

            // Identifiers and keywords
            if (isalpha(c) || c == '_') {
                std::string id;
                while (pos_ < src_.size() && (isalnum(peek()) || peek() == '_'))
                    id += eat();

                if (id == "true")          tokens.push_back({Tok::TRUE_LIT, id, ln, cl});
                else if (id == "false")    tokens.push_back({Tok::FALSE_LIT, id, ln, cl});
                else if (id == "null")     tokens.push_back({Tok::NULL_LIT, id, ln, cl});
                else if (id == "if")       tokens.push_back({Tok::KW_IF, id, ln, cl});
                else if (id == "else")     tokens.push_back({Tok::KW_ELSE, id, ln, cl});
                else if (id == "while")    tokens.push_back({Tok::KW_WHILE, id, ln, cl});
                else if (id == "for")      tokens.push_back({Tok::KW_FOR, id, ln, cl});
                else if (id == "in")       tokens.push_back({Tok::KW_IN, id, ln, cl});
                else if (id == "rep")      tokens.push_back({Tok::KW_REP, id, ln, cl});
                else if (id == "return")   tokens.push_back({Tok::KW_RETURN, id, ln, cl});
                else if (id == "break")    tokens.push_back({Tok::KW_BREAK, id, ln, cl});
                else if (id == "continue") tokens.push_back({Tok::KW_CONTINUE, id, ln, cl});
                else if (id == "try")      tokens.push_back({Tok::KW_TRY, id, ln, cl});
                else if (id == "catch")    tokens.push_back({Tok::KW_CATCH, id, ln, cl});
                else if (id == "match")    tokens.push_back({Tok::KW_MATCH, id, ln, cl});
                else if (id == "case")     tokens.push_back({Tok::KW_CASE, id, ln, cl});
                else if (id == "default")  tokens.push_back({Tok::KW_DEFAULT, id, ln, cl});
                else if (id == "forever")  tokens.push_back({Tok::KW_FOREVER, id, ln, cl});
                else if (id == "do")       tokens.push_back({Tok::KW_DO, id, ln, cl});
                else if (id == "lock")     tokens.push_back({Tok::KW_LOCK, id, ln, cl});
                else if (id == "import")   tokens.push_back({Tok::KW_IMPORT, id, ln, cl});
                else if (id == "class")    tokens.push_back({Tok::KW_CLASS, id, ln, cl});
                else if (id == "new")      tokens.push_back({Tok::KW_NEW, id, ln, cl});
                else if (id == "this")     tokens.push_back({Tok::KW_THIS, id, ln, cl});
                else if (id == "async")    tokens.push_back({Tok::KW_ASYNC, id, ln, cl});
                else if (id == "await")    tokens.push_back({Tok::KW_AWAIT, id, ln, cl});
                else if (id == "enum")     tokens.push_back({Tok::KW_ENUM, id, ln, cl});
                else if (id == "extends")  tokens.push_back({Tok::KW_EXTENDS, id, ln, cl});
                else                       tokens.push_back({Tok::IDENT, id, ln, cl});
                continue;
            }

            // $identifier
            if (c == '$') {
                eat();
                std::string id;
                while (pos_ < src_.size() && (isalnum(peek()) || peek() == '_'))
                    id += eat();
                if (id.empty())
                    tokens.push_back({Tok::DOLLAR, "$", ln, cl});
                else
                    tokens.push_back({Tok::DOLLAR_IDENT, id, ln, cl});
                continue;
            }

            // Operators and punctuation
            eat();
            switch (c) {
                case '+':
                    if (match('=')) tokens.push_back(make(Tok::PLUS_EQ, "+="));
                    else tokens.push_back(make(Tok::PLUS, "+"));
                    break;
                case '*':
                    if (match('=')) tokens.push_back(make(Tok::STAR_EQ, "*="));
                    else tokens.push_back(make(Tok::STAR, "*"));
                    break;
                case '^': tokens.push_back(make(Tok::CARET, "^")); break;
                case '%': tokens.push_back(make(Tok::PERCENT, "%")); break;
                case '(': tokens.push_back(make(Tok::LPAREN, "(")); break;
                case ')': tokens.push_back(make(Tok::RPAREN, ")")); break;
                case '[': tokens.push_back(make(Tok::LBRACKET, "[")); break;
                case ']': tokens.push_back(make(Tok::RBRACKET, "]")); break;
                case '{': tokens.push_back(make(Tok::LBRACE, "{")); break;
                case '}': tokens.push_back(make(Tok::RBRACE, "}")); break;
                case ',': tokens.push_back(make(Tok::COMMA, ",")); break;
                case '~': tokens.push_back(make(Tok::TILDE, "~")); break;
                case '-':
                    if (match('>')) tokens.push_back(make(Tok::ARROW, "->"));
                    else if (match('=')) tokens.push_back(make(Tok::MINUS_EQ, "-="));
                    else tokens.push_back(make(Tok::MINUS, "-"));
                    break;
                case '=':
                    if (match('>')) tokens.push_back(make(Tok::FAT_ARROW, "=>"));
                    else if (match('=')) tokens.push_back(make(Tok::EQ, "=="));
                    else tokens.push_back(make(Tok::ERROR_TOK, "="));
                    break;
                case '!':
                    if (match('=')) tokens.push_back(make(Tok::NEQ, "!="));
                    else if (match('!')) tokens.push_back(make(Tok::NOT, "!!"));
                    else tokens.push_back(make(Tok::BANG, "!"));
                    break;
                case '<':
                    if (match('<')) tokens.push_back(make(Tok::LSHIFT, "<<"));
                    else if (match('=')) tokens.push_back(make(Tok::LTE, "<="));
                    else tokens.push_back(make(Tok::LT, "<"));
                    break;
                case '>':
                    if (match('>')) {
                        if (match('>')) tokens.push_back(make(Tok::TRIPLE_RSHIFT, ">>>"));
                        else tokens.push_back(make(Tok::RSHIFT, ">>"));
                    }
                    else if (match('=')) tokens.push_back(make(Tok::GTE, ">="));
                    else tokens.push_back(make(Tok::GT, ">"));
                    break;
                case '&':
                    if (match('&')) tokens.push_back(make(Tok::AND, "&&"));
                    else tokens.push_back(make(Tok::AMP, "&"));
                    break;
                case '|':
                    if (match('|')) tokens.push_back(make(Tok::OR, "||"));
                    else tokens.push_back(make(Tok::ERROR_TOK, "|"));
                    break;
                case ':':
                    if (match(':')) tokens.push_back(make(Tok::DOUBLE_COLON, "::"));
                    else tokens.push_back(make(Tok::COLON, ":"));
                    break;
                case ';':
                    if (match(';')) tokens.push_back(make(Tok::DOUBLE_SEMI, ";;"));
                    else tokens.push_back(make(Tok::SEMICOLON, ";"));
                    break;
                case '.':
                    if (match('.')) tokens.push_back(make(Tok::DOTDOT, ".."));
                    else tokens.push_back(make(Tok::DOT, "."));
                    break;
                case '?':
                    if (match('?')) tokens.push_back(make(Tok::DOUBLE_Q, "??"));
                    else tokens.push_back(make(Tok::QUESTION, "?"));
                    break;
                case '#': tokens.push_back(make(Tok::HASH, "#")); break;
                case '/':
                    if (match('=')) tokens.push_back(make(Tok::SLASH_EQ, "/="));
                    else tokens.push_back(make(Tok::SLASH, "/"));
                    break;
                default: break;
            }
        }

        tokens.push_back({Tok::EOF_TOK, "", line_, col_});
        return tokens;
    }
};
