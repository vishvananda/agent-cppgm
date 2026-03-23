// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

#define CPPGM_EMBED_PREPROC 1
#include "preproc.cpp"

bool PA6_IsClassName(const string& identifier)
{
	return identifier.find('C') != string::npos;
}

bool PA6_IsTemplateName(const string& identifier)
{
	return identifier.find('T') != string::npos;
}

bool PA6_IsTypedefName(const string& identifier)
{
	return identifier.find('Y') != string::npos;
}

bool PA6_IsEnumName(const string& identifier)
{
	return identifier.find('E') != string::npos;
}

bool PA6_IsNamespaceName(const string& identifier)
{
	return identifier.find('N') != string::npos;
}

struct RecogToken
{
	string source;
	bool is_identifier = false;
	bool is_literal = false;
	bool is_empty_string = false;
	bool is_zero = false;
	bool has_simple = false;
	ETokenType simple = OP_PLUS;
	bool is_rshift_1 = false;
	bool is_rshift_2 = false;
	bool is_eof = false;
};

string TrimLeft(const string& text)
{
	size_t pos = 0;
	while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
	{
		++pos;
	}
	return text.substr(pos);
}

string Trim(const string& text)
{
	size_t begin = 0;
	size_t end = text.size();
	while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r'))
	{
		++begin;
	}
	while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
	{
		--end;
	}
	return text.substr(begin, end - begin);
}

bool StartsWith(const string& text, const string& prefix)
{
	return text.size() >= prefix.size() &&
		equal(prefix.begin(), prefix.end(), text.begin());
}

void PushSimpleToken(vector<RecogToken>& out, const string& source, ETokenType token_type)
{
	if (token_type == OP_RSHIFT)
	{
		RecogToken first;
		first.source = source;
		first.is_rshift_1 = true;
		out.push_back(first);

		RecogToken second;
		second.source = source;
		second.is_rshift_2 = true;
		out.push_back(second);
		return;
	}

	RecogToken token;
	token.source = source;
	token.has_simple = true;
	token.simple = token_type;
	out.push_back(token);
}

void PushIdentifierToken(vector<RecogToken>& out, const string& source)
{
	RecogToken token;
	token.source = source;
	token.is_identifier = true;
	out.push_back(token);
}

void PushLiteralToken(vector<RecogToken>& out, const string& source)
{
	RecogToken token;
	token.source = source;
	token.is_literal = true;
	token.is_empty_string = source == "\"\"";
	token.is_zero = source == "0";
	out.push_back(token);
}

string CapturePostTokenOutput(const function<void(DebugPostTokenOutputStream&)>& emit)
{
	DebugPostTokenOutputStream output;
	ostringstream oss;
	{
		CoutRedirectGuard guard(oss.rdbuf());
		emit(output);
	}

	string text = oss.str();
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
	{
		text.pop_back();
	}
	return text;
}

struct GrammarNode
{
	enum Kind
	{
		GNK_SYMBOL,
		GNK_SEQUENCE,
		GNK_OPTIONAL,
		GNK_STAR,
		GNK_PLUS
	};

	Kind kind = GNK_SYMBOL;
	string symbol;
	vector<shared_ptr<GrammarNode>> children;
};

shared_ptr<GrammarNode> MakeGrammarNodeSymbol(const string& symbol)
{
	shared_ptr<GrammarNode> node(new GrammarNode);
	node->kind = GrammarNode::GNK_SYMBOL;
	node->symbol = symbol;
	return node;
}

shared_ptr<GrammarNode> MakeGrammarNodeSequence(const vector<shared_ptr<GrammarNode>>& children)
{
	if (children.size() == 1)
	{
		return children[0];
	}

	shared_ptr<GrammarNode> node(new GrammarNode);
	node->kind = GrammarNode::GNK_SEQUENCE;
	node->children = children;
	return node;
}

shared_ptr<GrammarNode> MakeGrammarNodeUnary(GrammarNode::Kind kind, const shared_ptr<GrammarNode>& child)
{
	shared_ptr<GrammarNode> node(new GrammarNode);
	node->kind = kind;
	node->children.push_back(child);
	return node;
}

vector<string> TokenizeGrammarBody(const string& text)
{
	vector<string> tokens;
	for (size_t i = 0; i < text.size(); )
	{
		char c = text[i];
		if (c == ' ' || c == '\t' || c == '\r')
		{
			++i;
			continue;
		}

		if (c == '(' || c == ')' || c == '?' || c == '*' || c == '+')
		{
			tokens.push_back(string(1, c));
			++i;
			continue;
		}

		size_t start = i;
		while (i < text.size() &&
			text[i] != ' ' &&
			text[i] != '\t' &&
			text[i] != '\r' &&
			text[i] != '(' &&
			text[i] != ')' &&
			text[i] != '?' &&
			text[i] != '*' &&
			text[i] != '+')
		{
			++i;
		}
		tokens.push_back(text.substr(start, i - start));
	}
	return tokens;
}

shared_ptr<GrammarNode> ParseGrammarSequenceTokens(const vector<string>& tokens, size_t& pos);

