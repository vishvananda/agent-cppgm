// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

#if defined(__GNUC__)
#pragma GCC optimize("O3")
#endif

using namespace std;

#define CPPGM_EMBED_PREPROC 1
#include "preproc.cpp"

struct CYToken
{
	EPPTokenKind kind = PPT_EOF;
	string source;
	string file;
	int line = 0;

	CYToken()
	{}

	CYToken(EPPTokenKind kind, const string& source, const string& file, int line)
		: kind(kind), source(source), file(file), line(line)
	{}
};

enum RegId
{
	REG_SP,
	REG_BP,
	REG_X8,
	REG_X16,
	REG_X32,
	REG_X64,
	REG_Y8,
	REG_Y16,
	REG_Y32,
	REG_Y64,
	REG_Z8,
	REG_Z16,
	REG_Z32,
	REG_Z64,
	REG_T8,
	REG_T16,
	REG_T32,
	REG_T64
};

bool IsRegisterName(const string& name, RegId& out)
{
	static const unordered_map<string, RegId> kRegs =
	{
		{"sp", REG_SP},
		{"bp", REG_BP},
		{"x8", REG_X8},
		{"x16", REG_X16},
		{"x32", REG_X32},
		{"x64", REG_X64},
		{"y8", REG_Y8},
		{"y16", REG_Y16},
		{"y32", REG_Y32},
		{"y64", REG_Y64},
		{"z8", REG_Z8},
		{"z16", REG_Z16},
		{"z32", REG_Z32},
		{"z64", REG_Z64},
		{"t8", REG_T8},
		{"t16", REG_T16},
		{"t32", REG_T32},
		{"t64", REG_T64}
	};

	unordered_map<string, RegId>::const_iterator it = kRegs.find(name);
	if (it == kRegs.end())
	{
		return false;
	}
	out = it->second;
	return true;
}

int RegisterWidthBits(RegId reg)
{
	switch (reg)
	{
	case REG_SP:
	case REG_BP:
	case REG_X64:
	case REG_Y64:
	case REG_Z64:
	case REG_T64:
		return 64;
	case REG_X32:
	case REG_Y32:
	case REG_Z32:
	case REG_T32:
		return 32;
	case REG_X16:
	case REG_Y16:
	case REG_Z16:
	case REG_T16:
		return 16;
	case REG_X8:
	case REG_Y8:
	case REG_Z8:
	case REG_T8:
		return 8;
	}
	return 0;
}

size_t FundamentalTypeSize(EFundamentalType type)
{
	switch (type)
	{
	case FT_CHAR:
	case FT_SIGNED_CHAR:
	case FT_UNSIGNED_CHAR:
	case FT_BOOL:
		return 1;
	case FT_CHAR16_T:
	case FT_SHORT_INT:
	case FT_UNSIGNED_SHORT_INT:
		return 2;
	case FT_WCHAR_T:
	case FT_CHAR32_T:
	case FT_INT:
	case FT_UNSIGNED_INT:
	case FT_FLOAT:
		return 4;
	case FT_LONG_INT:
	case FT_LONG_LONG_INT:
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT:
	case FT_DOUBLE:
	case FT_NULLPTR_T:
		return 8;
	case FT_LONG_DOUBLE:
		return sizeof(long double);
	case FT_VOID:
		return 0;
	}
	return 0;
}

bool FundamentalTypeSigned(EFundamentalType type)
{
	switch (type)
	{
	case FT_CHAR:
		return numeric_limits<char>::is_signed;
	case FT_SIGNED_CHAR:
	case FT_SHORT_INT:
	case FT_INT:
	case FT_LONG_INT:
	case FT_LONG_LONG_INT:
	case FT_WCHAR_T:
		return true;
	default:
		return false;
	}
}

template <typename T>
vector<unsigned char> EncodeObject(const T& value)
{
	vector<unsigned char> out(sizeof(T), 0);
	memcpy(out.data(), &value, sizeof(T));
	return out;
}

struct LiteralValue
{
	enum Kind
	{
		LV_INTEGER,
		LV_FLOAT,
		LV_STRING
	};

	Kind kind = LV_INTEGER;
	bool int_signed = false;
	size_t nbytes = 0;
	vector<unsigned char> bytes;
	long double numeric = 0;
};

