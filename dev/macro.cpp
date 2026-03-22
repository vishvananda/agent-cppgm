// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#ifndef CPPGM_POSTTOKEN_INTERNAL_MAIN
#define CPPGM_POSTTOKEN_INTERNAL_MAIN posttoken_internal_main
#endif
#define main CPPGM_POSTTOKEN_INTERNAL_MAIN
#include "posttoken.cpp"
#undef main

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <set>
#include <map>

using namespace std;

struct MacroDef
{
	bool function_like;
	bool variadic;
	vector<string> params;
	vector<PPToken> replacement;

	MacroDef() : function_like(false), variadic(false) {}
};

vector<PPToken> CollapseWhitespace(const vector<PPToken>& tokens);

string MacroKey(const MacroDef& def)
{
	ostringstream oss;
	oss << def.function_like << "|" << def.variadic;
	for (size_t i = 0; i < def.params.size(); ++i)
	{
		oss << "|" << def.params[i];
	}
	oss << "|/";
	vector<PPToken> replacement = CollapseWhitespace(def.replacement);
	for (size_t i = 0; i < replacement.size(); ++i)
	{
		oss << static_cast<int>(replacement[i].kind) << ":" << replacement[i].data << "/";
	}
	return oss.str();
}

struct MToken
{
	PPToken tok;
	set<string> blocked;
};

vector<MToken> ToMTokens(const vector<PPToken>& tokens)
{
	vector<MToken> out;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		out.push_back({tokens[i], set<string>()});
	}
	return out;
}

vector<PPToken> ToPPTokens(const vector<MToken>& tokens)
{
	vector<PPToken> out;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		out.push_back(tokens[i].tok);
	}
	return out;
}

vector<PPToken> CollapseWhitespace(const vector<PPToken>& tokens)
{
	vector<PPToken> out;
	bool in_ws = false;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (tokens[i].kind == PP_WS || tokens[i].kind == PP_NL)
		{
			if (!in_ws)
			{
				out.push_back({PP_WS, ""});
				in_ws = true;
			}
		}
		else
		{
			out.push_back(tokens[i]);
			in_ws = false;
		}
	}
	if (!out.empty() && out.front().kind == PP_WS) out.erase(out.begin());
	if (!out.empty() && out.back().kind == PP_WS) out.pop_back();
	return out;
}

bool TokensEqual(const vector<PPToken>& a, const vector<PPToken>& b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].kind != b[i].kind || a[i].data != b[i].data) return false;
	}
	return true;
}

vector<MToken> ExpandTokens(const vector<MToken>& tokens, map<string, MacroDef>& macros, set<string> disabled, bool allow_tail_attach = true);

vector<MToken> RetokenizeFragment(const string& text)
{
	vector<int> cps = TransformSource(DecodeUTF8(text));
	PPCollector collector;
	PPTokenizer tokenizer(collector);
	vector<PPToken> toks = tokenizer.tokenize(cps);
	vector<PPToken> out;
	for (size_t i = 0; i < toks.size(); ++i)
	{
		if (toks[i].kind != PP_WS && toks[i].kind != PP_NL)
		{
			out.push_back(toks[i]);
		}
	}
	return ToMTokens(out);
}

string StringizeTokens(const vector<PPToken>& tokens)
{
	string body;
	bool need_space = false;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (tokens[i].kind == PP_WS)
		{
			need_space = !body.empty();
			continue;
		}
		if (need_space)
		{
			body.push_back(' ');
			need_space = false;
		}
		bool escape_chars =
			tokens[i].kind == PP_STRING || tokens[i].kind == PP_UD_STRING ||
			tokens[i].kind == PP_CHAR || tokens[i].kind == PP_UD_CHAR;
		for (size_t j = 0; j < tokens[i].data.size(); ++j)
		{
			char c = tokens[i].data[j];
			if (escape_chars && (c == '\\' || c == '"'))
			{
				body.push_back('\\');
			}
			body.push_back(c);
		}
	}
	return "\"" + body + "\"";
}

vector<MToken> MarkReplacementTokens(const set<string>& inherited_blocked, const string& macro_name, const vector<PPToken>& tokens)
{
	vector<MToken> out;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		MToken mt;
		mt.tok = tokens[i];
		mt.blocked = inherited_blocked;
		mt.blocked.insert(macro_name);
		out.push_back(mt);
	}
	return out;
}

