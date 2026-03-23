// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <ctime>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

#define CPPGM_EMBED_CTRLEXPR 1
#include "ctrlexpr.cpp"
#define CPPGM_EMBED_MACRO 1
#include "macro.cpp"

struct IfState
{
	bool parent_active;
	bool current_active;
	bool any_taken;
	bool saw_else;
};

struct SourceFrame
{
	string actual_path;
	string presumed_file;
	int line;
};

struct PreprocContext
{
	MacroProcessor processor;
	set<PA5FileId> pragma_once_files;
	vector<MacroToken> output_tokens;
	string build_date_literal;
	string build_time_literal;
};

struct SourcedPPToken
{
	PPToken token;
	int line;
};

struct LogicalLine
{
	vector<PPToken> tokens;
	int line;
	int next_line;
};

struct CoutRedirectGuard
{
	streambuf* old;

	explicit CoutRedirectGuard(streambuf* replacement)
		: old(cout.rdbuf(replacement))
	{}

	~CoutRedirectGuard()
	{
		cout.rdbuf(old);
	}
};

MacroDef MakeObjectMacro(const vector<MacroToken>& replacement)
{
	MacroDef macro;
	macro.replacement = replacement;
	return macro;
}

vector<MacroToken> MakeReplacement(EPPTokenKind kind, const string& source)
{
	return vector<MacroToken>(1, MakeMacroToken(kind, source));
}

vector<MacroToken> ToMacroTokens(const vector<PPToken>& tokens, const SourceFrame* frame = nullptr)
{
	vector<MacroToken> out;
	for (const PPToken& token : tokens)
	{
		MacroToken macro = MakeMacroToken(token.kind, token.source);
		if (frame)
		{
			macro.file = frame->presumed_file;
			macro.line = frame->line;
		}
		out.push_back(macro);
	}
	return out;
}

vector<PPToken> ToPPTokens(const vector<MacroToken>& tokens)
{
	vector<PPToken> out;
	for (const MacroToken& token : tokens)
	{
		out.push_back(PPToken{token.kind, token.source});
	}
	return out;
}

vector<LogicalLine> SplitLines(const vector<SourcedPPToken>& tokens)
{
	vector<LogicalLine> lines;
	vector<PPToken> current;
	int current_start = 1;
	for (const SourcedPPToken& sourced : tokens)
	{
		if (sourced.token.kind == PPT_EOF)
		{
			break;
		}
		if (sourced.token.kind == PPT_NEWLINE)
		{
			lines.push_back(LogicalLine{current, current_start, sourced.line + 1});
			current.clear();
			current_start = sourced.line + 1;
			continue;
		}
		current.push_back(sourced.token);
	}
	if (!current.empty())
	{
		lines.push_back(LogicalLine{current, current_start, current_start + 1});
	}
	return lines;
}

struct TrackingPPTokenStream : IPPTokenStream
{
	vector<SourcedPPToken> tokens;
	int* current_line;

	explicit TrackingPPTokenStream(int& line)
		: current_line(&line)
	{}

	void push(EPPTokenKind kind, const string& source)
	{
		tokens.push_back(SourcedPPToken{PPToken{kind, source}, *current_line});
	}

	void emit_whitespace_sequence() override { push(PPT_WHITESPACE, ""); }
	void emit_new_line() override { push(PPT_NEWLINE, ""); }
	void emit_header_name(const string& data) override { push(PPT_HEADER_NAME, data); }
	void emit_identifier(const string& data) override { push(PPT_IDENTIFIER, data); }
	void emit_pp_number(const string& data) override { push(PPT_PP_NUMBER, data); }
	void emit_character_literal(const string& data) override { push(PPT_CHARACTER_LITERAL, data); }
	void emit_user_defined_character_literal(const string& data) override { push(PPT_USER_DEFINED_CHARACTER_LITERAL, data); }
	void emit_string_literal(const string& data) override { push(PPT_STRING_LITERAL, data); }
	void emit_user_defined_string_literal(const string& data) override { push(PPT_USER_DEFINED_STRING_LITERAL, data); }
	void emit_preprocessing_op_or_punc(const string& data) override { push(PPT_PREPROCESSING_OP_OR_PUNC, data); }
	void emit_non_whitespace_char(const string& data) override { push(PPT_NON_WHITESPACE_CHARACTER, data); }
	void emit_eof() override { push(PPT_EOF, ""); }
};