bool TryParseIntegerLiteralValue(const string& source, LiteralValue& out)
{
	string prefix;
	string ud_suffix;
	IntegerSuffix suffix;
	bool is_udl = false;
	bool is_hex = false;
	bool is_octal = false;
	if (!ParseIntegerLiteralParts(source, prefix, ud_suffix, suffix, is_udl, is_hex, is_octal) || is_udl)
	{
		return false;
	}

	string digits = prefix;
	int base = 10;
	if (is_hex)
	{
		digits = prefix.substr(2);
		base = 16;
	}
	else if (is_octal && prefix.size() > 1)
	{
		base = 8;
	}

	unsigned __int128 value = 0;
	if (!ParseUnsignedIntegerValue(digits, base, value))
	{
		return false;
	}

	vector<EFundamentalType> candidates;
	bool decimal = !is_hex && !is_octal;
	if (decimal)
	{
		if (!suffix.is_unsigned && suffix.long_count == 0) candidates = {FT_INT, FT_LONG_INT, FT_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 0) candidates = {FT_UNSIGNED_INT, FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (!suffix.is_unsigned && suffix.long_count == 1) candidates = {FT_LONG_INT, FT_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 1) candidates = {FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (!suffix.is_unsigned && suffix.long_count == 2) candidates = {FT_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 2) candidates = {FT_UNSIGNED_LONG_LONG_INT};
	}
	else
	{
		if (!suffix.is_unsigned && suffix.long_count == 0) candidates = {FT_INT, FT_UNSIGNED_INT, FT_LONG_INT, FT_UNSIGNED_LONG_INT, FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 0) candidates = {FT_UNSIGNED_INT, FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (!suffix.is_unsigned && suffix.long_count == 1) candidates = {FT_LONG_INT, FT_UNSIGNED_LONG_INT, FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 1) candidates = {FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (!suffix.is_unsigned && suffix.long_count == 2) candidates = {FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 2) candidates = {FT_UNSIGNED_LONG_LONG_INT};
	}

	for (EFundamentalType candidate : candidates)
	{
		switch (candidate)
		{
		case FT_INT:
			if (FitsSigned(value, candidate))
			{
				int x = static_cast<int>(value);
				out.kind = LiteralValue::LV_INTEGER;
				out.int_signed = true;
				out.nbytes = sizeof(x);
				out.bytes = EncodeObject(x);
				out.numeric = x;
				return true;
			}
			break;
		case FT_LONG_INT:
			if (FitsSigned(value, candidate))
			{
				long x = static_cast<long>(value);
				out.kind = LiteralValue::LV_INTEGER;
				out.int_signed = true;
				out.nbytes = sizeof(x);
				out.bytes = EncodeObject(x);
				out.numeric = x;
				return true;
			}
			break;
		case FT_LONG_LONG_INT:
			if (FitsSigned(value, candidate))
			{
				long long x = static_cast<long long>(value);
				out.kind = LiteralValue::LV_INTEGER;
				out.int_signed = true;
				out.nbytes = sizeof(x);
				out.bytes = EncodeObject(x);
				out.numeric = x;
				return true;
			}
			break;
		case FT_UNSIGNED_INT:
			if (FitsUnsigned(value, candidate))
			{
				unsigned int x = static_cast<unsigned int>(value);
				out.kind = LiteralValue::LV_INTEGER;
				out.int_signed = false;
				out.nbytes = sizeof(x);
				out.bytes = EncodeObject(x);
				out.numeric = x;
				return true;
			}
			break;
		case FT_UNSIGNED_LONG_INT:
			if (FitsUnsigned(value, candidate))
			{
				unsigned long x = static_cast<unsigned long>(value);
				out.kind = LiteralValue::LV_INTEGER;
				out.int_signed = false;
				out.nbytes = sizeof(x);
				out.bytes = EncodeObject(x);
				out.numeric = x;
				return true;
			}
			break;
		case FT_UNSIGNED_LONG_LONG_INT:
			if (FitsUnsigned(value, candidate))
			{
				unsigned long long x = static_cast<unsigned long long>(value);
				out.kind = LiteralValue::LV_INTEGER;
				out.int_signed = false;
				out.nbytes = sizeof(x);
				out.bytes = EncodeObject(x);
				out.numeric = x;
				return true;
			}
			break;
		default:
			break;
		}
	}

	return false;
}

bool TryParseCharacterLiteralValue(const string& source, LiteralValue& out)
{
	CharacterLiteralInfo info;
	if (!ParseCharacterLiteralSource(source, info) || !info.valid || info.user_defined)
	{
		return false;
	}

	uint32_t value = info.value;
	out.kind = LiteralValue::LV_INTEGER;
	out.int_signed = FundamentalTypeSigned(info.type);
	out.nbytes = FundamentalTypeSize(info.type);
	out.bytes.assign(out.nbytes, 0);
	for (size_t i = 0; i < out.nbytes; ++i)
	{
		out.bytes[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xFF);
	}
	out.numeric = value;
	return true;
}

bool TryParseFloatingLiteralValue(const string& source, LiteralValue& out)
{
	string prefix;
	string ud_suffix;
	char suffix = '\0';
	bool is_udl = false;
	if (!ParseFloatingLiteralParts(source, prefix, ud_suffix, suffix, is_udl) || is_udl)
	{
		return false;
	}

	out.kind = LiteralValue::LV_FLOAT;
	if (suffix == 'f' || suffix == 'F')
	{
		float x = PA2Decode_float(prefix);
		out.nbytes = sizeof(float);
		out.bytes = EncodeObject(x);
		out.numeric = static_cast<long double>(x);
		return true;
	}
	if (suffix == 'l' || suffix == 'L')
	{
		long double x = PA2Decode_long_double(prefix);
		out.nbytes = sizeof(long double);
		out.bytes = EncodeObject(x);
		out.numeric = x;
		return true;
	}

	double x = PA2Decode_double(prefix);
	out.nbytes = sizeof(double);
	out.bytes = EncodeObject(x);
	out.numeric = static_cast<long double>(x);
	return true;
}

bool TryParseStringLiteralValue(const string& source, LiteralValue& out)
{
	StringLiteralPiece piece;
	if (!ParseStringLiteralPiece(source, piece) || !piece.valid || piece.user_defined)
	{
		return false;
	}

	vector<char> bytes;
	if (!EncodeCodePointsUtf8(piece.code_points, bytes))
	{
		return false;
	}

	out.kind = LiteralValue::LV_STRING;
	out.int_signed = false;
	out.nbytes = bytes.size();
	out.bytes.assign(bytes.begin(), bytes.end());
	out.numeric = 0;
	return true;
}

LiteralValue ParseLiteralValue(const string& source)
{
	LiteralValue out;
	if (TryParseStringLiteralValue(source, out) ||
		TryParseCharacterLiteralValue(source, out) ||
		TryParseFloatingLiteralValue(source, out) ||
		TryParseIntegerLiteralValue(source, out))
	{
		return out;
	}
	throw runtime_error("invalid literal: " + source);
}

uint64_t MaskBits(int bits)
{
	if (bits >= 64)
	{
		return ~0ULL;
	}
	return (1ULL << bits) - 1ULL;
}

uint64_t BytesToU64(const vector<unsigned char>& bytes)
{
	uint64_t out = 0;
	size_t n = min<size_t>(bytes.size(), 8);
	for (size_t i = 0; i < n; ++i)
	{
		out |= static_cast<uint64_t>(bytes[i]) << (8 * i);
	}
	return out;
}

uint64_t SignExtendTo64(uint64_t value, int bits)
{
	if (bits >= 64)
	{
		return value;
	}
	uint64_t mask = MaskBits(bits);
	value &= mask;
	uint64_t sign = 1ULL << (bits - 1);
	if (value & sign)
	{
		value |= ~mask;
	}
	return value;
}

uint64_t ConvertIntegralLiteral(const LiteralValue& value, int target_bits)
{
	if (value.kind != LiteralValue::LV_INTEGER)
	{
		throw runtime_error("expected integral literal");
	}

	int source_bits = static_cast<int>(value.nbytes * 8);
	uint64_t raw = BytesToU64(value.bytes);
	if (source_bits >= target_bits)
	{
		return raw & MaskBits(target_bits);
	}
	return value.int_signed ? SignExtendTo64(raw, source_bits) & MaskBits(target_bits) : raw & MaskBits(target_bits);
}

uint64_t ConvertLiteralBits(const LiteralValue& value, int target_bits)
{
	int source_bits = static_cast<int>(value.nbytes * 8);
	uint64_t raw = BytesToU64(value.bytes);
	if (source_bits >= target_bits)
	{
		return raw & MaskBits(target_bits);
	}
	if (value.kind == LiteralValue::LV_INTEGER && value.int_signed)
	{
		return SignExtendTo64(raw, source_bits) & MaskBits(target_bits);
	}
	return raw & MaskBits(target_bits);
}

LiteralValue NegateLiteralValue(const LiteralValue& in)
{
	LiteralValue out = in;
	if (in.kind == LiteralValue::LV_STRING)
	{
		throw runtime_error("cannot negate string literal");
	}
	if (in.kind == LiteralValue::LV_FLOAT)
	{
		long double x = -in.numeric;
		out.numeric = x;
		if (in.nbytes == sizeof(float))
		{
			float y = static_cast<float>(x);
			out.bytes = EncodeObject(y);
		}
		else if (in.nbytes == sizeof(double))
		{
			double y = static_cast<double>(x);
			out.bytes = EncodeObject(y);
		}
		else
		{
			long double y = x;
			out.bytes = EncodeObject(y);
		}
		return out;
	}

	uint64_t raw = BytesToU64(in.bytes);
	uint64_t neg = (~raw + 1ULL) & MaskBits(static_cast<int>(in.nbytes * 8));
	out.bytes.assign(in.nbytes, 0);
	for (size_t i = 0; i < in.nbytes; ++i)
	{
		out.bytes[i] = static_cast<unsigned char>((neg >> (8 * i)) & 0xFF);
	}
	out.numeric = -in.numeric;
	return out;
}

bool IsKeywordIdentifier(const string& source)
{
	unordered_map<string, ETokenType>::const_iterator it = StringToTokenTypeMap.find(source);
	return it != StringToTokenTypeMap.end() && it->second < OP_LSQUARE;
}

bool IsLiteralToken(const CYToken& token)
{
	return token.kind == PPT_PP_NUMBER ||
		token.kind == PPT_CHARACTER_LITERAL ||
		token.kind == PPT_STRING_LITERAL;
}

struct ParsedValueExpr
{
	enum Kind
	{
		VE_LITERAL,
		VE_LABEL
	};

	Kind kind = VE_LITERAL;
	string literal_source;
	bool literal_negated = false;
	string label;
	bool has_offset = false;
	string offset_literal;
	bool offset_negated = false;
};

struct ParsedOperand
{
	enum Kind
	{
		PO_REG,
		PO_IMM,
		PO_MEM
	};

	Kind kind = PO_REG;
	RegId reg = REG_X64;
	bool mem_reg_base = false;
	RegId mem_base_reg = REG_X64;
	ParsedValueExpr expr;
};

struct ParsedStatement
{
	vector<string> labels;
	bool is_literal_data = false;
	string literal_source;
	bool literal_negated = false;
	string opcode;
	vector<ParsedOperand> operands;
	string file;
	int line = 0;
	uint64_t addr = 0;
	uint64_t size = 0;
	uint64_t align = 1;
	bool is_runtime_instruction = false;
};

enum OperandClass
{
	OC_BITS,
	OC_INT,
	OC_SINT,
	OC_UINT,
	OC_BOOL,
	OC_ADDR,
	OC_FLOAT
};

struct OperandRole
{
	bool write = false;
	bool immediate_only = false;
	int width_bits = 0;
	OperandClass cls = OC_BITS;
};

enum ExecClass
{
	EX_MOVE = 1,
	EX_JUMP,
	EX_JUMPIF,
	EX_CALL,
	EX_RET,
	EX_NOT,
	EX_AND,
	EX_OR,
	EX_XOR,
	EX_LSHIFT,
	EX_SRSHIFT,
	EX_URSHIFT,
	EX_SCONVF80,
	EX_UCONVF80,
	EX_F32CONVF80,
	EX_F64CONVF80,
	EX_F80CONVS,
	EX_F80CONVU,
	EX_F80CONVF32,
	EX_F80CONVF64,
	EX_IADD,
	EX_ISUB,
	EX_SMUL,
	EX_UMUL,
	EX_SDIV,
	EX_UDIV,
	EX_SMOD,
	EX_UMOD,
	EX_FADD,
	EX_FSUB,
	EX_FMUL,
	EX_FDIV,
	EX_IEQ,
	EX_INE,
	EX_SLT,
	EX_ULT,
	EX_SGT,
	EX_UGT,
	EX_SLE,
	EX_ULE,
	EX_SGE,
	EX_UGE,
	EX_FEQ,
	EX_FNE,
	EX_FLT,
	EX_FGT,
	EX_FLE,
	EX_FGE,
	EX_SYSCALL
};

struct OpcodeInfo
{
	bool is_data = false;
	int data_width_bits = 0;
	ExecClass exec = EX_MOVE;
	int width_bits = 0;
	int aux = 0;
	vector<OperandRole> roles;
};

bool EndsWith(const string& text, const string& suffix)
{
	return text.size() >= suffix.size() &&
		equal(suffix.begin(), suffix.end(), text.end() - suffix.size());
}

bool ParseTrailingWidth(const string& name, const string& prefix, int& width)
{
	if (name.find(prefix) != 0)
	{
		return false;
	}
	string tail = name.substr(prefix.size());
	if (tail == "8") { width = 8; return true; }
	if (tail == "16") { width = 16; return true; }
	if (tail == "32") { width = 32; return true; }
	if (tail == "64") { width = 64; return true; }
	if (tail == "80") { width = 80; return true; }
	return false;
}

bool ParseLeadingWidth(const string& name, const string& prefix, const string& suffix, int& width)
{
	if (name.find(prefix) != 0 || !EndsWith(name, suffix))
	{
		return false;
	}
	string middle = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
	if (middle == "8") { width = 8; return true; }
	if (middle == "16") { width = 16; return true; }
	if (middle == "32") { width = 32; return true; }
	if (middle == "64") { width = 64; return true; }
	return false;
}

vector<OperandRole> MakeRoles(initializer_list<OperandRole> items)
{
	return vector<OperandRole>(items.begin(), items.end());
}

OperandRole MakeRole(bool write, int width_bits, OperandClass cls, bool immediate_only = false)
{
	OperandRole role;
	role.write = write;
	role.width_bits = width_bits;
	role.cls = cls;
	role.immediate_only = immediate_only;
	return role;
}

bool DescribeOpcode(const string& opcode, OpcodeInfo& out)
{
	int width = 0;

	if (opcode == "jump")
	{
		out.exec = EX_JUMP;
		out.roles = MakeRoles({MakeRole(false, 64, OC_ADDR)});
		return true;
	}
	if (opcode == "jumpif")
	{
		out.exec = EX_JUMPIF;
		out.roles = MakeRoles({MakeRole(false, 8, OC_BOOL), MakeRole(false, 64, OC_ADDR)});
		return true;
	}
	if (opcode == "call")
	{
		out.exec = EX_CALL;
		out.roles = MakeRoles({MakeRole(false, 64, OC_ADDR)});
		return true;
	}
	if (opcode == "ret")
	{
		out.exec = EX_RET;
		return true;
	}
	if (ParseTrailingWidth(opcode, "data", width) && width != 80)
	{
		out.is_data = true;
		out.data_width_bits = width;
		out.roles = MakeRoles({MakeRole(false, width, OC_BITS, true)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "move", width))
	{
		out.exec = EX_MOVE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_BITS), MakeRole(false, width, OC_BITS)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "not", width))
	{
		out.exec = EX_NOT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_BITS), MakeRole(false, width, OC_BITS)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "and", width))
	{
		out.exec = EX_AND;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_BITS), MakeRole(false, width, OC_BITS), MakeRole(false, width, OC_BITS)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "or", width))
	{
		out.exec = EX_OR;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_BITS), MakeRole(false, width, OC_BITS), MakeRole(false, width, OC_BITS)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "xor", width))
	{
		out.exec = EX_XOR;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_BITS), MakeRole(false, width, OC_BITS), MakeRole(false, width, OC_BITS)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "lshift", width))
	{
		out.exec = EX_LSHIFT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_INT), MakeRole(false, width, OC_INT), MakeRole(false, 8, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "srshift", width))
	{
		out.exec = EX_SRSHIFT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_SINT), MakeRole(false, width, OC_SINT), MakeRole(false, 8, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "urshift", width))
	{
		out.exec = EX_URSHIFT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_UINT), MakeRole(false, width, OC_UINT), MakeRole(false, 8, OC_UINT)});
		return true;
	}
	if (ParseLeadingWidth(opcode, "s", "convf80", width))
	{
		out.exec = EX_SCONVF80;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 80, OC_FLOAT), MakeRole(false, width, OC_SINT)});
		return true;
	}
	if (ParseLeadingWidth(opcode, "u", "convf80", width))
	{
		out.exec = EX_UCONVF80;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 80, OC_FLOAT), MakeRole(false, width, OC_UINT)});
		return true;
	}
	if (opcode == "f32convf80")
	{
		out.exec = EX_F32CONVF80;
		out.width_bits = 32;
		out.roles = MakeRoles({MakeRole(true, 80, OC_FLOAT), MakeRole(false, 32, OC_FLOAT)});
		return true;
	}
	if (opcode == "f64convf80")
	{
		out.exec = EX_F64CONVF80;
		out.width_bits = 64;
		out.roles = MakeRoles({MakeRole(true, 80, OC_FLOAT), MakeRole(false, 64, OC_FLOAT)});
		return true;
	}
	if (ParseLeadingWidth(opcode, "f80convs", "", width))
	{
		out.exec = EX_F80CONVS;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_SINT), MakeRole(false, 80, OC_FLOAT)});
		return true;
	}
	if (ParseLeadingWidth(opcode, "f80convu", "", width))
	{
		out.exec = EX_F80CONVU;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_UINT), MakeRole(false, 80, OC_FLOAT)});
		return true;
	}
	if (opcode == "f80convf32")
	{
		out.exec = EX_F80CONVF32;
		out.width_bits = 32;
		out.roles = MakeRoles({MakeRole(true, 32, OC_FLOAT), MakeRole(false, 80, OC_FLOAT)});
		return true;
	}
	if (opcode == "f80convf64")
	{
		out.exec = EX_F80CONVF64;
		out.width_bits = 64;
		out.roles = MakeRoles({MakeRole(true, 64, OC_FLOAT), MakeRole(false, 80, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "iadd", width))
	{
		out.exec = EX_IADD;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_INT), MakeRole(false, width, OC_INT), MakeRole(false, width, OC_INT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "isub", width))
	{
		out.exec = EX_ISUB;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_INT), MakeRole(false, width, OC_INT), MakeRole(false, width, OC_INT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "smul", width))
	{
		out.exec = EX_SMUL;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_SINT), MakeRole(false, width, OC_SINT), MakeRole(false, width, OC_SINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "umul", width))
	{
		out.exec = EX_UMUL;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_UINT), MakeRole(false, width, OC_UINT), MakeRole(false, width, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "sdiv", width))
	{
		out.exec = EX_SDIV;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_SINT), MakeRole(false, width, OC_SINT), MakeRole(false, width, OC_SINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "udiv", width))
	{
		out.exec = EX_UDIV;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_UINT), MakeRole(false, width, OC_UINT), MakeRole(false, width, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "smod", width))
	{
		out.exec = EX_SMOD;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_SINT), MakeRole(false, width, OC_SINT), MakeRole(false, width, OC_SINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "umod", width))
	{
		out.exec = EX_UMOD;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_UINT), MakeRole(false, width, OC_UINT), MakeRole(false, width, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "ieq", width))
	{
		out.exec = EX_IEQ;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_INT), MakeRole(false, width, OC_INT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "ine", width))
	{
		out.exec = EX_INE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_INT), MakeRole(false, width, OC_INT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "slt", width))
	{
		out.exec = EX_SLT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_SINT), MakeRole(false, width, OC_SINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "ult", width))
	{
		out.exec = EX_ULT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_UINT), MakeRole(false, width, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "sgt", width))
	{
		out.exec = EX_SGT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_SINT), MakeRole(false, width, OC_SINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "ugt", width))
	{
		out.exec = EX_UGT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_UINT), MakeRole(false, width, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "sle", width))
	{
		out.exec = EX_SLE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_SINT), MakeRole(false, width, OC_SINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "ule", width))
	{
		out.exec = EX_ULE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_UINT), MakeRole(false, width, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "sge", width))
	{
		out.exec = EX_SGE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_SINT), MakeRole(false, width, OC_SINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "uge", width))
	{
		out.exec = EX_UGE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_UINT), MakeRole(false, width, OC_UINT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "fadd", width))
	{
		out.exec = EX_FADD;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "fsub", width))
	{
		out.exec = EX_FSUB;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "fmul", width))
	{
		out.exec = EX_FMUL;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "fdiv", width))
	{
		out.exec = EX_FDIV;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "feq", width))
	{
		out.exec = EX_FEQ;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "fne", width))
	{
		out.exec = EX_FNE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "flt", width))
	{
		out.exec = EX_FLT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "fgt", width))
	{
		out.exec = EX_FGT;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "fle", width))
	{
		out.exec = EX_FLE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (ParseTrailingWidth(opcode, "fge", width))
	{
		out.exec = EX_FGE;
		out.width_bits = width;
		out.roles = MakeRoles({MakeRole(true, 8, OC_BOOL), MakeRole(false, width, OC_FLOAT), MakeRole(false, width, OC_FLOAT)});
		return true;
	}
	if (opcode.find("syscall") == 0 && opcode.size() == 8 && opcode[7] >= '0' && opcode[7] <= '6')
	{
		int nargs = opcode[7] - '0';
		out.exec = EX_SYSCALL;
		out.aux = nargs;
		out.roles.push_back(MakeRole(true, 64, OC_UINT));
		for (int i = 0; i < nargs + 1; ++i)
		{
			out.roles.push_back(MakeRole(false, 64, OC_UINT));
		}
		return true;
	}

	return false;
}

