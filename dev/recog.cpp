// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#define CPPGM_POSTTOKEN_INTERNAL_MAIN recog_posttoken_internal_main
#define CPPGM_MACRO_MAIN_NAME recog_macro_internal_main
#define CPPGM_PREPROC_MAIN_NAME recog_preproc_internal_main
#include "preproc.cpp"
#undef CPPGM_PREPROC_MAIN_NAME
#undef CPPGM_MACRO_MAIN_NAME
#undef CPPGM_POSTTOKEN_INTERNAL_MAIN

#include <vector>
#include <string>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <memory>

using namespace std;

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

struct PA6Token
{
	string term;
	string spell;
	bool is_identifier;
	bool is_literal;
	bool is_eof;
};

struct GrammarNode
{
	enum Kind
	{
		GN_SYMBOL,
		GN_SEQUENCE,
		GN_OPTIONAL,
		GN_STAR,
		GN_PLUS,
		GN_EMPTY
	} kind;

	string symbol;
	vector<GrammarNode*> children;
};

struct GrammarRule
{
	vector<GrammarNode*> alts;
};

struct Grammar
{
	vector< unique_ptr<GrammarNode> > storage;
	map<string, GrammarRule> rules;

	GrammarNode* MakeNode(GrammarNode::Kind kind)
	{
		storage.emplace_back(new GrammarNode());
		storage.back()->kind = kind;
		return storage.back().get();
	}
};

vector<string> TokenizeGrammarExpr(const string& expr)
{
	vector<string> out;
	for (size_t i = 0; i < expr.size(); )
	{
		if (expr[i] == ' ' || expr[i] == '\t')
		{
			++i;
			continue;
		}
		if (expr[i] == '(' || expr[i] == ')' || expr[i] == '?' || expr[i] == '*' || expr[i] == '+')
		{
			out.push_back(expr.substr(i, 1));
			++i;
			continue;
		}
		size_t j = i;
		while (j < expr.size() &&
			expr[j] != ' ' && expr[j] != '\t' &&
			expr[j] != '(' && expr[j] != ')' &&
			expr[j] != '?' && expr[j] != '*' && expr[j] != '+')
		{
			++j;
		}
		out.push_back(expr.substr(i, j - i));
		i = j;
	}
	return out;
}

GrammarNode* ParseGrammarSequence(Grammar& grammar, const vector<string>& toks, size_t& pos);

GrammarNode* ParseGrammarAtom(Grammar& grammar, const vector<string>& toks, size_t& pos)
{
	if (pos >= toks.size()) throw runtime_error("bad grammar");
	if (toks[pos] == "(")
	{
		++pos;
		GrammarNode* inner = ParseGrammarSequence(grammar, toks, pos);
		if (pos >= toks.size() || toks[pos] != ")") throw runtime_error("bad grammar");
		++pos;
		return inner;
	}
	GrammarNode* sym = grammar.MakeNode(GrammarNode::GN_SYMBOL);
	sym->symbol = toks[pos++];
	return sym;
}

GrammarNode* ParseGrammarFactor(Grammar& grammar, const vector<string>& toks, size_t& pos)
{
	GrammarNode* node = ParseGrammarAtom(grammar, toks, pos);
	if (pos < toks.size())
	{
		if (toks[pos] == "?")
		{
			GrammarNode* out = grammar.MakeNode(GrammarNode::GN_OPTIONAL);
			out->children.push_back(node);
			++pos;
			return out;
		}
		if (toks[pos] == "*")
		{
			GrammarNode* out = grammar.MakeNode(GrammarNode::GN_STAR);
			out->children.push_back(node);
			++pos;
			return out;
		}
		if (toks[pos] == "+")
		{
			GrammarNode* out = grammar.MakeNode(GrammarNode::GN_PLUS);
			out->children.push_back(node);
			++pos;
			return out;
		}
	}
	return node;
}

GrammarNode* ParseGrammarSequence(Grammar& grammar, const vector<string>& toks, size_t& pos)
{
	vector<GrammarNode*> parts;
	while (pos < toks.size() && toks[pos] != ")")
	{
		parts.push_back(ParseGrammarFactor(grammar, toks, pos));
	}
	if (parts.empty()) return grammar.MakeNode(GrammarNode::GN_EMPTY);
	if (parts.size() == 1) return parts[0];
	GrammarNode* seq = grammar.MakeNode(GrammarNode::GN_SEQUENCE);
	seq->children = parts;
	return seq;
}

