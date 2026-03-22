// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <utility>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <algorithm>
#include <functional>
#include <ctime>
#include <cstdint>

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

struct PA5Engine;
static PA5Engine* g_pa5_defined_engine = nullptr;
bool PA5IsDefinedIdentifier(const string& identifier);

#define CPPGM_POSTTOKEN_NO_MAIN
#include "posttoken.cpp"
#include "PA5CtrlExprEval.h"

enum MacroLexKind
{
	MLK_WS,
	MLK_NL,
	MLK_PP,
	MLK_PLACEMARKER
};

struct MacroToken
{
	MacroLexKind lex_kind;
	EPPTokenKind pp_kind;
	string source;
	set<string> blacklist;
	bool noninvokable;
	bool paste_op;
	bool from_arg_original;
	string file;
	int line;
};

MacroToken MakeWS()
{
	MacroToken t;
	t.lex_kind = MLK_WS;
	t.pp_kind = PPTOK_NON_WHITESPACE_CHAR;
	t.source.clear();
	t.noninvokable = false;
	t.paste_op = false;
	t.from_arg_original = false;
	t.file.clear();
	t.line = 0;
	return t;
}

MacroToken MakeNL()
{
	MacroToken t = MakeWS();
	t.lex_kind = MLK_NL;
	return t;
}

MacroToken MakePP(EPPTokenKind kind, const string& source)
{
	MacroToken t;
	t.lex_kind = MLK_PP;
	t.pp_kind = kind;
	t.source = source;
	t.noninvokable = false;
	t.paste_op = false;
	t.from_arg_original = false;
	t.file.clear();
	t.line = 0;
	return t;
}

MacroToken MakePlacemaker()
{
	MacroToken t = MakeWS();
	t.lex_kind = MLK_PLACEMARKER;
	return t;
}

bool IsWS(const MacroToken& t)
{
	return t.lex_kind == MLK_WS || t.lex_kind == MLK_NL;
}

bool IsPP(const MacroToken& t)
{
	return t.lex_kind == MLK_PP;
}

bool IsPlacemaker(const MacroToken& t)
{
	return t.lex_kind == MLK_PLACEMARKER;
}

bool IsIdentifierTok(const MacroToken& t)
{
	return IsPP(t) && t.pp_kind == PPTOK_IDENTIFIER;
}

bool IsIdentifierTok(const MacroToken& t, const string& name)
{
	return IsIdentifierTok(t) && t.source == name;
}

bool IsOpTok(const MacroToken& t, const string& op)
{
	return IsPP(t) && t.pp_kind == PPTOK_PREPROCESSING_OP_OR_PUNC && t.source == op;
}

bool IsHashTok(const MacroToken& t)
{
	return IsOpTok(t, "#") || IsOpTok(t, "%:");
}

bool IsHashHashTok(const MacroToken& t)
{
	return IsOpTok(t, "##") || IsOpTok(t, "%:%:");
}

size_t NextNonWS(const vector<MacroToken>& v, size_t i)
{
	while (i < v.size() && IsWS(v[i])) i++;
	return i;
}

size_t PrevNonWS(const vector<MacroToken>& v, size_t i)
{
	while (i > 0)
	{
		i--;
		if (!IsWS(v[i])) return i;
	}
	return static_cast<size_t>(-1);
}

vector<MacroToken> TrimWS(const vector<MacroToken>& in)
{
	size_t b = 0;
	while (b < in.size() && IsWS(in[b])) b++;
	size_t e = in.size();
	while (e > b && IsWS(in[e - 1])) e--;
	return vector<MacroToken>(in.begin() + static_cast<long>(b), in.begin() + static_cast<long>(e));
}

vector<MacroToken> CollapseWS(const vector<MacroToken>& in)
{
	vector<MacroToken> out;
	bool prev_ws = false;
	for (size_t i = 0; i < in.size(); i++)
	{
		const MacroToken& t = in[i];
		if (IsWS(t))
		{
			if (!prev_ws)
			{
				MacroToken w = MakeWS();
				w.file = t.file;
				w.line = t.line;
				out.push_back(w);
				prev_ws = true;
			}
			continue;
		}
		out.push_back(t);
		prev_ws = false;
	}
	return TrimWS(out);
}

string EscapeForStringLiteral(const string& s)
{
	string out;
	for (size_t i = 0; i < s.size(); i++)
	{
		char c = s[i];
		if (c == '\\' || c == '"') out.push_back('\\');
		out.push_back(c);
	}
	return out;
}

bool ShouldEscapeTokenWhenStringizing(const MacroToken& t)
{
	if (!IsPP(t)) return false;
	return
		t.pp_kind == PPTOK_CHARACTER_LITERAL ||
		t.pp_kind == PPTOK_USER_DEFINED_CHARACTER_LITERAL ||
		t.pp_kind == PPTOK_STRING_LITERAL ||
		t.pp_kind == PPTOK_USER_DEFINED_STRING_LITERAL;
}

string StringizeArgument(const vector<MacroToken>& arg)
{
	string body;
	bool want_space = false;
	for (size_t i = 0; i < arg.size(); i++)
	{
		if (IsWS(arg[i]))
		{
			if (!body.empty()) want_space = true;
			continue;
		}
		if (!IsPP(arg[i]) && !IsPlacemaker(arg[i])) continue;
		if (IsPlacemaker(arg[i])) continue;
		if (want_space)
		{
			body.push_back(' ');
			want_space = false;
		}
		if (ShouldEscapeTokenWhenStringizing(arg[i])) body += EscapeForStringLiteral(arg[i].source);
		else body += arg[i].source;
	}
	return string("\"") + body + string("\"");
}

struct CollectMacroPPTokenStream : IPPTokenStream
{
	int* cur_line;
	vector<MacroToken> tokens;

	CollectMacroPPTokenStream(int* cur_line)
		: cur_line(cur_line)
	{}

	void push(MacroToken t)
	{
		t.line = *cur_line;
		tokens.push_back(t);
	}

