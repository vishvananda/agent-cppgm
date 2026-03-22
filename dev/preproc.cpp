// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <utility>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <fstream>
#include <unordered_set>
#include <memory>
#include <cctype>
#include <cstdint>
#include <climits>
#include <ctime>
#include <cstdlib>

using namespace std;

// For pragma once implementation:
// system-wide unique file id type `PA5FileId`
typedef pair<unsigned long int, unsigned long int> PA5FileId;

// bootstrap system call interface, used by PA5GetFileId
extern "C" long int syscall(long int n, ...) throw ();

// PA5GetFileId returns true iff file found at path `path`.
// out parameter `out_fileid` is set to file id
bool PA5GetFileId(const string& path, PA5FileId& out_fileid)
{
	struct
	{
		unsigned long int dev;
		unsigned long int ino;
		long int unused[16];
	} data;

	int res = syscall(4, path.c_str(), &data);
	out_fileid = make_pair(data.dev, data.ino);
	return res == 0;
}

// OPTIONAL: Also search `PA5StdIncPaths` on `--stdinc` command-line switch (not by default)
vector<string> PA5StdIncPaths =
{
	"/usr/include/c++/4.7/",
	"/usr/include/c++/4.7/x86_64-linux-gnu/",
	"/usr/include/c++/4.7/backward/",
	"/usr/lib/gcc/x86_64-linux-gnu/4.7/include/",
	"/usr/local/include/",
	"/usr/lib/gcc/x86_64-linux-gnu/4.7/include-fixed/",
	"/usr/include/x86_64-linux-gnu/",
	"/usr/include/"
};

#define CPPGM_PPTOKEN_LIBRARY
#define CPPGM_PPTOKEN_SKIP_TRIGRAPHS_IN_STRING_LITERALS
#define CPPGM_MACRO_LIBRARY
#define main CPPGM_POSTTOKEN_MAIN
#include "macro.cpp"
#undef main

namespace pa5preproc
{
	struct PA5FileIdHash
	{
		size_t operator()(const PA5FileId& id) const
		{
			size_t h1 = std::hash<unsigned long int>()(id.first);
			size_t h2 = std::hash<unsigned long int>()(id.second);
			return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
		}
	};

	struct CondFrame
	{
		bool parent_active = true;
		bool branch_taken = false;
		bool active = true;
		bool saw_else = false;
	};

	struct State
	{
		unordered_map<string, Macro> macros;
		unordered_set<PA5FileId, PA5FileIdHash> once_files;
		string build_date;
		string build_time;
		string author;
	};

	// ------------------------------------------------------------------
	// Controlling-expression evaluator
	// ------------------------------------------------------------------

	enum class ExprTypeKind
	{
		Signed,
		Unsigned
	};

	struct ExprValue
	{
		ExprTypeKind type;
		long long s;
		unsigned long long u;

		static ExprValue make_signed(long long v)
		{
			ExprValue x;
			x.type = ExprTypeKind::Signed;
			x.s = v;
			x.u = static_cast<unsigned long long>(v);
			return x;
		}

		static ExprValue make_unsigned(unsigned long long v)
		{
			ExprValue x;
			x.type = ExprTypeKind::Unsigned;
			x.s = static_cast<long long>(v);
			x.u = v;
			return x;
		}
	};

	struct ExprLit
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

	unsigned __int128 expr_parse_u128(const string& s, int base, bool& ok)
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

	ExprValue promote_bool(bool b)
	{
		return ExprValue::make_signed(b ? 1 : 0);
	}

	bool is_truthy(const ExprValue& v)
	{
		return v.type == ExprTypeKind::Unsigned ? v.u != 0 : v.s != 0;
	}

	unsigned long long as_unsigned(const ExprValue& v)
	{
		return v.type == ExprTypeKind::Unsigned ? v.u : static_cast<unsigned long long>(v.s);
	}

	bool result_is_unsigned(const ExprValue& a, const ExprValue& b)
	{
		return a.type == ExprTypeKind::Unsigned || b.type == ExprTypeKind::Unsigned;
	}