vector<SourcedPPToken> TokenizeSource(const string& source)
{
	int current_line = 1;
	TrackingPPTokenStream output(current_line);
	PPTokenizer tokenizer(output);
	for (unsigned char c : source)
	{
		tokenizer.process(c);
		if (c == '\n')
		{
			++current_line;
		}
	}
	tokenizer.process(EndOfFile);
	return output.tokens;
}

vector<PPToken> TokenizePlainSource(const string& source)
{
	CollectingPPTokenStream output;
	PPTokenizer tokenizer(output);
	for (unsigned char c : source)
	{
		tokenizer.process(c);
	}
	tokenizer.process(EndOfFile);
	return output.tokens;
}

vector<pair<int, int>> ComputeLogicalLineSpans(const string& source)
{
	enum State
	{
		ST_NORMAL,
		ST_LINE_COMMENT,
		ST_BLOCK_COMMENT,
		ST_STRING,
		ST_CHAR
	};

	State state = ST_NORMAL;
	int current_line = 1;
	int current_start = 1;
	bool saw_non_spliced = false;
	vector<pair<int, int>> spans;

	for (size_t i = 0; i < source.size(); )
	{
		if (source[i] == '\\' && i + 1 < source.size() && source[i + 1] == '\n')
		{
			i += 2;
			++current_line;
			saw_non_spliced = true;
			continue;
		}

		if (state == ST_NORMAL)
		{
			if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '/')
			{
				state = ST_LINE_COMMENT;
				i += 2;
				continue;
			}
			if (source[i] == '/' && i + 1 < source.size() && source[i + 1] == '*')
			{
				state = ST_BLOCK_COMMENT;
				i += 2;
				continue;
			}
			if (source[i] == '"')
			{
				state = ST_STRING;
				++i;
				continue;
			}
			if (source[i] == '\'')
			{
				state = ST_CHAR;
				++i;
				continue;
			}
			if (source[i] == '\n')
			{
				spans.push_back(make_pair(current_start, current_line + 1));
				++current_line;
				current_start = current_line;
				++i;
				saw_non_spliced = false;
				continue;
			}
			saw_non_spliced = true;
			++i;
			continue;
		}

		if (state == ST_LINE_COMMENT)
		{
			if (source[i] == '\n')
			{
				spans.push_back(make_pair(current_start, current_line + 1));
				++current_line;
				current_start = current_line;
				state = ST_NORMAL;
				++i;
				saw_non_spliced = false;
				continue;
			}
			++i;
			continue;
		}

		if (state == ST_BLOCK_COMMENT)
		{
			if (source[i] == '\n')
			{
				++current_line;
				saw_non_spliced = true;
				++i;
				continue;
			}
			if (source[i] == '*' && i + 1 < source.size() && source[i + 1] == '/')
			{
				state = ST_NORMAL;
				i += 2;
				continue;
			}
			++i;
			continue;
		}

		if (source[i] == '\\' && i + 1 < source.size())
		{
			i += 2;
			continue;
		}
		if ((state == ST_STRING && source[i] == '"') || (state == ST_CHAR && source[i] == '\''))
		{
			state = ST_NORMAL;
			++i;
			continue;
		}
		if (source[i] == '\n')
		{
			++current_line;
		}
		++i;
	}

	if (saw_non_spliced)
	{
		spans.push_back(make_pair(current_start, current_line + 1));
	}
	return spans;
}

bool IsDirectiveLine(const vector<PPToken>& line, size_t& hash_pos)
{
	hash_pos = 0;
	while (hash_pos < line.size() && line[hash_pos].kind == PPT_WHITESPACE)
	{
		++hash_pos;
	}
	return hash_pos < line.size() &&
		line[hash_pos].kind == PPT_PREPROCESSING_OP_OR_PUNC &&
		(line[hash_pos].source == "#" || line[hash_pos].source == "%:");
}

