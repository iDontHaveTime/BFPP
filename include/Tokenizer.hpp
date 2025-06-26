#ifndef TOKENIZER_HPP
#define TOKENIZER_HPP

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <cstdint>

namespace Tokenizer{
    enum class TokenType : uint8_t{
        T_NONE,
        T_RPAREN, // )
        T_LPAREN, // (

        T_RBRACE, // }
        T_LBRACE, // {

        T_RSQUARE, // ]
        T_LSQUARE, // [

        T_LSHIFT, // <
        T_RSHIFT, // >

        T_QUESTION, // ?
        T_DOT, // .
        T_COMMA, // ,
            
        T_STAR, // *
        T_MINUS, // -
        T_PLUS, // +
        T_SLASH, // /

        T_EXCLAMATION, // !
        T_AT, // @
        T_HASHTAG, // #
        T_DOLLAR, // $
        T_PERCENT, // %
        T_CARET, // ^

        T_AND, // &
        T_BOTTOMLINE, // _
        T_EQUALS, // =

        T_STRING, // "
        T_QUOTE, // '

        T_SEMICOLON, // ;
        T_COLON, // :

        T_BACKSLASH, // '\'
            
        T_OR, // |
        T_NOT, // ~

        T_ALPHA,
        T_DECIMAL,
        T_FLOAT,
        T_HEX,
    };

    enum class CharType{
        None,
        Symbol,
        Alpha,
        Number,
        Special,
    };

    struct Token{
        std::string val;
        size_t line;
        TokenType type;
        int kwd;

        Token() : val(), line(0), type(TokenType::T_NONE), kwd(0){};

        Token(std::string& str, size_t _line, TokenType tp) :
            val(str), line(_line), type(tp){};

        Token(const char* str, size_t len, size_t _line, TokenType tp) :
            val(str, len), line(_line), type(tp){};

        friend std::ostream& operator<<(std::ostream& lhs, const Token& rhs){
            lhs<<rhs.val;
            return lhs;
        }

        bool operator==(const Token& rhs){
            return val == rhs.val;
        }
    };

    std::vector<Token> Tokenize(std::string& file, size_t reserve);
    std::vector<Token> Tokenize(std::string& file);
}

#endif // TOKENIZER_HPP