shared_ptr<GrammarNode> ParseGrammarAtom(const vector<string>& tokens, size_t& pos)
{
	if (pos >= tokens.size())
	{
		throw runtime_error("unexpected end of grammar body");
	}

	if (tokens[pos] == "(")
	{
		++pos;
		shared_ptr<GrammarNode> inner = ParseGrammarSequenceTokens(tokens, pos);
		if (pos >= tokens.size() || tokens[pos] != ")")
		{
			throw runtime_error("unmatched grammar group");
		}
		++pos;
		return inner;
	}

	return MakeGrammarNodeSymbol(tokens[pos++]);
}

shared_ptr<GrammarNode> ParseGrammarPostfix(const vector<string>& tokens, size_t& pos)
{
	shared_ptr<GrammarNode> node = ParseGrammarAtom(tokens, pos);
	if (pos < tokens.size())
	{
		if (tokens[pos] == "?")
		{
			++pos;
			return MakeGrammarNodeUnary(GrammarNode::GNK_OPTIONAL, node);
		}
		if (tokens[pos] == "*")
		{
			++pos;
			return MakeGrammarNodeUnary(GrammarNode::GNK_STAR, node);
		}
		if (tokens[pos] == "+")
		{
			++pos;
			return MakeGrammarNodeUnary(GrammarNode::GNK_PLUS, node);
		}
	}
	return node;
}

shared_ptr<GrammarNode> ParseGrammarSequenceTokens(const vector<string>& tokens, size_t& pos)
{
	vector<shared_ptr<GrammarNode>> children;
	while (pos < tokens.size() && tokens[pos] != ")")
	{
		children.push_back(ParseGrammarPostfix(tokens, pos));
	}
	return MakeGrammarNodeSequence(children);
}

shared_ptr<GrammarNode> ParseGrammarBody(const string& body)
{
	vector<string> tokens = TokenizeGrammarBody(body);
	size_t pos = 0;
	shared_ptr<GrammarNode> node = ParseGrammarSequenceTokens(tokens, pos);
	if (pos != tokens.size())
	{
		throw runtime_error("trailing tokens in grammar body");
	}
	return node;
}

struct Grammar
{
	map<string, vector<shared_ptr<GrammarNode>>> productions;
	set<string> nonterminals;
};

bool EndsWithContinuation(const string& line)
{
	size_t end = line.size();
	while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r'))
	{
		--end;
	}
	return end > 0 && line[end - 1] == '\\';
}

string RemoveContinuation(const string& line)
{
	size_t end = line.size();
	while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r'))
	{
		--end;
	}
	if (end > 0 && line[end - 1] == '\\')
	{
		--end;
	}
	return line.substr(0, end);
}

string FindPA6GrammarPath()
{
	const vector<string> candidates =
	{
		"pa6.gram",
		"pa6/pa6.gram",
		"../pa6/pa6.gram"
	};

	for (const string& path : candidates)
	{
		ifstream in(path.c_str());
		if (in)
		{
			return path;
		}
	}

	throw runtime_error("unable to locate pa6.gram");
}

Grammar LoadGrammar(const string& path)
{
	ifstream in(path.c_str());
	if (!in)
	{
		throw runtime_error("unable to open grammar: " + path);
	}

	Grammar grammar;
	string current_name;
	string line;
	while (getline(in, line))
	{
		string logical = line;
		while (EndsWithContinuation(logical))
		{
			string next_line;
			if (!getline(in, next_line))
			{
				throw runtime_error("dangling grammar continuation");
			}
			logical = RemoveContinuation(logical) + " " + TrimLeft(next_line);
		}

		if (Trim(logical).empty())
		{
			continue;
		}

		if (logical[0] != ' ' && logical[0] != '\t')
		{
			size_t colon = logical.find(':');
			if (colon == string::npos)
			{
				throw runtime_error("invalid grammar rule header");
			}

			current_name = Trim(logical.substr(0, colon));
			grammar.nonterminals.insert(current_name);
			grammar.productions[current_name];
			continue;
		}

		if (current_name.empty())
		{
			throw runtime_error("grammar alternative without current rule");
		}

		string body = Trim(logical);
		grammar.productions[current_name].push_back(ParseGrammarBody(body));
	}

	return grammar;
}

Grammar& GetPA6Grammar()
{
	static Grammar grammar = LoadGrammar(FindPA6GrammarPath());
	return grammar;
}