	void emit_whitespace_sequence() { push(MakeWS()); }
	void emit_new_line() { push(MakeNL()); }
	void emit_header_name(const string& data) { push(MakePP(PPTOK_HEADER_NAME, data)); }
	void emit_identifier(const string& data) { push(MakePP(PPTOK_IDENTIFIER, data)); }
	void emit_pp_number(const string& data) { push(MakePP(PPTOK_PP_NUMBER, data)); }
	void emit_character_literal(const string& data) { push(MakePP(PPTOK_CHARACTER_LITERAL, data)); }
	void emit_user_defined_character_literal(const string& data) { push(MakePP(PPTOK_USER_DEFINED_CHARACTER_LITERAL, data)); }
	void emit_string_literal(const string& data) { push(MakePP(PPTOK_STRING_LITERAL, data)); }
	void emit_user_defined_string_literal(const string& data) { push(MakePP(PPTOK_USER_DEFINED_STRING_LITERAL, data)); }
	void emit_preprocessing_op_or_punc(const string& data) { push(MakePP(PPTOK_PREPROCESSING_OP_OR_PUNC, data)); }
	void emit_non_whitespace_char(const string& data) { push(MakePP(PPTOK_NON_WHITESPACE_CHAR, data)); }
	void emit_eof() {}
};

struct MacroDef
{
	string name;
	bool function_like;
	bool variadic;
	vector<string> params;
	vector<MacroToken> replacement;
};

bool EquivalentReplacement(const vector<MacroToken>& a, const vector<MacroToken>& b)
{
	vector<MacroToken> na = CollapseWS(a);
	vector<MacroToken> nb = CollapseWS(b);
	if (na.size() != nb.size()) return false;
	for (size_t i = 0; i < na.size(); i++)
	{
		if (na[i].lex_kind != nb[i].lex_kind) return false;
		if (na[i].lex_kind == MLK_WS || na[i].lex_kind == MLK_NL) continue;
		if (na[i].lex_kind == MLK_PLACEMARKER || nb[i].lex_kind == MLK_PLACEMARKER) return false;
		if (na[i].pp_kind != nb[i].pp_kind) return false;
		if (na[i].source != nb[i].source) return false;
	}
	return true;
}

string TokenDescForRParenError(bool at_eol)
{
	if (at_eol) return "PP_NEW_LINE()";
	return "token";
}

struct IfFrame
{
	bool parent_active;
	bool this_active;
	bool any_taken;
	bool seen_else;
};

struct PA5Engine
{
	map<string, MacroDef> macros;
	set<PA5FileId> pragma_once_ids;
	string build_date_lit;
	string build_time_lit;
	string author_lit;

	PA5Engine(const string& build_date_lit, const string& build_time_lit)
		: build_date_lit(build_date_lit), build_time_lit(build_time_lit), author_lit("\"John Smith\"")
	{
		InitFixedPredefinedMacros();
	}

	void InitFixedPredefinedMacros()
	{
		AddObjectLikeMacro("__CPPGM__", vector<MacroToken>{MakePP(PPTOK_PP_NUMBER, "201303L")});
		AddObjectLikeMacro("__cplusplus", vector<MacroToken>{MakePP(PPTOK_PP_NUMBER, "201103L")});
		AddObjectLikeMacro("__STDC_HOSTED__", vector<MacroToken>{MakePP(PPTOK_PP_NUMBER, "1")});
		AddObjectLikeMacro("__CPPGM_AUTHOR__", vector<MacroToken>{MakePP(PPTOK_STRING_LITERAL, author_lit)});
		AddObjectLikeMacro("__DATE__", vector<MacroToken>{MakePP(PPTOK_STRING_LITERAL, build_date_lit)});
		AddObjectLikeMacro("__TIME__", vector<MacroToken>{MakePP(PPTOK_STRING_LITERAL, build_time_lit)});
	}

	void AddObjectLikeMacro(const string& name, const vector<MacroToken>& repl)
	{
		MacroDef d;
		d.name = name;
		d.function_like = false;
		d.variadic = false;
		d.replacement = repl;
		macros[name] = d;
	}

	bool IsDefinedIdentifier(const string& identifier) const
	{
		if (identifier == "__FILE__" || identifier == "__LINE__") return true;
		return macros.find(identifier) != macros.end();
	}

	vector<MacroToken> Tokenize(const string& s)
	{
		int cur_line = 1;
		CollectMacroPPTokenStream out(&cur_line);
		PPTokenizer tokenizer(out);
		for (size_t i = 0; i < s.size(); i++)
		{
			tokenizer.process(static_cast<unsigned char>(s[i]));
			if (s[i] == '\n') cur_line++;
		}
		tokenizer.process(EndOfFile);
		return out.tokens;
	}

	MacroToken RetokenizeOne(const MacroToken& left, const MacroToken& right)
	{
		string joined = left.source + right.source;
		vector<MacroToken> toks = Tokenize(joined);
		vector<MacroToken> non_ws;
		for (size_t i = 0; i < toks.size(); i++)
		{
			if (!IsWS(toks[i])) non_ws.push_back(toks[i]);
		}
		if (non_ws.size() == 1)
		{
			MacroToken t = non_ws[0];
			t.blacklist = left.blacklist;
			t.blacklist.insert(right.blacklist.begin(), right.blacklist.end());
			t.noninvokable = false;
			t.paste_op = false;
			t.from_arg_original = false;
			t.file = left.file;
			t.line = left.line;
			return t;
		}
		MacroToken bad = MakePP(PPTOK_NON_WHITESPACE_CHAR, joined);
		bad.blacklist = left.blacklist;
		bad.blacklist.insert(right.blacklist.begin(), right.blacklist.end());
		bad.noninvokable = false;
		bad.from_arg_original = false;
		bad.file = left.file;
		bad.line = left.line;
		return bad;
	}

	void EnsureNoInvalidVaArgsInText(const vector<MacroToken>& toks)
	{
		for (size_t i = 0; i < toks.size(); i++)
		{
			if (IsIdentifierTok(toks[i], "__VA_ARGS__"))
			{
				throw runtime_error("__VA_ARGS__ token in text-lines: __VA_ARGS__");
			}
		}
	}

	bool MacroEquals(const MacroDef& a, const MacroDef& b)
	{
		if (a.function_like != b.function_like) return false;
		if (a.variadic != b.variadic) return false;
		if (a.params != b.params) return false;
		return EquivalentReplacement(a.replacement, b.replacement);
	}

	bool ParamExists(const MacroDef& def, const string& name)
	{
		for (size_t i = 0; i < def.params.size(); i++)
			if (def.params[i] == name) return true;
		if (def.variadic && name == "__VA_ARGS__") return true;
		return false;
	}

