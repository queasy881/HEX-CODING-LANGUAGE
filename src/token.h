#pragma once
// HEX Language — Token types and Token struct
#include <string>

enum class Tok {
    // Literals
    NUMBER, STRING, IDENT, DOLLAR_IDENT,
    TRUE_LIT, FALSE_LIT, NULL_LIT,

    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT, CARET,
    ARROW,      // ->
    FAT_ARROW,  // =>
    LSHIFT,     // <<
    RSHIFT,     // >>

    // Comparison
    EQ, NEQ, LT, GT, LTE, GTE,

    // Logic
    AND, OR, NOT,

    // Grouping
    LPAREN, RPAREN,
    LBRACKET, RBRACKET,
    LBRACE, RBRACE,

    // Punctuation
    COMMA, COLON, DOT, DOTDOT, SEMICOLON,
    DOUBLE_SEMI, // ;;
    DOUBLE_COLON, // ::

    // Prefix symbols
    DOLLAR,     // $
    TILDE,      // ~
    BANG,       // !
    AMP,        // &
    HASH,       // #
    QUESTION,   // ?
    DOUBLE_Q,   // ??

    // Compound assignment
    PLUS_EQ, MINUS_EQ, STAR_EQ, SLASH_EQ,

    // Triple >>
    TRIPLE_RSHIFT,  // >>>

    // Keywords
    KW_IF, KW_ELSE, KW_WHILE, KW_FOR, KW_IN,
    KW_FUNC, KW_RETURN, KW_BREAK, KW_CONTINUE,
    KW_REP, KW_TRY, KW_CATCH, KW_END,
    KW_MATCH, KW_CASE, KW_DEFAULT, KW_FOREVER,
    KW_DO, KW_LOCK, KW_IMPORT, KW_CLASS, KW_NEW, KW_THIS,
    KW_ASYNC, KW_AWAIT,
    KW_ENUM, KW_EXTENDS,

    // Special
    NEWLINE, EOF_TOK, ERROR_TOK
};

struct Token {
    Tok         type;
    std::string value;
    int         line;
    int         col;
};
