// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

#define CPPGM_EMBED_POSTTOKEN 1
#include "posttoken.cpp"

// mock implementation of IsDefinedIdentifier for PA3
// return true iff first code point is odd
bool (*CPPGM_IsDefinedIdentifierHook)(const string&) = nullptr;

bool PA3Mock_IsDefinedIdentifier(const string& identifier)
{
	if (CPPGM_IsDefinedIdentifierHook)
	{
		return CPPGM_IsDefinedIdentifierHook(identifier);
	}
	if (identifier.empty())
		return false;
	else
		return identifier[0] % 2;
}

struct CtrlValue
{
	bool is_unsigned;
	uint64_t bits;

	CtrlValue(bool is_unsigned = false, uint64_t bits = 0)
		: is_unsigned(is_unsigned), bits(bits)
	{}
};

CtrlValue MakeSigned(int64_t value)
{
	return CtrlValue{false, static_cast<uint64_t>(value)};
}

CtrlValue MakeUnsigned(uint64_t value)
{
	return CtrlValue{true, value};
}

int64_t AsSigned(CtrlValue value)
{
	return static_cast<int64_t>(value.bits);
}

uint64_t AsUnsigned(CtrlValue value)
{
	return value.bits;
}

bool IsZero(CtrlValue value)
{
	return value.bits == 0;
}

bool IsTrue(CtrlValue value)
{
	return !IsZero(value);
}

CtrlValue ConvertToCommonType(CtrlValue value, bool want_unsigned)
{
	if (want_unsigned)
	{
		return MakeUnsigned(value.bits);
	}

	return MakeSigned(static_cast<int64_t>(value.bits));
}

bool UseUnsignedCommonType(CtrlValue lhs, CtrlValue rhs)
{
	return lhs.is_unsigned || rhs.is_unsigned;
}

enum ECtrlTokenKind
{
	CTK_LITERAL,
	CTK_IDENTIFIER,
	CTK_SIMPLE
};

struct CtrlToken
{
	ECtrlTokenKind kind;
	string source;
	CtrlValue value;
	ETokenType simple;

	CtrlToken(ECtrlTokenKind kind = CTK_LITERAL, const string& source = string(), CtrlValue value = CtrlValue(), ETokenType simple = OP_PLUS)
		: kind(kind), source(source), value(value), simple(simple)
	{}
};

bool FundamentalTypeIsUnsigned(EFundamentalType type)
{
	switch (type)
	{
	case FT_UNSIGNED_CHAR:
	case FT_UNSIGNED_SHORT_INT:
	case FT_UNSIGNED_INT:
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT:
	case FT_CHAR16_T:
	case FT_CHAR32_T:
		return true;
	default:
		return false;
	}
}

bool IsControllingExprOperator(ETokenType type)
{
	switch (type)
	{
	case OP_LPAREN:
	case OP_RPAREN:
	case OP_PLUS:
	case OP_MINUS:
	case OP_LNOT:
	case OP_COMPL:
	case OP_STAR:
	case OP_DIV:
	case OP_MOD:
	case OP_LSHIFT:
	case OP_RSHIFT:
	case OP_LT:
	case OP_GT:
	case OP_LE:
	case OP_GE:
	case OP_EQ:
	case OP_NE:
	case OP_AMP:
	case OP_XOR:
	case OP_BOR:
	case OP_LAND:
	case OP_LOR:
	case OP_QMARK:
	case OP_COLON:
		return true;
	default:
		return false;
	}
}