void SkipWhitespaceTokens(const vector<PPToken>& line, size_t& pos)
{
	while (pos < line.size() && line[pos].kind == PPT_WHITESPACE)
	{
		++pos;
	}
}

string ParseDecodedStringLiteral(const string& source)
{
	StringLiteralPiece piece;
	if (!ParseStringLiteralPiece(source, piece) || !piece.valid || piece.user_defined)
	{
		throw runtime_error("invalid string literal");
	}

	vector<char> bytes;
	if (!EncodeCodePointsUtf8(piece.code_points, bytes) || bytes.empty())
	{
		throw runtime_error("invalid string literal");
	}

	return string(bytes.data(), bytes.data() + bytes.size() - 1);
}

string ParseIncludeTargetToken(const PPToken& token)
{
	if (token.kind == PPT_HEADER_NAME)
	{
		if (token.source.size() < 2)
		{
			throw runtime_error("invalid include");
		}
		return token.source.substr(1, token.source.size() - 2);
	}
	if (token.kind == PPT_STRING_LITERAL)
	{
		return ParseDecodedStringLiteral(token.source);
	}
	throw runtime_error("invalid include");
}

string QuoteStringLiteral(const string& text)
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

bool ParsePositiveLineNumber(const string& source, int& value)
{
	string prefix;
	string ud_suffix;
	IntegerSuffix suffix;
	bool is_udl = false;
	bool is_hex = false;
	bool is_octal = false;
	if (!ParseIntegerLiteralParts(source, prefix, ud_suffix, suffix, is_udl, is_hex, is_octal) ||
		is_udl || suffix.is_unsigned || suffix.long_count != 0)
	{
		return false;
	}

	unsigned __int128 parsed = 0;
	int base = 10;
	size_t digits_start = 0;
	if (prefix.size() >= 2 && prefix[0] == '0' && (prefix[1] == 'x' || prefix[1] == 'X'))
	{
		base = 16;
		digits_start = 2;
	}
	else if (prefix.size() > 1 && prefix[0] == '0')
	{
		base = 8;
		digits_start = 1;
	}
	if (!ParseUnsignedIntegerValue(prefix.substr(digits_start), base, parsed) || parsed == 0)
	{
		return false;
	}

	value = static_cast<int>(parsed);
	return true;
}

void SetDynamicPredefinedMacros(PreprocContext& ctx, const SourceFrame& frame)
{
	ctx.processor.macros["__CPPGM__"] = MakeObjectMacro(MakeReplacement(PPT_PP_NUMBER, "201303L"));
	ctx.processor.macros["__cplusplus"] = MakeObjectMacro(MakeReplacement(PPT_PP_NUMBER, "201103L"));
	ctx.processor.macros["__STDC_HOSTED__"] = MakeObjectMacro(MakeReplacement(PPT_PP_NUMBER, "1"));
	ctx.processor.macros["__CPPGM_AUTHOR__"] = MakeObjectMacro(MakeReplacement(PPT_STRING_LITERAL, "\"OpenAI\""));
	ctx.processor.macros["__DATE__"] = MakeObjectMacro(MakeReplacement(PPT_STRING_LITERAL, ctx.build_date_literal));
	ctx.processor.macros["__TIME__"] = MakeObjectMacro(MakeReplacement(PPT_STRING_LITERAL, ctx.build_time_literal));
}

bool CurrentActive(const vector<IfState>& stack)
{
	return stack.empty() ? true : stack.back().current_active;
}

bool IsMacroDefined(const PreprocContext& ctx, const string& name)
{
	if (name == "__FILE__" || name == "__LINE__")
	{
		return true;
	}
	return ctx.processor.macros.find(name) != ctx.processor.macros.end();
}

PreprocContext* g_defined_context = nullptr;

bool PreprocIsDefinedIdentifier(const string& identifier)
{
	return g_defined_context && IsMacroDefined(*g_defined_context, identifier);
}

