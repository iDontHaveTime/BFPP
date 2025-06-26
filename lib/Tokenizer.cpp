#include "Tokenizer.hpp"
#include <cstdint>

using namespace Tokenizer;

char EscapeLookup[128] = {0};

void InitEscapeLookup(void){
    EscapeLookup['n'] = '\n';
    EscapeLookup['0'] = '\0';
    EscapeLookup['t'] = '\t';
    EscapeLookup['b'] = '\b';
    EscapeLookup['r'] = '\r';
    EscapeLookup['a'] = '\a';
    EscapeLookup['\''] = '\'';
    EscapeLookup['"'] = '"';
    EscapeLookup['?'] = '\?';
    EscapeLookup['\\'] = '\\';
    EscapeLookup['f'] = '\f';
    EscapeLookup['v'] = '\v';
}

TokenType lookup[128] = {TokenType::T_NONE};

#define TokenTypeLookup lookup
#define TT_TYPE TokenType
CharType CharLookup[128] = {CharType::None};
void InitLookup(){
    InitEscapeLookup();

    TokenTypeLookup['('] = TT_TYPE::T_LPAREN;
    TokenTypeLookup[')'] = TT_TYPE::T_RPAREN;

    TokenTypeLookup['{'] = TT_TYPE::T_LBRACE;
    TokenTypeLookup['}'] = TT_TYPE::T_RBRACE;

    TokenTypeLookup['['] = TT_TYPE::T_LSQUARE;
    TokenTypeLookup[']'] = TT_TYPE::T_RSQUARE;

    TokenTypeLookup['<'] = TT_TYPE::T_LSHIFT;
    TokenTypeLookup['>'] = TT_TYPE::T_RSHIFT;

    TokenTypeLookup['?'] = TT_TYPE::T_QUESTION;
    TokenTypeLookup['.'] = TT_TYPE::T_DOT;
    TokenTypeLookup[','] = TT_TYPE::T_COMMA;

    TokenTypeLookup['*'] = TT_TYPE::T_STAR;
    TokenTypeLookup['-'] = TT_TYPE::T_MINUS;
    TokenTypeLookup['+'] = TT_TYPE::T_PLUS;
    TokenTypeLookup['/'] = TT_TYPE::T_SLASH;

    TokenTypeLookup['!'] = TT_TYPE::T_EXCLAMATION;
    TokenTypeLookup['@'] = TT_TYPE::T_AT;
    TokenTypeLookup['#'] = TT_TYPE::T_HASHTAG;
    TokenTypeLookup['$'] = TT_TYPE::T_DOLLAR;
    TokenTypeLookup['%'] = TT_TYPE::T_PERCENT;
    TokenTypeLookup['^'] = TT_TYPE::T_CARET;

    TokenTypeLookup['&'] = TT_TYPE::T_AND;
    TokenTypeLookup['_'] = TT_TYPE::T_BOTTOMLINE;
    TokenTypeLookup['='] = TT_TYPE::T_EQUALS;

    TokenTypeLookup['"'] = TT_TYPE::T_STRING;
    TokenTypeLookup['\''] = TT_TYPE::T_QUOTE;

    TokenTypeLookup[';'] = TT_TYPE::T_SEMICOLON;
    TokenTypeLookup[':'] = TT_TYPE::T_COLON;

    TokenTypeLookup['\\'] = TT_TYPE::T_BACKSLASH;

    TokenTypeLookup['|'] = TT_TYPE::T_OR;
    TokenTypeLookup['~'] = TT_TYPE::T_NOT;

    const char* symbols = "!@#$%^&*()+-=\\|[]{}\"';:,<.>/?~`";
    const char* alphas = "ABCDEFGHIJKLMNOPQRSTUVWXYZ_abcdefghijklmnopqrstuvwxyz";
    const char* nums = "0123456789";

    for(size_t i = 0; symbols[i] != '\0'; i++){
        CharLookup[(uint8_t)symbols[i]] = CharType::Symbol;
    }
    for(size_t i = 0; alphas[i] != '\0'; i++){
        CharLookup[(uint8_t)alphas[i]] = CharType::Alpha;
    }
    for(size_t i = 0; nums[i] != '\0'; i++){
        CharLookup[(uint8_t)nums[i]] = CharType::Number;
    }

    CharLookup['\0'] = CharType::Special;
    CharLookup['\a'] = CharType::Special;
    CharLookup['\b'] = CharType::Special;
    CharLookup['\e'] = CharType::Special;
    CharLookup['\f'] = CharType::Special;
    CharLookup['\n'] = CharType::Special;
    CharLookup['\r'] = CharType::Special;
    CharLookup['\t'] = CharType::Special;
    CharLookup['\v'] = CharType::Special;
    CharLookup[' '] = CharType::Special;
}