Grammar LoadGrammar()
{
	ifstream in("pa6.gram");
	if (!in) in.open("pa6/pa6.gram");
	if (!in) throw runtime_error("failed to open pa6.gram");

	vector<string> logical_lines;
	string raw;
	string cur;
	while (getline(in, raw))
	{
		if (!raw.empty() && raw[raw.size() - 1] == '\r') raw.erase(raw.size() - 1);
		if (!raw.empty() && raw[raw.size() - 1] == '\\')
		{
			cur += raw.substr(0, raw.size() - 1);
			cur += " ";
			continue;
		}
		cur += raw;
		logical_lines.push_back(cur);
		cur.clear();
	}
	if (!cur.empty()) logical_lines.push_back(cur);

	Grammar grammar;
	string current_rule;
	for (size_t i = 0; i < logical_lines.size(); ++i)
	{
		const string& line = logical_lines[i];
		if (line.empty()) continue;
		if (line[0] != ' ' && line[0] != '\t')
		{
			size_t colon = line.find(':');
			if (colon == string::npos) continue;
			current_rule = line.substr(0, colon);
			grammar.rules[current_rule] = GrammarRule();
			continue;
		}
		size_t start = line.find_first_not_of(" \t");
		if (start == string::npos || current_rule.empty()) continue;
		vector<string> toks = TokenizeGrammarExpr(line.substr(start));
		size_t pos = 0;
		grammar.rules[current_rule].alts.push_back(ParseGrammarSequence(grammar, toks, pos));
		if (pos != toks.size()) throw runtime_error("bad grammar parse");
	}
	return grammar;
}

void AppendToken(vector<PA6Token>& out, const string& term, const string& spell, bool is_identifier, bool is_literal, bool is_eof)
{
	out.push_back({term, spell, is_identifier, is_literal, is_eof});
}

vector<PA6Token> LexForPA6(const vector<PPToken>& tokens)
{
	vector<PA6Token> out;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		const PPToken& tok = tokens[i];
		if (tok.kind == PP_IDENTIFIER || tok.kind == PP_OP)
		{
			if (tok.kind == PP_OP && tok.data == ">>")
			{
				AppendToken(out, "ST_RSHIFT_1", tok.data, false, false, false);
				AppendToken(out, "ST_RSHIFT_2", tok.data, false, false, false);
				continue;
			}
			auto it = StringToTokenTypeMap.find(tok.data);
			if (it != StringToTokenTypeMap.end())
			{
				AppendToken(out, TokenTypeToStringMap.at(it->second), tok.data, tok.kind == PP_IDENTIFIER, false, false);
			}
			else if (tok.kind == PP_IDENTIFIER)
			{
				AppendToken(out, "TT_IDENTIFIER", tok.data, true, false, false);
			}
			else
			{
				throw runtime_error("bad token");
			}
			continue;
		}
		if (tok.kind == PP_NUMBER || tok.kind == PP_CHAR || tok.kind == PP_UD_CHAR || tok.kind == PP_STRING || tok.kind == PP_UD_STRING)
		{
			AppendToken(out, "TT_LITERAL", tok.data, false, true, false);
			continue;
		}
		throw runtime_error("bad token");
	}
	AppendToken(out, "ST_EOF", "", false, false, true);
	return out;
}

struct RecogParser
{
	const Grammar& grammar;
	const vector<PA6Token>& tokens;
	map< pair<string, size_t>, vector<size_t> > memo;
	set< pair<string, size_t> > active;

	RecogParser(const Grammar& g, const vector<PA6Token>& t) : grammar(g), tokens(t) {}

	static void AddPos(vector<size_t>& out, size_t pos)
	{
		for (size_t i = 0; i < out.size(); ++i) if (out[i] == pos) return;
		out.push_back(pos);
	}

	bool MatchTerminal(const string& term, size_t pos) const
	{
		if (pos >= tokens.size()) return false;
		const PA6Token& tok = tokens[pos];
		if (term == "TT_IDENTIFIER") return tok.is_identifier;
		if (term == "TT_LITERAL") return tok.is_literal;
		if (term == "ST_EMPTYSTR") return tok.is_literal && tok.spell == "\"\"";
		if (term == "ST_ZERO") return tok.is_literal && tok.spell == "0";
		if (term == "ST_EOF") return tok.is_eof;
		if (term == "ST_NONPAREN")
		{
			return !tok.is_eof &&
				tok.term != "OP_LPAREN" && tok.term != "OP_RPAREN" &&
				tok.term != "OP_LSQUARE" && tok.term != "OP_RSQUARE" &&
				tok.term != "OP_LBRACE" && tok.term != "OP_RBRACE";
		}
		if (term == "ST_OVERRIDE") return tok.is_identifier && tok.spell == "override";
		if (term == "ST_FINAL") return tok.is_identifier && tok.spell == "final";
		return tok.term == term;
	}

