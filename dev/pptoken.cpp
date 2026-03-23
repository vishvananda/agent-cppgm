#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
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

const vector<pair<int, int>> AnnexE2_DisallowedInitially_RangesSorted =
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

struct CodePoint
{
	int value;
	size_t raw_start;
	size_t raw_end;
	bool synthetic;
};

bool InRangeSet(const vector<pair<int, int>>& ranges, int value)
{
	auto it = lower_bound(
		ranges.begin(),
		ranges.end(),
		make_pair(value, value),
		[](const pair<int, int>& lhs, const pair<int, int>& rhs)
		{
			return lhs.second < rhs.first;
		}
	);

	return it != ranges.end() && it->first <= value && value <= it->second;
}

bool IsIdentifierNondigit(int value)
{
	if (value == '_' ||
		('a' <= value && value <= 'z') ||
		('A' <= value && value <= 'Z'))
	{
		return true;
	}

	return InRangeSet(AnnexE1_Allowed_RangesSorted, value);
}

bool IsIdentifierStart(int value)
{
	return IsIdentifierNondigit(value) &&
		!InRangeSet(AnnexE2_DisallowedInitially_RangesSorted, value);
}

bool IsIdentifierContinue(int value)
{
	return IsIdentifierNondigit(value) || ('0' <= value && value <= '9');
}

bool IsDigit(int value)
{
	return '0' <= value && value <= '9';
}

bool IsOctalDigit(int value)
{
	return '0' <= value && value <= '7';
}

bool IsHexDigit(int value)
{
	return ('0' <= value && value <= '9') ||
		('a' <= value && value <= 'f') ||
		('A' <= value && value <= 'F');
}

bool IsHorizontalWhitespace(int value)
{
	return value == ' ' || value == '\t' || value == '\v' || value == '\f';
}

string EncodeUtf8(int value)
{
	string out;

	if (value < 0 || value > 0x10FFFF || (0xD800 <= value && value <= 0xDFFF))
	{
		throw runtime_error("invalid code point");
	}

	if (value <= 0x7F)
	{
		out.push_back(static_cast<char>(value));
	}
	else if (value <= 0x7FF)
	{
		out.push_back(static_cast<char>(0xC0 | (value >> 6)));
		out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
	}
	else if (value <= 0xFFFF)
	{
		out.push_back(static_cast<char>(0xE0 | (value >> 12)));
		out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
	}
	else
	{
		out.push_back(static_cast<char>(0xF0 | (value >> 18)));
		out.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
	}

	return out;
}

int DecodeUtf8CodePoint(const string& raw, size_t& pos)
{
	unsigned char lead = static_cast<unsigned char>(raw[pos]);

	if ((lead & 0x80) == 0)
	{
		++pos;
		return lead;
	}

	int width = 0;
	int value = 0;

	if ((lead & 0xE0) == 0xC0)
	{
		width = 2;
		value = lead & 0x1F;
	}
	else if ((lead & 0xF0) == 0xE0)
	{
		width = 3;
		value = lead & 0x0F;
	}
	else if ((lead & 0xF8) == 0xF0)
	{
		width = 4;
		value = lead & 0x07;
	}
	else
	{
		throw runtime_error("utf8 invalid unit (111111xx)");
	}

	if (pos + static_cast<size_t>(width) > raw.size())
	{
		throw runtime_error("utf8 truncated sequence");
	}

	for (int i = 1; i < width; ++i)
	{
		unsigned char unit = static_cast<unsigned char>(raw[pos + i]);
		if ((unit & 0xC0) != 0x80)
		{
			throw runtime_error("utf8 invalid continuation");
		}
		value = (value << 6) | (unit & 0x3F);
	}

	if ((width == 2 && value < 0x80) ||
		(width == 3 && value < 0x800) ||
		(width == 4 && value < 0x10000) ||
		value > 0x10FFFF ||
		(0xD800 <= value && value <= 0xDFFF))
	{
		throw runtime_error("utf8 invalid code point");
	}

	pos += width;
	return value;
}