enum class TokenizerState : uint8_t{
    Normal,
    Alpha,
    Number,
    Symbol,
};

struct TokenizerContext{
    std::vector<Token> tokens;
    const char* cur;
    const char* end;
    size_t line = 1;
    char build[512] = {0};
    unsigned int index = 0;
    CharType look;
    TokenizerState state = TokenizerState::Normal;
    TokenType ct = TokenType::T_NONE;


    TokenizerContext(const char* _cur, const char* _end) :
        cur(_cur), end(_end){};
};

inline void TAlphaState(TokenizerContext& ctx);
inline void TSymbolState(TokenizerContext& ctx);
inline void TNumberState(TokenizerContext& ctx);

inline void CallBasedOnType(TokenizerContext& ctx){
    if(ctx.look == CharType::Special){
        if(*ctx.cur == '\n'){
            ctx.line++;
        }
        ctx.state = TokenizerState::Normal;
    }
    else if(ctx.look == CharType::Symbol){
        ctx.state = TokenizerState::Symbol;
        TSymbolState(ctx);
    }
    else if(ctx.look == CharType::Number){
        ctx.ct = TokenType::T_DECIMAL;
        ctx.state = TokenizerState::Number;
        TNumberState(ctx);
    }
    else if(ctx.look == CharType::Alpha){
        ctx.ct = TokenType::T_ALPHA;
        ctx.state = TokenizerState::Alpha;
        TAlphaState(ctx);
    }
}

inline void AddChar(TokenizerContext& ctx){
    ctx.build[ctx.index++] = *ctx.cur;
}

inline void ClearBuild(TokenizerContext& ctx){
    ctx.index = 0;
}

inline void EatToken(TokenizerContext& ctx){
    if(ctx.index > 0){
        ctx.tokens.emplace_back(ctx.build, ctx.index, ctx.line, ctx.ct);
        ClearBuild(ctx);
    }
}

inline void TNormalState(TokenizerContext& ctx){
    CallBasedOnType(ctx);
}

inline void TAlphaState(TokenizerContext& ctx){
    if(ctx.look == CharType::Alpha){
        AddChar(ctx);
    }
    else if(ctx.look == CharType::Number){
        AddChar(ctx);
    }
    else{
        EatToken(ctx);
        CallBasedOnType(ctx);
    }
}

inline void TSymbolState(TokenizerContext& ctx){
    if(ctx.look == CharType::Symbol){
        AddChar(ctx);
        ctx.ct = lookup[(uint8_t)*ctx.cur];
        EatToken(ctx);
    }
    else{
        CallBasedOnType(ctx);
    }
}

inline void TNumberState(TokenizerContext& ctx){
    if(ctx.look == CharType::Number){
        AddChar(ctx);
    }
    else if(*ctx.cur == '.' && ctx.ct != TokenType::T_FLOAT){
        ctx.ct = TokenType::T_FLOAT;
        AddChar(ctx);
    }
    else if(*ctx.cur == 'x' && ctx.index == 1 && ctx.ct != TokenType::T_HEX){
        ctx.ct = TokenType::T_HEX;
        AddChar(ctx);
    }
    else if(ctx.look == Tokenizer::CharType::Alpha && ctx.ct == Tokenizer::TokenType::T_HEX){
        AddChar(ctx);
    }
    else{
        EatToken(ctx);
        CallBasedOnType(ctx);
    }
}

inline void RerouteTokenizer(TokenizerContext& ctx){
    switch(ctx.state){
        case TokenizerState::Normal:
            TNormalState(ctx);
            break;
        case TokenizerState::Symbol:
            TSymbolState(ctx);
            break;
        case TokenizerState::Number:
            TNumberState(ctx);
            break;
        case TokenizerState::Alpha:
            TAlphaState(ctx);
            break;
        default:
            break;
    }
}

std::vector<Token> Tokenizer::Tokenize(std::string& file, size_t reserve){
    static bool initted = false;
    if(!initted){
        InitLookup();
        initted = true;
    }

    TokenizerContext ctx(file.data(), file.data() + file.size());

    ctx.tokens.reserve(reserve);


    while(ctx.cur != ctx.end){
        ctx.look = CharLookup[(uint8_t)*ctx.cur];
        RerouteTokenizer(ctx);
        ctx.cur++;
    }

    EatToken(ctx);

    return ctx.tokens;
}

size_t AssumeReserve(std::string& file){
    const char* cur = file.data();
    const char* end = file.data() + file.size();

    size_t assumed = 0;

    while(cur != end){
        CharType t = CharLookup[(uint8_t)*cur];
        if(t == CharType::Symbol || t == CharType::Special){
            assumed++;
        }
        cur++;
    }
    return assumed;
}

    

std::vector<Token> Tokenizer::Tokenize(std::string& file){
    return Tokenizer::Tokenize(file, AssumeReserve(file));
}