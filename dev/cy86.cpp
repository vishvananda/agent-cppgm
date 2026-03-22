// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#define CPPGM_POSTTOKEN_INTERNAL_MAIN cy86_posttoken_internal_main
#define CPPGM_MACRO_MAIN_NAME cy86_macro_internal_main
#define CPPGM_PREPROC_MAIN_NAME cy86_preproc_internal_main
#include "preproc.cpp"
#undef CPPGM_PREPROC_MAIN_NAME
#undef CPPGM_MACRO_MAIN_NAME
#undef CPPGM_POSTTOKEN_INTERNAL_MAIN

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

struct ElfHeader
{
    unsigned char ident[16] =
    {
        0x7f, 'E', 'L', 'F',
        2,
        1,
        1,
        0,
        0,
        0, 0, 0, 0, 0, 0, 0
    };

    short int type = 2;
    short int machine = 0x3E;
    int version = 1;
    long int entry;

    long int phoff = 64;
    long int shoff = 0;

    int processor_flags = 0;
    short int ehsize = 64;
    short int phentsize = 56;
    short int phnum = 1;
    short int shentsize = 0;
    short int shnum = 0;
    short int shstrndx = 0;
};

struct ProgramSegmentHeader
{
    int type = 1;

    static constexpr int executable = 1 << 0;
    static constexpr int writable = 1 << 1;
    static constexpr int readable = 1 << 2;

    int flags = executable | writable | readable;

    long int offset = 0;
    long int vaddr = 0x400000;
    long int paddr = 0;
    long int filesz;
    long int memsz;
    long int align = 0;
};

extern "C" long int syscall(long int n, ...) throw ();

bool PA9SetFileExecutable(const string& path)
{
    return syscall(90, path.c_str(), 0755) == 0;
}

struct CYToken
{
    string term;
    string spell;
    bool is_identifier;
    bool is_literal;
    bool is_eof;
};

vector<CYToken> LexForPA9(const vector<PPToken>& tokens)
{
    vector<CYToken> out;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        const PPToken& tok = tokens[i];
        if (tok.kind == PP_IDENTIFIER)
        {
            out.push_back({"TT_IDENTIFIER", tok.data, true, false, false});
            continue;
        }
        if (tok.kind == PP_OP)
        {
            unordered_map<string, ETokenType>::const_iterator it = StringToTokenTypeMap.find(tok.data);
            if (it == StringToTokenTypeMap.end())
            {
                throw runtime_error("bad token");
            }
            out.push_back({TokenTypeToStringMap.at(it->second), tok.data, false, false, false});
            continue;
        }
        if (tok.kind == PP_NUMBER || tok.kind == PP_CHAR || tok.kind == PP_STRING)
        {
            out.push_back({"TT_LITERAL", tok.data, false, true, false});
            continue;
        }
        if (tok.kind == PP_UD_CHAR || tok.kind == PP_UD_STRING)
        {
            throw runtime_error("invalid token");
        }
        throw runtime_error("bad token");
    }
    out.push_back({"ST_EOF", "", false, false, true});
    return out;
}

enum XReg
{
    X_RAX = 0,
    X_RCX = 1,
    X_RDX = 2,
    X_RBX = 3,
    X_RSP = 4,
    X_RBP = 5,
    X_RSI = 6,
    X_RDI = 7,
    X_R8 = 8,
    X_R9 = 9,
    X_R10 = 10,
    X_R11 = 11,
    X_R12 = 12,
    X_R13 = 13,
    X_R14 = 14,
    X_R15 = 15
};

XReg MapCY86Register(const string& name)
{
    if (name == "sp") return X_RSP;
    if (name == "bp") return X_RBP;
    if (name == "x8" || name == "x16" || name == "x32" || name == "x64") return X_R12;
    if (name == "y8" || name == "y16" || name == "y32" || name == "y64") return X_R13;
    if (name == "z8" || name == "z16" || name == "z32" || name == "z64") return X_R14;
    if (name == "t8" || name == "t16" || name == "t32" || name == "t64") return X_R15;
    throw runtime_error("bad register");
}

int RegisterWidthBits(const string& name)
{
    if (name == "sp" || name == "bp") return 64;
    if (name.size() >= 2)
    {
        string suffix = name.substr(1);
        if (suffix == "8") return 8;
        if (suffix == "16") return 16;
        if (suffix == "32") return 32;
        if (suffix == "64") return 64;
    }
    throw runtime_error("bad register");
}

bool IsRegisterName(const string& name)
{
    static set<string> regs =
    {
        "sp", "bp",
        "x8", "x16", "x32", "x64",
        "y8", "y16", "y32", "y64",
        "z8", "z16", "z32", "z64",
        "t8", "t16", "t32", "t64"
    };
    return regs.count(name) != 0;
}

struct DecodedLiteral
{
    EFundamentalType type;
    vector<unsigned char> bytes;
    bool is_array;
    size_t elements;
    bool is_integral;
    bool is_signed_integral;
    bool is_floating;
};

int FundamentalWidthBytes(EFundamentalType type)
{
    switch (type)
    {
    case FT_SIGNED_CHAR:
    case FT_UNSIGNED_CHAR:
    case FT_CHAR:
    case FT_BOOL:
        return 1;
    case FT_SHORT_INT:
    case FT_UNSIGNED_SHORT_INT:
    case FT_CHAR16_T:
        return 2;
    case FT_INT:
    case FT_UNSIGNED_INT:
    case FT_FLOAT:
    case FT_CHAR32_T:
    case FT_WCHAR_T:
        return 4;
    case FT_LONG_INT:
    case FT_UNSIGNED_LONG_INT:
    case FT_LONG_LONG_INT:
    case FT_UNSIGNED_LONG_LONG_INT:
    case FT_DOUBLE:
        return 8;
    case FT_LONG_DOUBLE:
        return 10;
    default:
        throw runtime_error("bad literal type");
    }
}

int LiteralAlignmentBytes(const DecodedLiteral& lit)
{
    if (lit.is_array)
    {
        if (lit.type == FT_CHAR16_T) return 2;
        if (lit.type == FT_CHAR32_T || lit.type == FT_WCHAR_T) return 4;
        return 1;
    }
    return FundamentalWidthBytes(lit.type);
}

bool IsSignedIntegralType(EFundamentalType type)
{
    return type == FT_SIGNED_CHAR || type == FT_SHORT_INT || type == FT_INT ||
        type == FT_LONG_INT || type == FT_LONG_LONG_INT;
}

bool IsIntegralType(EFundamentalType type)
{
    return IsSignedIntegralType(type) ||
        type == FT_UNSIGNED_CHAR || type == FT_UNSIGNED_SHORT_INT ||
        type == FT_UNSIGNED_INT || type == FT_UNSIGNED_LONG_INT ||
        type == FT_UNSIGNED_LONG_LONG_INT || type == FT_CHAR ||
        type == FT_CHAR16_T || type == FT_CHAR32_T || type == FT_WCHAR_T ||
        type == FT_BOOL;
}

bool IsFloatingType(EFundamentalType type)
{
    return type == FT_FLOAT || type == FT_DOUBLE || type == FT_LONG_DOUBLE;
}