vector<CodePoint> DecodePhase1(const string& raw)
{
	static const unordered_map<char, char> trigraphs =
	{
		{'=', '#'},
		{'/', '\\'},
		{'\'', '^'},
		{'(', '['},
		{')', ']'},
		{'!', '|'},
		{'<', '{'},
		{'>', '}'},
		{'-', '~'}
	};

	vector<CodePoint> out;
	size_t pos = 0;

	while (pos < raw.size())
	{
		if (pos + 2 < raw.size() && raw[pos] == '?' && raw[pos + 1] == '?')
		{
			auto it = trigraphs.find(raw[pos + 2]);
			if (it != trigraphs.end())
			{
				out.push_back(CodePoint{it->second, pos, pos + 3, false});
				pos += 3;
				continue;
			}
		}

		size_t start = pos;
		int value = DecodeUtf8CodePoint(raw, pos);
		out.push_back(CodePoint{value, start, pos, false});
	}

	return out;
}

vector<CodePoint> ApplyLineSplicing(const vector<CodePoint>& input)
{
	vector<CodePoint> out;

	for (size_t i = 0; i < input.size(); ++i)
	{
		if (input[i].value == '\\' &&
			i + 1 < input.size() &&
			input[i + 1].value == '\n')
		{
			++i;
			continue;
		}

		out.push_back(input[i]);
	}

	return out;
}

vector<CodePoint> ApplyUniversalCharacterNames(const vector<CodePoint>& input)
{
	vector<CodePoint> out;

	for (size_t i = 0; i < input.size(); ++i)
	{
		if (input[i].value == '\\' &&
			i + 1 < input.size() &&
			(input[i + 1].value == 'u' || input[i + 1].value == 'U'))
		{
			const size_t digits = input[i + 1].value == 'u' ? 4 : 8;
			if (i + 1 + digits >= input.size())
			{
				throw runtime_error("invalid universal-character-name");
			}

			int value = 0;
			for (size_t j = 0; j < digits; ++j)
			{
				int cp = input[i + 2 + j].value;
				if (!IsHexDigit(cp))
				{
					throw runtime_error("invalid universal-character-name");
				}
				value = value * 16 + HexCharToValue(cp);
			}

			if (value > 0x10FFFF || (0xD800 <= value && value <= 0xDFFF))
			{
				throw runtime_error("invalid universal-character-name");
			}

			out.push_back(CodePoint{
				value,
				input[i].raw_start,
				input[i + 1 + digits].raw_end,
				false
			});
			i += 1 + digits;
			continue;
		}

		out.push_back(input[i]);
	}

	return out;
}

vector<CodePoint> BuildTranslatedStream(const string& raw)
{
	vector<CodePoint> out = ApplyUniversalCharacterNames(
		ApplyLineSplicing(
			DecodePhase1(raw)
		)
	);

	if (!raw.empty() && (out.empty() || out.back().value != '\n'))
	{
		out.push_back(CodePoint{'\n', raw.size(), raw.size(), true});
	}

	return out;
}

struct PPTokenizer
{
	IPPTokenStream& output;
	string raw_input;
	vector<CodePoint> translated;
	size_t index = 0;

	enum IncludeState
	{
		IncludeNone,
		IncludeAfterHash,
		IncludeExpectHeader
	};

	IncludeState include_state = IncludeNone;
	bool line_start = true;

	PPTokenizer(IPPTokenStream& output)
		: output(output)
	{}

	void process(int c)
	{
		if (c != EndOfFile)
		{
			raw_input.push_back(static_cast<char>(c));
			return;
		}

		translated = BuildTranslatedStream(raw_input);
		index = 0;

		while (index < translated.size())
		{
			if (translated[index].value == '\n')
			{
				output.emit_new_line();
				++index;
				line_start = true;
				include_state = IncludeNone;
				continue;
			}

			if (TryEmitWhitespaceSequence())
			{
				continue;
			}

			if (TryEmitHeaderName())
			{
				continue;
			}

			if (TryEmitStringLiteral())
			{
				continue;
			}

			if (TryEmitCharacterLiteral())
			{
				continue;
			}

			if (TryEmitIdentifierOrWordOperator())
			{
				continue;
			}

			if (TryEmitPPNumber())
			{
				continue;
			}

			if (TryEmitOperator())
			{
				continue;
			}

			EmitNonWhitespaceCharacter();
		}

		output.emit_eof();
	}