vector<MacroToken> ReplaceDefinedOperator(const vector<PPToken>& line, size_t begin, PreprocContext& ctx, const SourceFrame& frame)
{
	vector<MacroToken> out;
	for (size_t i = begin; i < line.size(); )
	{
		if (line[i].kind == PPT_IDENTIFIER && line[i].source == "defined")
		{
			size_t pos = i + 1;
			SkipWhitespaceTokens(line, pos);
			string identifier;
			if (pos < line.size() && line[pos].kind == PPT_PREPROCESSING_OP_OR_PUNC && line[pos].source == "(")
			{
				++pos;
				SkipWhitespaceTokens(line, pos);
				if (pos >= line.size() || line[pos].kind != PPT_IDENTIFIER)
				{
					throw runtime_error("invalid defined operator");
				}
				identifier = line[pos].source;
				++pos;
				SkipWhitespaceTokens(line, pos);
				if (pos >= line.size() || line[pos].kind != PPT_PREPROCESSING_OP_OR_PUNC || line[pos].source != ")")
				{
					throw runtime_error("invalid defined operator");
				}
				++pos;
			}
			else
			{
				if (pos >= line.size() || line[pos].kind != PPT_IDENTIFIER)
				{
					throw runtime_error("invalid defined operator");
				}
				identifier = line[pos].source;
				++pos;
			}
			MacroToken replaced = MakeMacroToken(PPT_PP_NUMBER, IsMacroDefined(ctx, identifier) ? "1" : "0");
			replaced.file = frame.presumed_file;
			replaced.line = frame.line;
			out.push_back(replaced);
			i = pos;
			continue;
		}
		MacroToken token = MakeMacroToken(line[i].kind, line[i].source);
		token.file = frame.presumed_file;
		token.line = frame.line;
		out.push_back(token);
		++i;
	}
	return out;
}

bool EvaluateIfExpression(const vector<PPToken>& line, size_t begin, PreprocContext& ctx, const SourceFrame& frame)
{
	vector<MacroToken> expr = ReplaceDefinedOperator(line, begin, ctx, frame);
	vector<MacroToken> expanded = ctx.processor.Expand(expr);
	vector<PPToken> tokens = ToPPTokens(expanded);

	g_defined_context = &ctx;
	CPPGM_IsDefinedIdentifierHook = PreprocIsDefinedIdentifier;
	string result;
	EvaluateLine(tokens, result);
	CPPGM_IsDefinedIdentifierHook = nullptr;
	g_defined_context = nullptr;

	if (result == "error")
	{
		throw runtime_error("invalid #if expression");
	}
	return result != "0" && result != "0u";
}

string ResolveInclude(const string& current_file, const string& nextf)
{
	string pathrel;
	size_t slash = current_file.rfind('/');
	if (slash != string::npos)
	{
		pathrel = current_file.substr(0, slash + 1) + nextf;
		ifstream rel(pathrel.c_str());
		if (rel.good())
		{
			return pathrel;
		}
	}

	ifstream direct(nextf.c_str());
	if (direct.good())
	{
		return nextf;
	}

	throw runtime_error("include file not found");
}

void ExecutePragmaText(PreprocContext& ctx, const string& pragma_text, const SourceFrame& frame)
{
	if (pragma_text == "once")
	{
		PA5FileId fileid;
		if (PA5GetFileId(frame.actual_path, fileid))
		{
			ctx.pragma_once_files.insert(fileid);
		}
		return;
	}
	if (pragma_text.find("cppgm_mock_unknown") == 0)
	{
		return;
	}
}

void ExecutePragmaDirective(PreprocContext& ctx, const vector<PPToken>& line, size_t begin, const SourceFrame& frame)
{
	vector<PPToken> payload(line.begin() + begin, line.end());
	if (payload.empty())
	{
		return;
	}

	vector<MacroToken> tokens = ctx.processor.Expand(ToMacroTokens(payload, &frame));
	vector<PPToken> expanded = ToPPTokens(tokens);
	size_t pos = 0;
	SkipWhitespaceTokens(expanded, pos);
	if (pos >= expanded.size() || expanded[pos].kind != PPT_IDENTIFIER)
	{
		return;
	}
	string text = expanded[pos].source;
	++pos;
	for (; pos < expanded.size(); ++pos)
	{
		text += expanded[pos].kind == PPT_WHITESPACE ? " " : " " + expanded[pos].source;
	}
	ExecutePragmaText(ctx, text, frame);
}