class CYParser
{
public:
	explicit CYParser(const vector<CYToken>& tokens)
		: tokens_(tokens)
	{}

	vector<ParsedStatement> ParseProgram()
	{
		vector<ParsedStatement> out;
		while (!AtEnd())
		{
			out.push_back(ParseStatement());
			ExpectOp(";");
		}
		return out;
	}

private:
	const vector<CYToken>& tokens_;
	size_t pos_ = 0;

	bool AtEnd() const
	{
		return pos_ >= tokens_.size();
	}

	const CYToken& Peek() const
	{
		if (AtEnd())
		{
			throw runtime_error("unexpected end of input");
		}
		return tokens_[pos_];
	}

	bool MatchOp(const string& op)
	{
		if (!AtEnd() &&
			Peek().kind == PPT_PREPROCESSING_OP_OR_PUNC &&
			Peek().source == op)
		{
			++pos_;
			return true;
		}
		return false;
	}

	void ExpectOp(const string& op)
	{
		if (!MatchOp(op))
		{
			throw runtime_error("expected `" + op + "`");
		}
	}

	string ExpectIdentifier()
	{
		if (AtEnd() || Peek().kind != PPT_IDENTIFIER || IsKeywordIdentifier(Peek().source))
		{
			throw runtime_error("expected identifier");
		}
		return tokens_[pos_++].source;
	}

	ParsedValueExpr ParseValueExpr(bool allow_label)
	{
		ParsedValueExpr expr;
		if (MatchOp("-"))
		{
			if (AtEnd() || !IsLiteralToken(Peek()))
			{
				throw runtime_error("expected literal after `-`");
			}
			expr.kind = ParsedValueExpr::VE_LITERAL;
			expr.literal_source = tokens_[pos_++].source;
			expr.literal_negated = true;
			return expr;
		}

		if (!AtEnd() && IsLiteralToken(Peek()))
		{
			expr.kind = ParsedValueExpr::VE_LITERAL;
			expr.literal_source = tokens_[pos_++].source;
			return expr;
		}

		if (allow_label && !AtEnd() && Peek().kind == PPT_IDENTIFIER && !IsKeywordIdentifier(Peek().source))
		{
			expr.kind = ParsedValueExpr::VE_LABEL;
			expr.label = tokens_[pos_++].source;
			if (MatchOp("+") || MatchOp("-"))
			{
				bool neg = tokens_[pos_ - 1].source == "-";
				if (AtEnd() || !IsLiteralToken(Peek()))
				{
					throw runtime_error("expected literal after label offset");
				}
				expr.has_offset = true;
				expr.offset_negated = neg;
				expr.offset_literal = tokens_[pos_++].source;
			}
			return expr;
		}

		throw runtime_error("expected operand value");
	}

	ParsedOperand ParseOperand()
	{
		ParsedOperand operand;
		if (!AtEnd() && Peek().kind == PPT_IDENTIFIER && !IsKeywordIdentifier(Peek().source))
		{
			RegId reg;
			if (IsRegisterName(Peek().source, reg))
			{
				operand.kind = ParsedOperand::PO_REG;
				operand.reg = reg;
				++pos_;
				return operand;
			}

			operand.kind = ParsedOperand::PO_IMM;
			operand.expr.kind = ParsedValueExpr::VE_LABEL;
			operand.expr.label = tokens_[pos_++].source;
			if (MatchOp("+") || MatchOp("-"))
			{
				bool neg = tokens_[pos_ - 1].source == "-";
				if (AtEnd() || !IsLiteralToken(Peek()))
				{
					throw runtime_error("expected literal after label offset");
				}
				operand.expr.has_offset = true;
				operand.expr.offset_negated = neg;
				operand.expr.offset_literal = tokens_[pos_++].source;
			}
			return operand;
		}

		if (MatchOp("("))
		{
			operand.kind = ParsedOperand::PO_IMM;
			operand.expr = ParseValueExpr(true);
			ExpectOp(")");
			return operand;
		}

		if (MatchOp("["))
		{
			operand.kind = ParsedOperand::PO_MEM;
			if (!AtEnd() && Peek().kind == PPT_IDENTIFIER && !IsKeywordIdentifier(Peek().source))
			{
				RegId reg;
				if (IsRegisterName(Peek().source, reg))
				{
					operand.mem_reg_base = true;
					operand.mem_base_reg = reg;
					++pos_;
					if (MatchOp("+") || MatchOp("-"))
					{
						bool neg = tokens_[pos_ - 1].source == "-";
						if (AtEnd() || !IsLiteralToken(Peek()))
						{
							throw runtime_error("expected literal after register offset");
						}
						operand.expr.kind = ParsedValueExpr::VE_LITERAL;
						operand.expr.has_offset = true;
						operand.expr.offset_negated = neg;
						operand.expr.offset_literal = tokens_[pos_++].source;
					}
					ExpectOp("]");
					return operand;
				}
			}

			operand.expr = ParseValueExpr(true);
			ExpectOp("]");
			return operand;
		}

		operand.kind = ParsedOperand::PO_IMM;
		operand.expr = ParseValueExpr(false);
		return operand;
	}

