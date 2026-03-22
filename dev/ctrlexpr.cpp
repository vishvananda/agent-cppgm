// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <memory>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <climits>

using namespace std;

#define CPPGM_PPTOKEN_LIBRARY
#include "pptoken.cpp"

// mock implementation of IsDefinedIdentifier for PA3
// return true iff first code point is odd
bool PA3Mock_IsDefinedIdentifier(const string& identifier)
{
	if (identifier.empty())
		return false;
	else
		return identifier[0] % 2;
}

namespace
{
	enum class Kind
	{
		WS,
		NL,
		HEADER,
		IDENT,
		PPNUM,
		CHAR,
		UCHAR,
		STR,
		USTR,
		PUNC,
		NONWS,
		EOFK
	};

	struct Tok
	{
		Kind kind;
		string data;
	};

	struct Sink : IPPTokenStream
	{
		vector<Tok> toks;

		void emit_whitespace_sequence() { toks.push_back({Kind::WS, string()}); }
		void emit_new_line() { toks.push_back({Kind::NL, string()}); }
		void emit_header_name(const string& data) { toks.push_back({Kind::HEADER, data}); }
		void emit_identifier(const string& data) { toks.push_back({Kind::IDENT, data}); }
		void emit_pp_number(const string& data) { toks.push_back({Kind::PPNUM, data}); }
		void emit_character_literal(const string& data) { toks.push_back({Kind::CHAR, data}); }
		void emit_user_defined_character_literal(const string& data) { toks.push_back({Kind::UCHAR, data}); }
		void emit_string_literal(const string& data) { toks.push_back({Kind::STR, data}); }
		void emit_user_defined_string_literal(const string& data) { toks.push_back({Kind::USTR, data}); }
		void emit_preprocessing_op_or_punc(const string& data) { toks.push_back({Kind::PUNC, data}); }
		void emit_non_whitespace_char(const string& data) { toks.push_back({Kind::NONWS, data}); }
		void emit_eof() { toks.push_back({Kind::EOFK, string()}); }
	};

	enum class TypeKind
	{
		Signed,
		Unsigned
	};

	struct Value
	{
		TypeKind type;
		long long s;
		unsigned long long u;

		static Value make_signed(long long v)
		{
			Value x;
			x.type = TypeKind::Signed;
			x.s = v;
			x.u = static_cast<unsigned long long>(v);
			return x;
		}

		static Value make_unsigned(unsigned long long v)
		{
			Value x;
			x.type = TypeKind::Unsigned;
			x.s = static_cast<long long>(v);
			x.u = v;
			return x;
		}
	};

	struct Lit
	{
		bool ok = false;
		bool raw = false;
		bool is_char = false;
		bool has_ud = false;
		bool bad_ud_suffix = false;
		string ud;
		enum class Enc { Ordinary, U8, U16, U32, Wide } enc = Enc::Ordinary;
		vector<uint32_t> cps;
	};

	struct ParseError : runtime_error
	{
		explicit ParseError(const string& s) : runtime_error(s) {}
	};

	struct EvalError : runtime_error
	{
		EvalError() : runtime_error("evaluation error in controlling expression") {}
	};

	bool is_identifier_continue_char(unsigned char c)
	{
		return c == '_' || isalnum(c);
	}

	unsigned __int128 parse_u128(const string& s, int base, bool& ok)
	{
		ok = true;
		unsigned __int128 v = 0;
		for (size_t i = 0; i < s.size(); ++i)
		{
			int d = -1;
			unsigned char c = static_cast<unsigned char>(s[i]);
			if (c >= '0' && c <= '9')
				d = c - '0';
			else if (c >= 'a' && c <= 'f')
				d = 10 + (c - 'a');
			else if (c >= 'A' && c <= 'F')
				d = 10 + (c - 'A');
			if (d < 0 || d >= base)
			{
				ok = false;
				return 0;
			}
			v = v * static_cast<unsigned>(base) + static_cast<unsigned>(d);
		}
		return v;
	}

	Value promote_bool(bool b)
	{
		return Value::make_signed(b ? 1 : 0);
	}

	bool is_truthy(const Value& v)
	{
		return v.type == TypeKind::Unsigned ? v.u != 0 : v.s != 0;
	}

	unsigned long long as_unsigned(const Value& v)
	{
		return v.type == TypeKind::Unsigned ? v.u : static_cast<unsigned long long>(v.s);
	}

	bool result_is_unsigned(const Value& a, const Value& b)
	{
		return a.type == TypeKind::Unsigned || b.type == TypeKind::Unsigned;
	}

