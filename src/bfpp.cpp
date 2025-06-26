#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "Tokenizer.hpp"

#define SYS_IN 0
#define SYS_OUT 1
#define SYS_ERR 2
#define SYS_OUT_INDEX 1

unsigned int ALLOCATE = 16384;
int BASE_OFFSET = 128;

struct BFPPRegisters;

bool CheckAvailable(const char* cmd) {
#ifdef _WIN32
    std::string checkCmd = "where ";
#else
    std::string checkCmd = "which ";
#endif
    checkCmd += cmd;
#ifdef _WIN32
    checkCmd += " >nul 2>&1";
#else
    checkCmd += " >/dev/null 2>&1";
#endif

    int ret = std::system(checkCmd.c_str());
    return ret == 0;
}

// used for stuff like syscalls, to keep track of rax's value for example
struct Register{
    long long value = 0;
    // to keep track if the compiler knows the current value of the register
    // for example if a function is called rax would be desynced because returns go into rax
    bool synced = false;
    std::string name, lower32, lower16, lower8;

    Register() : name(){};
    Register(std::string& str) : name(str){};
    Register(const char* str) : name(str){};
    Register(const char* b64, const char* b32, const char* b16, const char* b8) :
        name(b64), lower32(b32), lower16(b16), lower8(b8){};

    inline Register& operator=(const char* rhs){
        name = rhs;
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& lhs, const Register& rhs){
        lhs<<rhs.name;
        return lhs;
    }
};

enum class Keyword : uint8_t{
    None,
    i8,
    i16,
    i32,
    i64,
    u8,
    u16,
    u32,
    u64,
    Void,
    mov,
    extrn,
    call,
};

struct Label{
    std::string Name;
    size_t pos;
    unsigned int ptrl; // pointer level, not used
    size_t end = 0;
    size_t extraAlloc = 0;
    Keyword type;
    Label(std::string& name, size_t _pos, unsigned int _ptrl, Keyword _type) : Name(name), pos(_pos), ptrl(_ptrl), type(_type){};

    friend std::ostream& operator<<(std::ostream& lhs, const Label& rhs){
        lhs<<rhs.Name;
        return lhs;
    }
};

// bf++ keywords
struct BFPPKWD{
    std::unordered_map<std::string, Keyword> keywords = {
        {"i8", Keyword::i8},
        {"i16", Keyword::i16},
        {"i32", Keyword::i32},
        {"i64", Keyword::i64},
        {"u8", Keyword::u8},
        {"u16", Keyword::u16},
        {"u32", Keyword::u32},
        {"u64", Keyword::u64},
        {"mov", Keyword::mov},
        {"void", Keyword::Void},
        {"extern", Keyword::extrn},
        {"call", Keyword::call},
    };
};

enum class ParsingState{
    Normal,
    Label,
    BFPP,
};

enum class Widths : uint8_t{
    Byte = 1, Word = 2, Dword = 4, Qword = 8
};

struct WidthSwitch{
    Widths to;
    size_t pos;
    WidthSwitch(Widths _to, size_t _pos) : to(_to), pos(_pos){};
};

struct MoveValue{
    long long val;
    size_t pos;
    MoveValue(long long v, size_t p) : val(v), pos(p){};
};

struct FReturn{
    size_t pos;
    size_t label;
    FReturn(size_t _p, size_t lbl) : pos(_p), label(lbl){};
};

enum class BFInstructionType{
    NONE, LEFT, RIGHT, PLUS, MINUS, OUTPUT, ARGUMENT, LOOP, GETARG
};

struct BFInstruction{
    BFInstructionType type;
    unsigned int count;
    size_t pos;
    bool address = false;

    BFInstruction() : type(BFInstructionType::NONE), count(0){};

    BFInstruction(BFInstructionType t, unsigned int c, size_t _pos) : type(t), count(c), pos(_pos){};
};

struct Call{
    std::string_view name;
    size_t pos;

    Call(size_t p, std::string& str) : name(str), pos(p){};
};

struct Loop{
    size_t start, end;
    Loop(size_t s, size_t e) : start(s), end(e){};  
};

// used for codegen
struct ParsedContext{
    std::vector<Label> labels;
    std::vector<WidthSwitch> switches;
    std::vector<FReturn> rets;
    std::vector<Call> calls;
    std::vector<std::string> externs; 
    std::vector<Loop> loops;
    std::vector<Loop> done_loops;
    size_t pos;
    BFPPKWD& bfpp;
    Tokenizer::Token* curTok;
    ParsingState state = ParsingState::Normal;
    Keyword type = Keyword::Void;
    bool special = false;
    unsigned short ptrl = 0;
    size_t tokensLen;
    std::vector<Tokenizer::Token>& tokens;
    std::vector<BFInstruction> ins;
    std::vector<MoveValue> movs;
    BFPPRegisters& regs;
    BFInstructionType curIns = BFInstructionType::NONE;
    unsigned int insCount = 0;
    