void ExpandPragmasInText(PreprocContext& ctx, vector<MacroToken>& tokens, const SourceFrame& frame)
{
	vector<MacroToken> out;
	for (size_t i = 0; i < tokens.size(); )
	{
		if (!IsIdentifierToken(tokens[i], "_Pragma"))
		{
			out.push_back(tokens[i]);
			++i;
			continue;
		}

		size_t pos = i + 1;
		while (pos < tokens.size() && tokens[pos].kind == PPT_WHITESPACE)
		{
			++pos;
		}
		if (pos >= tokens.size() || !IsOpToken(tokens[pos], "("))
		{
			throw runtime_error("invalid _Pragma use");
		}
		++pos;
		while (pos < tokens.size() && tokens[pos].kind == PPT_WHITESPACE)
		{
			++pos;
		}
		if (pos >= tokens.size() || (tokens[pos].kind != PPT_STRING_LITERAL && tokens[pos].kind != PPT_USER_DEFINED_STRING_LITERAL))
		{
			throw runtime_error("invalid _Pragma use");
		}
		string pragma_text = ParseDecodedStringLiteral(tokens[pos].source);
		++pos;
		while (pos < tokens.size() && tokens[pos].kind == PPT_WHITESPACE)
		{
			++pos;
		}
		if (pos >= tokens.size() || !IsOpToken(tokens[pos], ")"))
		{
			throw runtime_error("invalid _Pragma use");
		}
		++pos;

		ExecutePragmaText(ctx, pragma_text, frame);
		i = pos;
	}
	tokens.swap(out);
}

string EmitFileOutput(const vector<MacroToken>& tokens)
{
	ostringstream oss;
	CoutRedirectGuard guard(oss.rdbuf());
	try
	{
		DebugPostTokenOutputStream output;
		EmitPostTokens(output, tokens);
		output.emit_eof();
	}
	catch (...)
	{
		throw;
	}

	if (oss.str().find("invalid ") != string::npos)
	{
		throw runtime_error("invalid token");
	}
	return oss.str();
}

void ProcessFile(PreprocContext& ctx, const string& actual_path, const string& presumed_file);

void HandleIncludeDirective(PreprocContext& ctx, const vector<PPToken>& line, size_t begin, const SourceFrame& frame)
{
	vector<MacroToken> expanded = ctx.processor.Expand(ToMacroTokens(vector<PPToken>(line.begin() + begin, line.end()), &frame));
	vector<PPToken> tokens = ToPPTokens(expanded);
	size_t pos = 0;
	SkipWhitespaceTokens(tokens, pos);
	if (pos >= tokens.size())
	{
		throw runtime_error("invalid include");
	}
	string nextf = ParseIncludeTargetToken(tokens[pos]);
	string resolved = ResolveInclude(frame.presumed_file, nextf);

	PA5FileId fileid;
	if (PA5GetFileId(resolved, fileid) && ctx.pragma_once_files.count(fileid) != 0)
	{
		return;
	}

	ProcessFile(ctx, resolved, resolved);
}

void HandleLineDirective(PreprocContext& ctx, const vector<PPToken>& line, size_t begin, const SourceFrame& frame, int& next_line, string& next_file)
{
	vector<MacroToken> expanded = ctx.processor.Expand(ToMacroTokens(vector<PPToken>(line.begin() + begin, line.end()), &frame));
	vector<PPToken> tokens = ToPPTokens(expanded);
	size_t pos = 0;
	SkipWhitespaceTokens(tokens, pos);
	if (pos >= tokens.size() || tokens[pos].kind != PPT_PP_NUMBER)
	{
		throw runtime_error("invalid #line");
	}

	int parsed_line = 0;
	if (!ParsePositiveLineNumber(tokens[pos].source, parsed_line))
	{
		throw runtime_error("invalid #line");
	}
	next_line = parsed_line;
	++pos;
	SkipWhitespaceTokens(tokens, pos);
	if (pos < tokens.size())
	{
		if (tokens[pos].kind != PPT_STRING_LITERAL)
		{
			throw runtime_error("invalid #line");
		}
		next_file = ParseDecodedStringLiteral(tokens[pos].source);
		++pos;
		SkipWhitespaceTokens(tokens, pos);
	}
	if (pos != tokens.size())
	{
		throw runtime_error("invalid #line");
	}
}