vector<MToken> ApplyPastes(const vector<MToken>& expanded, const string& macro_name)
{
	vector<MToken> out;
	for (size_t i = 0; i < expanded.size(); ++i)
	{
		if (expanded[i].tok.kind == PP_OP &&
			expanded[i].tok.data == "##" &&
			expanded[i].blocked.count(macro_name) != 0)
		{
			while (!out.empty() && out.back().tok.kind == PP_WS) out.pop_back();
			if (out.empty()) continue;
			size_t rhs_index = i + 1;
			while (rhs_index < expanded.size() && expanded[rhs_index].tok.kind == PP_WS) ++rhs_index;
			string lhs_text = out.back().tok.data;
			set<string> blocked = out.back().blocked;
			out.pop_back();
			string rhs_text;
			bool consume_rhs = rhs_index < expanded.size();
			if (rhs_index < expanded.size() &&
				expanded[rhs_index].tok.kind == PP_OP &&
				expanded[rhs_index].tok.data == "##")
			{
				rhs_text = "";
				consume_rhs = false;
			}
			else
			{
				rhs_text = rhs_index < expanded.size() ? expanded[rhs_index].tok.data : "";
			}
			if (consume_rhs && rhs_index < expanded.size())
			{
				blocked.insert(expanded[rhs_index].blocked.begin(), expanded[rhs_index].blocked.end());
			}
			if (lhs_text.empty() && rhs_text.empty()) continue;
			vector<MToken> pasted = RetokenizeFragment(lhs_text + rhs_text);
			for (size_t p = 0; p < pasted.size(); ++p)
			{
				pasted[p].blocked.insert(blocked.begin(), blocked.end());
			}
			out.insert(out.end(), pasted.begin(), pasted.end());
			i = consume_rhs ? rhs_index : (rhs_index - 1);
			continue;
		}
		out.push_back(expanded[i]);
	}
	return out;
}

vector<MToken> NormalizeHashHashOperand(const vector<MToken>& tokens)
{
	vector<MToken> out;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (tokens[i].tok.kind == PP_NONWS)
		{
			vector<int> cps = DecodeUTF8(tokens[i].tok.data);
			if (cps.size() > 1)
			{
				for (size_t j = 0; j < cps.size(); ++j)
				{
					MToken piece = tokens[i];
					piece.tok.data = EncodeUTF8(cps[j]);
					out.push_back(piece);
				}
				continue;
			}
		}
		if (tokens[i].tok.kind == PP_IDENTIFIER)
		{
			vector<int> cps = DecodeUTF8(tokens[i].tok.data);
			bool split = cps.size() > 1;
			for (size_t j = 0; j < cps.size(); ++j)
			{
				if (cps[j] < 0x80)
				{
					split = false;
					break;
				}
			}
			if (split)
			{
				for (size_t j = 0; j < cps.size(); ++j)
				{
					MToken piece = tokens[i];
					piece.tok.kind = PP_NONWS;
					piece.tok.data = EncodeUTF8(cps[j]);
					out.push_back(piece);
				}
				continue;
			}
		}
		out.push_back(tokens[i]);
	}
	return out;
}

vector<MToken> GatherVarArgs(const vector<vector<MToken> >& args, size_t start)
{
	vector<MToken> out;
	for (size_t i = start; i < args.size(); ++i)
	{
		if (i > start)
		{
			out.push_back({{PP_OP, ","}, set<string>()});
		}
		out.insert(out.end(), args[i].begin(), args[i].end());
	}
	return out;
}

bool IsWhitespaceOnly(const vector<MToken>& tokens)
{
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		if (tokens[i].tok.kind != PP_WS) return false;
	}
	return true;
}