	void ValidateReplacement(const MacroDef& def)
	{
		size_t first_non_ws = NextNonWS(def.replacement, 0);
		if (first_non_ws < def.replacement.size() && IsHashHashTok(def.replacement[first_non_ws]))
		{
			throw runtime_error("## at edge of replacement list");
		}
		size_t last_non_ws = PrevNonWS(def.replacement, def.replacement.size());
		if (last_non_ws != static_cast<size_t>(-1) && IsHashHashTok(def.replacement[last_non_ws]))
		{
			throw runtime_error("## at edge of replacement list");
		}

		for (size_t i = 0; i < def.replacement.size(); i++)
		{
			const MacroToken& t = def.replacement[i];
			if (IsIdentifierTok(t, "__VA_ARGS__") && !def.variadic)
			{
				throw runtime_error("invalid __VA_ARGS__ use");
			}

			if (!def.function_like) continue;
			if (!IsHashTok(t)) continue;

			size_t n = NextNonWS(def.replacement, i + 1);
			if (n >= def.replacement.size())
			{
				throw runtime_error("# at end of function-like macro replacement list");
			}
			if (!IsIdentifierTok(def.replacement[n]) || !ParamExists(def, def.replacement[n].source))
			{
				throw runtime_error("# must be followed by parameter in function-like macro");
			}
		}
	}

	void ParseDefine(const vector<MacroToken>& line, size_t pos)
	{
		pos = NextNonWS(line, pos);
		if (pos >= line.size() || !IsIdentifierTok(line[pos]))
		{
			throw runtime_error("expected identifier");
		}

		MacroDef def;
		def.name = line[pos].source;
		def.function_like = false;
		def.variadic = false;
		def.params.clear();
		def.replacement.clear();

		if (def.name == "__VA_ARGS__")
		{
			throw runtime_error("invalid __VA_ARGS__ use");
		}

		size_t name_idx = pos;
		if (name_idx + 1 < line.size() && IsOpTok(line[name_idx + 1], "("))
		{
			def.function_like = true;
			size_t p = name_idx + 2;
			p = NextNonWS(line, p);

			if (p >= line.size())
			{
				throw runtime_error("expected identifier after lparen");
			}

			if (IsOpTok(line[p], ")"))
			{
				p++;
			}
			else if (IsOpTok(line[p], "..."))
			{
				def.variadic = true;
				p++;
				p = NextNonWS(line, p);
				if (p >= line.size() || !IsOpTok(line[p], ")"))
				{
					throw runtime_error(string("expected rparen (#2): ") + TokenDescForRParenError(p >= line.size()));
				}
				p++;
			}
			else
			{
				bool first_param = true;
				while (true)
				{
					if (p >= line.size())
					{
						throw runtime_error(first_param ? "expected identifier after lparen" : "expected identifier");
					}
					if (!IsIdentifierTok(line[p]))
					{
						throw runtime_error(first_param ? "expected identifier after lparen" : "expected identifier");
					}

					string param = line[p].source;
					if (param == "__VA_ARGS__")
					{
						throw runtime_error("__VA_ARGS__ in macro parameter list");
					}
					if (find(def.params.begin(), def.params.end(), param) != def.params.end())
					{
						throw runtime_error(string("duplicate parameter ") + param + string(" in macro definition"));
					}
					def.params.push_back(param);
					p++;
					p = NextNonWS(line, p);

					if (p >= line.size())
					{
						throw runtime_error(string("expected rparen (#2): ") + TokenDescForRParenError(true));
					}

					if (IsOpTok(line[p], ")"))
					{
						p++;
						break;
					}
					if (!IsOpTok(line[p], ","))
					{
						throw runtime_error(string("expected rparen (#2): ") + TokenDescForRParenError(false));
					}
					p++;
					p = NextNonWS(line, p);
					if (p >= line.size())
					{
						throw runtime_error("expected identifier");
					}
					if (IsOpTok(line[p], "..."))
					{
						def.variadic = true;
						p++;
						p = NextNonWS(line, p);
						if (p >= line.size() || !IsOpTok(line[p], ")"))
						{
							throw runtime_error(string("expected rparen (#2): ") + TokenDescForRParenError(p >= line.size()));
						}
						p++;
						break;
					}
					first_param = false;
				}
			}

			def.replacement.assign(line.begin() + static_cast<long>(p), line.end());
		}
		else
		{
			def.replacement.assign(line.begin() + static_cast<long>(name_idx + 1), line.end());
		}

		ValidateReplacement(def);

		auto it = macros.find(def.name);
		if (it != macros.end())
		{
			if (!MacroEquals(it->second, def))
			{
				throw runtime_error("macro redefined");
			}
			return;
		}
		macros[def.name] = def;
	}

	void ParseUndef(const vector<MacroToken>& line, size_t pos)
	{
		pos = NextNonWS(line, pos);
		if (pos >= line.size() || !IsIdentifierTok(line[pos]))
		{
			throw runtime_error("expected identifier");
		}
		string name = line[pos].source;
		if (name == "__VA_ARGS__")
		{
			throw runtime_error("invalid __VA_ARGS__ use");
		}

		pos++;
		pos = NextNonWS(line, pos);
		if (pos != line.size())
		{
			throw runtime_error("expected new line");
		}

		macros.erase(name);
	}

	bool ParseInvocationArguments(const vector<MacroToken>& seq, size_t lparen_idx, const MacroDef& def,
		vector<vector<MacroToken>>& args, size_t& end_idx)
	{
		args.clear();
		vector<vector<MacroToken>> split;
		vector<MacroToken> cur;
		int depth = 0;
		bool saw_comma = false;
		bool saw_non_ws = false;

		size_t p = lparen_idx + 1;
		for (; p < seq.size(); p++)
		{
			const MacroToken& t = seq[p];
			if (IsOpTok(t, "("))
			{
				depth++;
				cur.push_back(t);
				saw_non_ws = true;
				continue;
			}
			if (IsOpTok(t, ")"))
			{
				if (depth == 0)
				{
					end_idx = p + 1;
					break;
				}
				depth--;
				cur.push_back(t);
				saw_non_ws = true;
				continue;
			}
			if (depth == 0 && IsOpTok(t, ","))
			{
				split.push_back(TrimWS(cur));
				cur.clear();
				saw_comma = true;
				continue;
			}
			cur.push_back(t);
			if (!IsWS(t)) saw_non_ws = true;
		}

		if (p >= seq.size())
		{
			return false;
		}

		if (!saw_comma && !saw_non_ws)
		{
			args.clear();
		}
		else
		{
			split.push_back(TrimWS(cur));
			args = split;
		}

		if (!def.variadic)
		{
			if (def.params.empty())
			{
				if (!args.empty())
				{
					throw runtime_error(string("macro function-like invocation wrong num of params: ") + def.name);
				}
			}
			else
			{
				if (args.empty()) args.push_back(vector<MacroToken>());
				if (args.size() != def.params.size())
				{
					throw runtime_error(string("macro function-like invocation wrong num of params: ") + def.name);
				}
			}
		}
		else
		{
			if (args.empty() && !def.params.empty()) args.push_back(vector<MacroToken>());
			if (args.size() < def.params.size())
			{
				throw runtime_error(string("macro function-like invocation wrong num of params: ") + def.name);
			}
		}

		return true;
	}

