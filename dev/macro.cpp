// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

#define CPPGM_EMBED_POSTTOKEN 1
#include "posttoken.cpp"

struct MacroToken
{
	EPPTokenKind kind;
	string source;
	set<string> blacklist;
	string file;
	int line = 0;
};

struct MacroDef
{
	bool function_like = false;
	bool variadic = false;
	vector<string> params;
	vector<MacroToken> replacement;
};

MacroToken MakeMacroToken(EPPTokenKind kind, const string& source)
{
	MacroToken token;
	token.kind = kind;
	token.source = source;
	return token;
}

string QuoteMacroStringLiteral(const string& text)
{
	string out = "\"";
	for (char c : text)
	{
		if (c == '\\' || c == '"')
		{
			out += '\\';
		}
		out += c;
	}
	out += '"';
	return out;
}

bool IsWhitespaceToken(const MacroToken& token)
{
	return token.kind == PPT_WHITESPACE || token.kind == PPT_NEWLINE;
}

bool IsIdentifierToken(const MacroToken& token, const string& source = string())
{
	return token.kind == PPT_IDENTIFIER && (source.empty() || token.source == source);
}

bool IsVaArgsToken(const MacroToken& token)
{
	return IsIdentifierToken(token, "__VA_ARGS__");
}

bool IsOpToken(const MacroToken& token, const string& source)
{
	if (token.kind != PPT_PREPROCESSING_OP_OR_PUNC)
	{
		return false;
	}
	if (token.source == source)
	{
		return true;
	}
	return (source == "#" && token.source == "%:") ||
		(source == "##" && token.source == "%:%:");
}

void SkipWhitespace(const vector<MacroToken>& tokens, size_t& pos)
{
	while (pos < tokens.size() && tokens[pos].kind == PPT_WHITESPACE)
	{
		++pos;
	}
}

vector<MacroToken> CollapseTextSequence(const vector<MacroToken>& input)
{
	vector<MacroToken> out;
	bool pending_ws = false;
	for (const MacroToken& token : input)
	{
		if (IsWhitespaceToken(token))
		{
			pending_ws = !out.empty();
			continue;
		}
		if (pending_ws)
		{
			out.push_back(MakeMacroToken(PPT_WHITESPACE, ""));
			pending_ws = false;
		}
		out.push_back(token);
	}
	return out;
}

size_t NextNonWhitespace(const vector<MacroToken>& tokens, size_t pos)
{
	while (pos < tokens.size() && IsWhitespaceToken(tokens[pos]))
	{
		++pos;
	}
	return pos;
}

size_t PreviousNonWhitespace(const vector<MacroToken>& tokens, size_t pos)
{
	while (pos > 0)
	{
		--pos;
		if (!IsWhitespaceToken(tokens[pos]))
		{
			return pos;
		}
	}
	return tokens.size();
}

vector<MacroToken> TokenizeMacroText(const string& text)
{
	CollectingPPTokenStream output;
	PPTokenizer tokenizer(output);
	for (unsigned char c : text)
	{
		tokenizer.process(c);
	}
	tokenizer.process(EndOfFile);

	vector<MacroToken> tokens;
	for (const PPToken& token : output.tokens)
	{
		if (token.kind == PPT_EOF || token.kind == PPT_NEWLINE || token.kind == PPT_WHITESPACE)
		{
			continue;
		}
		tokens.push_back(MakeMacroToken(token.kind, token.source));
	}
	return tokens;
}

string StringizeTokens(const vector<MacroToken>& tokens)
{
	vector<MacroToken> collapsed = CollapseTextSequence(tokens);
	string out = "\"";
	for (const MacroToken& token : collapsed)
	{
		if (token.kind == PPT_WHITESPACE)
		{
			out += ' ';
			continue;
		}
		bool escape = token.kind == PPT_STRING_LITERAL ||
			token.kind == PPT_USER_DEFINED_STRING_LITERAL ||
			token.kind == PPT_CHARACTER_LITERAL ||
			token.kind == PPT_USER_DEFINED_CHARACTER_LITERAL;
		for (char c : token.source)
		{
			if (escape && (c == '\\' || c == '"'))
			{
				out += '\\';
			}
			out += c;
		}
	}
	out += '"';
	return out;
}

