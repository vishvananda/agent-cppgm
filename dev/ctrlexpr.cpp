// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#define main posttoken_internal_main
#include "posttoken.cpp"
#undef main

#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace std;

// mock implementation of IsDefinedIdentifier for PA3
// return true iff first code point is odd
bool PA3Mock_IsDefinedIdentifier(const string& identifier)
{
	if (identifier.empty())
		return false;
	else
		return identifier[0] % 2;
}

struct CEValue
{
	uint64_t bits;
	bool is_unsigned;

	CEValue(uint64_t bits = 0, bool is_unsigned = false)
		: bits(bits), is_unsigned(is_unsigned)
	{}

	int64_t as_signed() const
	{
		return static_cast<int64_t>(bits);
	}

	uint64_t as_unsigned() const
	{
		return bits;
	}
};

CEValue MakeSigned(int64_t v)
{
	return CEValue(static_cast<uint64_t>(v), false);
}

CEValue MakeUnsigned(uint64_t v)
{
	return CEValue(v, true);
}

CEValue ConvertToType(const CEValue& v, bool want_unsigned)
{
	return want_unsigned ? MakeUnsigned(v.as_unsigned()) : MakeSigned(v.as_signed());
}

bool CommonUnsigned(const CEValue& a, const CEValue& b)
{
	return a.is_unsigned || b.is_unsigned;
}

struct ParseError : runtime_error
{
	explicit ParseError(const string& what)
		: runtime_error(what)
	{}
};

struct ExpressionParser
{
	const vector<PPToken>& tokens;
	size_t pos;

	explicit ExpressionParser(const vector<PPToken>& tokens)
		: tokens(tokens), pos(0)
	{}

	bool at_end() const
	{
		return pos >= tokens.size();
	}

	const PPToken* peek(size_t off = 0) const
	{
		size_t idx = pos + off;
		return idx < tokens.size() ? &tokens[idx] : nullptr;
	}

	bool peek_op(const string& op) const
	{
		const PPToken* t = peek();
		if (t == nullptr)
		{
			return false;
		}
		if (t->kind == PP_OP && t->data == op)
		{
			return true;
		}
		if (t->kind == PP_IDENTIFIER)
		{
			return (op == "!" && t->data == "not") ||
				(op == "~" && t->data == "compl") ||
				(op == "&" && t->data == "bitand") ||
				(op == "^" && t->data == "xor") ||
				(op == "|" && t->data == "bitor") ||
				(op == "&&" && t->data == "and") ||
				(op == "||" && t->data == "or");
		}
		return false;
	}

	bool peek_identifier_like() const
	{
		const PPToken* t = peek();
		return t != nullptr && t->kind == PP_IDENTIFIER;
	}

	const PPToken& get()
	{
		if (at_end())
		{
			throw ParseError("unexpected eof");
		}
		return tokens[pos++];
	}

	string get_op()
	{
		const PPToken& t = get();
		if (t.kind == PP_OP)
		{
			return t.data;
		}
		if (t.kind == PP_IDENTIFIER)
		{
			if (t.data == "not") return "!";
			if (t.data == "compl") return "~";
			if (t.data == "bitand") return "&";
			if (t.data == "xor") return "^";
			if (t.data == "bitor") return "|";
			if (t.data == "and") return "&&";
			if (t.data == "or") return "||";
		}
		throw ParseError("expected operator");
	}

	CEValue parse(bool eval = true)
	{
		CEValue v = parse_conditional(eval);
		if (!at_end())
		{
			throw ParseError("trailing tokens");
		}
		return v;
	}

	CEValue parse_conditional(bool eval)
	{
		CEValue cond = parse_logical_or(eval);
		if (!peek_op("?"))
		{
			return cond;
		}

		get();
		bool common_eval = eval && ToBool(cond);
		CEValue left = parse_conditional(common_eval);
		if (!peek_op(":"))
		{
			throw ParseError("expected colon");
		}
		get();
		CEValue right = parse_conditional(eval && !ToBool(cond));
		bool result_unsigned = CommonUnsigned(left, right);
		if (!eval)
		{
			return CEValue(0, result_unsigned);
		}
		return ConvertToType(ToBool(cond) ? left : right, result_unsigned);
	}