    ParsedContext(std::vector<Tokenizer::Token>& toks, BFPPKWD& bf, BFPPRegisters& _regs) : bfpp(bf), tokensLen(toks.size()), tokens(toks), regs(_regs){};
};

inline bool LookableAhead(ParsedContext& ctx){
    return (ctx.pos + 1 != ctx.tokensLen);
}

inline Tokenizer::Token& LookAhead(ParsedContext& ctx){
    return ctx.tokens[ctx.pos+1];
}

inline void ResetContext(ParsedContext& ctx){
    ctx.state = ParsingState::Normal;
    ctx.special = false;
    ctx.type = Keyword::Void;
    ctx.curIns = BFInstructionType::NONE;
}

inline void PushBackInstruction(ParsedContext& ctx){
    if(ctx.curIns != BFInstructionType::NONE){
        ctx.ins.emplace_back(ctx.curIns, ctx.insCount, ctx.pos - 1);
        ctx.curIns = BFInstructionType::NONE;
        ctx.insCount = 0;
    }
}

inline BFInstructionType GetInstructionType(Tokenizer::Token& tok){
    switch(tok.type){
        case Tokenizer::TokenType::T_LSHIFT:
            return BFInstructionType::LEFT;
        case Tokenizer::TokenType::T_RSHIFT:
            return BFInstructionType::RIGHT;
        case Tokenizer::TokenType::T_PLUS:
            return BFInstructionType::PLUS;
        case Tokenizer::TokenType::T_MINUS:
            return BFInstructionType::MINUS;
        case Tokenizer::TokenType::T_DOT:
            return BFInstructionType::OUTPUT;
        case Tokenizer::TokenType::T_STAR:
            return BFInstructionType::ARGUMENT;
        case Tokenizer::TokenType::T_RSQUARE:
            return BFInstructionType::LOOP;
        case Tokenizer::TokenType::T_LSQUARE:
            return BFInstructionType::LOOP;
        case Tokenizer::TokenType::T_AND:
            return BFInstructionType::GETARG;
        default:
            return BFInstructionType::NONE;
    }
}

inline bool IsInstruction(ParsedContext& ctx){
    return GetInstructionType(*ctx.curTok) != BFInstructionType::NONE;
}

inline void ParseInstruction(ParsedContext& ctx){
    BFInstructionType t = GetInstructionType(*ctx.curTok);
    if(t != ctx.curIns && t != BFInstructionType::LOOP){
        PushBackInstruction(ctx);
        ctx.curIns = t;
        ctx.insCount++;
    }
    else if(t == BFInstructionType::LOOP){
        if(ctx.curIns != BFInstructionType::LOOP){
            PushBackInstruction(ctx);
        }
        ctx.curIns = BFInstructionType::LOOP;
        if(ctx.curTok->type == Tokenizer::TokenType::T_LSQUARE){
            ctx.loops.emplace_back(ctx.pos, 0);
        }
        else if(ctx.curTok->type == Tokenizer::TokenType::T_RSQUARE){
            Loop& back = ctx.loops.back();
            ctx.done_loops.emplace_back(back.start, ctx.pos);
            ctx.loops.pop_back();
        }
    }
    else{
        ctx.insCount++;
    }
}

inline void NormalParse(ParsedContext& ctx){
    if(ctx.curTok->type == Tokenizer::TokenType::T_AT){
        PushBackInstruction(ctx);
        ctx.state = ParsingState::Label;
    }
    else if(ctx.curTok->type == Tokenizer::TokenType::T_EXCLAMATION){
        if(ctx.labels.empty()){
            std::cerr<<"Global returns are not permitted"<<std::endl;
            return;
        }
        PushBackInstruction(ctx);
        ctx.rets.emplace_back(ctx.pos, ctx.labels.size() - 1);
    }
    else if(ctx.curTok->type == Tokenizer::TokenType::T_QUESTION){
        PushBackInstruction(ctx);
        ctx.state = ParsingState::BFPP;
    }
    else{
        if(ctx.curTok->type == Tokenizer::TokenType::T_CARET){
            PushBackInstruction(ctx);
            if(!ctx.ins.empty()){
                ctx.ins.back().address = true;
            }
        }
        else if(IsInstruction(ctx)){
            ParseInstruction(ctx);
        }
        else{
            PushBackInstruction(ctx);
        }
    }
}