bool SameMacro(const MacroDef& lhs, const MacroDef& rhs)
{
	if (lhs.function_like != rhs.function_like || lhs.variadic != rhs.variadic || lhs.params != rhs.params)
	{
		return false;
	}
	if (lhs.replacement.size() != rhs.replacement.size())
	{
		return false;
	}
	for (size_t i = 0; i < lhs.replacement.size(); ++i)
	{
		if (lhs.replacement[i].kind != rhs.replacement[i].kind || lhs.replacement[i].source != rhs.replacement[i].source)
		{
			return false;
		}
	}
	return true;
}

bool EmitRelaxedNumericUdLiteral(DebugPostTokenOutputStream& output, const string& source);

void EmitPostTokens(DebugPostTokenOutputStream& output, const vector<MacroToken>& macro_tokens)
{
	vector<PPToken> tokens;
	for (const MacroToken& token : macro_tokens)
	{
		if (token.kind == PPT_WHITESPACE || token.kind == PPT_NEWLINE || token.kind == PPT_EOF)
		{
			continue;
		}
		tokens.push_back(PPToken{token.kind, token.source});
	}

	for (size_t i = 0; i < tokens.size(); ++i)
	{
		const PPToken& token = tokens[i];
		switch (token.kind)
		{
		case PPT_HEADER_NAME:
		case PPT_NON_WHITESPACE_CHARACTER:
			output.emit_invalid(token.source);
			break;
		case PPT_IDENTIFIER:
		case PPT_PREPROCESSING_OP_OR_PUNC:
		{
			auto it = StringToTokenTypeMap.find(token.source);
			if (it != StringToTokenTypeMap.end() &&
				!(token.source == "#" || token.source == "##" || token.source == "%:" || token.source == "%:%:"))
			{
				output.emit_simple(token.source, it->second);
			}
			else if (token.kind == PPT_IDENTIFIER)
			{
				output.emit_identifier(token.source);
			}
			else
			{
				output.emit_invalid(token.source);
			}
			break;
		}
			case PPT_PP_NUMBER:
				if (!EmitFloatingLiteral(output, token.source) &&
					!EmitIntegerLiteral(output, token.source) &&
					!EmitRelaxedNumericUdLiteral(output, token.source))
				{
					output.emit_invalid(token.source);
				}
				break;
		case PPT_CHARACTER_LITERAL:
		case PPT_USER_DEFINED_CHARACTER_LITERAL:
			if (!EmitCharacterLiteral(output, token.source))
			{
				output.emit_invalid(token.source);
			}
			break;
		case PPT_STRING_LITERAL:
		case PPT_USER_DEFINED_STRING_LITERAL:
		{
			size_t end = i + 1;
			while (end < tokens.size() &&
				(tokens[end].kind == PPT_STRING_LITERAL ||
				 tokens[end].kind == PPT_USER_DEFINED_STRING_LITERAL))
			{
				++end;
			}
			EmitStringSequence(output, tokens, i, end);
			i = end - 1;
			break;
		}
		default:
			break;
		}
	}
}

bool EmitRelaxedNumericUdLiteral(DebugPostTokenOutputStream& output, const string& source)
{
	size_t suffix_pos = source.find('_');
	if (suffix_pos == string::npos || suffix_pos + 1 >= source.size())
	{
		return false;
	}

	string prefix;
	string ignored_ud_suffix;
	char float_suffix = '\0';
	bool is_udl = false;
	if (ParseFloatingLiteralParts(source.substr(0, suffix_pos), prefix, ignored_ud_suffix, float_suffix, is_udl) && !is_udl)
	{
		output.emit_user_defined_literal_floating(source, source.substr(suffix_pos), prefix);
		return true;
	}

	IntegerSuffix integer_suffix;
	bool is_hex = false;
	bool is_octal = false;
	if (ParseIntegerLiteralParts(source.substr(0, suffix_pos), prefix, ignored_ud_suffix, integer_suffix, is_udl, is_hex, is_octal) && !is_udl)
	{
		output.emit_user_defined_literal_integer(source, source.substr(suffix_pos), prefix);
		return true;
	}

	return false;
}

