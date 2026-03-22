// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#ifndef CPPGM_POSTTOKEN_INTERNAL_MAIN
#define CPPGM_POSTTOKEN_INTERNAL_MAIN preproc_posttoken_internal_main
#endif
#ifndef CPPGM_MACRO_MAIN_NAME
#define CPPGM_MACRO_MAIN_NAME macro_internal_main
#endif
#include "macro.cpp"
#undef CPPGM_MACRO_MAIN_NAME
#undef CPPGM_POSTTOKEN_INTERNAL_MAIN

#include <utility>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <ctime>

using namespace std;

typedef pair<unsigned long int, unsigned long int> PA5FileId;

extern "C" long int syscall(long int n, ...) throw ();

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

struct ScopedCoutRedirect
{
	streambuf* old;

	explicit ScopedCoutRedirect(streambuf* next) : old(cout.rdbuf(next)) {}
	~ScopedCoutRedirect() { cout.rdbuf(old); }
};

struct ConditionalFrame
{
	bool parent_active;
	bool active;
	bool branch_taken;
	bool seen_else;
};

long long ParsePPInteger(const string& text)
{
	string core = text;
	while (!core.empty())
	{
		char c = core[core.size() - 1];
		if (c == 'u' || c == 'U' || c == 'l' || c == 'L') core.erase(core.size() - 1);
		else break;
	}
	if (core.empty()) throw runtime_error("invalid integer");
	return strtoll(core.c_str(), 0, 0);
}

struct IfParser
{
	const vector<PPToken>& toks;
	size_t pos;

	IfParser(const vector<PPToken>& t) : toks(t), pos(0) {}

	bool match(const string& op)
	{
		if (pos < toks.size() && toks[pos].kind == PP_OP && toks[pos].data == op)
		{
			++pos;
			return true;
		}
		return false;
	}

	long long primary()
	{
		if (match("("))
		{
			long long v = expr();
			if (!match(")")) throw runtime_error("unterminated #if");
			return v;
		}
		if (pos >= toks.size()) throw runtime_error("bad #if");
		if (toks[pos].kind == PP_NUMBER) return ParsePPInteger(toks[pos++].data);
		if (toks[pos].kind == PP_IDENTIFIER) { ++pos; return 0; }
		throw runtime_error("bad #if");
	}

	long long unary()
	{
		if (match("!")) return !unary();
		if (match("+")) return unary();
		if (match("-")) return -unary();
		if (match("~")) return ~unary();
		return primary();
	}

	long long add()
	{
		long long v = unary();
		while (true)
		{
			if (match("+")) v += unary();
			else if (match("-")) v -= unary();
			else break;
		}
		return v;
	}

	long long rel()
	{
		long long v = add();
		while (true)
		{
			if (match("==")) v = (v == add());
			else if (match("!=")) v = (v != add());
			else if (match("<")) v = (v < add());
			else if (match(">")) v = (v > add());
			else if (match("<=")) v = (v <= add());
			else if (match(">=")) v = (v >= add());
			else break;
		}
		return v;
	}

	long long land()
	{
		long long v = rel();
		while (match("&&")) v = (v && rel());
		return v;
	}

	long long expr()
	{
		long long v = land();
		while (match("||")) v = (v || land());
		return v;
	}
};

vector<PPToken> ReplaceDefined(const vector<PPToken>& tokens, const map<string, MacroDef>& macros)
{
	vector<PPToken> flat = CollapseWhitespace(tokens);
	vector<PPToken> out;
	for (size_t i = 0; i < flat.size(); ++i)
	{
		if (flat[i].kind == PP_IDENTIFIER && flat[i].data == "defined")
		{
			size_t j = i + 1;
			while (j < flat.size() && flat[j].kind == PP_WS) ++j;
			string name;
			if (j < flat.size() && flat[j].kind == PP_OP && flat[j].data == "(")
			{
				++j;
				while (j < flat.size() && flat[j].kind == PP_WS) ++j;
				if (j >= flat.size() || flat[j].kind != PP_IDENTIFIER) throw runtime_error("bad defined");
				name = flat[j].data;
				++j;
				while (j < flat.size() && flat[j].kind == PP_WS) ++j;
				if (j >= flat.size() || flat[j].kind != PP_OP || flat[j].data != ")") throw runtime_error("bad defined");
			}
			else if (j < flat.size() && flat[j].kind == PP_IDENTIFIER)
			{
				name = flat[j].data;
			}
			else
			{
				throw runtime_error("bad defined");
			}
			out.push_back({PP_NUMBER, macros.count(name) ? "1" : "0"});
			i = j;
			continue;
		}
		out.push_back(flat[i]);
	}
	return out;
}