inline void BFPPParse(ParsedContext& ctx){
    Keyword kwd = (Keyword)ctx.curTok->kwd;
    if(kwd == Keyword::None){
        ctx.state = ParsingState::Normal;
        return;
    }
    else if(kwd == Keyword::i8 || kwd == Keyword::u8){
        ctx.switches.emplace_back(Widths::Byte, ctx.pos);
    }
    else if(kwd == Keyword::i16 || kwd == Keyword::u16){
        ctx.switches.emplace_back(Widths::Word, ctx.pos);
    }
    else if(kwd == Keyword::i32 || kwd == Keyword::u32){
        ctx.switches.emplace_back(Widths::Dword, ctx.pos);
    }
    else if(kwd == Keyword::i64 || kwd == Keyword::u64){
        ctx.switches.emplace_back(Widths::Qword, ctx.pos);
    }
    else if(kwd == Keyword::mov){
        if(LookableAhead(ctx)){
            Tokenizer::Token& tok = LookAhead(ctx);
            ctx.pos++;
            if(tok.type == Tokenizer::TokenType::T_DECIMAL){
                ctx.movs.emplace_back(std::stoul(tok.val), ctx.pos);
            }
            else if(tok.type == Tokenizer::TokenType::T_HEX){
                ctx.movs.emplace_back(std::stoul(tok.val, nullptr, 16), ctx.pos);
            }
            else{
                std::cerr<<"Unknown value on mov instruction on line "<<tok.line<<std::endl;
            }
        }
        else{
            std::cerr<<"Error on mov instruction, abruptly ended on line "<<ctx.curTok->line<<std::endl;
        }
    }
    else if(kwd == Keyword::extrn){
        if(LookableAhead(ctx)){
            Tokenizer::Token& tok = LookAhead(ctx);
            ctx.pos++;
            if(tok.type == Tokenizer::TokenType::T_ALPHA){
                ctx.externs.emplace_back(tok.val);
            }
            else{
                std::cerr<<"Unknown token on extern instruction on line "<<tok.line<<std::endl;
            }
        }
        else{
            std::cerr<<"Error on extern instruction, abruptly ended on line "<<ctx.curTok->line<<std::endl;
        }
    }
    else if(kwd == Keyword::call){
        if(LookableAhead(ctx)){
            Tokenizer::Token& tok = LookAhead(ctx);
            ctx.pos++;
            if(tok.type == Tokenizer::TokenType::T_ALPHA){
                ctx.calls.emplace_back(ctx.pos, tok.val);
            }
            else{
                std::cerr<<"Unknown token on call instruction on line "<<tok.line<<std::endl;
            }
        }
        else{
            std::cerr<<"Error on call instruction, abruptly ended on line "<<ctx.curTok->line<<std::endl;
        }
    }
    ctx.state = ParsingState::Normal;
}

inline bool IsType(Keyword kwd){
    switch(kwd){
        case Keyword::i8:
            return true;
        case Keyword::i16:
            return true;
        case Keyword::i32:
            return true;
        case Keyword::i64:
            return true;
        case Keyword::u8:
            return true;
        case Keyword::u16:
            return true;
        case Keyword::u32:
            return true;
        case Keyword::u64:
            return true;
        case Keyword::Void:
            return true;
        default:
            return false;
    }
}

inline void LabelParse(ParsedContext& ctx){
    if(!ctx.special){
        if(ctx.labels.size() > 0){
            ctx.labels.back().end = ctx.pos;
        }
        ctx.labels.emplace_back(ctx.curTok->val, ctx.pos, ctx.ptrl, ctx.type);
        ResetContext(ctx);
        if(LookableAhead(ctx) && LookAhead(ctx).type == Tokenizer::TokenType::T_COLON){
            ctx.special = true;
            ctx.state = ParsingState::Label;
        }
        else{
            ctx.state = ParsingState::Normal;
        }
    }
    else{
        //std::cout<<ctx.curTok->val<<std::endl;
        if(LookableAhead(ctx)){
            Tokenizer::Token& tok = LookAhead(ctx);
            if(IsType((Keyword)tok.kwd)){
                ctx.labels.back().type = (Keyword)ctx.curTok->kwd;
            }
            ctx.pos++;
        }
        ResetContext(ctx);
    }
}

inline void ParsingStateHandle(ParsedContext& ctx){
    switch(ctx.state){
        case ParsingState::Normal:
            NormalParse(ctx);
            break;
        case ParsingState::Label:
            LabelParse(ctx);
            break;
        case ParsingState::BFPP:
            BFPPParse(ctx);
            break;
        default:
            break;
    }
}

ParsedContext ParseTokensBFPP(std::vector<Tokenizer::Token> toks, BFPPKWD& bf, BFPPRegisters& regs){
    ParsedContext out(toks, bf, regs);

    for(out.pos = 0; out.pos < out.tokensLen; out.pos++){
        out.curTok = &toks[out.pos];
        ParsingStateHandle(out);
    }
    Tokenizer::Token temp;
    out.curTok = &temp;
    ParsingStateHandle(out);
    return out;
}

std::string FileIntoString(const char* fileName){
    std::ifstream f(fileName);
    std::stringstream ss;
    ss<<f.rdbuf();
    return ss.str(); 
}

void ClassifyTokens(std::vector<Tokenizer::Token>& toks, BFPPKWD& bf){
    for(Tokenizer::Token& tok : toks){
        auto it = bf.keywords.find(tok.val);
        if(it != bf.keywords.end()){
            tok.kwd = (int)it->second;
        }
        else{
            tok.kwd = (int)Keyword::None;
        }
    }
}