	ParsedStatement ParseStatement()
	{
		if (AtEnd())
		{
			throw runtime_error("unexpected end of input");
		}

		ParsedStatement stmt;
		stmt.file = Peek().file;
		stmt.line = Peek().line;

		while (pos_ + 1 < tokens_.size() &&
			tokens_[pos_].kind == PPT_IDENTIFIER &&
			!IsKeywordIdentifier(tokens_[pos_].source) &&
			tokens_[pos_ + 1].kind == PPT_PREPROCESSING_OP_OR_PUNC &&
			tokens_[pos_ + 1].source == ":")
		{
			stmt.labels.push_back(tokens_[pos_].source);
			pos_ += 2;
		}

		if (AtEnd())
		{
			throw runtime_error("expected statement body");
		}

		if (MatchOp("-"))
		{
			if (AtEnd() || !IsLiteralToken(Peek()))
			{
				throw runtime_error("expected literal after unary minus");
			}
			stmt.is_literal_data = true;
			stmt.literal_negated = true;
			stmt.literal_source = tokens_[pos_++].source;
			return stmt;
		}

		if (IsLiteralToken(Peek()))
		{
			stmt.is_literal_data = true;
			stmt.literal_source = tokens_[pos_++].source;
			return stmt;
		}

		stmt.opcode = ExpectIdentifier();
		while (!AtEnd() &&
			!(Peek().kind == PPT_PREPROCESSING_OP_OR_PUNC && Peek().source == ";"))
		{
			stmt.operands.push_back(ParseOperand());
		}
		return stmt;
	}
};

struct ResolvedOperand
{
	enum Kind
	{
		RO_REG = 1,
		RO_IMM_INT,
		RO_IMM_F32,
		RO_IMM_F64,
		RO_IMM_F80,
		RO_MEM_ABS,
		RO_MEM_REG
	};

	Kind kind = RO_REG;
	RegId reg = REG_X64;
	uint64_t u64 = 0;
	vector<unsigned char> bytes;
};

struct ResolvedInstruction
{
	uint64_t addr = 0;
	uint64_t next_addr = 0;
	ExecClass exec = EX_MOVE;
	int width_bits = 0;
	int aux = 0;
	vector<ResolvedOperand> operands;
};

struct Program
{
	uint64_t entry = 0;
	vector<unsigned char> image;
	vector<ResolvedInstruction> instructions;
};

uint64_t ConvertOffsetLiteral(const string& source, bool negated)
{
	LiteralValue literal = ParseLiteralValue(source);
	uint64_t value = ConvertIntegralLiteral(literal, 64);
	if (negated)
	{
		value = (~value + 1ULL);
	}
	return value;
}

ResolvedOperand ResolveValueExprAsImmediate(const ParsedValueExpr& expr, const OperandRole& role, const unordered_map<string, uint64_t>& labels)
{
	ResolvedOperand out;
	if (role.cls == OC_FLOAT)
	{
		LiteralValue literal;
		if (expr.kind == ParsedValueExpr::VE_LABEL)
		{
			throw runtime_error("floating operand cannot use label");
		}
		literal = ParseLiteralValue(expr.literal_source);
		if (expr.literal_negated)
		{
			literal = NegateLiteralValue(literal);
		}

		long double value = literal.numeric;
		if (role.width_bits == 32)
		{
			float x = static_cast<float>(value);
			out.kind = ResolvedOperand::RO_IMM_F32;
			out.bytes = EncodeObject(x);
			return out;
		}
		if (role.width_bits == 64)
		{
			double x = static_cast<double>(value);
			out.kind = ResolvedOperand::RO_IMM_F64;
			out.bytes = EncodeObject(x);
			return out;
		}
		long double x = value;
		out.kind = ResolvedOperand::RO_IMM_F80;
		out.bytes = EncodeObject(x);
		return out;
	}

	out.kind = ResolvedOperand::RO_IMM_INT;
	if (expr.kind == ParsedValueExpr::VE_LITERAL)
	{
		LiteralValue literal = ParseLiteralValue(expr.literal_source);
		if (expr.literal_negated)
		{
			literal = NegateLiteralValue(literal);
		}
		out.u64 = ConvertLiteralBits(literal, role.width_bits ? role.width_bits : 64);
		return out;
	}

	unordered_map<string, uint64_t>::const_iterator it = labels.find(expr.label);
	if (it == labels.end())
	{
		throw runtime_error("unknown label `" + expr.label + "`");
	}
	uint64_t value = it->second;
	if (expr.has_offset)
	{
		value += ConvertOffsetLiteral(expr.offset_literal, expr.offset_negated);
	}
	out.u64 = value & MaskBits(role.width_bits ? role.width_bits : 64);
	return out;
}

ResolvedOperand ResolveOperand(const ParsedOperand& operand, const OperandRole& role, const unordered_map<string, uint64_t>& labels)
{
	ResolvedOperand out;
	if (operand.kind == ParsedOperand::PO_REG)
	{
		if (role.immediate_only)
		{
			throw runtime_error("immediate required");
		}
		if (RegisterWidthBits(operand.reg) != role.width_bits)
		{
			throw runtime_error("register width mismatch");
		}
		out.kind = ResolvedOperand::RO_REG;
		out.reg = operand.reg;
		return out;
	}

	if (operand.kind == ParsedOperand::PO_IMM)
	{
		return ResolveValueExprAsImmediate(operand.expr, role, labels);
	}

	if (role.immediate_only)
	{
		throw runtime_error("immediate required");
	}
	if (operand.mem_reg_base)
	{
		if (RegisterWidthBits(operand.mem_base_reg) != 64)
		{
			throw runtime_error("memory address must use 64-bit register");
		}
		out.kind = ResolvedOperand::RO_MEM_REG;
		out.reg = operand.mem_base_reg;
		out.u64 = operand.expr.has_offset ? ConvertOffsetLiteral(operand.expr.offset_literal, operand.expr.offset_negated) : 0;
		return out;
	}

	out.kind = ResolvedOperand::RO_MEM_ABS;
	OperandRole addr_role = MakeRole(false, 64, OC_ADDR);
	ResolvedOperand imm = ResolveValueExprAsImmediate(operand.expr, addr_role, labels);
	out.u64 = imm.u64;
	return out;
}

vector<unsigned char> ConvertLiteralToDataBytes(const LiteralValue& literal, int width_bits)
{
	if (literal.kind == LiteralValue::LV_STRING)
	{
		// raw literal-data width conversion follows CY86 immediate rules
	}

	uint64_t raw = ConvertLiteralBits(literal, width_bits);
	vector<unsigned char> out(width_bits / 8, 0);
	for (size_t i = 0; i < out.size(); ++i)
	{
		out[i] = static_cast<unsigned char>((raw >> (8 * i)) & 0xFF);
	}
	return out;
}

Program BuildProgram(const vector<ParsedStatement>& parsed)
{
	vector<ParsedStatement> statements = parsed;
	unordered_map<string, uint64_t> labels;

	for (const ParsedStatement& stmt : statements)
	{
		for (const string& label : stmt.labels)
		{
			if (IsKeywordIdentifier(label))
			{
				throw runtime_error("keyword cannot be label");
			}
			RegId reg;
			OpcodeInfo info;
			if (labels.find(label) != labels.end())
			{
				throw runtime_error("duplicate label `" + label + "`");
			}
			if (IsRegisterName(label, reg) || DescribeOpcode(label, info))
			{
				throw runtime_error("label conflicts with reserved name `" + label + "`");
			}
		}
	}

	uint64_t cursor = 0;
	for (size_t i = 0; i < statements.size(); ++i)
	{
		ParsedStatement& stmt = statements[i];
		stmt.align = 1;
		stmt.size = 1;
		stmt.is_runtime_instruction = true;

		if (stmt.is_literal_data)
		{
			LiteralValue literal = ParseLiteralValue(stmt.literal_source);
			if (stmt.literal_negated)
			{
				literal = NegateLiteralValue(literal);
			}
			stmt.align = literal.kind == LiteralValue::LV_STRING ? 1 : max<uint64_t>(1, literal.nbytes);
			stmt.size = literal.nbytes;
			stmt.is_runtime_instruction = false;
		}
		else
		{
			OpcodeInfo info;
			if (!DescribeOpcode(stmt.opcode, info))
			{
				throw runtime_error("unknown opcode `" + stmt.opcode + "`");
			}
			if (info.is_data)
			{
				stmt.align = info.data_width_bits / 8;
				stmt.size = info.data_width_bits / 8;
				stmt.is_runtime_instruction = false;
			}
		}

		if (stmt.align > 1 && cursor % stmt.align != 0)
		{
			cursor += stmt.align - (cursor % stmt.align);
		}
		stmt.addr = cursor;
		for (const string& label : stmt.labels)
		{
			labels[label] = stmt.addr;
		}
		cursor += stmt.size;
	}

	Program program;
	program.image.assign(cursor, 0);
	if (!statements.empty())
	{
		unordered_map<string, uint64_t>::const_iterator it = labels.find("start");
		program.entry = it != labels.end() ? it->second : statements.front().addr;
	}

	for (size_t i = 0; i < statements.size(); ++i)
	{
		const ParsedStatement& stmt = statements[i];
		uint64_t next_addr = i + 1 < statements.size() ? statements[i + 1].addr : cursor;
		if (stmt.is_literal_data)
		{
			LiteralValue literal = ParseLiteralValue(stmt.literal_source);
			if (stmt.literal_negated)
			{
				literal = NegateLiteralValue(literal);
			}
			copy(literal.bytes.begin(), literal.bytes.end(), program.image.begin() + stmt.addr);
			continue;
		}

		OpcodeInfo info;
		if (!DescribeOpcode(stmt.opcode, info))
		{
			throw runtime_error("unknown opcode `" + stmt.opcode + "`");
		}

		if (stmt.operands.size() != info.roles.size())
		{
			throw runtime_error("wrong number of operands for `" + stmt.opcode + "`");
		}

		if (info.is_data)
		{
			const ParsedOperand& operand = stmt.operands.front();
			if (operand.kind != ParsedOperand::PO_IMM)
			{
				throw runtime_error("data operand must be immediate");
			}
			if (operand.expr.kind != ParsedValueExpr::VE_LITERAL)
			{
				throw runtime_error("data operand must use literal");
			}
			LiteralValue literal = ParseLiteralValue(operand.expr.literal_source);
			if (operand.expr.literal_negated)
			{
				literal = NegateLiteralValue(literal);
			}
			vector<unsigned char> bytes = ConvertLiteralToDataBytes(literal, info.data_width_bits);
			copy(bytes.begin(), bytes.end(), program.image.begin() + stmt.addr);
			continue;
		}

		ResolvedInstruction inst;
		inst.addr = stmt.addr;
		inst.next_addr = next_addr;
		inst.exec = info.exec;
		inst.width_bits = info.width_bits;
		inst.aux = info.aux;
		for (size_t j = 0; j < stmt.operands.size(); ++j)
		{
			inst.operands.push_back(ResolveOperand(stmt.operands[j], info.roles[j], labels));
		}
		program.instructions.push_back(inst);
	}

	return program;
}