bool EvaluateIfCondition(const vector<PPToken>& tokens, map<string, MacroDef>& macros)
{
	vector<PPToken> with_defined = ReplaceDefined(tokens, macros);
	vector<PPToken> expanded = ToPPTokens(ExpandTokens(ToMTokens(with_defined), macros, set<string>()));
	vector<PPToken> flat;
	for (size_t i = 0; i < expanded.size(); ++i)
	{
		if (expanded[i].kind == PP_WS) continue;
		if (expanded[i].kind == PP_IDENTIFIER)
		{
			if (expanded[i].data == "true") flat.push_back({PP_NUMBER, "1"});
			else if (expanded[i].data == "false") flat.push_back({PP_NUMBER, "0"});
			else flat.push_back({PP_NUMBER, "0"});
		}
		else flat.push_back(expanded[i]);
	}
	IfParser parser(flat);
	long long value = parser.expr();
	if (parser.pos != flat.size()) throw runtime_error("bad #if");
	return value != 0;
}

void EnsureValidTokens(const vector<PPToken>& flat)
{
	for (size_t i = 0; i < flat.size(); ++i)
	{
		const PPToken& tok = flat[i];
		if (tok.kind == PP_HEADER || tok.kind == PP_NONWS)
		{
			throw runtime_error("invalid token");
		}
		if (tok.kind == PP_IDENTIFIER || tok.kind == PP_OP)
		{
			if (tok.kind == PP_OP &&
				(tok.data == "#" || tok.data == "##" || tok.data == "%:" || tok.data == "%:%:"))
			{
				throw runtime_error("invalid token");
			}
			continue;
		}
		if (tok.kind == PP_NUMBER)
		{
			ostringstream sink_text;
			ScopedCoutRedirect guard(sink_text.rdbuf());
			DebugPostTokenOutputStream sink;
			bool ok = ParseFloatingLiteral(tok.data, sink) || ParseIntegerLiteral(tok.data, sink);
			if (!ok)
			{
				throw runtime_error("invalid token");
			}
			continue;
		}
		if (tok.kind == PP_CHAR || tok.kind == PP_UD_CHAR)
		{
			if (tok.kind == PP_UD_CHAR)
			{
				string suffix;
				string core = StripUDSuffix(tok.data, suffix);
				EFundamentalType type;
				vector<unsigned char> bytes;
				if (suffix.empty() || !IsValidUDSuffix(suffix) || !ParseCharacterLiteralCore(core, type, bytes))
					throw runtime_error("invalid token");
			}
			else
			{
				EFundamentalType type;
				vector<unsigned char> bytes;
				if (!ParseCharacterLiteralCore(tok.data, type, bytes))
					throw runtime_error("invalid token");
			}
			continue;
		}
		if (tok.kind == PP_STRING || tok.kind == PP_UD_STRING)
		{
			size_t j = i;
			while (j < flat.size() && (flat[j].kind == PP_STRING || flat[j].kind == PP_UD_STRING)) ++j;
			if (j > i + 1)
			{
				string joined;
				vector<string> group;
				for (size_t k = i; k < j; ++k) group.push_back(flat[k].data);
				for (size_t k = 0; k < group.size(); ++k)
				{
					if (k) joined += " ";
					joined += group[k];
				}
				bool ok = true;
				vector<string> cores(group.size());
				string ud_suffix;
				bool have_ud = false;
				bool has_u8 = false;
				string effective = "";
				for (size_t k = 0; k < group.size(); ++k)
				{
					string suffix;
					cores[k] = group[k];
					if (flat[i + k].kind == PP_UD_STRING)
					{
						cores[k] = StripUDSuffix(group[k], suffix);
						if (!IsValidUDSuffix(suffix)) ok = false;
						if (!have_ud) { have_ud = true; ud_suffix = suffix; }
						else if (ud_suffix != suffix) ok = false;
					}
					string prefix = cores[k].rfind("u8", 0) == 0 ? "u8" :
						(!cores[k].empty() && (cores[k][0] == 'u' || cores[k][0] == 'U' || cores[k][0] == 'L') ? cores[k].substr(0, 1) : "");
					if (prefix == "u8") has_u8 = true;
					else if (prefix == "u" || prefix == "U" || prefix == "L")
					{
						if (effective.empty()) effective = prefix;
						else if (effective != prefix) ok = false;
					}
				}
				if (!effective.empty() && has_u8) ok = false;
				EFundamentalType type = effective == "u" ? FT_CHAR16_T : (effective == "U" ? FT_CHAR32_T : (effective == "L" ? FT_WCHAR_T : FT_CHAR));
				vector<unsigned char> bytes;
				size_t elems = 0;
				if (ok)
				{
					for (size_t k = 0; k < cores.size(); ++k)
					{
						vector<int> vals;
						if (!ExtractStringCodePoints(cores[k], vals)) { ok = false; break; }
						EFundamentalType cur_type = StringLiteralTypeOf(cores[k]);
						if (type != FT_CHAR && cur_type != FT_CHAR && cur_type != type) { ok = false; break; }
						for (size_t m = 0; m < vals.size(); ++m)
						{
							if (type == FT_CHAR16_T && vals[m] > 0x10FFFF) { ok = false; break; }
							if (type == FT_CHAR16_T && vals[m] > 0xFFFF) elems += 2;
							else if (type == FT_CHAR) elems += EncodeUTF8(vals[m]).size();
							else ++elems;
							AppendCodePoint(bytes, type, vals[m]);
						}
						if (!ok) break;
					}
				}
				if (!ok) throw runtime_error("invalid token");
				i = j - 1;
				continue;
			}

			EFundamentalType type;
			vector<unsigned char> bytes;
			size_t elems = 0;
			if (tok.kind == PP_UD_STRING)
			{
				string suffix;
				string core = StripUDSuffix(tok.data, suffix);
				if (suffix.empty() || !IsValidUDSuffix(suffix) || !ParseStringLiteralCore(core, type, bytes, elems))
					throw runtime_error("invalid token");
			}
			else
			{
				if (!ParseStringLiteralCore(tok.data, type, bytes, elems))
					throw runtime_error("invalid token");
			}
			continue;
		}
		throw runtime_error("invalid token");
	}
}