bool ParseIntegralPPNumber(const string& source, CtrlValue& value)
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

	unsigned __int128 parsed = 0;
	if (!ParseUnsignedIntegerValue(digits, base, parsed))
	{
		return false;
	}

	vector<EFundamentalType> candidates;
	const bool decimal = !is_hex && !is_octal;
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
			if (FitsSigned(parsed, candidate)) { value = MakeSigned(static_cast<int>(parsed)); return true; }
			break;
		case FT_LONG_INT:
			if (FitsSigned(parsed, candidate)) { value = MakeSigned(static_cast<long>(parsed)); return true; }
			break;
		case FT_LONG_LONG_INT:
			if (FitsSigned(parsed, candidate)) { value = MakeSigned(static_cast<long long>(parsed)); return true; }
			break;
		case FT_UNSIGNED_INT:
			if (FitsUnsigned(parsed, candidate)) { value = MakeUnsigned(static_cast<unsigned int>(parsed)); return true; }
			break;
		case FT_UNSIGNED_LONG_INT:
			if (FitsUnsigned(parsed, candidate)) { value = MakeUnsigned(static_cast<unsigned long>(parsed)); return true; }
			break;
		case FT_UNSIGNED_LONG_LONG_INT:
			if (FitsUnsigned(parsed, candidate)) { value = MakeUnsigned(static_cast<unsigned long long>(parsed)); return true; }
			break;
		default:
			break;
		}
	}

	return false;
}

bool ParseIntegralCharacterLiteral(const string& source, CtrlValue& value)
{
	CharacterLiteralInfo info;
	if (!ParseCharacterLiteralSource(source, info) || !info.valid || info.user_defined)
	{
		return false;
	}

	value = FundamentalTypeIsUnsigned(info.type)
		? MakeUnsigned(info.value)
		: MakeSigned(static_cast<int64_t>(static_cast<int32_t>(info.value)));
	return true;
}

bool ConvertLineTokens(const vector<PPToken>& line, vector<CtrlToken>& out)
{
	out.clear();

	for (const PPToken& token : line)
	{
		switch (token.kind)
		{
		case PPT_IDENTIFIER:
			out.push_back(CtrlToken{CTK_IDENTIFIER, token.source, {}});
			break;
		case PPT_PREPROCESSING_OP_OR_PUNC:
		{
			auto it = StringToTokenTypeMap.find(token.source);
			if (it == StringToTokenTypeMap.end() || !IsControllingExprOperator(it->second))
			{
				return false;
			}
			out.push_back(CtrlToken{CTK_SIMPLE, token.source, {}, it->second});
			break;
		}
		case PPT_PP_NUMBER:
		{
			CtrlValue value;
			if (!ParseIntegralPPNumber(token.source, value))
			{
				return false;
			}
			out.push_back(CtrlToken{CTK_LITERAL, token.source, value});
			break;
		}
		case PPT_CHARACTER_LITERAL:
		{
			CtrlValue value;
			if (!ParseIntegralCharacterLiteral(token.source, value))
			{
				return false;
			}
			out.push_back(CtrlToken{CTK_LITERAL, token.source, value});
			break;
		}
		case PPT_WHITESPACE:
		case PPT_NEWLINE:
			break;
		default:
			return false;
		}
	}

	return true;
}

struct EvalResult
{
	bool ok;
	CtrlValue value;

	EvalResult(bool ok = false, CtrlValue value = CtrlValue())
		: ok(ok), value(value)
	{}
};

struct CtrlExprParser
{
	const vector<CtrlToken>& tokens;
	size_t pos = 0;

	CtrlExprParser(const vector<CtrlToken>& tokens)
		: tokens(tokens)
	{}

	bool AtEnd() const
	{
		return pos >= tokens.size();
	}

	bool MatchSimple(ETokenType type)
	{
		if (pos < tokens.size() &&
			tokens[pos].kind == CTK_SIMPLE &&
			tokens[pos].simple == type)
		{
			++pos;
			return true;
		}
		return false;
	}

	bool PeekSimple(ETokenType type) const
	{
		return pos < tokens.size() &&
			tokens[pos].kind == CTK_SIMPLE &&
			tokens[pos].simple == type;
	}

	bool PeekIdentifier(const string& text) const
	{
		return pos < tokens.size() &&
			tokens[pos].kind == CTK_IDENTIFIER &&
			tokens[pos].source == text;
	}

