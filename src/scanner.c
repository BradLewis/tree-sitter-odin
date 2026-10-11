#include "tree_sitter/parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <wctype.h>

enum {
    NEWLINE,
    BACKSLASH,
    NL_COMMA,
    FLOAT,
    BLOCK_COMMENT,
    BLOCK_START,
    QUOTE,
    MULTILINE_STRING_CONTENT,
    MULTILINE_RAW_STRING_CONTENT,
};

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

void *tree_sitter_odin_external_scanner_create() { return NULL; }

unsigned tree_sitter_odin_external_scanner_serialize(void *payload, char *buffer) { return 0; }

void tree_sitter_odin_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {}

bool tree_sitter_odin_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
    // These tokens take priority over comment extras inside multiline strings.
    if (valid_symbols[MULTILINE_STRING_CONTENT] || valid_symbols[MULTILINE_RAW_STRING_CONTENT]) {
        if (valid_symbols[MULTILINE_STRING_CONTENT] && valid_symbols[MULTILINE_RAW_STRING_CONTENT]) {
            return false; // Both are enabled during error recovery, not inside a valid string.
        }
        const bool raw = valid_symbols[MULTILINE_RAW_STRING_CONTENT];
        const int32_t quote = raw ? '`' : '"';
        bool consumed = false;
        lexer->mark_end(lexer);
        while (!lexer->eof(lexer)) {
            if (!raw && lexer->lookahead == '\\') {
                break;
            }
            if (lexer->lookahead == quote) {
                unsigned quotes = 0;
                while (quotes < 3 && lexer->lookahead == quote) {
                    advance(lexer);
                    quotes++;
                }
                if (quotes == 3) {
                    break; // Keep the closing delimiter outside the content token.
                }
            } else {
                advance(lexer);
            }
            consumed = true;
            lexer->mark_end(lexer);
        }
        if (consumed) {
            lexer->result_symbol = raw ? MULTILINE_RAW_STRING_CONTENT : MULTILINE_STRING_CONTENT;
        }
        return consumed;
    }

    if (valid_symbols[FLOAT]) {
        while (iswspace(lexer->lookahead) && lexer->lookahead != '\n') {
            skip(lexer);
        }

        if (!valid_symbols[NEWLINE]) { // skip newlines too
            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }
        }

        // basically, -? [0-9]+ \. [0-9]*, BUT a second . after isnt allowed
        // cuz it could be ..< operator,
        // it can have an i at the end for imaginary numbers
        // and exponents, [eE][+-]?[0-9]+, imaginary comes after

        // needs ONE of these two to be float
        bool found_decimal = false;
        bool found_exponent = false;
        bool found_number_before_decimal = false;
        bool found_number_after_decimal = false;
        bool found_number_after_expontent = false;
        for (int i = 0;; i++) {
            switch (lexer->lookahead) {
                case '.':
                    if ((found_decimal || found_exponent) &&
                        (found_number_after_decimal || found_number_before_decimal)) {
                        lexer->result_symbol = FLOAT;
                        lexer->mark_end(lexer);
                        return true;
                    } else {
                        lexer->mark_end(lexer);
                        found_decimal = true;
                        advance(lexer);
                        if (lexer->lookahead == '.') {
                            advance(lexer);
                            goto newline;
                        }
                        lexer->mark_end(lexer);
                        if (!isdigit(lexer->lookahead) && (found_number_after_decimal || found_number_before_decimal)) {
                            lexer->result_symbol = FLOAT;
                            lexer->mark_end(lexer);
                            return true;
                        }
                    }
                    break;
                case 'i':
                case 'j':
                case 'k':
                    if (!found_number_after_decimal) {
                        goto newline;
                    }
                    if ((found_decimal || found_exponent) &&
                        (found_number_after_decimal || found_number_before_decimal)) {
                        advance(lexer);
                        lexer->result_symbol = FLOAT;
                        lexer->mark_end(lexer);
                        return true;
                    }
                    goto newline;
                case '_':
                    if (!found_number_before_decimal && !found_number_after_decimal) {
                        goto newline;
                    }
                    advance(lexer);
                    break;
                case 'e':
                case 'E':
                    if ((found_exponent) && (found_number_after_decimal || found_number_before_decimal)) {
                        lexer->result_symbol = FLOAT;
                        lexer->mark_end(lexer);
                        return true;
                    } else if (found_number_before_decimal || found_number_after_decimal) {
                        found_exponent = true;
                        advance(lexer);
                    } else {
                        goto newline;
                    }
                    break;
                case '+':
                case '-':
                    if (i == 0 || (found_exponent && !found_number_after_expontent)) {
                        advance(lexer);
                    } else if ((found_decimal || found_exponent) &&
                               (found_number_after_decimal || found_number_before_decimal)) {
                        lexer->result_symbol = FLOAT;
                        lexer->mark_end(lexer);
                        return true;
                    } else {
                        goto newline;
                    }
                    break;
                default:
                    if (lexer->lookahead <= 255 && isdigit(lexer->lookahead)) {
                        advance(lexer);
                        if (found_decimal) {
                            found_number_after_decimal = true;
                        } else {
                            found_number_before_decimal = true;
                        }
                        if (found_exponent && !found_number_after_expontent) {
                            found_number_after_expontent = true;
                        }
                    } else {
                        if ((found_decimal || found_exponent) &&
                            (found_number_after_decimal || found_number_before_decimal)) {
                            lexer->result_symbol = FLOAT;
                            lexer->mark_end(lexer);
                            return true;
                        }
                        if (found_number_before_decimal) {
                            return false; // number needs to match
                        }
                        goto newline;
                    }
            }
        }
    }

    if (valid_symbols[NL_COMMA]) {
        while (iswspace(lexer->lookahead) && lexer->lookahead != '\n') {
            skip(lexer);
        }

        if (lexer->lookahead == ',') {
            advance(lexer);
            lexer->result_symbol = NL_COMMA;
            lexer->mark_end(lexer);
            while (iswspace(lexer->lookahead) && lexer->lookahead != '\n') {
                advance(lexer);
            }

            if (lexer->lookahead == '\n') {
                while (iswspace(lexer->lookahead)) {
                    advance(lexer);
                }
                return lexer->lookahead != '}';
            }
        }
    }