string EscapeStringLiteral(const string& s)
{
	string out = "\"";
	for (size_t i = 0; i < s.size(); ++i)
	{
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (c == '\\' || c == '"')
		{
			out.push_back('\\');
			out.push_back(static_cast<char>(c));
		}
		else if (c == '\n')
		{
			out += "\\n";
		}
		else if (c == '\t')
		{
			out += "\\t";
		}
		else
		{
			out.push_back(static_cast<char>(c));
		}
	}
	out.push_back('"');
	return out;
}

string PA5DateLiteral()
{
	static string cached;
	if (!cached.empty()) return cached;
	time_t now = time(0);
	tm* parts = localtime(&now);
	char buf[32];
	if (parts == 0 || strftime(buf, sizeof(buf), "%b %e %Y", parts) == 0) return "\"??? ?? ????\"";
	cached = EscapeStringLiteral(buf);
	return cached;
}

string PA5TimeLiteral()
{
	static string cached;
	if (!cached.empty()) return cached;
	time_t now = time(0);
	tm* parts = localtime(&now);
	char buf[32];
	if (parts == 0 || strftime(buf, sizeof(buf), "%H:%M:%S", parts) == 0) return "\"??:??:??\"";
	cached = EscapeStringLiteral(buf);
	return cached;
}

MacroDef MakeObjectMacro(PPTokenKind kind, const string& data)
{
	MacroDef def;
	def.replacement.push_back({kind, data});
	return def;
}

PPToken CanonicalizeToken(PPToken tok)
{
	if (tok.kind == PP_OP)
	{
		if (tok.data == "%:") tok.data = "#";
		else if (tok.data == "%:%:") tok.data = "##";
	}
	return tok;
}

string DirName(const string& path)
{
	size_t pos = path.rfind('/');
	return pos == string::npos ? "" : path.substr(0, pos);
}

string JoinPath(const string& dir, const string& file)
{
	if (dir.empty()) return file;
	return dir + "/" + file;
}

bool CanReadFile(const string& path)
{
	ifstream in(path.c_str());
	return in.good();
}