	vector<size_t> ParseNode(GrammarNode* node, size_t pos)
	{
		vector<size_t> out;
		if (node->kind == GrammarNode::GN_EMPTY)
		{
			AddPos(out, pos);
			return out;
		}
		if (node->kind == GrammarNode::GN_SYMBOL)
		{
			return ParseSymbol(node->symbol, pos);
		}
		if (node->kind == GrammarNode::GN_SEQUENCE)
		{
			vector<size_t> cur(1, pos);
			for (size_t i = 0; i < node->children.size(); ++i)
			{
				vector<size_t> next;
				for (size_t j = 0; j < cur.size(); ++j)
				{
					vector<size_t> part = ParseNode(node->children[i], cur[j]);
					for (size_t k = 0; k < part.size(); ++k) AddPos(next, part[k]);
				}
				cur.swap(next);
				if (cur.empty()) break;
			}
			return cur;
		}
		if (node->kind == GrammarNode::GN_OPTIONAL)
		{
			AddPos(out, pos);
			vector<size_t> part = ParseNode(node->children[0], pos);
			for (size_t i = 0; i < part.size(); ++i) AddPos(out, part[i]);
			return out;
		}
		if (node->kind == GrammarNode::GN_STAR || node->kind == GrammarNode::GN_PLUS)
		{
			vector<size_t> frontier;
			if (node->kind == GrammarNode::GN_STAR) AddPos(out, pos);
			vector<size_t> first = ParseNode(node->children[0], pos);
			for (size_t i = 0; i < first.size(); ++i)
			{
				if (first[i] != pos)
				{
					AddPos(out, first[i]);
					AddPos(frontier, first[i]);
				}
			}
			while (!frontier.empty())
			{
				vector<size_t> next;
				for (size_t i = 0; i < frontier.size(); ++i)
				{
					vector<size_t> part = ParseNode(node->children[0], frontier[i]);
					for (size_t j = 0; j < part.size(); ++j)
					{
						if (part[j] != frontier[i] && part[j] != pos)
						{
							bool fresh = true;
							for (size_t k = 0; k < out.size(); ++k) if (out[k] == part[j]) fresh = false;
							if (fresh)
							{
								AddPos(out, part[j]);
								AddPos(next, part[j]);
							}
						}
					}
				}
				frontier.swap(next);
			}
			return out;
		}
		return out;
	}

	vector<size_t> ParseSymbol(const string& sym, size_t pos)
	{
		if (grammar.rules.count(sym) == 0)
		{
			vector<size_t> out;
			if (MatchTerminal(sym, pos)) AddPos(out, pos + 1);
			return out;
		}
		return ParseNonterminal(sym, pos);
	}

	vector<size_t> ParseSpecialName(const string& name, size_t pos)
	{
		vector<size_t> out;
		if (pos >= tokens.size() || !tokens[pos].is_identifier) return out;
		const string& spell = tokens[pos].spell;
		if (name == "template-name")
		{
			if (PA6_IsTemplateName(spell)) AddPos(out, pos + 1);
			return out;
		}
		if (name == "typedef-name")
		{
			if (PA6_IsTypedefName(spell)) AddPos(out, pos + 1);
			return out;
		}
		if (name == "enum-name")
		{
			if (PA6_IsEnumName(spell)) AddPos(out, pos + 1);
			return out;
		}
		if (name == "namespace-name")
		{
			if (PA6_IsNamespaceName(spell)) AddPos(out, pos + 1);
			return out;
		}
		if (name == "class-name")
		{
			if (PA6_IsClassName(spell)) AddPos(out, pos + 1);
			if (PA6_IsClassName(spell) && PA6_IsTemplateName(spell))
			{
				vector<size_t> stid = ParseNonterminalRule("simple-template-id", pos);
				for (size_t i = 0; i < stid.size(); ++i) AddPos(out, stid[i]);
			}
			return out;
		}
		return out;
	}