DecodedLiteral DecodeIntegerLiteral(const string& source)
{
    size_t ud_pos = source.find('_');
    string core = ud_pos == string::npos ? source : source.substr(0, ud_pos);
    string suffix = ud_pos == string::npos ? "" : source.substr(ud_pos);
    if (!suffix.empty())
    {
        throw runtime_error("invalid token");
    }

    size_t pos = 0;
    int base = 10;
    if (core.size() >= 2 && core[0] == '0' && (core[1] == 'x' || core[1] == 'X'))
    {
        base = 16;
        pos = 2;
    }
    else if (core.size() > 1 && core[0] == '0' && core[1] >= '0' && core[1] <= '7')
    {
        base = 8;
        pos = 1;
    }

    size_t digit_end = pos;
    while (digit_end < core.size())
    {
        int c = core[digit_end];
        bool ok = base == 16 ? IsHexDigit(c) : (base == 8 ? IsOctDigit(c) : (c >= '0' && c <= '9'));
        if (!ok) break;
        ++digit_end;
    }
    if (digit_end == pos)
    {
        throw runtime_error("invalid token");
    }
    string std_suffix = core.substr(digit_end);

    bool is_unsigned = false;
    int long_count = 0;
    if (std_suffix.empty())
    {
    }
    else if (std_suffix == "u" || std_suffix == "U")
    {
        is_unsigned = true;
    }
    else if (std_suffix == "l" || std_suffix == "L")
    {
        long_count = 1;
    }
    else if (std_suffix == "ll" || std_suffix == "LL")
    {
        long_count = 2;
    }
    else if (std_suffix == "ul" || std_suffix == "uL" || std_suffix == "Ul" || std_suffix == "UL" ||
        std_suffix == "lu" || std_suffix == "lU" || std_suffix == "Lu" || std_suffix == "LU")
    {
        is_unsigned = true;
        long_count = 1;
    }
    else if (std_suffix == "ull" || std_suffix == "uLL" || std_suffix == "Ull" || std_suffix == "ULL" ||
        std_suffix == "llu" || std_suffix == "llU" || std_suffix == "LLu" || std_suffix == "LLU")
    {
        is_unsigned = true;
        long_count = 2;
    }
    else
    {
        throw runtime_error("invalid token");
    }

    unsigned __int128 value = 0;
    for (size_t i = pos; i < digit_end; ++i)
    {
        int d = base == 16 ? HexCharToValue(core[i]) : core[i] - '0';
        value = value * base + static_cast<unsigned>(d);
    }

    const unsigned __int128 I_MAX = 0x7FFFFFFFu;
    const unsigned __int128 UI_MAX = 0xFFFFFFFFu;
    const unsigned __int128 L_MAX = 0x7FFFFFFFFFFFFFFFull;
    const unsigned __int128 UL_MAX = 0xFFFFFFFFFFFFFFFFull;
    vector<pair<EFundamentalType, unsigned __int128> > candidates;

    if (!is_unsigned && long_count == 0)
    {
        candidates.push_back(make_pair(FT_INT, I_MAX));
        if (base != 10) candidates.push_back(make_pair(FT_UNSIGNED_INT, UI_MAX));
        candidates.push_back(make_pair(FT_LONG_INT, L_MAX));
        if (base != 10) candidates.push_back(make_pair(FT_UNSIGNED_LONG_INT, UL_MAX));
        candidates.push_back(make_pair(FT_LONG_LONG_INT, L_MAX));
        if (base != 10) candidates.push_back(make_pair(FT_UNSIGNED_LONG_LONG_INT, UL_MAX));
    }
    else if (!is_unsigned && long_count == 1)
    {
        candidates.push_back(make_pair(FT_LONG_INT, L_MAX));
        if (base != 10) candidates.push_back(make_pair(FT_UNSIGNED_LONG_INT, UL_MAX));
        candidates.push_back(make_pair(FT_LONG_LONG_INT, L_MAX));
        if (base != 10) candidates.push_back(make_pair(FT_UNSIGNED_LONG_LONG_INT, UL_MAX));
    }
    else if (!is_unsigned && long_count == 2)
    {
        candidates.push_back(make_pair(FT_LONG_LONG_INT, L_MAX));
        if (base != 10) candidates.push_back(make_pair(FT_UNSIGNED_LONG_LONG_INT, UL_MAX));
    }
    else if (is_unsigned && long_count == 0)
    {
        candidates.push_back(make_pair(FT_UNSIGNED_INT, UI_MAX));
        candidates.push_back(make_pair(FT_UNSIGNED_LONG_INT, UL_MAX));
        candidates.push_back(make_pair(FT_UNSIGNED_LONG_LONG_INT, UL_MAX));
    }
    else if (is_unsigned && long_count == 1)
    {
        candidates.push_back(make_pair(FT_UNSIGNED_LONG_INT, UL_MAX));
        candidates.push_back(make_pair(FT_UNSIGNED_LONG_LONG_INT, UL_MAX));
    }
    else
    {
        candidates.push_back(make_pair(FT_UNSIGNED_LONG_LONG_INT, UL_MAX));
    }

    DecodedLiteral out;
    out.is_array = false;
    out.elements = 0;
    out.is_floating = false;

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (value <= candidates[i].second)
        {
            out.type = candidates[i].first;
            out.is_integral = true;
            out.is_signed_integral = IsSignedIntegralType(out.type);
            size_t width = FundamentalWidthBytes(out.type);
            out.bytes.resize(width, 0);
            for (size_t j = 0; j < width; ++j)
            {
                out.bytes[j] = static_cast<unsigned char>((value >> (j * 8)) & 0xFF);
            }
            return out;
        }
    }

    throw runtime_error("invalid token");
}

DecodedLiteral DecodeFloatingLiteral(const string& source)
{
    if (source.size() >= 2 && source[0] == '0' && (source[1] == 'x' || source[1] == 'X'))
    {
        throw runtime_error("invalid token");
    }
    if (source.find('.') == string::npos && source.find('e') == string::npos && source.find('E') == string::npos)
    {
        throw runtime_error("invalid token");
    }

    string s = source;
    size_t underscore = s.find('_');
    if (underscore != string::npos)
    {
        throw runtime_error("invalid token");
    }
    char suffix = 0;
    if (!s.empty() && (s[s.size() - 1] == 'f' || s[s.size() - 1] == 'F' || s[s.size() - 1] == 'l' || s[s.size() - 1] == 'L'))
    {
        suffix = s[s.size() - 1];
        s.erase(s.size() - 1);
    }
    if (s.empty())
    {
        throw runtime_error("invalid token");
    }

    DecodedLiteral out;
    out.is_array = false;
    out.elements = 0;
    out.is_integral = false;
    out.is_signed_integral = false;
    out.is_floating = true;

    if (suffix == 'f' || suffix == 'F')
    {
        float v = PA2Decode_float(s);
        out.type = FT_FLOAT;
        out.bytes.resize(4);
        memcpy(out.bytes.data(), &v, 4);
    }
    else if (suffix == 'l' || suffix == 'L')
    {
        long double v = PA2Decode_long_double(s);
        out.type = FT_LONG_DOUBLE;
        out.bytes.resize(10);
        memcpy(out.bytes.data(), &v, 10);
    }
    else
    {
        double v = PA2Decode_double(s);
        out.type = FT_DOUBLE;
        out.bytes.resize(8);
        memcpy(out.bytes.data(), &v, 8);
    }
    return out;
}

DecodedLiteral DecodeLiteralToken(const string& token)
{
    if (!token.empty() && token[token.size() - 1] == '"')
    {
        EFundamentalType type;
        vector<unsigned char> bytes;
        size_t elements = 0;
        if (!ParseStringLiteralCore(token, type, bytes, elements))
        {
            throw runtime_error("invalid token");
        }
        DecodedLiteral out;
        out.type = type;
        out.bytes = bytes;
        out.is_array = true;
        out.elements = elements;
        out.is_integral = false;
        out.is_signed_integral = false;
        out.is_floating = false;
        return out;
    }
    if (!token.empty() && token[token.size() - 1] == '\'')
    {
        EFundamentalType type;
        vector<unsigned char> bytes;
        if (!ParseCharacterLiteralCore(token, type, bytes))
        {
            throw runtime_error("invalid token");
        }
        DecodedLiteral out;
        out.type = type;
        out.bytes = bytes;
        out.is_array = false;
        out.elements = 0;
        out.is_integral = IsIntegralType(type);
        out.is_signed_integral = IsSignedIntegralType(type);
        out.is_floating = false;
        return out;
    }
    if (token.find('.') != string::npos || token.find('e') != string::npos || token.find('E') != string::npos)
    {
        return DecodeFloatingLiteral(token);
    }
    return DecodeIntegerLiteral(token);
}

DecodedLiteral NegateLiteral(const DecodedLiteral& lit)
{
    if (!lit.is_integral && !lit.is_floating)
    {
        throw runtime_error("invalid negation");
    }
    DecodedLiteral out = lit;
    if (lit.is_floating)
    {
        if (lit.type == FT_FLOAT)
        {
            float v;
            memcpy(&v, lit.bytes.data(), 4);
            v = -v;
            memcpy(out.bytes.data(), &v, 4);
        }
        else if (lit.type == FT_DOUBLE)
        {
            double v;
            memcpy(&v, lit.bytes.data(), 8);
            v = -v;
            memcpy(out.bytes.data(), &v, 8);
        }
        else
        {
            long double v = 0;
            memcpy(&v, lit.bytes.data(), 10);
            v = -v;
            memcpy(out.bytes.data(), &v, 10);
        }
        return out;
    }

    unsigned __int128 value = 0;
    for (size_t i = 0; i < lit.bytes.size(); ++i)
    {
        value |= (static_cast<unsigned __int128>(lit.bytes[i]) << (8 * i));
    }
    unsigned __int128 mask = lit.bytes.size() >= 16 ? ~static_cast<unsigned __int128>(0) :
        ((static_cast<unsigned __int128>(1) << (8 * lit.bytes.size())) - 1);
    value = ((~value) + 1) & mask;
    for (size_t i = 0; i < lit.bytes.size(); ++i)
    {
        out.bytes[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xFF);
    }
    return out;
}

uint64_t CoerceLiteralToWidth(const DecodedLiteral& lit, int width_bits)
{
    int width = width_bits / 8;
    vector<unsigned char> bytes = lit.bytes;
    if (static_cast<int>(bytes.size()) > width)
    {
        bytes.resize(width);
    }
    else if (static_cast<int>(bytes.size()) < width)
    {
        unsigned char fill = 0;
        if (!lit.is_array && lit.is_integral && lit.is_signed_integral && !bytes.empty() && (bytes.back() & 0x80) != 0)
        {
            fill = 0xFF;
        }
        bytes.resize(width, fill);
    }

    uint64_t out = 0;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        out |= (static_cast<uint64_t>(bytes[i]) << (8 * i));
    }
    return out;
}