	ExprLit expr_parse_literal(const string& src, bool is_char, ExprLit& lit)
	{
		lit = ExprLit();
		size_t p = 0;
		if (src.compare(0, 2, "u8") == 0)
		{
			lit.enc = ExprLit::Enc::U8;
			p = 2;
		}
		else if (!src.empty() && src[0] == 'u')
		{
			lit.enc = ExprLit::Enc::U16;
			p = 1;
		}
		else if (!src.empty() && src[0] == 'U')
		{
			lit.enc = ExprLit::Enc::U32;
			p = 1;
		}
		else if (!src.empty() && src[0] == 'L')
		{
			lit.enc = ExprLit::Enc::Wide;
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

	ExprValue expr_parse_character_literal(const string& src)
	{
		ExprLit lit;
		if (!expr_parse_literal(src, true, lit).ok)
			throw ParseError("character-literal in controlling expression");
		if (lit.has_ud)
			throw ParseError("character-literal in controlling expression");
		if (lit.cps.size() != 1)
			throw ParseError("character-literal in controlling expression");

		uint32_t cp = lit.cps[0];
		if (lit.enc == ExprLit::Enc::Ordinary)
		{
			if (cp <= 127)
			{
				char v = static_cast<char>(cp);
				return ExprValue::make_signed(static_cast<long long>(v));
			}
			return ExprValue::make_signed(static_cast<long long>(cp));
		}
		if (lit.enc == ExprLit::Enc::U16 || lit.enc == ExprLit::Enc::U32)
		{
			if (lit.enc == ExprLit::Enc::U16 && cp > 0xFFFF)
				throw ParseError(string("UTF-16 char literal out of range: ") + src + " " + to_string(cp));
			if (lit.enc == ExprLit::Enc::U32 && cp > 0x10FFFF)
				throw ParseError(string("UTF-32 char literal out of range: ") + src + " " + to_string(cp));
			return ExprValue::make_unsigned(static_cast<unsigned long long>(cp));
		}
		if (cp > 0x10FFFF)
			throw ParseError(string("wchar_t char literal out of range: ") + src + " " + to_string(cp));
		return ExprValue::make_signed(static_cast<long long>(cp));
	}

	ExprValue expr_parse_pp_number(const string& src)
	{
		if (src.find_first_of(".eEpP") != string::npos)
			throw ParseError("floating literal in controlling expression");

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
			throw ParseError("expected identifier or literal in controlling expression");

		bool ok = false;
		unsigned __int128 v = expr_parse_u128(digits, is_hex ? 16 : (is_oct ? 8 : 10), ok);
		if (!ok)
			throw ParseError("expected identifier or literal in controlling expression");

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
				throw ParseError("expected identifier or literal in controlling expression");
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
				throw ParseError("expected identifier or literal in controlling expression");

			if (is_hex || is_oct)
			{
				if (v > static_cast<unsigned __int128>(ULLONG_MAX))
					throw ParseError(string(is_hex ? "hex integer literal out of range (#1): " : "octal integer literal out of range (#2): ") + digits);
				if (u && l)
					return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
				if (u && ll)
					return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
				if (u)
					return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
				if (l)
				{
					if (v <= static_cast<unsigned __int128>(LONG_MAX))
						return ExprValue::make_signed(static_cast<long long>(v));
					if (v <= static_cast<unsigned __int128>(ULONG_MAX))
						return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
					throw ParseError(string(is_hex ? "hex integer literal out of range (#1): " : "octal integer literal out of range (#2): ") + digits);
				}
				if (ll)
				{
					if (v <= static_cast<unsigned __int128>(LLONG_MAX))
						return ExprValue::make_signed(static_cast<long long>(v));
					return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
				}
			}
			else
			{
				if (v > static_cast<unsigned __int128>(ULLONG_MAX))
					throw ParseError("decimal integer literal out of range(#2): " + digits);
				if (u && l)
				{
					if (v <= static_cast<unsigned __int128>(ULONG_MAX))
						return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
					throw ParseError("decimal integer literal out of range(#4): " + digits);
				}
				if (u && ll)
				{
					if (v <= static_cast<unsigned __int128>(ULLONG_MAX))
						return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
					throw ParseError("decimal integer literal out of range(#4): " + digits);
				}
				if (u)
					return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
				if (l)
				{
					if (v <= static_cast<unsigned __int128>(LONG_MAX))
						return ExprValue::make_signed(static_cast<long long>(v));
					throw ParseError("decimal integer literal out of range(#4): " + digits);
				}
				if (ll)
				{
					if (v <= static_cast<unsigned __int128>(LLONG_MAX))
						return ExprValue::make_signed(static_cast<long long>(v));
					throw ParseError("decimal integer literal out of range(#4): " + digits);
				}
			}
		}

		if (is_hex)
		{
			if (v > static_cast<unsigned __int128>(ULLONG_MAX))
				throw ParseError("hex integer literal out of range (#1): " + digits);
			if (v <= static_cast<unsigned __int128>(INT_MAX))
				return ExprValue::make_signed(static_cast<long long>(static_cast<int>(v)));
			if (v <= static_cast<unsigned __int128>(UINT_MAX))
				return ExprValue::make_unsigned(static_cast<unsigned long long>(static_cast<unsigned>(v)));
			if (v <= static_cast<unsigned __int128>(LONG_MAX))
				return ExprValue::make_signed(static_cast<long long>(static_cast<long>(v)));
			if (v <= static_cast<unsigned __int128>(ULONG_MAX))
				return ExprValue::make_unsigned(static_cast<unsigned long long>(static_cast<unsigned long>(v)));
			if (v <= static_cast<unsigned __int128>(LLONG_MAX))
				return ExprValue::make_signed(static_cast<long long>(static_cast<long long>(v)));
			return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
		}

		if (is_oct)
		{
			if (v > static_cast<unsigned __int128>(ULLONG_MAX))
				throw ParseError("octal integer literal out of range (#2): " + digits);
			if (v <= static_cast<unsigned __int128>(INT_MAX))
				return ExprValue::make_signed(static_cast<long long>(static_cast<int>(v)));
			if (v <= static_cast<unsigned __int128>(UINT_MAX))
				return ExprValue::make_unsigned(static_cast<unsigned long long>(static_cast<unsigned>(v)));
			if (v <= static_cast<unsigned __int128>(LONG_MAX))
				return ExprValue::make_signed(static_cast<long long>(static_cast<long>(v)));
			if (v <= static_cast<unsigned __int128>(ULONG_MAX))
				return ExprValue::make_unsigned(static_cast<unsigned long long>(static_cast<unsigned long>(v)));
			if (v <= static_cast<unsigned __int128>(LLONG_MAX))
				return ExprValue::make_signed(static_cast<long long>(static_cast<long long>(v)));
			return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
		}

		if (v <= static_cast<unsigned __int128>(INT_MAX))
			return ExprValue::make_signed(static_cast<long long>(static_cast<int>(v)));
		if (v <= static_cast<unsigned __int128>(LONG_MAX))
			return ExprValue::make_signed(static_cast<long long>(static_cast<long>(v)));
		if (v <= static_cast<unsigned __int128>(LLONG_MAX))
			return ExprValue::make_signed(static_cast<long long>(static_cast<long long>(v)));
		if (v <= static_cast<unsigned __int128>(ULLONG_MAX))
			return ExprValue::make_unsigned(static_cast<unsigned long long>(v));
		throw ParseError("decimal integer literal out of range(#2): " + digits);
	}

	struct ExprNode
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
		ExprValue value;
		string ident;
		unique_ptr<ExprNode> left;
		unique_ptr<ExprNode> middle;
		unique_ptr<ExprNode> right;
	};