	vector<size_t> ParseSimpleTemplateId(size_t pos)
	{
		vector<size_t> out;
		vector<size_t> names = ParseSpecialName("template-name", pos);
		for (size_t i = 0; i < names.size(); ++i)
		{
			size_t p = names[i];
			if (p >= tokens.size() || tokens[p].term != "OP_LT") continue;
			int tmpl_depth = 1;
			int paren = 0;
			int square = 0;
			int brace = 0;
			for (size_t q = p + 1; q < tokens.size(); ++q)
			{
				const string& term = tokens[q].term;
				if (term == "OP_LPAREN") ++paren;
				else if (term == "OP_RPAREN" && paren > 0) --paren;
				else if (term == "OP_LSQUARE") ++square;
				else if (term == "OP_RSQUARE" && square > 0) --square;
				else if (term == "OP_LBRACE") ++brace;
				else if (term == "OP_RBRACE" && brace > 0) --brace;
				else if (paren == 0 && square == 0 && brace == 0)
				{
					if (term == "OP_LT") ++tmpl_depth;
					else if (term == "OP_GT" || term == "ST_RSHIFT_1" || term == "ST_RSHIFT_2")
					{
						--tmpl_depth;
						if (tmpl_depth == 0)
						{
							if (q == p + 1)
							{
								AddPos(out, q + 1);
							}
							else
							{
								vector<size_t> args = ParseNonterminal("template-argument-list", p + 1);
								for (size_t j = 0; j < args.size(); ++j)
								{
									if (args[j] == q) AddPos(out, q + 1);
								}
							}
							break;
						}
					}
				}
			}
		}
		return out;
	}

	vector<size_t> ParseNonterminalRule(const string& name, size_t pos)
	{
		vector<size_t> out;
		GrammarRule rule = grammar.rules.at(name);
		for (size_t i = 0; i < rule.alts.size(); ++i)
		{
			vector<size_t> part = ParseNode(rule.alts[i], pos);
			for (size_t j = 0; j < part.size(); ++j) AddPos(out, part[j]);
		}
		return out;
	}

	vector<size_t> ParseNonterminal(const string& name, size_t pos)
	{
		pair<string, size_t> key = make_pair(name, pos);
		map< pair<string, size_t>, vector<size_t> >::const_iterator it = memo.find(key);
		if (it != memo.end()) return it->second;
		if (active.count(key) != 0) return vector<size_t>();
		active.insert(key);

		vector<size_t> out;
		if (name == "simple-template-id")
		{
			out = ParseSimpleTemplateId(pos);
		}
		else if (name == "class-name" || name == "template-name" || name == "typedef-name" ||
			name == "enum-name" || name == "namespace-name")
		{
			out = ParseSpecialName(name, pos);
		}
		else
		{
			out = ParseNonterminalRule(name, pos);
		}

		active.erase(key);
		memo[key] = out;
		return out;
	}

	bool ParseTranslationUnit()
	{
		vector<size_t> res = ParseNonterminal("translation-unit", 0);
		for (size_t i = 0; i < res.size(); ++i)
		{
			if (res[i] == tokens.size()) return true;
		}
		return false;
	}
};

const Grammar& GetPA6Grammar()
{
	static Grammar grammar = LoadGrammar();
	return grammar;
}

bool HasBadTemplateLiteralAdjacency(const vector<PA6Token>& tokens)
{
	RecogParser parser(GetPA6Grammar(), tokens);
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (!tokens[i].is_identifier || !PA6_IsTemplateName(tokens[i].spell)) continue;
		if (i + 1 >= tokens.size() || tokens[i + 1].term != "OP_LT") continue;
		vector<size_t> ends = parser.ParseSimpleTemplateId(i);
		for (size_t j = 0; j < ends.size(); ++j)
		{
			if (ends[j] < tokens.size() && tokens[ends[j]].is_literal) return true;
		}
	}
	return false;
}

void DoRecog(const string& srcfile)
{
	vector<PPToken> preprocessed = PreprocessSourceTokens(srcfile);
	vector<PA6Token> tokens = LexForPA6(preprocessed);
	if (HasBadTemplateLiteralAdjacency(tokens))
	{
		throw runtime_error("bad template close");
	}
	RecogParser parser(GetPA6Grammar(), tokens);
	if (!parser.ParseTranslationUnit())
	{
		throw runtime_error("parse failed");
	}
}

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

		ofstream out(outfile);

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