	CEValue parse_logical_or(bool eval)
	{
		CEValue lhs = parse_logical_and(eval);
		while (peek_op("||"))
		{
			get();
			bool lhs_true = eval && ToBool(lhs);
			CEValue rhs = parse_logical_and(eval && !lhs_true);
			lhs = MakeSigned(lhs_true || (eval && ToBool(rhs)));
			if (!eval)
			{
				lhs = MakeSigned(0);
			}
		}
		return lhs;
	}

	CEValue parse_logical_and(bool eval)
	{
		CEValue lhs = parse_inclusive_or(eval);
		while (peek_op("&&"))
		{
			get();
			bool lhs_true = eval && ToBool(lhs);
			CEValue rhs = parse_inclusive_or(eval && lhs_true);
			lhs = MakeSigned(lhs_true && (eval && ToBool(rhs)));
			if (!eval)
			{
				lhs = MakeSigned(0);
			}
		}
		return lhs;
	}

	CEValue parse_inclusive_or(bool eval)
	{
		CEValue lhs = parse_exclusive_or(eval);
		while (peek_op("|"))
		{
			get();
			CEValue rhs = parse_exclusive_or(eval);
			bool is_unsigned = CommonUnsigned(lhs, rhs);
			if (!eval)
			{
				lhs = CEValue(0, is_unsigned);
			}
			else if (is_unsigned)
			{
				lhs = MakeUnsigned(lhs.as_unsigned() | rhs.as_unsigned());
			}
			else
			{
				lhs = MakeSigned(lhs.as_signed() | rhs.as_signed());
			}
		}
		return lhs;
	}

	CEValue parse_exclusive_or(bool eval)
	{
		CEValue lhs = parse_and(eval);
		while (peek_op("^"))
		{
			get();
			CEValue rhs = parse_and(eval);
			bool is_unsigned = CommonUnsigned(lhs, rhs);
			if (!eval)
			{
				lhs = CEValue(0, is_unsigned);
			}
			else if (is_unsigned)
			{
				lhs = MakeUnsigned(lhs.as_unsigned() ^ rhs.as_unsigned());
			}
			else
			{
				lhs = MakeSigned(lhs.as_signed() ^ rhs.as_signed());
			}
		}
		return lhs;
	}

	CEValue parse_and(bool eval)
	{
		CEValue lhs = parse_equality(eval);
		while (peek_op("&"))
		{
			get();
			CEValue rhs = parse_equality(eval);
			bool is_unsigned = CommonUnsigned(lhs, rhs);
			if (!eval)
			{
				lhs = CEValue(0, is_unsigned);
			}
			else if (is_unsigned)
			{
				lhs = MakeUnsigned(lhs.as_unsigned() & rhs.as_unsigned());
			}
			else
			{
				lhs = MakeSigned(lhs.as_signed() & rhs.as_signed());
			}
		}
		return lhs;
	}

	CEValue parse_equality(bool eval)
	{
		CEValue lhs = parse_relational(eval);
		while (peek_op("==") || peek_op("!="))
		{
			string op = get_op();
			CEValue rhs = parse_relational(eval);
			bool result = false;
			if (eval)
			{
				if (CommonUnsigned(lhs, rhs))
				{
					result = lhs.as_unsigned() == rhs.as_unsigned();
				}
				else
				{
					result = lhs.as_signed() == rhs.as_signed();
				}
				if (op == "!=")
				{
					result = !result;
				}
			}
			lhs = MakeSigned(result);
		}
		return lhs;
	}

