#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstdint>

using namespace std;

#include "IPPTokenStream.h"
#include "DebugPPTokenStream.h"

// Translation features you need to implement:
// - utf8 decoder
// - utf8 encoder
// - universal-character-name decoder
// - trigraphs
// - line splicing
// - newline at eof
// - comment striping (can be part of whitespace-sequence)

// EndOfFile: synthetic "character" to represent the end of source file
constexpr int EndOfFile = -1;

// given hex digit character c, return its value
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

// See C++ standard 2.11 Identifiers and Appendix/Annex E.1
const vector<pair<int, int>> AnnexE1_Allowed_RangesSorted =
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

// See C++ standard 2.11 Identifiers and Appendix/Annex E.2
const vector<pair<int, int>> AnnexE2_DisallowedInitially_RangesSorted =
{
	{0x300,0x36F},
	{0x1DC0,0x1DFF},
	{0x20D0,0x20FF},
	{0xFE20,0xFE2F}
};

// See C++ standard 2.13 Operators and punctuators
const unordered_set<string> Digraph_IdentifierLike_Operators =
{
	"new", "delete", "and", "and_eq", "bitand",
	"bitor", "compl", "not", "not_eq", "or",
	"or_eq", "xor", "xor_eq"
};

// See `simple-escape-sequence` grammar
const unordered_set<int> SimpleEscapeSequence_CodePoints =
{
	'\'', '"', '?', '\\', 'a', 'b', 'f', 'n', 'r', 't', 'v'
};

// Tokenizer
struct PPTokenizer
{
	IPPTokenStream& output;
	vector<unsigned char> input_bytes;
	vector<int> source;
	size_t pos;

	enum DirectiveState
	{
		DS_NONE,
		DS_AFTER_HASH,
		DS_AFTER_INCLUDE
	};

	bool at_line_start;
	DirectiveState directive_state;

	static bool IsAsciiDigit(int c)
	{
		return c >= '0' && c <= '9';
	}

	static bool IsAsciiNondigit(int c)
	{
		return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
	}

	static bool IsHorizontalWhitespace(int c)
	{
		return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r';
	}

	static bool IsOctalDigit(int c)
	{
		return c >= '0' && c <= '7';
	}

	static bool IsHexDigit(int c)
	{
		return
			(c >= '0' && c <= '9') ||
			(c >= 'a' && c <= 'f') ||
			(c >= 'A' && c <= 'F');
	}