void WriteU8(vector<unsigned char>& out, unsigned char value)
{
	out.push_back(value);
}

void WriteU16(vector<unsigned char>& out, uint16_t value)
{
	out.push_back(static_cast<unsigned char>(value & 0xFF));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

void WriteU64(vector<unsigned char>& out, uint64_t value)
{
	for (int i = 0; i < 8; ++i)
	{
		out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFF));
	}
}

void WriteBytes(vector<unsigned char>& out, const vector<unsigned char>& bytes)
{
	WriteU64(out, bytes.size());
	out.insert(out.end(), bytes.begin(), bytes.end());
}

vector<unsigned char> SerializeProgram(const Program& program)
{
	vector<unsigned char> out;
	WriteU64(out, 0x314d475050435943ULL);
	WriteU64(out, program.entry);
	WriteBytes(out, program.image);
	WriteU64(out, program.instructions.size());
	for (const ResolvedInstruction& inst : program.instructions)
	{
		WriteU64(out, inst.addr);
		WriteU64(out, inst.next_addr);
		WriteU16(out, static_cast<uint16_t>(inst.exec));
		WriteU16(out, static_cast<uint16_t>(inst.width_bits));
		WriteU16(out, static_cast<uint16_t>(inst.aux));
		WriteU16(out, static_cast<uint16_t>(inst.operands.size()));
		for (const ResolvedOperand& operand : inst.operands)
		{
			WriteU8(out, static_cast<unsigned char>(operand.kind));
			WriteU8(out, static_cast<unsigned char>(operand.reg));
			WriteU64(out, operand.u64);
			WriteBytes(out, operand.bytes);
		}
	}
	return out;
}

struct ByteReader
{
	const vector<unsigned char>& bytes;
	size_t pos = 0;

	explicit ByteReader(const vector<unsigned char>& bytes)
		: bytes(bytes)
	{}

	uint8_t ReadU8()
	{
		if (pos >= bytes.size())
		{
			throw runtime_error("corrupt program blob");
		}
		return bytes[pos++];
	}

	uint16_t ReadU16()
	{
		uint16_t out = 0;
		for (int i = 0; i < 2; ++i)
		{
			out |= static_cast<uint16_t>(ReadU8()) << (8 * i);
		}
		return out;
	}

	uint64_t ReadU64()
	{
		uint64_t out = 0;
		for (int i = 0; i < 8; ++i)
		{
			out |= static_cast<uint64_t>(ReadU8()) << (8 * i);
		}
		return out;
	}

	vector<unsigned char> ReadBytes()
	{
		uint64_t n = ReadU64();
		if (pos + n > bytes.size())
		{
			throw runtime_error("corrupt program blob");
		}
		vector<unsigned char> out(bytes.begin() + pos, bytes.begin() + pos + n);
		pos += n;
		return out;
	}
};

Program DeserializeProgram(const vector<unsigned char>& data)
{
	ByteReader rd{data};
	if (rd.ReadU64() != 0x314d475050435943ULL)
	{
		throw runtime_error("invalid program blob");
	}

	Program program;
	program.entry = rd.ReadU64();
	program.image = rd.ReadBytes();
	uint64_t ninst = rd.ReadU64();
	for (uint64_t i = 0; i < ninst; ++i)
	{
		ResolvedInstruction inst;
		inst.addr = rd.ReadU64();
		inst.next_addr = rd.ReadU64();
		inst.exec = static_cast<ExecClass>(rd.ReadU16());
		inst.width_bits = rd.ReadU16();
		inst.aux = rd.ReadU16();
		uint16_t nops = rd.ReadU16();
		for (uint16_t j = 0; j < nops; ++j)
		{
			ResolvedOperand operand;
			operand.kind = static_cast<ResolvedOperand::Kind>(rd.ReadU8());
			operand.reg = static_cast<RegId>(rd.ReadU8());
			operand.u64 = rd.ReadU64();
			operand.bytes = rd.ReadBytes();
			inst.operands.push_back(operand);
		}
		program.instructions.push_back(inst);
	}
	return program;
}

const string kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string Base64Encode(const vector<unsigned char>& data)
{
	string out;
	for (size_t i = 0; i < data.size(); i += 3)
	{
		unsigned int value = static_cast<unsigned int>(data[i]) << 16;
		bool has1 = i + 1 < data.size();
		bool has2 = i + 2 < data.size();
		if (has1) value |= static_cast<unsigned int>(data[i + 1]) << 8;
		if (has2) value |= static_cast<unsigned int>(data[i + 2]);

		out += kBase64Alphabet[(value >> 18) & 0x3F];
		out += kBase64Alphabet[(value >> 12) & 0x3F];
		out += has1 ? kBase64Alphabet[(value >> 6) & 0x3F] : '=';
		out += has2 ? kBase64Alphabet[value & 0x3F] : '=';
	}
	return out;
}

vector<unsigned char> Base64Decode(const string& text)
{
	vector<int> table(256, -1);
	for (size_t i = 0; i < kBase64Alphabet.size(); ++i)
	{
		table[static_cast<unsigned char>(kBase64Alphabet[i])] = static_cast<int>(i);
	}

	vector<unsigned char> out;
	unsigned int value = 0;
	int bits = -8;
	for (unsigned char c : text)
	{
		if (isspace(c))
		{
			continue;
		}
		if (c == '=')
		{
			break;
		}
		int decoded = c < table.size() ? table[c] : -1;
		if (decoded < 0)
		{
			throw runtime_error("invalid base64");
		}
		value = (value << 6) | static_cast<unsigned int>(decoded);
		bits += 6;
		if (bits >= 0)
		{
			out.push_back(static_cast<unsigned char>((value >> bits) & 0xFF));
			bits -= 8;
		}
	}
	return out;
}

class Memory
{
public:
	Memory()
	{
		AllocateSegment(0x6ffffff00000ULL, 1ULL << 20);
	}

	void LoadImage(const vector<unsigned char>& image)
	{
		segments_.push_back(Segment{0, image});
	}

	void AllocateSegment(uint64_t addr, uint64_t size)
	{
		segments_.push_back(Segment{addr, vector<unsigned char>(size, 0)});
	}

	uint8_t ReadByte(uint64_t addr) const
	{
		const Segment* seg = FindSegment(addr, 1);
		if (!seg)
		{
			return 0;
		}
		return seg->bytes[static_cast<size_t>(addr - seg->base)];
	}

	void WriteByte(uint64_t addr, uint8_t value)
	{
		Segment* seg = FindMutableSegment(addr, 1);
		if (!seg)
		{
			ostringstream oss;
			oss << "write to unmapped memory @0x" << hex << addr;
			throw runtime_error(oss.str());
		}
		seg->bytes[static_cast<size_t>(addr - seg->base)] = value;
	}

	uint64_t ReadUnsigned(uint64_t addr, int bits) const
	{
		const Segment* seg = FindSegment(addr, static_cast<size_t>(bits / 8));
		if (seg)
		{
			uint64_t out = 0;
			size_t offset = static_cast<size_t>(addr - seg->base);
			for (int i = 0; i < bits / 8; ++i)
			{
				out |= static_cast<uint64_t>(seg->bytes[offset + static_cast<size_t>(i)]) << (8 * i);
			}
			return out & MaskBits(bits);
		}

		uint64_t out = 0;
		for (int i = 0; i < bits / 8; ++i)
		{
			out |= static_cast<uint64_t>(ReadByte(addr + static_cast<uint64_t>(i))) << (8 * i);
		}
		return out & MaskBits(bits);
	}