	CEValue parse_relational(bool eval)
	{
		CEValue lhs = parse_shift(eval);
		while (peek_op("<") || peek_op(">") || peek_op("<=") || peek_op(">="))
		{
			string op = get_op();
			CEValue rhs = parse_shift(eval);
			bool result = false;
			if (eval)
			{
				if (CommonUnsigned(lhs, rhs))
				{
					if (op == "<") result = lhs.as_unsigned() < rhs.as_unsigned();
					else if (op == ">") result = lhs.as_unsigned() > rhs.as_unsigned();
					else if (op == "<=") result = lhs.as_unsigned() <= rhs.as_unsigned();
					else result = lhs.as_unsigned() >= rhs.as_unsigned();
				}
				else
				{
					if (op == "<") result = lhs.as_signed() < rhs.as_signed();
					else if (op == ">") result = lhs.as_signed() > rhs.as_signed();
					else if (op == "<=") result = lhs.as_signed() <= rhs.as_signed();
					else result = lhs.as_signed() >= rhs.as_signed();
				}
			}
			lhs = MakeSigned(result);
		}
		return lhs;
	}

	CEValue parse_shift(bool eval)
	{
		CEValue lhs = parse_additive(eval);
		while (peek_op("<<") || peek_op(">>"))
		{
			string op = get_op();
			CEValue rhs = parse_additive(eval);
			if (!eval)
			{
				continue;
			}
			int64_t shift_signed = rhs.is_unsigned ? static_cast<int64_t>(rhs.as_unsigned()) : rhs.as_signed();
			if (shift_signed < 0 || shift_signed >= 64)
			{
				throw ParseError("bad shift");
			}
			uint64_t shift = static_cast<uint64_t>(shift_signed);
			if (lhs.is_unsigned)
			{
				lhs = op == "<<" ? MakeUnsigned(lhs.as_unsigned() << shift) : MakeUnsigned(lhs.as_unsigned() >> shift);
			}
			else
			{
				lhs = op == "<<" ? MakeSigned(lhs.as_signed() << shift) : MakeSigned(lhs.as_signed() >> shift);
			}
		}
		return lhs;
	}

	CEValue parse_additive(bool eval)
	{
		CEValue lhs = parse_multiplicative(eval);
		while (peek_op("+") || peek_op("-"))
		{
			string op = get_op();
			CEValue rhs = parse_multiplicative(eval);
			bool is_unsigned = CommonUnsigned(lhs, rhs);
			if (!eval)
			{
				lhs = CEValue(0, is_unsigned);
			}
			else if (is_unsigned)
			{
				lhs = op == "+" ? MakeUnsigned(lhs.as_unsigned() + rhs.as_unsigned()) : MakeUnsigned(lhs.as_unsigned() - rhs.as_unsigned());
			}
			else
			{
				lhs = op == "+" ? MakeSigned(lhs.as_signed() + rhs.as_signed()) : MakeSigned(lhs.as_signed() - rhs.as_signed());
			}
		}
		return lhs;
	}

	CEValue parse_multiplicative(bool eval)
	{
		CEValue lhs = parse_unary(eval);
		while (peek_op("*") || peek_op("/") || peek_op("%"))
		{
			string op = get_op();
			CEValue rhs = parse_unary(eval);
			bool is_unsigned = CommonUnsigned(lhs, rhs);
			if (!eval)
			{
				lhs = CEValue(0, is_unsigned);
				continue;
			}
			if (rhs.as_unsigned() == 0 && (op == "/" || op == "%"))
			{
				throw ParseError("divide by zero");
			}
			if (is_unsigned)
			{
				if (op == "*") lhs = MakeUnsigned(lhs.as_unsigned() * rhs.as_unsigned());
				else if (op == "/") lhs = MakeUnsigned(lhs.as_unsigned() / rhs.as_unsigned());
				else lhs = MakeUnsigned(lhs.as_unsigned() % rhs.as_unsigned());
			}
			else
			{
				if ((op == "/" || op == "%") && lhs.as_signed() == LLONG_MIN && rhs.as_signed() == -1)
				{
					throw ParseError("signed overflow");
				}
				if (op == "*") lhs = MakeSigned(lhs.as_signed() * rhs.as_signed());
				else if (op == "/") lhs = MakeSigned(lhs.as_signed() / rhs.as_signed());
				else lhs = MakeSigned(lhs.as_signed() % rhs.as_signed());
			}
		}
		return lhs;
	}