	EvalResult ParseControllingExpression(bool evaluate)
	{
		EvalResult lhs = ParseLogicalOrExpression(evaluate);
		if (!lhs.ok)
		{
			return lhs;
		}

		if (!MatchSimple(OP_QMARK))
		{
			return lhs;
		}

		const bool choose_true = evaluate && IsTrue(lhs.value);
		EvalResult if_true = ParseControllingExpression(choose_true);
		if (!if_true.ok || !MatchSimple(OP_COLON))
		{
			return EvalResult{};
		}
		EvalResult if_false = ParseControllingExpression(evaluate && !choose_true);
		if (!if_false.ok)
		{
			return EvalResult{};
		}

		const bool result_unsigned = UseUnsignedCommonType(if_true.value, if_false.value);
		CtrlValue chosen = choose_true ? if_true.value : if_false.value;
		if (!evaluate)
		{
			chosen = result_unsigned ? MakeUnsigned(0) : MakeSigned(0);
		}
		return EvalResult{true, ConvertToCommonType(chosen, result_unsigned)};
	}

	EvalResult ParseLogicalOrExpression(bool evaluate)
	{
		EvalResult lhs = ParseLogicalAndExpression(evaluate);
		if (!lhs.ok)
		{
			return lhs;
		}

		while (MatchSimple(OP_LOR))
		{
			const bool rhs_eval = evaluate && !IsTrue(lhs.value);
			EvalResult rhs = ParseLogicalAndExpression(rhs_eval);
			if (!rhs.ok)
			{
				return EvalResult{};
			}
			lhs = EvalResult{true, MakeSigned(evaluate ? (IsTrue(lhs.value) || IsTrue(rhs.value)) : 0)};
		}

		return lhs;
	}

	EvalResult ParseLogicalAndExpression(bool evaluate)
	{
		EvalResult lhs = ParseInclusiveOrExpression(evaluate);
		if (!lhs.ok)
		{
			return lhs;
		}

		while (MatchSimple(OP_LAND))
		{
			const bool rhs_eval = evaluate && IsTrue(lhs.value);
			EvalResult rhs = ParseInclusiveOrExpression(rhs_eval);
			if (!rhs.ok)
			{
				return EvalResult{};
			}
			lhs = EvalResult{true, MakeSigned(evaluate ? (IsTrue(lhs.value) && IsTrue(rhs.value)) : 0)};
		}

		return lhs;
	}

	EvalResult ParseInclusiveOrExpression(bool evaluate)
	{
		return ParseLeftAssociativeBinary(
			evaluate,
			[this](bool eval) { return ParseExclusiveOrExpression(eval); },
			{OP_BOR},
			[](CtrlValue lhs, CtrlValue rhs, ETokenType op, bool eval) { return ApplyBitwise(lhs, rhs, op, eval); }
		);
	}

	EvalResult ParseExclusiveOrExpression(bool evaluate)
	{
		return ParseLeftAssociativeBinary(
			evaluate,
			[this](bool eval) { return ParseAndExpression(eval); },
			{OP_XOR},
			[](CtrlValue lhs, CtrlValue rhs, ETokenType op, bool eval) { return ApplyBitwise(lhs, rhs, op, eval); }
		);
	}

	EvalResult ParseAndExpression(bool evaluate)
	{
		return ParseLeftAssociativeBinary(
			evaluate,
			[this](bool eval) { return ParseEqualityExpression(eval); },
			{OP_AMP},
			[](CtrlValue lhs, CtrlValue rhs, ETokenType op, bool eval) { return ApplyBitwise(lhs, rhs, op, eval); }
		);
	}

	EvalResult ParseEqualityExpression(bool evaluate)
	{
		return ParseLeftAssociativeBinary(
			evaluate,
			[this](bool eval) { return ParseRelationalExpression(eval); },
			{OP_EQ, OP_NE},
			[](CtrlValue lhs, CtrlValue rhs, ETokenType op, bool eval) { return ApplyCompare(lhs, rhs, op, eval); }
		);
	}

	EvalResult ParseRelationalExpression(bool evaluate)
	{
		return ParseLeftAssociativeBinary(
			evaluate,
			[this](bool eval) { return ParseShiftExpression(eval); },
			{OP_LT, OP_GT, OP_LE, OP_GE},
			[](CtrlValue lhs, CtrlValue rhs, ETokenType op, bool eval) { return ApplyCompare(lhs, rhs, op, eval); }
		);
	}