vector<RecogToken> ConvertToRecogTokens(const vector<MacroToken>& macro_tokens)
{
	vector<PPToken> tokens = ToPPTokens(macro_tokens);
	vector<PPToken> filtered;
	for (const PPToken& token : tokens)
	{
		if (token.kind == PPT_WHITESPACE || token.kind == PPT_NEWLINE || token.kind == PPT_EOF)
		{
			continue;
		}
		filtered.push_back(token);
	}

	vector<RecogToken> output;
	for (size_t i = 0; i < filtered.size(); ++i)
	{
		const PPToken& token = filtered[i];
		switch (token.kind)
		{
		case PPT_HEADER_NAME:
		case PPT_NON_WHITESPACE_CHARACTER:
			throw runtime_error("invalid phase 7 token");
		case PPT_IDENTIFIER:
		case PPT_PREPROCESSING_OP_OR_PUNC:
		{
			auto it = StringToTokenTypeMap.find(token.source);
			if (it != StringToTokenTypeMap.end() &&
				!(token.source == "#" || token.source == "##" || token.source == "%:" || token.source == "%:%:"))
			{
				PushSimpleToken(output, token.source, it->second);
			}
			else if (token.kind == PPT_IDENTIFIER)
			{
				PushIdentifierToken(output, token.source);
			}
			else
			{
				throw runtime_error("invalid phase 7 token");
			}
			break;
		}
		case PPT_PP_NUMBER:
			if (!CapturePostTokenOutput([&](DebugPostTokenOutputStream& stream)
				{
					EmitFloatingLiteral(stream, token.source);
				}).empty())
			{
				PushLiteralToken(output, token.source);
				break;
			}
			if (!CapturePostTokenOutput([&](DebugPostTokenOutputStream& stream)
				{
					EmitIntegerLiteral(stream, token.source);
				}).empty())
			{
				PushLiteralToken(output, token.source);
				break;
			}
			throw runtime_error("invalid phase 7 token");
			break;
		case PPT_CHARACTER_LITERAL:
		case PPT_USER_DEFINED_CHARACTER_LITERAL:
		{
			string line = CapturePostTokenOutput([&](DebugPostTokenOutputStream& stream)
			{
				EmitCharacterLiteral(stream, token.source);
			});
			if (line.empty())
			{
				throw runtime_error("invalid phase 7 token");
			}
			PushLiteralToken(output, token.source);
			break;
		}
		case PPT_STRING_LITERAL:
		case PPT_USER_DEFINED_STRING_LITERAL:
		{
			size_t end = i + 1;
			while (end < filtered.size() &&
				(filtered[end].kind == PPT_STRING_LITERAL ||
				 filtered[end].kind == PPT_USER_DEFINED_STRING_LITERAL))
			{
				++end;
			}
			string joined_source;
			for (size_t j = i; j < end; ++j)
			{
				if (!joined_source.empty())
				{
					joined_source += ' ';
				}
				joined_source += filtered[j].source;
			}

			string line = CapturePostTokenOutput([&](DebugPostTokenOutputStream& stream)
			{
				EmitStringSequence(stream, filtered, i, end);
			});
			if (StartsWith(line, "invalid ") || line.empty())
			{
				throw runtime_error("invalid phase 7 token");
			}
			PushLiteralToken(output, joined_source);
			i = end - 1;
			break;
		}
		case PPT_WHITESPACE:
		case PPT_NEWLINE:
		case PPT_EOF:
			break;
		}

	}

	RecogToken eof;
	eof.is_eof = true;
	output.push_back(eof);
	return output;
}

vector<RecogToken> PreprocessAndTokenize(const string& srcfile)
{
	time_t now = time(nullptr);
	tm* build_tm = localtime(&now);

	PreprocContext ctx;
	ctx.build_date_literal = MakeAsctimeLiteralSlice(build_tm, 4, 11);
	ctx.build_time_literal = "\"" + string(asctime(build_tm)).substr(11, 8) + "\"";

	ProcessFile(ctx, srcfile, srcfile);
	return ConvertToRecogTokens(ctx.output_tokens);
}

struct AngleContext
{
	int paren = 0;
	int square = 0;
	int brace = 0;

	AngleContext()
	{}

	AngleContext(int paren, int square, int brace)
		: paren(paren), square(square), brace(brace)
	{}

	bool operator==(const AngleContext& other) const
	{
		return paren == other.paren &&
			square == other.square &&
			brace == other.brace;
	}
};

struct ParseState
{
	size_t pos = 0;
	int paren = 0;
	int square = 0;
	int brace = 0;
	vector<AngleContext> angle_stack;

	bool operator==(const ParseState& other) const
	{
		return pos == other.pos &&
			paren == other.paren &&
			square == other.square &&
			brace == other.brace &&
			angle_stack == other.angle_stack;
	}
};

struct ParseStateHash
{
	size_t operator()(const ParseState& state) const
	{
		size_t h = state.pos;
		h = h * 1315423911u + static_cast<size_t>(state.paren + 17);
		h = h * 1315423911u + static_cast<size_t>(state.square + 31);
		h = h * 1315423911u + static_cast<size_t>(state.brace + 47);
		for (const AngleContext& ctx : state.angle_stack)
		{
			h = h * 1315423911u + static_cast<size_t>(ctx.paren + 59);
			h = h * 1315423911u + static_cast<size_t>(ctx.square + 71);
			h = h * 1315423911u + static_cast<size_t>(ctx.brace + 83);
		}
		return h;
	}
};

struct MemoKey
{
	string name;
	ParseState state;

	bool operator==(const MemoKey& other) const
	{
		return name == other.name && state == other.state;
	}
};

struct MemoKeyHash
{
	size_t operator()(const MemoKey& key) const
	{
		return hash<string>()(key.name) * 1315423911u + ParseStateHash()(key.state);
	}
};

bool ParseStateBetter(const ParseState& lhs, const ParseState& rhs)
{
	if (lhs.pos != rhs.pos)
	{
		return lhs.pos > rhs.pos;
	}
	if (lhs.angle_stack.size() != rhs.angle_stack.size())
	{
		return lhs.angle_stack.size() > rhs.angle_stack.size();
	}
	if (lhs.paren != rhs.paren)
	{
		return lhs.paren < rhs.paren;
	}
	if (lhs.square != rhs.square)
	{
		return lhs.square < rhs.square;
	}
	return lhs.brace < rhs.brace;
}