int64_t SignExtend(uint64_t value, int width_bits)
{
    if (width_bits == 64) return static_cast<int64_t>(value);
    uint64_t sign_bit = static_cast<uint64_t>(1) << (width_bits - 1);
    uint64_t mask = (static_cast<uint64_t>(1) << width_bits) - 1;
    value &= mask;
    if ((value & sign_bit) != 0)
    {
        value |= ~mask;
    }
    return static_cast<int64_t>(value);
}

struct ImmediateExpr
{
    enum Kind
    {
        IMM_LITERAL,
        IMM_LABEL
    } kind;

    DecodedLiteral literal;
    string label;
    bool has_offset;
    DecodedLiteral offset_literal;
    bool offset_negative;

    ImmediateExpr() : kind(IMM_LITERAL), has_offset(false), offset_negative(false) {}
};

struct MemoryExpr
{
    enum Kind
    {
        MEM_REG,
        MEM_LITERAL,
        MEM_LABEL
    } kind;

    string reg;
    DecodedLiteral literal;
    string label;
    bool has_offset;
    DecodedLiteral offset_literal;
    bool offset_negative;

    MemoryExpr() : kind(MEM_REG), has_offset(false), offset_negative(false) {}
};

struct Operand
{
    enum Kind
    {
        OP_REGISTER,
        OP_IMMEDIATE,
        OP_MEMORY
    } kind;

    string reg;
    ImmediateExpr imm;
    MemoryExpr mem;
};

struct Statement
{
    vector<string> labels;
    bool is_literal;
    DecodedLiteral literal;
    string opcode;
    vector<Operand> operands;
    uint64_t offset;

    Statement() : is_literal(false), offset(0) {}
};

struct Parser
{
    const vector<CYToken>& tokens;
    size_t pos;

    Parser(const vector<CYToken>& t) : tokens(t), pos(0) {}

    const CYToken& Peek(size_t offset = 0) const
    {
        if (pos + offset >= tokens.size()) return tokens.back();
        return tokens[pos + offset];
    }

    bool Accept(const string& term)
    {
        if (Peek().term != term) return false;
        ++pos;
        return true;
    }

    void Expect(const string& term)
    {
        if (!Accept(term))
        {
            throw runtime_error("expected " + term);
        }
    }

    string ExpectIdentifier()
    {
        if (!Peek().is_identifier)
        {
            throw runtime_error("expected identifier");
        }
        return tokens[pos++].spell;
    }

    DecodedLiteral ExpectLiteral()
    {
        if (!Peek().is_literal)
        {
            throw runtime_error("expected literal");
        }
        return DecodeLiteralToken(tokens[pos++].spell);
    }

    ImmediateExpr ParseImmediateCore(bool allow_paren)
    {
        ImmediateExpr out;
        if (allow_paren && Accept("OP_LPAREN"))
        {
            out = ParseImmediateCore(false);
            Expect("OP_RPAREN");
            return out;
        }
        if (Accept("OP_MINUS"))
        {
            DecodedLiteral lit = ExpectLiteral();
            out.kind = ImmediateExpr::IMM_LITERAL;
            out.literal = NegateLiteral(lit);
            return out;
        }
        if (Peek().is_literal)
        {
            out.kind = ImmediateExpr::IMM_LITERAL;
            out.literal = ExpectLiteral();
            return out;
        }
        if (!Peek().is_identifier)
        {
            throw runtime_error("expected immediate");
        }
        out.kind = ImmediateExpr::IMM_LABEL;
        out.label = ExpectIdentifier();
        if (Accept("OP_PLUS"))
        {
            out.has_offset = true;
            out.offset_negative = false;
            out.offset_literal = ExpectLiteral();
        }
        else if (Accept("OP_MINUS"))
        {
            out.has_offset = true;
            out.offset_negative = true;
            out.offset_literal = ExpectLiteral();
        }
        return out;
    }

    Operand ParseOperand()
    {
        Operand out;
        if (Accept("OP_LSQUARE"))
        {
            out.kind = Operand::OP_MEMORY;
            if (Peek().is_literal)
            {
                out.mem.kind = MemoryExpr::MEM_LITERAL;
                out.mem.literal = ExpectLiteral();
            }
            else if (Peek().is_identifier && IsRegisterName(Peek().spell))
            {
                out.mem.kind = MemoryExpr::MEM_REG;
                out.mem.reg = ExpectIdentifier();
            }
            else if (Peek().is_identifier)
            {
                out.mem.kind = MemoryExpr::MEM_LABEL;
                out.mem.label = ExpectIdentifier();
            }
            else
            {
                throw runtime_error("bad memory operand");
            }
            if (Accept("OP_PLUS"))
            {
                out.mem.has_offset = true;
                out.mem.offset_negative = false;
                out.mem.offset_literal = ExpectLiteral();
            }
            else if (Accept("OP_MINUS"))
            {
                out.mem.has_offset = true;
                out.mem.offset_negative = true;
                out.mem.offset_literal = ExpectLiteral();
            }
            Expect("OP_RSQUARE");
            return out;
        }

        if (Peek().is_identifier && IsRegisterName(Peek().spell))
        {
            out.kind = Operand::OP_REGISTER;
            out.reg = ExpectIdentifier();
            return out;
        }

        out.kind = Operand::OP_IMMEDIATE;
        out.imm = ParseImmediateCore(true);
        return out;
    }

    Statement ParseStatement()
    {
        Statement out;
        while (Peek().is_identifier && Peek(1).term == "OP_COLON")
        {
            out.labels.push_back(ExpectIdentifier());
            Expect("OP_COLON");
        }

        if (Peek().is_literal || Peek().term == "OP_MINUS")
        {
            out.is_literal = true;
            if (Accept("OP_MINUS"))
            {
                out.literal = NegateLiteral(ExpectLiteral());
            }
            else
            {
                out.literal = ExpectLiteral();
            }
            return out;
        }

        out.opcode = ExpectIdentifier();
        while (Peek().term != "OP_SEMICOLON")
        {
            out.operands.push_back(ParseOperand());
        }
        return out;
    }

    vector<Statement> ParseProgram()
    {
        vector<Statement> out;
        while (!Peek().is_eof)
        {
            Statement stmt = ParseStatement();
            Expect("OP_SEMICOLON");
            out.push_back(stmt);
        }
        return out;
    }
};

string TrailingDigits(const string& s)
{
    size_t i = s.size();
    while (i > 0 && isdigit(static_cast<unsigned char>(s[i - 1]))) --i;
    return s.substr(i);
}

int ParseTrailingWidth(const string& s)
{
    string digits = TrailingDigits(s);
    if (digits.empty()) return 0;
    return atoi(digits.c_str());
}

struct CodeBuffer
{
    vector<unsigned char> data;

    size_t size() const { return data.size(); }

    void u8(unsigned char v) { data.push_back(v); }

    void u16(uint16_t v)
    {
        data.push_back(static_cast<unsigned char>(v & 0xFF));
        data.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
    }

    void u32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i) data.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
    }

    void u64(uint64_t v)
    {
        for (int i = 0; i < 8; ++i) data.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
    }

    void bytes(const vector<unsigned char>& v)
    {
        data.insert(data.end(), v.begin(), v.end());
    }

    void patch32(size_t pos, int32_t value)
    {
        for (int i = 0; i < 4; ++i)
        {
            data[pos + i] = static_cast<unsigned char>((static_cast<uint32_t>(value) >> (8 * i)) & 0xFF);
        }
    }
};

struct X86Emitter
{
    CodeBuffer& out;

    explicit X86Emitter(CodeBuffer& o) : out(o) {}

    void Rex(bool w, int reg, int index, int base, bool force = false)
    {
        unsigned char rex = 0x40;
        if (w) rex |= 0x08;
        if ((reg & 8) != 0) rex |= 0x04;
        if ((index & 8) != 0) rex |= 0x02;
        if ((base & 8) != 0) rex |= 0x01;
        if (force || rex != 0x40) out.u8(rex);
    }