	EvalResult ParseShiftExpression(bool evaluate)
	{
		return ParseLeftAssociativeBinary(
			evaluate,
			[this](bool eval) { return ParseAdditiveExpression(eval); },
			{OP_LSHIFT, OP_RSHIFT},
			[](CtrlValue lhs, CtrlValue rhs, ETokenType op, bool eval) { return ApplyShift(lhs, rhs, op, eval); }
		);
	}

	EvalResult ParseAdditiveExpression(bool evaluate)
	{
		return ParseLeftAssociativeBinary(
			evaluate,
			[this](bool eval) { return ParseMultiplicativeExpression(eval); },
			{OP_PLUS, OP_MINUS},
			[](CtrlValue lhs, CtrlValue rhs, ETokenType op, bool eval) { return ApplyAdditive(lhs, rhs, op, eval); }
		);
	}

	EvalResult ParseMultiplicativeExpression(bool evaluate)
	{
		return ParseLeftAssociativeBinary(
			evaluate,
			[this](bool eval) { return ParseUnaryExpression(eval); },
			{OP_STAR, OP_DIV, OP_MOD},
			[](CtrlValue lhs, CtrlValue rhs, ETokenType op, bool eval) { return ApplyMultiplicative(lhs, rhs, op, eval); }
		);
	}

	EvalResult ParseUnaryExpression(bool evaluate)
	{
		if (MatchSimple(OP_PLUS))
		{
			EvalResult operand = ParseUnaryExpression(evaluate);
			return operand.ok ? EvalResult{true, evaluate ? operand.value : MakeSigned(0)} : EvalResult{};
		}
		if (MatchSimple(OP_MINUS))
		{
			EvalResult operand = ParseUnaryExpression(evaluate);
			if (!operand.ok)
			{
				return EvalResult{};
			}
			if (!evaluate)
			{
				return EvalResult{true, operand.value.is_unsigned ? MakeUnsigned(0) : MakeSigned(0)};
			}
			if (operand.value.is_unsigned)
			{
				return EvalResult{true, MakeUnsigned(0 - operand.value.bits)};
			}
			return EvalResult{true, MakeSigned(static_cast<int64_t>(0 - operand.value.bits))};
		}
		if (MatchSimple(OP_LNOT))
		{
			EvalResult operand = ParseUnaryExpression(evaluate);
			return operand.ok ? EvalResult{true, MakeSigned(evaluate ? !IsTrue(operand.value) : 0)} : EvalResult{};
		}
		if (MatchSimple(OP_COMPL))
		{
			EvalResult operand = ParseUnaryExpression(evaluate);
			if (!operand.ok)
			{
				return EvalResult{};
			}
			if (!evaluate)
			{
				return EvalResult{true, operand.value.is_unsigned ? MakeUnsigned(0) : MakeSigned(0)};
			}
			if (operand.value.is_unsigned)
			{
				return EvalResult{true, MakeUnsigned(~operand.value.bits)};
			}
			return EvalResult{true, MakeSigned(static_cast<int64_t>(~operand.value.bits))};
		}

		return ParsePrimaryExpression(evaluate);
	}

	EvalResult ParsePrimaryExpression(bool evaluate)
	{
		if (pos >= tokens.size())
		{
			return EvalResult{};
		}

		if (MatchSimple(OP_LPAREN))
		{
			EvalResult value = ParseControllingExpression(evaluate);
			if (!value.ok || !MatchSimple(OP_RPAREN))
			{
				return EvalResult{};
			}
			return value;
		}

		if (PeekIdentifier("defined"))
		{
			++pos;
			string identifier;
			if (MatchSimple(OP_LPAREN))
			{
				if (pos >= tokens.size() || tokens[pos].kind != CTK_IDENTIFIER)
				{
					return EvalResult{};
				}
				identifier = tokens[pos++].source;
				if (!MatchSimple(OP_RPAREN))
				{
					return EvalResult{};
				}
			}
			else
			{
				if (pos >= tokens.size() || tokens[pos].kind != CTK_IDENTIFIER)
				{
					return EvalResult{};
				}
				identifier = tokens[pos++].source;
			}

			return EvalResult{true, MakeSigned(evaluate ? PA3Mock_IsDefinedIdentifier(identifier) : 0)};
		}

		if (tokens[pos].kind == CTK_LITERAL)
		{
			return EvalResult{true, evaluate ? tokens[pos++].value : (tokens[pos++].value.is_unsigned ? MakeUnsigned(0) : MakeSigned(0))};
		}

		if (tokens[pos].kind == CTK_IDENTIFIER)
		{
			string name = tokens[pos++].source;
			if (name == "true")
			{
				return EvalResult{true, MakeSigned(evaluate ? 1 : 0)};
			}
			if (name == "false")
			{
				return EvalResult{true, MakeSigned(0)};
			}
			return EvalResult{true, MakeSigned(0)};
		}

		return EvalResult{};
	}