inline std::ofstream& GenerateTextSectionGAS(std::ofstream& file){
    file<<'\t'<<".text";
    return file;
}

inline const char* GetGlobalSyntax(){
    return ".globl";
}

inline const char* GetExternSyntax(){
    return ".extern";
}

inline const char* GetPowerAlignSyntax(){
    return ".p2align";
}

inline const char* GetByteAlignSyntax(){
    return ".balign";
}

enum class AssemblyInstruction{
    ADD, SUB, MOV, PUSH, POP, RET, SYSCALL, CMP
};

inline const char* GenerateSuffix(Widths width){
    switch(width){
        case Widths::Byte:
            return "b";
        case Widths::Word:
            return "w";
        case Widths::Dword:
            return "l";
        case Widths::Qword:
            return "q";
        default:
            return "";
    }
}

inline const char* GenerateOperation(AssemblyInstruction ins){
    switch(ins){
        case AssemblyInstruction::MOV:
            return "mov";
        case AssemblyInstruction::ADD:
            return "add";
        case AssemblyInstruction::POP:
            return "pop";
        case AssemblyInstruction::PUSH:
            return "push";
        case AssemblyInstruction::SUB:
            return "sub";
        case AssemblyInstruction::RET:
            return "ret";
        case AssemblyInstruction::SYSCALL:
            return "syscall";
        case AssemblyInstruction::CMP:
            return "cmp";
    }
}

inline std::string& GetRegisterWidth(Register& reg, Widths width){
    switch(width){
        case Widths::Byte:
            return reg.lower8;
        case Widths::Word:
            return reg.lower16;
        case Widths::Dword:
            return reg.lower32;
        case Widths::Qword:
            return reg.name;
        default:
            return reg.name;
    }
}

inline std::string GenerateInstruction(AssemblyInstruction type, Widths width){
    std::string ins;
    ins += GenerateOperation(type);
    if(type == AssemblyInstruction::RET || type == AssemblyInstruction::SYSCALL){
        return ins;
    }
    ins += GenerateSuffix(width);
    return ins;
}

inline std::string GenerateRegisterOP(Register& reg){
    return '%' + reg.name;
}

inline std::string GenerateMemRegisterOP(Register& reg){
    std::string regmem;

    regmem += '(';


    regmem += '%' + reg.name +')';
    return regmem;
}

inline std::string GenerateDirectOP(std::string& str){
    return '$' + str;
}

inline std::string GenerateDirectOP(long long val){
    return '$' + std::to_string(val);
}

struct BFPPRegisters{
    Register frameReg = {"rbp", "ebp", "bp", "bpl"};
    Register stackReg = {"rsp", "esp", "sp", "spl"};

    Register rax = {"rax", "eax", "ax", "al"};
    Register rcx = {"rcx", "ecx", "cx", "cl"};
    Register rdx = {"rdx", "edx", "dx", "dl"};
    Register rbx = {"rbx", "ebx", "bx", "bl"};

    Register rsi = {"rsi", "esi", "si", "sil"};
    Register rdi = {"rdi", "edi", "di", "dil"};

    Register r8 = {"r8", "r8d", "r8w", "r8b"};
    Register r9 = {"r9", "r9d", "r9w", "r9b"};
    Register r10 = {"r10", "r10d", "r10w", "r10b"};
    Register r11 = {"r11", "r11d", "r11w", "r11b"};
    Register r12 = {"r12", "r12d", "r12w", "r12b"};
    Register r13 = {"r13", "r13d", "r13w", "r13b"};
    Register r14 = {"r14", "r14d", "r14w", "r14b"};
    Register r15 = {"r15", "r15d", "r15w", "r15w"};

    Register rip = {"rip", "eip", "ip", ""};

    Register& arg1 = rdi;
    Register& arg2 = rsi;
    Register& arg3 = rdx;
    Register& arg4 = rcx;
    Register& arg5 = r8;
    Register& arg6 = r9;
};

inline std::string AlignTo(unsigned int alignment, bool power = true){
    std::string end;
    if(power){
        end += GetPowerAlignSyntax();
    }
    else{
        end += GetByteAlignSyntax();
    }
    end += ' ' + std::to_string(alignment);
    return end;
}

inline std::ofstream& GenerateGlobals(ParsedContext& ctx, std::ofstream& file){
    for(Label& lbl : ctx.labels){
        file<<'\t'<<GetGlobalSyntax()<<' '<<lbl.Name<<std::endl;
    }
    return file;
}

inline std::ofstream& GenerateLabelEndName(Label& lbl, std::ofstream& file){
    file<<"__"<<lbl<<"__end__"<<std::to_string(lbl.pos);
    return file;
}

inline std::ofstream& GenerateLabelName(Label& lbl, std::ofstream& file){
    file<<lbl;
    return file;
}

inline std::string GeneratePushRegister(Register& reg, Widths width){
    return GenerateInstruction(AssemblyInstruction::PUSH, width) + ' ' + GenerateRegisterOP(reg);
}