    void ModRM(int mod, int reg, int rm)
    {
        out.u8(static_cast<unsigned char>((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
    }

    void SIB(int scale, int index, int base)
    {
        out.u8(static_cast<unsigned char>((scale << 6) | ((index & 7) << 3) | (base & 7)));
    }

    void Mem(int reg_field, XReg base, long long disp)
    {
        int rm = static_cast<int>(base) & 7;
        bool need_sib = rm == 4;
        bool force_disp = rm == 5;
        int mod = 0;
        if (disp == 0 && !force_disp)
        {
            mod = 0;
        }
        else if (disp >= -128 && disp <= 127)
        {
            mod = 1;
        }
        else
        {
            mod = 2;
        }
        ModRM(mod, reg_field, need_sib ? 4 : rm);
        if (need_sib)
        {
            SIB(0, 4, rm);
        }
        if (mod == 1)
        {
            out.u8(static_cast<unsigned char>(disp & 0xFF));
        }
        else if (mod == 2 || (mod == 0 && force_disp))
        {
            out.u32(static_cast<uint32_t>(disp));
        }
    }

    void MovImm64(XReg dst, uint64_t imm)
    {
        Rex(true, 0, 0, dst);
        out.u8(static_cast<unsigned char>(0xB8 + (dst & 7)));
        out.u64(imm);
    }

    void MovRegReg(int width, XReg dst, XReg src)
    {
        if (width == 16) out.u8(0x66);
        Rex(width == 64, src, 0, dst, width == 8 && (src >= 8 || dst >= 8));
        out.u8(width == 8 ? 0x88 : 0x89);
        ModRM(3, src, dst);
    }

    void MovRegFromReg(int width, XReg dst, XReg src)
    {
        if (width == 16) out.u8(0x66);
        Rex(width == 64, dst, 0, src, width == 8 && (dst >= 8 || src >= 8));
        out.u8(width == 8 ? 0x8A : 0x8B);
        ModRM(3, dst, src);
    }

    void MovMemReg(int width, XReg base, long long disp, XReg src)
    {
        if (width == 16) out.u8(0x66);
        Rex(width == 64, src, 0, base, width == 8 && src >= 8);
        out.u8(width == 8 ? 0x88 : 0x89);
        Mem(src, base, disp);
    }

    void MovRegMem(int width, XReg dst, XReg base, long long disp)
    {
        if (width == 16) out.u8(0x66);
        Rex(width == 64, dst, 0, base, width == 8 && dst >= 8);
        out.u8(width == 8 ? 0x8A : 0x8B);
        Mem(dst, base, disp);
    }

    void MovZXRegReg(int src_width, XReg dst, XReg src)
    {
        Rex(false, dst, 0, src, src >= 8 || dst >= 8);
        out.u8(0x0F);
        out.u8(src_width == 8 ? 0xB6 : 0xB7);
        ModRM(3, dst, src);
    }

    void MovZXRegMem(int src_width, XReg dst, XReg base, long long disp)
    {
        Rex(false, dst, 0, base);
        out.u8(0x0F);
        out.u8(src_width == 8 ? 0xB6 : 0xB7);
        Mem(dst, base, disp);
    }

    void MovSXRegReg(int src_width, XReg dst, XReg src)
    {
        Rex(true, dst, 0, src, src >= 8 || dst >= 8);
        out.u8(0x0F);
        out.u8(src_width == 8 ? 0xBE : 0xBF);
        ModRM(3, dst, src);
    }

    void MovSXRegMem(int src_width, XReg dst, XReg base, long long disp)
    {
        Rex(true, dst, 0, base);
        out.u8(0x0F);
        out.u8(src_width == 8 ? 0xBE : 0xBF);
        Mem(dst, base, disp);
    }

    void MovSXDRegReg(XReg dst, XReg src)
    {
        Rex(true, dst, 0, src);
        out.u8(0x63);
        ModRM(3, dst, src);
    }

    void MovSXDRegMem(XReg dst, XReg base, long long disp)
    {
        Rex(true, dst, 0, base);
        out.u8(0x63);
        Mem(dst, base, disp);
    }

    void BinRegReg(unsigned char opcode, XReg dst, XReg src)
    {
        Rex(true, src, 0, dst);
        out.u8(opcode);
        ModRM(3, src, dst);
    }

    void AddRegReg(XReg dst, XReg src) { BinRegReg(0x01, dst, src); }
    void OrRegReg(XReg dst, XReg src) { BinRegReg(0x09, dst, src); }
    void AndRegReg(XReg dst, XReg src) { BinRegReg(0x21, dst, src); }
    void SubRegReg(XReg dst, XReg src) { BinRegReg(0x29, dst, src); }
    void XorRegReg(XReg dst, XReg src) { BinRegReg(0x31, dst, src); }
    void CmpRegReg(XReg dst, XReg src) { BinRegReg(0x39, dst, src); }

    void AddRegImm32(XReg dst, int32_t imm)
    {
        Rex(true, 0, 0, dst);
        out.u8(0x81);
        ModRM(3, 0, dst);
        out.u32(static_cast<uint32_t>(imm));
    }

    void TestReg8(XReg reg)
    {
        Rex(false, reg, 0, reg, reg >= 8);
        out.u8(0x84);
        ModRM(3, reg, reg);
    }

    void TestReg64(XReg reg)
    {
        Rex(true, reg, 0, reg);
        out.u8(0x85);
        ModRM(3, reg, reg);
    }

    void NotReg(XReg reg)
    {
        Rex(true, 0, 0, reg);
        out.u8(0xF7);
        ModRM(3, 2, reg);
    }

    void IMulRegReg(XReg dst, XReg src)
    {
        Rex(true, dst, 0, src);
        out.u8(0x0F);
        out.u8(0xAF);
        ModRM(3, dst, src);
    }

    void CQO()
    {
        out.u8(0x48);
        out.u8(0x99);
    }

    void DivReg(XReg src, bool is_signed)
    {
        Rex(true, 0, 0, src);
        out.u8(0xF7);
        ModRM(3, is_signed ? 7 : 6, src);
    }

    void ShiftRegCL(XReg reg, int kind)
    {
        Rex(true, 0, 0, reg);
        out.u8(0xD3);
        ModRM(3, kind, reg);
    }

    void SetCC(unsigned char cc, XReg dst)
    {
        Rex(false, 0, 0, dst, dst >= 8);
        out.u8(0x0F);
        out.u8(static_cast<unsigned char>(0x90 + cc));
        ModRM(3, 0, dst);
    }

    size_t Jcc(unsigned char cc)
    {
        out.u8(0x0F);
        out.u8(static_cast<unsigned char>(0x80 + cc));
        size_t pos = out.size();
        out.u32(0);
        return pos;
    }

    size_t JmpRel32()
    {
        out.u8(0xE9);
        size_t pos = out.size();
        out.u32(0);
        return pos;
    }

    void PatchRel32(size_t disp_pos, size_t target)
    {
        int32_t disp = static_cast<int32_t>(target) - static_cast<int32_t>(disp_pos + 4);
        out.patch32(disp_pos, disp);
    }

    void JmpReg(XReg reg)
    {
        Rex(false, 0, 0, reg);
        out.u8(0xFF);
        ModRM(3, 4, reg);
    }

    void CallReg(XReg reg)
    {
        Rex(false, 0, 0, reg);
        out.u8(0xFF);
        ModRM(3, 2, reg);
    }

    void Ret() { out.u8(0xC3); }
    void Syscall() { out.u8(0x0F); out.u8(0x05); }

    void FldMem(int width, XReg base, long long disp)
    {
        if (width == 4)
        {
            Rex(false, 0, 0, base);
            out.u8(0xD9);
            Mem(0, base, disp);
        }
        else if (width == 8)
        {
            Rex(false, 0, 0, base);
            out.u8(0xDD);
            Mem(0, base, disp);
        }
        else
        {
            Rex(false, 0, 0, base);
            out.u8(0xDB);
            Mem(5, base, disp);
        }
    }

    void FstpMem(int width, XReg base, long long disp)
    {
        if (width == 4)
        {
            Rex(false, 0, 0, base);
            out.u8(0xD9);
            Mem(3, base, disp);
        }
        else if (width == 8)
        {
            Rex(false, 0, 0, base);
            out.u8(0xDD);
            Mem(3, base, disp);
        }
        else
        {
            Rex(false, 0, 0, base);
            out.u8(0xDB);
            Mem(7, base, disp);
        }
    }

    void FildMem(int width, XReg base, long long disp)
    {
        Rex(false, 0, 0, base);
        if (width == 2)
        {
            out.u8(0xDF);
            Mem(0, base, disp);
        }
        else if (width == 4)
        {
            out.u8(0xDB);
            Mem(0, base, disp);
        }
        else
        {
            out.u8(0xDF);
            Mem(5, base, disp);
        }
    }

    void FistpMem(int width, XReg base, long long disp)
    {
        Rex(false, 0, 0, base);
        if (width == 2)
        {
            out.u8(0xDF);
            Mem(3, base, disp);
        }
        else if (width == 4)
        {
            out.u8(0xDB);
            Mem(3, base, disp);
        }
        else
        {
            out.u8(0xDF);
            Mem(7, base, disp);
        }
    }

    void FAddP() { out.u8(0xDE); out.u8(0xC1); }
    void FSubP() { out.u8(0xDE); out.u8(0xE9); }
    void FMulP() { out.u8(0xDE); out.u8(0xC9); }
    void FDivP() { out.u8(0xDE); out.u8(0xF9); }
    void FUComIP() { out.u8(0xDF); out.u8(0xE9); }
    void FstpST0() { out.u8(0xDD); out.u8(0xD8); }
    void Fld1() { out.u8(0xD9); out.u8(0xE8); }
};

struct CY86Compiler
{
    vector<Statement> statements;
    map<string, uint64_t> label_offsets;
    uint64_t program_size;
    uint64_t support_two63_offset;
    vector<unsigned char> support_two63;

    CY86Compiler() : program_size(0), support_two63_offset(0) {}

    static bool IsDataOpcode(const string& op)
    {
        return op == "data8" || op == "data16" || op == "data32" || op == "data64";
    }

    static bool IsFloatBinary(const string& op)
    {
        return op.rfind("fadd", 0) == 0 || op.rfind("fsub", 0) == 0 || op.rfind("fmul", 0) == 0 ||
            op.rfind("fdiv", 0) == 0 || op.rfind("feq", 0) == 0 || op.rfind("fne", 0) == 0 ||
            op.rfind("flt", 0) == 0 || op.rfind("fgt", 0) == 0 || op.rfind("fle", 0) == 0 ||
            op.rfind("fge", 0) == 0;
    }

    static bool IsIntegerCompare(const string& op)
    {
        return op.rfind("ieq", 0) == 0 || op.rfind("ine", 0) == 0 ||
            op.rfind("slt", 0) == 0 || op.rfind("ult", 0) == 0 ||
            op.rfind("sgt", 0) == 0 || op.rfind("ugt", 0) == 0 ||
            op.rfind("sle", 0) == 0 || op.rfind("ule", 0) == 0 ||
            op.rfind("sge", 0) == 0 || op.rfind("uge", 0) == 0;
    }

    static uint64_t AlignTo(uint64_t value, int align)
    {
        if (align <= 1) return value;
        uint64_t mask = static_cast<uint64_t>(align - 1);
        return (value + mask) & ~mask;
    }

    void PrepareSupportData()
    {
        long double v = 9223372036854775808.0L;
        support_two63.resize(10);
        memcpy(support_two63.data(), &v, 10);
    }

    uint64_t StatementSize(const Statement& stmt)
    {
        if (stmt.is_literal)
        {
            uint64_t start = AlignTo(0, LiteralAlignmentBytes(stmt.literal));
            return start + stmt.literal.bytes.size();
        }
        if (IsDataOpcode(stmt.opcode))
        {
            int width = ParseTrailingWidth(stmt.opcode) / 8;
            uint64_t start = AlignTo(0, width);
            return start + width;
        }

        CodeBuffer tmp;
        EmitInstruction(stmt, tmp, label_offsets, 0, 0);
        return tmp.size();
    }

    void AssignOffsets()
    {
        PrepareSupportData();
        for (size_t i = 0; i < statements.size(); ++i)
        {
            for (size_t j = 0; j < statements[i].labels.size(); ++j)
            {
                label_offsets[statements[i].labels[j]] = 0;
            }
        }
        uint64_t cur = 0;
        for (size_t i = 0; i < statements.size(); ++i)
        {
            Statement& stmt = statements[i];
            if (stmt.is_literal)
            {
                cur = AlignTo(cur, LiteralAlignmentBytes(stmt.literal));
            }
            else if (IsDataOpcode(stmt.opcode))
            {
                cur = AlignTo(cur, ParseTrailingWidth(stmt.opcode) / 8);
            }
            stmt.offset = cur;
            for (size_t j = 0; j < stmt.labels.size(); ++j)
            {
                label_offsets[stmt.labels[j]] = cur;
            }
            cur += StatementSize(stmt);
        }
        support_two63_offset = AlignTo(cur, 16);
        program_size = support_two63_offset + support_two63.size();
    }

    uint64_t LabelAddress(const map<string, uint64_t>& labels, const string& label, uint64_t image_base) const
    {
        map<string, uint64_t>::const_iterator it = labels.find(label);
        if (it == labels.end())
        {
            throw runtime_error("unknown label: " + label);
        }
        return image_base + it->second;
    }

    uint64_t ImmediateValue(const ImmediateExpr& imm, int width_bits, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        uint64_t base = 0;
        if (imm.kind == ImmediateExpr::IMM_LITERAL)
        {
            base = CoerceLiteralToWidth(imm.literal, width_bits);
        }
        else
        {
            base = LabelAddress(labels, imm.label, image_base);
        }

        if (!imm.has_offset) return base;
        if (!imm.offset_literal.is_integral)
        {
            throw runtime_error("non-integral offset");
        }
        uint64_t raw = CoerceLiteralToWidth(imm.offset_literal, 64);
        int64_t signed_offset = imm.offset_literal.is_signed_integral ? SignExtend(raw, min(64, static_cast<int>(imm.offset_literal.bytes.size() * 8))) : static_cast<int64_t>(raw);
        if (imm.offset_negative) signed_offset = -signed_offset;
        return static_cast<uint64_t>(static_cast<int64_t>(base) + signed_offset);
    }

    uint64_t MemoryBaseAddress(const MemoryExpr& mem, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        if (mem.kind == MemoryExpr::MEM_LITERAL)
        {
            return CoerceLiteralToWidth(mem.literal, 64);
        }
        if (mem.kind == MemoryExpr::MEM_LABEL)
        {
            return LabelAddress(labels, mem.label, image_base);
        }
        throw runtime_error("memory address is register-based");
    }

    uint64_t MemoryOffsetValue(const MemoryExpr& mem) const
    {
        if (!mem.has_offset) return 0;
        if (!mem.offset_literal.is_integral) throw runtime_error("non-integral offset");
        uint64_t raw = CoerceLiteralToWidth(mem.offset_literal, 64);
        int64_t signed_offset = mem.offset_literal.is_signed_integral ? SignExtend(raw, min(64, static_cast<int>(mem.offset_literal.bytes.size() * 8))) : static_cast<int64_t>(raw);
        if (mem.offset_negative) signed_offset = -signed_offset;
        return static_cast<uint64_t>(signed_offset);
    }

    void EmitLoadAddress(X86Emitter& x, const MemoryExpr& mem, XReg dst, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        if (mem.kind == MemoryExpr::MEM_REG)
        {
            x.MovRegFromReg(64, dst, MapCY86Register(mem.reg));
        }
        else
        {
            x.MovImm64(dst, MemoryBaseAddress(mem, labels, image_base));
        }
        if (mem.has_offset)
        {
            x.AddRegImm32(dst, static_cast<int32_t>(MemoryOffsetValue(mem)));
        }
    }

    void EmitLoadRawOperand(X86Emitter& x, const Operand& op, int width_bits, XReg dst, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        int width = width_bits / 8;
        if (op.kind == Operand::OP_IMMEDIATE)
        {
            x.MovImm64(dst, ImmediateValue(op.imm, width_bits, labels, image_base));
            return;
        }
        if (op.kind == Operand::OP_REGISTER)
        {
            XReg src = MapCY86Register(op.reg);
            if (width_bits == 64) x.MovRegFromReg(64, dst, src);
            else if (width_bits == 32) x.MovRegFromReg(32, dst, src);
            else if (width_bits == 16) x.MovZXRegReg(16, dst, src);
            else x.MovZXRegReg(8, dst, src);
            return;
        }

        EmitLoadAddress(x, op.mem, X_R11, labels, image_base);
        if (width_bits == 64) x.MovRegMem(64, dst, X_R11, 0);
        else if (width_bits == 32) x.MovRegMem(32, dst, X_R11, 0);
        else if (width_bits == 16) x.MovZXRegMem(16, dst, X_R11, 0);
        else x.MovZXRegMem(8, dst, X_R11, 0);
        (void) width;
    }

    void EmitLoadSignedOperand(X86Emitter& x, const Operand& op, int width_bits, XReg dst, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        if (op.kind == Operand::OP_IMMEDIATE)
        {
            uint64_t value = ImmediateValue(op.imm, width_bits, labels, image_base);
            x.MovImm64(dst, static_cast<uint64_t>(SignExtend(value, width_bits)));
            return;
        }
        if (op.kind == Operand::OP_REGISTER)
        {
            XReg src = MapCY86Register(op.reg);
            if (width_bits == 64) x.MovRegFromReg(64, dst, src);
            else if (width_bits == 32) x.MovSXDRegReg(dst, src);
            else if (width_bits == 16) x.MovSXRegReg(16, dst, src);
            else x.MovSXRegReg(8, dst, src);
            return;
        }

        EmitLoadAddress(x, op.mem, X_R11, labels, image_base);
        if (width_bits == 64) x.MovRegMem(64, dst, X_R11, 0);
        else if (width_bits == 32) x.MovSXDRegMem(dst, X_R11, 0);
        else if (width_bits == 16) x.MovSXRegMem(16, dst, X_R11, 0);
        else x.MovSXRegMem(8, dst, X_R11, 0);
    }

    void EmitStoreRegToOperand(X86Emitter& x, const Operand& op, int width_bits, XReg src, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        if (op.kind == Operand::OP_REGISTER)
        {
            x.MovRegReg(width_bits, MapCY86Register(op.reg), src);
            return;
        }
        if (op.kind == Operand::OP_MEMORY)
        {
            EmitLoadAddress(x, op.mem, X_R11, labels, image_base);
            x.MovMemReg(width_bits, X_R11, 0, src);
            return;
        }
        throw runtime_error("immediate destination");
    }

    void EmitStoreImmBytesToStack(X86Emitter& x, const vector<unsigned char>& bytes, long long disp) const
    {
        uint64_t lo = 0;
        for (size_t i = 0; i < min<size_t>(8, bytes.size()); ++i) lo |= static_cast<uint64_t>(bytes[i]) << (8 * i);
        x.MovImm64(X_RAX, lo);
        if (!bytes.empty()) x.MovMemReg(min<int>(64, static_cast<int>(min<size_t>(8, bytes.size()) * 8)), X_RSP, disp, X_RAX);
        if (bytes.size() > 8)
        {
            uint16_t hi = static_cast<uint16_t>(bytes[8]) | (static_cast<uint16_t>(bytes[9]) << 8);
            x.MovImm64(X_RAX, hi);
            x.MovMemReg(16, X_RSP, disp + 8, X_RAX);
        }
    }

    void EmitMaterializeOperand(X86Emitter& x, const Operand& op, int width_bytes, long long disp, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        if (width_bytes <= 8)
        {
            EmitLoadRawOperand(x, op, width_bytes * 8, X_RAX, labels, image_base);
            x.MovMemReg(width_bytes * 8, X_RSP, disp, X_RAX);
            return;
        }

        if (op.kind == Operand::OP_IMMEDIATE)
        {
            vector<unsigned char> bytes = op.imm.kind == ImmediateExpr::IMM_LITERAL ? op.imm.literal.bytes : vector<unsigned char>();
            if (op.imm.kind == ImmediateExpr::IMM_LABEL)
            {
                uint64_t addr = ImmediateValue(op.imm, 64, labels, image_base);
                bytes.resize(10, 0);
                for (int i = 0; i < 8; ++i) bytes[i] = static_cast<unsigned char>((addr >> (8 * i)) & 0xFF);
            }
            EmitStoreImmBytesToStack(x, bytes, disp);
            return;
        }

        if (op.kind == Operand::OP_MEMORY)
        {
            EmitLoadAddress(x, op.mem, X_R11, labels, image_base);
            x.MovRegMem(64, X_RAX, X_R11, 0);
            x.MovMemReg(64, X_RSP, disp, X_RAX);
            x.MovRegMem(16, X_RAX, X_R11, 8);
            x.MovMemReg(16, X_RSP, disp + 8, X_RAX);
            return;
        }

        throw runtime_error("bad 80-bit operand");
    }

    void EmitStoreStackToOperand(X86Emitter& x, long long disp, int width_bytes, const Operand& dst, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        if (width_bytes <= 8)
        {
            x.MovRegMem(width_bytes * 8, X_RAX, X_RSP, disp);
            EmitStoreRegToOperand(x, dst, width_bytes * 8, X_RAX, labels, image_base);
            return;
        }

        if (dst.kind != Operand::OP_MEMORY)
        {
            throw runtime_error("80-bit destination must be memory");
        }
        EmitLoadAddress(x, dst.mem, X_R11, labels, image_base);
        x.MovRegMem(64, X_RAX, X_RSP, disp);
        x.MovMemReg(64, X_R11, 0, X_RAX);
        x.MovRegMem(16, X_RAX, X_RSP, disp + 8);
        x.MovMemReg(16, X_R11, 8, X_RAX);
    }

    void EmitJumpTarget(X86Emitter& x, const Operand& op, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        if (op.kind == Operand::OP_IMMEDIATE)
        {
            x.MovImm64(X_R11, ImmediateValue(op.imm, 64, labels, image_base));
            return;
        }
        if (op.kind == Operand::OP_REGISTER)
        {
            x.MovRegFromReg(64, X_R11, MapCY86Register(op.reg));
            return;
        }
        EmitLoadAddress(x, op.mem, X_R10, labels, image_base);
        x.MovRegMem(64, X_R11, X_R10, 0);
    }

    void EmitIntegerBinary(const Statement& stmt, CodeBuffer& out, const map<string, uint64_t>& labels, uint64_t image_base, const string& kind) const
    {
        X86Emitter x(out);
        int width = ParseTrailingWidth(stmt.opcode);
        bool signed_mode = kind == "signed";
        if (stmt.opcode.rfind("srshift", 0) == 0)
        {
            EmitLoadSignedOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
            EmitLoadRawOperand(x, stmt.operands[2], 8, X_RBX, labels, image_base);
        }
        else if (kind == "raw")
        {
            EmitLoadRawOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
            EmitLoadRawOperand(x, stmt.operands[2], width, X_RBX, labels, image_base);
        }
        else
        {
            EmitLoadSignedOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
            EmitLoadSignedOperand(x, stmt.operands[2], width, X_RBX, labels, image_base);
            if (!signed_mode)
            {
                EmitLoadRawOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
                EmitLoadRawOperand(x, stmt.operands[2], width, X_RBX, labels, image_base);
            }
        }

        if (stmt.opcode.rfind("and", 0) == 0) x.AndRegReg(X_RAX, X_RBX);
        else if (stmt.opcode.rfind("or", 0) == 0) x.OrRegReg(X_RAX, X_RBX);
        else if (stmt.opcode.rfind("xor", 0) == 0) x.XorRegReg(X_RAX, X_RBX);
        else if (stmt.opcode.rfind("iadd", 0) == 0) x.AddRegReg(X_RAX, X_RBX);
        else if (stmt.opcode.rfind("isub", 0) == 0) x.SubRegReg(X_RAX, X_RBX);
        else if (stmt.opcode.rfind("smul", 0) == 0 || stmt.opcode.rfind("umul", 0) == 0) x.IMulRegReg(X_RAX, X_RBX);
        else if (stmt.opcode.rfind("sdiv", 0) == 0 || stmt.opcode.rfind("udiv", 0) == 0 ||
            stmt.opcode.rfind("smod", 0) == 0 || stmt.opcode.rfind("umod", 0) == 0)
        {
            if (stmt.opcode[0] == 'u')
            {
                x.XorRegReg(X_RDX, X_RDX);
                x.DivReg(X_RBX, false);
                if (stmt.opcode.rfind("umod", 0) == 0) x.MovRegFromReg(64, X_RAX, X_RDX);
            }
            else
            {
                x.CQO();
                x.DivReg(X_RBX, true);
                if (stmt.opcode.rfind("smod", 0) == 0) x.MovRegFromReg(64, X_RAX, X_RDX);
            }
        }
        else if (stmt.opcode.rfind("lshift", 0) == 0 || stmt.opcode.rfind("srshift", 0) == 0 || stmt.opcode.rfind("urshift", 0) == 0)
        {
            EmitLoadRawOperand(x, stmt.operands[2], 8, X_RCX, labels, image_base);
            if (stmt.opcode.rfind("lshift", 0) == 0) x.ShiftRegCL(X_RAX, 4);
            else if (stmt.opcode.rfind("srshift", 0) == 0) x.ShiftRegCL(X_RAX, 7);
            else x.ShiftRegCL(X_RAX, 5);
        }
        else
        {
            throw runtime_error("unsupported integer opcode");
        }
        EmitStoreRegToOperand(x, stmt.operands[0], width, X_RAX, labels, image_base);
    }

    void EmitIntegerCompare(const Statement& stmt, CodeBuffer& out, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        X86Emitter x(out);
        int width = ParseTrailingWidth(stmt.opcode);
        bool signed_cmp = stmt.opcode[0] == 's';
        if (stmt.opcode.rfind("ieq", 0) == 0 || stmt.opcode.rfind("ine", 0) == 0)
        {
            EmitLoadRawOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
            EmitLoadRawOperand(x, stmt.operands[2], width, X_RBX, labels, image_base);
        }
        else if (signed_cmp)
        {
            EmitLoadSignedOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
            EmitLoadSignedOperand(x, stmt.operands[2], width, X_RBX, labels, image_base);
        }
        else
        {
            EmitLoadRawOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
            EmitLoadRawOperand(x, stmt.operands[2], width, X_RBX, labels, image_base);
        }
        x.CmpRegReg(X_RAX, X_RBX);

        unsigned char cc = 0;
        if (stmt.opcode.rfind("ieq", 0) == 0) cc = 4;
        else if (stmt.opcode.rfind("ine", 0) == 0) cc = 5;
        else if (stmt.opcode.rfind("slt", 0) == 0) cc = 12;
        else if (stmt.opcode.rfind("ult", 0) == 0) cc = 2;
        else if (stmt.opcode.rfind("sgt", 0) == 0) cc = 15;
        else if (stmt.opcode.rfind("ugt", 0) == 0) cc = 7;
        else if (stmt.opcode.rfind("sle", 0) == 0) cc = 14;
        else if (stmt.opcode.rfind("ule", 0) == 0) cc = 6;
        else if (stmt.opcode.rfind("sge", 0) == 0) cc = 13;
        else if (stmt.opcode.rfind("uge", 0) == 0) cc = 3;
        else throw runtime_error("bad compare opcode");

        x.SetCC(cc, X_RAX);
        EmitStoreRegToOperand(x, stmt.operands[0], 8, X_RAX, labels, image_base);
    }

    void EmitFloatBinary(const Statement& stmt, CodeBuffer& out, const map<string, uint64_t>& labels, uint64_t image_base, uint64_t two63_addr) const
    {
        (void) two63_addr;
        X86Emitter x(out);
        int width = ParseTrailingWidth(stmt.opcode) / 8;
        EmitMaterializeOperand(x, stmt.operands[1], width, -32, labels, image_base);
        EmitMaterializeOperand(x, stmt.operands[2], width, -48, labels, image_base);
        if (stmt.opcode.rfind("fadd", 0) == 0 || stmt.opcode.rfind("fsub", 0) == 0 ||
            stmt.opcode.rfind("fmul", 0) == 0 || stmt.opcode.rfind("fdiv", 0) == 0)
        {
            x.FldMem(width, X_RSP, -32);
            x.FldMem(width, X_RSP, -48);
            if (stmt.opcode.rfind("fadd", 0) == 0) x.FAddP();
            else if (stmt.opcode.rfind("fsub", 0) == 0) x.FSubP();
            else if (stmt.opcode.rfind("fmul", 0) == 0) x.FMulP();
            else x.FDivP();
            x.FstpMem(width, X_RSP, -64);
            EmitStoreStackToOperand(x, -64, width, stmt.operands[0], labels, image_base);
            return;
        }
        else
        {
            x.FldMem(width, X_RSP, -48);
            x.FldMem(width, X_RSP, -32);
            x.FUComIP();
            x.FstpST0();
            unsigned char cc = 0;
            if (stmt.opcode.rfind("feq", 0) == 0) cc = 4;
            else if (stmt.opcode.rfind("fne", 0) == 0) cc = 5;
            else if (stmt.opcode.rfind("flt", 0) == 0) cc = 2;
            else if (stmt.opcode.rfind("fgt", 0) == 0) cc = 7;
            else if (stmt.opcode.rfind("fle", 0) == 0) cc = 6;
            else if (stmt.opcode.rfind("fge", 0) == 0) cc = 3;
            else throw runtime_error("bad float compare opcode");
            x.SetCC(cc, X_RAX);
            EmitStoreRegToOperand(x, stmt.operands[0], 8, X_RAX, labels, image_base);
            return;
        }
    }

    void EmitToF80(const Statement& stmt, CodeBuffer& out, const map<string, uint64_t>& labels, uint64_t image_base) const
    {
        X86Emitter x(out);
        string op = stmt.opcode;
        if (op == "s8convf80")
        {
            EmitLoadSignedOperand(x, stmt.operands[1], 8, X_RAX, labels, image_base);
            x.MovMemReg(64, X_RSP, -32, X_RAX);
            x.FildMem(8, X_RSP, -32);
        }
        else if (op == "s16convf80")
        {
            EmitMaterializeOperand(x, stmt.operands[1], 2, -32, labels, image_base);
            x.FildMem(2, X_RSP, -32);
        }
        else if (op == "s32convf80")
        {
            EmitMaterializeOperand(x, stmt.operands[1], 4, -32, labels, image_base);
            x.FildMem(4, X_RSP, -32);
        }
        else if (op == "s64convf80")
        {
            EmitMaterializeOperand(x, stmt.operands[1], 8, -32, labels, image_base);
            x.FildMem(8, X_RSP, -32);
        }
        else if (op == "u8convf80" || op == "u16convf80" || op == "u32convf80")
        {
            int src_width = op == "u8convf80" ? 8 : (op == "u16convf80" ? 16 : 32);
            EmitLoadRawOperand(x, stmt.operands[1], src_width, X_RAX, labels, image_base);
            x.MovMemReg(64, X_RSP, -32, X_RAX);
            x.FildMem(8, X_RSP, -32);
        }
        else if (op == "u64convf80")
        {
            EmitLoadRawOperand(x, stmt.operands[1], 64, X_RAX, labels, image_base);
            x.TestReg64(X_RAX);
            size_t small = x.Jcc(9);
            x.MovRegFromReg(64, X_RBX, X_RAX);
            x.MovImm64(X_RCX, 1);
            x.AndRegReg(X_RBX, X_RCX);
            x.MovImm64(X_RCX, 1);
            x.ShiftRegCL(X_RAX, 5);
            x.MovMemReg(64, X_RSP, -40, X_RAX);
            x.FildMem(8, X_RSP, -40);
            x.FildMem(8, X_RSP, -40);
            x.FAddP();
            x.TestReg8(X_RBX);
            size_t skip_add = x.Jcc(4);
            x.Fld1();
            x.FAddP();
            x.PatchRel32(skip_add, out.size());
            size_t done = x.JmpRel32();
            x.PatchRel32(small, out.size());
            x.MovMemReg(64, X_RSP, -32, X_RAX);
            x.FildMem(8, X_RSP, -32);
            x.PatchRel32(done, out.size());
        }
        else if (op == "f32convf80")
        {
            EmitMaterializeOperand(x, stmt.operands[1], 4, -32, labels, image_base);
            x.FldMem(4, X_RSP, -32);
        }
        else if (op == "f64convf80")
        {
            EmitMaterializeOperand(x, stmt.operands[1], 8, -32, labels, image_base);
            x.FldMem(8, X_RSP, -32);
        }
        else
        {
            throw runtime_error("unsupported conversion");
        }

        x.FstpMem(10, X_RSP, -64);
        EmitStoreStackToOperand(x, -64, 10, stmt.operands[0], labels, image_base);
    }

    void EmitFromF80(const Statement& stmt, CodeBuffer& out, const map<string, uint64_t>& labels, uint64_t image_base, uint64_t two63_addr) const
    {
        X86Emitter x(out);
        EmitMaterializeOperand(x, stmt.operands[1], 10, -64, labels, image_base);
        if (stmt.opcode == "f80convf32")
        {
            x.FldMem(10, X_RSP, -64);
            x.FstpMem(4, X_RSP, -32);
            EmitStoreStackToOperand(x, -32, 4, stmt.operands[0], labels, image_base);
            return;
        }
        if (stmt.opcode == "f80convf64")
        {
            x.FldMem(10, X_RSP, -64);
            x.FstpMem(8, X_RSP, -32);
            EmitStoreStackToOperand(x, -32, 8, stmt.operands[0], labels, image_base);
            return;
        }
        if (stmt.opcode == "f80convu64")
        {
            x.MovImm64(X_R10, two63_addr);
            x.FldMem(10, X_R10, 0);
            x.FldMem(10, X_RSP, -64);
            x.FUComIP();
            x.FstpST0();
            size_t small = x.Jcc(2);
            x.FldMem(10, X_RSP, -64);
            x.MovImm64(X_R10, two63_addr);
            x.FldMem(10, X_R10, 0);
            x.FSubP();
            x.FistpMem(8, X_RSP, -32);
            x.MovRegMem(64, X_RAX, X_RSP, -32);
            x.MovImm64(X_RBX, 0x8000000000000000ull);
            x.OrRegReg(X_RAX, X_RBX);
            EmitStoreRegToOperand(x, stmt.operands[0], 64, X_RAX, labels, image_base);
            size_t done = x.JmpRel32();
            x.PatchRel32(small, out.size());
            x.FldMem(10, X_RSP, -64);
            x.FistpMem(8, X_RSP, -32);
            EmitStoreStackToOperand(x, -32, 8, stmt.operands[0], labels, image_base);
            x.PatchRel32(done, out.size());
            return;
        }

        x.FldMem(10, X_RSP, -64);
        if (stmt.opcode == "f80convs8")
        {
            x.FistpMem(2, X_RSP, -32);
            x.MovZXRegMem(8, X_RAX, X_RSP, -32);
            EmitStoreRegToOperand(x, stmt.operands[0], 8, X_RAX, labels, image_base);
            return;
        }
        if (stmt.opcode == "f80convs16")
        {
            x.FistpMem(2, X_RSP, -32);
            EmitStoreStackToOperand(x, -32, 2, stmt.operands[0], labels, image_base);
            return;
        }
        if (stmt.opcode == "f80convs32")
        {
            x.FistpMem(4, X_RSP, -32);
            EmitStoreStackToOperand(x, -32, 4, stmt.operands[0], labels, image_base);
            return;
        }
        if (stmt.opcode == "f80convs64" || stmt.opcode == "f80convu8" || stmt.opcode == "f80convu16" || stmt.opcode == "f80convu32")
        {
            x.FistpMem(8, X_RSP, -32);
            int width = stmt.opcode == "f80convu8" ? 1 : (stmt.opcode == "f80convu16" ? 2 : (stmt.opcode == "f80convu32" ? 4 : 8));
            EmitStoreStackToOperand(x, -32, width, stmt.operands[0], labels, image_base);
            return;
        }
        throw runtime_error("unsupported conversion");
    }

    void EmitInstruction(const Statement& stmt, CodeBuffer& out, const map<string, uint64_t>& labels, uint64_t image_base, uint64_t two63_addr) const
    {
        X86Emitter x(out);
        if (stmt.is_literal)
        {
            uint64_t aligned = AlignTo(out.size(), LiteralAlignmentBytes(stmt.literal));
            while (out.size() < aligned) out.u8(0);
            out.bytes(stmt.literal.bytes);
            return;
        }

        if (IsDataOpcode(stmt.opcode))
        {
            int width = ParseTrailingWidth(stmt.opcode) / 8;
            uint64_t aligned = AlignTo(out.size(), width);
            while (out.size() < aligned) out.u8(0);
            if (stmt.operands.size() != 1 || stmt.operands[0].kind != Operand::OP_IMMEDIATE)
            {
                throw runtime_error("bad data operand");
            }
            uint64_t value = ImmediateValue(stmt.operands[0].imm, width * 8, labels, image_base);
            for (int i = 0; i < width; ++i) out.u8(static_cast<unsigned char>((value >> (8 * i)) & 0xFF));
            return;
        }

        if (stmt.opcode.rfind("move", 0) == 0)
        {
            int width = ParseTrailingWidth(stmt.opcode);
            if (width == 80)
            {
                EmitMaterializeOperand(x, stmt.operands[1], 10, -32, labels, image_base);
                EmitStoreStackToOperand(x, -32, 10, stmt.operands[0], labels, image_base);
            }
            else
            {
                EmitLoadRawOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
                EmitStoreRegToOperand(x, stmt.operands[0], width, X_RAX, labels, image_base);
            }
            return;
        }

        if (stmt.opcode == "jump")
        {
            EmitJumpTarget(x, stmt.operands[0], labels, image_base);
            x.JmpReg(X_R11);
            return;
        }
        if (stmt.opcode == "call")
        {
            EmitJumpTarget(x, stmt.operands[0], labels, image_base);
            x.CallReg(X_R11);
            return;
        }
        if (stmt.opcode == "ret")
        {
            x.Ret();
            return;
        }
        if (stmt.opcode == "jumpif")
        {
            EmitLoadRawOperand(x, stmt.operands[0], 8, X_RAX, labels, image_base);
            x.TestReg8(X_RAX);
            size_t skip = x.Jcc(4);
            EmitJumpTarget(x, stmt.operands[1], labels, image_base);
            x.JmpReg(X_R11);
            x.PatchRel32(skip, out.size());
            return;
        }
        if (stmt.opcode.rfind("syscall", 0) == 0)
        {
            int argc = atoi(stmt.opcode.substr(7).c_str());
            static XReg arg_regs[] = {X_RDI, X_RSI, X_RDX, X_R10, X_R8, X_R9};
            EmitLoadRawOperand(x, stmt.operands[1], 64, X_RAX, labels, image_base);
            for (int i = 0; i < argc; ++i)
            {
                EmitLoadRawOperand(x, stmt.operands[i + 2], 64, arg_regs[i], labels, image_base);
            }
            x.Syscall();
            EmitStoreRegToOperand(x, stmt.operands[0], 64, X_RAX, labels, image_base);
            return;
        }
        if (stmt.opcode.rfind("not", 0) == 0)
        {
            int width = ParseTrailingWidth(stmt.opcode);
            EmitLoadRawOperand(x, stmt.operands[1], width, X_RAX, labels, image_base);
            x.NotReg(X_RAX);
            EmitStoreRegToOperand(x, stmt.operands[0], width, X_RAX, labels, image_base);
            return;
        }
        if (stmt.opcode.rfind("and", 0) == 0 || stmt.opcode.rfind("or", 0) == 0 || stmt.opcode.rfind("xor", 0) == 0 ||
            stmt.opcode.rfind("iadd", 0) == 0 || stmt.opcode.rfind("isub", 0) == 0)
        {
            EmitIntegerBinary(stmt, out, labels, image_base, "raw");
            return;
        }
        if (stmt.opcode.rfind("smul", 0) == 0 || stmt.opcode.rfind("sdiv", 0) == 0 || stmt.opcode.rfind("smod", 0) == 0)
        {
            EmitIntegerBinary(stmt, out, labels, image_base, "signed");
            return;
        }
        if (stmt.opcode.rfind("umul", 0) == 0 || stmt.opcode.rfind("udiv", 0) == 0 || stmt.opcode.rfind("umod", 0) == 0 ||
            stmt.opcode.rfind("lshift", 0) == 0 || stmt.opcode.rfind("srshift", 0) == 0 || stmt.opcode.rfind("urshift", 0) == 0)
        {
            EmitIntegerBinary(stmt, out, labels, image_base, "unsigned");
            return;
        }
        if (IsIntegerCompare(stmt.opcode))
        {
            EmitIntegerCompare(stmt, out, labels, image_base);
            return;
        }
        if (stmt.opcode.find("convf80") != string::npos && stmt.opcode.rfind("f80conv", 0) != 0)
        {
            EmitToF80(stmt, out, labels, image_base);
            return;
        }
        if (stmt.opcode.rfind("f80conv", 0) == 0)
        {
            EmitFromF80(stmt, out, labels, image_base, two63_addr);
            return;
        }
        if (IsFloatBinary(stmt.opcode))
        {
            EmitFloatBinary(stmt, out, labels, image_base, two63_addr);
            return;
        }

        throw runtime_error("unsupported opcode: " + stmt.opcode);
    }

    vector<unsigned char> BuildImage(uint64_t image_base) const
    {
        CodeBuffer out;
        uint64_t two63_addr = image_base + support_two63_offset;
        for (size_t i = 0; i < statements.size(); ++i)
        {
            while (out.size() < statements[i].offset) out.u8(0);
            EmitInstruction(statements[i], out, label_offsets, image_base, two63_addr);
        }
        while (out.size() < support_two63_offset) out.u8(0);
        out.bytes(support_two63);
        return out.data;
    }
};

vector<PPToken> LoadAllTranslationUnits(const vector<string>& srcfiles)
{
    vector<PPToken> out;
    for (size_t i = 0; i < srcfiles.size(); ++i)
    {
        vector<PPToken> cur = PreprocessSourceTokens(srcfiles[i]);
        out.insert(out.end(), cur.begin(), cur.end());
    }
    return out;
}

int main(int argc, char** argv)
{
    try
    {
        vector<string> args;
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        if (args.size() < 3 || args[0] != "-o")
        {
            throw logic_error("invalid usage");
        }

        string outfile = args[1];
        vector<string> srcfiles(args.begin() + 2, args.end());

        vector<PPToken> pp = LoadAllTranslationUnits(srcfiles);
        vector<CYToken> toks = LexForPA9(pp);
        Parser parser(toks);

        CY86Compiler compiler;
        compiler.statements = parser.ParseProgram();
        if (compiler.statements.empty())
        {
            throw runtime_error("empty program");
        }
        compiler.AssignOffsets();

        const uint64_t segment_base = 0x400000;
        const uint64_t image_base = segment_base + 64 + 56;
        vector<unsigned char> image = compiler.BuildImage(image_base);

        uint64_t entry_offset = compiler.statements[0].offset;
        map<string, uint64_t>::const_iterator start = compiler.label_offsets.find("start");
        if (start != compiler.label_offsets.end()) entry_offset = start->second;

        ElfHeader elf_header;
        ProgramSegmentHeader program_segment_header;
        elf_header.entry = image_base + entry_offset;
        program_segment_header.filesz = 64 + 56 + image.size();
        program_segment_header.memsz = program_segment_header.filesz;

        ofstream out(outfile.c_str(), ios::binary);
        out.write(reinterpret_cast<const char*>(&elf_header), 64);
        out.write(reinterpret_cast<const char*>(&program_segment_header), 56);
        out.write(reinterpret_cast<const char*>(image.data()), static_cast<streamsize>(image.size()));
        out.close();

        if (!PA9SetFileExecutable(outfile))
        {
            throw runtime_error("chmod failed");
        }
        return EXIT_SUCCESS;
    }
    catch (exception& e)
    {
        cerr << "ERROR: " << e.what() << endl;
        return EXIT_FAILURE;
    }
}