	void WriteUnsigned(uint64_t addr, int bits, uint64_t value)
	{
		Segment* seg = FindMutableSegment(addr, static_cast<size_t>(bits / 8));
		if (seg)
		{
			size_t offset = static_cast<size_t>(addr - seg->base);
			for (int i = 0; i < bits / 8; ++i)
			{
				seg->bytes[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
			}
			return;
		}

		for (int i = 0; i < bits / 8; ++i)
		{
			WriteByte(addr + static_cast<uint64_t>(i), static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
		}
	}

	float ReadFloat32(uint64_t addr) const
	{
		float out = 0;
		const Segment* seg = FindSegment(addr, sizeof(float));
		if (seg)
		{
			memcpy(&out, &seg->bytes[static_cast<size_t>(addr - seg->base)], sizeof(float));
			return out;
		}
		unsigned char bytes[sizeof(float)] = {};
		for (size_t i = 0; i < sizeof(float); ++i)
		{
			bytes[i] = ReadByte(addr + i);
		}
		memcpy(&out, bytes, sizeof(float));
		return out;
	}

	double ReadFloat64(uint64_t addr) const
	{
		double out = 0;
		const Segment* seg = FindSegment(addr, sizeof(double));
		if (seg)
		{
			memcpy(&out, &seg->bytes[static_cast<size_t>(addr - seg->base)], sizeof(double));
			return out;
		}
		unsigned char bytes[sizeof(double)] = {};
		for (size_t i = 0; i < sizeof(double); ++i)
		{
			bytes[i] = ReadByte(addr + i);
		}
		memcpy(&out, bytes, sizeof(double));
		return out;
	}

	long double ReadFloat80(uint64_t addr) const
	{
		long double out = 0;
		unsigned char bytes[sizeof(long double)] = {};
		const Segment* seg = FindSegment(addr, 10);
		if (seg)
		{
			memcpy(bytes, &seg->bytes[static_cast<size_t>(addr - seg->base)], 10);
			memcpy(&out, bytes, sizeof(long double));
			return out;
		}
		for (int i = 0; i < 10; ++i)
		{
			bytes[i] = ReadByte(addr + static_cast<uint64_t>(i));
		}
		memcpy(&out, bytes, sizeof(long double));
		return out;
	}

	void WriteFloat32(uint64_t addr, float value)
	{
		vector<unsigned char> bytes = EncodeObject(value);
		Segment* seg = FindMutableSegment(addr, bytes.size());
		if (seg)
		{
			memcpy(&seg->bytes[static_cast<size_t>(addr - seg->base)], bytes.data(), bytes.size());
			return;
		}
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			WriteByte(addr + i, bytes[i]);
		}
	}

	void WriteFloat64(uint64_t addr, double value)
	{
		vector<unsigned char> bytes = EncodeObject(value);
		Segment* seg = FindMutableSegment(addr, bytes.size());
		if (seg)
		{
			memcpy(&seg->bytes[static_cast<size_t>(addr - seg->base)], bytes.data(), bytes.size());
			return;
		}
		for (size_t i = 0; i < bytes.size(); ++i)
		{
			WriteByte(addr + i, bytes[i]);
		}
	}

	void WriteFloat80(uint64_t addr, long double value)
	{
		vector<unsigned char> bytes = EncodeObject(value);
		Segment* seg = FindMutableSegment(addr, 10);
		if (seg)
		{
			memcpy(&seg->bytes[static_cast<size_t>(addr - seg->base)], bytes.data(), 10);
			return;
		}
		for (int i = 0; i < 10; ++i)
		{
			WriteByte(addr + static_cast<uint64_t>(i), bytes[i]);
		}
	}

	void ReadBuffer(uint64_t addr, char* out, size_t n) const
	{
		const Segment* seg = FindSegment(addr, n);
		if (seg)
		{
			memcpy(out, &seg->bytes[static_cast<size_t>(addr - seg->base)], n);
			return;
		}
		for (size_t i = 0; i < n; ++i)
		{
			out[i] = static_cast<char>(ReadByte(addr + i));
		}
	}

	void WriteBuffer(uint64_t addr, const char* data, size_t n)
	{
		Segment* seg = FindMutableSegment(addr, n);
		if (seg)
		{
			memcpy(&seg->bytes[static_cast<size_t>(addr - seg->base)], data, n);
			return;
		}
		{
			ostringstream oss;
			oss << "write buffer to unmapped memory @0x" << hex << addr << " size 0x" << n;
			throw runtime_error(oss.str());
		}
		for (size_t i = 0; i < n; ++i)
		{
			WriteByte(addr + i, static_cast<unsigned char>(data[i]));
		}
	}

private:
	struct Segment
	{
		uint64_t base = 0;
		vector<unsigned char> bytes;

		Segment()
		{}

		Segment(uint64_t base, const vector<unsigned char>& bytes)
			: base(base), bytes(bytes)
		{}
	};

	vector<Segment> segments_;

	const Segment* FindSegment(uint64_t addr, size_t n) const
	{
		for (vector<Segment>::const_iterator it = segments_.begin(); it != segments_.end(); ++it)
		{
			if (addr >= it->base && addr + n >= addr && addr + n <= it->base + it->bytes.size())
			{
				return &*it;
			}
		}
		return nullptr;
	}

	Segment* FindMutableSegment(uint64_t addr, size_t n)
	{
		for (vector<Segment>::iterator it = segments_.begin(); it != segments_.end(); ++it)
		{
			if (addr >= it->base && addr + n >= addr && addr + n <= it->base + it->bytes.size())
			{
				return &*it;
			}
		}
		return nullptr;
	}
};

uint64_t ToUnsignedWidth(uint64_t value, int bits)
{
	return value & MaskBits(bits);
}

template <typename T>
T BitsAsSigned(uint64_t value)
{
	return static_cast<T>(value);
}

uint64_t ReadRegisterValue(const uint64_t* regs, RegId reg)
{
	switch (reg)
	{
	case REG_SP: return regs[0];
	case REG_BP: return regs[1];
	case REG_X8:
	case REG_X16:
	case REG_X32:
	case REG_X64:
		return regs[2];
	case REG_Y8:
	case REG_Y16:
	case REG_Y32:
	case REG_Y64:
		return regs[3];
	case REG_Z8:
	case REG_Z16:
	case REG_Z32:
	case REG_Z64:
		return regs[4];
	case REG_T8:
	case REG_T16:
	case REG_T32:
	case REG_T64:
		return regs[5];
	}
	return 0;
}

void WriteRegisterValue(uint64_t* regs, RegId reg, uint64_t value)
{
	uint64_t* slot = nullptr;
	int width = RegisterWidthBits(reg);
	switch (reg)
	{
	case REG_SP: slot = &regs[0]; break;
	case REG_BP: slot = &regs[1]; break;
	case REG_X8:
	case REG_X16:
	case REG_X32:
	case REG_X64:
		slot = &regs[2];
		break;
	case REG_Y8:
	case REG_Y16:
	case REG_Y32:
	case REG_Y64:
		slot = &regs[3];
		break;
	case REG_Z8:
	case REG_Z16:
	case REG_Z32:
	case REG_Z64:
		slot = &regs[4];
		break;
	case REG_T8:
	case REG_T16:
	case REG_T32:
	case REG_T64:
		slot = &regs[5];
		break;
	}

	if (!slot)
	{
		return;
	}

	if (width == 64)
	{
		*slot = value;
	}
	else if (width == 32)
	{
		*slot = value & 0xFFFFFFFFULL;
	}
	else if (width == 16)
	{
		*slot = (*slot & ~0xFFFFULL) | (value & 0xFFFFULL);
	}
	else if (width == 8)
	{
		*slot = (*slot & ~0xFFULL) | (value & 0xFFULL);
	}
}

class Interpreter
{
public:
	explicit Interpreter(const Program& program)
		: program_(program)
	{
		memory_.LoadImage(program.image);
		for (size_t i = 0; i < program.instructions.size(); ++i)
		{
			addr_to_index_[program.instructions[i].addr] = i;
		}
		regs_[0] = 0x700000000000ULL;
		regs_[1] = 0;
		pc_ = program.entry;
	}

	int Run()
	{
		ostringstream input_ss;
		input_ss << cin.rdbuf();
		input_ = input_ss.str();
		input_pos_ = 0;

		for (;;)
		{
			unordered_map<uint64_t, size_t>::const_iterator it = addr_to_index_.find(pc_);
			if (it == addr_to_index_.end())
			{
				throw runtime_error("invalid instruction address");
			}
			const ResolvedInstruction& inst = program_.instructions[it->second];
			if (Execute(inst))
			{
				return exit_status_;
			}
		}
	}

private:
	const Program& program_;
	Memory memory_;
	unordered_map<uint64_t, size_t> addr_to_index_;
	uint64_t regs_[6] = {};
	uint64_t pc_ = 0;
	string input_;
	size_t input_pos_ = 0;
	int exit_status_ = 0;

	uint64_t AddressOf(const ResolvedOperand& operand) const
	{
		if (operand.kind == ResolvedOperand::RO_MEM_ABS)
		{
			return operand.u64;
		}
		if (operand.kind == ResolvedOperand::RO_MEM_REG)
		{
			return ReadRegisterValue(regs_, operand.reg) + operand.u64;
		}
		throw runtime_error("not a memory operand");
	}

	uint64_t ReadBitsOperand(const ResolvedOperand& operand, int bits) const
	{
		switch (operand.kind)
		{
		case ResolvedOperand::RO_REG:
			return ToUnsignedWidth(ReadRegisterValue(regs_, operand.reg), bits);
		case ResolvedOperand::RO_IMM_INT:
			return ToUnsignedWidth(operand.u64, bits);
		case ResolvedOperand::RO_MEM_ABS:
		case ResolvedOperand::RO_MEM_REG:
			return memory_.ReadUnsigned(AddressOf(operand), bits);
		default:
			throw runtime_error("invalid integer operand kind");
		}
	}

	long double ReadFloatOperand(const ResolvedOperand& operand, int bits) const
	{
		switch (operand.kind)
		{
		case ResolvedOperand::RO_IMM_F32:
		{
			float x = 0;
			memcpy(&x, operand.bytes.data(), sizeof(float));
			return static_cast<long double>(x);
		}
		case ResolvedOperand::RO_IMM_F64:
		{
			double x = 0;
			memcpy(&x, operand.bytes.data(), sizeof(double));
			return static_cast<long double>(x);
		}
		case ResolvedOperand::RO_IMM_F80:
		{
			long double x = 0;
			memcpy(&x, operand.bytes.data(), sizeof(long double));
			return x;
		}
		case ResolvedOperand::RO_REG:
			if (bits == 32)
			{
				uint32_t raw = static_cast<uint32_t>(ReadBitsOperand(operand, 32));
				float x = 0;
				memcpy(&x, &raw, sizeof(x));
				return static_cast<long double>(x);
			}
			if (bits == 64)
			{
				uint64_t raw = ReadBitsOperand(operand, 64);
				double x = 0;
				memcpy(&x, &raw, sizeof(x));
				return static_cast<long double>(x);
			}
			break;
		case ResolvedOperand::RO_MEM_ABS:
		case ResolvedOperand::RO_MEM_REG:
			if (bits == 32) return static_cast<long double>(memory_.ReadFloat32(AddressOf(operand)));
			if (bits == 64) return static_cast<long double>(memory_.ReadFloat64(AddressOf(operand)));
			if (bits == 80) return memory_.ReadFloat80(AddressOf(operand));
			break;
		default:
			break;
		}
		throw runtime_error("invalid floating operand kind");
	}

	void WriteBitsOperand(const ResolvedOperand& operand, int bits, uint64_t value)
	{
		value &= MaskBits(bits);
		switch (operand.kind)
		{
		case ResolvedOperand::RO_REG:
			WriteRegisterValue(regs_, operand.reg, value);
			return;
		case ResolvedOperand::RO_MEM_ABS:
		case ResolvedOperand::RO_MEM_REG:
			memory_.WriteUnsigned(AddressOf(operand), bits, value);
			return;
		default:
			throw runtime_error("invalid write operand");
		}
	}

	void WriteFloatOperand(const ResolvedOperand& operand, int bits, long double value)
	{
		switch (operand.kind)
		{
		case ResolvedOperand::RO_REG:
			if (bits == 32)
			{
				float x = static_cast<float>(value);
				uint32_t raw = 0;
				memcpy(&raw, &x, sizeof(x));
				WriteRegisterValue(regs_, operand.reg, raw);
				return;
			}
			if (bits == 64)
			{
				double x = static_cast<double>(value);
				uint64_t raw = 0;
				memcpy(&raw, &x, sizeof(x));
				WriteRegisterValue(regs_, operand.reg, raw);
				return;
			}
			break;
		case ResolvedOperand::RO_MEM_ABS:
		case ResolvedOperand::RO_MEM_REG:
			if (bits == 32)
			{
				memory_.WriteFloat32(AddressOf(operand), static_cast<float>(value));
				return;
			}
			if (bits == 64)
			{
				memory_.WriteFloat64(AddressOf(operand), static_cast<double>(value));
				return;
			}
			if (bits == 80)
			{
				memory_.WriteFloat80(AddressOf(operand), value);
				return;
			}
			break;
		default:
			break;
		}
		throw runtime_error("invalid floating write operand");
	}