vector<ParseState> UniqueStates(vector<ParseState> states)
{
	unordered_set<ParseState, ParseStateHash> seen;
	vector<ParseState> unique;
	for (const ParseState& state : states)
	{
		if (seen.insert(state).second)
		{
			unique.push_back(state);
		}
	}
	sort(unique.begin(), unique.end(), ParseStateBetter);
	return unique;
}

struct DeclSpecifierStep
{
	ParseState state;
	bool saw_non_cv_type_specifier = false;

	DeclSpecifierStep()
	{}

	DeclSpecifierStep(const ParseState& state, bool saw_non_cv_type_specifier)
		: state(state), saw_non_cv_type_specifier(saw_non_cv_type_specifier)
	{}

	bool operator==(const DeclSpecifierStep& other) const
	{
		return state == other.state &&
			saw_non_cv_type_specifier == other.saw_non_cv_type_specifier;
	}
};

struct DeclSpecifierStepHash
{
	size_t operator()(const DeclSpecifierStep& step) const
	{
		return ParseStateHash()(step.state) * 1315423911u + (step.saw_non_cv_type_specifier ? 1u : 0u);
	}
};

struct RecogParser
{
	const Grammar& grammar;
	const vector<RecogToken>& tokens;
	unordered_map<MemoKey, vector<ParseState>, MemoKeyHash> memo;

	RecogParser(const Grammar& grammar, const vector<RecogToken>& tokens)
		: grammar(grammar), tokens(tokens)
	{}

	const RecogToken* Peek(const ParseState& state) const
	{
		if (state.pos >= tokens.size())
		{
			return nullptr;
		}
		return &tokens[state.pos];
	}

	bool TokenIsSimple(const ParseState& state, ETokenType type) const
	{
		const RecogToken* token = Peek(state);
		return token && token->has_simple && token->simple == type;
	}

	bool TokenStartsFundamentalSimpleTypeSpecifier(const ParseState& state) const
	{
		const RecogToken* token = Peek(state);
		if (!token || !token->has_simple)
		{
			return false;
		}

		switch (token->simple)
		{
		case KW_CHAR:
		case KW_CHAR16_T:
		case KW_CHAR32_T:
		case KW_WCHAR_T:
		case KW_BOOL:
		case KW_SHORT:
		case KW_INT:
		case KW_LONG:
		case KW_SIGNED:
		case KW_UNSIGNED:
		case KW_FLOAT:
		case KW_DOUBLE:
		case KW_VOID:
		case KW_AUTO:
		case KW_DECLTYPE:
			return true;
		default:
			return false;
		}
	}

	bool TokenStartsCvQualifier(const ParseState& state) const
	{
		return TokenIsSimple(state, KW_CONST) || TokenIsSimple(state, KW_VOLATILE);
	}

	bool TokenStartsTypeNameLikeSpecifier(const ParseState& state) const
	{
		const RecogToken* token = Peek(state);
		if (!token)
		{
			return false;
		}

		if (token->is_identifier)
		{
			return true;
		}

		if (!token->has_simple)
		{
			return false;
		}

		switch (token->simple)
		{
		case OP_COLON2:
		case KW_TYPENAME:
		case KW_CLASS:
		case KW_STRUCT:
		case KW_UNION:
		case KW_ENUM:
		case KW_DECLTYPE:
			return true;
		default:
			return false;
		}
	}

	bool MatchesTerminal(const RecogToken& token, const string& name) const
	{
		if (name == "TT_IDENTIFIER")
		{
			return token.is_identifier;
		}
		if (name == "TT_LITERAL")
		{
			return token.is_literal;
		}
		if (name == "ST_EMPTYSTR")
		{
			return token.is_empty_string;
		}
		if (name == "ST_ZERO")
		{
			return token.is_zero;
		}
		if (name == "ST_OVERRIDE")
		{
			return token.is_identifier && token.source == "override";
		}
		if (name == "ST_FINAL")
		{
			return token.is_identifier && token.source == "final";
		}
		if (name == "ST_NONPAREN")
		{
			if (token.is_eof)
			{
				return false;
			}
			return !(token.has_simple &&
				(token.simple == OP_LPAREN ||
				 token.simple == OP_RPAREN ||
				 token.simple == OP_LSQUARE ||
				 token.simple == OP_RSQUARE ||
				 token.simple == OP_LBRACE ||
				 token.simple == OP_RBRACE));
		}
		if (name == "ST_EOF")
		{
			return token.is_eof;
		}
		if (name == "ST_RSHIFT_1")
		{
			return token.is_rshift_1;
		}
		if (name == "ST_RSHIFT_2")
		{
			return token.is_rshift_2;
		}

		if (!token.has_simple)
		{
			return false;
		}

		auto it = TokenTypeToStringMap.find(token.simple);
		return it != TokenTypeToStringMap.end() && it->second == name;
	}