	template<typename ParseFn, typename ApplyFn>
	EvalResult ParseLeftAssociativeBinary(bool evaluate, ParseFn parse_operand, initializer_list<ETokenType> operators, ApplyFn apply)
	{
		EvalResult lhs = parse_operand(evaluate);
		if (!lhs.ok)
		{
			return lhs;
		}

		while (true)
		{
			ETokenType op;
			bool matched = false;
			for (ETokenType candidate : operators)
			{
				if (MatchSimple(candidate))
				{
					op = candidate;
					matched = true;
					break;
				}
			}
			if (!matched)
			{
				break;
			}

			EvalResult rhs = parse_operand(evaluate);
			if (!rhs.ok)
			{
				return EvalResult{};
			}
			lhs = apply(lhs.value, rhs.value, op, evaluate);
			if (!lhs.ok)
			{
				return lhs;
			}
		}

		return lhs;
	}

	static EvalResult ApplyBitwise(CtrlValue lhs, CtrlValue rhs, ETokenType op, bool evaluate)
	{
		const bool use_unsigned = UseUnsignedCommonType(lhs, rhs);
		if (!evaluate)
		{
			return EvalResult{true, use_unsigned ? MakeUnsigned(0) : MakeSigned(0)};
		}

		uint64_t lv = ConvertToCommonType(lhs, use_unsigned).bits;
		uint64_t rv = ConvertToCommonType(rhs, use_unsigned).bits;
		uint64_t result = 0;
		if (op == OP_AMP) result = lv & rv;
		else if (op == OP_XOR) result = lv ^ rv;
		else result = lv | rv;
		return EvalResult{true, use_unsigned ? MakeUnsigned(result) : MakeSigned(static_cast<int64_t>(result))};
	}

	static EvalResult ApplyCompare(CtrlValue lhs, CtrlValue rhs, ETokenType op, bool evaluate)
	{
		if (!evaluate)
		{
			return EvalResult{true, MakeSigned(0)};
		}

		const bool use_unsigned = UseUnsignedCommonType(lhs, rhs);
		bool result = false;
		if (use_unsigned)
		{
			uint64_t lv = lhs.bits;
			uint64_t rv = rhs.bits;
			switch (op)
			{
			case OP_EQ: result = lv == rv; break;
			case OP_NE: result = lv != rv; break;
			case OP_LT: result = lv < rv; break;
			case OP_GT: result = lv > rv; break;
			case OP_LE: result = lv <= rv; break;
			case OP_GE: result = lv >= rv; break;
			default: break;
			}
		}
		else
		{
			int64_t lv = AsSigned(lhs);
			int64_t rv = AsSigned(rhs);
			switch (op)
			{
			case OP_EQ: result = lv == rv; break;
			case OP_NE: result = lv != rv; break;
			case OP_LT: result = lv < rv; break;
			case OP_GT: result = lv > rv; break;
			case OP_LE: result = lv <= rv; break;
			case OP_GE: result = lv >= rv; break;
			default: break;
			}
		}
		return EvalResult{true, MakeSigned(result)};
	}

	static EvalResult ApplyShift(CtrlValue lhs, CtrlValue rhs, ETokenType op, bool evaluate)
	{
		if (!evaluate)
		{
			return EvalResult{true, lhs.is_unsigned ? MakeUnsigned(0) : MakeSigned(0)};
		}

		if ((!rhs.is_unsigned && AsSigned(rhs) < 0) || rhs.bits >= 64)
		{
			return EvalResult{};
		}
		unsigned shift = static_cast<unsigned>(rhs.bits);

		if (op == OP_LSHIFT)
		{
			uint64_t result = lhs.bits << shift;
			return EvalResult{true, lhs.is_unsigned ? MakeUnsigned(result) : MakeSigned(static_cast<int64_t>(result))};
		}

		if (lhs.is_unsigned)
		{
			return EvalResult{true, MakeUnsigned(lhs.bits >> shift)};
		}

		return EvalResult{true, MakeSigned(AsSigned(lhs) >> shift)};
	}