vector<MToken> Substitute(const set<string>& inherited_blocked, const string& macro_name, const MacroDef& def, const vector<vector<MToken> >& args, map<string, MacroDef>& macros)
{
	vector<MToken> expanded;
	set<string> arg_disabled;
	arg_disabled.insert(macro_name);
	vector<MToken> var_args = GatherVarArgs(args, def.params.size());
	for (size_t i = 0; i < def.replacement.size(); ++i)
	{
		const PPToken& tok = def.replacement[i];
		bool handled = false;
		if (tok.kind == PP_OP && tok.data == "#")
		{
			size_t next = i + 1;
			while (next < def.replacement.size() && def.replacement[next].kind == PP_WS) ++next;
			if (next < def.replacement.size() && def.replacement[next].kind == PP_IDENTIFIER)
			{
				for (size_t p = 0; p < def.params.size(); ++p)
				{
					if (def.params[p] == def.replacement[next].data)
					{
						expanded.push_back({{PP_STRING, StringizeTokens(ToPPTokens(args[p]))}, set<string>()});
						i = next;
						handled = true;
						break;
					}
				}
				if (!handled && def.variadic && def.replacement[next].data == "__VA_ARGS__")
				{
					expanded.push_back({{PP_STRING, StringizeTokens(ToPPTokens(var_args))}, set<string>()});
					i = next;
					handled = true;
				}
			}
		}
		if (handled) continue;
		size_t prev_non_ws = i;
		while (prev_non_ws > 0 && def.replacement[prev_non_ws - 1].kind == PP_WS) --prev_non_ws;
		size_t next_non_ws = i + 1;
		while (next_non_ws < def.replacement.size() && def.replacement[next_non_ws].kind == PP_WS) ++next_non_ws;
		bool adjacent_hashhash =
			(prev_non_ws > 0 && def.replacement[prev_non_ws - 1].kind == PP_OP && def.replacement[prev_non_ws - 1].data == "##") ||
			(next_non_ws < def.replacement.size() && def.replacement[next_non_ws].kind == PP_OP && def.replacement[next_non_ws].data == "##");
		if (tok.kind == PP_IDENTIFIER)
		{
			bool replaced = false;
			bool followed_by_lparen = next_non_ws < def.replacement.size() &&
				def.replacement[next_non_ws].kind == PP_OP &&
				def.replacement[next_non_ws].data == "(";
			for (size_t p = 0; p < def.params.size(); ++p)
			{
				if (tok.data == def.params[p])
				{
					vector<MToken> repl = adjacent_hashhash ? NormalizeHashHashOperand(args[p]) : ExpandTokens(args[p], macros, arg_disabled);
					if (followed_by_lparen)
					{
						for (size_t q = repl.size(); q > 0; --q)
						{
							if (repl[q - 1].tok.kind == PP_WS) continue;
							if (repl[q - 1].tok.kind == PP_IDENTIFIER && repl[q - 1].tok.data == macro_name)
							{
								repl[q - 1].blocked.insert(macro_name);
							}
							break;
						}
					}
					expanded.insert(expanded.end(), repl.begin(), repl.end());
					replaced = true;
					break;
				}
			}
			if (!replaced && def.variadic && tok.data == "__VA_ARGS__")
			{
				if (!var_args.empty())
				{
					vector<MToken> repl = adjacent_hashhash ? NormalizeHashHashOperand(var_args) : ExpandTokens(var_args, macros, arg_disabled);
					if (followed_by_lparen)
					{
						for (size_t q = repl.size(); q > 0; --q)
						{
							if (repl[q - 1].tok.kind == PP_WS) continue;
							if (repl[q - 1].tok.kind == PP_IDENTIFIER && repl[q - 1].tok.data == macro_name)
							{
								repl[q - 1].blocked.insert(macro_name);
							}
							break;
						}
					}
					expanded.insert(expanded.end(), repl.begin(), repl.end());
				}
				replaced = true;
			}
			if (replaced) continue;
		}
		expanded.push_back({tok, inherited_blocked});
		expanded.back().blocked.insert(macro_name);
	}

	return ApplyPastes(expanded, macro_name);
}