	bool IsAngleCloseReserved(const ParseState& state) const
	{
		if (state.angle_stack.empty())
		{
			return false;
		}

		const AngleContext& top = state.angle_stack.back();
		return top.paren == state.paren &&
			top.square == state.square &&
			top.brace == state.brace;
	}

	vector<ParseState> ParseTerminal(const string& name, const ParseState& state)
	{
		const RecogToken* token = Peek(state);
		if (!token || !MatchesTerminal(*token, name))
		{
			return {};
		}

		if ((name == "OP_GT" || name == "ST_RSHIFT_1" || name == "ST_RSHIFT_2") &&
			IsAngleCloseReserved(state))
		{
			return {};
		}

		ParseState next = state;
		next.pos++;

		if (name == "OP_LPAREN")
		{
			++next.paren;
		}
		else if (name == "OP_RPAREN")
		{
			if (next.paren == 0)
			{
				return {};
			}
			--next.paren;
		}
		else if (name == "OP_LSQUARE")
		{
			++next.square;
		}
		else if (name == "OP_RSQUARE")
		{
			if (next.square == 0)
			{
				return {};
			}
			--next.square;
		}
		else if (name == "OP_LBRACE")
		{
			++next.brace;
		}
		else if (name == "OP_RBRACE")
		{
			if (next.brace == 0)
			{
				return {};
			}
			--next.brace;
		}

		return vector<ParseState>(1, next);
	}

	ParseState EnterAngleContext(const ParseState& state) const
	{
		ParseState next = state;
		next.angle_stack.push_back(AngleContext{state.paren, state.square, state.brace});
		return next;
	}

	vector<ParseState> ParseNode(const shared_ptr<GrammarNode>& node, const ParseState& state)
	{
		switch (node->kind)
		{
		case GrammarNode::GNK_SYMBOL:
			return Parse(node->symbol, state);

		case GrammarNode::GNK_SEQUENCE:
		{
			vector<ParseState> states(1, state);
			for (const shared_ptr<GrammarNode>& child : node->children)
			{
				vector<ParseState> next_states;
				for (const ParseState& current : states)
				{
					vector<ParseState> parsed = ParseNode(child, current);
					next_states.insert(next_states.end(), parsed.begin(), parsed.end());
				}
				states = UniqueStates(next_states);
				if (states.empty())
				{
					break;
				}
			}
			return states;
		}

		case GrammarNode::GNK_OPTIONAL:
		{
			vector<ParseState> states = ParseNode(node->children[0], state);
			states.push_back(state);
			return UniqueStates(states);
		}

		case GrammarNode::GNK_STAR:
		{
			vector<ParseState> results;
			unordered_set<ParseState, ParseStateHash> seen;

			function<void(const ParseState&)> dfs = [&](const ParseState& current)
			{
				if (!seen.insert(current).second)
				{
					return;
				}

				results.push_back(current);
				vector<ParseState> next_states = ParseNode(node->children[0], current);
				for (const ParseState& next : next_states)
				{
					if (!(next == current))
					{
						dfs(next);
					}
				}
			};

			dfs(state);
			return UniqueStates(results);
		}

		case GrammarNode::GNK_PLUS:
		{
			vector<ParseState> results;
			vector<ParseState> first = ParseNode(node->children[0], state);
			for (const ParseState& current : first)
			{
				if (current == state)
				{
					continue;
				}
				shared_ptr<GrammarNode> star = MakeGrammarNodeUnary(GrammarNode::GNK_STAR, node->children[0]);
				vector<ParseState> suffix = ParseNode(star, current);
				results.insert(results.end(), suffix.begin(), suffix.end());
			}
			return UniqueStates(results);
		}
		}

		return {};
	}

	vector<ParseState> ParseFilteredIdentifier(const ParseState& state, bool (*predicate)(const string&))
	{
		const RecogToken* token = Peek(state);
		if (!token || !token->is_identifier || !predicate(token->source))
		{
			return {};
		}

		ParseState next = state;
		++next.pos;
		return vector<ParseState>(1, next);
	}

	vector<ParseState> ParseCloseAngleBracket(const ParseState& state)
	{
		if (!IsAngleCloseReserved(state))
		{
			return {};
		}

		const RecogToken* token = Peek(state);
		if (!token)
		{
			return {};
		}

		if (!(token->has_simple && token->simple == OP_GT) &&
			!token->is_rshift_1 &&
			!token->is_rshift_2)
		{
			return {};
		}

		ParseState next = state;
		++next.pos;
		next.angle_stack.pop_back();
		return vector<ParseState>(1, next);
	}

	vector<ParseState> AdvanceStates(const vector<ParseState>& states, const string& name)
	{
		vector<ParseState> out;
		for (const ParseState& state : states)
		{
			vector<ParseState> parsed = Parse(name, state);
			out.insert(out.end(), parsed.begin(), parsed.end());
		}
		return UniqueStates(out);
	}

	vector<ParseState> AddCurrentState(const vector<ParseState>& states, const ParseState& state)
	{
		vector<ParseState> out = states;
		out.push_back(state);
		return UniqueStates(out);
	}