	Lit parse_literal(const string& src, bool is_char, Lit& lit)
	{
		lit = Lit();
		size_t p = 0;
		if (src.compare(0, 2, "u8") == 0)
		{
			lit.enc = Lit::Enc::U8;
			p = 2;
		}
		else if (!src.empty() && src[0] == 'u')
		{
			lit.enc = Lit::Enc::U16;
			p = 1;
		}
		else if (!src.empty() && src[0] == 'U')
		{
			lit.enc = Lit::Enc::U32;
			p = 1;
		}
		else if (!src.empty() && src[0] == 'L')
		{
			lit.enc = Lit::Enc::Wide;
			p = 1;
		}
		if (p < src.size() && src[p] == 'R')
		{
			lit.raw = true;
			++p;
		}
		if (p >= src.size())
			return lit;
		char q = src[p];
		if (q != '"' && q != '\'')
			return lit;
		lit.is_char = (q == '\'');
		++p;
		size_t body_begin = p, body_end = string::npos;
		if (lit.raw)
		{
			size_t delim_begin = p;
			while (p < src.size() && src[p] != '(')
				++p;
			if (p >= src.size())
				return lit;
			string delim = src.substr(delim_begin, p - delim_begin);
			if (delim.size() > 16)
				return lit;
			++p;
			body_begin = p;
			while (p < src.size())
			{
				if (src[p] == ')' && p + delim.size() + 1 < src.size() && src.compare(p + 1, delim.size(), delim) == 0 && src[p + 1 + delim.size()] == '"')
				{
					body_end = p;
					p += delim.size() + 2;
					break;
				}
				++p;
			}
			if (body_end == string::npos)
				return lit;
		}
		else
		{
			while (p < src.size())
			{
				char c = src[p];
				if (c == '\\')
				{
					if (p + 1 >= src.size())
						return lit;
					char n = src[p + 1];
					if (n == 'x')
					{
						p += 2;
						bool any = false;
						while (p < src.size())
						{
							unsigned char d = static_cast<unsigned char>(src[p]);
							if (!((d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F')))
								break;
							any = true;
							++p;
						}
						if (!any)
							return lit;
						continue;
					}
					if (n >= '0' && n <= '7')
					{
						p += 2;
						int count = 1;
						while (p < src.size() && count < 3 && src[p] >= '0' && src[p] <= '7')
						{
							++p;
							++count;
						}
						continue;
					}
					p += 2;
					continue;
				}
				if (c == q)
				{
					body_end = p;
					++p;
					break;
				}
				++p;
			}
			if (body_end == string::npos)
				return lit;
		}
		if (p < src.size())
		{
			string suf = src.substr(p);
			if (suf[0] != '_')
			{
				lit.bad_ud_suffix = true;
				return lit;
			}
			for (size_t i = 1; i < suf.size(); ++i)
			{
				unsigned char c = static_cast<unsigned char>(suf[i]);
				if (!is_identifier_continue_char(c))
					return lit;
			}
			lit.has_ud = true;
			lit.ud = suf;
		}
		vector<unsigned char> body(src.begin() + body_begin, src.begin() + body_end);
		vector<uint32_t> cps = decode_utf8_bytes(body);
		if (lit.raw)
		{
			lit.cps = cps;
			lit.ok = true;
			return lit;
		}
		for (size_t i = 0; i < cps.size(); ++i)
		{
			uint32_t c = cps[i];
			if (c != '\\')
			{
				lit.cps.push_back(c);
				continue;
			}
			if (i + 1 >= cps.size())
				return lit;
			uint32_t n = cps[i + 1];
			if (SimpleEscapeSequence_CodePoints.count(static_cast<int>(n)))
			{
				switch (n)
				{
				case '\'': lit.cps.push_back('\''); break;
				case '"': lit.cps.push_back('"'); break;
				case '?': lit.cps.push_back('?'); break;
				case '\\': lit.cps.push_back('\\'); break;
				case 'a': lit.cps.push_back(0x07); break;
				case 'b': lit.cps.push_back(0x08); break;
				case 'f': lit.cps.push_back(0x0C); break;
				case 'n': lit.cps.push_back(0x0A); break;
				case 'r': lit.cps.push_back(0x0D); break;
				case 't': lit.cps.push_back(0x09); break;
				case 'v': lit.cps.push_back(0x0B); break;
				default: return lit;
				}
				++i;
				continue;
			}
			if (n == 'x')
			{
				size_t j = i + 2;
				unsigned __int128 v = 0;
				bool any = false;
				while (j < cps.size())
				{
					uint32_t d = cps[j];
					int dig = -1;
					if (d >= '0' && d <= '9')
						dig = d - '0';
					else if (d >= 'a' && d <= 'f')
						dig = 10 + (d - 'a');
					else if (d >= 'A' && d <= 'F')
						dig = 10 + (d - 'A');
					else
						break;
					any = true;
					v = v * 16 + static_cast<unsigned>(dig);
					++j;
				}
				if (!any)
					return lit;
				lit.cps.push_back(static_cast<uint32_t>(v));
				i = j - 1;
				continue;
			}
			if (n == 'u' || n == 'U')
			{
				size_t digits = (n == 'u') ? 4 : 8;
				if (i + 2 + digits > cps.size())
					return lit;
				unsigned __int128 v = 0;
				for (size_t j = 0; j < digits; ++j)
				{
					uint32_t d = cps[i + 2 + j];
					if (!((d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F')))
						return lit;
					int dig = -1;
					if (d >= '0' && d <= '9')
						dig = d - '0';
					else if (d >= 'a' && d <= 'f')
						dig = 10 + (d - 'a');
					else
						dig = 10 + (d - 'A');
					v = v * 16 + static_cast<unsigned>(dig);
				}
				lit.cps.push_back(static_cast<uint32_t>(v));
				i += digits + 1;
				continue;
			}
			if (n >= '0' && n <= '7')
			{
				size_t j = i + 1;
				unsigned __int128 v = 0;
				int count = 0;
				while (j < cps.size() && cps[j] >= '0' && cps[j] <= '7' && count < 3)
				{
					v = v * 8 + static_cast<unsigned>(cps[j] - '0');
					++j;
					++count;
				}
				lit.cps.push_back(static_cast<uint32_t>(v));
				i = j - 1;
				continue;
			}
			return lit;
		}
		lit.ok = true;
		return lit;
	}

	Value parse_character_literal(const string& src)
	{
		Lit lit;
		if (!parse_literal(src, true, lit).ok)
		{
			if (lit.bad_ud_suffix)
				throw ParseError(string("character-literal in controlling expression"));
			throw ParseError(string("character-literal in controlling expression"));
		}
		if (lit.has_ud)
			throw ParseError(string("character-literal in controlling expression"));
		if (lit.cps.size() != 1)
			throw ParseError(string("character-literal in controlling expression"));

		uint32_t cp = lit.cps[0];
		if (lit.enc == Lit::Enc::Ordinary)
		{
			if (cp <= 127)
			{
				char v = static_cast<char>(cp);
				return Value::make_signed(static_cast<long long>(v));
			}
			return Value::make_signed(static_cast<long long>(cp));
		}
		if (lit.enc == Lit::Enc::U16 || lit.enc == Lit::Enc::U32)
		{
			if (lit.enc == Lit::Enc::U16 && cp > 0xFFFF)
				throw ParseError(string("UTF-16 char literal out of range: ") + src + " " + to_string(cp));
			if (lit.enc == Lit::Enc::U32 && cp > 0x10FFFF)
				throw ParseError(string("UTF-32 char literal out of range: ") + src + " " + to_string(cp));
			return Value::make_unsigned(static_cast<unsigned long long>(cp));
		}
		if (cp > 0x10FFFF)
			throw ParseError(string("wchar_t char literal out of range: ") + src + " " + to_string(cp));
		return Value::make_signed(static_cast<long long>(cp));
	}

	Value parse_pp_number(const string& src)
	{
		if (src.find_first_of(".eEpP") != string::npos)
		{
			throw ParseError(string("floating literal in controlling expression"));
		}

		string main = src;
		bool is_hex = main.compare(0, 2, "0x") == 0 || main.compare(0, 2, "0X") == 0;
		bool is_oct = !is_hex && !main.empty() && main[0] == '0';

		size_t p = 0;
		if (is_hex)
		{
			p = 2;
			while (p < main.size() && isxdigit(static_cast<unsigned char>(main[p])))
				++p;
		}
		else if (is_oct)
		{
			p = 1;
			while (p < main.size() && main[p] >= '0' && main[p] <= '7')
				++p;
		}
		else
		{
			while (p < main.size() && isdigit(static_cast<unsigned char>(main[p])))
				++p;
		}

		string digits;
		if (is_hex)
			digits = main.substr(2, p - 2);
		else if (is_oct)
			digits = (p > 1 ? main.substr(1, p - 1) : string("0"));
		else
			digits = main.substr(0, p);

		string suffix = main.substr(p);
		if (digits.empty())
			throw ParseError(string("expected identifier or literal in controlling expression"));

		bool ok = false;
		unsigned __int128 v = parse_u128(digits, is_hex ? 16 : (is_oct ? 8 : 10), ok);
		if (!ok)
			throw ParseError(string("expected identifier or literal in controlling expression"));

		if (!suffix.empty())
		{
			bool u = false, l = false, ll = false;
			int uc = 0, lc = 0;
			bool mixed_l = false;
			char first_l = 0;
			bool bad_char = false;
			for (size_t i = 0; i < suffix.size(); ++i)
			{
				char c = suffix[i];
				if (c == 'u' || c == 'U')
					++uc;
				else if (c == 'l' || c == 'L')
				{
					++lc;
					if (first_l == 0)
						first_l = c;
					else if (c != first_l)
						mixed_l = true;
				}
				else
					bad_char = true;
			}
			if (bad_char)
				throw ParseError(string("expected identifier or literal in controlling expression"));
			if (uc == 1 && lc == 0)
				u = true;
			else if (uc == 0 && lc == 1)
				l = true;
			else if (uc == 0 && lc == 2 && !mixed_l)
				ll = true;
			else if (uc == 1 && lc == 1)
			{
				u = true;
				l = true;
			}
			else if (uc == 1 && lc == 2 && !mixed_l)
			{
				u = true;
				ll = true;
			}
			else
				throw ParseError(string("expected identifier or literal in controlling expression"));

			if (is_hex || is_oct)
			{
				if (v > static_cast<unsigned __int128>(ULLONG_MAX))
					throw ParseError(string(is_hex ? "hex integer literal out of range (#1): " : "octal integer literal out of range (#2): ") + digits);
				if (u && l)
					return Value::make_unsigned(static_cast<unsigned long long>(v));
				if (u && ll)
					return Value::make_unsigned(static_cast<unsigned long long>(v));
				if (u)
					return Value::make_unsigned(static_cast<unsigned long long>(v));
				if (l)
				{
					if (v <= static_cast<unsigned __int128>(LONG_MAX))
						return Value::make_signed(static_cast<long long>(v));
					if (v <= static_cast<unsigned __int128>(ULONG_MAX))
						return Value::make_unsigned(static_cast<unsigned long long>(v));
					throw ParseError(string(is_hex ? "hex integer literal out of range (#1): " : "octal integer literal out of range (#2): ") + digits);
				}
				if (ll)
				{
					if (v <= static_cast<unsigned __int128>(LLONG_MAX))
						return Value::make_signed(static_cast<long long>(v));
					return Value::make_unsigned(static_cast<unsigned long long>(v));
				}
			}
			else
			{
				if (v > static_cast<unsigned __int128>(ULLONG_MAX))
					throw ParseError(string("decimal integer literal out of range(#2): ") + digits);
				if (u && l)
				{
					if (v <= static_cast<unsigned __int128>(ULONG_MAX))
						return Value::make_unsigned(static_cast<unsigned long long>(v));
					throw ParseError(string("decimal integer literal out of range(#4): ") + digits);
				}
				if (u && ll)
				{
					if (v <= static_cast<unsigned __int128>(ULLONG_MAX))
						return Value::make_unsigned(static_cast<unsigned long long>(v));
					throw ParseError(string("decimal integer literal out of range(#4): ") + digits);
				}
				if (u)
					return Value::make_unsigned(static_cast<unsigned long long>(v));
				if (l)
				{
					if (v <= static_cast<unsigned __int128>(LONG_MAX))
						return Value::make_signed(static_cast<long long>(v));
					throw ParseError(string("decimal integer literal out of range(#4): ") + digits);
				}
				if (ll)
				{
					if (v <= static_cast<unsigned __int128>(LLONG_MAX))
						return Value::make_signed(static_cast<long long>(v));
					throw ParseError(string("decimal integer literal out of range(#4): ") + digits);
				}
			}
		}

		if (is_hex)
		{
			if (v > static_cast<unsigned __int128>(ULLONG_MAX))
				throw ParseError(string("hex integer literal out of range (#1): ") + digits);
			if (v <= static_cast<unsigned __int128>(INT_MAX))
				return Value::make_signed(static_cast<long long>(static_cast<int>(v)));
			if (v <= static_cast<unsigned __int128>(UINT_MAX))
				return Value::make_unsigned(static_cast<unsigned long long>(static_cast<unsigned>(v)));
			if (v <= static_cast<unsigned __int128>(LONG_MAX))
				return Value::make_signed(static_cast<long long>(static_cast<long>(v)));
			if (v <= static_cast<unsigned __int128>(ULONG_MAX))
				return Value::make_unsigned(static_cast<unsigned long long>(static_cast<unsigned long>(v)));
			if (v <= static_cast<unsigned __int128>(LLONG_MAX))
				return Value::make_signed(static_cast<long long>(static_cast<long long>(v)));
			return Value::make_unsigned(static_cast<unsigned long long>(v));
		}

		if (is_oct)
		{
			if (v > static_cast<unsigned __int128>(ULLONG_MAX))
				throw ParseError(string("octal integer literal out of range (#2): ") + digits);
			if (v <= static_cast<unsigned __int128>(INT_MAX))
				return Value::make_signed(static_cast<long long>(static_cast<int>(v)));
			if (v <= static_cast<unsigned __int128>(UINT_MAX))
				return Value::make_unsigned(static_cast<unsigned long long>(static_cast<unsigned>(v)));
			if (v <= static_cast<unsigned __int128>(LONG_MAX))
				return Value::make_signed(static_cast<long long>(static_cast<long>(v)));
			if (v <= static_cast<unsigned __int128>(ULONG_MAX))
				return Value::make_unsigned(static_cast<unsigned long long>(static_cast<unsigned long>(v)));
			if (v <= static_cast<unsigned __int128>(LLONG_MAX))
				return Value::make_signed(static_cast<long long>(static_cast<long long>(v)));
			return Value::make_unsigned(static_cast<unsigned long long>(v));
		}

		if (v <= static_cast<unsigned __int128>(INT_MAX))
			return Value::make_signed(static_cast<long long>(static_cast<int>(v)));
		if (v <= static_cast<unsigned __int128>(LONG_MAX))
			return Value::make_signed(static_cast<long long>(static_cast<long>(v)));
		if (v <= static_cast<unsigned __int128>(LLONG_MAX))
			return Value::make_signed(static_cast<long long>(static_cast<long long>(v)));
		if (v <= static_cast<unsigned __int128>(ULLONG_MAX))
			return Value::make_unsigned(static_cast<unsigned long long>(v));
		throw ParseError(string("decimal integer literal out of range(#2): ") + digits);
	}

	struct Node
	{
		enum class Kind
		{
			VALUE,
			UNARY,
			BINARY,
			COND,
			DEFINED
		};

		Kind kind;
		string op;
		Value value;
		string ident;
		unique_ptr<Node> left;
		unique_ptr<Node> middle;
		unique_ptr<Node> right;
	};

	struct Parser
	{
		const vector<Tok>& toks;
		size_t pos = 0;

		explicit Parser(const vector<Tok>& toks) : toks(toks) {}

		bool at_end() const
		{
			return pos >= toks.size();
		}

		const Tok& peek(size_t off = 0) const
		{
			static Tok eof = {Kind::EOFK, string()};
			return pos + off < toks.size() ? toks[pos + off] : eof;
		}

		bool accept_punc(const string& s)
		{
			if (!at_end() && peek().kind == Kind::PUNC && peek().data == s)
			{
				++pos;
				return true;
			}
			return false;
		}

		bool accept_ident(const string& s)
		{
			if (!at_end() && peek().kind == Kind::IDENT && peek().data == s)
			{
				++pos;
				return true;
			}
			return false;
		}

		const Tok& consume()
		{
			if (at_end())
				throw ParseError("expected identifier or literal in controlling expression");
			return toks[pos++];
		}

		unique_ptr<Node> make_value(const Value& v)
		{
			unique_ptr<Node> n(new Node());
			n->kind = Node::Kind::VALUE;
			n->value = v;
			return n;
		}

		unique_ptr<Node> make_unary(const string& op, unique_ptr<Node> child)
		{
			unique_ptr<Node> n(new Node());
			n->kind = Node::Kind::UNARY;
			n->op = op;
			n->left = std::move(child);
			return n;
		}

		unique_ptr<Node> make_binary(const string& op, unique_ptr<Node> lhs, unique_ptr<Node> rhs)
		{
			unique_ptr<Node> n(new Node());
			n->kind = Node::Kind::BINARY;
			n->op = op;
			n->left = std::move(lhs);
			n->right = std::move(rhs);
			return n;
		}

		unique_ptr<Node> make_cond(unique_ptr<Node> cond, unique_ptr<Node> t, unique_ptr<Node> f)
		{
			unique_ptr<Node> n(new Node());
			n->kind = Node::Kind::COND;
			n->left = std::move(cond);
			n->middle = std::move(t);
			n->right = std::move(f);
			return n;
		}

		unique_ptr<Node> make_defined(const string& ident)
		{
			unique_ptr<Node> n(new Node());
			n->kind = Node::Kind::DEFINED;
			n->ident = ident;
			return n;
		}

		unique_ptr<Node> parse()
		{
			auto expr = parse_conditional();
			if (!at_end())
				throw ParseError("left over tokens at end of controlling expression");
			return expr;
		}

		unique_ptr<Node> parse_conditional()
		{
			auto cond = parse_logical_or();
			if (accept_punc("?"))
			{
				auto t = parse_conditional();
				if (!accept_punc(":"))
					throw ParseError("expected identifier or literal in controlling expression");
				auto f = parse_conditional();
				return make_cond(std::move(cond), std::move(t), std::move(f));
			}
			return cond;
		}

		unique_ptr<Node> parse_logical_or()
		{
			auto lhs = parse_logical_and();
			while (accept_punc("||"))
			{
				auto rhs = parse_logical_and();
				lhs = make_binary("||", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<Node> parse_logical_and()
		{
			auto lhs = parse_inclusive_or();
			while (accept_punc("&&"))
			{
				auto rhs = parse_inclusive_or();
				lhs = make_binary("&&", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<Node> parse_inclusive_or()
		{
			auto lhs = parse_exclusive_or();
			while (accept_punc("|"))
			{
				auto rhs = parse_exclusive_or();
				lhs = make_binary("|", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<Node> parse_exclusive_or()
		{
			auto lhs = parse_and();
			while (accept_punc("^"))
			{
				auto rhs = parse_and();
				lhs = make_binary("^", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<Node> parse_and()
		{
			auto lhs = parse_equality();
			while (accept_punc("&"))
			{
				auto rhs = parse_equality();
				lhs = make_binary("&", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<Node> parse_equality()
		{
			auto lhs = parse_relational();
			while (true)
			{
				if (accept_punc("=="))
				{
					auto rhs = parse_relational();
					lhs = make_binary("==", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("!="))
				{
					auto rhs = parse_relational();
					lhs = make_binary("!=", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<Node> parse_relational()
		{
			auto lhs = parse_shift();
			while (true)
			{
				if (accept_punc("<"))
				{
					auto rhs = parse_shift();
					lhs = make_binary("<", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc(">"))
				{
					auto rhs = parse_shift();
					lhs = make_binary(">", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("<="))
				{
					auto rhs = parse_shift();
					lhs = make_binary("<=", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc(">="))
				{
					auto rhs = parse_shift();
					lhs = make_binary(">=", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<Node> parse_shift()
		{
			auto lhs = parse_additive();
			while (true)
			{
				if (accept_punc("<<"))
				{
					auto rhs = parse_additive();
					lhs = make_binary("<<", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc(">>"))
				{
					auto rhs = parse_additive();
					lhs = make_binary(">>", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<Node> parse_additive()
		{
			auto lhs = parse_multiplicative();
			while (true)
			{
				if (accept_punc("+"))
				{
					auto rhs = parse_multiplicative();
					lhs = make_binary("+", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("-"))
				{
					auto rhs = parse_multiplicative();
					lhs = make_binary("-", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<Node> parse_multiplicative()
		{
			auto lhs = parse_unary();
			while (true)
			{
				if (accept_punc("*"))
				{
					auto rhs = parse_unary();
					lhs = make_binary("*", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("/"))
				{
					auto rhs = parse_unary();
					lhs = make_binary("/", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("%"))
				{
					auto rhs = parse_unary();
					lhs = make_binary("%", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<Node> parse_unary()
		{
			if (accept_punc("+"))
				return make_unary("+", parse_unary());
			if (accept_punc("-"))
				return make_unary("-", parse_unary());
			if (accept_punc("!"))
				return make_unary("!", parse_unary());
			if (accept_punc("~"))
				return make_unary("~", parse_unary());
			return parse_primary();
		}

		unique_ptr<Node> parse_primary()
		{
			if (accept_punc("("))
			{
				auto expr = parse_conditional();
				if (!accept_punc(")"))
					throw ParseError("closing bracket expected in controlling expression");
				return expr;
			}

			if (at_end())
				throw ParseError("expected identifier or literal in controlling expression");

			const Tok& t = peek();
			if (t.kind == Kind::IDENT)
			{
				if (t.data == "defined")
					return parse_defined();
				++pos;
				if (t.data == "true")
					return make_value(Value::make_signed(1));
				if (t.data == "false")
					return make_value(Value::make_signed(0));
				return make_value(Value::make_signed(0));
			}
			if (t.kind == Kind::PPNUM)
			{
				++pos;
				return make_value(parse_pp_number(t.data));
			}
			if (t.kind == Kind::CHAR)
			{
				++pos;
				return make_value(parse_character_literal(t.data));
			}
			if (t.kind == Kind::UCHAR)
			{
				++pos;
				throw ParseError("character-literal in controlling expression");
			}
			if (t.kind == Kind::STR || t.kind == Kind::USTR)
			{
				++pos;
				throw ParseError("string-literal in controlling expression");
			}
			if (t.kind == Kind::HEADER || t.kind == Kind::NONWS || t.kind == Kind::PUNC || t.kind == Kind::NL || t.kind == Kind::WS || t.kind == Kind::EOFK)
				throw ParseError("expected identifier or literal in controlling expression");
			throw ParseError("expected identifier or literal in controlling expression");
		}

		unique_ptr<Node> parse_defined()
		{
			consume();
			if (accept_punc("("))
			{
				if (at_end() || peek().kind != Kind::IDENT)
				{
					if (at_end() || (peek().kind == Kind::PUNC && peek().data == ")"))
						throw ParseError("no closing paren on defined(identifier) in controlling expression");
					throw ParseError("non-identifier after defined in controlling expression");
				}
				string ident = consume().data;
				if (!accept_punc(")"))
					throw ParseError("no closing paren on defined(identifier) in controlling expression");
				return make_value(promote_bool(PA3Mock_IsDefinedIdentifier(ident)));
			}

			if (at_end() || peek().kind != Kind::IDENT)
				throw ParseError("non-identifier after defined in controlling expression");
			string ident = consume().data;
			return make_value(promote_bool(PA3Mock_IsDefinedIdentifier(ident)));
		}
	};

	bool type_is_unsigned(const Node& n);

	bool type_is_unsigned(const Node& n)
	{
		switch (n.kind)
		{
		case Node::Kind::VALUE:
			return n.value.type == TypeKind::Unsigned;
		case Node::Kind::DEFINED:
			return false;
		case Node::Kind::UNARY:
			return type_is_unsigned(*n.left);
		case Node::Kind::BINARY:
			if (n.op == "<<" || n.op == ">>")
				return type_is_unsigned(*n.left);
			if (n.op == "&&" || n.op == "||" || n.op == "==" || n.op == "!=" || n.op == "<" || n.op == ">" || n.op == "<=" || n.op == ">=")
				return false;
			return type_is_unsigned(*n.left) || type_is_unsigned(*n.right);
		case Node::Kind::COND:
			return type_is_unsigned(*n.middle) || type_is_unsigned(*n.right);
		}
		return false;
	}

	Value eval(const Node& n)
	{
		switch (n.kind)
		{
		case Node::Kind::VALUE:
			return n.value;
		case Node::Kind::DEFINED:
			return promote_bool(PA3Mock_IsDefinedIdentifier(n.ident));
		case Node::Kind::UNARY:
		{
			Value v = eval(*n.left);
			if (n.op == "+")
				return v;
			if (n.op == "-")
			{
				if (v.type == TypeKind::Unsigned)
					return Value::make_unsigned(0ULL - v.u);
				return Value::make_signed(-v.s);
			}
			if (n.op == "!")
				return promote_bool(!is_truthy(v));
			if (n.op == "~")
			{
				if (v.type == TypeKind::Unsigned)
					return Value::make_unsigned(~v.u);
				return Value::make_signed(~v.s);
			}
			throw EvalError();
		}
		case Node::Kind::BINARY:
		{
			if (n.op == "&&")
			{
				Value lhs = eval(*n.left);
				if (!is_truthy(lhs))
					return promote_bool(false);
				return promote_bool(is_truthy(eval(*n.right)));
			}
			if (n.op == "||")
			{
				Value lhs = eval(*n.left);
				if (is_truthy(lhs))
					return promote_bool(true);
				return promote_bool(is_truthy(eval(*n.right)));
			}

			Value lhs = eval(*n.left);
			Value rhs = eval(*n.right);

			bool use_unsigned = result_is_unsigned(lhs, rhs);
			if (n.op == "+")
			{
				if (use_unsigned)
					return Value::make_unsigned(as_unsigned(lhs) + as_unsigned(rhs));
				return Value::make_signed(lhs.s + rhs.s);
			}
			if (n.op == "-")
			{
				if (use_unsigned)
					return Value::make_unsigned(as_unsigned(lhs) - as_unsigned(rhs));
				return Value::make_signed(lhs.s - rhs.s);
			}
			if (n.op == "*")
			{
				if (use_unsigned)
					return Value::make_unsigned(as_unsigned(lhs) * as_unsigned(rhs));
				return Value::make_signed(lhs.s * rhs.s);
			}
			if (n.op == "/")
			{
				if ((rhs.type == TypeKind::Unsigned && rhs.u == 0) || (rhs.type == TypeKind::Signed && rhs.s == 0))
					throw EvalError();
				if (use_unsigned)
					return Value::make_unsigned(as_unsigned(lhs) / as_unsigned(rhs));
				if (lhs.s == LLONG_MIN && rhs.s == -1)
					throw EvalError();
				return Value::make_signed(lhs.s / rhs.s);
			}
			if (n.op == "%")
			{
				if ((rhs.type == TypeKind::Unsigned && rhs.u == 0) || (rhs.type == TypeKind::Signed && rhs.s == 0))
					throw EvalError();
				if (use_unsigned)
					return Value::make_unsigned(as_unsigned(lhs) % as_unsigned(rhs));
				if (lhs.s == LLONG_MIN && rhs.s == -1)
					throw EvalError();
				return Value::make_signed(lhs.s % rhs.s);
			}
			if (n.op == "&")
			{
				if (use_unsigned)
					return Value::make_unsigned(as_unsigned(lhs) & as_unsigned(rhs));
				return Value::make_signed(lhs.s & rhs.s);
			}
			if (n.op == "|")
			{
				if (use_unsigned)
					return Value::make_unsigned(as_unsigned(lhs) | as_unsigned(rhs));
				return Value::make_signed(lhs.s | rhs.s);
			}
			if (n.op == "^")
			{
				if (use_unsigned)
					return Value::make_unsigned(as_unsigned(lhs) ^ as_unsigned(rhs));
				return Value::make_signed(lhs.s ^ rhs.s);
			}
			if (n.op == "<<")
			{
				unsigned long long shift = rhs.type == TypeKind::Unsigned ? rhs.u : static_cast<unsigned long long>(rhs.s);
				if (rhs.type == TypeKind::Signed && rhs.s < 0)
					throw EvalError();
				if (shift >= 64)
					throw EvalError();
				if (lhs.type == TypeKind::Unsigned)
					return Value::make_unsigned(lhs.u << shift);
				unsigned long long bits = static_cast<unsigned long long>(lhs.s);
				bits <<= shift;
				return Value::make_signed(static_cast<long long>(bits));
			}
			if (n.op == ">>")
			{
				unsigned long long shift = rhs.type == TypeKind::Unsigned ? rhs.u : static_cast<unsigned long long>(rhs.s);
				if (rhs.type == TypeKind::Signed && rhs.s < 0)
					throw EvalError();
				if (shift >= 64)
					throw EvalError();
				if (lhs.type == TypeKind::Unsigned)
					return Value::make_unsigned(lhs.u >> shift);
				return Value::make_signed(lhs.s >> shift);
			}
			if (n.op == "==")
			{
				bool b = use_unsigned ? (as_unsigned(lhs) == as_unsigned(rhs)) : (lhs.s == rhs.s);
				return promote_bool(b);
			}
			if (n.op == "!=")
			{
				bool b = use_unsigned ? (as_unsigned(lhs) != as_unsigned(rhs)) : (lhs.s != rhs.s);
				return promote_bool(b);
			}
			if (n.op == "<")
			{
				bool b = use_unsigned ? (as_unsigned(lhs) < as_unsigned(rhs)) : (lhs.s < rhs.s);
				return promote_bool(b);
			}
			if (n.op == ">")
			{
				bool b = use_unsigned ? (as_unsigned(lhs) > as_unsigned(rhs)) : (lhs.s > rhs.s);
				return promote_bool(b);
			}
			if (n.op == "<=")
			{
				bool b = use_unsigned ? (as_unsigned(lhs) <= as_unsigned(rhs)) : (lhs.s <= rhs.s);
				return promote_bool(b);
			}
			if (n.op == ">=")
			{
				bool b = use_unsigned ? (as_unsigned(lhs) >= as_unsigned(rhs)) : (lhs.s >= rhs.s);
				return promote_bool(b);
			}
			throw EvalError();
		}
		case Node::Kind::COND:
		{
			Value cond = eval(*n.left);
			bool use_unsigned = type_is_unsigned(*n.middle) || type_is_unsigned(*n.right);
			if (is_truthy(cond))
			{
				Value t = eval(*n.middle);
				return use_unsigned ? Value::make_unsigned(as_unsigned(t)) : t;
			}
			else
			{
				Value f = eval(*n.right);
				return use_unsigned ? Value::make_unsigned(as_unsigned(f)) : f;
			}
		}
		}
		throw EvalError();
	}

	void write_result(const Value& v)
	{
		if (v.type == TypeKind::Unsigned)
			cout << v.u << "u\n";
		else
			cout << v.s << "\n";
	}

	vector<Tok> line_tokens(const vector<Tok>& toks, size_t begin, size_t end)
	{
		vector<Tok> out;
		for (size_t i = begin; i < end; ++i)
		{
			if (toks[i].kind == Kind::WS)
				continue;
			Tok t = toks[i];
			if (t.kind == Kind::PUNC)
			{
				if (t.data == "and")
					t.data = "&&";
				else if (t.data == "or")
					t.data = "||";
				else if (t.data == "not")
					t.data = "!";
				else if (t.data == "compl")
					t.data = "~";
				else if (t.data == "bitand")
					t.data = "&";
				else if (t.data == "bitor")
					t.data = "|";
				else if (t.data == "xor")
					t.data = "^";
				else if (t.data == "not_eq")
					t.data = "!=";
				else if (t.data == "and_eq")
					t.data = "&=";
				else if (t.data == "or_eq")
					t.data = "|=";
				else if (t.data == "xor_eq")
					t.data = "^=";
			}
			out.push_back(t);
		}
		return out;
	}
}

int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		string input = oss.str();

		Sink sink;
		PPTokenizer tokenizer(sink);
		for (size_t i = 0; i < input.size(); ++i)
			tokenizer.process(static_cast<unsigned char>(input[i]));
		tokenizer.process(EndOfFile);

		for (size_t i = 0; i < sink.toks.size(); )
		{
			size_t j = i;
			while (j < sink.toks.size() && sink.toks[j].kind != Kind::NL && sink.toks[j].kind != Kind::EOFK)
				++j;
			vector<Tok> line = line_tokens(sink.toks, i, j);
			if (!line.empty())
			{
				try
				{
					Parser parser(line);
					unique_ptr<Node> expr = parser.parse();
					Value v = eval(*expr);
					write_result(v);
				}
				catch (const ParseError& e)
				{
					cerr << "ERROR: " << e.what() << endl;
					cout << "error\n";
				}
				catch (const EvalError& e)
				{
					cerr << "ERROR: " << e.what() << endl;
					cout << "error\n";
				}
			}
			if (j < sink.toks.size() && sink.toks[j].kind == Kind::NL)
				i = j + 1;
			else
				i = j;
			if (j < sink.toks.size() && sink.toks[j].kind == Kind::EOFK)
				break;
		}

		cout << "eof\n";
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