	CEValue parse_unary(bool eval)
	{
		if (peek_op("+"))
		{
			get();
			return parse_unary(eval);
		}
		if (peek_op("-"))
		{
			get();
			CEValue v = parse_unary(eval);
			if (!eval) return v;
			return v.is_unsigned ? MakeUnsigned(static_cast<uint64_t>(-v.as_unsigned())) : MakeSigned(-v.as_signed());
		}
		if (peek_op("!"))
		{
			get();
			CEValue v = parse_unary(eval);
			return MakeSigned(eval ? !ToBool(v) : 0);
		}
		if (peek_op("~"))
		{
			get();
			CEValue v = parse_unary(eval);
			if (!eval) return v;
			return v.is_unsigned ? MakeUnsigned(~v.as_unsigned()) : MakeSigned(~v.as_signed());
		}
		return parse_primary(eval);
	}

	CEValue parse_primary(bool eval)
	{
		if (peek_op("("))
		{
			get();
			CEValue v = parse_conditional(eval);
			if (!peek_op(")"))
			{
				throw ParseError("missing rparen");
			}
			get();
			return v;
		}

		const PPToken& tok = get();
		if (tok.kind == PP_IDENTIFIER)
		{
			if (tok.data == "defined")
			{
				string id;
				if (peek_op("("))
				{
					get();
					if (!peek_identifier_like())
					{
						throw ParseError("bad defined");
					}
					id = get().data;
					if (!peek_op(")"))
					{
						throw ParseError("bad defined");
					}
					get();
				}
				else
				{
					if (!peek_identifier_like())
					{
						throw ParseError("bad defined");
					}
					id = get().data;
				}
				return MakeSigned(eval ? PA3Mock_IsDefinedIdentifier(id) : 0);
			}
			if (tok.data == "true")
			{
				return MakeSigned(1);
			}
			if (tok.data == "false")
			{
				return MakeSigned(0);
			}
			return MakeSigned(0);
		}
		if (tok.kind == PP_NUMBER)
		{
			CEValue v;
			if (!ParseIntegralToken(tok.data, v))
			{
				throw ParseError("non-integral literal");
			}
			return v;
		}
		if (tok.kind == PP_CHAR)
		{
			CEValue v;
			if (!ParseCharToken(tok.data, v))
			{
				throw ParseError("bad char");
			}
			return v;
		}
		throw ParseError("invalid primary");
	}

	bool ToBool(const CEValue& v) const
	{
		return v.as_unsigned() != 0;
	}

	bool ParseCharToken(const string& source, CEValue& out)
	{
		EFundamentalType type;
		vector<unsigned char> bytes;
		if (!ParseCharacterLiteralCore(source, type, bytes))
		{
			return false;
		}
		switch (type)
		{
		case FT_CHAR:
		{
			char v;
			memcpy(&v, bytes.data(), sizeof(v));
			out = MakeSigned(v);
			return true;
		}
		case FT_INT:
		{
			int v;
			memcpy(&v, bytes.data(), sizeof(v));
			out = MakeSigned(v);
			return true;
		}
		case FT_WCHAR_T:
		{
			wchar_t v;
			memcpy(&v, bytes.data(), sizeof(v));
			out = MakeSigned(v);
			return true;
		}
		case FT_CHAR16_T:
		{
			char16_t v;
			memcpy(&v, bytes.data(), sizeof(v));
			out = MakeUnsigned(v);
			return true;
		}
		case FT_CHAR32_T:
		{
			char32_t v;
			memcpy(&v, bytes.data(), sizeof(v));
			out = MakeUnsigned(v);
			return true;
		}
		default:
			return false;
		}
	}