newline:
    if (valid_symbols[NEWLINE]) {
        while (iswspace(lexer->lookahead) && lexer->lookahead != '\n') {
            skip(lexer);
        }

        if (lexer->lookahead == '\n') {
            advance(lexer);
            lexer->result_symbol = NEWLINE;
            lexer->mark_end(lexer);

            while (iswspace(lexer->lookahead)) {
                skip(lexer);
            }

            const char *where = "where";
            const char *_else = "else";

            // Only body braces suppress a separator; initializer braces do not.
            if (lexer->lookahead == '{' && valid_symbols[BLOCK_START]) {
                return false;
            }

            char next_word[6] = {0};

            // check for where and _else

            for (int i = 0; i < 5; i++) {
                if (iswspace(lexer->lookahead)) {
                    break;
                }
                next_word[i] = (char)lexer->lookahead;
                advance(lexer);
            }

            if (strcmp(next_word, where) == 0 || strcmp(next_word, _else) == 0) {
                if (!iswspace(lexer->lookahead)) {
                    return true;
                }
                goto backslash;
            }

            return true;
        }
        // if (lexer->lookahead == ';') {
        //     advance(lexer);
        //     lexer->result_symbol = SEPARATOR;
        //     lexer->mark_end(lexer);
        //     while (iswspace(lexer->lookahead)) {
        //         advance(lexer);
        //     }
        //     return true;
        // }
    }

backslash:
    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }

    if (valid_symbols[BACKSLASH] && lexer->lookahead == '\\') {
        advance(lexer);
        if (lexer->lookahead == '\n') {
            advance(lexer);
            while (iswspace(lexer->lookahead)) {
                advance(lexer);
            }
            lexer->result_symbol = BACKSLASH;
            return true;
        }
    }

    while (iswspace(lexer->lookahead)) {
        skip(lexer);
    }

    if (valid_symbols[BLOCK_START] && lexer->lookahead == '{') {
        advance(lexer);
        lexer->mark_end(lexer);
        lexer->result_symbol = BLOCK_START;
        return true;
    }

    if (valid_symbols[BLOCK_COMMENT] && lexer->lookahead == '/') {
        advance(lexer);
        if (lexer->lookahead != '*') {
            return false;
        }
        advance(lexer);

        if (lexer->lookahead == '"') {
            return false;
        }

        bool after_star = false;
        unsigned nesting_depth = 1;
        for (;;) {
            switch (lexer->lookahead) {
                case '\0':
                    return false;
                case '*':
                    advance(lexer);
                    after_star = true;
                    break;
                case '/':
                    if (after_star) {
                        advance(lexer);
                        after_star = false;
                        nesting_depth--;
                        if (nesting_depth == 0) {
                            lexer->result_symbol = BLOCK_COMMENT;
                            return true;
                        }
                    } else {
                        advance(lexer);
                        after_star = false;
                        if (lexer->lookahead == '*') {
                            nesting_depth++;
                            advance(lexer);
                        }
                    }
                    break;
                default:
                    advance(lexer);
                    after_star = false;
                    break;
            }
        }
    }

    return false;
}

void tree_sitter_odin_external_scanner_destroy(void *payload) {}