	void MarkCurrentMacroNameAsUnavailableInArgument(vector<MacroToken>& toks, const MacroDef& def)
	{
		if (!def.function_like) return;
		for (size_t i = 0; i < toks.size(); i++)
		{
			if (!IsIdentifierTok(toks[i], def.name)) continue;
			size_t n = NextNonWS(toks, i + 1);
			if (n < toks.size() && IsOpTok(toks[n], "(")) continue;
			toks[i].noninvokable = true;
		}
	}

	void MarkAsOriginalArgumentTokens(vector<MacroToken>& toks)
	{
		for (size_t i = 0; i < toks.size(); i++)
		{
			if (IsPP(toks[i])) toks[i].from_arg_original = true;
		}
	}

	MacroToken MakeLineToken(const MacroToken& origin)
	{
		MacroToken t = MakePP(PPTOK_PP_NUMBER, to_string(origin.line));
		t.file = origin.file;
		t.line = origin.line;
		t.blacklist = origin.blacklist;
		return t;
	}

	MacroToken MakeFileToken(const MacroToken& origin)
	{
		MacroToken t = MakePP(PPTOK_STRING_LITERAL, string("\"") + EscapeForStringLiteral(origin.file) + string("\""));
		t.file = origin.file;
		t.line = origin.line;
		t.blacklist = origin.blacklist;
		return t;
	}

	void ExpandTokens(vector<MacroToken>& seq)
	{
		seq = CollapseWS(seq);

		size_t i = 0;
		while (i < seq.size())
		{
			if (IsWS(seq[i]) || !IsIdentifierTok(seq[i]))
			{
				i++;
				continue;
			}

			string name = seq[i].source;

			if (name == "__LINE__" || name == "__FILE__")
			{
				if (seq[i].noninvokable || seq[i].blacklist.count(name))
				{
					seq[i].noninvokable = true;
					i++;
					continue;
				}
				if (name == "__LINE__") seq[i] = MakeLineToken(seq[i]);
				else seq[i] = MakeFileToken(seq[i]);
				i++;
				continue;
			}

			auto it = macros.find(name);
			if (it == macros.end())
			{
				i++;
				continue;
			}

			if (seq[i].noninvokable || seq[i].blacklist.count(name))
			{
				seq[i].noninvokable = true;
				i++;
				continue;
			}

			const MacroDef& def = it->second;
			size_t invocation_end = i + 1;
			vector<vector<MacroToken>> args;

			if (def.function_like)
			{
				size_t p = i + 1;
				p = NextNonWS(seq, p);
				if (p >= seq.size() || !IsOpTok(seq[p], "("))
				{
					i++;
					continue;
				}
				if (!ParseInvocationArguments(seq, p, def, args, invocation_end))
				{
					i++;
					continue;
				}
			}

			map<string, vector<MacroToken>> arg_raw;
			map<string, vector<MacroToken>> arg_expanded;
			if (def.function_like)
			{
				for (size_t pi = 0; pi < def.params.size(); pi++)
				{
					vector<MacroToken> a = (pi < args.size()) ? args[pi] : vector<MacroToken>();
					MarkAsOriginalArgumentTokens(a);
					arg_raw[def.params[pi]] = a;
				}

				vector<MacroToken> va;
				if (def.variadic)
				{
					if (args.size() > def.params.size())
					{
						for (size_t ai = def.params.size(); ai < args.size(); ai++)
						{
							vector<MacroToken> a = args[ai];
							MarkAsOriginalArgumentTokens(a);
							if (!va.empty())
							{
								MacroToken comma = MakePP(PPTOK_PREPROCESSING_OP_OR_PUNC, ",");
								comma.file = seq[i].file;
								comma.line = seq[i].line;
								va.push_back(comma);
								MacroToken w = MakeWS();
								w.file = seq[i].file;
								w.line = seq[i].line;
								va.push_back(w);
							}
							va.insert(va.end(), a.begin(), a.end());
						}
					}
					arg_raw["__VA_ARGS__"] = va;
				}

				set<string> need_expanded;
				for (size_t ri = 0; ri < def.replacement.size(); ri++)
				{
					const MacroToken& rt = def.replacement[ri];
					if (!IsIdentifierTok(rt) || !ParamExists(def, rt.source)) continue;

					size_t prev = PrevNonWS(def.replacement, ri);
					size_t next = NextNonWS(def.replacement, ri + 1);
					bool adjacent_hashhash =
						(prev != static_cast<size_t>(-1) && IsHashHashTok(def.replacement[prev])) ||
						(next < def.replacement.size() && IsHashHashTok(def.replacement[next]));
					bool preceded_by_hash =
						(prev != static_cast<size_t>(-1) && IsHashTok(def.replacement[prev]));

					if (!adjacent_hashhash && !preceded_by_hash)
					{
						need_expanded.insert(rt.source);
					}
				}

				for (set<string>::const_iterator itneed = need_expanded.begin(); itneed != need_expanded.end(); ++itneed)
				{
					vector<MacroToken> expanded = arg_raw[*itneed];
					MarkCurrentMacroNameAsUnavailableInArgument(expanded, def);
					ExpandTokens(expanded);
					arg_expanded[*itneed] = expanded;
				}
			}

			set<string> inherited = seq[i].blacklist;
			inherited.insert(def.name);

			vector<MacroToken> repl;
			for (size_t ri = 0; ri < def.replacement.size();)
			{
				const MacroToken& rt = def.replacement[ri];

				if (def.function_like && IsHashTok(rt))
				{
					size_t pn = NextNonWS(def.replacement, ri + 1);
					string pname = def.replacement[pn].source;
					MacroToken st = MakePP(PPTOK_STRING_LITERAL, StringizeArgument(arg_raw[pname]));
					st.blacklist = inherited;
					st.file = seq[i].file;
					st.line = seq[i].line;
					repl.push_back(st);
					ri = pn + 1;
					continue;
				}

				if (IsHashHashTok(rt))
				{
					MacroToken op = MakePP(PPTOK_PREPROCESSING_OP_OR_PUNC, "##");
					op.blacklist = inherited;
					op.paste_op = true;
					op.file = seq[i].file;
					op.line = seq[i].line;
					repl.push_back(op);
					ri++;
					continue;
				}

				if (def.function_like && IsIdentifierTok(rt) && ParamExists(def, rt.source))
				{
					size_t prev = PrevNonWS(def.replacement, ri);
					size_t next = NextNonWS(def.replacement, ri + 1);
					bool adjacent_hashhash =
						(prev != static_cast<size_t>(-1) && IsHashHashTok(def.replacement[prev])) ||
						(next < def.replacement.size() && IsHashHashTok(def.replacement[next]));

					const vector<MacroToken>& sub = adjacent_hashhash ? arg_raw[rt.source] : arg_expanded[rt.source];
					if (sub.empty() && adjacent_hashhash)
					{
						MacroToken pm = MakePlacemaker();
						pm.blacklist = inherited;
						pm.file = seq[i].file;
						pm.line = seq[i].line;
						repl.push_back(pm);
					}
					else
					{
						for (size_t si = 0; si < sub.size(); si++)
						{
							MacroToken st = sub[si];
							if (st.from_arg_original)
							{
								st.blacklist.insert(inherited.begin(), inherited.end());
								st.from_arg_original = false;
							}
							repl.push_back(st);
						}
					}
					ri++;
					continue;
				}

				MacroToken cp = rt;
				if (IsPP(cp))
				{
					cp.blacklist = inherited;
					cp.noninvokable = false;
					cp.paste_op = false;
					cp.from_arg_original = false;
					cp.file = seq[i].file;
					cp.line = seq[i].line;
				}
				repl.push_back(cp);
				ri++;
			}

			while (true)
			{
				size_t opi = static_cast<size_t>(-1);
				for (size_t k = 0; k < repl.size(); k++)
				{
					if (IsPP(repl[k]) && repl[k].paste_op)
					{
						opi = k;
						break;
					}
				}
				if (opi == static_cast<size_t>(-1)) break;

				size_t li = PrevNonWS(repl, opi);
				size_t ri = NextNonWS(repl, opi + 1);
				if (li == static_cast<size_t>(-1) || ri >= repl.size())
				{
					throw runtime_error("## at edge of replacement list");
				}

				MacroToken joined;
				if (IsPlacemaker(repl[li]) && IsPlacemaker(repl[ri]))
				{
					joined = MakePlacemaker();
					joined.blacklist = repl[li].blacklist;
					joined.blacklist.insert(repl[ri].blacklist.begin(), repl[ri].blacklist.end());
					joined.file = repl[li].file;
					joined.line = repl[li].line;
				}
				else if (IsPlacemaker(repl[li]))
				{
					joined = repl[ri];
					joined.paste_op = false;
				}
				else if (IsPlacemaker(repl[ri]))
				{
					joined = repl[li];
					joined.paste_op = false;
				}
				else
				{
					joined = RetokenizeOne(repl[li], repl[ri]);
				}

				vector<MacroToken> next;
				next.insert(next.end(), repl.begin(), repl.begin() + static_cast<long>(li));
				next.push_back(joined);
				next.insert(next.end(), repl.begin() + static_cast<long>(ri + 1), repl.end());
				repl.swap(next);
			}

			vector<MacroToken> repl2;
			for (size_t k = 0; k < repl.size(); k++)
			{
				if (IsPlacemaker(repl[k])) continue;
				if (IsPP(repl[k])) repl[k].paste_op = false;
				repl2.push_back(repl[k]);
			}
			repl.swap(repl2);

			vector<MacroToken> next;
			next.insert(next.end(), seq.begin(), seq.begin() + static_cast<long>(i));
			next.insert(next.end(), repl.begin(), repl.end());
			next.insert(next.end(), seq.begin() + static_cast<long>(invocation_end), seq.end());
			seq.swap(next);
			seq = CollapseWS(seq);
			if (i > 0) i--; else i = 0;
		}
	}

