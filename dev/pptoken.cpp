#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

#include "IPPTokenStream.h"
#include "DebugPPTokenStream.h"

constexpr int EndOfFile = -1;

int HexCharToValue(int c)
{
	switch (c)
	{
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'A': return 10;
	case 'a': return 10;
	case 'B': return 11;
	case 'b': return 11;
	case 'C': return 12;
	case 'c': return 12;
	case 'D': return 13;
	case 'd': return 13;
	case 'E': return 14;
	case 'e': return 14;
	case 'F': return 15;
	case 'f': return 15;
	default: throw logic_error("HexCharToValue of nonhex char");
	}
}

const vector<pair<int, int> > AnnexE1_Allowed_RangesSorted =
{
	{0xA8,0xA8},
	{0xAA,0xAA},
	{0xAD,0xAD},
	{0xAF,0xAF},
	{0xB2,0xB5},
	{0xB7,0xBA},
	{0xBC,0xBE},
	{0xC0,0xD6},
	{0xD8,0xF6},
	{0xF8,0xFF},
	{0x100,0x167F},
	{0x1681,0x180D},
	{0x180F,0x1FFF},
	{0x200B,0x200D},
	{0x202A,0x202E},
	{0x203F,0x2040},
	{0x2054,0x2054},
	{0x2060,0x206F},
	{0x2070,0x218F},
	{0x2460,0x24FF},
	{0x2776,0x2793},
	{0x2C00,0x2DFF},
	{0x2E80,0x2FFF},
	{0x3004,0x3007},
	{0x3021,0x302F},
	{0x3031,0x303F},
	{0x3040,0xD7FF},
	{0xF900,0xFD3D},
	{0xFD40,0xFDCF},
	{0xFDF0,0xFE44},
	{0xFE47,0xFFFD},
	{0x10000,0x1FFFD},
	{0x20000,0x2FFFD},
	{0x30000,0x3FFFD},
	{0x40000,0x4FFFD},
	{0x50000,0x5FFFD},
	{0x60000,0x6FFFD},
	{0x70000,0x7FFFD},
	{0x80000,0x8FFFD},
	{0x90000,0x9FFFD},
	{0xA0000,0xAFFFD},
	{0xB0000,0xBFFFD},
	{0xC0000,0xCFFFD},
	{0xD0000,0xDFFFD},
	{0xE0000,0xEFFFD}
};

const vector<pair<int, int> > AnnexE2_DisallowedInitially_RangesSorted =
{
	{0x300,0x36F},
	{0x1DC0,0x1DFF},
	{0x20D0,0x20FF},
	{0xFE20,0xFE2F}
};

const unordered_set<string> Digraph_IdentifierLike_Operators =
{
	"new", "delete", "and", "and_eq", "bitand",
	"bitor", "compl", "not", "not_eq", "or",
	"or_eq", "xor", "xor_eq"
};

const unordered_set<int> SimpleEscapeSequence_CodePoints =
{
	'\'', '"', '?', '\\', 'a', 'b', 'f', 'n', 'r', 't', 'v'
};

bool InRangeList(int cp, const vector<pair<int, int> >& ranges)
{
	size_t lo = 0;
	size_t hi = ranges.size();
	while (lo < hi)
	{
		size_t mid = lo + (hi - lo) / 2;
		if (cp < ranges[mid].first)
		{
			hi = mid;
		}
		else if (cp > ranges[mid].second)
		{
			lo = mid + 1;
		}
		else
		{
			return true;
		}
	}
	return false;
}