vector<unsigned long> ComputeLogicalLineEnds(const string& transformed_text)
{
	enum
	{
		ST_NORMAL,
		ST_LINE_COMMENT,
		ST_BLOCK_COMMENT,
		ST_STRING,
		ST_CHAR
	} state = ST_NORMAL;

	vector<unsigned long> out;
	unsigned long raw_line = 1;
	for (size_t i = 0; i < transformed_text.size(); ++i)
	{
		char c = transformed_text[i];
		char n = i + 1 < transformed_text.size() ? transformed_text[i + 1] : '\0';
		if (state == ST_NORMAL)
		{
			if (c == '/' && n == '/')
			{
				state = ST_LINE_COMMENT;
				++i;
			}
			else if (c == '/' && n == '*')
			{
				state = ST_BLOCK_COMMENT;
				++i;
			}
			else if (c == '"')
			{
				state = ST_STRING;
			}
			else if (c == '\'')
			{
				state = ST_CHAR;
			}
			else if (c == '\n')
			{
				out.push_back(raw_line++);
			}
		}
		else if (state == ST_LINE_COMMENT)
		{
			if (c == '\n')
			{
				out.push_back(raw_line++);
				state = ST_NORMAL;
			}
		}
		else if (state == ST_BLOCK_COMMENT)
		{
			if (c == '*' && n == '/')
			{
				state = ST_NORMAL;
				++i;
			}
			else if (c == '\n')
			{
				++raw_line;
			}
		}
		else
		{
			if (c == '\\')
			{
				++i;
			}
			else if ((state == ST_STRING && c == '"') || (state == ST_CHAR && c == '\''))
			{
				state = ST_NORMAL;
			}
			else if (c == '\n')
			{
				out.push_back(raw_line++);
				state = ST_NORMAL;
			}
		}
	}
	return out;
}

map<string, MacroDef> BuildMacroEnv(const map<string, MacroDef>& macros, const string& logical_file, unsigned long logical_line)
{
	map<string, MacroDef> env = macros;
	env["__CPPGM__"] = MakeObjectMacro(PP_NUMBER, "201303L");
	env["__CPPGM_AUTHOR__"] = MakeObjectMacro(PP_NUMBER, "1");
	env["__cplusplus"] = MakeObjectMacro(PP_NUMBER, "201103L");
	env["__STDC_HOSTED__"] = MakeObjectMacro(PP_NUMBER, "1");
	env["__FILE__"] = MakeObjectMacro(PP_STRING, EscapeStringLiteral(logical_file));
	env["__LINE__"] = MakeObjectMacro(PP_NUMBER, to_string(logical_line));
	env["__DATE__"] = MakeObjectMacro(PP_STRING, PA5DateLiteral());
	env["__TIME__"] = MakeObjectMacro(PP_STRING, PA5TimeLiteral());
	return env;
}

string DecodeStringLiteralToken(const string& token)
{
	vector<int> vals;
	if (!ExtractStringCodePoints(token, vals))
	{
		throw runtime_error("invalid string literal");
	}
	string out;
	for (size_t i = 0; i < vals.size(); ++i)
	{
		out += EncodeUTF8(vals[i]);
	}
	return out;
}

struct PreprocState
{
	map<string, MacroDef> macros;
	set<PA5FileId> pragma_once_ids;
	set<string> pragma_once_paths;
	vector<PPToken> out_tokens;
};

void HandlePragmaText(const string& text, const string& current_path, const PA5FileId* fileid, PreprocState& state)
{
	size_t begin = 0;
	while (begin < text.size() && IsWSNoNL(static_cast<unsigned char>(text[begin]))) ++begin;
	size_t end = text.size();
	while (end > begin && IsWSNoNL(static_cast<unsigned char>(text[end - 1]))) --end;
	string trimmed = text.substr(begin, end - begin);
	if (trimmed == "once")
	{
		if (fileid != 0) state.pragma_once_ids.insert(*fileid);
		else state.pragma_once_paths.insert(current_path);
	}
}