	void MarkDefinedOperands(vector<MacroToken>& expr)
	{
		for (size_t i = 0; i < expr.size(); i++)
		{
			if (!IsIdentifierTok(expr[i], "defined")) continue;
			size_t j = NextNonWS(expr, i + 1);
			if (j >= expr.size()) continue;
			if (IsOpTok(expr[j], "("))
			{
				size_t k = NextNonWS(expr, j + 1);
				if (k < expr.size() && IsIdentifierTok(expr[k])) expr[k].noninvokable = true;
			}
			else if (IsIdentifierTok(expr[j]))
			{
				expr[j].noninvokable = true;
			}
		}
	}

	bool EvaluateIfExpression(vector<MacroToken> expr)
	{
		MarkDefinedOperands(expr);
		ExpandTokens(expr);
		expr = CollapseWS(expr);

		vector<PPToken> pp;
		for (size_t i = 0; i < expr.size(); i++)
		{
			if (!IsPP(expr[i])) continue;
			pp.push_back({expr[i].pp_kind, expr[i].source});
		}

		Parser parser(pp);
		unique_ptr<Node> root = parser.parse_controlling();
		if (parser.failed || !root || parser.peek() != nullptr)
		{
			throw runtime_error("if expression parse error");
		}

		g_pa5_defined_engine = this;
		EvalResult r = Eval(root.get());
		g_pa5_defined_engine = nullptr;
		if (!r.ok)
		{
			throw runtime_error("if expression eval error");
		}
		return ToBool(r.value);
	}

	string DecodeStringLiteralToUTF8Path(const MacroToken& tok)
	{
		if (!IsPP(tok)) throw runtime_error("bad include string token");
		if (tok.pp_kind != PPTOK_STRING_LITERAL) throw runtime_error("bad include string token");

		ParsedStringPiece p = ParseStringPiece(tok.source, false);
		if (!p.ok || p.has_ud) throw runtime_error("bad include string literal");
		if (p.encoding != SE_ORDINARY) throw runtime_error("bad include string literal");
		return UTF8Encode(p.codepoints);
	}

	string DecodeHeaderNameToPath(const MacroToken& tok)
	{
		if (!IsPP(tok) || tok.pp_kind != PPTOK_HEADER_NAME) throw runtime_error("bad include header-name");
		if (tok.source.size() < 2) throw runtime_error("bad include header-name");
		char a = tok.source.front();
		char b = tok.source.back();
		if (!((a == '<' && b == '>') || (a == '"' && b == '"')))
			throw runtime_error("bad include header-name");
		return tok.source.substr(1, tok.source.size() - 2);
	}