	int Current(int offset = 0) const
	{
		size_t at = index + static_cast<size_t>(offset);
		if (at >= translated.size())
		{
			return EndOfFile;
		}
		return translated[at].value;
	}

	bool StartsWith(const string& text) const
	{
		for (size_t i = 0; i < text.size(); ++i)
		{
			if (Current(static_cast<int>(i)) != static_cast<unsigned char>(text[i]))
			{
				return false;
			}
		}
		return true;
	}

	string ConsumeRange(size_t begin, size_t end) const
	{
		string out;
		for (size_t i = begin; i < end; ++i)
		{
			out += EncodeUtf8(translated[i].value);
		}
		return out;
	}

	string ConsumeIdentifierFrom(size_t& pos) const
	{
		string out;

		if (pos >= translated.size() || !IsIdentifierStart(translated[pos].value))
		{
			return out;
		}

		out += EncodeUtf8(translated[pos].value);
		++pos;
		while (pos < translated.size() && IsIdentifierContinue(translated[pos].value))
		{
			out += EncodeUtf8(translated[pos].value);
			++pos;
		}

		return out;
	}

	size_t ConsumeEscapeSequence(size_t pos) const
	{
		if (pos + 1 >= translated.size() || translated[pos + 1].value == '\n')
		{
			throw runtime_error("unterminated string literal");
		}

		int next = translated[pos + 1].value;
		if (SimpleEscapeSequence_CodePoints.count(next) != 0)
		{
			return pos + 2;
		}

		if (next == 'x')
		{
			size_t at = pos + 2;
			if (at >= translated.size() || !IsHexDigit(translated[at].value))
			{
				throw runtime_error("invalid hex escape sequence");
			}
			while (at < translated.size() && IsHexDigit(translated[at].value))
			{
				++at;
			}
			return at;
		}

		if (IsOctalDigit(next))
		{
			size_t at = pos + 2;
			for (int count = 1; count < 3 && at < translated.size() && IsOctalDigit(translated[at].value); ++count)
			{
				++at;
			}
			return at;
		}

		throw runtime_error("invalid escape sequence");
	}

	void NoteEmittedToken(bool directive_starter, bool include_word)
	{
		if (directive_starter && line_start)
		{
			include_state = IncludeAfterHash;
		}
		else if (include_state == IncludeAfterHash && include_word)
		{
			include_state = IncludeExpectHeader;
		}
		else
		{
			include_state = IncludeNone;
		}

		line_start = false;
	}

	bool TryEmitWhitespaceSequence()
	{
		if (!(IsHorizontalWhitespace(Current()) ||
			(Current() == '/' && (Current(1) == '/' || Current(1) == '*'))))
		{
			return false;
		}

		while (true)
		{
			if (IsHorizontalWhitespace(Current()))
			{
				++index;
				while (IsHorizontalWhitespace(Current()))
				{
					++index;
				}
				continue;
			}

			if (Current() == '/' && Current(1) == '/')
			{
				index += 2;
				while (Current() != EndOfFile && Current() != '\n')
				{
					++index;
				}
				continue;
			}

			if (Current() == '/' && Current(1) == '*')
			{
				index += 2;
				bool closed = false;
				while (Current() != EndOfFile)
				{
					if (Current() == '*' && Current(1) == '/')
					{
						index += 2;
						closed = true;
						break;
					}
					++index;
				}
				if (!closed)
				{
					throw runtime_error("partial comment");
				}
				continue;
			}

			break;
		}

		output.emit_whitespace_sequence();
		return true;
	}

	bool TryEmitHeaderName()
	{
		if (include_state != IncludeExpectHeader || (Current() != '<' && Current() != '"'))
		{
			return false;
		}

		const int terminator = Current() == '<' ? '>' : '"';
		size_t begin = index++;
		while (Current() != EndOfFile && Current() != '\n' && Current() != terminator)
		{
			++index;
		}

		if (Current() != terminator)
		{
			index = begin;
			return false;
		}

		++index;
		output.emit_header_name(ConsumeRange(begin, index));
		NoteEmittedToken(false, false);
		return true;
	}