struct MacroProcessor
{
	map<string, MacroDef> macros;

	vector<MacroToken> CopyWithBlacklist(const vector<MacroToken>& tokens, const set<string>& add) const
	{
		vector<MacroToken> out = tokens;
		for (MacroToken& token : out)
		{
			token.blacklist.insert(add.begin(), add.end());
		}
		return out;
	}

	void StampOrigin(vector<MacroToken>& tokens, const MacroToken& origin) const
	{
		for (MacroToken& token : tokens)
		{
			token.file = origin.file;
			token.line = origin.line;
		}
	}

	void CheckNoVaArgsInText(const vector<MacroToken>& tokens) const
	{
		for (const MacroToken& token : tokens)
		{
			if (IsVaArgsToken(token))
			{
				throw runtime_error("__VA_ARGS__ token in text-lines: " + token.source);
			}
		}
	}

	bool IsMacroParameter(const MacroDef& macro, const string& name) const
	{
		for (const string& param : macro.params)
		{
			if (param == name)
			{
				return true;
			}
		}
		return macro.variadic && name == "__VA_ARGS__";
	}

	vector<MacroToken> NormalizeReplacement(const vector<MacroToken>& input) const
	{
		return CollapseTextSequence(input);
	}

	void ValidateReplacement(const string& name, const MacroDef& macro) const
	{
		if (name == "__VA_ARGS__")
		{
			throw runtime_error("invalid __VA_ARGS__ use");
		}

		size_t first = NextNonWhitespace(macro.replacement, 0);
		size_t last = macro.replacement.empty() ? macro.replacement.size() : PreviousNonWhitespace(macro.replacement, macro.replacement.size());
		if (first < macro.replacement.size() && IsOpToken(macro.replacement[first], "##"))
		{
			throw runtime_error("## at edge of replacement list");
		}
		if (last < macro.replacement.size() && IsOpToken(macro.replacement[last], "##"))
		{
			throw runtime_error("## at edge of replacement list");
		}

		for (size_t i = 0; i < macro.replacement.size(); ++i)
		{
			const MacroToken& token = macro.replacement[i];
			if (IsVaArgsToken(token) && !macro.variadic)
			{
				throw runtime_error("invalid __VA_ARGS__ use");
			}
			if (!macro.function_like || !IsOpToken(token, "#"))
			{
				continue;
			}
			if (i + 1 >= macro.replacement.size())
			{
				throw runtime_error("# at end of function-like macro replacement list");
			}
			size_t next = NextNonWhitespace(macro.replacement, i + 1);
			if (next >= macro.replacement.size())
			{
				throw runtime_error("# at end of function-like macro replacement list");
			}
			if (!IsIdentifierToken(macro.replacement[next]) ||
				!IsMacroParameter(macro, macro.replacement[next].source))
			{
				throw runtime_error("# must be followed by parameter in function-like macro");
			}
		}
	}

	void ThrowWrongParamCount(const string& name) const
	{
		throw runtime_error("macro function-like invocation wrong num of params: " + name);
	}

	bool ParseInvocationArguments(const vector<MacroToken>& seq, size_t name_pos, const string& name,
		const MacroDef& macro, size_t& invoke_end, vector<vector<MacroToken>>& args) const
	{
		size_t j = name_pos + 1;
		while (j < seq.size() && seq[j].kind == PPT_WHITESPACE)
		{
			++j;
		}
		if (j >= seq.size() || !IsOpToken(seq[j], "("))
		{
			return false;
		}

		vector<MacroToken> current;
		int depth = 0;
		for (size_t k = j + 1; ; ++k)
		{
			if (k >= seq.size())
			{
				throw runtime_error("unterminated macro invocation");
			}

			if (IsOpToken(seq[k], "("))
			{
				++depth;
				current.push_back(seq[k]);
				continue;
			}
			if (IsOpToken(seq[k], ")"))
			{
				if (depth == 0)
				{
					args.push_back(CollapseTextSequence(current));
					invoke_end = k + 1;
					break;
				}
				--depth;
				current.push_back(seq[k]);
				continue;
			}
			if (depth == 0 && IsOpToken(seq[k], ","))
			{
				args.push_back(CollapseTextSequence(current));
				current.clear();
				continue;
			}
			current.push_back(seq[k]);
		}

		if (args.size() == 1 && args[0].empty() && macro.params.empty())
		{
			args.clear();
		}
		if (!macro.variadic)
		{
			if (args.size() != macro.params.size())
			{
				ThrowWrongParamCount(name);
			}
		}
		else if (args.size() < macro.params.size())
		{
			ThrowWrongParamCount(name);
		}
		return true;
	}