	bool FileExists(const string& path)
	{
		ifstream in(path.c_str(), ios::binary);
		return in.good();
	}

	string ResolveIncludePath(const string& nextf, const string& cur_file)
	{
		size_t slash = cur_file.find_last_of('/');
		if (slash != string::npos)
		{
			string pathrel = cur_file.substr(0, slash + 1) + nextf;
			if (FileExists(pathrel)) return pathrel;
		}
		if (FileExists(nextf)) return nextf;
		return "";
	}

	void ApplyPragmaOnce(const string& cur_file)
	{
		PA5FileId id;
		if (PA5GetFileId(cur_file, id))
		{
			pragma_once_ids.insert(id);
		}
	}

	void ExecutePragmaTokens(const vector<MacroToken>& toks, const string& cur_file)
	{
		size_t p = NextNonWS(toks, 0);
		if (p >= toks.size()) return;
		if (!IsIdentifierTok(toks[p])) return;
		string pragma_name = toks[p].source;

		if (pragma_name == "once")
		{
			ApplyPragmaOnce(cur_file);
			return;
		}

		// course-defined unknown pragma is ignored
	}

	void ExecutePragmaString(const MacroToken& str_tok, const string& cur_file)
	{
		ParsedStringPiece p = ParseStringPiece(str_tok.source, false);
		if (!p.ok || p.has_ud) throw runtime_error("bad _Pragma string");
		string pragma_text = UTF8Encode(p.codepoints);
		vector<MacroToken> toks = Tokenize(pragma_text);
		for (size_t i = 0; i < toks.size(); i++)
		{
			toks[i].file = cur_file;
			toks[i].line = str_tok.line;
		}
		ExecutePragmaTokens(toks, cur_file);
	}

	void ProcessPragmaOperators(vector<MacroToken>& seq)
	{
		size_t i = 0;
		while (i < seq.size())
		{
			if (!IsIdentifierTok(seq[i], "_Pragma"))
			{
				i++;
				continue;
			}

			size_t p1 = NextNonWS(seq, i + 1);
			size_t p2 = (p1 < seq.size() ? NextNonWS(seq, p1 + 1) : p1);
			size_t p3 = (p2 < seq.size() ? NextNonWS(seq, p2 + 1) : p2);
			if (p1 >= seq.size() || !IsOpTok(seq[p1], "(") ||
				p2 >= seq.size() || !IsPP(seq[p2]) || seq[p2].pp_kind != PPTOK_STRING_LITERAL ||
				p3 >= seq.size() || !IsOpTok(seq[p3], ")"))
			{
				throw runtime_error("bad _Pragma operator usage");
			}

			ExecutePragmaString(seq[p2], seq[i].file);

			vector<MacroToken> next;
			next.insert(next.end(), seq.begin(), seq.begin() + static_cast<long>(i));
			next.insert(next.end(), seq.begin() + static_cast<long>(p3 + 1), seq.end());
			seq.swap(next);
		}
	}

	void FlushTextSequence(vector<MacroToken>& buf, vector<PPToken>& out_pp)
	{
		if (buf.empty()) return;
		for (size_t i = 0; i < buf.size(); i++)
		{
			if (buf[i].lex_kind == MLK_NL)
			{
				MacroToken w = MakeWS();
				w.file = buf[i].file;
				w.line = buf[i].line;
				buf[i] = w;
			}
		}
		buf = CollapseWS(buf);
		ExpandTokens(buf);
		ProcessPragmaOperators(buf);
		EnsureNoInvalidVaArgsInText(buf);
		for (size_t i = 0; i < buf.size(); i++)
		{
			if (!IsPP(buf[i])) continue;
			out_pp.push_back({buf[i].pp_kind, buf[i].source});
		}
		buf.clear();
	}

	void HandleIncludeDirective(const vector<MacroToken>& line, size_t pos, const string& cur_file, vector<PPToken>& out_pp)
	{
		vector<MacroToken> arg(line.begin() + static_cast<long>(pos), line.end());
		arg = CollapseWS(arg);
		ExpandTokens(arg);
		arg = CollapseWS(arg);

		vector<MacroToken> nonws;
		for (size_t i = 0; i < arg.size(); i++)
			if (!IsWS(arg[i])) nonws.push_back(arg[i]);

		if (nonws.size() != 1)
			throw runtime_error("bad include tokens");

		string nextf;
		if (nonws[0].pp_kind == PPTOK_HEADER_NAME)
		{
			nextf = DecodeHeaderNameToPath(nonws[0]);
		}
		else if (nonws[0].pp_kind == PPTOK_STRING_LITERAL)
		{
			nextf = DecodeStringLiteralToUTF8Path(nonws[0]);
		}
		else
		{
			throw runtime_error("bad include tokens");
		}

		string include_path = ResolveIncludePath(nextf, cur_file);
		if (include_path.empty())
			throw runtime_error("include file not found");

		PA5FileId id;
		if (PA5GetFileId(include_path, id) && pragma_once_ids.count(id))
			return;

		ProcessFile(include_path, include_path, out_pp);
	}

	void HandleLineDirective(const vector<MacroToken>& line, size_t pos, bool& set_line, int& new_line, bool& set_file, string& new_file)
	{
		vector<MacroToken> arg(line.begin() + static_cast<long>(pos), line.end());
		arg = CollapseWS(arg);
		ExpandTokens(arg);
		arg = CollapseWS(arg);

		vector<MacroToken> nonws;
		for (size_t i = 0; i < arg.size(); i++)
			if (!IsWS(arg[i])) nonws.push_back(arg[i]);

		if (nonws.size() < 1 || nonws.size() > 2)
			throw runtime_error("bad #line");

		Value v;
		if (!(nonws[0].pp_kind == PPTOK_PP_NUMBER && ParseIntegralPPNumber(nonws[0].source, v)))
			throw runtime_error("bad #line number");

		int64_t n = v.is_unsigned ? static_cast<int64_t>(AsUnsigned(v)) : AsSigned(v);
		if (n <= 0)
			throw runtime_error("bad #line number");

		set_line = true;
		new_line = static_cast<int>(n);
		set_file = false;

		if (nonws.size() == 2)
		{
			if (nonws[1].pp_kind != PPTOK_STRING_LITERAL)
				throw runtime_error("bad #line file");
			ParsedStringPiece p = ParseStringPiece(nonws[1].source, false);
			if (!p.ok || p.has_ud || p.encoding != SE_ORDINARY)
				throw runtime_error("bad #line file");
			set_file = true;
			new_file = UTF8Encode(p.codepoints);
		}
	}

