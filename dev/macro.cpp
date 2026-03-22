// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>

using namespace std;

#define CPPGM_PPTOKEN_LIBRARY
#define main CPPGM_POSTTOKEN_MAIN
#include "posttoken.cpp"
#undef main

namespace
{
	struct Token
	{
		Kind kind;
		string data;
		size_t line = 1;
		bool space_before = false;
		unordered_set<string> hide;
		bool placemarker = false;
		bool macro_op = false;
	};

	struct Line
	{
		vector<Token> tokens;
		bool is_directive = false;
	};

	struct Macro
	{
		bool function_like = false;
		bool variadic = false;
		vector<string> params;
		vector<Token> replacement;
	};

	bool is_ws_kind(Kind k)
	{
		return k == Kind::WS;
	}

	bool is_nl_kind(Kind k)
	{
		return k == Kind::NL || k == Kind::EOFK;
	}

	bool is_ident(const Token& t)
	{
		return t.kind == Kind::IDENT;
	}

	bool is_punc(const Token& t, const string& s)
	{
		return t.kind == Kind::PUNC && t.data == s;
	}

	Token from_raw(const Tok& t)
	{
		Token out;
		out.kind = t.kind;
		out.data = t.data;
		out.line = t.line;
		out.macro_op = false;
		return out;
	}