	vector<MacroToken> MakeVarArgsSequence(const vector<vector<MacroToken>>& args, size_t first) const
	{
		vector<MacroToken> out;
		for (size_t i = first; i < args.size(); ++i)
		{
			if (i != first)
			{
				out.push_back(MakeMacroToken(PPT_PREPROCESSING_OP_OR_PUNC, ","));
				out.push_back(MakeMacroToken(PPT_WHITESPACE, ""));
			}
			out.insert(out.end(), args[i].begin(), args[i].end());
		}
		return out;
	}

	vector<MacroToken> PasteTokens(vector<MacroToken> lhs, vector<MacroToken> rhs) const
	{
		if (lhs.empty())
		{
			return rhs;
		}
		if (rhs.empty())
		{
			return lhs;
		}

		MacroToken left = lhs.back();
		lhs.pop_back();
		MacroToken right = rhs.front();
		rhs.erase(rhs.begin());

		vector<MacroToken> pasted = TokenizeMacroText(left.source + right.source);
		set<string> blacklist = left.blacklist;
		blacklist.insert(right.blacklist.begin(), right.blacklist.end());
		for (MacroToken& token : pasted)
		{
			token.blacklist.insert(blacklist.begin(), blacklist.end());
			token.file = !left.file.empty() ? left.file : right.file;
			token.line = left.line != 0 ? left.line : right.line;
		}

		lhs.insert(lhs.end(), pasted.begin(), pasted.end());
		lhs.insert(lhs.end(), rhs.begin(), rhs.end());
		return lhs;
	}