	struct ExprParser
	{
		const vector<Token>& toks;
		const unordered_map<string, Macro>& macros;
		size_t pos = 0;

		ExprParser(const vector<Token>& toks, const unordered_map<string, Macro>& macros)
			: toks(toks), macros(macros)
		{
		}

		bool at_end() const
		{
			return pos >= toks.size();
		}

		const Token& peek(size_t off = 0) const
		{
			static Token eof;
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

		const Token& consume()
		{
			if (at_end())
				throw ParseError("expected identifier or literal in controlling expression");
			return toks[pos++];
		}

		unique_ptr<ExprNode> make_value(const ExprValue& v)
		{
			unique_ptr<ExprNode> n(new ExprNode());
			n->kind = ExprNode::Kind::VALUE;
			n->value = v;
			return n;
		}

		unique_ptr<ExprNode> make_unary(const string& op, unique_ptr<ExprNode> child)
		{
			unique_ptr<ExprNode> n(new ExprNode());
			n->kind = ExprNode::Kind::UNARY;
			n->op = op;
			n->left = std::move(child);
			return n;
		}

		unique_ptr<ExprNode> make_binary(const string& op, unique_ptr<ExprNode> lhs, unique_ptr<ExprNode> rhs)
		{
			unique_ptr<ExprNode> n(new ExprNode());
			n->kind = ExprNode::Kind::BINARY;
			n->op = op;
			n->left = std::move(lhs);
			n->right = std::move(rhs);
			return n;
		}

		unique_ptr<ExprNode> make_cond(unique_ptr<ExprNode> cond, unique_ptr<ExprNode> t, unique_ptr<ExprNode> f)
		{
			unique_ptr<ExprNode> n(new ExprNode());
			n->kind = ExprNode::Kind::COND;
			n->left = std::move(cond);
			n->middle = std::move(t);
			n->right = std::move(f);
			return n;
		}

		unique_ptr<ExprNode> make_defined(const string& ident)
		{
			unique_ptr<ExprNode> n(new ExprNode());
			n->kind = ExprNode::Kind::DEFINED;
			n->ident = ident;
			return n;
		}

		unique_ptr<ExprNode> parse()
		{
			unique_ptr<ExprNode> expr = parse_conditional();
			if (!at_end())
				throw ParseError("left over tokens at end of controlling expression");
			return expr;
		}

		unique_ptr<ExprNode> parse_conditional()
		{
			unique_ptr<ExprNode> cond = parse_logical_or();
			if (accept_punc("?"))
			{
				unique_ptr<ExprNode> t = parse_conditional();
				if (!accept_punc(":"))
					throw ParseError("expected identifier or literal in controlling expression");
				unique_ptr<ExprNode> f = parse_conditional();
				return make_cond(std::move(cond), std::move(t), std::move(f));
			}
			return cond;
		}

		unique_ptr<ExprNode> parse_logical_or()
		{
			unique_ptr<ExprNode> lhs = parse_logical_and();
			while (accept_punc("||"))
			{
				unique_ptr<ExprNode> rhs = parse_logical_and();
				lhs = make_binary("||", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_logical_and()
		{
			unique_ptr<ExprNode> lhs = parse_inclusive_or();
			while (accept_punc("&&"))
			{
				unique_ptr<ExprNode> rhs = parse_inclusive_or();
				lhs = make_binary("&&", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_inclusive_or()
		{
			unique_ptr<ExprNode> lhs = parse_exclusive_or();
			while (accept_punc("|"))
			{
				unique_ptr<ExprNode> rhs = parse_exclusive_or();
				lhs = make_binary("|", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_exclusive_or()
		{
			unique_ptr<ExprNode> lhs = parse_and();
			while (accept_punc("^"))
			{
				unique_ptr<ExprNode> rhs = parse_and();
				lhs = make_binary("^", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_and()
		{
			unique_ptr<ExprNode> lhs = parse_equality();
			while (accept_punc("&"))
			{
				unique_ptr<ExprNode> rhs = parse_equality();
				lhs = make_binary("&", std::move(lhs), std::move(rhs));
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_equality()
		{
			unique_ptr<ExprNode> lhs = parse_relational();
			while (true)
			{
				if (accept_punc("=="))
				{
					unique_ptr<ExprNode> rhs = parse_relational();
					lhs = make_binary("==", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("!="))
				{
					unique_ptr<ExprNode> rhs = parse_relational();
					lhs = make_binary("!=", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_relational()
		{
			unique_ptr<ExprNode> lhs = parse_shift();
			while (true)
			{
				if (accept_punc("<"))
				{
					unique_ptr<ExprNode> rhs = parse_shift();
					lhs = make_binary("<", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc(">"))
				{
					unique_ptr<ExprNode> rhs = parse_shift();
					lhs = make_binary(">", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("<="))
				{
					unique_ptr<ExprNode> rhs = parse_shift();
					lhs = make_binary("<=", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc(">="))
				{
					unique_ptr<ExprNode> rhs = parse_shift();
					lhs = make_binary(">=", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_shift()
		{
			unique_ptr<ExprNode> lhs = parse_additive();
			while (true)
			{
				if (accept_punc("<<"))
				{
					unique_ptr<ExprNode> rhs = parse_additive();
					lhs = make_binary("<<", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc(">>"))
				{
					unique_ptr<ExprNode> rhs = parse_additive();
					lhs = make_binary(">>", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_additive()
		{
			unique_ptr<ExprNode> lhs = parse_multiplicative();
			while (true)
			{
				if (accept_punc("+"))
				{
					unique_ptr<ExprNode> rhs = parse_multiplicative();
					lhs = make_binary("+", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("-"))
				{
					unique_ptr<ExprNode> rhs = parse_multiplicative();
					lhs = make_binary("-", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_multiplicative()
		{
			unique_ptr<ExprNode> lhs = parse_unary();
			while (true)
			{
				if (accept_punc("*"))
				{
					unique_ptr<ExprNode> rhs = parse_unary();
					lhs = make_binary("*", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("/"))
				{
					unique_ptr<ExprNode> rhs = parse_unary();
					lhs = make_binary("/", std::move(lhs), std::move(rhs));
				}
				else if (accept_punc("%"))
				{
					unique_ptr<ExprNode> rhs = parse_unary();
					lhs = make_binary("%", std::move(lhs), std::move(rhs));
				}
				else
					break;
			}
			return lhs;
		}

		unique_ptr<ExprNode> parse_unary()
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

		unique_ptr<ExprNode> parse_primary()
		{
			if (accept_punc("("))
			{
				unique_ptr<ExprNode> expr = parse_conditional();
				if (!accept_punc(")"))
					throw ParseError("closing bracket expected in controlling expression");
				return expr;
			}

			if (at_end())
				throw ParseError("expected identifier or literal in controlling expression");

			const Token& t = peek();
			if (t.kind == Kind::IDENT)
			{
				if (t.data == "defined")
					return parse_defined();
				++pos;
				if (t.data == "true")
					return make_value(ExprValue::make_signed(1));
				if (t.data == "false")
					return make_value(ExprValue::make_signed(0));
				return make_value(ExprValue::make_signed(0));
			}
			if (t.kind == Kind::PPNUM)
			{
				++pos;
				return make_value(expr_parse_pp_number(t.data));
			}
			if (t.kind == Kind::CHAR)
			{
				++pos;
				return make_value(expr_parse_character_literal(t.data));
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

		unique_ptr<ExprNode> parse_defined()
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
				unique_ptr<ExprNode> n = make_defined(ident);
				return n;
			}

			if (at_end() || peek().kind != Kind::IDENT)
				throw ParseError("non-identifier after defined in controlling expression");
			string ident = consume().data;
			return make_defined(ident);
		}
	};

	bool type_is_unsigned(const ExprNode& n)
	{
		switch (n.kind)
		{
		case ExprNode::Kind::VALUE:
			return n.value.type == ExprTypeKind::Unsigned;
		case ExprNode::Kind::DEFINED:
			return false;
		case ExprNode::Kind::UNARY:
			return type_is_unsigned(*n.left);
		case ExprNode::Kind::BINARY:
			if (n.op == "<<" || n.op == ">>")
				return type_is_unsigned(*n.left);
			if (n.op == "&&" || n.op == "||" || n.op == "==" || n.op == "!=" || n.op == "<" || n.op == ">" || n.op == "<=" || n.op == ">=")
				return false;
			return type_is_unsigned(*n.left) || type_is_unsigned(*n.right);
		case ExprNode::Kind::COND:
			return type_is_unsigned(*n.middle) || type_is_unsigned(*n.right);
		}
		return false;
	}

	ExprValue eval_expr(const ExprNode& n, const unordered_map<string, Macro>& macros)
	{
		switch (n.kind)
		{
		case ExprNode::Kind::VALUE:
			return n.value;
		case ExprNode::Kind::DEFINED:
			return promote_bool(macros.find(n.ident) != macros.end());
		case ExprNode::Kind::UNARY:
		{
			ExprValue v = eval_expr(*n.left, macros);
			if (n.op == "+")
				return v;
			if (n.op == "-")
			{
				if (v.type == ExprTypeKind::Unsigned)
					return ExprValue::make_unsigned(0ULL - v.u);
				return ExprValue::make_signed(-v.s);
			}
			if (n.op == "!")
				return promote_bool(!is_truthy(v));
			if (n.op == "~")
			{
				if (v.type == ExprTypeKind::Unsigned)
					return ExprValue::make_unsigned(~v.u);
				return ExprValue::make_signed(~v.s);
			}
			throw EvalError();
		}
		case ExprNode::Kind::BINARY:
		{
			if (n.op == "&&")
			{
				ExprValue lhs = eval_expr(*n.left, macros);
				if (!is_truthy(lhs))
					return promote_bool(false);
				return promote_bool(is_truthy(eval_expr(*n.right, macros)));
			}
			if (n.op == "||")
			{
				ExprValue lhs = eval_expr(*n.left, macros);
				if (is_truthy(lhs))
					return promote_bool(true);
				return promote_bool(is_truthy(eval_expr(*n.right, macros)));
			}

			ExprValue lhs = eval_expr(*n.left, macros);
			ExprValue rhs = eval_expr(*n.right, macros);
			bool use_unsigned = result_is_unsigned(lhs, rhs);
			if (n.op == "+")
			{
				if (use_unsigned)
					return ExprValue::make_unsigned(as_unsigned(lhs) + as_unsigned(rhs));
				return ExprValue::make_signed(lhs.s + rhs.s);
			}
			if (n.op == "-")
			{
				if (use_unsigned)
					return ExprValue::make_unsigned(as_unsigned(lhs) - as_unsigned(rhs));
				return ExprValue::make_signed(lhs.s - rhs.s);
			}
			if (n.op == "*")
			{
				if (use_unsigned)
					return ExprValue::make_unsigned(as_unsigned(lhs) * as_unsigned(rhs));
				return ExprValue::make_signed(lhs.s * rhs.s);
			}
			if (n.op == "/")
			{
				if ((rhs.type == ExprTypeKind::Unsigned && rhs.u == 0) || (rhs.type == ExprTypeKind::Signed && rhs.s == 0))
					throw EvalError();
				if (use_unsigned)
					return ExprValue::make_unsigned(as_unsigned(lhs) / as_unsigned(rhs));
				if (lhs.s == LLONG_MIN && rhs.s == -1)
					throw EvalError();
				return ExprValue::make_signed(lhs.s / rhs.s);
			}
			if (n.op == "%")
			{
				if ((rhs.type == ExprTypeKind::Unsigned && rhs.u == 0) || (rhs.type == ExprTypeKind::Signed && rhs.s == 0))
					throw EvalError();
				if (use_unsigned)
					return ExprValue::make_unsigned(as_unsigned(lhs) % as_unsigned(rhs));
				if (lhs.s == LLONG_MIN && rhs.s == -1)
					throw EvalError();
				return ExprValue::make_signed(lhs.s % rhs.s);
			}
			if (n.op == "&")
			{
				if (use_unsigned)
					return ExprValue::make_unsigned(as_unsigned(lhs) & as_unsigned(rhs));
				return ExprValue::make_signed(lhs.s & rhs.s);
			}
			if (n.op == "|")
			{
				if (use_unsigned)
					return ExprValue::make_unsigned(as_unsigned(lhs) | as_unsigned(rhs));
				return ExprValue::make_signed(lhs.s | rhs.s);
			}
			if (n.op == "^")
			{
				if (use_unsigned)
					return ExprValue::make_unsigned(as_unsigned(lhs) ^ as_unsigned(rhs));
				return ExprValue::make_signed(lhs.s ^ rhs.s);
			}
			if (n.op == "<<")
			{
				unsigned long long shift = rhs.type == ExprTypeKind::Unsigned ? rhs.u : static_cast<unsigned long long>(rhs.s);
				if (rhs.type == ExprTypeKind::Signed && rhs.s < 0)
					throw EvalError();
				if (shift >= 64)
					throw EvalError();
				if (lhs.type == ExprTypeKind::Unsigned)
					return ExprValue::make_unsigned(lhs.u << shift);
				unsigned long long bits = static_cast<unsigned long long>(lhs.s);
				bits <<= shift;
				return ExprValue::make_signed(static_cast<long long>(bits));
			}
			if (n.op == ">>")
			{
				unsigned long long shift = rhs.type == ExprTypeKind::Unsigned ? rhs.u : static_cast<unsigned long long>(rhs.s);
				if (rhs.type == ExprTypeKind::Signed && rhs.s < 0)
					throw EvalError();
				if (shift >= 64)
					throw EvalError();
				if (lhs.type == ExprTypeKind::Unsigned)
					return ExprValue::make_unsigned(lhs.u >> shift);
				return ExprValue::make_signed(lhs.s >> shift);
			}
			if (n.op == "==")
				return promote_bool(use_unsigned ? (as_unsigned(lhs) == as_unsigned(rhs)) : (lhs.s == rhs.s));
			if (n.op == "!=")
				return promote_bool(use_unsigned ? (as_unsigned(lhs) != as_unsigned(rhs)) : (lhs.s != rhs.s));
			if (n.op == "<")
				return promote_bool(use_unsigned ? (as_unsigned(lhs) < as_unsigned(rhs)) : (lhs.s < rhs.s));
			if (n.op == ">")
				return promote_bool(use_unsigned ? (as_unsigned(lhs) > as_unsigned(rhs)) : (lhs.s > rhs.s));
			if (n.op == "<=")
				return promote_bool(use_unsigned ? (as_unsigned(lhs) <= as_unsigned(rhs)) : (lhs.s <= rhs.s));
			if (n.op == ">=")
				return promote_bool(use_unsigned ? (as_unsigned(lhs) >= as_unsigned(rhs)) : (lhs.s >= rhs.s));
			throw EvalError();
		}
		case ExprNode::Kind::COND:
		{
			ExprValue cond = eval_expr(*n.left, macros);
			bool use_unsigned = type_is_unsigned(*n.middle) || type_is_unsigned(*n.right);
			if (is_truthy(cond))
			{
				ExprValue t = eval_expr(*n.middle, macros);
				return use_unsigned ? ExprValue::make_unsigned(as_unsigned(t)) : t;
			}
			else
			{
				ExprValue f = eval_expr(*n.right, macros);
				return use_unsigned ? ExprValue::make_unsigned(as_unsigned(f)) : f;
			}
		}
		}
		throw EvalError();
	}

	vector<Token> normalize_expr_tokens(const vector<Token>& toks)
	{
		vector<Token> out;
		for (size_t i = 0; i < toks.size(); ++i)
		{
			if (toks[i].placemarker)
				continue;
			Token t = toks[i];
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

	bool eval_if_expression(const vector<Token>& expr_toks, const unordered_map<string, Macro>& macros)
	{
		vector<Token> toks = normalize_expr_tokens(expr_toks);
		for (size_t i = 0; i < toks.size(); ++i)
		{
			if (toks[i].kind == Kind::IDENT && toks[i].data == "defined")
			{
				size_t j = i + 1;
				if (j < toks.size() && toks[j].kind == Kind::PUNC && toks[j].data == "(")
				{
					++j;
					if (j < toks.size() && toks[j].kind == Kind::IDENT)
						toks[j].hide.insert(toks[j].data);
				}
				else if (j < toks.size() && toks[j].kind == Kind::IDENT)
				{
					toks[j].hide.insert(toks[j].data);
				}
			}
		}
		toks = expand_tokens(toks, macros);
		ExprParser parser(toks, macros);
		unique_ptr<ExprNode> expr = parser.parse();
		ExprValue v = eval_expr(*expr, macros);
		return is_truthy(v);
	}

	// ------------------------------------------------------------------
	// Preprocessor helpers
	// ------------------------------------------------------------------

	bool pp5_is_ws_kind(Kind k)
	{
		return k == Kind::WS;
	}

	bool pp5_is_nl_kind(Kind k)
	{
		return k == Kind::NL || k == Kind::EOFK;
	}

	bool pp5_is_ident(const Token& t)
	{
		return t.kind == Kind::IDENT;
	}

	bool pp5_is_punc(const Token& t, const string& s)
	{
		return t.kind == Kind::PUNC && t.data == s;
	}

	Token make_token(Kind kind, const string& data)
	{
		Token t;
		t.kind = kind;
		t.data = data;
		return t;
	}

	Token pp5_from_raw(const Tok& t)
	{
		Token out = make_token(t.kind, t.data);
		out.line = t.line;
		return out;
	}

	vector<Token> pp5_strip_ws_line(const vector<Tok>& toks, size_t begin, size_t end, bool& is_directive)
	{
		vector<Token> out;
		bool saw_nonws = false;
		bool pending_space = false;
		is_directive = false;
		for (size_t i = begin; i < end; ++i)
		{
			if (pp5_is_ws_kind(toks[i].kind))
			{
				pending_space = true;
				continue;
			}
			Token t = pp5_from_raw(toks[i]);
			if (t.kind == Kind::PUNC && t.data == "%:")
				t.data = "#";
			else if (t.kind == Kind::PUNC && t.data == "%:%:")
				t.data = "##";
			t.space_before = saw_nonws ? pending_space : false;
			pending_space = false;
			if (!saw_nonws && t.kind == Kind::PUNC && t.data == "#")
				is_directive = true;
			saw_nonws = true;
			out.push_back(t);
		}
		return out;
	}

	string quote_string_literal(const string& s)
	{
		string out = "\"";
		for (size_t i = 0; i < s.size(); ++i)
		{
			char c = s[i];
			if (c == '\\' || c == '"')
				out.push_back('\\');
			out.push_back(c);
		}
		out.push_back('"');
		return out;
	}

	Macro make_object_macro(const Token& value)
	{
		Macro m;
		m.function_like = false;
		m.replacement.push_back(value);
		return m;
	}

	Token make_ppnumber_token(const string& value)
	{
		Token t;
		t.kind = Kind::PPNUM;
		t.data = value;
		return t;
	}

	Token make_string_token(const string& value)
	{
		Token t;
		t.kind = Kind::STR;
		t.data = quote_string_literal(value);
		return t;
	}

	string decode_string_literal_token(const Token& t)
	{
		ExprLit lit;
		if (!expr_parse_literal(t.data, false, lit).ok)
			throw runtime_error("expected string-literal");
		string out;
		for (size_t i = 0; i < lit.cps.size(); ++i)
			out += utf8_encode(lit.cps[i]);
		return out;
	}

	string decode_header_name_token(const Token& t)
	{
		if (t.kind != Kind::HEADER)
			throw runtime_error("expected header-name");
		if (t.data.size() >= 2 && t.data[0] == '<' && t.data[t.data.size() - 1] == '>')
			return t.data.substr(1, t.data.size() - 2);
		if (t.data.size() >= 2 && t.data[0] == '"' && t.data[t.data.size() - 1] == '"')
			return decode_string_literal_token(t);
		if (t.data.size() >= 2)
			return t.data.substr(1, t.data.size() - 2);
		return t.data;
	}

	string extract_error_message(const string& s)
	{
		istringstream iss(s);
		string line;
		while (getline(iss, line))
		{
			if (line.compare(0, 7, "ERROR: ") == 0)
				return line.substr(7);
		}
		return string();
	}

	string run_posttoken(const string& source)
	{
		istringstream in(source);
		ostringstream out;
		ostringstream err;
		streambuf* oldcin = cin.rdbuf(in.rdbuf());
		streambuf* oldcout = cout.rdbuf(out.rdbuf());
		streambuf* oldcerr = cerr.rdbuf(err.rdbuf());
		int rc = CPPGM_POSTTOKEN_MAIN();
		cin.rdbuf(oldcin);
		cout.rdbuf(oldcout);
		cerr.rdbuf(oldcerr);
		if (rc != EXIT_SUCCESS)
		{
			string msg = extract_error_message(err.str());
			if (msg.empty())
				msg = "posttoken failed";
			throw runtime_error(msg);
		}
		string output = out.str();
		istringstream lines(output);
		string line;
		while (getline(lines, line))
		{
			if (line.compare(0, 8, "invalid ") == 0)
				throw runtime_error(string("invalid token: ") + line.substr(8));
		}
		return output;
	}

	void install_builtins(State& st, const string& file, size_t line)
	{
		st.macros["__CPPGM__"] = make_object_macro(make_ppnumber_token("201303L"));
		st.macros["__cplusplus"] = make_object_macro(make_ppnumber_token("201103L"));
		st.macros["__STDC_HOSTED__"] = make_object_macro(make_ppnumber_token("1"));
		st.macros["__CPPGM_AUTHOR__"] = make_object_macro(make_string_token(st.author));
		st.macros["__DATE__"] = make_object_macro(make_string_token(st.build_date));
		st.macros["__TIME__"] = make_object_macro(make_string_token(st.build_time));
		st.macros["__FILE__"] = make_object_macro(make_string_token(file));
		st.macros["__LINE__"] = make_object_macro(make_ppnumber_token(to_string(line)));
	}

	string stringify_error_tail(const vector<Token>& toks)
	{
		return stringify_arg(toks);
	}

	void append_block(string& out, const string& block)
	{
		if (block.empty())
			return;
		if (!out.empty())
			out.push_back('\n');
		out += block;
	}

	string expand_text_sequence(const vector<Token>& line, State& st, bool& pragma_once)
	{
		vector<Token> expanded = expand_tokens(line, st.macros);
		vector<Token> filtered;
		for (size_t i = 0; i < expanded.size(); ++i)
		{
			if (expanded[i].placemarker)
				continue;
			if (expanded[i].kind == Kind::IDENT && expanded[i].data == "_Pragma")
			{
				if (i + 3 >= expanded.size() || !pp5_is_punc(expanded[i + 1], "(") || (expanded[i + 2].kind != Kind::STR && expanded[i + 2].kind != Kind::USTR) || !pp5_is_punc(expanded[i + 3], ")"))
					throw runtime_error("expected _Pragma(string-literal)");
				string pragma_text = decode_string_literal_token(expanded[i + 2]);
				if (pragma_text == "once")
					pragma_once = true;
				i += 3;
				continue;
			}
			filtered.push_back(expanded[i]);
		}
		return serialize_segment(filtered);
	}

	string resolve_include_path(const string& current_file, const string& nextf)
	{
		string pathrel;
		size_t slash = current_file.rfind('/');
		if (slash != string::npos)
			pathrel = current_file.substr(0, slash + 1) + nextf;

		if (!pathrel.empty())
		{
			ifstream test(pathrel.c_str());
			if (test.good())
				return pathrel;
		}

		ifstream test(nextf.c_str());
		if (test.good())
			return nextf;

		throw runtime_error(string("include file not found: ") + nextf);
	}

	string normalize_include_target(const Token& t)
	{
		if (t.kind == Kind::HEADER)
			return decode_header_name_token(t);
		if (t.kind == Kind::STR || t.kind == Kind::USTR)
			return decode_string_literal_token(t);
		throw runtime_error("expected header-name or string-literal");
	}

	string process_file(State& st, const string& actual_path, const string& logical_name)
	{
		PA5FileId fileid;
		bool have_fileid = PA5GetFileId(actual_path, fileid);
		if (have_fileid && st.once_files.find(fileid) != st.once_files.end())
			return string();

		ifstream in(actual_path.c_str());
		if (!in.good())
			throw runtime_error(string("include file not found: ") + actual_path);

		string input((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());

		Sink sink;
		PPTokenizer tokenizer(sink);
		for (size_t i = 0; i < input.size(); ++i)
			tokenizer.process(static_cast<unsigned char>(input[i]));
		tokenizer.process(EndOfFile);

		string expanded_source;
		vector<CondFrame> conds;
		string current_file = logical_name;
		long long line_bias = 0;
		vector<Token> pending_tokens;
		size_t pending_line = 1;
		bool pending_needs_space = false;

		auto flush_pending = [&]()
		{
			if (pending_tokens.empty())
				return;
			install_builtins(st, current_file, pending_line);
			bool pragma_once = false;
			string block = expand_text_sequence(pending_tokens, st, pragma_once);
			if (pragma_once && have_fileid)
				st.once_files.insert(fileid);
			append_block(expanded_source, block);
			pending_tokens.clear();
			pending_needs_space = false;
		};

		auto append_pending_line = [&](const vector<Token>& line, size_t logical_line)
		{
			if (line.empty())
			{
				if (!pending_tokens.empty())
					pending_needs_space = true;
				return;
			}
			vector<Token> copy = line;
			if (!pending_tokens.empty() || pending_needs_space)
				copy[0].space_before = true;
			pending_tokens.insert(pending_tokens.end(), copy.begin(), copy.end());
			pending_line = logical_line;
			pending_needs_space = true;
		};

		for (size_t i = 0; i < sink.toks.size(); )
		{
			size_t j = i;
			while (j < sink.toks.size() && !pp5_is_nl_kind(sink.toks[j].kind))
				++j;

			bool is_directive = false;
			vector<Token> line = pp5_strip_ws_line(sink.toks, i, j, is_directive);
			bool current_active = conds.empty() ? true : conds.back().active;

			size_t physical_line = line.empty() ? 0 : line.back().line;
			size_t current_line = physical_line == 0 ? 1 : static_cast<size_t>(static_cast<long long>(physical_line) + line_bias);

			if (is_directive)
			{
				flush_pending();
				install_builtins(st, current_file, current_line);

				if (!line.empty() && line[0].kind == Kind::PUNC && line[0].data == "%:")
					line[0].data = "#";

				if (line.size() == 1)
				{
					// null directive
				}
				else if (line.size() >= 2 && pp5_is_ident(line[1]))
				{
					const string& directive = line[1].data;
					if (directive == "if" || directive == "ifdef" || directive == "ifndef")
					{
						CondFrame frame;
						frame.parent_active = current_active;
						frame.saw_else = false;
						if (!current_active)
						{
							frame.active = false;
							frame.branch_taken = false;
							conds.push_back(frame);
						}
						else if (directive == "if")
						{
							vector<Token> expr(line.begin() + 2, line.end());
							bool cond = eval_if_expression(expr, st.macros);
							frame.active = cond;
							frame.branch_taken = cond;
							conds.push_back(frame);
						}
						else if (directive == "ifdef")
						{
							if (line.size() < 3 || !pp5_is_ident(line[2]))
								throw runtime_error("expected identifier");
							bool cond = st.macros.find(line[2].data) != st.macros.end();
							frame.active = cond;
							frame.branch_taken = cond;
							conds.push_back(frame);
						}
						else
						{
							if (line.size() < 3 || !pp5_is_ident(line[2]))
								throw runtime_error("expected identifier");
							bool cond = st.macros.find(line[2].data) == st.macros.end();
							frame.active = cond;
							frame.branch_taken = cond;
							conds.push_back(frame);
						}
					}
					else if (directive == "elif")
					{
						if (conds.empty() || conds.back().saw_else)
							throw runtime_error("unexpected #elif");
						CondFrame& frame = conds.back();
						if (!frame.parent_active || frame.branch_taken)
						{
							frame.active = false;
						}
						else
						{
							vector<Token> expr(line.begin() + 2, line.end());
							bool cond = eval_if_expression(expr, st.macros);
							frame.active = cond;
							frame.branch_taken = cond;
						}
					}
					else if (directive == "else")
					{
						if (conds.empty() || conds.back().saw_else)
							throw runtime_error("unexpected #else");
						CondFrame& frame = conds.back();
						frame.saw_else = true;
						if (!frame.parent_active || frame.branch_taken)
							frame.active = false;
						else
						{
							frame.active = true;
							frame.branch_taken = true;
						}
					}
					else if (directive == "endif")
					{
						if (conds.empty())
							throw runtime_error("unexpected #endif");
						conds.pop_back();
					}
					else if (current_active && directive == "define")
					{
						string name;
						Macro m = parse_define(line, name);
						unordered_map<string, Macro>::iterator it = st.macros.find(name);
						if (it != st.macros.end())
						{
							if (!same_macro(it->second, m))
								throw runtime_error("macro redefined");
						}
						else
						{
							st.macros[name] = m;
						}
					}
					else if (current_active && directive == "undef")
					{
						if (line.size() < 3 || !pp5_is_ident(line[2]))
							throw runtime_error("#undef expected id");
						if (line[2].data == "__VA_ARGS__")
							throw runtime_error("invalid __VA_ARGS__ use");
						if (line.size() > 3)
							throw runtime_error("#undef expected id");
						st.macros.erase(line[2].data);
					}
					else if (current_active && directive == "include")
					{
						if (line.size() < 3)
							throw runtime_error("expected header-name or string-literal");
						vector<Token> arg(line.begin() + 2, line.end());
						vector<Token> expanded = expand_tokens(arg, st.macros);
						if (expanded.size() != 1)
							throw runtime_error("expected header-name or string-literal");
						string inc_target = normalize_include_target(expanded[0]);
						string resolved = resolve_include_path(current_file, inc_target);
						string included = process_file(st, resolved, resolved);
						append_block(expanded_source, included);
					}
					else if (current_active && directive == "line")
					{
						vector<Token> arg(line.begin() + 2, line.end());
						vector<Token> expanded = expand_tokens(arg, st.macros);
						if (expanded.empty() || expanded[0].kind != Kind::PPNUM)
							throw runtime_error("expected ppnumber");
						char* endp = 0;
						unsigned long long new_line = strtoull(expanded[0].data.c_str(), &endp, 10);
						if (endp == 0 || *endp != '\0' || new_line == 0)
							throw runtime_error("expected ppnumber");
						if (expanded.size() > 1)
						{
							if (expanded.size() != 2 || (expanded[1].kind != Kind::STR && expanded[1].kind != Kind::USTR))
								throw runtime_error("expected string-literal");
							current_file = decode_string_literal_token(expanded[1]);
						}
						if (physical_line != 0)
							line_bias = static_cast<long long>(new_line) - static_cast<long long>(physical_line) - 1;
					}
					else if (current_active && directive == "error")
					{
						vector<Token> tail(line.begin() + 2, line.end());
						throw runtime_error(string("#error ") + stringify_error_tail(tail));
					}
					else if (current_active && directive == "pragma")
					{
						if (line.size() >= 3 && pp5_is_ident(line[2]) && line[2].data == "once")
						{
							if (have_fileid)
								st.once_files.insert(fileid);
						}
					}
					else if (current_active)
					{
						throw runtime_error(string("active non-directive found: ") + serialize_segment(vector<Token>(line.begin() + 1, line.end())));
					}
				}
				else if (current_active)
				{
					throw runtime_error(string("active non-directive found: ") + serialize_segment(vector<Token>(line.begin() + 1, line.end())));
				}
			}
			else if (current_active)
			{
				append_pending_line(line, current_line);
			}

			if (j < sink.toks.size() && sink.toks[j].kind == Kind::EOFK)
				break;
			i = j + 1;
		}

		flush_pending();

		if (!conds.empty())
			throw runtime_error("include completed in bad group state (maybe unterminated #if)");

		return expanded_source;
	}

	void write_section(ostream& out, const string& srcfile, const string& source)
	{
		out << "sof " << srcfile << '\n';
		out << run_posttoken(source);
	}
} // namespace pa5preproc

#ifndef CPPGM_PREPROC_LIBRARY
int main(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int i = 1; i < argc; ++i)
			args.emplace_back(argv[i]);

		if (args.size() < 3 || args[0] != "-o")
			throw logic_error("invalid usage");

		string outfile = args[1];
		size_t nsrcfiles = args.size() - 2;

		time_t now = time(0);
		tm* tmv = localtime(&now);
		char date_buf[32];
		char time_buf[32];
		strftime(date_buf, sizeof(date_buf), "%b %e %Y", tmv);
		strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tmv);

		pa5preproc::State base_state;
		base_state.build_date = date_buf;
		base_state.build_time = time_buf;
		const char* author = getenv("CPPGM_AUTHOR");
		if (author != 0 && *author != '\0')
			base_state.author = author;
		else
			base_state.author = "Jesse Andrews";

		ofstream out(outfile.c_str());
		out << "preproc " << nsrcfiles << '\n';

		for (size_t i = 0; i < nsrcfiles; ++i)
		{
			string srcfile = args[i + 2];
			pa5preproc::State st = base_state;
			string expanded = pa5preproc::process_file(st, srcfile, srcfile);
			pa5preproc::write_section(out, srcfile, expanded);
		}

		return EXIT_SUCCESS;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