	static bool InRanges(const vector<pair<int, int>>& ranges, int cp)
	{
		int lo = 0;
		int hi = static_cast<int>(ranges.size()) - 1;
		while (lo <= hi)
		{
			int mid = lo + (hi - lo) / 2;
			if (cp < ranges[mid].first)
			{
				hi = mid - 1;
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

	static bool IsAnnexE1(int cp)
	{
		return InRanges(AnnexE1_Allowed_RangesSorted, cp);
	}

	static bool IsAnnexE2(int cp)
	{
		return InRanges(AnnexE2_DisallowedInitially_RangesSorted, cp);
	}

	static bool IsIdentifierStart(int cp)
	{
		return IsAsciiNondigit(cp) || (IsAnnexE1(cp) && !IsAnnexE2(cp));
	}

	static bool IsIdentifierNondigit(int cp)
	{
		return IsAsciiNondigit(cp) || IsAnnexE1(cp);
	}

	static bool IsIdentifierContinue(int cp)
	{
		return IsAsciiDigit(cp) || IsIdentifierNondigit(cp);
	}

	static string EncodeUTF8CodePoint(int cp)
	{
		if (cp < 0 || cp > 0x10FFFF)
		{
			throw runtime_error("invalid code point");
		}
		if (cp >= 0xD800 && cp <= 0xDFFF)
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
			out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 0) & 0x3F)));
		}
		else if (cp <= 0xFFFF)
		{
			out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 0) & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 0) & 0x3F)));
		}
		return out;
	}

	static string EncodeUTF8(const vector<int>& cps)
	{
		string out;
		for (int cp : cps)
		{
			out += EncodeUTF8CodePoint(cp);
		}
		return out;
	}

	static vector<int> DecodeUTF8(const vector<unsigned char>& bytes)
	{
		vector<int> out;
		size_t i = 0;
		while (i < bytes.size())
		{
			unsigned char b0 = bytes[i];
			int cp = 0;
			size_t need = 0;
			int min_cp = 0;

			if ((b0 & 0x80) == 0x00)
			{
				cp = b0;
				need = 0;
				min_cp = 0;
			}
			else if ((b0 & 0xE0) == 0xC0)
			{
				cp = b0 & 0x1F;
				need = 1;
				min_cp = 0x80;
			}
			else if ((b0 & 0xF0) == 0xE0)
			{
				cp = b0 & 0x0F;
				need = 2;
				min_cp = 0x800;
			}
			else if ((b0 & 0xF8) == 0xF0)
			{
				cp = b0 & 0x07;
				need = 3;
				min_cp = 0x10000;
			}
			else if ((b0 & 0xC0) == 0x80)
			{
				throw runtime_error("utf8 invalid unit (10xxxxxx)");
			}
			else if ((b0 & 0xFE) == 0xFE)
			{
				throw runtime_error("utf8 invalid unit (111111xx)");
			}
			else
			{
				throw runtime_error("utf8 invalid unit (11111xxx)");
			}

			if (i + need >= bytes.size())
			{
				throw runtime_error("utf8 partial code point");
			}

			for (size_t j = 0; j < need; j++)
			{
				unsigned char bx = bytes[i + 1 + j];
				if ((bx & 0xC0) != 0x80)
				{
					throw runtime_error("utf8 invalid continuation byte");
				}
				cp = (cp << 6) | (bx & 0x3F);
			}

			if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
			{
				throw runtime_error("utf8 invalid code point");
			}

			out.push_back(cp);
			i += 1 + need;
		}
		return out;
	}

	struct ReadResult
	{
		int cp;
		size_t next;
		bool eof;
	};

	ReadResult ReadPhase1(size_t p) const
	{
		if (p >= source.size())
		{
			return {EndOfFile, p, true};
		}

		int c = source[p];
		size_t next = p + 1;

		if (p + 2 < source.size() && source[p] == '?' && source[p + 1] == '?')
		{
			switch (source[p + 2])
			{
			case '=': c = '#'; next = p + 3; break;
			case '/': c = '\\'; next = p + 3; break;
			case '\'': c = '^'; next = p + 3; break;
			case '(': c = '['; next = p + 3; break;
			case ')': c = ']'; next = p + 3; break;
			case '!': c = '|'; next = p + 3; break;
			case '<': c = '{'; next = p + 3; break;
			case '>': c = '}'; next = p + 3; break;
			case '-': c = '~'; next = p + 3; break;
			default: break;
			}
		}

		if (c == '\\')
		{
			if (next < source.size() && source[next] == 'u')
			{
				if (next + 4 < source.size())
				{
					bool all_hex = true;
					int value = 0;
					for (size_t i = next + 1; i <= next + 4; i++)
					{
						if (!IsHexDigit(source[i]))
						{
							all_hex = false;
							break;
						}
						value = value * 16 + HexCharToValue(source[i]);
					}
					if (all_hex)
					{
						return {value, next + 5, false};
					}
				}
			}
			else if (next < source.size() && source[next] == 'U')
			{
				if (next + 8 < source.size())
				{
					bool all_hex = true;
					uint32_t value = 0;
					for (size_t i = next + 1; i <= next + 8; i++)
					{
						if (!IsHexDigit(source[i]))
						{
							all_hex = false;
							break;
						}
						value = value * 16 + static_cast<uint32_t>(HexCharToValue(source[i]));
					}
					if (all_hex)
					{
						if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
						{
							throw runtime_error("invalid universal-character-name");
						}
						return {static_cast<int>(value), next + 9, false};
					}
				}
			}
		}

		return {c, next, false};
	}

	ReadResult ReadNormal(size_t p) const
	{
		size_t cur = p;
		while (true)
		{
			ReadResult a = ReadPhase1(cur);
			if (a.eof)
			{
				return a;
			}

			if (a.cp == '\\')
			{
				ReadResult b = ReadPhase1(a.next);
				if (!b.eof && b.cp == '\n')
				{
					cur = b.next;
					continue;
				}
			}

			return a;
		}
	}

	int PeekNormal(size_t p, size_t lookahead = 0) const
	{
		size_t cur = p;
		for (size_t i = 0; ; i++)
		{
			ReadResult r = ReadNormal(cur);
			if (r.eof)
			{
				return EndOfFile;
			}
			if (i == lookahead)
			{
				return r.cp;
			}
			cur = r.next;
		}
	}

	int ConsumeNormal(size_t& p, vector<int>* out = nullptr) const
	{
		ReadResult r = ReadNormal(p);
		if (r.eof)
		{
			return EndOfFile;
		}
		p = r.next;
		if (out)
		{
			out->push_back(r.cp);
		}
		return r.cp;
	}

	bool ParseIdentifierAt(size_t p, size_t& out_next, vector<int>& out_cps) const
	{
		out_cps.clear();

		int c0 = PeekNormal(p);
		if (!IsIdentifierStart(c0))
		{
			return false;
		}

		size_t cur = p;
		while (true)
		{
			int c = PeekNormal(cur);
			if (!IsIdentifierContinue(c))
			{
				break;
			}
			ConsumeNormal(cur, &out_cps);
		}

		out_next = cur;
		return true;
	}

	void NoteWhitespace()
	{
	}

	void NoteNewLine()
	{
		at_line_start = true;
		directive_state = DS_NONE;
	}

	void NoteNonWhitespace(const string& token_type, const string& data)
	{
		if (directive_state == DS_AFTER_HASH)
		{
			if (token_type == "identifier" && data == "include")
			{
				directive_state = DS_AFTER_INCLUDE;
			}
			else
			{
				directive_state = DS_NONE;
			}
		}
		else if (directive_state == DS_AFTER_INCLUDE)
		{
			directive_state = DS_NONE;
		}
		else
		{
			if (at_line_start && token_type == "preprocessing-op-or-punc" &&
				(data == "#" || data == "%:"))
			{
				directive_state = DS_AFTER_HASH;
			}
			else
			{
				directive_state = DS_NONE;
			}
		}

		at_line_start = false;
	}

	bool TryWhitespaceSequence()
	{
		size_t cur = pos;
		bool had_any = false;

		while (true)
		{
			int c = PeekNormal(cur);
			if (c == EndOfFile || c == '\n')
			{
				break;
			}

			if (IsHorizontalWhitespace(c))
			{
				had_any = true;
				ConsumeNormal(cur);
				continue;
			}

			if (c == '/')
			{
				int c1 = PeekNormal(cur, 1);
				if (c1 == '/')
				{
					had_any = true;
					ConsumeNormal(cur);
					ConsumeNormal(cur);
					while (true)
					{
						int d = PeekNormal(cur);
						if (d == EndOfFile || d == '\n')
						{
							break;
						}
						ConsumeNormal(cur);
					}
					continue;
				}
				if (c1 == '*')
				{
					had_any = true;
					ConsumeNormal(cur);
					ConsumeNormal(cur);
					while (true)
					{
						int d = PeekNormal(cur);
						if (d == EndOfFile)
						{
							throw runtime_error("partial comment");
						}
						if (d == '*' && PeekNormal(cur, 1) == '/')
						{
							ConsumeNormal(cur);
							ConsumeNormal(cur);
							break;
						}
						ConsumeNormal(cur);
					}
					continue;
				}
			}

			break;
		}

		if (!had_any)
		{
			return false;
		}

		pos = cur;
		output.emit_whitespace_sequence();
		NoteWhitespace();
		return true;
	}

	bool TryHeaderName()
	{
		if (directive_state != DS_AFTER_INCLUDE)
		{
			return false;
		}

		int open = PeekNormal(pos);
		if (open != '<' && open != '"')
		{
			return false;
		}

		size_t cur = pos;
		vector<int> cps;
		ConsumeNormal(cur, &cps);

		if (open == '<')
		{
			while (true)
			{
				int c = PeekNormal(cur);
				if (c == EndOfFile || c == '\n')
				{
					return false;
				}
				ConsumeNormal(cur, &cps);
				if (c == '>')
				{
					break;
				}
			}
		}
		else
		{
			while (true)
			{
				int c = PeekNormal(cur);
				if (c == EndOfFile || c == '\n')
				{
					return false;
				}
				ConsumeNormal(cur, &cps);
				if (c == '"')
				{
					break;
				}
			}
		}

		pos = cur;
		string data = EncodeUTF8(cps);
		output.emit_header_name(data);
		NoteNonWhitespace("header-name", data);
		return true;
	}

	void ParseEscapeInLiteral(size_t& cur, vector<int>& cps, bool string_literal) const
	{
		int c = PeekNormal(cur);
		if (c == EndOfFile || c == '\n')
		{
			if (string_literal)
			{
				throw runtime_error("unterminated string literal");
			}
			throw runtime_error("unterminated character literal");
		}

		if (SimpleEscapeSequence_CodePoints.count(c))
		{
			ConsumeNormal(cur, &cps);
			return;
		}

		if (IsOctalDigit(c))
		{
			for (int i = 0; i < 3; i++)
			{
				int d = PeekNormal(cur);
				if (!IsOctalDigit(d))
				{
					break;
				}
				ConsumeNormal(cur, &cps);
			}
			return;
		}

		if (c == 'x')
		{
			ConsumeNormal(cur, &cps);
			if (!IsHexDigit(PeekNormal(cur)))
			{
				throw runtime_error("invalid hex escape sequence");
			}
			while (IsHexDigit(PeekNormal(cur)))
			{
				ConsumeNormal(cur, &cps);
			}
			return;
		}

		throw runtime_error("invalid escape sequence");
	}

	bool TryCharacterLiteral()
	{
		size_t cur = pos;
		vector<int> cps;

		int c0 = PeekNormal(cur);
		bool has_prefix = false;
		if (c0 == 'u' || c0 == 'U' || c0 == 'L')
		{
			if (PeekNormal(cur, 1) == '\'')
			{
				has_prefix = true;
				ConsumeNormal(cur, &cps);
			}
		}

		if (!has_prefix && c0 != '\'')
		{
			return false;
		}

		if (PeekNormal(cur) != '\'')
		{
			return false;
		}
		ConsumeNormal(cur, &cps);

		while (true)
		{
			int c = PeekNormal(cur);
			if (c == EndOfFile || c == '\n')
			{
				throw runtime_error("unterminated character literal");
			}

			ConsumeNormal(cur, &cps);
			if (c == '\\')
			{
				ParseEscapeInLiteral(cur, cps, false);
				continue;
			}
			if (c == '\'')
			{
				break;
			}
		}

		size_t suffix_next = cur;
		vector<int> suffix;
		if (ParseIdentifierAt(cur, suffix_next, suffix))
		{
			cps.insert(cps.end(), suffix.begin(), suffix.end());
			cur = suffix_next;
			pos = cur;
			string data = EncodeUTF8(cps);
			output.emit_user_defined_character_literal(data);
			NoteNonWhitespace("user-defined-character-literal", data);
			return true;
		}

		pos = cur;
		string data = EncodeUTF8(cps);
		output.emit_character_literal(data);
		NoteNonWhitespace("character-literal", data);
		return true;
	}

	bool TryRawStringLiteral()
	{
		size_t cur = pos;
		bool matched = false;

		int c0 = PeekNormal(cur);
		if (c0 == 'R' && PeekNormal(cur, 1) == '"')
		{
			matched = true;
			ConsumeNormal(cur);
			ConsumeNormal(cur);
		}
		else if (c0 == 'u')
		{
			if (PeekNormal(cur, 1) == '8' && PeekNormal(cur, 2) == 'R' && PeekNormal(cur, 3) == '"')
			{
				matched = true;
				ConsumeNormal(cur);
				ConsumeNormal(cur);
				ConsumeNormal(cur);
				ConsumeNormal(cur);
			}
			else if (PeekNormal(cur, 1) == 'R' && PeekNormal(cur, 2) == '"')
			{
				matched = true;
				ConsumeNormal(cur);
				ConsumeNormal(cur);
				ConsumeNormal(cur);
			}
		}
		else if ((c0 == 'U' || c0 == 'L') && PeekNormal(cur, 1) == 'R' && PeekNormal(cur, 2) == '"')
		{
			matched = true;
			ConsumeNormal(cur);
			ConsumeNormal(cur);
			ConsumeNormal(cur);
		}

		if (!matched)
		{
			return false;
		}

		size_t raw_cur = cur;
		vector<int> delimiter;
		while (true)
		{
			if (raw_cur >= source.size())
			{
				throw runtime_error("unterminated raw string literal");
			}
			int c = source[raw_cur];
			if (c == '(')
			{
				raw_cur++;
				break;
			}
			if (c == '\n')
			{
				throw runtime_error("unterminated raw string literal");
			}
			if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\\' || c == ')' || c == '(')
			{
				throw runtime_error("unterminated raw string literal");
			}
			delimiter.push_back(c);
			raw_cur++;
			if (delimiter.size() > 16)
			{
				throw runtime_error("raw string delimiter too long");
			}
		}

		size_t close = raw_cur;
		bool closed = false;
		while (close < source.size())
		{
			if (source[close] != ')')
			{
				close++;
				continue;
			}

			bool ok = true;
			for (size_t i = 0; i < delimiter.size(); i++)
			{
				size_t idx = close + 1 + i;
				if (idx >= source.size() || source[idx] != delimiter[i])
				{
					ok = false;
					break;
				}
			}
			size_t qidx = close + 1 + delimiter.size();
			if (!ok || qidx >= source.size() || source[qidx] != '"')
			{
				close++;
				continue;
			}

			close = qidx + 1;
			closed = true;
			break;
		}

		if (!closed)
		{
			throw runtime_error("unterminated raw string literal");
		}

		vector<int> raw_literal;
		for (size_t i = pos; i < close; i++)
		{
			raw_literal.push_back(source[i]);
		}

		size_t suffix_next = close;
		vector<int> suffix;
		bool has_suffix = ParseIdentifierAt(close, suffix_next, suffix);
		if (has_suffix)
		{
			raw_literal.insert(raw_literal.end(), suffix.begin(), suffix.end());
			pos = suffix_next;
		}
		else
		{
			pos = close;
		}

		string data = EncodeUTF8(raw_literal);
		if (has_suffix)
		{
			output.emit_user_defined_string_literal(data);
			NoteNonWhitespace("user-defined-string-literal", data);
		}
		else
		{
			output.emit_string_literal(data);
			NoteNonWhitespace("string-literal", data);
		}

		return true;
	}

	bool TryOrdinaryStringLiteral()
	{
		size_t cur = pos;
		vector<int> cps;

		bool matched = false;
		int c0 = PeekNormal(cur);
		if (c0 == '"')
		{
			matched = true;
		}
		else if (c0 == 'u')
		{
			if (PeekNormal(cur, 1) == '8' && PeekNormal(cur, 2) == '"')
			{
				matched = true;
				ConsumeNormal(cur, &cps);
				ConsumeNormal(cur, &cps);
			}
			else if (PeekNormal(cur, 1) == '"')
			{
				matched = true;
				ConsumeNormal(cur, &cps);
			}
		}
		else if ((c0 == 'U' || c0 == 'L') && PeekNormal(cur, 1) == '"')
		{
			matched = true;
			ConsumeNormal(cur, &cps);
		}

		if (!matched)
		{
			return false;
		}

		if (PeekNormal(cur) != '"')
		{
			return false;
		}
		ConsumeNormal(cur, &cps);

		while (true)
		{
			int c = PeekNormal(cur);
			if (c == EndOfFile || c == '\n')
			{
				throw runtime_error("unterminated string literal");
			}

			ConsumeNormal(cur, &cps);
			if (c == '\\')
			{
				ParseEscapeInLiteral(cur, cps, true);
				continue;
			}
			if (c == '"')
			{
				break;
			}
		}

		size_t suffix_next = cur;
		vector<int> suffix;
		bool has_suffix = ParseIdentifierAt(cur, suffix_next, suffix);
		if (has_suffix)
		{
			cps.insert(cps.end(), suffix.begin(), suffix.end());
			cur = suffix_next;
		}

		pos = cur;
		string data = EncodeUTF8(cps);
		if (has_suffix)
		{
			output.emit_user_defined_string_literal(data);
			NoteNonWhitespace("user-defined-string-literal", data);
		}
		else
		{
			output.emit_string_literal(data);
			NoteNonWhitespace("string-literal", data);
		}
		return true;
	}

	bool TryPPNumber()
	{
		size_t cur = pos;
		vector<int> cps;

		int c0 = PeekNormal(cur);
		if (IsAsciiDigit(c0))
		{
			ConsumeNormal(cur, &cps);
		}
		else if (c0 == '.' && IsAsciiDigit(PeekNormal(cur, 1)))
		{
			ConsumeNormal(cur, &cps);
			ConsumeNormal(cur, &cps);
		}
		else
		{
			return false;
		}

		while (true)
		{
			int c = PeekNormal(cur);
			if (IsAsciiDigit(c) || IsIdentifierNondigit(c) || c == '.')
			{
				ConsumeNormal(cur, &cps);
				continue;
			}
			if ((c == '+' || c == '-') && !cps.empty() &&
				(cps.back() == 'e' || cps.back() == 'E'))
			{
				ConsumeNormal(cur, &cps);
				continue;
			}
			break;
		}

		pos = cur;
		string data = EncodeUTF8(cps);
		output.emit_pp_number(data);
		NoteNonWhitespace("pp-number", data);
		return true;
	}

	bool MatchOperatorString(size_t p, const string& op) const
	{
		for (size_t i = 0; i < op.size(); i++)
		{
			if (PeekNormal(p, i) != static_cast<unsigned char>(op[i]))
			{
				return false;
			}
		}
		return true;
	}

	bool TryOperatorOrPunc()
	{
		static const vector<string> ops =
		{
			"%:%:", ">>=", "<<=", "->*", "...", "##",
			"<:", ":>", "<%", "%>", "%:", "::", ".*",
			"+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=",
			"<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
			"++", "--", "->",
			"{", "}", "[", "]", "#", "(", ")", ";", ":", "?",
			".", "+", "-", "*", "/", "%", "^", "&", "|", "~",
			"!", "=", "<", ">", ","
		};

		if (PeekNormal(pos) == '<' && PeekNormal(pos, 1) == ':' && PeekNormal(pos, 2) == ':')
		{
			int c4 = PeekNormal(pos, 3);
			if (c4 != ':' && c4 != '>')
			{
				size_t cur = pos;
				vector<int> cps;
				ConsumeNormal(cur, &cps);
				pos = cur;
				string data = EncodeUTF8(cps);
				output.emit_preprocessing_op_or_punc(data);
				NoteNonWhitespace("preprocessing-op-or-punc", data);
				return true;
			}
		}

		for (const string& op : ops)
		{
			if (!MatchOperatorString(pos, op))
			{
				continue;
			}

			size_t cur = pos;
			vector<int> cps;
			for (size_t i = 0; i < op.size(); i++)
			{
				ConsumeNormal(cur, &cps);
			}

			pos = cur;
			string data = EncodeUTF8(cps);
			output.emit_preprocessing_op_or_punc(data);
			NoteNonWhitespace("preprocessing-op-or-punc", data);
			return true;
		}

		return false;
	}

	bool TryIdentifier()
	{
		size_t next = pos;
		vector<int> cps;
		if (!ParseIdentifierAt(pos, next, cps))
		{
			return false;
		}

		pos = next;
		string data = EncodeUTF8(cps);
		if (Digraph_IdentifierLike_Operators.count(data))
		{
			output.emit_preprocessing_op_or_punc(data);
			NoteNonWhitespace("preprocessing-op-or-punc", data);
		}
		else
		{
			output.emit_identifier(data);
			NoteNonWhitespace("identifier", data);
		}
		return true;
	}

	void Tokenize()
	{
		at_line_start = true;
		directive_state = DS_NONE;
		pos = 0;

		while (true)
		{
			int c = PeekNormal(pos);
			if (c == EndOfFile)
			{
				break;
			}

			if (c == '\n')
			{
				ConsumeNormal(pos);
				output.emit_new_line();
				NoteNewLine();
				continue;
			}

			if (TryWhitespaceSequence())
			{
				continue;
			}

			if (TryHeaderName())
			{
				continue;
			}

			if (TryRawStringLiteral())
			{
				continue;
			}

			if (TryOrdinaryStringLiteral())
			{
				continue;
			}

			if (TryCharacterLiteral())
			{
				continue;
			}

			if (TryPPNumber())
			{
				continue;
			}

			if (TryIdentifier())
			{
				continue;
			}

			if (TryOperatorOrPunc())
			{
				continue;
			}

			if (c == '"')
			{
				throw runtime_error("unterminated string literal");
			}
			if (c == '\'')
			{
				throw runtime_error("unterminated character literal");
			}

			vector<int> bad;
			ConsumeNormal(pos, &bad);
			string data = EncodeUTF8(bad);
			output.emit_non_whitespace_char(data);
			NoteNonWhitespace("non-whitespace-character", data);
		}

		output.emit_eof();
	}

	PPTokenizer(IPPTokenStream& output)
		: output(output), pos(0), at_line_start(true), directive_state(DS_NONE)
	{}

	void process(int c)
	{
		if (c != EndOfFile)
		{
			input_bytes.push_back(static_cast<unsigned char>(c));
			return;
		}

		source = DecodeUTF8(input_bytes);
		if (!source.empty() && source.back() != '\n')
		{
			source.push_back('\n');
		}
		Tokenize();
	}
};

int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();

		string input = oss.str();

		DebugPPTokenStream output;

		PPTokenizer tokenizer(output);

		for (char c : input)
		{
			unsigned char code_unit = c;
			tokenizer.process(code_unit);
		}

		tokenizer.process(EndOfFile);
		return EXIT_SUCCESS;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