	vector<MacroToken> ExpandFunctionReplacement(const MacroDef& macro,
		const vector<vector<MacroToken>>& args, const set<string>& add_blacklist, const MacroToken& origin)
	{
		map<string, vector<MacroToken>> original_args;
		map<string, vector<MacroToken>> raw_args;
		map<string, vector<MacroToken>> expanded_args;
		for (size_t i = 0; i < macro.params.size(); ++i)
		{
			vector<MacroToken> raw = i < args.size() ? args[i] : vector<MacroToken>();
			original_args[macro.params[i]] = raw;
			raw_args[macro.params[i]] = CopyWithBlacklist(raw, add_blacklist);
		}
		if (macro.variadic)
		{
			vector<MacroToken> raw_va = MakeVarArgsSequence(args, macro.params.size());
			original_args["__VA_ARGS__"] = raw_va;
			raw_args["__VA_ARGS__"] = CopyWithBlacklist(raw_va, add_blacklist);
		}

		vector<MacroToken> out;
		vector<MacroToken> current_piece;
		bool have_piece = false;
		bool pending_paste = false;

		for (size_t i = 0; i < macro.replacement.size(); )
		{
			if (IsWhitespaceToken(macro.replacement[i]))
			{
				size_t prev = PreviousNonWhitespace(macro.replacement, i);
				size_t next = NextNonWhitespace(macro.replacement, i + 1);
				if ((prev < macro.replacement.size() && IsOpToken(macro.replacement[prev], "##")) ||
					(next < macro.replacement.size() && IsOpToken(macro.replacement[next], "##")))
				{
					++i;
					continue;
				}
			}

			if (IsOpToken(macro.replacement[i], "##"))
			{
				pending_paste = true;
				++i;
				continue;
			}

			vector<MacroToken> piece;
			if (IsOpToken(macro.replacement[i], "#"))
			{
				size_t next = NextNonWhitespace(macro.replacement, i + 1);
				MacroToken stringized = MakeMacroToken(PPT_STRING_LITERAL, StringizeTokens(raw_args[macro.replacement[next].source]));
				stringized.blacklist.insert(add_blacklist.begin(), add_blacklist.end());
				stringized.file = origin.file;
				stringized.line = origin.line;
				piece.push_back(stringized);
				i = next + 1;
			}
			else
			{
				size_t prev = PreviousNonWhitespace(macro.replacement, i);
				size_t next = NextNonWhitespace(macro.replacement, i + 1);
				bool adjacent_paste =
					(prev < macro.replacement.size() && IsOpToken(macro.replacement[prev], "##")) ||
					(next < macro.replacement.size() && IsOpToken(macro.replacement[next], "##"));
				if (macro.replacement[i].kind == PPT_IDENTIFIER && IsMacroParameter(macro, macro.replacement[i].source))
				{
					const string& param = macro.replacement[i].source;
					if (adjacent_paste)
					{
						piece = raw_args[param];
					}
					else
					{
						if (expanded_args.count(param) == 0)
						{
							expanded_args[param] = CopyWithBlacklist(Expand(original_args[param]), add_blacklist);
						}
						piece = expanded_args[param];
					}
				}
				else
				{
					MacroToken copied = macro.replacement[i];
					copied.blacklist.insert(add_blacklist.begin(), add_blacklist.end());
					copied.file = origin.file;
					copied.line = origin.line;
					piece.push_back(copied);
				}
				++i;
			}

			if (!have_piece)
			{
				current_piece = piece;
				have_piece = true;
				continue;
			}
			if (pending_paste)
			{
				current_piece = PasteTokens(current_piece, piece);
				pending_paste = false;
				continue;
			}

			out.insert(out.end(), current_piece.begin(), current_piece.end());
			current_piece = piece;
		}

		if (have_piece)
		{
			out.insert(out.end(), current_piece.begin(), current_piece.end());
		}
		return out;
	}

	vector<MacroToken> ExpandObjectReplacement(const MacroDef& macro, const set<string>& add_blacklist, const MacroToken& origin) const
	{
		vector<MacroToken> out;
		vector<MacroToken> current_piece;
		bool have_piece = false;
		bool pending_paste = false;

		for (size_t i = 0; i < macro.replacement.size(); )
		{
			if (IsWhitespaceToken(macro.replacement[i]))
			{
				size_t prev = PreviousNonWhitespace(macro.replacement, i);
				size_t next = NextNonWhitespace(macro.replacement, i + 1);
				if ((prev < macro.replacement.size() && IsOpToken(macro.replacement[prev], "##")) ||
					(next < macro.replacement.size() && IsOpToken(macro.replacement[next], "##")))
				{
					++i;
					continue;
				}
			}

			if (IsOpToken(macro.replacement[i], "##"))
			{
				pending_paste = true;
				++i;
				continue;
			}

			MacroToken copied = macro.replacement[i];
			copied.blacklist.insert(add_blacklist.begin(), add_blacklist.end());
			copied.file = origin.file;
			copied.line = origin.line;
			vector<MacroToken> piece(1, copied);
			++i;

			if (!have_piece)
			{
				current_piece = piece;
				have_piece = true;
				continue;
			}
			if (pending_paste)
			{
				current_piece = PasteTokens(current_piece, piece);
				pending_paste = false;
				continue;
			}

			out.insert(out.end(), current_piece.begin(), current_piece.end());
			current_piece = piece;
		}

		if (have_piece)
		{
			out.insert(out.end(), current_piece.begin(), current_piece.end());
		}
		return out;
	}