vector<PPToken> ApplyPragmaOperators(const vector<PPToken>& tokens, const string& current_path, const PA5FileId* fileid, PreprocState& state)
{
	vector<PPToken> out;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (tokens[i].kind == PP_IDENTIFIER && tokens[i].data == "_Pragma")
		{
			size_t p = i + 1;
			while (p < tokens.size() && tokens[p].kind == PP_WS) ++p;
			if (p >= tokens.size() || tokens[p].kind != PP_OP || tokens[p].data != "(") throw runtime_error("invalid _Pragma");
			++p;
			while (p < tokens.size() && tokens[p].kind == PP_WS) ++p;
			if (p >= tokens.size() || (tokens[p].kind != PP_STRING && tokens[p].kind != PP_UD_STRING)) throw runtime_error("invalid _Pragma");
			if (tokens[p].kind == PP_UD_STRING) throw runtime_error("invalid _Pragma");
			string text = DecodeStringLiteralToken(tokens[p].data);
			++p;
			while (p < tokens.size() && tokens[p].kind == PP_WS) ++p;
			if (p >= tokens.size() || tokens[p].kind != PP_OP || tokens[p].data != ")") throw runtime_error("invalid _Pragma");
			HandlePragmaText(text, current_path, fileid, state);
			i = p;
			continue;
		}
		out.push_back(tokens[i]);
	}
	return out;
}

pair<string, bool> ParseIncludeTarget(const vector<PPToken>& tokens, const map<string, MacroDef>& env)
{
	map<string, MacroDef> expanded_env = env;
	vector<PPToken> expanded = ToPPTokens(ExpandTokens(ToMTokens(CollapseWhitespace(tokens)), expanded_env, set<string>()));
	vector<PPToken> flat;
	for (size_t i = 0; i < expanded.size(); ++i)
	{
		if (expanded[i].kind != PP_WS) flat.push_back(expanded[i]);
	}
	if (flat.size() == 1 && flat[0].kind == PP_STRING)
	{
		return make_pair(DecodeStringLiteralToken(flat[0].data), false);
	}
	if (!flat.empty() && flat.front().kind == PP_OP && flat.front().data == "<" &&
		flat.back().kind == PP_OP && flat.back().data == ">")
	{
		string name;
		for (size_t i = 1; i + 1 < flat.size(); ++i)
		{
			if (flat[i].kind == PP_OP && flat[i].data == ">") throw runtime_error("invalid include");
			name += flat[i].data;
		}
		return make_pair(name, true);
	}
	throw runtime_error("invalid include");
}

string ResolveIncludePath(const string& include_name, bool angled, const string& includer_path)
{
	vector<string> candidates;
	if (!angled)
	{
		string dir = DirName(includer_path);
		if (!dir.empty()) candidates.push_back(JoinPath(dir, include_name));
	}
	candidates.push_back(include_name);
	for (size_t i = 0; i < PA5StdIncPaths.size(); ++i)
	{
		candidates.push_back(JoinPath(PA5StdIncPaths[i], include_name));
	}
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		if (CanReadFile(candidates[i])) return candidates[i];
	}
	throw runtime_error("include not found");
}

bool EndsWithFunctionLikeMacroName(const vector<PPToken>& tokens, const map<string, MacroDef>& macros)
{
	vector<PPToken> flat = CollapseWhitespace(tokens);
	if (flat.empty() || flat.back().kind != PP_IDENTIFIER) return false;
	map<string, MacroDef>::const_iterator it = macros.find(flat.back().data);
	return it != macros.end() && it->second.function_like;
}

vector<PPToken> ExpandActiveTokens(const vector<PPToken>& tokens, const string& logical_file, unsigned long logical_line, const PA5FileId* fileid, PreprocState& state)
{
	vector<PPToken> text = CollapseWhitespace(tokens);
	if (text.empty()) return vector<PPToken>();
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i].kind == PP_IDENTIFIER && text[i].data == "__VA_ARGS__")
		{
			throw runtime_error("invalid __VA_ARGS__");
		}
	}
	map<string, MacroDef> env = BuildMacroEnv(state.macros, logical_file, logical_line);
	vector<PPToken> expanded = ToPPTokens(ExpandTokens(ToMTokens(text), env, set<string>()));
	expanded = ApplyPragmaOperators(expanded, logical_file, fileid, state);
	vector<PPToken> no_ws;
	for (size_t i = 0; i < expanded.size(); ++i)
	{
		if (expanded[i].kind != PP_WS) no_ws.push_back(expanded[i]);
	}
	EnsureValidTokens(no_ws);
	return no_ws;
}