	bool TryEmitIdentifierOrWordOperator()
	{
		if (!IsIdentifierStart(Current()))
		{
			return false;
		}

		size_t begin = index;
		++index;
		while (IsIdentifierContinue(Current()))
		{
			++index;
		}

		string text = ConsumeRange(begin, index);

		if (Digraph_IdentifierLike_Operators.count(text) != 0)
		{
			output.emit_preprocessing_op_or_punc(text);
			NoteEmittedToken(false, false);
		}
		else
		{
			output.emit_identifier(text);
			NoteEmittedToken(false, text == "include");
		}

		return true;
	}

	bool TryEmitPPNumber()
	{
		if (!(IsDigit(Current()) || (Current() == '.' && IsDigit(Current(1)))))
		{
			return false;
		}

		size_t begin = index++;
		while (true)
		{
			if (IsDigit(Current()) || IsIdentifierNondigit(Current()) || Current() == '.')
			{
				++index;
				continue;
			}

			if ((Current() == '+' || Current() == '-') &&
				index > begin &&
				(translated[index - 1].value == 'e' || translated[index - 1].value == 'E'))
			{
				++index;
				continue;
			}

			break;
		}

		output.emit_pp_number(ConsumeRange(begin, index));
		NoteEmittedToken(false, false);
		return true;
	}

	bool TryEmitCharacterLiteral()
	{
		size_t begin = index;
		size_t pos = index;

		if (Current() == 'u' || Current() == 'U' || Current() == 'L')
		{
			if (Current(1) != '\'')
			{
				return false;
			}
			++pos;
		}
		else if (Current() != '\'')
		{
			return false;
		}

		if (translated[pos].value != '\'')
		{
			return false;
		}

		++pos;
		while (pos < translated.size())
		{
			int value = translated[pos].value;
			if (value == '\n' || value == EndOfFile)
			{
				throw runtime_error("unterminated character literal");
			}
			if (value == '\\')
			{
				size_t next = ConsumeEscapeSequence(pos);
				if (next <= pos)
				{
					throw runtime_error("unterminated character literal");
				}
				pos = next;
				continue;
			}
			if (value == '\'')
			{
				++pos;
				break;
			}
			++pos;
		}

		if (pos == translated.size())
		{
			throw runtime_error("unterminated character literal");
		}

		index = pos;
		string literal = ConsumeRange(begin, index);

		size_t suffix_pos = index;
		string suffix = ConsumeIdentifierFrom(suffix_pos);
		if (!suffix.empty())
		{
			index = suffix_pos;
			output.emit_user_defined_character_literal(literal + suffix);
		}
		else
		{
			output.emit_character_literal(literal);
		}

		NoteEmittedToken(false, false);
		return true;
	}