	vector<MacroToken> Expand(vector<MacroToken> seq)
	{
		seq = CollapseTextSequence(seq);

		for (size_t i = 0; i < seq.size(); )
		{
			if (seq[i].kind != PPT_IDENTIFIER)
			{
				++i;
				continue;
			}

			if (seq[i].source == "__LINE__" && seq[i].line > 0)
			{
				vector<MacroToken> replacement(1, MakeMacroToken(PPT_PP_NUMBER, to_string(seq[i].line)));
				StampOrigin(replacement, seq[i]);
				seq.erase(seq.begin() + i, seq.begin() + i + 1);
				seq.insert(seq.begin() + i, replacement.begin(), replacement.end());
				continue;
			}
			if (seq[i].source == "__FILE__" && !seq[i].file.empty())
			{
				vector<MacroToken> replacement(1, MakeMacroToken(PPT_STRING_LITERAL, QuoteMacroStringLiteral(seq[i].file)));
				StampOrigin(replacement, seq[i]);
				seq.erase(seq.begin() + i, seq.begin() + i + 1);
				seq.insert(seq.begin() + i, replacement.begin(), replacement.end());
				continue;
			}

			auto it = macros.find(seq[i].source);
			if (it == macros.end())
			{
				++i;
				continue;
			}
			if (seq[i].blacklist.count(seq[i].source) != 0)
			{
				++i;
				continue;
			}

			const string name = seq[i].source;
			const MacroDef& macro = it->second;
			set<string> add_blacklist = seq[i].blacklist;
			add_blacklist.insert(name);

			size_t invoke_end = i + 1;
			vector<vector<MacroToken>> args;
			if (macro.function_like && !ParseInvocationArguments(seq, i, name, macro, invoke_end, args))
			{
				++i;
				continue;
			}

			vector<MacroToken> replacement = macro.function_like
				? ExpandFunctionReplacement(macro, args, add_blacklist, seq[i])
				: ExpandObjectReplacement(macro, add_blacklist, seq[i]);

			seq.erase(seq.begin() + i, seq.begin() + invoke_end);
			seq.insert(seq.begin() + i, replacement.begin(), replacement.end());
			seq = CollapseTextSequence(seq);
		}
		return seq;
	}

	MacroDef ParseDefine(const vector<MacroToken>& directive, size_t& pos)
	{
		MacroDef macro;
		if (pos >= directive.size() || directive[pos].kind != PPT_IDENTIFIER)
		{
			throw runtime_error("expected identifier");
		}

		string name = directive[pos].source;
		++pos;

		if (pos < directive.size() && directive[pos].kind != PPT_WHITESPACE && IsOpToken(directive[pos], "("))
		{
			macro.function_like = true;
			++pos;
			SkipWhitespace(directive, pos);
			if (pos < directive.size() && IsOpToken(directive[pos], ")"))
			{
				++pos;
			}
			else
			{
				while (true)
				{
					SkipWhitespace(directive, pos);
					if (pos < directive.size() && IsOpToken(directive[pos], "..."))
					{
						macro.variadic = true;
						++pos;
						SkipWhitespace(directive, pos);
						if (pos >= directive.size() || !IsOpToken(directive[pos], ")"))
						{
							throw runtime_error("expected rparen (#2): PP_NEW_LINE()");
						}
						++pos;
						break;
					}
					if (pos >= directive.size() || directive[pos].kind != PPT_IDENTIFIER)
					{
						throw runtime_error("expected identifier after lparen");
					}
					if (directive[pos].source == "__VA_ARGS__")
					{
						throw runtime_error("__VA_ARGS__ in macro parameter list");
					}
					for (const string& param : macro.params)
					{
						if (param == directive[pos].source)
						{
							throw runtime_error("duplicate parameter " + param + " in macro definition");
						}
					}
					macro.params.push_back(directive[pos].source);
					++pos;
					SkipWhitespace(directive, pos);
					if (pos < directive.size() && IsOpToken(directive[pos], ")"))
					{
						++pos;
						break;
					}
					if (pos >= directive.size() || !IsOpToken(directive[pos], ","))
					{
						throw runtime_error("expected rparen (#2): PP_NEW_LINE()");
					}
					++pos;
					SkipWhitespace(directive, pos);
					if (pos < directive.size() && IsOpToken(directive[pos], "..."))
					{
						macro.variadic = true;
						++pos;
						SkipWhitespace(directive, pos);
						if (pos >= directive.size() || !IsOpToken(directive[pos], ")"))
						{
							throw runtime_error("expected rparen (#2): PP_NEW_LINE()");
						}
						++pos;
						break;
					}
					if (pos >= directive.size() || directive[pos].kind != PPT_IDENTIFIER)
					{
						throw runtime_error("expected identifier");
					}
				}
			}
		}
		else
		{
			SkipWhitespace(directive, pos);
		}

		for (; pos < directive.size(); ++pos)
		{
			macro.replacement.push_back(directive[pos]);
		}
		macro.replacement = NormalizeReplacement(macro.replacement);
		ValidateReplacement(name, macro);
		return macro;
	}