vector<MToken> ExpandTokens(const vector<MToken>& tokens, map<string, MacroDef>& macros, set<string> disabled, bool allow_tail_attach)
{
	vector<MToken> out;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		const MToken& tok = tokens[i];
		if (tok.tok.kind != PP_IDENTIFIER || tok.blocked.count(tok.tok.data) != 0 || macros.count(tok.tok.data) == 0)
		{
			out.push_back(tok);
			continue;
		}
		if (disabled.count(tok.tok.data) != 0)
		{
			out.push_back(tok);
			continue;
		}

		MacroDef def = macros[tok.tok.data];
		if (!def.function_like)
		{
			vector<MToken> repl = MarkReplacementTokens(tok.blocked, tok.tok.data, def.replacement);
			repl = ApplyPastes(repl, tok.tok.data);
			vector<MToken> tail = repl;
			tail.insert(tail.end(), tokens.begin() + i + 1, tokens.end());
			vector<MToken> expanded_tail = ExpandTokens(tail, macros, disabled);
			out.insert(out.end(), expanded_tail.begin(), expanded_tail.end());
			return out;
		}

		size_t call_start = i + 1;
		while (call_start < tokens.size() && tokens[call_start].tok.kind == PP_WS) ++call_start;
		if (call_start >= tokens.size() || tokens[call_start].tok.kind != PP_OP || tokens[call_start].tok.data != "(")
		{
			out.push_back(tok);
			continue;
		}

		size_t j = call_start + 1;
		int depth = 1;
		vector<vector<MToken> > args(1);
		while (j < tokens.size() && depth > 0)
		{
			if (tokens[j].tok.kind == PP_OP && tokens[j].tok.data == "(")
			{
				++depth;
				args.back().push_back(tokens[j]);
			}
			else if (tokens[j].tok.kind == PP_OP && tokens[j].tok.data == ")")
			{
				--depth;
				if (depth > 0)
				{
					args.back().push_back(tokens[j]);
				}
			}
			else if (depth == 1 && tokens[j].tok.kind == PP_OP && tokens[j].tok.data == ",")
			{
				args.push_back(vector<MToken>());
			}
			else
			{
				args.back().push_back(tokens[j]);
			}
			++j;
		}
		if (depth != 0)
		{
			throw runtime_error("unterminated macro invocation");
		}
		if (args.size() == 1 && def.params.empty() && !def.variadic && IsWhitespaceOnly(args[0]))
		{
			args.clear();
		}

		if ((!def.variadic && args.size() != def.params.size()) ||
			(def.variadic && args.size() < def.params.size()))
		{
			throw runtime_error("macro function-like invocation wrong num of params: " + tok.tok.data);
		}

		vector<MToken> repl = Substitute(tok.blocked, tok.tok.data, def, args, macros);
		repl = ExpandTokens(repl, macros, disabled);
		if (allow_tail_attach && repl.size() == 1 && j < tokens.size())
		{
			size_t tail_start = j;
			vector<MToken> tail = repl;
			tail.insert(tail.end(), tokens.begin() + tail_start, tokens.end());
			vector<MToken> expanded_tail = ExpandTokens(tail, macros, disabled, false);
			out.insert(out.end(), expanded_tail.begin(), expanded_tail.end());
			return out;
		}
		if (allow_tail_attach && j < tokens.size())
		{
			size_t last = repl.size();
			while (last > 0 && repl[last - 1].tok.kind == PP_WS) --last;
			if (last > 0 &&
				repl[last - 1].tok.kind == PP_IDENTIFIER &&
				repl[last - 1].tok.data != tok.tok.data)
			{
				vector<MToken> tail(repl.begin() + last - 1, repl.end());
				tail.insert(tail.end(), tokens.begin() + j, tokens.end());
				vector<MToken> expanded_tail = ExpandTokens(tail, macros, disabled, false);
				out.insert(out.end(), repl.begin(), repl.begin() + last - 1);
				out.insert(out.end(), expanded_tail.begin(), expanded_tail.end());
				return out;
			}
		}
		out.insert(out.end(), repl.begin(), repl.end());
		i = j - 1;
	}
	return out;
}