	bool TryEmitStringLiteral()
	{
		size_t begin = index;
		size_t pos = index;
		bool has_prefix = false;
		bool raw = false;

		if (StartsWith("u8"))
		{
			if (Current(2) == '"' || (Current(2) == 'R' && Current(3) == '"'))
			{
				pos += 2;
				has_prefix = true;
			}
		}
		else if (Current() == 'u' || Current() == 'U' || Current() == 'L')
		{
			if (Current(1) == '"' || (Current(1) == 'R' && Current(2) == '"'))
			{
				++pos;
				has_prefix = true;
			}
		}

		if ((has_prefix && translated[pos].value == 'R' && translated[pos + 1].value == '"') ||
			(!has_prefix && Current() == 'R' && Current(1) == '"'))
		{
			raw = true;
		}

		if (!raw)
		{
			size_t quote = has_prefix ? pos : begin;
			if (quote >= translated.size() || translated[quote].value != '"')
			{
				return false;
			}

			pos = quote + 1;
			while (pos < translated.size())
			{
				int value = translated[pos].value;
				if (value == '\n' || value == EndOfFile)
				{
					throw runtime_error("unterminated string literal");
				}
				if (value == '\\')
				{
					pos = ConsumeEscapeSequence(pos);
					continue;
				}
				if (value == '"')
				{
					++pos;
					break;
				}
				++pos;
			}

			if (pos == translated.size())
			{
				throw runtime_error("unterminated string literal");
			}

			index = pos;
			string literal = ConsumeRange(begin, index);
			size_t suffix_pos = index;
			string suffix = ConsumeIdentifierFrom(suffix_pos);
			if (!suffix.empty())
			{
				index = suffix_pos;
				output.emit_user_defined_string_literal(literal + suffix);
			}
			else
			{
				output.emit_string_literal(literal);
			}

			NoteEmittedToken(false, false);
			return true;
		}

		size_t raw_start = translated[begin].raw_start;
		size_t raw_pos = raw_start;
		string prefix;

		if (StartsWith("u8R\""))
		{
			prefix = "u8R\"";
		}
		else if ((Current() == 'u' || Current() == 'U' || Current() == 'L') &&
			Current(1) == 'R' && Current(2) == '"')
		{
			prefix.push_back(static_cast<char>(Current()));
			prefix += "R\"";
		}
		else if (Current() == 'R' && Current(1) == '"')
		{
			prefix = "R\"";
		}
		else
		{
			return false;
		}

		raw_pos += prefix.size();
		size_t delim_start = raw_pos;
		while (raw_pos < raw_input.size() && raw_input[raw_pos] != '(')
		{
			unsigned char ch = static_cast<unsigned char>(raw_input[raw_pos]);
			if (ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f' ||
				ch == '\n' || ch == '\\' || ch == '(' || ch == ')')
			{
				throw runtime_error("unterminated string literal");
			}
			++raw_pos;
		}

		if (raw_pos - delim_start > 16)
		{
			throw runtime_error("raw string delimiter too long");
		}

		if (raw_pos >= raw_input.size() || raw_input[raw_pos] != '(')
		{
			throw runtime_error("unterminated raw string literal");
		}

		string delimiter = raw_input.substr(delim_start, raw_pos - delim_start);
		++raw_pos;

		string terminator = ")" + delimiter + "\"";
		size_t close = raw_input.find(terminator, raw_pos);
		if (close == string::npos)
		{
			throw runtime_error("unterminated raw string literal");
		}

		size_t raw_end = close + terminator.size();
		string literal = raw_input.substr(raw_start, raw_end - raw_start);

		while (index < translated.size() && translated[index].raw_start < raw_end)
		{
			++index;
		}

		size_t suffix_pos = index;
		string suffix = ConsumeIdentifierFrom(suffix_pos);
		if (!suffix.empty())
		{
			index = suffix_pos;
			output.emit_user_defined_string_literal(literal + suffix);
		}
		else
		{
			output.emit_string_literal(literal);
		}

		NoteEmittedToken(false, false);
		return true;
	}

	bool TryEmitOperator()
	{
		if (Current() == '<' && Current(1) == ':' && Current(2) == ':')
		{
			int fourth = Current(3);
			if (fourth != ':' && fourth != '>')
			{
				output.emit_preprocessing_op_or_punc("<");
				++index;
				NoteEmittedToken(false, false);
				return true;
			}
		}

		static const vector<string> operators =
		{
			"%:%:",
			">>=",
			"<<=",
			"...",
			"->*",
			"##",
			"<:",
			":>",
			"<%",
			"%>",
			"%:",
			"::",
			".*",
			"+=",
			"-=",
			"*=",
			"/=",
			"%=",
			"^=",
			"&=",
			"|=",
			"<<",
			">>",
			"<=",
			">=",
			"&&",
			"==",
			"!=",
			"||",
			"++",
			"--",
			"->",
			"{",
			"}",
			"[",
			"]",
			"#",
			"(",
			")",
			";",
			":",
			"?",
			".",
			"+",
			"-",
			"*",
			"/",
			"%",
			"^",
			"&",
			"|",
			"~",
			"!",
			"=",
			"<",
			">",
			","
		};

		for (const string& op : operators)
		{
			if (StartsWith(op))
			{
				index += op.size();
				output.emit_preprocessing_op_or_punc(op);
				NoteEmittedToken(op == "#" || op == "%:", false);
				return true;
			}
		}

		return false;
	}

	void EmitNonWhitespaceCharacter()
	{
		int value = Current();
		if (value == '\'' || value == '"')
		{
			throw runtime_error("unterminated string literal");
		}

		output.emit_non_whitespace_char(EncodeUtf8(value));
		++index;
		NoteEmittedToken(false, false);
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
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