	void HandleDirective(const vector<MacroToken>& directive)
	{
		size_t pos = 0;
		SkipWhitespace(directive, pos);
		if (pos >= directive.size() || !(IsOpToken(directive[pos], "#") || directive[pos].source == "%:"))
		{
			throw runtime_error("expected directive");
		}
		++pos;
		SkipWhitespace(directive, pos);
		if (pos >= directive.size() || directive[pos].kind != PPT_IDENTIFIER)
		{
			throw runtime_error("expected directive name");
		}

		string kind = directive[pos].source;
		++pos;
		SkipWhitespace(directive, pos);

		if (kind == "define")
		{
			if (pos >= directive.size() || directive[pos].kind != PPT_IDENTIFIER)
			{
				throw runtime_error("expected identifier");
			}
			string name = directive[pos].source;
			MacroDef macro = ParseDefine(directive, pos);
			auto it = macros.find(name);
			if (it != macros.end() && !SameMacro(it->second, macro))
			{
				throw runtime_error("macro redefined");
			}
			macros[name] = macro;
			return;
		}

		if (kind == "undef")
		{
			if (pos >= directive.size() || directive[pos].kind != PPT_IDENTIFIER)
			{
				throw runtime_error("expected identifier");
			}
			string name = directive[pos].source;
			if (name == "__VA_ARGS__")
			{
				throw runtime_error("invalid __VA_ARGS__ use");
			}
			++pos;
			SkipWhitespace(directive, pos);
			if (pos != directive.size())
			{
				throw runtime_error("expected new line");
			}
			macros.erase(name);
			return;
		}

		throw runtime_error("unsupported directive");
	}
};

#ifndef CPPGM_EMBED_MACRO
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

			MacroProcessor processor;
			DebugPostTokenOutputStream output;

			vector<MacroToken> line;
			vector<MacroToken> pending_text;

			auto flush_text = [&]()
			{
				if (pending_text.empty())
				{
					return;
				}
				processor.CheckNoVaArgsInText(pending_text);
				vector<MacroToken> expanded = processor.Expand(pending_text);
				EmitPostTokens(output, expanded);
				pending_text.clear();
			};

		for (const PPToken& ppt : pp_output.tokens)
		{
			MacroToken token = MakeMacroToken(ppt.kind, ppt.source);

			if (ppt.kind == PPT_EOF)
			{
				break;
			}

			if (ppt.kind == PPT_NEWLINE)
			{
				bool directive = false;
				size_t pos = 0;
				if (pos < line.size() && line[pos].kind == PPT_WHITESPACE)
				{
					++pos;
				}
				if (pos < line.size() && (IsOpToken(line[pos], "#") || line[pos].source == "%:"))
				{
					directive = true;
				}

				if (directive)
				{
					flush_text();
					processor.HandleDirective(line);
				}
				else
				{
					pending_text.insert(pending_text.end(), line.begin(), line.end());
					pending_text.push_back(MakeMacroToken(PPT_NEWLINE, ""));
					}
					line.clear();
					continue;
				}

				line.push_back(token);
			}

		if (!line.empty())
		{
			bool directive = false;
			size_t pos = 0;
			if (pos < line.size() && line[pos].kind == PPT_WHITESPACE)
			{
				++pos;
			}
			if (pos < line.size() && (IsOpToken(line[pos], "#") || line[pos].source == "%:"))
			{
				directive = true;
			}
			if (directive)
			{
				flush_text();
				processor.HandleDirective(line);
			}
			else
			{
				pending_text.insert(pending_text.end(), line.begin(), line.end());
			}
		}

		flush_text();
		output.emit_eof();
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