inline int GetMultiplier(Widths width){
    switch(width){
        case Widths::Byte:
            return 1;
        case Widths::Word:
            return 2;
        case Widths::Dword:
            return 4;
        case Widths::Qword:
            return 8;
    }
}

inline std::ofstream& GeneratePrologue(ParsedContext& ctx, std::ofstream& file){
    // push rbp
    file<<'\t'<<GeneratePushRegister(ctx.regs.frameReg, Widths::Qword)<<std::endl;

    // mov rbp to rsp
    file<<'\t'<<GenerateInstruction(AssemblyInstruction::MOV, Widths::Qword)<<' ';
    file<<GenerateRegisterOP(ctx.regs.stackReg)<<", "<<GenerateRegisterOP(ctx.regs.frameReg)<<std::endl;

    // sub allocation from rsp
    file<<'\t'<<GenerateInstruction(AssemblyInstruction::SUB, Widths::Qword)<<' ';
    file<<GenerateDirectOP(ALLOCATE)<<", "<<GenerateRegisterOP(ctx.regs.stackReg)<<std::endl;

    // sub offset from rbp
    if(BASE_OFFSET > 0){
        file<<'\t'<<GenerateInstruction(AssemblyInstruction::SUB, Widths::Qword)<<' ';
        file<<GenerateDirectOP(BASE_OFFSET)<<", "<<GenerateRegisterOP(ctx.regs.frameReg)<<std::endl;
    }
    return file;
}

inline std::ofstream& GenerateEpilogue(ParsedContext& ctx, std::ofstream& file, Label& lbl){
    // add back to rsp
    file<<'\t'<<GenerateInstruction(AssemblyInstruction::ADD, Widths::Qword)<<' ';
    file<<GenerateDirectOP(ALLOCATE + lbl.extraAlloc)<<", "<<GenerateRegisterOP(ctx.regs.stackReg)<<std::endl;

    // pop rbp
    file<<'\t'<<GenerateInstruction(AssemblyInstruction::POP, Widths::Qword)<<' ';
    file<<GenerateRegisterOP(ctx.regs.frameReg)<<std::endl;

    // return
    file<<'\t'<<GenerateInstruction(AssemblyInstruction::RET, Widths::Byte)<<std::endl;
    return file;
}

inline void GenerateLabelEnd(Label& lbl, std::ofstream& file){
    GenerateLabelEndName(lbl, file)<<":\n";
}

inline const char* GetUJumpSyntax(){
    return "jmp";
}

inline void GenerateInstructionComment(std::ofstream& file, BFInstruction& ins){
    char cc;
    switch(ins.type){
        case BFInstructionType::LEFT:
            cc = '<';
            break;
        case BFInstructionType::RIGHT:
            cc = '>';
            break;
        case BFInstructionType::PLUS:
            cc = '+';
            break;
        case BFInstructionType::MINUS:
            cc = '-';
            break;
        case BFInstructionType::OUTPUT:
            cc = '.';
            break;
        case BFInstructionType::ARGUMENT:
            cc = '*';
            break;
        case BFInstructionType::LOOP:
            cc = '[';
            break;
        case BFInstructionType::GETARG:
            cc = '&';
            break;
        default:
            cc = ' ';
            break;
    }
    for(size_t i = 0; i < ins.count; i++){
        file<<cc;
    }
}

inline std::ofstream& GenerateDirectToReg(std::ofstream& file, long long direct, Register& reg){
    file<<GenerateInstruction(AssemblyInstruction::MOV, Widths::Qword)<<' ';
    file<<GenerateDirectOP(direct)<<", "<<GenerateRegisterOP(reg);
    return file;
}

inline void UnsyncRegister(Register& reg){
    reg.synced = false;
}

inline void UnsyncAll(BFPPRegisters& regs){
    UnsyncRegister(regs.arg1);
    UnsyncRegister(regs.arg2);
    UnsyncRegister(regs.arg3);
    UnsyncRegister(regs.arg4);
    UnsyncRegister(regs.arg5);
    UnsyncRegister(regs.arg6);
    UnsyncRegister(regs.rax);
}

inline std::ofstream& GenerateExterns(ParsedContext& ctx, std::ofstream& file){
    for(std::string& str : ctx.externs){
        file<<'\t'<<GetExternSyntax()<<' '<<str<<std::endl;
    }
    return file;
}

inline const char* GetCallSyntax(){
    return "call";
}