	static EvalResult ApplyAdditive(CtrlValue lhs, CtrlValue rhs, ETokenType op, bool evaluate)
	{
		const bool use_unsigned = UseUnsignedCommonType(lhs, rhs);
		if (!evaluate)
		{
			return EvalResult{true, use_unsigned ? MakeUnsigned(0) : MakeSigned(0)};
		}
		uint64_t lv = ConvertToCommonType(lhs, use_unsigned).bits;
		uint64_t rv = ConvertToCommonType(rhs, use_unsigned).bits;
		uint64_t result = op == OP_PLUS ? lv + rv : lv - rv;
		return EvalResult{true, use_unsigned ? MakeUnsigned(result) : MakeSigned(static_cast<int64_t>(result))};
	}

	static EvalResult ApplyMultiplicative(CtrlValue lhs, CtrlValue rhs, ETokenType op, bool evaluate)
	{
		const bool use_unsigned = UseUnsignedCommonType(lhs, rhs);
		if (!evaluate)
		{
			return EvalResult{true, use_unsigned ? MakeUnsigned(0) : MakeSigned(0)};
		}

		if (op == OP_STAR)
		{
			uint64_t lv = ConvertToCommonType(lhs, use_unsigned).bits;
			uint64_t rv = ConvertToCommonType(rhs, use_unsigned).bits;
			uint64_t result = lv * rv;
			return EvalResult{true, use_unsigned ? MakeUnsigned(result) : MakeSigned(static_cast<int64_t>(result))};
		}

		if (rhs.bits == 0)
		{
			return EvalResult{};
		}

		if (use_unsigned)
		{
			uint64_t lv = lhs.bits;
			uint64_t rv = rhs.bits;
			uint64_t result = op == OP_DIV ? lv / rv : lv % rv;
			return EvalResult{true, MakeUnsigned(result)};
		}

		int64_t lv = AsSigned(lhs);
		int64_t rv = AsSigned(rhs);
		if (lv == numeric_limits<int64_t>::min() && rv == -1)
		{
			return EvalResult{};
		}
		int64_t result = op == OP_DIV ? lv / rv : lv % rv;
		return EvalResult{true, MakeSigned(result)};
	}
};

bool EvaluateLine(const vector<PPToken>& line, string& output_line)
{
	vector<CtrlToken> tokens;
	if (!ConvertLineTokens(line, tokens))
	{
		output_line = "error";
		return true;
	}

	if (tokens.empty())
	{
		output_line.clear();
		return true;
	}

	CtrlExprParser parser(tokens);
	EvalResult result = parser.ParseControllingExpression(true);
	if (!result.ok || !parser.AtEnd())
	{
		output_line = "error";
		return true;
	}

	ostringstream oss;
	if (result.value.is_unsigned)
	{
		oss << result.value.bits << 'u';
	}
	else
	{
		oss << AsSigned(result.value);
	}
	output_line = oss.str();
	return true;
}

#ifndef CPPGM_EMBED_CTRLEXPR
int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		string input = oss.str();

		CollectingPPTokenStream pp_output;
		PPTokenizer tokenizer(pp_output);
		for (char c : input)
		{
			unsigned char unit = c;
			tokenizer.process(unit);
		}
		tokenizer.process(EndOfFile);

		vector<PPToken> line;
		for (const PPToken& token : pp_output.tokens)
		{
			if (token.kind == PPT_EOF)
			{
				continue;
			}
			if (token.kind == PPT_NEWLINE)
			{
				string output_line;
				EvaluateLine(line, output_line);
				if (!output_line.empty())
				{
					cout << output_line << endl;
				}
				line.clear();
				continue;
			}
			line.push_back(token);
		}

		cout << "eof" << endl;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
