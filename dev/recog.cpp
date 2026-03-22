// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

#define CPPGM_PREPROC_NO_MAIN
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

string PA6TrimRight(const string& s)
{
	size_t e = s.size();
	while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
	return s.substr(0, e);
}

string PA6TrimLeft(const string& s)
{
	size_t b = 0;
	while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) b++;
	return s.substr(b);
}

string PA6Trim(const string& s)
{
	return PA6TrimLeft(PA6TrimRight(s));
}

bool PA6StartsWith(const string& s, const string& prefix)
{
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

struct PA6Expr
{
	enum Kind
	{
		EK_SYMBOL,
		EK_SEQ,
		EK_OPT,
		EK_STAR,
		EK_PLUS
	};

	Kind kind;
	string symbol;
	vector<PA6Expr> children;
};

PA6Expr PA6MakeSymbol(const string& symbol)
{
	PA6Expr e;
	e.kind = PA6Expr::EK_SYMBOL;
	e.symbol = symbol;
	return e;
}

PA6Expr PA6MakeUnary(PA6Expr::Kind kind, const PA6Expr& child)
{
	PA6Expr e;
	e.kind = kind;
	e.children.push_back(child);
	return e;
}

PA6Expr PA6MakeSeq(const vector<PA6Expr>& items)
{
	PA6Expr e;
	e.kind = PA6Expr::EK_SEQ;
	e.children = items;
	return e;
}

struct PA6Rule
{
	string name;
	vector<PA6Expr> alternatives;
};

struct PA6Grammar
{
	vector<PA6Rule> rules;
	unordered_map<string, size_t> index;

	bool HasRule(const string& name) const
	{
		return index.find(name) != index.end();
	}
};

struct PA6RhsParser
{
	vector<string> toks;
	size_t pos = 0;

	bool AtEnd() const
	{
		return pos >= toks.size();
	}

	string Peek() const
	{
		return AtEnd() ? string() : toks[pos];
	}

	string Take()
	{
		if (AtEnd()) throw runtime_error("bad grammar rhs");
		return toks[pos++];
	}

	PA6Expr ParseSequence(bool until_rparen)
	{
		vector<PA6Expr> items;
		while (!AtEnd())
		{
			if (until_rparen && Peek() == ")") break;
			items.push_back(ParsePostfix());
		}
		return PA6MakeSeq(items);
	}

	PA6Expr ParsePostfix()
	{
		PA6Expr base = ParsePrimary();
		if (!AtEnd())
		{
			string q = Peek();
			if (q == "?" || q == "*" || q == "+")
			{
				Take();
				if (q == "?") return PA6MakeUnary(PA6Expr::EK_OPT, base);
				if (q == "*") return PA6MakeUnary(PA6Expr::EK_STAR, base);
				return PA6MakeUnary(PA6Expr::EK_PLUS, base);
			}
		}
		return base;
	}

	PA6Expr ParsePrimary()
	{
		if (AtEnd()) throw runtime_error("bad grammar rhs");
		if (Peek() == "(")
		{
			Take();
			PA6Expr inner = ParseSequence(true);
			if (AtEnd() || Take() != ")")
				throw runtime_error("bad grammar rhs");
			return inner;
		}
		return PA6MakeSymbol(Take());
	}
};

vector<string> PA6TokenizeRhs(const string& rhs)
{
	vector<string> out;
	for (size_t i = 0; i < rhs.size(); )
	{
		char c = rhs[i];
		if (c == ' ' || c == '\t')
		{
			i++;
			continue;
		}
		if (c == '(' || c == ')' || c == '?' || c == '*' || c == '+')
		{
			out.push_back(string(1, c));
			i++;
			continue;
		}
		size_t j = i;
		while (j < rhs.size())
		{
			char d = rhs[j];
			if (d == ' ' || d == '\t' || d == '(' || d == ')' || d == '?' || d == '*' || d == '+')
				break;
			j++;
		}
		if (j == i)
			throw runtime_error("bad grammar rhs tokenization");
		out.push_back(rhs.substr(i, j - i));
		i = j;
	}
	return out;
}

PA6Expr PA6ParseRhsSequence(const string& rhs)
{
	PA6RhsParser p;
	p.toks = PA6TokenizeRhs(rhs);
	PA6Expr e = p.ParseSequence(false);
	if (!p.AtEnd())
		throw runtime_error("bad grammar rhs trailing tokens");
	return e;
}

PA6Grammar PA6LoadGrammar(const string& path)
{
	ifstream in(path.c_str(), ios::binary);
	if (!in) throw runtime_error("cannot open pa6.gram");

	vector<string> logical_lines;
	string line;
	while (getline(in, line))
	{
		line = PA6TrimRight(line);
		while (!line.empty() && line[line.size() - 1] == '\\')
		{
			line.pop_back();
			string next;
			if (!getline(in, next))
				throw runtime_error("bad grammar line splice");
			next = PA6TrimLeft(PA6TrimRight(next));
			if (!line.empty()) line += " ";
			line += next;
		}
		logical_lines.push_back(line);
	}

	PA6Grammar g;
	string cur_rule;
	for (size_t i = 0; i < logical_lines.size(); i++)
	{
		string ln = logical_lines[i];
		if (PA6Trim(ln).empty()) continue;

		size_t first = 0;
		while (first < ln.size() && (ln[first] == ' ' || ln[first] == '\t')) first++;
		bool indented = first > 0;

		if (!indented)
		{
			if (ln.empty() || ln[ln.size() - 1] != ':')
				throw runtime_error("bad grammar header line");
			string name = PA6Trim(ln.substr(0, ln.size() - 1));
			if (name.empty()) throw runtime_error("bad grammar rule name");
			cur_rule = name;
			if (g.index.find(name) == g.index.end())
			{
				size_t idx = g.rules.size();
				g.index[name] = idx;
				PA6Rule r;
				r.name = name;
				g.rules.push_back(r);
			}
			continue;
		}

		if (cur_rule.empty())
			throw runtime_error("bad grammar body without header");

		string rhs = PA6TrimLeft(ln);
		size_t idx = g.index.at(cur_rule);
		g.rules[idx].alternatives.push_back(PA6ParseRhsSequence(rhs));
	}

	return g;
}

struct PA6Token
{
	string kind;
	string source;
};

void PA6MarkTemplateAngleTokens(vector<PA6Token>& toks)
{
	vector<int> angle_nonangle_depth;
	int nonangle_depth = 0;

	for (size_t i = 0; i < toks.size(); i++)
	{
		if (toks[i].kind == "OP_LPAREN" || toks[i].kind == "OP_LSQUARE" || toks[i].kind == "OP_LBRACE")
		{
			nonangle_depth++;
			continue;
		}
		if (toks[i].kind == "OP_RPAREN" || toks[i].kind == "OP_RSQUARE" || toks[i].kind == "OP_RBRACE")
		{
			if (nonangle_depth > 0) nonangle_depth--;
			continue;
		}

		if (toks[i].kind == "OP_LT")
		{
			if (i > 0 && toks[i - 1].kind == "TT_IDENTIFIER" && PA6_IsTemplateName(toks[i - 1].source))
			{
				toks[i].kind = "ST_TA_LT";
				angle_nonangle_depth.push_back(nonangle_depth);
			}
			continue;
		}

		if (toks[i].kind == "OP_GT" || toks[i].kind == "ST_RSHIFT_1" || toks[i].kind == "ST_RSHIFT_2")
		{
			if (!angle_nonangle_depth.empty() && angle_nonangle_depth.back() == nonangle_depth)
			{
				if (toks[i].kind == "OP_GT") toks[i].kind = "ST_TA_GT";
				if (toks[i].kind == "ST_RSHIFT_1") toks[i].kind = "ST_TA_RSHIFT_1";
				if (toks[i].kind == "ST_RSHIFT_2") toks[i].kind = "ST_TA_RSHIFT_2";
				angle_nonangle_depth.pop_back();
			}
			continue;
		}
	}
}

string PA6SecondField(const string& s)
{
	size_t p = s.find(' ');
	if (p == string::npos) return s;
	return s.substr(0, p);
}

void PA6PushSimple(vector<PA6Token>& out, const string& source, const string& token_type)
{
	if (token_type == "OP_RSHIFT")
	{
		out.push_back({"ST_RSHIFT_1", source});
		out.push_back({"ST_RSHIFT_2", source});
		return;
	}
	out.push_back({token_type, source});
}

vector<PA6Token> PA6ToGrammarTokens(const string& posttoken_out)
{
	vector<PA6Token> out;

	istringstream iss(posttoken_out);
	string line;
	while (getline(iss, line))
	{
		line = PA6TrimRight(line);
		if (line.empty()) continue;

		if (PA6StartsWith(line, "simple "))
		{
			size_t last_space = line.find_last_of(' ');
			if (last_space == string::npos || last_space <= 7)
				throw runtime_error("bad posttoken simple line");
			string source = line.substr(7, last_space - 7);
			string token_type = line.substr(last_space + 1);
			PA6PushSimple(out, source, token_type);
			continue;
		}

		if (PA6StartsWith(line, "identifier "))
		{
			out.push_back({"TT_IDENTIFIER", line.substr(11)});
			continue;
		}

		if (PA6StartsWith(line, "literal "))
		{
			string src = PA6SecondField(line.substr(8));
			out.push_back({"TT_LITERAL", src});
			continue;
		}

		if (PA6StartsWith(line, "user-defined-literal "))
		{
			string src = PA6SecondField(line.substr(21));
			out.push_back({"TT_LITERAL", src});
			continue;
		}

		if (PA6StartsWith(line, "invalid "))
			throw runtime_error("invalid token in phase 7");

		if (line == "eof")
			continue;

		throw runtime_error("unknown posttoken output line");
	}

	PA6MarkTemplateAngleTokens(out);
	out.push_back({"ST_EOF", ""});
	return out;
}

struct PA6MemoKey
{
	size_t rule_idx;
	size_t pos;

	bool operator==(const PA6MemoKey& rhs) const
	{
		return rule_idx == rhs.rule_idx && pos == rhs.pos;
	}
};

struct PA6MemoKeyHash
{
	size_t operator()(const PA6MemoKey& k) const
	{
		size_t h1 = std::hash<size_t>()(k.rule_idx);
		size_t h2 = std::hash<size_t>()(k.pos);
		return h1 ^ (h2 * 1315423911u);
	}
};

struct PA6Recognizer
{
	const PA6Grammar& grammar;
	const vector<PA6Token>& toks;

	unordered_map<PA6MemoKey, vector<size_t>, PA6MemoKeyHash> memo;
	unordered_set<PA6MemoKey, PA6MemoKeyHash> in_progress;

	PA6Recognizer(const PA6Grammar& grammar, const vector<PA6Token>& toks)
		: grammar(grammar), toks(toks)
	{
	}

	vector<size_t> ParseRule(const string& name, size_t pos)
	{
		auto it = grammar.index.find(name);
		if (it == grammar.index.end())
			throw runtime_error("missing grammar rule");
		return ParseRuleIndex(it->second, pos);
	}

	vector<size_t> ParseRuleIndex(size_t rule_idx, size_t pos)
	{
		PA6MemoKey key{rule_idx, pos};
		auto mit = memo.find(key);
		if (mit != memo.end())
			return mit->second;

		if (in_progress.find(key) != in_progress.end())
			return {};
		in_progress.insert(key);

		const string& name = grammar.rules[rule_idx].name;
		vector<size_t> out;

		if (name == "class-name")
		{
			out = ParseClassName(pos);
		}
		else if (name == "enum-name")
		{
			out = ParseIdentifierCategory(pos, PA6_IsEnumName);
		}
		else if (name == "namespace-name")
		{
			out = ParseIdentifierCategory(pos, PA6_IsNamespaceName);
		}
		else if (name == "template-name")
		{
			out = ParseIdentifierCategory(pos, PA6_IsTemplateName);
		}
		else if (name == "typedef-name")
		{
			out = ParseIdentifierCategory(pos, PA6_IsTypedefName);
		}
		else if (name == "relational-operator")
		{
			out = ParseRelationalOperator(pos);
		}
		else if (name == "shift-operator")
		{
			out = ParseShiftOperator(pos);
		}
		else
		{
			const PA6Rule& r = grammar.rules[rule_idx];
			for (size_t i = 0; i < r.alternatives.size(); i++)
				AppendUnique(out, ParseExpr(r.alternatives[i], pos));
		}

		sort(out.begin(), out.end());
		out.erase(unique(out.begin(), out.end()), out.end());

		in_progress.erase(key);
		memo[key] = out;
		return out;
	}

	vector<size_t> ParseIdentifierCategory(size_t pos, bool (*pred)(const string&))
	{
		vector<size_t> out;
		if (pos < toks.size() && toks[pos].kind == "TT_IDENTIFIER" && pred(toks[pos].source))
			out.push_back(pos + 1);
		return out;
	}

	vector<size_t> ParseClassName(size_t pos)
	{
		vector<size_t> out;
		if (pos >= toks.size() || toks[pos].kind != "TT_IDENTIFIER")
			return out;
		if (!PA6_IsClassName(toks[pos].source))
			return out;

		out.push_back(pos + 1);
		AppendUnique(out, ParseRule("simple-template-id", pos));
		sort(out.begin(), out.end());
		out.erase(unique(out.begin(), out.end()), out.end());
		return out;
	}

	vector<size_t> ParseRelationalOperator(size_t pos)
	{
		if (pos >= toks.size()) return {};
		const string& k = toks[pos].kind;
		if (k == "OP_LT")
		{
			if (pos > 0 && toks[pos - 1].kind == "TT_IDENTIFIER" && PA6_IsTemplateName(toks[pos - 1].source))
				return {};
			return vector<size_t>(1, pos + 1);
		}
		if (k == "OP_GT" || k == "OP_LE" || k == "OP_GE")
			return vector<size_t>(1, pos + 1);
		return {};
	}

	vector<size_t> ParseShiftOperator(size_t pos)
	{
		if (pos >= toks.size()) return {};
		if (toks[pos].kind == "OP_LSHIFT") return vector<size_t>(1, pos + 1);
		if (pos + 1 < toks.size() && toks[pos].kind == "ST_RSHIFT_1" && toks[pos + 1].kind == "ST_RSHIFT_2")
			return vector<size_t>(1, pos + 2);
		return {};
	}

	vector<size_t> ParseExpr(const PA6Expr& e, size_t pos)
	{
		switch (e.kind)
		{
		case PA6Expr::EK_SYMBOL:
			if (grammar.HasRule(e.symbol))
				return ParseRule(e.symbol, pos);
			return MatchTerminal(e.symbol, pos);

		case PA6Expr::EK_SEQ:
		{
			vector<size_t> cur(1, pos);
			for (size_t i = 0; i < e.children.size(); i++)
			{
				vector<size_t> nxt;
				for (size_t j = 0; j < cur.size(); j++)
					AppendUnique(nxt, ParseExpr(e.children[i], cur[j]));
				sort(nxt.begin(), nxt.end());
				nxt.erase(unique(nxt.begin(), nxt.end()), nxt.end());
				cur.swap(nxt);
				if (cur.empty()) break;
			}
			return cur;
		}

		case PA6Expr::EK_OPT:
		{
			vector<size_t> out(1, pos);
			AppendUnique(out, ParseExpr(e.children[0], pos));
			sort(out.begin(), out.end());
			out.erase(unique(out.begin(), out.end()), out.end());
			return out;
		}

		case PA6Expr::EK_STAR:
			return ParseStar(e.children[0], pos);

		case PA6Expr::EK_PLUS:
		{
			vector<size_t> first = ParseExpr(e.children[0], pos);
			vector<size_t> out;
			for (size_t i = 0; i < first.size(); i++)
				AppendUnique(out, ParseStar(e.children[0], first[i]));
			sort(out.begin(), out.end());
			out.erase(unique(out.begin(), out.end()), out.end());
			return out;
		}
		}

		throw runtime_error("bad parse expression kind");
	}

	vector<size_t> ParseStar(const PA6Expr& sub, size_t pos)
	{
		vector<char> seen(toks.size() + 1, 0);
		queue<size_t> q;
		seen[pos] = 1;
		q.push(pos);

		while (!q.empty())
		{
			size_t cur = q.front();
			q.pop();

			vector<size_t> nxt = ParseExpr(sub, cur);
			for (size_t i = 0; i < nxt.size(); i++)
			{
				size_t n = nxt[i];
				if (n == cur) continue;
				if (n > toks.size()) continue;
				if (!seen[n])
				{
					seen[n] = 1;
					q.push(n);
				}
			}
		}

		vector<size_t> out;
		for (size_t i = 0; i < seen.size(); i++)
			if (seen[i]) out.push_back(i);
		return out;
	}

	vector<size_t> MatchTerminal(const string& term, size_t pos)
	{
		if (pos >= toks.size()) return {};
		const PA6Token& t = toks[pos];

		if (term == "TT_IDENTIFIER")
			return (t.kind == "TT_IDENTIFIER") ? vector<size_t>(1, pos + 1) : vector<size_t>();

		if (term == "TT_LITERAL")
			return (t.kind == "TT_LITERAL") ? vector<size_t>(1, pos + 1) : vector<size_t>();

		if (term == "ST_EMPTYSTR")
			return (t.kind == "TT_LITERAL" && t.source == "\"\"") ? vector<size_t>(1, pos + 1) : vector<size_t>();

		if (term == "ST_ZERO")
			return (t.kind == "TT_LITERAL" && t.source == "0") ? vector<size_t>(1, pos + 1) : vector<size_t>();

		if (term == "ST_OVERRIDE")
			return (t.kind == "TT_IDENTIFIER" && t.source == "override") ? vector<size_t>(1, pos + 1) : vector<size_t>();

		if (term == "ST_FINAL")
			return (t.kind == "TT_IDENTIFIER" && t.source == "final") ? vector<size_t>(1, pos + 1) : vector<size_t>();

		if (term == "ST_NONPAREN")
		{
			if (t.kind == "OP_LPAREN" || t.kind == "OP_RPAREN" ||
				t.kind == "OP_LSQUARE" || t.kind == "OP_RSQUARE" ||
				t.kind == "OP_LBRACE" || t.kind == "OP_RBRACE" ||
				t.kind == "ST_EOF")
				return {};
			return vector<size_t>(1, pos + 1);
		}

		if (term == "OP_LT")
		{
			if (t.kind == "OP_LT" || t.kind == "ST_TA_LT")
				return vector<size_t>(1, pos + 1);
			return {};
		}

		if (term == "OP_GT")
		{
			if (t.kind == "OP_GT" || t.kind == "ST_TA_GT")
				return vector<size_t>(1, pos + 1);
			return {};
		}

		if (term == "ST_RSHIFT_1")
		{
			if (t.kind == "ST_RSHIFT_1" || t.kind == "ST_TA_RSHIFT_1")
				return vector<size_t>(1, pos + 1);
			return {};
		}

		if (term == "ST_RSHIFT_2")
		{
			if (t.kind == "ST_RSHIFT_2" || t.kind == "ST_TA_RSHIFT_2")
				return vector<size_t>(1, pos + 1);
			return {};
		}

		if (t.kind == term)
			return vector<size_t>(1, pos + 1);

		return {};
	}

	static void AppendUnique(vector<size_t>& dst, const vector<size_t>& src)
	{
		for (size_t i = 0; i < src.size(); i++)
			dst.push_back(src[i]);
	}
};

bool PA6RecognizeOne(const string& srcfile, const PA6Grammar& grammar, const string& date_lit, const string& time_lit)
{
	PA5Engine engine(date_lit, time_lit);
	vector<PPToken> preprocessed;
	engine.ProcessFile(srcfile, srcfile, preprocessed);

	ostringstream posttoken_capture;
	EmitPostTokensOrThrow(posttoken_capture, preprocessed);

	vector<PA6Token> toks = PA6ToGrammarTokens(posttoken_capture.str());
	PA6Recognizer recognizer(grammar, toks);
	vector<size_t> ends = recognizer.ParseRule("translation-unit", 0);

	for (size_t i = 0; i < ends.size(); i++)
	{
		if (ends[i] == toks.size())
			return true;
	}
	return false;
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

		time_t now = time(nullptr);
		string now_s = asctime(localtime(&now));
		string date_lit = BuildDateLiteralFromAsctime(now_s);
		string time_lit = BuildTimeLiteralFromAsctime(now_s);

		PA6Grammar grammar = PA6LoadGrammar("pa6.gram");

		ofstream out(outfile.c_str(), ios::binary);
		out << "recog " << nsrcfiles << endl;

		for (size_t i = 0; i < nsrcfiles; i++)
		{
			string srcfile = args[i + 2];
			try
			{
				bool ok = PA6RecognizeOne(srcfile, grammar, date_lit, time_lit);
				out << srcfile << (ok ? " OK" : " BAD") << endl;
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