void BFPPCodegen(ParsedContext& ctx, const char* file_out){
    std::ofstream file(file_out);
    if(!file){
        std::cerr<<"Error opening file for codegen"<<std::endl;
        return;
    }
    
    GenerateTextSectionGAS(file)<<'\n';

    GenerateGlobals(ctx, file)<<'\n';
    GenerateExterns(ctx, file)<<'\n';

    bool labelEnd = false;
    size_t labelEndIndex = 0;

    size_t lblsize = ctx.labels.size();
    size_t retsize = ctx.rets.size();
    size_t switches = ctx.switches.size();
    size_t inslen = ctx.ins.size();
    size_t loopsize = ctx.done_loops.size();

    Widths currentWidth = Widths::Byte;

    for(size_t i = 0; i < ctx.pos; i++){
        for(size_t l = 0; l < loopsize; l++){
            Loop& loop = ctx.done_loops[l];
            if(loop.start == i){
                file<<'\t'<<"__loop__start__"<<std::to_string(l)<<':'<<std::endl;
                file<<'\t'<<GenerateInstruction(AssemblyInstruction::CMP, currentWidth)<<' ';
                file<<GenerateDirectOP(0)<<", "<<GenerateMemRegisterOP(ctx.regs.frameReg)<<std::endl;
                file<<'\t'<<"je "<<"__loop__end__"<<std::to_string(l)<<std::endl;
            }
            else if(loop.end == i){
                file<<'\t'<<GetUJumpSyntax()<<' '<<"__loop__start__"<<std::to_string(l)<<std::endl;
                file<<'\t'<<"__loop__end__"<<std::to_string(l)<<':'<<std::endl;
            }
        }
        for(Call& call : ctx.calls){
            if(call.pos == i){
                UnsyncAll(ctx.regs);
                file<<'\t'<<GetCallSyntax()<<' '<<call.name<<std::endl;
                file<<'\t'<<GenerateInstruction(AssemblyInstruction::MOV, currentWidth)<<' ';
                file<<'%'<<GetRegisterWidth(ctx.regs.rax, currentWidth)<<", ";
                file<<GenerateMemRegisterOP(ctx.regs.frameReg)<<std::endl;
            }
        }
        for(MoveValue& mov : ctx.movs){
            if(mov.pos == i){
                file<<'\t';
                file<<GenerateInstruction(AssemblyInstruction::MOV, currentWidth)<<' ';
                file<<GenerateDirectOP(mov.val)<<", "<<GenerateMemRegisterOP(ctx.regs.frameReg);
                file<<std::endl;
            }
        }
        for(size_t s = 0; s < switches; s++){
            WidthSwitch& sw = ctx.switches[s];
            if(sw.pos == i){
                currentWidth = sw.to;
            }
        }
        for(size_t j = 0; j < inslen; j++){
            BFInstruction& ins = ctx.ins[j];
            if(ins.pos == i){
                if(ins.type == BFInstructionType::PLUS){
                    file<<'\t'<<GenerateInstruction(AssemblyInstruction::ADD, currentWidth)<<' ';
                    file<<GenerateDirectOP(ins.count)<<", "<<GenerateMemRegisterOP(ctx.regs.frameReg);
                    file<<std::endl;
                }
                else if(ins.type == BFInstructionType::MINUS){
                    file<<'\t'<<GenerateInstruction(AssemblyInstruction::SUB, currentWidth)<<' ';
                    file<<GenerateDirectOP(ins.count)<<", "<<GenerateMemRegisterOP(ctx.regs.frameReg);
                    file<<std::endl;
                }
                else if(ins.type == BFInstructionType::LEFT){
                    file<<'\t'<<GenerateInstruction(AssemblyInstruction::ADD, Widths::Qword)<<' ';
                    file<<GenerateDirectOP(ins.count * GetMultiplier(currentWidth))<<", "<<GenerateRegisterOP(ctx.regs.frameReg);
                    file<<std::endl;
                }
                else if(ins.type == BFInstructionType::RIGHT){
                    file<<'\t'<<GenerateInstruction(AssemblyInstruction::SUB, Widths::Qword)<<' ';
                    file<<GenerateDirectOP(ins.count * GetMultiplier(currentWidth))<<", "<<GenerateRegisterOP(ctx.regs.frameReg);
                    file<<std::endl;
                }
                else if(ins.type == BFInstructionType::OUTPUT){
                    for(size_t c = 0; c < ins.count; c++){
                        if(ctx.regs.rax.synced == false || ctx.regs.rax.value != SYS_OUT_INDEX){
                            ctx.regs.rax.synced = true;
                            ctx.regs.rax.value = SYS_OUT_INDEX;
                            file<<'\t';
                            GenerateDirectToReg(file, SYS_OUT_INDEX, ctx.regs.rax)<<std::endl;
                        }
                        if(ctx.regs.rdi.synced == false || ctx.regs.rdi.value != SYS_OUT){
                            ctx.regs.rdi.synced = true;
                            ctx.regs.rdi.value = SYS_OUT;
                            file<<'\t';
                            GenerateDirectToReg(file, SYS_OUT, ctx.regs.rdi)<<std::endl;
                        }
                        if(ctx.regs.rsi.synced == false){
                            ctx.regs.rsi.synced = true;
                            // cannot guarantee value
                            file<<'\t';
                            file<<GenerateInstruction(AssemblyInstruction::MOV, Widths::Qword)<<' ';
                            file<<GenerateRegisterOP(ctx.regs.frameReg)<<", "<<GenerateRegisterOP(ctx.regs.rsi);
                            file<<std::endl;
                        }
                        if(ctx.regs.rdx.synced == false || ctx.regs.rdx.value != 1){
                            ctx.regs.rdx.synced = true;
                            ctx.regs.rdx.value = 1;
                            file<<'\t';
                            GenerateDirectToReg(file, 1, ctx.regs.rdx)<<std::endl;
                        }
                        file<<'\t'<<GenerateInstruction(AssemblyInstruction::SYSCALL, Widths::Byte)<<std::endl;
                        UnsyncRegister(ctx.regs.rcx);
                        UnsyncRegister(ctx.regs.r11);
                    }
                }
                else if(ins.type == BFInstructionType::ARGUMENT){
                    if(ins.count <= 6){
                        Register* reg;
                        switch(ins.count){
                            case 1:
                                reg = &ctx.regs.arg1;
                                break;
                            case 2:
                                reg = &ctx.regs.arg2;
                                break;
                            case 3:
                                reg = &ctx.regs.arg3;
                                break;
                            case 4:
                                reg = &ctx.regs.arg4;
                                break;
                            case 5:
                                reg = &ctx.regs.arg5;
                                break;
                            case 6:
                                reg = &ctx.regs.arg6;
                                break;
                            default:
                                reg = nullptr;
                                break;
                            }
                        if(reg == nullptr){
                            return;
                        }
                        UnsyncRegister(*reg);
                        file<<'\t'<<GenerateInstruction(AssemblyInstruction::MOV, currentWidth)<<' ';
                        if(!ins.address){
                            file<<GenerateMemRegisterOP(ctx.regs.frameReg)<<", ";
                        }
                        else{
                            file<<GenerateRegisterOP(ctx.regs.frameReg)<<", ";
                        }
                        file<<'%'<<GetRegisterWidth(*reg, currentWidth)<<std::endl;
                    }
                    else{
                        if(ins.address){
                            file<<'\t'<<GenerateInstruction(AssemblyInstruction::MOV, Widths::Qword)<<' ';
                            file<<GenerateRegisterOP(ctx.regs.frameReg)<<", ";
                        }
                        else{
                            UnsyncRegister(ctx.regs.rax);
                            file<<'\t'<<GenerateInstruction(AssemblyInstruction::MOV, currentWidth)<<' ';
                            file<<GenerateMemRegisterOP(ctx.regs.frameReg)<<", %"<<GetRegisterWidth(ctx.regs.rax, currentWidth)<<std::endl;
                            file<<'\t'<<GenerateInstruction(AssemblyInstruction::MOV, Widths::Qword)<<' ';
                            file<<GenerateRegisterOP(ctx.regs.rax)<<", ";
                        }
                        unsigned int offset = ins.count - 7;
                        if(offset > 0){
                            file<<offset * 8;
                        }
                        file<<GenerateMemRegisterOP(ctx.regs.stackReg)<<std::endl;
                    }
                }
                else if(ins.type == BFInstructionType::GETARG){
                    if(ins.count <= 6){
                        Register* reg;
                        switch(ins.count){
                            case 1:
                                reg = &ctx.regs.arg1;
                                break;
                            case 2:
                                reg = &ctx.regs.arg2;
                                break;
                            case 3:
                                reg = &ctx.regs.arg3;
                                break;
                            case 4:
                                reg = &ctx.regs.arg4;
                                break;
                            case 5:
                                reg = &ctx.regs.arg5;
                                break;
                            case 6:
                                reg = &ctx.regs.arg6;
                                break;
                            default:
                                reg = nullptr;
                                break;
                            }
                        if(reg == nullptr){
                            return;
                        }
                        file<<'\t'<<GenerateInstruction(AssemblyInstruction::MOV, currentWidth)<<' ';
                        file<<'%'<<GetRegisterWidth(*reg, currentWidth)<<", ";
                        if(ins.address){
                            file<<'%'<<GetRegisterWidth(ctx.regs.frameReg, currentWidth)<<std::endl;
                        }
                        else{
                            file<<GenerateMemRegisterOP(ctx.regs.frameReg)<<std::endl;
                        }
                    }
                    else{
                        std::cerr<<"Accepting stack arguments isnt available currently"<<std::endl;
                        // ugh i dont wanna
                    }
                }
                file<<"\t#\t";
                GenerateInstructionComment(file, ins);
                file<<std::endl;
            }
        }
        for(size_t r = 0; r < retsize; r++){
            FReturn& ret = ctx.rets[r];
            if(ret.pos == i){
                Label& lbl = ctx.labels[ret.label];
                if(lbl.type != Keyword::Void){
                    UnsyncRegister(ctx.regs.rax);
                    file<<'\t';
                    file<<GenerateInstruction(AssemblyInstruction::MOV, currentWidth)<<' ';
                    file<<GenerateMemRegisterOP(ctx.regs.frameReg)<<", %"<<GetRegisterWidth(ctx.regs.rax, currentWidth);
                    file<<std::endl;
                }
                file<<'\t'<<GetUJumpSyntax()<<' ';
                GenerateLabelEndName(lbl, file)<<std::endl;
            }
        }
        for(size_t l = 0; l < lblsize; l++){
            Label& lbl = ctx.labels[l];
            if(i == lbl.end){
                if(lbl.end != 0){
                    GenerateLabelEnd(lbl, file);
                    GenerateEpilogue(ctx, file, lbl)<<std::endl;
                }
                else{
                    labelEnd = true;
                    labelEndIndex = l;
                }
            }
            else if(i == lbl.pos){
                file<<'\t'<<AlignTo(4)<<std::endl;
                GenerateLabelName(lbl, file)<<":\n";
                GeneratePrologue(ctx, file)<<std::endl;
            }
        }
    }

    if(labelEnd){
        Label& lbl = ctx.labels[labelEndIndex];
        GenerateLabelEnd(lbl, file);
        GenerateEpilogue(ctx, file, lbl)<<std::endl;
    }

    file.close();
}