	template <typename T>
	uint64_t DoSignedCmp(ExecClass exec, uint64_t lhs_bits, uint64_t rhs_bits, int bits) const
	{
		T lhs = static_cast<T>(SignExtendTo64(lhs_bits, bits));
		T rhs = static_cast<T>(SignExtendTo64(rhs_bits, bits));
		switch (exec)
		{
		case EX_SLT: return lhs < rhs;
		case EX_SGT: return lhs > rhs;
		case EX_SLE: return lhs <= rhs;
		case EX_SGE: return lhs >= rhs;
		default: break;
		}
		throw runtime_error("invalid signed comparison");
	}

	template <typename T>
	uint64_t DoUnsignedCmp(ExecClass exec, uint64_t lhs_bits, uint64_t rhs_bits) const
	{
		T lhs = static_cast<T>(lhs_bits);
		T rhs = static_cast<T>(rhs_bits);
		switch (exec)
		{
		case EX_ULT: return lhs < rhs;
		case EX_UGT: return lhs > rhs;
		case EX_ULE: return lhs <= rhs;
		case EX_UGE: return lhs >= rhs;
		default: break;
		}
		throw runtime_error("invalid unsigned comparison");
	}

	template <typename T>
	uint64_t DoSignedMath(ExecClass exec, uint64_t lhs_bits, uint64_t rhs_bits, int bits) const
	{
		T lhs = static_cast<T>(SignExtendTo64(lhs_bits, bits));
		T rhs = static_cast<T>(SignExtendTo64(rhs_bits, bits));
		switch (exec)
		{
		case EX_SMUL: return static_cast<uint64_t>(static_cast<T>(lhs * rhs));
		case EX_SDIV: return static_cast<uint64_t>(static_cast<T>(lhs / rhs));
		case EX_SMOD: return static_cast<uint64_t>(static_cast<T>(lhs % rhs));
		case EX_SRSHIFT:
			return static_cast<uint64_t>(static_cast<T>(lhs >> static_cast<unsigned int>(rhs_bits & 0xFF)));
		default: break;
		}
		throw runtime_error("invalid signed math");
	}

	template <typename T>
	uint64_t DoUnsignedMath(ExecClass exec, uint64_t lhs_bits, uint64_t rhs_bits) const
	{
		T lhs = static_cast<T>(lhs_bits);
		T rhs = static_cast<T>(rhs_bits);
		switch (exec)
		{
		case EX_UMUL: return static_cast<uint64_t>(static_cast<T>(lhs * rhs));
		case EX_UDIV: return static_cast<uint64_t>(static_cast<T>(lhs / rhs));
		case EX_UMOD: return static_cast<uint64_t>(static_cast<T>(lhs % rhs));
		case EX_URSHIFT: return static_cast<uint64_t>(static_cast<T>(lhs >> static_cast<unsigned int>(rhs_bits & 0xFF)));
		default: break;
		}
		throw runtime_error("invalid unsigned math");
	}