void FlushPendingText(PreprocContext& ctx, vector<MacroToken>& pending_text, const SourceFrame& frame)
{
	if (pending_text.empty())
	{
		return;
	}
	ctx.processor.CheckNoVaArgsInText(pending_text);
	vector<MacroToken> expanded = ctx.processor.Expand(pending_text);
	ExpandPragmasInText(ctx, expanded, frame);
	ctx.output_tokens.insert(ctx.output_tokens.end(), expanded.begin(), expanded.end());
	pending_text.clear();
}

void ProcessFile(PreprocContext& ctx, const string& actual_path, const string& presumed_file)
{
	ifstream in(actual_path.c_str());
	if (!in)
	{
		throw runtime_error("unable to open file");
	}

	ostringstream contents;
	contents << in.rdbuf();
	string source = contents.str();
	vector<LogicalLine> token_lines = SplitLines(TokenizeSource(source));
	vector<pair<int, int>> spans = ComputeLogicalLineSpans(source);
	vector<LogicalLine> lines;
	for (size_t i = 0; i < token_lines.size(); ++i)
	{
		int start = i < spans.size() ? spans[i].first : static_cast<int>(i + 1);
		int next = i < spans.size() ? spans[i].second : start + 1;
		lines.push_back(LogicalLine{token_lines[i].tokens, start, next});
	}

	vector<IfState> if_stack;
	vector<MacroToken> pending_text;
	SourceFrame frame{actual_path, presumed_file, 1};
	int line_offset = 0;

	for (const LogicalLine& logical_line : lines)
	{
		const vector<PPToken>& line = logical_line.tokens;
		frame.line = logical_line.line + line_offset;
		SetDynamicPredefinedMacros(ctx, frame);
		bool active = CurrentActive(if_stack);
		size_t hash_pos = 0;
		bool is_directive = IsDirectiveLine(line, hash_pos);

		int next_line = logical_line.next_line + line_offset;
		string next_file = frame.presumed_file;

		if (is_directive)
		{
			size_t pos = hash_pos + 1;
			SkipWhitespaceTokens(line, pos);
			if (pos >= line.size())
			{
				frame.line = next_line;
				frame.presumed_file = next_file;
				continue;
			}
			if (line[pos].kind != PPT_IDENTIFIER)
			{
				if (active)
				{
					throw runtime_error("active non-directive");
				}
				frame.line = next_line;
				frame.presumed_file = next_file;
				continue;
			}

			string kind = line[pos].source;
			++pos;
			SkipWhitespaceTokens(line, pos);

			if (kind == "if")
			{
				FlushPendingText(ctx, pending_text, frame);
				bool cond = active ? EvaluateIfExpression(line, pos, ctx, frame) : false;
				if_stack.push_back(IfState{active, active && cond, active && cond, false});
			}
			else if (kind == "ifdef")
			{
				FlushPendingText(ctx, pending_text, frame);
				if (pos >= line.size() || line[pos].kind != PPT_IDENTIFIER)
				{
					throw runtime_error("invalid #ifdef");
				}
				bool cond = active && IsMacroDefined(ctx, line[pos].source);
				if_stack.push_back(IfState{active, cond, cond, false});
			}
			else if (kind == "ifndef")
			{
				FlushPendingText(ctx, pending_text, frame);
				if (pos >= line.size() || line[pos].kind != PPT_IDENTIFIER)
				{
					throw runtime_error("invalid #ifndef");
				}
				bool cond = active && !IsMacroDefined(ctx, line[pos].source);
				if_stack.push_back(IfState{active, cond, cond, false});
			}
			else if (kind == "elif")
			{
				FlushPendingText(ctx, pending_text, frame);
				if (if_stack.empty() || if_stack.back().saw_else)
				{
					throw runtime_error("invalid #elif");
				}
				IfState& state = if_stack.back();
				if (!state.parent_active || state.any_taken)
				{
					state.current_active = false;
				}
				else
				{
					bool cond = EvaluateIfExpression(line, pos, ctx, frame);
					state.current_active = cond;
					state.any_taken = cond;
				}
			}
			else if (kind == "else")
			{
				FlushPendingText(ctx, pending_text, frame);
				if (if_stack.empty() || if_stack.back().saw_else)
				{
					throw runtime_error("invalid #else");
				}
				IfState& state = if_stack.back();
				state.saw_else = true;
				state.current_active = state.parent_active && !state.any_taken;
				state.any_taken = true;
			}
			else if (kind == "endif")
			{
				FlushPendingText(ctx, pending_text, frame);
				if (if_stack.empty())
				{
					throw runtime_error("invalid #endif");
				}
				if_stack.pop_back();
			}
			else if (!active)
			{
				// ignored in inactive sections
			}
			else if (kind == "define" || kind == "undef")
			{
				FlushPendingText(ctx, pending_text, frame);
				ctx.processor.HandleDirective(ToMacroTokens(line));
			}
			else if (kind == "include")
			{
				FlushPendingText(ctx, pending_text, frame);
				HandleIncludeDirective(ctx, line, pos, frame);
			}
			else if (kind == "line")
			{
				FlushPendingText(ctx, pending_text, frame);
				HandleLineDirective(ctx, line, pos, frame, next_line, next_file);
			}
			else if (kind == "error")
			{
				FlushPendingText(ctx, pending_text, frame);
				string payload;
				for (size_t i = pos; i < line.size(); ++i)
				{
					if (line[i].kind == PPT_WHITESPACE)
					{
						if (!payload.empty() && payload[payload.size() - 1] != ' ')
						{
							payload += ' ';
						}
					}
					else
					{
						payload += line[i].source;
					}
				}
				throw runtime_error("#error \"" + payload + "\"");
			}
			else if (kind == "pragma")
			{
				FlushPendingText(ctx, pending_text, frame);
				ExecutePragmaDirective(ctx, line, pos, frame);
			}
			else
			{
				throw runtime_error("active non-directive");
			}
		}
		else if (active)
		{
			vector<MacroToken> line_tokens = ToMacroTokens(line, &frame);
			pending_text.insert(pending_text.end(), line_tokens.begin(), line_tokens.end());
			pending_text.push_back(MakeMacroToken(PPT_NEWLINE, ""));
			pending_text.back().file = frame.presumed_file;
			pending_text.back().line = frame.line;
		}

		line_offset = next_line - logical_line.next_line;
		frame.line = next_line;
		frame.presumed_file = next_file;
	}
	FlushPendingText(ctx, pending_text, frame);

	if (!if_stack.empty())
	{
		throw runtime_error("unterminated conditional inclusion");
	}
}