void RemoveLineComments(std::string& str, char symb){
    size_t len = str.size();

    bool incom = false;
    for(size_t i = 0; i < len; i++){

        if(str[i] == '\n') incom = false;

        if(incom){
            str[i] = ' ';
        }
        else{
            if(str[i] == symb){
                incom = true;
                str[i] = ' ';
            }
        }
    }
}

std::string GetFileExtension(std::string& fileName){
    unsigned int dotCount = 0;

    for(size_t i = 0; i < fileName.length(); i++){
        if(fileName[i] == '.') dotCount++;
    }
    if(dotCount == 0){
        return "";
    }
    std::string ext;
    for(size_t i = 0; i < fileName.length(); i++){
        if(fileName[i] == '.') dotCount--;
        if(dotCount == 0){
            ext += fileName[i];
        }
    }

    return ext;
}

void RemoveFileExtension(std::string& str){
    if(GetFileExtension(str).empty()){
        return;
    }
    while(true){
        if(str.back() == '.'){
            str.pop_back();
            return;
        }
        str.pop_back();
    }
}

enum class FileType{
    Assembly,
    Object,
};

enum class CLIState{
    Normal,
    Output,
    Assembler,
};

int main(int argc, char** argv){
    if(argc <= 1){
        std::cerr<<"bf++: error: no input files"<<std::endl;
        return 1;
    }
    std::string input;
    std::string output;
    FileType type = FileType::Assembly;
    std::string assembler;
    CLIState state = CLIState::Normal;

    for(int i = 1; i < argc; i++){
        if(state == CLIState::Normal){
            if((argv[i])[0] == '-'){
                std::string flag = argv[i];
                if(flag == "-o"){
                    state = CLIState::Output;
                }
                else if(flag == "-a" || flag == "--assembler"){
                    state = CLIState::Assembler;
                }
            }
            else{
                input = argv[i];
            }
        }
        else if(state == CLIState::Assembler){
            assembler = argv[i];
            state = CLIState::Normal;
        }
        else if(state == CLIState::Output){
            output = argv[i];
            state = CLIState::Normal;
        }
    }
    std::string ext = GetFileExtension(output);
    std::transform(ext.begin(), ext.end(), ext.begin(),
    [](unsigned char c){return std::tolower(c);});
    
    RemoveFileExtension(output);
    
    if(ext == ".s" || ext == ".asm"){
        type = FileType::Assembly;
    }
    else if(ext == ".o" || ext == ".obj"){
        type = FileType::Object;
    }
    else{
        std::cerr<<"bf++: error: Unknown file extension"<<std::endl;
        return 1;
    }

    if(type == FileType::Object){
        if(assembler.empty()){
            assembler = "as";
        }
        if(!CheckAvailable(assembler.c_str())){
            std::cerr<<"bf++: error: Assembler "<<assembler<<" not found"<<std::endl;
            return 1;
        }
    }

    BFPPKWD bfpp;
    BFPPRegisters regs;

    std::string file = FileIntoString(input.c_str());
    if(file.empty()){
        std::cout<<"bf++: error: File not found or empty"<<std::endl;
    }
    RemoveLineComments(file, ';');

    std::vector<Tokenizer::Token> toks = Tokenizer::Tokenize(file);
    ClassifyTokens(toks, bfpp);

    bool print = false;
    if(print){
        for(Tokenizer::Token& tok : toks){
            std::cout<<tok<<' ';
        }
        std::cout<<std::endl;
    }

    ParsedContext parsed = ParseTokensBFPP(toks, bfpp, regs);
    std::string asmout;
    if(type == FileType::Assembly){
        asmout = output + ext;
    }
    else{
        asmout = "__temp_bfpp_assembly__file.s";
    }
    BFPPCodegen(parsed, asmout.c_str());
    
    if(type == FileType::Object){
        std::string cmd = assembler + ' ';
        cmd += asmout + " -o ";
        cmd += output + ext;
        std::system(cmd.c_str());
        std::remove((asmout).c_str());
    }

    return 0;
}