	vector<DeclSpecifierStep> ParseOneDeclSpecifier(const DeclSpecifierStep& step)
	{
		vector<DeclSpecifierStep> out;

		auto add_same = [&](const string& name)
		{
			for (const ParseState& parsed : Parse(name, step.state))
			{
				out.push_back(DeclSpecifierStep{parsed, step.saw_non_cv_type_specifier});
			}
		};

		auto add_non_cv = [&](const string& name)
		{
			for (const ParseState& parsed : Parse(name, step.state))
			{
				out.push_back(DeclSpecifierStep{parsed, true});
			}
		};

		add_same("storage-class-specifier");

		if (TokenStartsCvQualifier(step.state))
		{
			add_same("cv-qualifier");
		}
		else if (TokenStartsFundamentalSimpleTypeSpecifier(step.state))
		{
			add_non_cv("simple-type-specifier");
		}
		else if (!step.saw_non_cv_type_specifier)
		{
			add_non_cv("simple-type-specifier");
			add_non_cv("elaborated-type-specifier");
			add_non_cv("typename-specifier");
			add_non_cv("class-specifier");
			add_non_cv("enum-specifier");
		}

		add_same("function-specifier");
		add_same("KW_FRIEND");
		add_same("KW_TYPEDEF");
		add_same("KW_CONSTEXPR");

		unordered_set<DeclSpecifierStep, DeclSpecifierStepHash> seen;
		vector<DeclSpecifierStep> unique;
		for (const DeclSpecifierStep& candidate : out)
		{
			if (seen.insert(candidate).second)
			{
				unique.push_back(candidate);
			}
		}
		sort(unique.begin(), unique.end(), [&](const DeclSpecifierStep& lhs, const DeclSpecifierStep& rhs)
		{
			return ParseStateBetter(lhs.state, rhs.state);
		});
		return unique;
	}

	vector<ParseState> ParseDeclSpecifierSeq(const ParseState& state)
	{
		vector<DeclSpecifierStep> frontier = ParseOneDeclSpecifier(DeclSpecifierStep{state, false});
		if (frontier.empty())
		{
			return {};
		}

		vector<DeclSpecifierStep> all = frontier;
		unordered_set<DeclSpecifierStep, DeclSpecifierStepHash> seen(frontier.begin(), frontier.end());

		while (!frontier.empty())
		{
			vector<DeclSpecifierStep> next_frontier;
			for (const DeclSpecifierStep& step : frontier)
			{
				for (const DeclSpecifierStep& next : ParseOneDeclSpecifier(step))
				{
					if (seen.insert(next).second)
					{
						next_frontier.push_back(next);
						all.push_back(next);
					}
				}
			}
			frontier.swap(next_frontier);
		}

		vector<ParseState> results;
		for (const DeclSpecifierStep& step : all)
		{
			vector<ParseState> states(1, step.state);
			vector<ParseState> next = AdvanceStates(states, "attribute-specifier");
			states = AddCurrentState(next, step.state);
			results.insert(results.end(), states.begin(), states.end());
		}
		return UniqueStates(results);
	}

	vector<ParseState> ParseSimpleTemplateId(const ParseState& state)
	{
		vector<ParseState> states = Parse("template-name", state);
		states = AdvanceStates(states, "OP_LT");
		for (ParseState& current : states)
		{
			current = EnterAngleContext(current);
		}

		vector<ParseState> with_args = AdvanceStates(states, "template-argument-list");
		states.insert(states.end(), with_args.begin(), with_args.end());
		states = UniqueStates(states);
		states = AdvanceStates(states, "close-angle-bracket");
		return states;
	}

	vector<ParseState> ParseTemplateId(const ParseState& state)
	{
		vector<ParseState> results = ParseSimpleTemplateId(state);

		for (const string& prefix : vector<string>{"operator-function-id", "literal-operator-id"})
		{
			vector<ParseState> states = Parse(prefix, state);
			states = AdvanceStates(states, "OP_LT");
			for (ParseState& current : states)
			{
				current = EnterAngleContext(current);
			}

			vector<ParseState> with_args = AdvanceStates(states, "template-argument-list");
			states.insert(states.end(), with_args.begin(), with_args.end());
			states = UniqueStates(states);
			states = AdvanceStates(states, "close-angle-bracket");
			results.insert(results.end(), states.begin(), states.end());
		}

		return UniqueStates(results);
	}

	vector<ParseState> ParseTemplateDeclaration(const ParseState& state)
	{
		vector<ParseState> states = Parse("KW_TEMPLATE", state);
		states = AdvanceStates(states, "OP_LT");
		for (ParseState& current : states)
		{
			current = EnterAngleContext(current);
		}
		states = AdvanceStates(states, "template-parameter-list");
		states = AdvanceStates(states, "close-angle-bracket");
		states = AdvanceStates(states, "declaration");
		return states;
	}

	vector<ParseState> ParseExplicitSpecialization(const ParseState& state)
	{
		vector<ParseState> states = Parse("KW_TEMPLATE", state);
		states = AdvanceStates(states, "OP_LT");
		for (ParseState& current : states)
		{
			current = EnterAngleContext(current);
		}
		states = AdvanceStates(states, "close-angle-bracket");
		states = AdvanceStates(states, "declaration");
		return states;
	}