void EmitPostTokenSequence(const vector<PPToken>& flat, DebugPostTokenOutputStream& output)
{
	for (size_t i = 0; i < flat.size(); ++i)
	{
		const PPToken& tok = flat[i];
		if (tok.kind == PP_HEADER || tok.kind == PP_NONWS)
		{
			output.emit_invalid(tok.data);
			continue;
		}
		if (tok.kind == PP_IDENTIFIER || tok.kind == PP_OP)
		{
			if (tok.kind == PP_OP &&
				(tok.data == "#" || tok.data == "##" || tok.data == "%:" || tok.data == "%:%:"))
			{
				output.emit_invalid(tok.data);
				continue;
			}
			auto it = StringToTokenTypeMap.find(tok.data);
			if (it == StringToTokenTypeMap.end()) output.emit_identifier(tok.data);
			else output.emit_simple(tok.data, it->second);
			continue;
		}
		if (tok.kind == PP_NUMBER)
		{
			if (ParseFloatingLiteral(tok.data, output) || ParseIntegerLiteral(tok.data, output)) continue;
			output.emit_invalid(tok.data);
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
				if (!suffix.empty() && IsValidUDSuffix(suffix) && ParseCharacterLiteralCore(core, type, bytes))
					output.emit_user_defined_literal_character(tok.data, suffix, type, bytes.data(), bytes.size());
				else
					output.emit_invalid(tok.data);
			}
			else
			{
				EFundamentalType type;
				vector<unsigned char> bytes;
				if (ParseCharacterLiteralCore(tok.data, type, bytes))
					output.emit_literal(tok.data, type, bytes.data(), bytes.size());
				else
					output.emit_invalid(tok.data);
			}
			continue;
		}
		if (tok.kind == PP_STRING || tok.kind == PP_UD_STRING)
		{
			size_t j = i;
			vector<string> group;
			while (j < flat.size() && (flat[j].kind == PP_STRING || flat[j].kind == PP_UD_STRING))
			{
				group.push_back(flat[j].data);
				++j;
			}
			if (group.size() > 1)
			{
				string joined;
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
						if (!IsValidUDSuffix(suffix))
						{
							ok = false;
						}
						if (!have_ud)
						{
							have_ud = true;
							ud_suffix = suffix;
						}
						else if (ud_suffix != suffix)
						{
							ok = false;
						}
					}
					string prefix = cores[k].rfind("u8", 0) == 0 ? "u8" :
						(!cores[k].empty() && (cores[k][0] == 'u' || cores[k][0] == 'U' || cores[k][0] == 'L') ? cores[k].substr(0, 1) : "");
					if (prefix == "u8")
					{
						has_u8 = true;
					}
					else if (prefix == "u" || prefix == "U" || prefix == "L")
					{
						if (effective.empty())
						{
							effective = prefix;
						}
						else if (effective != prefix)
						{
							ok = false;
						}
					}
				}
				if (!effective.empty() && has_u8)
				{
					ok = false;
				}
				EFundamentalType type = effective == "u" ? FT_CHAR16_T : (effective == "U" ? FT_CHAR32_T : (effective == "L" ? FT_WCHAR_T : FT_CHAR));
				vector<unsigned char> bytes;
				size_t elems = 0;
				if (ok)
				{
					for (size_t k = 0; k < cores.size(); ++k)
					{
						vector<int> vals;
						if (!ExtractStringCodePoints(cores[k], vals))
						{
							ok = false;
							break;
						}
						EFundamentalType cur_type = StringLiteralTypeOf(cores[k]);
						if (type != FT_CHAR && cur_type != FT_CHAR && cur_type != type)
						{
							ok = false;
							break;
						}
						for (size_t m = 0; m < vals.size(); ++m)
						{
							if (type == FT_CHAR16_T && vals[m] > 0x10FFFF)
							{
								ok = false;
								break;
							}
							if (type == FT_CHAR16_T && vals[m] > 0xFFFF) elems += 2;
							else if (type == FT_CHAR) elems += EncodeUTF8(vals[m]).size();
							else ++elems;
							AppendCodePoint(bytes, type, vals[m]);
						}
						if (!ok) break;
					}
				}
				if (ok)
				{
					if (type == FT_CHAR) bytes.push_back(0);
					else if (type == FT_CHAR16_T) { char16_t z = 0; AppendPOD(bytes, z); }
					else { uint32_t z = 0; AppendPOD(bytes, z); }
					if (have_ud) output.emit_user_defined_literal_string_array(joined, ud_suffix, elems + 1, type, bytes.data(), bytes.size());
					else output.emit_literal_array(joined, elems + 1, type, bytes.data(), bytes.size());
				}
				else
				{
					output.emit_invalid(joined);
				}
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
				if (!suffix.empty() && IsValidUDSuffix(suffix) && ParseStringLiteralCore(core, type, bytes, elems))
					output.emit_user_defined_literal_string_array(tok.data, suffix, elems, type, bytes.data(), bytes.size());
				else
					output.emit_invalid(tok.data);
			}
			else
			{
				if (ParseStringLiteralCore(tok.data, type, bytes, elems))
					output.emit_literal_array(tok.data, elems, type, bytes.data(), bytes.size());
				else
					output.emit_invalid(tok.data);
			}
			continue;
		}
		output.emit_invalid(tok.data);
	}
}

#ifndef CPPGM_MACRO_MAIN_NAME
#define CPPGM_MACRO_MAIN_NAME main
#endif