	void HandlePragmaDirective(const vector<MacroToken>& line, size_t pos, const string& cur_file)
	{
		vector<MacroToken> arg(line.begin() + static_cast<long>(pos), line.end());
		ExecutePragmaTokens(arg, cur_file);
	}

	bool CurrentActive(const vector<IfFrame>& st)
	{
		return st.empty() ? true : st.back().this_active;
	}

	void HandleIfStart(const string& kw, const vector<MacroToken>& line, size_t pos, vector<IfFrame>& ifstack)
	{
		bool parent = CurrentActive(ifstack);
		IfFrame f;
		f.parent_active = parent;
		f.this_active = false;
		f.any_taken = false;
		f.seen_else = false;

		if (!parent)
		{
			ifstack.push_back(f);
			return;
		}

		bool cond = false;
		if (kw == "if")
		{
			vector<MacroToken> expr(line.begin() + static_cast<long>(pos), line.end());
			cond = EvaluateIfExpression(expr);
		}
		else
		{
			pos = NextNonWS(line, pos);
			if (pos >= line.size() || !IsIdentifierTok(line[pos]))
				throw runtime_error("expected identifier");
			string id = line[pos].source;
			bool defined = IsDefinedIdentifier(id);
			cond = (kw == "ifdef") ? defined : !defined;
		}

		f.this_active = cond;
		f.any_taken = cond;
		ifstack.push_back(f);
	}

	void HandleIfMiddle(const string& kw, const vector<MacroToken>& line, size_t pos, vector<IfFrame>& ifstack)
	{
		if (ifstack.empty()) throw runtime_error("if-stack underflow");
		IfFrame& f = ifstack.back();

		if (kw == "elif")
		{
			if (f.seen_else) throw runtime_error("elif after else");
			if (!f.parent_active)
			{
				f.this_active = false;
				return;
			}
			if (f.any_taken)
			{
				f.this_active = false;
				return;
			}
			vector<MacroToken> expr(line.begin() + static_cast<long>(pos), line.end());
			bool cond = EvaluateIfExpression(expr);
			f.this_active = cond;
			if (cond) f.any_taken = true;
			return;
		}

		if (kw == "else")
		{
			if (f.seen_else) throw runtime_error("duplicate else");
			f.seen_else = true;
			if (!f.parent_active) f.this_active = false;
			else f.this_active = !f.any_taken;
			f.any_taken = true;
			return;
		}

		if (kw == "endif")
		{
			ifstack.pop_back();
			return;
		}
	}

	void HandleDirectiveLine(const vector<MacroToken>& line, vector<IfFrame>& ifstack,
		string& cur_file, bool& set_line, int& new_line, bool& set_file, string& new_file,
		vector<PPToken>& out_pp)
	{
		size_t p = NextNonWS(line, 0);
		if (p >= line.size() || !IsHashTok(line[p])) return;
		p++;
		p = NextNonWS(line, p);
		if (p >= line.size())
		{
			return; // null directive
		}

		bool active = CurrentActive(ifstack);
		if (!IsIdentifierTok(line[p]))
		{
			if (active) throw runtime_error("non-directive in active section");
			return;
		}

		string kw = line[p].source;
		p++;

		if (kw == "if" || kw == "ifdef" || kw == "ifndef")
		{
			HandleIfStart(kw, line, p, ifstack);
			return;
		}
		if (kw == "elif" || kw == "else" || kw == "endif")
		{
			HandleIfMiddle(kw, line, p, ifstack);
			return;
		}

		if (!active)
		{
			return;
		}

		if (kw == "define")
		{
			ParseDefine(line, p);
			return;
		}
		if (kw == "undef")
		{
			ParseUndef(line, p);
			return;
		}
		if (kw == "include")
		{
			HandleIncludeDirective(line, p, cur_file, out_pp);
			return;
		}
		if (kw == "line")
		{
			HandleLineDirective(line, p, set_line, new_line, set_file, new_file);
			return;
		}
		if (kw == "error")
		{
			throw runtime_error("#error");
		}
		if (kw == "pragma")
		{
			HandlePragmaDirective(line, p, cur_file);
			return;
		}

		throw runtime_error("non-directive in active section");
	}

	void ProcessFile(const string& actual_path, const string& presumed_file, vector<PPToken>& out_pp)
	{
		ifstream in(actual_path.c_str(), ios::binary);
		if (!in)
			throw runtime_error("cannot open source file");
		ostringstream oss;
		oss << in.rdbuf();
		string input = oss.str();

		vector<MacroToken> toks = Tokenize(input);
		vector<int> logical_starts = ComputeLogicalLineStarts(input);

		vector<MacroToken> text_buf;
		vector<IfFrame> ifstack;
		string cur_file = presumed_file;
		int line_delta = 0;
		size_t logical_line_index = 0;
		bool pending_line_reset = false;
		int pending_line_value = 0;
		bool pending_file_reset = false;
		string pending_file_value;

		size_t i = 0;
		while (i < toks.size())
		{
			size_t j = i;
			while (j < toks.size() && toks[j].lex_kind != MLK_NL) j++;
			int logical_start_phys = (logical_line_index < logical_starts.size()) ? logical_starts[logical_line_index] : 1;

			if (pending_line_reset)
			{
				line_delta = pending_line_value - logical_start_phys;
				pending_line_reset = false;
			}
			if (pending_file_reset)
			{
				cur_file = pending_file_value;
				pending_file_reset = false;
			}

			vector<MacroToken> line(toks.begin() + static_cast<long>(i), toks.begin() + static_cast<long>(j));
			for (size_t k = 0; k < line.size(); k++)
			{
				line[k].file = cur_file;
				line[k].line = logical_start_phys + line_delta;
			}

			size_t k = NextNonWS(line, 0);
			bool is_directive = (k < line.size() && IsHashTok(line[k]));

			bool set_line = false;
			int new_line = 0;
			bool set_file = false;
			string new_file;

			if (is_directive)
			{
				FlushTextSequence(text_buf, out_pp);
				HandleDirectiveLine(line, ifstack, cur_file, set_line, new_line, set_file, new_file, out_pp);
			}
			else
			{
				if (CurrentActive(ifstack))
				{
					text_buf.insert(text_buf.end(), line.begin(), line.end());
					if (j < toks.size())
					{
						MacroToken w = MakeWS();
						w.file = cur_file;
						w.line = logical_start_phys + line_delta;
						text_buf.push_back(w);
					}
				}
			}

			if (set_line)
			{
				pending_line_reset = true;
				pending_line_value = new_line;
			}
			if (set_file)
			{
				pending_file_reset = true;
				pending_file_value = new_file;
			}
			logical_line_index++;

			i = (j < toks.size()) ? (j + 1) : j;
		}

		if (!ifstack.empty())
			throw runtime_error("unterminated if-group");

		FlushTextSequence(text_buf, out_pp);
	}