	vector<ParseState> ParseTypeParameter(const ParseState& state)
	{
		vector<ParseState> results;

		auto add = [&](vector<ParseState> states)
		{
			results.insert(results.end(), states.begin(), states.end());
		};

		for (const string& kw : vector<string>{"KW_CLASS", "KW_TYPENAME"})
		{
			vector<ParseState> states = Parse(kw, state);

			vector<ParseState> opt_dots = states;
			vector<ParseState> dotted = AdvanceStates(states, "OP_DOTS");
			opt_dots.insert(opt_dots.end(), dotted.begin(), dotted.end());
			opt_dots = UniqueStates(opt_dots);

			vector<ParseState> pack_decl = opt_dots;
			vector<ParseState> pack_id = AdvanceStates(opt_dots, "TT_IDENTIFIER");
			pack_decl.insert(pack_decl.end(), pack_id.begin(), pack_id.end());
			add(UniqueStates(pack_decl));

			vector<ParseState> opt_id = states;
			vector<ParseState> ids = AdvanceStates(states, "TT_IDENTIFIER");
			opt_id.insert(opt_id.end(), ids.begin(), ids.end());
			opt_id = UniqueStates(opt_id);
			opt_id = AdvanceStates(opt_id, "OP_ASS");
			opt_id = AdvanceStates(opt_id, "type-id");
			add(opt_id);
		}

		vector<ParseState> states = Parse("KW_TEMPLATE", state);
		states = AdvanceStates(states, "OP_LT");
		for (ParseState& current : states)
		{
			current = EnterAngleContext(current);
		}
		states = AdvanceStates(states, "template-parameter-list");
		states = AdvanceStates(states, "close-angle-bracket");
		states = AdvanceStates(states, "KW_CLASS");

		vector<ParseState> opt_dots = states;
		vector<ParseState> dotted = AdvanceStates(states, "OP_DOTS");
		opt_dots.insert(opt_dots.end(), dotted.begin(), dotted.end());
		opt_dots = UniqueStates(opt_dots);
		vector<ParseState> pack_decl = opt_dots;
		vector<ParseState> pack_id = AdvanceStates(opt_dots, "TT_IDENTIFIER");
		pack_decl.insert(pack_decl.end(), pack_id.begin(), pack_id.end());
		add(UniqueStates(pack_decl));

		vector<ParseState> template_assign = states;
		vector<ParseState> assign_id = AdvanceStates(states, "TT_IDENTIFIER");
		template_assign.insert(template_assign.end(), assign_id.begin(), assign_id.end());
		template_assign = UniqueStates(template_assign);
		template_assign = AdvanceStates(template_assign, "OP_ASS");
		template_assign = AdvanceStates(template_assign, "id-expression");
		add(template_assign);

		return UniqueStates(results);
	}

	vector<ParseState> ParseAngleCastLike(const ParseState& state, ETokenType keyword)
	{
		vector<ParseState> states = Parse(TokenTypeToStringMap.at(keyword), state);
		states = AdvanceStates(states, "OP_LT");
		for (ParseState& current : states)
		{
			current = EnterAngleContext(current);
		}
		states = AdvanceStates(states, "type-id");
		states = AdvanceStates(states, "close-angle-bracket");
		states = AdvanceStates(states, "OP_LPAREN");
		states = AdvanceStates(states, "expression");
		states = AdvanceStates(states, "OP_RPAREN");
		return states;
	}

	vector<ParseState> ParsePostfixRoot(const ParseState& state)
	{
		vector<ParseState> results;

		auto add = [&](const vector<ParseState>& states)
		{
			results.insert(results.end(), states.begin(), states.end());
		};

		add(Parse("primary-expression", state));

		for (const string& type_name : vector<string>{"simple-type-specifier", "typename-specifier"})
		{
			vector<ParseState> call = Parse(type_name, state);
			call = AdvanceStates(call, "OP_LPAREN");
			vector<ParseState> with_args = AdvanceStates(call, "expression-list");
			call.insert(call.end(), with_args.begin(), with_args.end());
			call = UniqueStates(call);
			call = AdvanceStates(call, "OP_RPAREN");
			add(call);

			vector<ParseState> brace = Parse(type_name, state);
			brace = AdvanceStates(brace, "braced-init-list");
			add(brace);
		}

		add(ParseAngleCastLike(state, KW_DYNAMIC_CAST));
		add(ParseAngleCastLike(state, KW_STATIC_CAST));
		add(ParseAngleCastLike(state, KW_REINTERPET_CAST));
		add(ParseAngleCastLike(state, KW_CONST_CAST));

		for (const string& inside : vector<string>{"expression", "type-id"})
		{
			vector<ParseState> states = Parse("KW_TYPEID", state);
			states = AdvanceStates(states, "OP_LPAREN");
			states = AdvanceStates(states, inside);
			states = AdvanceStates(states, "OP_RPAREN");
			add(states);
		}

		return UniqueStates(results);
	}