void ProcessSourceFileRecursive(const string& srcfile, bool is_include, PreprocState& state)
{
	PA5FileId fileid = make_pair(0, 0);
	bool have_fileid = PA5GetFileId(srcfile, fileid);
	if (is_include &&
		((have_fileid && state.pragma_once_ids.count(fileid) != 0) ||
		(!have_fileid && state.pragma_once_paths.count(srcfile) != 0)))
	{
		return;
	}

	ifstream in(srcfile.c_str());
	if (!in)
	{
		throw runtime_error("include not found");
	}
	ostringstream oss;
	oss << in.rdbuf();

	vector<int> cps = TransformSource(DecodeUTF8(oss.str()));
	if (!cps.empty() && cps.back() != '\n')
	{
		cps.push_back('\n');
	}
	string transformed_text;
	for (size_t i = 0; i < cps.size(); ++i)
	{
		transformed_text += EncodeUTF8(cps[i]);
	}

	PPCollector collector;
	PPTokenizer tokenizer(collector);
	vector<PPToken> tokens = tokenizer.tokenize(cps);
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		tokens[i] = CanonicalizeToken(tokens[i]);
	}

	vector<unsigned long> raw_line_numbers = ComputeLogicalLineEnds(transformed_text);

	vector<PPToken> line;
	vector<PPToken> pending_text;
	vector<ConditionalFrame> conds;
	string logical_file = srcfile;
	long long line_offset = 0;
	unsigned long pending_line = 1;
	string pending_file = srcfile;
	size_t line_index = 0;

	auto is_active = [&]() {
		for (size_t k = 0; k < conds.size(); ++k)
		{
			if (!conds[k].active) return false;
		}
		return true;
	};

	for (size_t i = 0; i < tokens.size(); ++i)
	{
		bool flush = tokens[i].kind == PP_NL;
		if (!flush)
		{
			line.push_back(tokens[i]);
			continue;
		}

		vector<PPToken> trimmed = line;
		while (!trimmed.empty() && trimmed.front().kind == PP_WS) trimmed.erase(trimmed.begin());
		unsigned long raw_line = raw_line_numbers[line_index++];
		unsigned long logical_line = static_cast<unsigned long>(static_cast<long long>(raw_line) + line_offset);

		auto flush_pending = [&]() {
			if (pending_text.empty()) return;
			vector<PPToken> expanded = ExpandActiveTokens(pending_text, pending_file, pending_line, have_fileid ? &fileid : 0, state);
			state.out_tokens.insert(state.out_tokens.end(), expanded.begin(), expanded.end());
			pending_text.clear();
		};

		if (!trimmed.empty() && trimmed.front().kind == PP_OP && trimmed.front().data == "#")
		{
			flush_pending();
			size_t p = 1;
			while (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
			if (p < trimmed.size())
			{
				bool active = is_active();
				if (trimmed[p].kind != PP_IDENTIFIER)
				{
					if (active) throw runtime_error("invalid directive");
				}
				else
				{
					string directive = trimmed[p].data;
					++p;
					if (directive == "if")
					{
						bool parent = is_active();
						bool cond = false;
						if (parent)
						{
							map<string, MacroDef> env = BuildMacroEnv(state.macros, logical_file, logical_line);
							cond = EvaluateIfCondition(vector<PPToken>(trimmed.begin() + p, trimmed.end()), env);
						}
						conds.push_back({parent, parent && cond, parent && cond, false});
					}
					else if (directive == "ifdef" || directive == "ifndef")
					{
						bool parent = is_active();
						while (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
						if (p >= trimmed.size() || trimmed[p].kind != PP_IDENTIFIER) throw runtime_error("invalid ifdef");
						map<string, MacroDef> env = BuildMacroEnv(state.macros, logical_file, logical_line);
						bool cond = env.count(trimmed[p].data) != 0;
						if (directive == "ifndef") cond = !cond;
						conds.push_back({parent, parent && cond, parent && cond, false});
					}
					else if (directive == "elif")
					{
						if (conds.empty() || conds.back().seen_else) throw runtime_error("invalid elif");
						bool cond = false;
						if (conds.back().parent_active && !conds.back().branch_taken)
						{
							map<string, MacroDef> env = BuildMacroEnv(state.macros, logical_file, logical_line);
							cond = EvaluateIfCondition(vector<PPToken>(trimmed.begin() + p, trimmed.end()), env);
						}
						conds.back().active = conds.back().parent_active && !conds.back().branch_taken && cond;
						if (conds.back().active) conds.back().branch_taken = true;
					}
					else if (directive == "else")
					{
						if (conds.empty() || conds.back().seen_else) throw runtime_error("invalid else");
						conds.back().seen_else = true;
						conds.back().active = conds.back().parent_active && !conds.back().branch_taken;
						if (conds.back().active) conds.back().branch_taken = true;
					}
					else if (directive == "endif")
					{
						if (conds.empty()) throw runtime_error("invalid endif");
						conds.pop_back();
					}
					else if (!active)
					{
					}
					else if (directive == "define")
					{
						while (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
						if (p >= trimmed.size() || trimmed[p].kind != PP_IDENTIFIER)
						{
							throw runtime_error("invalid define");
						}
						string name = trimmed[p].data;
						if (name == "__VA_ARGS__")
						{
							throw runtime_error("invalid define");
						}
						MacroDef def;
						++p;
						if (p < trimmed.size() && trimmed[p].kind == PP_OP && trimmed[p].data == "(")
						{
							def.function_like = true;
							++p;
							bool expect_param = true;
							while (p < trimmed.size() && !(trimmed[p].kind == PP_OP && trimmed[p].data == ")"))
							{
								if (trimmed[p].kind == PP_WS)
								{
									++p;
									continue;
								}
								if (trimmed[p].kind == PP_IDENTIFIER)
								{
									if (!expect_param) throw runtime_error("invalid function-like define");
									if (trimmed[p].data == "__VA_ARGS__") throw runtime_error("invalid define");
									for (size_t q = 0; q < def.params.size(); ++q)
									{
										if (def.params[q] == trimmed[p].data) throw runtime_error("invalid function-like define");
									}
									def.params.push_back(trimmed[p].data);
									expect_param = false;
									++p;
								}
								else if (trimmed[p].kind == PP_OP && trimmed[p].data == ",")
								{
									if (expect_param) throw runtime_error("invalid function-like define");
									expect_param = true;
									++p;
								}
								else if (trimmed[p].kind == PP_OP && trimmed[p].data == "...")
								{
									if (!expect_param) throw runtime_error("invalid function-like define");
									def.variadic = true;
									expect_param = false;
									++p;
									break;
								}
								else
								{
									throw runtime_error("invalid function-like define");
								}
							}
							if (p >= trimmed.size() || !(trimmed[p].kind == PP_OP && trimmed[p].data == ")"))
							{
								throw runtime_error("invalid function-like define");
							}
							if (expect_param && !def.params.empty() && !def.variadic)
							{
								throw runtime_error("invalid function-like define");
							}
							++p;
						}
						if (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
						def.replacement.assign(trimmed.begin() + p, trimmed.end());
						for (size_t r = 0; r < def.replacement.size(); ++r)
						{
							if (def.replacement[r].kind == PP_IDENTIFIER &&
								def.replacement[r].data == "__VA_ARGS__" &&
								!def.variadic)
							{
								throw runtime_error("invalid define");
							}
							if (def.replacement[r].kind == PP_OP && def.replacement[r].data == "#")
							{
								if (!def.function_like) continue;
								size_t next = r + 1;
								while (next < def.replacement.size() && def.replacement[next].kind == PP_WS) ++next;
								bool ok = next < def.replacement.size() && def.replacement[next].kind == PP_IDENTIFIER;
								if (ok)
								{
									ok = false;
									for (size_t q = 0; q < def.params.size(); ++q)
									{
										if (def.params[q] == def.replacement[next].data) { ok = true; break; }
									}
									if (def.variadic && def.replacement[next].data == "__VA_ARGS__") ok = true;
								}
								if (!ok) throw runtime_error("invalid define");
							}
							if (def.replacement[r].kind == PP_OP && def.replacement[r].data == "##")
							{
								size_t left = r;
								while (left > 0 && def.replacement[left - 1].kind == PP_WS) --left;
								size_t right = r + 1;
								while (right < def.replacement.size() && def.replacement[right].kind == PP_WS) ++right;
								if (left == 0 || right >= def.replacement.size()) throw runtime_error("invalid define");
							}
						}
						string key = MacroKey(def);
						if (state.macros.count(name) != 0 && MacroKey(state.macros[name]) != key)
						{
							throw runtime_error("invalid redefine");
						}
						state.macros[name] = def;
					}
					else if (directive == "undef")
					{
						while (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
						if (p >= trimmed.size() || trimmed[p].kind != PP_IDENTIFIER) throw runtime_error("invalid undef");
						string name = trimmed[p].data;
						if (name == "__VA_ARGS__") throw runtime_error("invalid undef");
						++p;
						while (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
						if (p != trimmed.size()) throw runtime_error("extra tokens after undef");
						state.macros.erase(name);
					}
					else if (directive == "include")
					{
						map<string, MacroDef> env = BuildMacroEnv(state.macros, logical_file, logical_line);
						pair<string, bool> header = ParseIncludeTarget(vector<PPToken>(trimmed.begin() + p, trimmed.end()), env);
						string include_path = ResolveIncludePath(header.first, header.second, srcfile);
						ProcessSourceFileRecursive(include_path, true, state);
					}
					else if (directive == "pragma")
					{
						string text;
						for (size_t q = p; q < trimmed.size(); ++q) text += trimmed[q].data;
						HandlePragmaText(text, srcfile, have_fileid ? &fileid : 0, state);
					}
					else if (directive == "line")
					{
						vector<PPToken> args = CollapseWhitespace(vector<PPToken>(trimmed.begin() + p, trimmed.end()));
						if (args.empty() || args[0].kind != PP_NUMBER) throw runtime_error("invalid line");
						long long line_value = ParsePPInteger(args[0].data);
						if (line_value <= 0) throw runtime_error("invalid line");
						if (args.size() == 1)
						{
							line_offset = line_value - static_cast<long long>(raw_line + 1);
						}
						else if (args.size() == 3 && args[1].kind == PP_WS && args[2].kind == PP_STRING)
						{
							line_offset = line_value - static_cast<long long>(raw_line + 1);
							logical_file = DecodeStringLiteralToken(args[2].data);
						}
						else
						{
							throw runtime_error("invalid line");
						}
					}
					else if (directive == "error")
					{
						throw runtime_error("error directive");
					}
					else
					{
						throw runtime_error("unsupported directive");
					}
				}
			}
		}
		else if (is_active())
		{
			if (!trimmed.empty() || !pending_text.empty())
			{
				if (pending_text.empty()) pending_file = logical_file;
				pending_line = logical_line;
				if (!pending_text.empty()) pending_text.push_back({PP_WS, ""});
				pending_text.insert(pending_text.end(), trimmed.begin(), trimmed.end());
				if (!EndsWithFunctionLikeMacroName(pending_text, state.macros))
				{
					try
					{
						flush_pending();
					}
					catch (exception& e)
					{
						if (string(e.what()) != "unterminated macro invocation") throw;
					}
				}
			}
		}
		else
		{
			flush_pending();
		}
		line.clear();
	}

	if (!pending_text.empty())
	{
		vector<PPToken> expanded = ExpandActiveTokens(pending_text, pending_file, pending_line, have_fileid ? &fileid : 0, state);
		state.out_tokens.insert(state.out_tokens.end(), expanded.begin(), expanded.end());
	}
	if (!conds.empty())
	{
		if (is_include) throw runtime_error("include completed in bad group state (maybe unterminated #if)");
		throw runtime_error("source file completed in bad group state (maybe unterminated #if)");
	}
}

vector<PPToken> PreprocessSourceTokens(const string& srcfile)
{
	PreprocState state;
	ProcessSourceFileRecursive(srcfile, false, state);
	EnsureValidTokens(state.out_tokens);
	return state.out_tokens;
}

void ProcessSourceFile(const string& srcfile)
{
	vector<PPToken> tokens = PreprocessSourceTokens(srcfile);
	DebugPostTokenOutputStream output;
	EmitPostTokenSequence(tokens, output);
	output.emit_eof();
}

#ifndef CPPGM_PREPROC_MAIN_NAME
#define CPPGM_PREPROC_MAIN_NAME main
#endif

int CPPGM_PREPROC_MAIN_NAME(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
		if (args.size() < 3 || args[0] != "-o") throw logic_error("invalid usage");

		string outfile = args[1];
		ofstream out(outfile.c_str());
		ScopedCoutRedirect guard(out.rdbuf());

		size_t nsrcfiles = args.size() - 2;
		cout << "preproc " << nsrcfiles << endl;
		for (size_t i = 0; i < nsrcfiles; ++i)
		{
			cout << "sof " << args[i + 2] << endl;
			ProcessSourceFile(args[i + 2]);
		}
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