int CPPGM_MACRO_MAIN_NAME()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		vector<int> cps = TransformSource(DecodeUTF8(oss.str()));
		if (!cps.empty() && cps.back() != '\n')
		{
			cps.push_back('\n');
		}

		PPCollector collector;
		PPTokenizer tokenizer(collector);
		vector<PPToken> tokens = tokenizer.tokenize(cps);

		map<string, MacroDef> macros;
		vector<PPToken> line;
		vector<PPToken> text_buffer;
		DebugPostTokenOutputStream output;

		auto flush_text = [&]() {
			vector<PPToken> text = CollapseWhitespace(text_buffer);
			if (text.empty()) return;
			for (size_t k = 0; k < text.size(); ++k)
			{
				if (text[k].kind == PP_IDENTIFIER && text[k].data == "__VA_ARGS__")
				{
					throw runtime_error("invalid __VA_ARGS__");
				}
			}
			vector<PPToken> expanded = ToPPTokens(ExpandTokens(ToMTokens(text), macros, set<string>()));
			vector<PPToken> no_ws;
			for (size_t k = 0; k < expanded.size(); ++k)
			{
				if (expanded[k].kind != PP_WS) no_ws.push_back(expanded[k]);
			}
			EmitPostTokenSequence(no_ws, output);
			text_buffer.clear();
		};

		for (size_t i = 0; i <= tokens.size(); ++i)
		{
			bool flush = i == tokens.size() || tokens[i].kind == PP_NL;
			if (!flush)
			{
				line.push_back(tokens[i]);
				continue;
			}

			vector<PPToken> trimmed = line;
			while (!trimmed.empty() && trimmed.front().kind == PP_WS) trimmed.erase(trimmed.begin());
			if (!trimmed.empty() && trimmed.front().kind == PP_OP && trimmed.front().data == "#")
			{
				flush_text();
				size_t p = 1;
				while (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
				if (p >= trimmed.size() || trimmed[p].kind != PP_IDENTIFIER)
				{
					throw runtime_error("invalid directive");
				}
				string directive = trimmed[p].data;
				++p;
				if (directive == "define")
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
							if (!expect_param)
							{
								throw runtime_error("invalid function-like define");
							}
							if (trimmed[p].data == "__VA_ARGS__")
							{
								throw runtime_error("invalid define");
							}
							for (size_t q = 0; q < def.params.size(); ++q)
							{
								if (def.params[q] == trimmed[p].data)
								{
									throw runtime_error("invalid function-like define");
								}
							}
							def.params.push_back(trimmed[p].data);
							expect_param = false;
							++p;
							}
							else if (trimmed[p].kind == PP_OP && trimmed[p].data == ",")
							{
								if (expect_param)
								{
									throw runtime_error("invalid function-like define");
								}
								expect_param = true;
								++p;
							}
							else if (trimmed[p].kind == PP_OP && trimmed[p].data == "...")
							{
								if (!expect_param)
								{
									throw runtime_error("invalid function-like define");
								}
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
							if (!def.function_like)
							{
								continue;
							}
							size_t next = r + 1;
							while (next < def.replacement.size() && def.replacement[next].kind == PP_WS) ++next;
							bool ok = next < def.replacement.size() &&
								def.replacement[next].kind == PP_IDENTIFIER;
							if (ok)
							{
								ok = false;
								for (size_t q = 0; q < def.params.size(); ++q)
								{
									if (def.params[q] == def.replacement[next].data)
									{
										ok = true;
										break;
									}
								}
								if (def.variadic && def.replacement[next].data == "__VA_ARGS__")
								{
									ok = true;
								}
							}
							if (!ok)
							{
								throw runtime_error("invalid define");
							}
						}
						if (def.replacement[r].kind == PP_OP && def.replacement[r].data == "##")
						{
							size_t left = r;
							while (left > 0 && def.replacement[left - 1].kind == PP_WS) --left;
							size_t right = r + 1;
							while (right < def.replacement.size() && def.replacement[right].kind == PP_WS) ++right;
							if (left == 0 || right >= def.replacement.size())
							{
								throw runtime_error("invalid define");
							}
						}
					}
					string key = MacroKey(def);
					if (macros.count(name) != 0 && MacroKey(macros[name]) != key)
					{
						throw runtime_error("invalid redefine");
					}
					macros[name] = def;
				}
				else if (directive == "undef")
				{
					while (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
					if (p >= trimmed.size() || trimmed[p].kind != PP_IDENTIFIER)
					{
						throw runtime_error("invalid undef");
					}
					string name = trimmed[p].data;
					if (name == "__VA_ARGS__")
					{
						throw runtime_error("invalid undef");
					}
					++p;
					while (p < trimmed.size() && trimmed[p].kind == PP_WS) ++p;
					if (p != trimmed.size())
					{
						throw runtime_error("extra tokens after undef");
					}
					macros.erase(name);
				}
				else
				{
					throw runtime_error("unsupported directive");
				}
			}
			else if (!trimmed.empty())
			{
				if (!text_buffer.empty()) text_buffer.push_back({PP_WS, ""});
				text_buffer.insert(text_buffer.end(), trimmed.begin(), trimmed.end());
			}
			line.clear();
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