bool IsIdentifierInitial(int cp)
{
	if (cp == '_' || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
	{
		return true;
	}
	if (cp < 0x80)
	{
		return false;
	}
	return InRangeList(cp, AnnexE1_Allowed_RangesSorted) &&
		!InRangeList(cp, AnnexE2_DisallowedInitially_RangesSorted);
}

bool IsIdentifierContinue(int cp)
{
	return IsIdentifierInitial(cp) || (cp >= '0' && cp <= '9') ||
		InRangeList(cp, AnnexE2_DisallowedInitially_RangesSorted);
}

bool IsWhitespaceNoNewline(int cp)
{
	return cp == ' ' || cp == '\t' || cp == '\v' || cp == '\f';
}

bool IsHexDigit(int cp)
{
	return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f') ||
		(cp >= 'A' && cp <= 'F');
}

bool IsOctDigit(int cp)
{
	return cp >= '0' && cp <= '7';
}

string EncodeUTF8(int cp)
{
	if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
	{
		throw runtime_error("invalid code point");
	}

	string out;
	if (cp <= 0x7F)
	{
		out.push_back(static_cast<char>(cp));
	}
	else if (cp <= 0x7FF)
	{
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else if (cp <= 0xFFFF)
	{
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	return out;
}

vector<int> DecodeUTF8(const string& input)
{
	vector<int> out;
	for (size_t i = 0; i < input.size(); )
	{
		unsigned char b0 = static_cast<unsigned char>(input[i]);
		if ((b0 & 0x80) == 0)
		{
			out.push_back(b0);
			++i;
			continue;
		}
		if ((b0 & 0xC0) == 0x80)
		{
			throw runtime_error("utf8 invalid head byte");
		}
		if ((b0 & 0xFE) == 0xFE)
		{
			throw runtime_error("utf8 invalid unit (111111xx)");
		}

		int need = 0;
		int cp = 0;
		int min_cp = 0;
		if ((b0 & 0xE0) == 0xC0)
		{
			need = 1;
			cp = b0 & 0x1F;
			min_cp = 0x80;
		}
		else if ((b0 & 0xF0) == 0xE0)
		{
			need = 2;
			cp = b0 & 0x0F;
			min_cp = 0x800;
		}
		else if ((b0 & 0xF8) == 0xF0)
		{
			need = 3;
			cp = b0 & 0x07;
			min_cp = 0x10000;
		}
		else
		{
			throw runtime_error("utf8 invalid head byte");
		}

		if (i + need >= input.size())
		{
			throw runtime_error("utf8 truncated sequence");
		}
		for (int j = 1; j <= need; ++j)
		{
			unsigned char bx = static_cast<unsigned char>(input[i + j]);
			if ((bx & 0xC0) != 0x80)
			{
				throw runtime_error("utf8 invalid tail byte");
			}
			cp = (cp << 6) | (bx & 0x3F);
		}
		if (cp < min_cp)
		{
			throw runtime_error("utf8 overlong sequence");
		}
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			throw runtime_error("utf8 invalid code point");
		}
		out.push_back(cp);
		i += need + 1;
	}
	return out;
}

vector<int> ReplaceTrigraphs(const vector<int>& cps)
{
	vector<int> out;
	for (size_t i = 0; i < cps.size(); ++i)
	{
		if (i + 2 < cps.size() && cps[i] == '?' && cps[i + 1] == '?')
		{
			int repl = 0;
			switch (cps[i + 2])
			{
			case '=': repl = '#'; break;
			case '/': repl = '\\'; break;
			case '\'': repl = '^'; break;
			case '(': repl = '['; break;
			case ')': repl = ']'; break;
			case '!': repl = '|'; break;
			case '<': repl = '{'; break;
			case '>': repl = '}'; break;
			case '-': repl = '~'; break;
			default: break;
			}
			if (repl != 0)
			{
				out.push_back(repl);
				i += 2;
				continue;
			}
		}
		out.push_back(cps[i]);
	}
	return out;
}

vector<int> SpliceLines(const vector<int>& cps)
{
	vector<int> out;
	for (size_t i = 0; i < cps.size(); ++i)
	{
		if (cps[i] == '\\' && i + 1 < cps.size() && cps[i + 1] == '\n')
		{
			++i;
			continue;
		}
		out.push_back(cps[i]);
	}
	return out;
}

vector<int> ReplaceUCNs(const vector<int>& cps)
{
	vector<int> out;
	for (size_t i = 0; i < cps.size(); ++i)
	{
		if (cps[i] == '\\' && i + 1 < cps.size() &&
			(cps[i + 1] == 'u' || cps[i + 1] == 'U'))
		{
			size_t digits = cps[i + 1] == 'u' ? 4u : 8u;
			if (i + 2 + digits > cps.size())
			{
				throw runtime_error("incomplete universal-character-name");
			}
			int cp = 0;
			for (size_t j = 0; j < digits; ++j)
			{
				int c = cps[i + 2 + j];
				if (!IsHexDigit(c))
				{
					throw runtime_error("invalid universal-character-name");
				}
				cp = (cp << 4) | HexCharToValue(c);
			}
			if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
			{
				throw runtime_error("invalid universal-character-name");
			}
			out.push_back(cp);
			i += digits + 1;
			continue;
		}
		out.push_back(cps[i]);
	}
	return out;
}

bool MatchRawLiteralPrefix(const vector<int>& cps, size_t pos, size_t& prefix_len)
{
	prefix_len = 0;
	if (pos + 3 < cps.size() && cps[pos] == 'u' && cps[pos + 1] == '8' &&
		cps[pos + 2] == 'R' && cps[pos + 3] == '"')
	{
		prefix_len = 3;
		return true;
	}
	if (pos + 2 < cps.size() &&
		(cps[pos] == 'u' || cps[pos] == 'U' || cps[pos] == 'L') &&
		cps[pos + 1] == 'R' && cps[pos + 2] == '"')
	{
		prefix_len = 2;
		return true;
	}
	if (pos + 1 < cps.size() && cps[pos] == 'R' && cps[pos + 1] == '"')
	{
		prefix_len = 1;
		return true;
	}
	return false;
}

size_t FindRawLiteralEnd(const vector<int>& cps, size_t pos)
{
	size_t prefix_len = 0;
	if (!MatchRawLiteralPrefix(cps, pos, prefix_len))
	{
		return pos;
	}

	size_t i = pos + prefix_len + 1;
	string delim;
	while (true)
	{
		if (i >= cps.size() || cps[i] == '\n')
		{
			throw runtime_error("unterminated raw string literal");
		}
		if (cps[i] == '(')
		{
			++i;
			break;
		}
		if (cps[i] == ' ' || cps[i] == '\\' || cps[i] == ')' ||
			cps[i] == '\t' || cps[i] == '\v' || cps[i] == '\f')
		{
			throw runtime_error("invalid raw string delimiter");
		}
		delim.push_back(static_cast<char>(cps[i]));
		++i;
		if (delim.size() > 16)
		{
			throw runtime_error("raw string delimiter too long");
		}
	}

	while (i < cps.size())
	{
		if (cps[i] == ')')
		{
			bool match = true;
			for (size_t j = 0; j < delim.size(); ++j)
			{
				if (i + 1 + j >= cps.size() ||
					cps[i + 1 + j] != static_cast<unsigned char>(delim[j]))
				{
					match = false;
					break;
				}
			}
			if (match && i + 1 + delim.size() < cps.size() &&
				cps[i + 1 + delim.size()] == '"')
			{
				return i + 2 + delim.size();
			}
		}
		++i;
	}

	throw runtime_error("unterminated raw string literal");
}

size_t DecodeUCNAt(const vector<int>& cps, size_t pos, int& cp)
{
	if (pos + 1 >= cps.size() || cps[pos] != '\\' ||
		(cps[pos + 1] != 'u' && cps[pos + 1] != 'U'))
	{
		return 0;
	}

	size_t digits = cps[pos + 1] == 'u' ? 4u : 8u;
	if (pos + 2 + digits > cps.size())
	{
		throw runtime_error("incomplete universal-character-name");
	}

	cp = 0;
	for (size_t j = 0; j < digits; ++j)
	{
		int c = cps[pos + 2 + j];
		if (!IsHexDigit(c))
		{
			throw runtime_error("invalid universal-character-name");
		}
		cp = (cp << 4) | HexCharToValue(c);
	}
	if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
	{
		throw runtime_error("invalid universal-character-name");
	}
	return digits + 2;
}

size_t DecodeUCNAfterSyntheticBackslash(const vector<int>& cps, size_t pos, int& cp)
{
	if (pos >= cps.size() || (cps[pos] != 'u' && cps[pos] != 'U'))
	{
		return 0;
	}

	size_t digits = cps[pos] == 'u' ? 4u : 8u;
	if (pos + 1 + digits > cps.size())
	{
		throw runtime_error("incomplete universal-character-name");
	}

	cp = 0;
	for (size_t j = 0; j < digits; ++j)
	{
		int c = cps[pos + 1 + j];
		if (!IsHexDigit(c))
		{
			throw runtime_error("invalid universal-character-name");
		}
		cp = (cp << 4) | HexCharToValue(c);
	}
	if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
	{
		throw runtime_error("invalid universal-character-name");
	}
	return digits + 1;
}

vector<int> TransformSource(const vector<int>& cps)
{
	vector<int> out;
	for (size_t i = 0; i < cps.size(); )
	{
		size_t raw_end = i;
		if (i == 0 || (!IsIdentifierContinue(cps[i - 1]) &&
			cps[i - 1] != '"' && cps[i - 1] != '\''))
		{
			raw_end = FindRawLiteralEnd(cps, i);
		}
		if (raw_end != i)
		{
			out.insert(out.end(), cps.begin() + i, cps.begin() + raw_end);
			i = raw_end;
			continue;
		}

		int c = cps[i];
		size_t consumed = 1;
		if (i + 2 < cps.size() && cps[i] == '?' && cps[i + 1] == '?')
		{
			switch (cps[i + 2])
			{
			case '=': c = '#'; consumed = 3; break;
			case '/': c = '\\'; consumed = 3; break;
			case '\'': c = '^'; consumed = 3; break;
			case '(': c = '['; consumed = 3; break;
			case ')': c = ']'; consumed = 3; break;
			case '!': c = '|'; consumed = 3; break;
			case '<': c = '{'; consumed = 3; break;
			case '>': c = '}'; consumed = 3; break;
			case '-': c = '~'; consumed = 3; break;
			default: break;
			}
		}

		if (c == '\\' && i + consumed < cps.size() && cps[i + consumed] == '\n')
		{
			i += consumed + 1;
			continue;
		}

		if (c == '\\')
		{
			int ucn_cp = 0;
			size_t ucn_len = consumed == 1 ? DecodeUCNAt(cps, i, ucn_cp) : 0;
			if (ucn_len != 0)
			{
				out.push_back(ucn_cp);
				i += ucn_len;
				continue;
			}
			if (consumed == 3)
			{
				ucn_len = DecodeUCNAfterSyntheticBackslash(cps, i + consumed, ucn_cp);
				if (ucn_len != 0)
				{
					out.push_back(ucn_cp);
					i += consumed + ucn_len;
					continue;
				}
			}
		}

		out.push_back(c);
		i += consumed;
	}
	return out;
}

struct Cursor
{
	const vector<int>& cps;
	size_t pos;

	explicit Cursor(const vector<int>& cps)
		: cps(cps), pos(0)
	{}

	int peek(size_t off = 0) const
	{
		size_t idx = pos + off;
		return idx < cps.size() ? cps[idx] : EndOfFile;
	}

	int get()
	{
		int c = peek();
		if (c != EndOfFile)
		{
			++pos;
		}
		return c;
	}

	bool starts_with(const string& s) const
	{
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (peek(i) != static_cast<unsigned char>(s[i]))
			{
				return false;
			}
		}
		return true;
	}
};

string EncodeRange(const vector<int>& cps, size_t begin, size_t end)
{
	string out;
	for (size_t i = begin; i < end; ++i)
	{
		out += EncodeUTF8(cps[i]);
	}
	return out;
}

struct PPTokenizer
{
	IPPTokenStream& output;
	bool at_line_start;
	enum DirectiveState
	{
		DIRECTIVE_NONE,
		DIRECTIVE_AFTER_HASH,
		DIRECTIVE_AFTER_INCLUDE
	} directive_state;

	explicit PPTokenizer(IPPTokenStream& output)
		: output(output),
		  at_line_start(true),
		  directive_state(DIRECTIVE_NONE)
	{}

	void process(int)
	{
	}

	void tokenize(const vector<int>& cps)
	{
		Cursor cur(cps);
		while (cur.peek() != EndOfFile)
		{
			if (cur.peek() == '\n')
			{
				cur.get();
				output.emit_new_line();
				at_line_start = true;
				directive_state = DIRECTIVE_NONE;
				continue;
			}

			if (IsWhitespaceNoNewline(cur.peek()) || StartsComment(cur))
			{
				ConsumeWhitespace(cur);
				output.emit_whitespace_sequence();
				continue;
			}

			if (directive_state == DIRECTIVE_AFTER_INCLUDE &&
				(cur.peek() == '<' || cur.peek() == '"'))
			{
				output.emit_header_name(ParseHeaderName(cur));
				at_line_start = false;
				directive_state = DIRECTIVE_NONE;
				continue;
			}

			if (TryLiteral(cur))
			{
				at_line_start = false;
				directive_state = DIRECTIVE_NONE;
				continue;
			}

			if (TryPPNumber(cur))
			{
				at_line_start = false;
				directive_state = DIRECTIVE_NONE;
				continue;
			}

			if (IsIdentifierInitial(cur.peek()))
			{
				string id = ParseIdentifier(cur);
				if (Digraph_IdentifierLike_Operators.count(id) != 0)
				{
					output.emit_preprocessing_op_or_punc(id);
				}
				else
				{
					output.emit_identifier(id);
					if (directive_state == DIRECTIVE_AFTER_HASH && id == "include")
					{
						directive_state = DIRECTIVE_AFTER_INCLUDE;
					}
					else
					{
						directive_state = DIRECTIVE_NONE;
					}
				}
				at_line_start = false;
				continue;
			}

			string op = ParseOperator(cur);
			if (!op.empty())
			{
				output.emit_preprocessing_op_or_punc(op);
				if (at_line_start && (op == "#" || op == "%:"))
				{
					directive_state = DIRECTIVE_AFTER_HASH;
				}
				else
				{
					directive_state = DIRECTIVE_NONE;
				}
				at_line_start = false;
				continue;
			}

			string ch = EncodeUTF8(cur.get());
			output.emit_non_whitespace_char(ch);
			at_line_start = false;
			directive_state = DIRECTIVE_NONE;
		}

		output.emit_eof();
	}

	bool StartsComment(const Cursor& cur) const
	{
		return (cur.peek() == '/' && cur.peek(1) == '/') ||
			(cur.peek() == '/' && cur.peek(1) == '*');
	}

	void ConsumeWhitespace(Cursor& cur)
	{
		bool consumed = false;
		while (true)
		{
			while (IsWhitespaceNoNewline(cur.peek()))
			{
				cur.get();
				consumed = true;
			}

			if (cur.peek() == '/' && cur.peek(1) == '/')
			{
				cur.get();
				cur.get();
				consumed = true;
				while (cur.peek() != EndOfFile && cur.peek() != '\n')
				{
					cur.get();
				}
				continue;
			}

			if (cur.peek() == '/' && cur.peek(1) == '*')
			{
				cur.get();
				cur.get();
				consumed = true;
				bool closed = false;
				while (cur.peek() != EndOfFile)
				{
					if (cur.peek() == '*' && cur.peek(1) == '/')
					{
						cur.get();
						cur.get();
						closed = true;
						break;
					}
					cur.get();
				}
				if (!closed)
				{
					throw runtime_error("partial comment");
				}
				continue;
			}

			break;
		}

		if (!consumed)
		{
			throw logic_error("ConsumeWhitespace with no whitespace");
		}
	}

	string ParseIdentifier(Cursor& cur)
	{
		size_t begin = cur.pos;
		cur.get();
		while (IsIdentifierContinue(cur.peek()))
		{
			cur.get();
		}
		return EncodeRange(cur.cps, begin, cur.pos);
	}

	bool TryPPNumber(Cursor& cur)
	{
		if (!(cur.peek() >= '0' && cur.peek() <= '9') &&
			!(cur.peek() == '.' && cur.peek(1) >= '0' && cur.peek(1) <= '9'))
		{
			return false;
		}

		size_t begin = cur.pos;
		cur.get();
		while (true)
		{
			int c = cur.peek();
			if ((c >= '0' && c <= '9') || c == '.')
			{
				cur.get();
				continue;
			}
			if (IsIdentifierContinue(c))
			{
				cur.get();
				continue;
			}
			int prev = cur.pos > begin ? cur.cps[cur.pos - 1] : EndOfFile;
			if ((prev == 'e' || prev == 'E' || prev == 'p' || prev == 'P') &&
				(c == '+' || c == '-'))
			{
				cur.get();
				continue;
			}
			break;
		}

		output.emit_pp_number(EncodeRange(cur.cps, begin, cur.pos));
		return true;
	}

	bool TryLiteral(Cursor& cur)
	{
		string prefix;
		bool raw = false;
		size_t prefix_len = 0;

		if (cur.starts_with("u8R\""))
		{
			prefix = "u8R";
			raw = true;
			prefix_len = 3;
		}
		else if (cur.starts_with("uR\"") || cur.starts_with("UR\"") || cur.starts_with("LR\""))
		{
			prefix.push_back(static_cast<char>(cur.peek()));
			prefix.push_back('R');
			raw = true;
			prefix_len = 2;
		}
		else if (cur.starts_with("R\""))
		{
			prefix = "R";
			raw = true;
			prefix_len = 1;
		}
		else if (cur.starts_with("u8\""))
		{
			prefix = "u8";
			prefix_len = 2;
		}
		else if ((cur.peek() == 'u' || cur.peek() == 'U' || cur.peek() == 'L') &&
			(cur.peek(1) == '"' || cur.peek(1) == '\''))
		{
			prefix.push_back(static_cast<char>(cur.peek()));
			prefix_len = 1;
		}
		else if (cur.peek() == '"' || cur.peek() == '\'')
		{
			prefix_len = 0;
		}
		else
		{
			return false;
		}

		int quote = cur.peek(prefix_len);
		if (quote != '"' && quote != '\'')
		{
			return false;
		}

		size_t begin = cur.pos;
		if (raw)
		{
			ParseRawLiteral(cur, quote, prefix_len);
		}
		else
		{
			ParseNormalLiteral(cur, quote, prefix_len);
		}

		size_t lit_end = cur.pos;
		while (IsIdentifierContinue(cur.peek()))
		{
			cur.get();
		}
		string data = EncodeRange(cur.cps, begin, cur.pos);
		bool user_defined = cur.pos != lit_end;
		bool is_string = quote == '"';
		if (is_string)
		{
			if (user_defined)
			{
				output.emit_user_defined_string_literal(data);
			}
			else
			{
				output.emit_string_literal(data);
			}
		}
		else
		{
			if (user_defined)
			{
				output.emit_user_defined_character_literal(data);
			}
			else
			{
				output.emit_character_literal(data);
			}
		}
		return true;
	}

	void ParseNormalLiteral(Cursor& cur, int quote, size_t prefix_len)
	{
		for (size_t i = 0; i < prefix_len; ++i)
		{
			cur.get();
		}
		cur.get();
		while (true)
		{
			int c = cur.peek();
			if (c == EndOfFile || c == '\n')
			{
				throw runtime_error(quote == '"' ?
					"unterminated string literal" :
					"unterminated character literal");
			}
			if (c == quote)
			{
				cur.get();
				return;
			}
			if (c == '\\')
			{
				cur.get();
				int esc = cur.peek();
				if (esc == EndOfFile || esc == '\n')
				{
					throw runtime_error(quote == '"' ?
						"unterminated string literal" :
						"unterminated character literal");
				}
				cur.get();
				if (esc == 'x')
				{
					if (!IsHexDigit(cur.peek()))
					{
						throw runtime_error("invalid hex escape sequence");
					}
					while (IsHexDigit(cur.peek()))
					{
						cur.get();
					}
				}
				else if (IsOctDigit(esc))
				{
					for (int i = 0; i < 2 && IsOctDigit(cur.peek()); ++i)
					{
						cur.get();
					}
				}
				else if (esc == '8' || esc == '9' ||
					SimpleEscapeSequence_CodePoints.count(esc) == 0)
				{
					throw runtime_error("invalid escape sequence");
				}
				continue;
			}
			cur.get();
		}
	}

	void ParseRawLiteral(Cursor& cur, int, size_t prefix_len)
	{
		for (size_t i = 0; i < prefix_len; ++i)
		{
			cur.get();
		}
		cur.get();

		string delim;
		while (true)
		{
			int c = cur.peek();
			if (c == EndOfFile || c == '\n')
			{
				throw runtime_error("unterminated raw string literal");
			}
			if (c == '(')
			{
				cur.get();
				break;
			}
			if (c == ' ' || c == '\\' || c == ')' || c == '\t' || c == '\v' || c == '\f')
			{
				throw runtime_error("invalid raw string delimiter");
			}
			delim += static_cast<char>(c);
			cur.get();
			if (delim.size() > 16)
			{
				throw runtime_error("raw string delimiter too long");
			}
		}

		while (true)
		{
			int c = cur.peek();
			if (c == EndOfFile)
			{
				throw runtime_error("unterminated raw string literal");
			}
			if (c == ')' && RawDelimiterMatches(cur, delim))
			{
				cur.get();
				for (size_t i = 0; i < delim.size(); ++i)
				{
					cur.get();
				}
				if (cur.peek() != '"')
				{
					throw runtime_error("unterminated raw string literal");
				}
				cur.get();
				return;
			}
			cur.get();
		}
	}

	bool RawDelimiterMatches(const Cursor& cur, const string& delim) const
	{
		for (size_t i = 0; i < delim.size(); ++i)
		{
			if (cur.peek(1 + i) != static_cast<unsigned char>(delim[i]))
			{
				return false;
			}
		}
		return cur.peek(1 + delim.size()) == '"';
	}

	string ParseHeaderName(Cursor& cur)
	{
		size_t begin = cur.pos;
		if (cur.peek() == '<')
		{
			cur.get();
			while (true)
			{
				int c = cur.peek();
				if (c == EndOfFile || c == '\n')
				{
					throw runtime_error("unterminated header name");
				}
				cur.get();
				if (c == '>')
				{
					break;
				}
			}
		}
		else
		{
			cur.get();
			while (true)
			{
				int c = cur.peek();
				if (c == EndOfFile || c == '\n')
				{
					throw runtime_error("unterminated header name");
				}
				cur.get();
				if (c == '"')
				{
					break;
				}
			}
		}
		return EncodeRange(cur.cps, begin, cur.pos);
	}

	string ParseOperator(Cursor& cur)
	{
		if (cur.peek() == '<' && cur.peek(1) == ':' && cur.peek(2) == ':' &&
			cur.peek(3) != ':' && cur.peek(3) != '>')
		{
			cur.get();
			return "<";
		}

		static const vector<string> ops = {
			"%:%:", ">>=", "<<=", "...", "->*", "->", ".*", "++", "--",
			"<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "+=",
			"-=", "*=", "/=", "%=", "^=", "&=", "|=", "::", "##",
			"<:", ":>", "<%", "%>", "%:", "{", "}", "[", "]", "#",
			"(", ")", ";", ":", "?", ".", "+", "-", "*", "/", "%",
			"^", "&", "|", "~", "!", "=", "<", ">", ","
		};

		for (size_t i = 0; i < ops.size(); ++i)
		{
			if (cur.starts_with(ops[i]))
			{
				for (size_t j = 0; j < ops[i].size(); ++j)
				{
					cur.get();
				}
				return ops[i];
			}
		}
		return string();
	}
};

int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		string input = oss.str();

		vector<int> cps = TransformSource(DecodeUTF8(input));
		if (!cps.empty() && cps.back() != '\n')
		{
			cps.push_back('\n');
		}

		DebugPPTokenStream output;
		PPTokenizer tokenizer(output);
		tokenizer.tokenize(cps);
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