	bool Execute(const ResolvedInstruction& inst)
	{
		pc_ = inst.next_addr;
		const vector<ResolvedOperand>& ops = inst.operands;

		switch (inst.exec)
		{
		case EX_MOVE:
			if (inst.width_bits == 80) WriteFloatOperand(ops[0], 80, ReadFloatOperand(ops[1], 80));
			else WriteBitsOperand(ops[0], inst.width_bits, ReadBitsOperand(ops[1], inst.width_bits));
			return false;
		case EX_JUMP:
			pc_ = ReadBitsOperand(ops[0], 64);
			return false;
		case EX_JUMPIF:
			if (ReadBitsOperand(ops[0], 8) != 0)
			{
				pc_ = ReadBitsOperand(ops[1], 64);
			}
			return false;
		case EX_CALL:
		{
			uint64_t sp = regs_[0] - 8;
			regs_[0] = sp;
			memory_.WriteUnsigned(sp, 64, inst.next_addr);
			pc_ = ReadBitsOperand(ops[0], 64);
			return false;
		}
		case EX_RET:
		{
			uint64_t sp = regs_[0];
			pc_ = memory_.ReadUnsigned(sp, 64);
			regs_[0] = sp + 8;
			return false;
		}
		case EX_NOT:
			WriteBitsOperand(ops[0], inst.width_bits, ~ReadBitsOperand(ops[1], inst.width_bits));
			return false;
		case EX_AND:
			WriteBitsOperand(ops[0], inst.width_bits, ReadBitsOperand(ops[1], inst.width_bits) & ReadBitsOperand(ops[2], inst.width_bits));
			return false;
		case EX_OR:
			WriteBitsOperand(ops[0], inst.width_bits, ReadBitsOperand(ops[1], inst.width_bits) | ReadBitsOperand(ops[2], inst.width_bits));
			return false;
		case EX_XOR:
			WriteBitsOperand(ops[0], inst.width_bits, ReadBitsOperand(ops[1], inst.width_bits) ^ ReadBitsOperand(ops[2], inst.width_bits));
			return false;
		case EX_LSHIFT:
			WriteBitsOperand(ops[0], inst.width_bits, ReadBitsOperand(ops[1], inst.width_bits) << (ReadBitsOperand(ops[2], 8) & 0xFF));
			return false;
		case EX_SRSHIFT:
			switch (inst.width_bits)
			{
			case 8: WriteBitsOperand(ops[0], 8, DoSignedMath<int8_t>(inst.exec, ReadBitsOperand(ops[1], 8), ReadBitsOperand(ops[2], 8), 8)); return false;
			case 16: WriteBitsOperand(ops[0], 16, DoSignedMath<int16_t>(inst.exec, ReadBitsOperand(ops[1], 16), ReadBitsOperand(ops[2], 8), 16)); return false;
			case 32: WriteBitsOperand(ops[0], 32, DoSignedMath<int32_t>(inst.exec, ReadBitsOperand(ops[1], 32), ReadBitsOperand(ops[2], 8), 32)); return false;
			case 64: WriteBitsOperand(ops[0], 64, DoSignedMath<int64_t>(inst.exec, ReadBitsOperand(ops[1], 64), ReadBitsOperand(ops[2], 8), 64)); return false;
			}
			break;
		case EX_URSHIFT:
			switch (inst.width_bits)
			{
			case 8: WriteBitsOperand(ops[0], 8, DoUnsignedMath<uint8_t>(inst.exec, ReadBitsOperand(ops[1], 8), ReadBitsOperand(ops[2], 8))); return false;
			case 16: WriteBitsOperand(ops[0], 16, DoUnsignedMath<uint16_t>(inst.exec, ReadBitsOperand(ops[1], 16), ReadBitsOperand(ops[2], 8))); return false;
			case 32: WriteBitsOperand(ops[0], 32, DoUnsignedMath<uint32_t>(inst.exec, ReadBitsOperand(ops[1], 32), ReadBitsOperand(ops[2], 8))); return false;
			case 64: WriteBitsOperand(ops[0], 64, DoUnsignedMath<uint64_t>(inst.exec, ReadBitsOperand(ops[1], 64), ReadBitsOperand(ops[2], 8))); return false;
			}
			break;
		case EX_IADD:
			WriteBitsOperand(ops[0], inst.width_bits, ReadBitsOperand(ops[1], inst.width_bits) + ReadBitsOperand(ops[2], inst.width_bits));
			return false;
		case EX_ISUB:
			WriteBitsOperand(ops[0], inst.width_bits, ReadBitsOperand(ops[1], inst.width_bits) - ReadBitsOperand(ops[2], inst.width_bits));
			return false;
		case EX_SMUL:
		case EX_SDIV:
		case EX_SMOD:
			switch (inst.width_bits)
			{
			case 8: WriteBitsOperand(ops[0], 8, DoSignedMath<int8_t>(inst.exec, ReadBitsOperand(ops[1], 8), ReadBitsOperand(ops[2], 8), 8)); return false;
			case 16: WriteBitsOperand(ops[0], 16, DoSignedMath<int16_t>(inst.exec, ReadBitsOperand(ops[1], 16), ReadBitsOperand(ops[2], 16), 16)); return false;
			case 32: WriteBitsOperand(ops[0], 32, DoSignedMath<int32_t>(inst.exec, ReadBitsOperand(ops[1], 32), ReadBitsOperand(ops[2], 32), 32)); return false;
			case 64: WriteBitsOperand(ops[0], 64, DoSignedMath<int64_t>(inst.exec, ReadBitsOperand(ops[1], 64), ReadBitsOperand(ops[2], 64), 64)); return false;
			}
			break;
		case EX_UMUL:
		case EX_UDIV:
		case EX_UMOD:
			switch (inst.width_bits)
			{
			case 8: WriteBitsOperand(ops[0], 8, DoUnsignedMath<uint8_t>(inst.exec, ReadBitsOperand(ops[1], 8), ReadBitsOperand(ops[2], 8))); return false;
			case 16: WriteBitsOperand(ops[0], 16, DoUnsignedMath<uint16_t>(inst.exec, ReadBitsOperand(ops[1], 16), ReadBitsOperand(ops[2], 16))); return false;
			case 32: WriteBitsOperand(ops[0], 32, DoUnsignedMath<uint32_t>(inst.exec, ReadBitsOperand(ops[1], 32), ReadBitsOperand(ops[2], 32))); return false;
			case 64: WriteBitsOperand(ops[0], 64, DoUnsignedMath<uint64_t>(inst.exec, ReadBitsOperand(ops[1], 64), ReadBitsOperand(ops[2], 64))); return false;
			}
			break;
		case EX_IEQ:
			WriteBitsOperand(ops[0], 8, ReadBitsOperand(ops[1], inst.width_bits) == ReadBitsOperand(ops[2], inst.width_bits));
			return false;
		case EX_INE:
			WriteBitsOperand(ops[0], 8, ReadBitsOperand(ops[1], inst.width_bits) != ReadBitsOperand(ops[2], inst.width_bits));
			return false;
		case EX_SLT:
		case EX_SGT:
		case EX_SLE:
		case EX_SGE:
			switch (inst.width_bits)
			{
			case 8: WriteBitsOperand(ops[0], 8, DoSignedCmp<int8_t>(inst.exec, ReadBitsOperand(ops[1], 8), ReadBitsOperand(ops[2], 8), 8)); return false;
			case 16: WriteBitsOperand(ops[0], 8, DoSignedCmp<int16_t>(inst.exec, ReadBitsOperand(ops[1], 16), ReadBitsOperand(ops[2], 16), 16)); return false;
			case 32: WriteBitsOperand(ops[0], 8, DoSignedCmp<int32_t>(inst.exec, ReadBitsOperand(ops[1], 32), ReadBitsOperand(ops[2], 32), 32)); return false;
			case 64: WriteBitsOperand(ops[0], 8, DoSignedCmp<int64_t>(inst.exec, ReadBitsOperand(ops[1], 64), ReadBitsOperand(ops[2], 64), 64)); return false;
			}
			break;
		case EX_ULT:
		case EX_UGT:
		case EX_ULE:
		case EX_UGE:
			switch (inst.width_bits)
			{
			case 8: WriteBitsOperand(ops[0], 8, DoUnsignedCmp<uint8_t>(inst.exec, ReadBitsOperand(ops[1], 8), ReadBitsOperand(ops[2], 8))); return false;
			case 16: WriteBitsOperand(ops[0], 8, DoUnsignedCmp<uint16_t>(inst.exec, ReadBitsOperand(ops[1], 16), ReadBitsOperand(ops[2], 16))); return false;
			case 32: WriteBitsOperand(ops[0], 8, DoUnsignedCmp<uint32_t>(inst.exec, ReadBitsOperand(ops[1], 32), ReadBitsOperand(ops[2], 32))); return false;
			case 64: WriteBitsOperand(ops[0], 8, DoUnsignedCmp<uint64_t>(inst.exec, ReadBitsOperand(ops[1], 64), ReadBitsOperand(ops[2], 64))); return false;
			}
			break;
		case EX_FADD:
			WriteFloatOperand(ops[0], inst.width_bits, ReadFloatOperand(ops[1], inst.width_bits) + ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FSUB:
			WriteFloatOperand(ops[0], inst.width_bits, ReadFloatOperand(ops[1], inst.width_bits) - ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FMUL:
			WriteFloatOperand(ops[0], inst.width_bits, ReadFloatOperand(ops[1], inst.width_bits) * ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FDIV:
			WriteFloatOperand(ops[0], inst.width_bits, ReadFloatOperand(ops[1], inst.width_bits) / ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FEQ:
			WriteBitsOperand(ops[0], 8, ReadFloatOperand(ops[1], inst.width_bits) == ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FNE:
			WriteBitsOperand(ops[0], 8, ReadFloatOperand(ops[1], inst.width_bits) != ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FLT:
			WriteBitsOperand(ops[0], 8, ReadFloatOperand(ops[1], inst.width_bits) < ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FGT:
			WriteBitsOperand(ops[0], 8, ReadFloatOperand(ops[1], inst.width_bits) > ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FLE:
			WriteBitsOperand(ops[0], 8, ReadFloatOperand(ops[1], inst.width_bits) <= ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_FGE:
			WriteBitsOperand(ops[0], 8, ReadFloatOperand(ops[1], inst.width_bits) >= ReadFloatOperand(ops[2], inst.width_bits));
			return false;
		case EX_SCONVF80:
			WriteFloatOperand(ops[0], 80, static_cast<long double>(static_cast<int64_t>(SignExtendTo64(ReadBitsOperand(ops[1], inst.width_bits), inst.width_bits))));
			return false;
		case EX_UCONVF80:
			WriteFloatOperand(ops[0], 80, static_cast<long double>(ReadBitsOperand(ops[1], inst.width_bits)));
			return false;
		case EX_F32CONVF80:
		case EX_F64CONVF80:
			WriteFloatOperand(ops[0], 80, ReadFloatOperand(ops[1], inst.width_bits));
			return false;
		case EX_F80CONVS:
		{
			long double value = ReadFloatOperand(ops[1], 80);
			switch (inst.width_bits)
			{
			case 8: WriteBitsOperand(ops[0], 8, static_cast<uint8_t>(static_cast<int8_t>(value))); return false;
			case 16: WriteBitsOperand(ops[0], 16, static_cast<uint16_t>(static_cast<int16_t>(value))); return false;
			case 32: WriteBitsOperand(ops[0], 32, static_cast<uint32_t>(static_cast<int32_t>(value))); return false;
			case 64: WriteBitsOperand(ops[0], 64, static_cast<uint64_t>(static_cast<int64_t>(value))); return false;
			}
			break;
		}
		case EX_F80CONVU:
		{
			long double value = ReadFloatOperand(ops[1], 80);
			switch (inst.width_bits)
			{
			case 8: WriteBitsOperand(ops[0], 8, static_cast<uint8_t>(value)); return false;
			case 16: WriteBitsOperand(ops[0], 16, static_cast<uint16_t>(value)); return false;
			case 32: WriteBitsOperand(ops[0], 32, static_cast<uint32_t>(value)); return false;
			case 64: WriteBitsOperand(ops[0], 64, static_cast<uint64_t>(value)); return false;
			}
			break;
		}
		case EX_F80CONVF32:
			WriteFloatOperand(ops[0], 32, ReadFloatOperand(ops[1], 80));
			return false;
		case EX_F80CONVF64:
			WriteFloatOperand(ops[0], 64, ReadFloatOperand(ops[1], 80));
			return false;
		case EX_SYSCALL:
			return ExecuteSyscall(inst);
		}

		throw runtime_error("unsupported instruction class");
	}

	bool ExecuteSyscall(const ResolvedInstruction& inst)
	{
		uint64_t nr = ReadBitsOperand(inst.operands[1], 64);
		vector<uint64_t> args;
		for (size_t i = 2; i < inst.operands.size(); ++i)
		{
			args.push_back(ReadBitsOperand(inst.operands[i], 64));
		}

		long long result = -1;
		if (nr == 0)
		{
			uint64_t fd = args.size() > 0 ? args[0] : 0;
			uint64_t buf = args.size() > 1 ? args[1] : 0;
			uint64_t count = args.size() > 2 ? args[2] : 0;
			if (fd == 0)
			{
				size_t n = min<size_t>(count, input_.size() - min(input_pos_, input_.size()));
				if (n != 0)
				{
					memory_.WriteBuffer(buf, input_.data() + input_pos_, n);
					input_pos_ += n;
				}
				result = static_cast<long long>(n);
			}
		}
		else if (nr == 1)
		{
			uint64_t fd = args.size() > 0 ? args[0] : 0;
			uint64_t buf = args.size() > 1 ? args[1] : 0;
			uint64_t count = args.size() > 2 ? args[2] : 0;
			vector<char> tmp(count, 0);
			if (count != 0)
			{
				memory_.ReadBuffer(buf, tmp.data(), count);
			}
			ostream* stream = fd == 1 ? &cout : (fd == 2 ? &cerr : nullptr);
			if (stream)
			{
				stream->write(tmp.data(), count);
				stream->flush();
				result = static_cast<long long>(count);
			}
		}
		else if (nr == 9)
		{
			uint64_t length = args.size() > 1 ? args[1] : 0;
			uint64_t aligned = (length + 4095ULL) & ~4095ULL;
			static uint64_t mmap_base = 0x100000000ULL;
			uint64_t addr = mmap_base;
			mmap_base += aligned;
			memory_.AllocateSegment(addr, aligned);
			result = static_cast<long long>(addr);
		}
		else if (nr == 60)
		{
			exit_status_ = static_cast<int>(args.empty() ? 0 : args[0]);
			WriteBitsOperand(inst.operands[0], 64, static_cast<uint64_t>(exit_status_));
			return true;
		}

		WriteBitsOperand(inst.operands[0], 64, static_cast<uint64_t>(result));
		return false;
	}
};

vector<CYToken> PreprocessSources(const vector<string>& srcfiles)
{
	time_t now = time(nullptr);
	tm* build_tm = localtime(&now);
	string build_date = MakeAsctimeLiteralSlice(build_tm, 4, 11);
	string build_time = "\"" + string(asctime(build_tm)).substr(11, 8) + "\"";

	vector<CYToken> out;
	for (const string& srcfile : srcfiles)
	{
		PreprocContext ctx;
		ctx.build_date_literal = build_date;
		ctx.build_time_literal = build_time;
		ProcessFile(ctx, srcfile, srcfile);
		for (const MacroToken& token : ctx.output_tokens)
		{
			if (token.kind == PPT_WHITESPACE || token.kind == PPT_NEWLINE)
			{
				continue;
			}
			if (token.kind == PPT_USER_DEFINED_STRING_LITERAL || token.kind == PPT_USER_DEFINED_CHARACTER_LITERAL)
			{
				throw runtime_error("user-defined literals not supported");
			}
			out.push_back(CYToken{token.kind, token.source, token.file, token.line});
		}
	}
	return out;
}

extern "C" long int syscall(long int n, ...) throw ();

bool PA9SetFileExecutable(const string& path)
{
	int res = syscall(90, path.c_str(), 0755);
	return res == 0;
}

string GetSelfPath()
{
	char buffer[4096];
	ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
	if (n < 0)
	{
		throw runtime_error("unable to resolve self path");
	}
	buffer[n] = '\0';
	return string(buffer);
}

void WriteWrapperScript(const string& outfile, const string& self_path, const string& blob)
{
	ofstream out(outfile.c_str());
	if (!out)
	{
		throw runtime_error("unable to open output file");
	}
	out << "#!/bin/bash\n";
	out << "exec '" << self_path << "' --run-base64 '" << blob << "'\n";
	out.close();
	if (!PA9SetFileExecutable(outfile))
	{
		throw runtime_error("unable to mark output executable");
	}
}

int RunSerializedProgram(const string& blob)
{
	Program program = DeserializeProgram(Base64Decode(blob));
	Interpreter interpreter(program);
	return interpreter.Run();
}

int main(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int i = 1; i < argc; ++i)
		{
			args.emplace_back(argv[i]);
		}

		if (args.size() == 2 && args[0] == "--run-base64")
		{
			return RunSerializedProgram(args[1]);
		}

		if (args.size() < 3 || args[0] != "-o")
		{
			throw logic_error("invalid usage");
		}

		string outfile = args[1];
		vector<string> srcfiles(args.begin() + 2, args.end());
		vector<CYToken> tokens = PreprocessSources(srcfiles);
		CYParser parser(tokens);
		Program program = BuildProgram(parser.ParseProgram());
		string blob = Base64Encode(SerializeProgram(program));
		WriteWrapperScript(outfile, GetSelfPath(), blob);
		return 0;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