	vector<int> ComputeLogicalLineStarts(const string& input)
	{
		enum ScanState
		{
			SS_NORMAL,
			SS_LINE_COMMENT,
			SS_BLOCK_COMMENT,
			SS_STRING,
			SS_CHAR
		};

		vector<int> starts;
		starts.push_back(1);

		ScanState st = SS_NORMAL;
		int physical_line = 1;
		size_t i = 0;
		while (i < input.size())
		{
			// phase 2 line-splice
			if (i + 1 < input.size() && input[i] == '\\' && input[i + 1] == '\n')
			{
				i += 2;
				physical_line++;
				continue;
			}

			char c = input[i];
			switch (st)
			{
			case SS_NORMAL:
				if (c == '/' && i + 1 < input.size() && input[i + 1] == '/')
				{
					st = SS_LINE_COMMENT;
					i += 2;
					continue;
				}
				if (c == '/' && i + 1 < input.size() && input[i + 1] == '*')
				{
					st = SS_BLOCK_COMMENT;
					i += 2;
					continue;
				}
				if (c == '"')
				{
					st = SS_STRING;
					i++;
					continue;
				}
				if (c == '\'')
				{
					st = SS_CHAR;
					i++;
					continue;
				}
				if (c == '\n')
				{
					physical_line++;
					starts.push_back(physical_line);
					i++;
					continue;
				}
				i++;
				continue;

			case SS_LINE_COMMENT:
				if (c == '\n')
				{
					st = SS_NORMAL;
					physical_line++;
					starts.push_back(physical_line);
					i++;
					continue;
				}
				i++;
				continue;

			case SS_BLOCK_COMMENT:
				if (c == '*' && i + 1 < input.size() && input[i + 1] == '/')
				{
					st = SS_NORMAL;
					i += 2;
					continue;
				}
				if (c == '\n')
				{
					physical_line++;
					i++;
					continue;
				}
				i++;
				continue;

			case SS_STRING:
				if (c == '\\' && i + 1 < input.size())
				{
					i += 2;
					continue;
				}
				if (c == '"')
				{
					st = SS_NORMAL;
					i++;
					continue;
				}
				if (c == '\n')
				{
					physical_line++;
					starts.push_back(physical_line);
					i++;
					continue;
				}
				i++;
				continue;

			case SS_CHAR:
				if (c == '\\' && i + 1 < input.size())
				{
					i += 2;
					continue;
				}
				if (c == '\'')
				{
					st = SS_NORMAL;
					i++;
					continue;
				}
				if (c == '\n')
				{
					physical_line++;
					starts.push_back(physical_line);
					i++;
					continue;
				}
				i++;
				continue;
			}
		}

		return starts;
	}
};

bool PA5IsDefinedIdentifier(const string& identifier)
{
	if (!g_pa5_defined_engine) return false;
	return g_pa5_defined_engine->IsDefinedIdentifier(identifier);
}

string BuildDateLiteralFromAsctime(const string& asctime_s)
{
	if (asctime_s.size() < 24) return "\"Jan 01 1970\"";
	string mmm = asctime_s.substr(4, 3);
	string dd = asctime_s.substr(8, 2);
	string yyyy = asctime_s.substr(20, 4);
	return string("\"") + mmm + string(" ") + dd + string(" ") + yyyy + string("\"");
}

string BuildTimeLiteralFromAsctime(const string& asctime_s)
{
	if (asctime_s.size() < 19) return "\"00:00:00\"";
	string hhmmss = asctime_s.substr(11, 8);
	return string("\"") + hhmmss + string("\"");
}

string EmitOneCaptured(const function<void(DebugPostTokenOutputStream&)>& fn)
{
	ostringstream cap;
	streambuf* old = cout.rdbuf(cap.rdbuf());
	try
	{
		DebugPostTokenOutputStream output;
		fn(output);
		cout.rdbuf(old);
	}
	catch (...)
	{
		cout.rdbuf(old);
		throw;
	}
	return cap.str();
}

void EmitPostTokensOrThrow(ostream& out, const vector<PPToken>& toks)
{
	size_t i = 0;
	while (i < toks.size())
	{
		if (toks[i].kind == PPTOK_STRING_LITERAL || toks[i].kind == PPTOK_USER_DEFINED_STRING_LITERAL)
		{
			size_t j = i;
			while (j < toks.size() &&
				(toks[j].kind == PPTOK_STRING_LITERAL || toks[j].kind == PPTOK_USER_DEFINED_STRING_LITERAL))
			{
				j++;
			}

			string s = EmitOneCaptured([&](DebugPostTokenOutputStream& output){
				ProcessStringRun(output, toks, i, j);
			});
			if (s.rfind("invalid ", 0) == 0) throw runtime_error("invalid token in phase 7");
			out << s;
			i = j;
			continue;
		}

		string s = EmitOneCaptured([&](DebugPostTokenOutputStream& output){
			ProcessOneToken(output, toks[i]);
		});
		if (s.rfind("invalid ", 0) == 0) throw runtime_error("invalid token in phase 7");
		out << s;
		i++;
	}
}

#ifndef CPPGM_PREPROC_NO_MAIN
int main(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int i = 1; i < argc; i++) args.push_back(argv[i]);

		if (args.size() < 3 || args[0] != "-o")
			throw logic_error("invalid usage");

		string outfile = args[1];
		vector<string> srcfiles(args.begin() + 2, args.end());

		time_t now = time(nullptr);
		string now_s = asctime(localtime(&now));
		string date_lit = BuildDateLiteralFromAsctime(now_s);
		string time_lit = BuildTimeLiteralFromAsctime(now_s);

		ofstream out(outfile.c_str(), ios::binary);
		out << "preproc " << srcfiles.size() << "\n";

		for (size_t si = 0; si < srcfiles.size(); si++)
		{
			const string& src = srcfiles[si];
			out << "sof " << src << "\n";

			PA5Engine engine(date_lit, time_lit);
			vector<PPToken> preprocessed;
			engine.ProcessFile(src, src, preprocessed);

			EmitPostTokensOrThrow(out, preprocessed);
			out << "eof\n";
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