	bool ParseIntegralToken(const string& source, CEValue& out)
	{
		size_t ud_pos = source.find('_');
		string core = ud_pos == string::npos ? source : source.substr(0, ud_pos);
		if (ud_pos != string::npos)
		{
			return false;
		}

		size_t pos0 = 0;
		int base = 10;
		if (core.size() >= 2 && core[0] == '0' && (core[1] == 'x' || core[1] == 'X'))
		{
			base = 16;
			pos0 = 2;
		}
		else if (core.size() > 1 && core[0] == '0' && core[1] >= '0' && core[1] <= '7')
		{
			base = 8;
			pos0 = 1;
		}

		size_t digit_end = pos0;
		while (digit_end < core.size())
		{
			int c = core[digit_end];
			bool ok = base == 16 ? IsHexDigit(c) : (base == 8 ? IsOctDigit(c) : (c >= '0' && c <= '9'));
			if (!ok) break;
			++digit_end;
		}
		if (digit_end == pos0)
		{
			return false;
		}
		string std_suffix = core.substr(digit_end);
		bool is_unsigned = false;
		int long_count = 0;
		if (std_suffix == "") {}
		else if (std_suffix == "u" || std_suffix == "U") is_unsigned = true;
		else if (std_suffix == "l" || std_suffix == "L") long_count = 1;
		else if (std_suffix == "ll" || std_suffix == "LL") long_count = 2;
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
			return false;
		}

		unsigned __int128 value = 0;
		for (size_t i = pos0; i < digit_end; ++i)
		{
			int d = base == 16 ? HexCharToValue(core[i]) : core[i] - '0';
			value = value * base + d;
		}

		const unsigned __int128 I_MAX = 0x7FFFFFFFu;
		const unsigned __int128 UI_MAX = 0xFFFFFFFFu;
		const unsigned __int128 L_MAX = 0x7FFFFFFFFFFFFFFFull;
		const unsigned __int128 UL_MAX = 0xFFFFFFFFFFFFFFFFull;

		struct Candidate { bool is_unsigned; unsigned __int128 max; };
		vector<Candidate> candidates;
		if (!is_unsigned && long_count == 0)
		{
			candidates.push_back({false, I_MAX});
			if (base != 10) candidates.push_back({true, UI_MAX});
			candidates.push_back({false, L_MAX});
			if (base != 10) candidates.push_back({true, UL_MAX});
			candidates.push_back({false, L_MAX});
			if (base != 10) candidates.push_back({true, UL_MAX});
		}
		else if (!is_unsigned && long_count == 1)
		{
			candidates.push_back({false, L_MAX});
			if (base != 10) candidates.push_back({true, UL_MAX});
			candidates.push_back({false, L_MAX});
			if (base != 10) candidates.push_back({true, UL_MAX});
		}
		else if (!is_unsigned && long_count == 2)
		{
			candidates.push_back({false, L_MAX});
			if (base != 10) candidates.push_back({true, UL_MAX});
		}
		else if (is_unsigned && long_count == 0)
		{
			candidates.push_back({true, UI_MAX});
			candidates.push_back({true, UL_MAX});
			candidates.push_back({true, UL_MAX});
		}
		else if (is_unsigned && long_count == 1)
		{
			candidates.push_back({true, UL_MAX});
			candidates.push_back({true, UL_MAX});
		}
		else
		{
			candidates.push_back({true, UL_MAX});
		}

		for (size_t i = 0; i < candidates.size(); ++i)
		{
			if (value <= candidates[i].max)
			{
				out = candidates[i].is_unsigned ?
					MakeUnsigned(static_cast<uint64_t>(value)) :
					MakeSigned(static_cast<int64_t>(value));
				return true;
			}
		}
		return false;
	}
};

string FormatValue(const CEValue& v)
{
	ostringstream oss;
	if (v.is_unsigned)
	{
		oss << v.as_unsigned() << "u";
	}
	else
	{
		oss << v.as_signed();
	}
	return oss.str();
}

int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		vector<int> cps = TransformSource(DecodeUTF8(oss.str()));
		if (!cps.empty() && cps.back() != '\n')
		{
			cps.push_back('\n');
		}

		PPCollector collector;
		PPTokenizer tokenizer(collector);
		vector<PPToken> tokens = tokenizer.tokenize(cps);

		vector<PPToken> line;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].kind == PP_NL)
			{
				if (!line.empty())
				{
					try
					{
						ExpressionParser parser(line);
						cout << FormatValue(parser.parse(true)) << endl;
					}
					catch (exception&)
					{
						cout << "error" << endl;
					}
				}
				line.clear();
			}
			else if (tokens[i].kind != PP_WS)
			{
				line.push_back(tokens[i]);
			}
		}

		cout << "eof" << endl;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