string MakeAsctimeLiteralSlice(const tm* info, size_t begin, size_t len)
{
	string text = asctime(const_cast<tm*>(info));
	return "\"" + text.substr(begin, len) + "\"";
}

#ifndef CPPGM_EMBED_PREPROC
int main(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int i = 1; i < argc; ++i)
		{
			args.emplace_back(argv[i]);
		}

		if (args.size() < 3 || args[0] != "-o")
		{
			throw logic_error("invalid usage");
		}

		string outfile = args[1];
		size_t nsrcfiles = args.size() - 2;

		time_t now = time(nullptr);
		tm* build_tm = localtime(&now);
		string build_date = MakeAsctimeLiteralSlice(build_tm, 4, 11);
		string build_time = "\"" + string(asctime(build_tm)).substr(11, 8) + "\"";

		ofstream out(outfile.c_str());
		CoutRedirectGuard guard(out.rdbuf());

		cout << "preproc " << nsrcfiles << endl;

		for (size_t i = 0; i < nsrcfiles; ++i)
		{
			string srcfile = args[i + 2];
			cout << "sof " << srcfile << endl;

			PreprocContext ctx;
			ctx.build_date_literal = build_date;
			ctx.build_time_literal = build_time;

			ProcessFile(ctx, srcfile, srcfile);
			cout << EmitFileOutput(ctx.output_tokens);
		}
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
