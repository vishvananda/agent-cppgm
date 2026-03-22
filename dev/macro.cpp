// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>

using namespace std;

#define CPPGM_POSTTOKEN_NO_MAIN
#include "posttoken.cpp"

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
				out.push_back(MakeWS());
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
	vector<MacroToken> tokens;

	void emit_whitespace_sequence() { tokens.push_back(MakeWS()); }
	void emit_new_line() { tokens.push_back(MakeNL()); }
	void emit_header_name(const string& data) { tokens.push_back(MakePP(PPTOK_HEADER_NAME, data)); }
	void emit_identifier(const string& data) { tokens.push_back(MakePP(PPTOK_IDENTIFIER, data)); }
	void emit_pp_number(const string& data) { tokens.push_back(MakePP(PPTOK_PP_NUMBER, data)); }
	void emit_character_literal(const string& data) { tokens.push_back(MakePP(PPTOK_CHARACTER_LITERAL, data)); }
	void emit_user_defined_character_literal(const string& data) { tokens.push_back(MakePP(PPTOK_USER_DEFINED_CHARACTER_LITERAL, data)); }
	void emit_string_literal(const string& data) { tokens.push_back(MakePP(PPTOK_STRING_LITERAL, data)); }
	void emit_user_defined_string_literal(const string& data) { tokens.push_back(MakePP(PPTOK_USER_DEFINED_STRING_LITERAL, data)); }
	void emit_preprocessing_op_or_punc(const string& data) { tokens.push_back(MakePP(PPTOK_PREPROCESSING_OP_OR_PUNC, data)); }
	void emit_non_whitespace_char(const string& data) { tokens.push_back(MakePP(PPTOK_NON_WHITESPACE_CHAR, data)); }
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

struct MacroProcessor
{
	map<string, MacroDef> macros;
	vector<MacroToken> final_tokens;

	vector<MacroToken> Tokenize(const string& s)
	{
		CollectMacroPPTokenStream out;
		PPTokenizer tokenizer(out);
		for (size_t i = 0; i < s.size(); i++)
			tokenizer.process(static_cast<unsigned char>(s[i]));
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
			return t;
		}
		MacroToken bad = MakePP(PPTOK_NON_WHITESPACE_CHAR, joined);
		bad.blacklist = left.blacklist;
		bad.blacklist.insert(right.blacklist.begin(), right.blacklist.end());
		bad.noninvokable = false;
		bad.from_arg_original = false;
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

	void ParseDirectiveLine(const vector<MacroToken>& line)
	{
		size_t p = 0;
		p = NextNonWS(line, p);
		if (p >= line.size() || !IsHashTok(line[p])) return;
		p++;
		p = NextNonWS(line, p);
		if (p >= line.size() || !IsIdentifierTok(line[p]))
		{
			throw runtime_error("expected identifier");
		}
		string kw = line[p].source;
		p++;
		if (kw == "define")
		{
			ParseDefine(line, p);
		}
		else if (kw == "undef")
		{
			ParseUndef(line, p);
		}
		else
		{
			throw runtime_error("expected identifier");
		}
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
			if (IsOpTok(t, "(") )
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
								va.push_back(MakePP(PPTOK_PREPROCESSING_OP_OR_PUNC, ","));
								va.push_back(MakeWS());
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
					repl.push_back(st);
					ri = pn + 1;
					continue;
				}

				if (IsHashHashTok(rt))
				{
					MacroToken op = MakePP(PPTOK_PREPROCESSING_OP_OR_PUNC, "##");
					op.blacklist = inherited;
					op.paste_op = true;
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

	void FlushTextSequence(vector<MacroToken>& buf)
	{
		if (buf.empty()) return;
		for (size_t i = 0; i < buf.size(); i++)
		{
			if (buf[i].lex_kind == MLK_NL) buf[i] = MakeWS();
		}
		buf = CollapseWS(buf);
		ExpandTokens(buf);
		EnsureNoInvalidVaArgsInText(buf);
		for (size_t i = 0; i < buf.size(); i++)
		{
			if (!IsWS(buf[i])) final_tokens.push_back(buf[i]);
		}
		buf.clear();
	}

	void Run(const string& input)
	{
		vector<MacroToken> toks = Tokenize(input);
		vector<MacroToken> text_buf;

		size_t i = 0;
		while (i < toks.size())
		{
			size_t j = i;
			while (j < toks.size() && toks[j].lex_kind != MLK_NL) j++;

			vector<MacroToken> line(toks.begin() + static_cast<long>(i), toks.begin() + static_cast<long>(j));
			size_t k = NextNonWS(line, 0);
			bool is_directive = (k < line.size() && IsHashTok(line[k]));

			if (is_directive)
			{
				FlushTextSequence(text_buf);
				ParseDirectiveLine(line);
			}
			else
			{
				text_buf.insert(text_buf.end(), line.begin(), line.end());
				if (j < toks.size()) text_buf.push_back(MakeWS());
			}

			i = (j < toks.size()) ? (j + 1) : j;
		}

		FlushTextSequence(text_buf);
	}
};

int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		string input = oss.str();

		MacroProcessor mp;
		mp.Run(input);

		vector<PPToken> out_tokens;
		for (size_t i = 0; i < mp.final_tokens.size(); i++)
		{
			if (!IsPP(mp.final_tokens[i])) continue;
			PPToken t;
			t.kind = mp.final_tokens[i].pp_kind;
			t.source = mp.final_tokens[i].source;
			out_tokens.push_back(t);
		}

		DebugPostTokenOutputStream output;
		size_t i = 0;
		while (i < out_tokens.size())
		{
			if (out_tokens[i].kind == PPTOK_STRING_LITERAL ||
				out_tokens[i].kind == PPTOK_USER_DEFINED_STRING_LITERAL)
			{
				size_t j = i;
				while (j < out_tokens.size() &&
					(out_tokens[j].kind == PPTOK_STRING_LITERAL ||
					 out_tokens[j].kind == PPTOK_USER_DEFINED_STRING_LITERAL))
				{
					j++;
				}
				ProcessStringRun(output, out_tokens, i, j);
				i = j;
				continue;
			}

			ProcessOneToken(output, out_tokens[i]);
			i++;
		}

		output.emit_eof();
		return EXIT_SUCCESS;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