	bool same_tokens(const vector<Token>& a, const vector<Token>& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (a[i].kind != b[i].kind || a[i].data != b[i].data)
				return false;
			if (a[i].placemarker != b[i].placemarker || a[i].macro_op != b[i].macro_op)
				return false;
			if (i > 0 && a[i].space_before != b[i].space_before)
				return false;
		}
		return true;
	}

	vector<Token> strip_ws_line(const vector<Tok>& toks, size_t begin, size_t end, bool& is_directive)
	{
		vector<Token> out;
		bool saw_nonws = false;
		bool pending_space = false;
		is_directive = false;
		for (size_t i = begin; i < end; ++i)
		{
			if (is_ws_kind(toks[i].kind))
			{
				pending_space = true;
				continue;
			}
			Token t = from_raw(toks[i]);
			t.space_before = saw_nonws ? pending_space : false;
			pending_space = false;
			if (!saw_nonws && t.kind == Kind::PUNC && t.data == "#")
				is_directive = true;
			saw_nonws = true;
			out.push_back(t);
		}
		return out;
	}

	string serialize_segment(const vector<Token>& toks)
	{
		string out;
		bool first = true;
		for (size_t i = 0; i < toks.size(); ++i)
		{
			if (toks[i].placemarker)
				continue;
			if (!first)
				out.push_back(' ');
			out += toks[i].data;
			first = false;
		}
		return out;
	}

	int param_index(const Macro& m, const string& name)
	{
		for (size_t i = 0; i < m.params.size(); ++i)
			if (m.params[i] == name)
				return static_cast<int>(i);
		if (m.variadic && name == "__VA_ARGS__")
			return static_cast<int>(m.params.size());
		return -1;
	}

	bool is_param_name(const Macro& m, const string& name)
	{
		return param_index(m, name) >= 0;
	}

	string stringify_arg(const vector<Token>& arg)
	{
		string out = "\"";
		bool first = true;
		for (size_t i = 0; i < arg.size(); ++i)
		{
			if (arg[i].placemarker)
				continue;
			if (!first && arg[i].space_before)
				out.push_back(' ');
			bool escape_backslash = arg[i].kind == Kind::CHAR || arg[i].kind == Kind::UCHAR || arg[i].kind == Kind::STR || arg[i].kind == Kind::USTR || arg[i].kind == Kind::HEADER;
			for (size_t j = 0; j < arg[i].data.size(); ++j)
			{
				char c = arg[i].data[j];
				if (c == '"' || (escape_backslash && c == '\\'))
					out.push_back('\\');
				out.push_back(c);
			}
			first = false;
		}
		out.push_back('"');
		return out;
	}

	vector<Token> tokenize_spelling(const string& source)
	{
		Sink sink;
		PPTokenizer tok(sink);
		for (size_t i = 0; i < source.size(); ++i)
			tok.process(static_cast<unsigned char>(source[i]));
		tok.process(EndOfFile);
		vector<Token> out;
		for (size_t i = 0; i < sink.toks.size(); ++i)
		{
			if (sink.toks[i].kind == Kind::EOFK)
				break;
			if (sink.toks[i].kind == Kind::WS || sink.toks[i].kind == Kind::NL)
				continue;
			out.push_back(from_raw(sink.toks[i]));
		}
		return out;
	}

	void add_hide(Token& t, const Token& head, const string& macro_name)
	{
		t.hide.insert(head.hide.begin(), head.hide.end());
		if (!macro_name.empty())
			t.hide.insert(macro_name);
	}

	void append_sequence(vector<Token>& out, const vector<Token>& seq, const Token& head, const string& macro_name)
	{
		for (size_t i = 0; i < seq.size(); ++i)
		{
			Token t = seq[i];
			add_hide(t, head, macro_name);
			if (out.empty() && i == 0)
				t.space_before = head.space_before;
			else if (i == 0)
				t.space_before = true;
			out.push_back(t);
		}
	}

	Token make_string_token(const vector<Token>& arg, const Token& head, const string& macro_name)
	{
		Token t;
		t.kind = Kind::STR;
		t.data = stringify_arg(arg);
		add_hide(t, head, macro_name);
		t.space_before = head.space_before;
		return t;
	}

	Token make_placemarker(const Token& head, const string& macro_name)
	{
		Token t;
		t.placemarker = true;
		add_hide(t, head, macro_name);
		t.space_before = head.space_before;
		return t;
	}

	vector<Token> paste_two(const Token& left, const Token& right, const Token& head, const string& macro_name)
	{
		if (left.placemarker && right.placemarker)
			return vector<Token>(1, make_placemarker(head, macro_name));
		if (left.placemarker)
		{
			Token t = right;
			add_hide(t, head, macro_name);
			t.space_before = left.space_before;
			return vector<Token>(1, t);
		}
		if (right.placemarker)
		{
			Token t = left;
			add_hide(t, head, macro_name);
			t.space_before = left.space_before;
			return vector<Token>(1, t);
		}

		string pasted = left.data + right.data;
		vector<Token> toks = tokenize_spelling(pasted);
		if (toks.empty())
			throw runtime_error("invalid ## result");
		for (size_t i = 0; i < toks.size(); ++i)
		{
			add_hide(toks[i], head, macro_name);
			if (i == 0)
				toks[i].space_before = left.space_before;
			toks[i].macro_op = false;
		}
		return toks;
	}

	vector<vector<Token> > split_argument_chunks(const vector<Token>& toks, size_t begin, size_t end)
	{
		vector<vector<Token> > chunks;
		vector<Token> cur;
		int depth = 0;
		bool saw_any = false;
		for (size_t i = begin; i < end; ++i)
		{
			if (is_punc(toks[i], "("))
			{
				++depth;
				cur.push_back(toks[i]);
				saw_any = true;
			}
			else if (is_punc(toks[i], ")"))
			{
				--depth;
				cur.push_back(toks[i]);
				saw_any = true;
			}
			else if (depth == 0 && is_punc(toks[i], ","))
			{
				chunks.push_back(cur);
				cur.clear();
				saw_any = true;
			}
			else
			{
				cur.push_back(toks[i]);
				saw_any = true;
			}
		}
		if (saw_any)
			chunks.push_back(cur);
		return chunks;
	}

	bool parse_invocation(const vector<Token>& toks, size_t lparen, const Macro& m, const string& macro_name, vector<vector<Token> >& raw_args, size_t& end_pos)
	{
		if (!is_punc(toks[lparen], "("))
			return false;

		size_t close = string::npos;
		int depth = 0;
		for (size_t i = lparen + 1; i < toks.size(); ++i)
		{
			if (is_punc(toks[i], "("))
			{
				++depth;
				continue;
			}
			if (is_punc(toks[i], ")"))
			{
				if (depth == 0)
				{
					close = i;
					break;
				}
				--depth;
			}
		}
		if (close == string::npos)
			return false;

		vector<vector<Token> > chunks = split_argument_chunks(toks, lparen + 1, close);
		if (chunks.empty())
		{
			if (m.params.empty() && !m.variadic)
			{
				raw_args.clear();
				end_pos = close + 1;
				return true;
			}
			chunks.push_back(vector<Token>());
		}

		if (!m.variadic)
		{
			if (chunks.size() != m.params.size())
				throw runtime_error(string("macro function-like invocation wrong num of params: ") + macro_name);
			raw_args = chunks;
			end_pos = close + 1;
			return true;
		}

		size_t fixed = m.params.size();
		if (fixed == 0)
		{
			raw_args.resize(1);
			for (size_t i = 0; i < chunks.size(); ++i)
			{
				if (i)
				{
					Token comma;
					comma.kind = Kind::PUNC;
					comma.data = ",";
					raw_args[0].push_back(comma);
				}
				raw_args[0].insert(raw_args[0].end(), chunks[i].begin(), chunks[i].end());
			}
			end_pos = close + 1;
			return true;
		}

		if (chunks.size() < fixed)
			throw runtime_error(string("macro function-like invocation wrong num of params: ") + macro_name);
		raw_args.clear();
		for (size_t i = 0; i < fixed; ++i)
			raw_args.push_back(chunks[i]);
		vector<Token> varargs;
		for (size_t i = fixed; i < chunks.size(); ++i)
		{
			if (i > fixed)
			{
				Token comma;
				comma.kind = Kind::PUNC;
				comma.data = ",";
				varargs.push_back(comma);
			}
			varargs.insert(varargs.end(), chunks[i].begin(), chunks[i].end());
		}
		raw_args.push_back(varargs);
		end_pos = close + 1;
		return true;
	}

	vector<Token> expand_tokens(vector<Token> toks, const unordered_map<string, Macro>& macros);

	vector<Token> collapse_pastes(vector<Token> seq, const Token& head, const string& macro_name)
	{
		for (size_t i = 0; i < seq.size(); ++i)
		{
			if (seq[i].placemarker)
				continue;
			if (seq[i].kind != Kind::PUNC || seq[i].data != "##" || !seq[i].macro_op)
				continue;
			if (i == 0 || i + 1 >= seq.size())
				throw runtime_error("## at edge of replacement list");
			vector<Token> pasted = paste_two(seq[i - 1], seq[i + 1], head, macro_name);
			seq.erase(seq.begin() + i - 1, seq.begin() + i + 2);
			seq.insert(seq.begin() + i - 1, pasted.begin(), pasted.end());
			if (i > 0)
				--i;
		}
		return seq;
	}

	vector<Token> substitute_macro(const Macro& m, const Token& head, const string& macro_name, const vector<vector<Token> >& raw_args, const unordered_map<string, Macro>& macros)
	{
		vector<Token> seq;
		for (size_t i = 0; i < m.replacement.size(); ++i)
		{
			const Token& rt = m.replacement[i];
			if (m.function_like && is_punc(rt, "#"))
			{
				if (i + 1 >= m.replacement.size())
					throw runtime_error("# at end of function-like macro replacement list");
				int idx = param_index(m, m.replacement[i + 1].data);
				if (idx < 0)
					throw runtime_error("# must be followed by parameter in function-like macro");
				seq.push_back(make_string_token(raw_args[static_cast<size_t>(idx)], head, macro_name));
				i += 1;
				continue;
			}

			if (m.function_like && is_punc(rt, "##"))
			{
				Token t;
				t.kind = Kind::PUNC;
				t.data = "##";
				t.macro_op = true;
				seq.push_back(t);
				continue;
			}

			int idx = -1;
			if (m.function_like && is_ident(rt))
				idx = param_index(m, rt.data);
			if (idx >= 0)
			{
				bool raw = (i > 0 && is_punc(m.replacement[i - 1], "##")) || (i + 1 < m.replacement.size() && is_punc(m.replacement[i + 1], "##"));
				vector<Token> expanded_arg;
				const vector<Token>* arg = &raw_args[static_cast<size_t>(idx)];
				if (!raw)
				{
					expanded_arg = expand_tokens(raw_args[static_cast<size_t>(idx)], macros);
					arg = &expanded_arg;
				}
				if (arg->empty())
				{
					if (raw)
						seq.push_back(make_placemarker(head, macro_name));
				}
				else
				{
					append_sequence(seq, *arg, head, macro_name);
				}
				continue;
			}

			Token t = rt;
			add_hide(t, head, macro_name);
			seq.push_back(t);
		}

		if (!m.function_like)
		{
			seq = collapse_pastes(seq, head, macro_name);
			for (size_t i = 0; i < seq.size(); ++i)
			{
				if (!seq[i].placemarker)
					add_hide(seq[i], head, macro_name);
			}
			if (!seq.empty())
				seq[0].space_before = head.space_before;
			return seq;
		}

		vector<Token> collapsed = collapse_pastes(seq, head, macro_name);
		for (size_t i = 0; i < collapsed.size(); ++i)
		{
			if (!collapsed[i].placemarker)
				add_hide(collapsed[i], head, macro_name);
		}
		if (!collapsed.empty())
			collapsed[0].space_before = head.space_before;
		return collapsed;
	}

	vector<Token> expand_tokens(vector<Token> toks, const unordered_map<string, Macro>& macros)
	{
		while (true)
		{
			bool changed = false;
			for (size_t i = 0; i < toks.size(); ++i)
			{
				if (toks[i].placemarker)
					continue;
				if (toks[i].kind != Kind::IDENT)
				{
					if (toks[i].kind == Kind::IDENT && toks[i].data == "__VA_ARGS__")
						throw runtime_error("__VA_ARGS__ token in text-lines: " + toks[i].data);
					continue;
				}
				if (toks[i].data == "__VA_ARGS__")
					throw runtime_error("__VA_ARGS__ token in text-lines: " + toks[i].data);
				unordered_map<string, Macro>::const_iterator it = macros.find(toks[i].data);
				if (it == macros.end())
					continue;
				if (toks[i].hide.find(toks[i].data) != toks[i].hide.end())
					continue;

				const Macro& m = it->second;
				size_t end_pos = i + 1;
				vector<vector<Token> > raw_args;
				if (m.function_like)
				{
					if (i + 1 >= toks.size() || !is_punc(toks[i + 1], "("))
						continue;
					if (!parse_invocation(toks, i + 1, m, toks[i].data, raw_args, end_pos))
						continue;
				}

				vector<Token> repl = substitute_macro(m, toks[i], toks[i].data, raw_args, macros);
				toks.erase(toks.begin() + i, toks.begin() + end_pos);
				toks.insert(toks.begin() + i, repl.begin(), repl.end());
				changed = true;
				break;
			}
			if (!changed)
				break;
		}
		return toks;
	}

	bool same_macro(const Macro& a, const Macro& b)
	{
		return a.function_like == b.function_like && a.variadic == b.variadic && a.params == b.params && same_tokens(a.replacement, b.replacement);
	}

	void validate_define(const Macro& m)
	{
		if (!m.function_like)
		{
			for (size_t i = 0; i < m.replacement.size(); ++i)
			{
				if (m.replacement[i].kind == Kind::IDENT && m.replacement[i].data == "__VA_ARGS__")
					throw runtime_error("invalid __VA_ARGS__ use");
				if (m.replacement[i].kind == Kind::PUNC && m.replacement[i].data == "##")
				{
					if (i == 0 || i + 1 >= m.replacement.size())
						throw runtime_error("## at edge of replacement list");
				}
			}
			return;
		}

		for (size_t i = 0; i < m.params.size(); ++i)
		{
			if (m.params[i] == "__VA_ARGS__")
				throw runtime_error("invalid __VA_ARGS__ use");
		}
		for (size_t i = 0; i < m.replacement.size(); ++i)
		{
			if (m.replacement[i].kind == Kind::IDENT && m.replacement[i].data == "__VA_ARGS__" && !m.variadic)
				throw runtime_error("invalid __VA_ARGS__ use");
			if (m.replacement[i].kind == Kind::PUNC && m.replacement[i].data == "#")
			{
				if (i + 1 >= m.replacement.size())
					throw runtime_error("# at end of function-like macro replacement list");
				if (!is_ident(m.replacement[i + 1]) || !is_param_name(m, m.replacement[i + 1].data))
					throw runtime_error("# must be followed by parameter in function-like macro");
			}
			if (m.replacement[i].kind == Kind::PUNC && m.replacement[i].data == "##")
			{
				if (i == 0 || i + 1 >= m.replacement.size())
					throw runtime_error("## at edge of replacement list");
			}
		}
	}

	Macro parse_define(const vector<Token>& line, string& name)
	{
		if (line.size() < 2 || !is_punc(line[0], "#") || !is_ident(line[1]) || line[1].data != "define")
			throw runtime_error("expected new line");
		Macro m;
		size_t i = 2;
		if (i >= line.size())
			throw runtime_error("expected identifier");
		if (!is_ident(line[i]))
			throw runtime_error("expected identifier");
		name = line[i].data;
		if (name == "__VA_ARGS__")
			throw runtime_error("invalid __VA_ARGS__ use");
		++i;

		if (i < line.size() && is_punc(line[i], "(") && !line[i].space_before)
		{
			m.function_like = true;
			++i;
			if (i >= line.size())
				throw runtime_error("expected identifier after lparen");
			if (i < line.size() && is_punc(line[i], ")"))
			{
				++i;
			}
			else
			{
				while (i < line.size())
				{
					if (is_punc(line[i], ")"))
					{
						++i;
						break;
					}
					if (is_punc(line[i], ","))
					{
						++i;
						if (i >= line.size() || is_punc(line[i], ")"))
							throw runtime_error("expected identifier");
						continue;
					}
					if (is_punc(line[i], "..."))
					{
						m.variadic = true;
						++i;
						if (i < line.size() && is_punc(line[i], ")"))
						{
							++i;
							break;
						}
						throw runtime_error("expected identifier");
					}
					if (!is_ident(line[i]))
						throw runtime_error("expected identifier");
					if (line[i].data == "__VA_ARGS__")
						throw runtime_error("__VA_ARGS__ in macro parameter list");
					for (size_t j = 0; j < m.params.size(); ++j)
					{
						if (m.params[j] == line[i].data)
							throw runtime_error(string("duplicate parameter ") + line[i].data + " in macro definition");
					}
					m.params.push_back(line[i].data);
					++i;
					if (i < line.size() && is_punc(line[i], ","))
					{
						++i;
						if (i >= line.size() || is_punc(line[i], ")"))
							throw runtime_error("expected identifier");
						continue;
					}
					if (i < line.size() && is_punc(line[i], ")"))
					{
						++i;
						break;
					}
					throw runtime_error("expected identifier");
				}
			}
		}

		for (; i < line.size(); ++i)
		{
			Token t = line[i];
			if (t.kind == Kind::PUNC && t.data == "##")
				t.macro_op = true;
			m.replacement.push_back(t);
		}

		validate_define(m);
		return m;
	}

	void parse_undef(const vector<Token>& line, unordered_map<string, Macro>& macros)
	{
		if (line.size() < 3 || !is_punc(line[0], "#") || !is_ident(line[1]) || line[1].data != "undef")
			throw runtime_error("expected new line");
		if (!is_ident(line[2]))
			throw runtime_error("expected identifier");
		if (line[2].data == "__VA_ARGS__")
			throw runtime_error("invalid __VA_ARGS__ use");
		if (line.size() > 3)
			throw runtime_error("expected new line");
		macros.erase(line[2].data);
	}

	string process_segment(vector<Token> segment, const unordered_map<string, Macro>& macros)
	{
		vector<Token> expanded = expand_tokens(segment, macros);
		return serialize_segment(expanded);
	}
}