	vector<ParseState> ParseUnqualifiedId(const ParseState& state)
	{
		vector<ParseState> results;
		const RecogToken* token = Peek(state);
		bool template_head = token &&
			token->is_identifier &&
			PA6_IsTemplateName(token->source) &&
			state.pos + 1 < tokens.size() &&
			tokens[state.pos + 1].has_simple &&
			tokens[state.pos + 1].simple == OP_LT;

		if (token && token->is_identifier && PA6_IsTemplateName(token->source))
		{
			vector<ParseState> template_states = ParseTemplateId(state);
			results.insert(results.end(), template_states.begin(), template_states.end());
		}

		for (const string& name : vector<string>{
			"operator-function-id",
			"conversion-function-id",
			"literal-operator-id"})
		{
			vector<ParseState> parsed = Parse(name, state);
			results.insert(results.end(), parsed.begin(), parsed.end());
		}

		if (!template_head)
		{
			vector<ParseState> parsed = Parse("TT_IDENTIFIER", state);
			results.insert(results.end(), parsed.begin(), parsed.end());
		}

		vector<ParseState> compl_class = Parse("OP_COMPL", state);
		compl_class = AdvanceStates(compl_class, "class-name");
		results.insert(results.end(), compl_class.begin(), compl_class.end());

		vector<ParseState> compl_decltype = Parse("OP_COMPL", state);
		compl_decltype = AdvanceStates(compl_decltype, "decltype-specifier");
		results.insert(results.end(), compl_decltype.begin(), compl_decltype.end());

		vector<ParseState> template_states = ParseTemplateId(state);
		results.insert(results.end(), template_states.begin(), template_states.end());

		return UniqueStates(results);
	}

	vector<ParseState> ParseImpl(const string& name, const ParseState& state)
	{
		if (name == "template-name")
		{
			return ParseFilteredIdentifier(state, PA6_IsTemplateName);
		}
		if (name == "typedef-name")
		{
			return ParseFilteredIdentifier(state, PA6_IsTypedefName);
		}
		if (name == "enum-name")
		{
			return ParseFilteredIdentifier(state, PA6_IsEnumName);
		}
		if (name == "namespace-name")
		{
			return ParseFilteredIdentifier(state, PA6_IsNamespaceName);
		}
		if (name == "class-name")
		{
			vector<ParseState> results = ParseFilteredIdentifier(state, PA6_IsClassName);
			if (Peek(state) && Peek(state)->is_identifier && PA6_IsClassName(Peek(state)->source))
			{
				vector<ParseState> template_states = ParseSimpleTemplateId(state);
				results.insert(results.end(), template_states.begin(), template_states.end());
			}
			return UniqueStates(results);
		}
		if (name == "close-angle-bracket")
		{
			return ParseCloseAngleBracket(state);
		}
		if (name == "decl-specifier-seq")
		{
			return ParseDeclSpecifierSeq(state);
		}
		if (name == "simple-template-id")
		{
			return ParseSimpleTemplateId(state);
		}
		if (name == "template-id")
		{
			return ParseTemplateId(state);
		}
		if (name == "template-declaration")
		{
			return ParseTemplateDeclaration(state);
		}
		if (name == "explicit-specialization")
		{
			return ParseExplicitSpecialization(state);
		}
		if (name == "type-parameter")
		{
			return ParseTypeParameter(state);
		}
		if (name == "postfix-root")
		{
			return ParsePostfixRoot(state);
		}
		if (name == "unqualified-id")
		{
			return ParseUnqualifiedId(state);
		}

		auto prod = grammar.productions.find(name);
		if (prod == grammar.productions.end())
		{
			return ParseTerminal(name, state);
		}

		vector<ParseState> results;
		for (const shared_ptr<GrammarNode>& alt : prod->second)
		{
			vector<ParseState> parsed = ParseNode(alt, state);
			results.insert(results.end(), parsed.begin(), parsed.end());
		}
		return UniqueStates(results);
	}

	vector<ParseState> Parse(const string& name, const ParseState& state)
	{
		MemoKey key{name, state};
		auto it = memo.find(key);
		if (it != memo.end())
		{
			return it->second;
		}

		vector<ParseState> result = ParseImpl(name, state);
		memo.insert(make_pair(key, result));
		return result;
	}
};

void DoRecog(const string& srcfile)
{
	vector<RecogToken> tokens = PreprocessAndTokenize(srcfile);
	RecogParser parser(GetPA6Grammar(), tokens);

	vector<ParseState> states = parser.Parse("translation-unit", ParseState{});
	for (const ParseState& state : states)
	{
		if (state.pos == tokens.size() &&
			state.paren == 0 &&
			state.square == 0 &&
			state.brace == 0 &&
			state.angle_stack.empty())
		{
			return;
		}
	}

	throw runtime_error("parse failed");
}

#ifndef CPPGM_EMBED_RECOG
int main(int argc, char** argv)
{
	try
	{
		vector<string> args;

		for (int i = 1; i < argc; i++)
			args.emplace_back(argv[i]);

		if (args.size() < 3 || args[0] != "-o")
			throw logic_error("invalid usage");

		string outfile = args[1];
		size_t nsrcfiles = args.size() - 2;

		ofstream out(outfile.c_str());

		out << "recog " << nsrcfiles << endl;

		for (size_t i = 0; i < nsrcfiles; i++)
		{
			string srcfile = args[i+2];

			try
			{
				DoRecog(srcfile);
				out << srcfile << " OK" << endl;
			}
			catch (exception& e)
			{
				cerr << e.what() << endl;
				out << srcfile << " BAD" << endl;
			}
		}
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