#ifndef CPPGM_MACRO_LIBRARY
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

		unordered_map<string, Macro> macros;
		string expanded_source;
		vector<Token> segment;
		for (size_t i = 0; i < sink.toks.size(); )
		{
			size_t j = i;
			while (j < sink.toks.size() && !is_nl_kind(sink.toks[j].kind))
				++j;
			bool is_directive = false;
			vector<Token> line = strip_ws_line(sink.toks, i, j, is_directive);
			if (is_directive)
			{
				if (!segment.empty())
				{
					string s = process_segment(segment, macros);
					if (!s.empty())
					{
						if (!expanded_source.empty())
							expanded_source.push_back('\n');
						expanded_source += s;
					}
					segment.clear();
				}
				if (line.size() >= 2 && is_ident(line[1]) && line[1].data == "define")
				{
					string name;
					Macro m = parse_define(line, name);
					unordered_map<string, Macro>::iterator it = macros.find(name);
					if (it != macros.end())
					{
						if (!same_macro(it->second, m))
							throw runtime_error("macro redefined");
					}
					else
					{
						macros[name] = m;
					}
				}
				else if (line.size() >= 2 && is_ident(line[1]) && line[1].data == "undef")
				{
					parse_undef(line, macros);
				}
				else
				{
					throw runtime_error("expected new line");
				}
			}
			else
			{
				if (!line.empty())
				{
					if (!segment.empty())
						line[0].space_before = true;
					segment.insert(segment.end(), line.begin(), line.end());
				}
			}
			if (j < sink.toks.size() && sink.toks[j].kind == Kind::EOFK)
				break;
			i = j + 1;
		}
		if (!segment.empty())
		{
			string s = process_segment(segment, macros);
			if (!s.empty())
			{
				if (!expanded_source.empty())
					expanded_source.push_back('\n');
				expanded_source += s;
			}
		}

		istringstream iss(expanded_source);
		streambuf* old = cin.rdbuf(iss.rdbuf());
		int rc = CPPGM_POSTTOKEN_MAIN();
		cin.rdbuf(old);
		return rc;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
