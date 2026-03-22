#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

namespace
{
	bool is_ascii_alpha(uint32_t c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
	}

	bool is_ascii_digit(uint32_t c)
	{
		return c >= '0' && c <= '9';
	}

	bool is_ascii_identifier_continue(uint32_t c)
	{
		return is_ascii_alpha(c) || is_ascii_digit(c) || c == '_';
	}

	bool is_whitespace_no_newline(uint32_t c)
	{
		return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r';
	}

	bool in_ranges(uint32_t c, const vector<pair<int, int>>& ranges)
	{
		for (size_t i = 0; i < ranges.size(); ++i)
		{
			if (c >= static_cast<uint32_t>(ranges[i].first) && c <= static_cast<uint32_t>(ranges[i].second))
				return true;
		}
		return false;
	}

	bool is_identifier_start(uint32_t c)
	{
		return c == '_' || is_ascii_alpha(c) || (in_ranges(c, AnnexE1_Allowed_RangesSorted) && !in_ranges(c, AnnexE2_DisallowedInitially_RangesSorted));
	}

	bool is_identifier_continue(uint32_t c)
	{
		return is_identifier_start(c) || is_ascii_digit(c) || in_ranges(c, AnnexE2_DisallowedInitially_RangesSorted);
	}

	string utf8_encode(uint32_t c)
	{
		string out;
		if (c <= 0x7F)
		{
			out.push_back(static_cast<char>(c));
		}
		else if (c <= 0x7FF)
		{
			out.push_back(static_cast<char>(0xC0 | ((c >> 6) & 0x1F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
		else if (c <= 0xFFFF)
		{
			if (c >= 0xD800 && c <= 0xDFFF)
				throw runtime_error("utf8 invalid code point");
			out.push_back(static_cast<char>(0xE0 | ((c >> 12) & 0x0F)));
			out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
		else if (c <= 0x10FFFF)
		{
			out.push_back(static_cast<char>(0xF0 | ((c >> 18) & 0x07)));
			out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
		else
		{
			throw runtime_error("utf8 invalid code point");
		}
		return out;
	}

	string utf8_encode_slice(const vector<uint32_t>& cps, size_t begin, size_t end)
	{
		string out;
		for (size_t i = begin; i < end; ++i)
			out += utf8_encode(cps[i]);
		return out;
	}

	string utf8_invalid_unit_message(unsigned char b)
	{
		if ((b & 0x80) == 0)
			return "0xxxxxxx";
		if ((b & 0xE0) == 0xC0)
			return "110xxxxx";
		if ((b & 0xF0) == 0xE0)
			return "1110xxxx";
		if ((b & 0xF8) == 0xF0)
			return "11110xxx";
		if ((b & 0xC0) == 0x80)
			return "10xxxxxx";
		return "111111xx";
	}

	bool match_ascii_word(const vector<uint32_t>& cps, size_t pos, const string& word)
	{
		if (pos + word.size() > cps.size())
			return false;
		for (size_t i = 0; i < word.size(); ++i)
		{
			if (cps[pos + i] != static_cast<unsigned char>(word[i]))
				return false;
		}
		return pos + word.size() >= cps.size() || !is_identifier_continue(cps[pos + word.size()]);
	}

	bool match_ascii_punct(const vector<uint32_t>& cps, size_t pos, const string& punct)
	{
		if (punct == "<:" && pos + 2 < cps.size() && cps[pos + 2] == ':'
			&& (pos + 3 >= cps.size() || (cps[pos + 3] != '>' && cps[pos + 3] != ':')))
			return false;
		if (pos + punct.size() > cps.size())
			return false;
		for (size_t i = 0; i < punct.size(); ++i)
		{
			if (cps[pos + i] != static_cast<unsigned char>(punct[i]))
				return false;
		}
		return true;
	}

	vector<uint32_t> decode_utf8_bytes(const vector<unsigned char>& bytes)
	{
		vector<uint32_t> out;
		for (size_t i = 0; i < bytes.size();)
		{
			unsigned char b = bytes[i];
			if (b < 0x80)
			{
				out.push_back(b);
				++i;
				continue;
			}

			int need = -1;
			uint32_t cp = 0;
			if (b >= 0xC2 && b <= 0xDF)
			{
				need = 1;
				cp = b & 0x1F;
			}
			else if (b >= 0xE0 && b <= 0xEF)
			{
				need = 2;
				cp = b & 0x0F;
			}
			else if (b >= 0xF0 && b <= 0xF4)
			{
				need = 3;
				cp = b & 0x07;
			}
			else
			{
				throw runtime_error(string("utf8 invalid unit (") + utf8_invalid_unit_message(b) + ")");
			}

			if (i + static_cast<size_t>(need) >= bytes.size())
				throw runtime_error(string("utf8 invalid unit (") + utf8_invalid_unit_message(b) + ")");

			for (int j = 1; j <= need; ++j)
			{
				unsigned char c = bytes[i + j];
				if ((c & 0xC0) != 0x80)
					throw runtime_error(string("utf8 invalid unit (") + utf8_invalid_unit_message(c) + ")");
				cp = (cp << 6) | (c & 0x3F);
			}

			if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000))
				throw runtime_error(string("utf8 invalid unit (") + utf8_invalid_unit_message(b) + ")");
			if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
				throw runtime_error(string("utf8 invalid unit (") + utf8_invalid_unit_message(b) + ")");

			out.push_back(cp);
			i += static_cast<size_t>(need) + 1;
		}
		return out;
	}

	size_t find_raw_string_end(const vector<uint32_t>& in, size_t i)
	{
		size_t j = i;
		if (j >= in.size())
			return string::npos;

		if (in[j] == 'u' && j + 1 < in.size() && in[j + 1] == '8')
		{
			j += 2;
		}
		else if (in[j] == 'u' || in[j] == 'U' || in[j] == 'L')
		{
			++j;
		}

		if (j < in.size() && in[j] == 'R')
		{
			if (i > 0 && (in[i - 1] == '"' || is_identifier_continue(in[i - 1])))
				return string::npos;
			++j;
		}
		else if (i < in.size() && in[i] == 'R')
		{
			j = i + 1;
		}
		else
		{
			return string::npos;
		}

		if (j >= in.size() || in[j] != '"')
			return string::npos;
		++j;

		size_t delimiter_start = j;
		while (j < in.size() && in[j] != '(')
		{
			if (in[j] == '\n')
				return string::npos;
			++j;
		}
		if (j >= in.size())
			return string::npos;

		vector<uint32_t> delimiter(in.begin() + delimiter_start, in.begin() + j);
		++j;

		while (j < in.size())
		{
			if (in[j] == ')' && j + delimiter.size() + 1 < in.size())
			{
				bool match = true;
				for (size_t k = 0; k < delimiter.size(); ++k)
				{
					if (in[j + 1 + k] != delimiter[k])
					{
						match = false;
						break;
					}
				}
				if (match && in[j + 1 + delimiter.size()] == '"')
					return j + delimiter.size() + 2;
			}
			++j;
		}

		return in.size();
	}

	#ifdef CPPGM_PPTOKEN_SKIP_TRIGRAPHS_IN_STRING_LITERALS
	size_t find_literal_end_for_trigraph_skip(const vector<uint32_t>& in, size_t i);
	#endif

	vector<uint32_t> replace_trigraphs(const vector<uint32_t>& in)
{
	vector<uint32_t> out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size();)
	{
#ifdef CPPGM_PPTOKEN_SKIP_TRIGRAPHS_IN_STRING_LITERALS
		size_t literal_end = find_literal_end_for_trigraph_skip(in, i);
		if (literal_end != string::npos)
		{
			out.insert(out.end(), in.begin() + i, in.begin() + literal_end);
			i = literal_end;
			continue;
		}
#else
		size_t raw_end = find_raw_string_end(in, i);
		if (raw_end != string::npos)
		{
			out.insert(out.end(), in.begin() + i, in.begin() + raw_end);
			i = raw_end;
			continue;
		}
#endif

		if (i + 2 < in.size() && in[i] == '?' && in[i + 1] == '?')
		{
				uint32_t mapped = 0;
				switch (in[i + 2])
				{
				case '=': mapped = '#'; break;
				case '/': mapped = '\\'; break;
				case '\'': mapped = '^'; break;
				case '(': mapped = '['; break;
				case ')': mapped = ']'; break;
				case '!': mapped = '|'; break;
				case '<': mapped = '{'; break;
				case '>': mapped = '}'; break;
				case '-': mapped = '~'; break;
				default: break;
				}
				if (mapped != 0)
				{
					out.push_back(mapped);
					i += 3;
					continue;
				}
			}
			out.push_back(in[i]);
			++i;
		}
	return out;
}

#ifdef CPPGM_PPTOKEN_SKIP_TRIGRAPHS_IN_STRING_LITERALS
size_t find_literal_end_for_trigraph_skip(const vector<uint32_t>& in, size_t i)
{
	size_t j = i;
	if (j < in.size() && in[j] == 'u' && j + 1 < in.size() && in[j + 1] == '8')
		j += 2;
	else if (j < in.size() && (in[j] == 'u' || in[j] == 'U' || in[j] == 'L'))
		++j;

	if (j < in.size() && in[j] == 'R')
	{
		if (i > 0 && (in[i - 1] == '"' || is_identifier_continue(in[i - 1])))
			return string::npos;
		return find_raw_string_end(in, i);
	}

	if (j >= in.size())
		return string::npos;
	if (in[j] != '"' && in[j] != '\'')
		return string::npos;

	uint32_t quote = in[j];
	++j;
	while (j < in.size())
	{
		if (in[j] == '\\')
		{
			if (j + 1 >= in.size())
				return in.size();
			j += 2;
			continue;
		}
		if (in[j] == quote)
			return j + 1;
		++j;
	}
	return in.size();
}
#endif

	vector<uint32_t> splice_lines(const vector<uint32_t>& in)
	{
		vector<uint32_t> out;
		out.reserve(in.size());
		for (size_t i = 0; i < in.size();)
		{
			size_t raw_end = find_raw_string_end(in, i);
			if (raw_end != string::npos)
			{
				out.insert(out.end(), in.begin() + i, in.begin() + raw_end);
				i = raw_end;
				continue;
			}
			if (in[i] == '\\' && i + 1 < in.size() && in[i + 1] == '\n')
			{
				i += 2;
				continue;
			}
			out.push_back(in[i]);
			++i;
		}
		return out;
	}

	vector<uint32_t> decode_ucns(const vector<uint32_t>& in)
	{
		vector<uint32_t> out;
		out.reserve(in.size());
		for (size_t i = 0; i < in.size();)
		{
			size_t raw_end = find_raw_string_end(in, i);
			if (raw_end != string::npos)
			{
				out.insert(out.end(), in.begin() + i, in.begin() + raw_end);
				i = raw_end;
				continue;
			}
			if (in[i] == '\\' && i + 1 < in.size() && (in[i + 1] == 'u' || in[i + 1] == 'U'))
			{
				size_t digits = (in[i + 1] == 'u') ? 4 : 8;
				if (i + 2 + digits <= in.size())
				{
					bool ok = true;
					uint32_t cp = 0;
					for (size_t j = 0; j < digits; ++j)
					{
						uint32_t c = in[i + 2 + j];
						if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
						{
							ok = false;
							break;
						}
						cp = (cp << 4) | static_cast<uint32_t>(HexCharToValue(c));
					}
					if (ok)
					{
						out.push_back(cp);
						i += 2 + digits;
						continue;
					}
				}
			}
			out.push_back(in[i]);
			++i;
		}
		return out;
	}

	vector<uint32_t> ensure_final_newline(const vector<uint32_t>& in)
	{
		if (in.empty())
			return in;
		if (in.back() == '\n')
			return in;
		vector<uint32_t> out = in;
		out.push_back('\n');
		return out;
	}
}

// Tokenizer
struct PPTokenizer
{
	IPPTokenStream& output;
	vector<unsigned char> raw_bytes;
	size_t current_line = 1;

	PPTokenizer(IPPTokenStream& output)
		: output(output)
	{}

	void process(int c)
	{
		if (c == EndOfFile)
		{
			run();
			output.emit_line_number(current_line);
			output.emit_eof();
			return;
		}
		raw_bytes.push_back(static_cast<unsigned char>(c));
	}

	void run()
	{
		current_line = 1;
		vector<uint32_t> cps = decode_utf8_bytes(raw_bytes);
		cps = replace_trigraphs(cps);
		cps = splice_lines(cps);
		cps = decode_ucns(cps);
		cps = ensure_final_newline(cps);

		size_t i = 0;
		bool at_line_start = true;
		bool after_hash = false;
		bool expect_header_name = false;

		while (i < cps.size())
		{
			if (cps[i] == '\n')
			{
				output.emit_line_number(current_line);
				output.emit_new_line();
				++i;
				++current_line;
				at_line_start = true;
				after_hash = false;
				expect_header_name = false;
				continue;
			}

			if (i + 1 < cps.size() && cps[i] == '/' && cps[i + 1] == '/')
			{
				i += 2;
				while (i < cps.size() && cps[i] != '\n')
					++i;
				output.emit_line_number(current_line);
				output.emit_whitespace_sequence();
				continue;
			}

			if (i + 1 < cps.size() && cps[i] == '/' && cps[i + 1] == '*')
			{
				i += 2;
				bool closed = false;
				while (i + 1 < cps.size())
				{
					if (cps[i] == '*' && cps[i + 1] == '/')
					{
						i += 2;
						closed = true;
						break;
					}
					if (cps[i] == '\n')
						++current_line;
					++i;
				}
				if (!closed)
					throw runtime_error("partial comment");
				while (i < cps.size() && is_whitespace_no_newline(cps[i]))
					++i;
				while (i + 1 < cps.size() && cps[i] == '/' && cps[i + 1] == '*')
				{
					i += 2;
					closed = false;
					while (i + 1 < cps.size())
					{
						if (cps[i] == '*' && cps[i + 1] == '/')
						{
							i += 2;
							closed = true;
							break;
						}
						if (cps[i] == '\n')
							++current_line;
						++i;
					}
					if (!closed)
						throw runtime_error("partial comment");
					while (i < cps.size() && is_whitespace_no_newline(cps[i]))
						++i;
				}
				output.emit_line_number(current_line);
				output.emit_whitespace_sequence();
				continue;
			}

			if (is_whitespace_no_newline(cps[i]))
			{
				bool consumed = false;
				while (i < cps.size() && is_whitespace_no_newline(cps[i]))
				{
					consumed = true;
					++i;
				}
				while (i + 1 < cps.size() && cps[i] == '/' && cps[i + 1] == '/')
				{
					consumed = true;
					i += 2;
					while (i < cps.size() && cps[i] != '\n')
						++i;
					break;
				}
				while (i + 1 < cps.size() && cps[i] == '/' && cps[i + 1] == '*')
				{
					consumed = true;
					i += 2;
					bool closed = false;
					while (i + 1 < cps.size())
					{
						if (cps[i] == '*' && cps[i + 1] == '/')
						{
							i += 2;
							closed = true;
							break;
						}
						if (cps[i] == '\n')
							++current_line;
						++i;
					}
					if (!closed)
						throw runtime_error("partial comment");
					while (i < cps.size() && is_whitespace_no_newline(cps[i]))
						++i;
					if (i + 1 < cps.size() && cps[i] == '/' && cps[i + 1] == '/')
						continue;
					if (i + 1 < cps.size() && cps[i] == '/' && cps[i + 1] == '*')
						continue;
					break;
				}
				if (consumed)
				{
					output.emit_line_number(current_line);
					output.emit_whitespace_sequence();
					continue;
				}
			}

			if (expect_header_name && (cps[i] == '<' || cps[i] == '"'))
			{
				size_t start = i;
				bool ok = false;
				if (cps[i] == '<')
				{
					++i;
					while (i < cps.size() && cps[i] != '\n')
					{
						if (cps[i] == '>')
						{
							++i;
							ok = true;
							break;
						}
						++i;
					}
				}
				else
				{
					++i;
					while (i < cps.size() && cps[i] != '\n')
					{
						if (cps[i] == '\\' && i + 1 < cps.size())
						{
							i += 2;
							continue;
						}
						if (cps[i] == '"')
						{
							++i;
							ok = true;
							break;
						}
						++i;
					}
				}
				if (ok)
				{
					output.emit_line_number(current_line);
					output.emit_header_name(utf8_encode_slice(cps, start, i));
					expect_header_name = false;
					at_line_start = false;
					continue;
				}
				i = start;
			}

			if (expect_header_name && cps[i] != '<' && cps[i] != '"')
				expect_header_name = false;

			if (after_hash && is_ascii_identifier_continue(cps[i]))
			{
				size_t start = i;
				while (i < cps.size() && is_identifier_continue(cps[i]))
					++i;
				string word = utf8_encode_slice(cps, start, i);
				if (word == "include")
				{
					output.emit_line_number(current_line);
					output.emit_identifier(word);
					expect_header_name = true;
					after_hash = false;
					at_line_start = false;
					continue;
				}
				after_hash = false;
				i = start;
			}
			else if (after_hash)
			{
				after_hash = false;
			}

			if (is_ascii_digit(cps[i]) || (cps[i] == '.' && i + 1 < cps.size() && is_ascii_digit(cps[i + 1])))
			{
				size_t start = i;
				++i;
				uint32_t prev = cps[start];
				while (i < cps.size())
				{
					uint32_t c = cps[i];
					if (is_ascii_digit(c) || is_ascii_alpha(c) || c == '_' || c == '.' || is_identifier_continue(c))
					{
						prev = c;
						++i;
						continue;
					}
					if ((c == '+' || c == '-') && (prev == 'e' || prev == 'E' || prev == 'p' || prev == 'P'))
					{
						prev = c;
						++i;
						continue;
					}
					break;
				}
				output.emit_line_number(current_line);
				output.emit_pp_number(utf8_encode_slice(cps, start, i));
				at_line_start = false;
				continue;
			}

			if (starts_literal(cps, i))
			{
				parse_literal(cps, i, expect_header_name, at_line_start);
				continue;
			}

			string punct;
			if (match_punctuator(cps, i, punct))
			{
				if (punct == "#" || punct == "%:")
					after_hash = at_line_start;
				else
					after_hash = false;
				output.emit_line_number(current_line);
				output.emit_preprocessing_op_or_punc(punct);
				i += punct.size();
				at_line_start = false;
				continue;
			}

			if (cps[i] == 'o' && i + 1 < cps.size() && cps[i + 1] == 'r' && (i + 2 >= cps.size() || !is_identifier_continue(cps[i + 2])))
			{
				output.emit_line_number(current_line);
				output.emit_preprocessing_op_or_punc("or");
				i += 2;
				at_line_start = false;
				continue;
			}

			if (is_identifier_start(cps[i]))
			{
				size_t start = i;
				++i;
				while (i < cps.size() && is_identifier_continue(cps[i]))
					++i;
				output.emit_line_number(current_line);
				output.emit_identifier(utf8_encode_slice(cps, start, i));
				at_line_start = false;
				continue;
			}

			output.emit_line_number(current_line);
			output.emit_non_whitespace_char(utf8_encode_slice(cps, i, i + 1));
			++i;
			at_line_start = false;
		}
	}

	bool starts_literal(const vector<uint32_t>& cps, size_t i)
	{
		if (cps[i] == '"' || cps[i] == '\'')
			return true;
		if (cps[i] == 'R' && i + 1 < cps.size() && cps[i + 1] == '"')
			return true;
		if (cps[i] == 'u')
		{
			if (i + 1 < cps.size() && cps[i + 1] == '8')
			{
				if (i + 2 < cps.size() && (cps[i + 2] == '"' || (cps[i + 2] == 'R' && i + 3 < cps.size() && cps[i + 3] == '"')))
					return true;
			}
			else if (i + 1 < cps.size() && (cps[i + 1] == '"' || cps[i + 1] == '\'' || (cps[i + 1] == 'R' && i + 2 < cps.size() && cps[i + 2] == '"')))
			{
				return true;
			}
		}
		if (cps[i] == 'U' || cps[i] == 'L')
		{
			if (i + 1 < cps.size() && (cps[i + 1] == '"' || cps[i + 1] == '\'' || (cps[i + 1] == 'R' && i + 2 < cps.size() && cps[i + 2] == '"')))
				return true;
		}
		return false;
	}

	void parse_literal(const vector<uint32_t>& cps, size_t& i, bool& expect_header_name, bool& at_line_start)
	{
		size_t start = i;
		bool is_char = false;
		bool is_raw = false;

		if (cps[i] == 'u' && i + 1 < cps.size() && cps[i + 1] == '8')
		{
			if (i + 2 < cps.size() && cps[i + 2] == 'R' && i + 3 < cps.size() && cps[i + 3] == '"')
			{
				is_raw = true;
				i += 4;
			}
			else if (i + 2 < cps.size() && cps[i + 2] == '"')
			{
				i += 3;
			}
			else
			{
				throw runtime_error("unterminated string literal");
			}
		}
		else if (cps[i] == 'u' || cps[i] == 'U' || cps[i] == 'L')
		{
			if (i + 1 < cps.size() && cps[i + 1] == 'R' && i + 2 < cps.size() && cps[i + 2] == '"')
			{
				is_raw = true;
				i += 3;
			}
			else if (i + 1 < cps.size() && cps[i + 1] == '"' )
			{
				i += 2;
			}
			else if (i + 1 < cps.size() && cps[i + 1] == '\'')
			{
				is_char = true;
				i += 2;
			}
			else
			{
				throw runtime_error("unterminated string literal");
			}
		}
		else if (cps[i] == 'R' && i + 1 < cps.size() && cps[i + 1] == '"')
		{
			is_raw = true;
			i += 2;
		}
		else if (cps[i] == '"')
		{
			i += 1;
		}
		else if (cps[i] == '\'')
		{
			is_char = true;
			i += 1;
		}

		if (cps[start] == '\'')
			is_char = true;

		if (is_raw)
		{
			size_t delimiter_start = i;
			while (i < cps.size() && cps[i] != '(')
			{
				if (cps[i] == '\n')
					throw runtime_error("unterminated raw string literal");
				++i;
			}
			if (i >= cps.size())
				throw runtime_error("unterminated raw string literal");
			if (i - delimiter_start > 16)
				throw runtime_error("raw string delimiter too long");
			vector<uint32_t> delimiter(cps.begin() + delimiter_start, cps.begin() + i);
			++i;
			bool found = false;
			while (i < cps.size())
			{
				if (cps[i] == ')' && i + delimiter.size() + 1 < cps.size())
				{
					bool match = true;
					for (size_t j = 0; j < delimiter.size(); ++j)
					{
						if (cps[i + 1 + j] != delimiter[j])
						{
							match = false;
							break;
						}
					}
					if (match && cps[i + 1 + delimiter.size()] == '"')
					{
						i += 2 + delimiter.size();
						found = true;
						break;
					}
				}
				if (cps[i] == '\n')
					++current_line;
				++i;
			}
			if (!found)
				throw runtime_error("unterminated raw string literal");
		}
		else
		{
			auto consume_escape = [&](size_t& j)
			{
				if (j + 1 >= cps.size())
					throw runtime_error("unterminated string literal");

				uint32_t next = cps[j + 1];
				if (SimpleEscapeSequence_CodePoints.count(static_cast<int>(next)))
				{
					j += 2;
					return;
				}
				if (next == 'x')
				{
					size_t k = j + 2;
					bool any = false;
					while (k < cps.size())
					{
						uint32_t c = cps[k];
						if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
							break;
						any = true;
						++k;
					}
					if (!any)
						throw runtime_error("invalid hex escape sequence");
					j = k;
					return;
				}
				if (next == 'u' || next == 'U')
				{
					size_t digits = (next == 'u') ? 4 : 8;
					if (j + 2 + digits > cps.size())
						throw runtime_error("invalid universal character name");
					uint32_t cp = 0;
					for (size_t k = 0; k < digits; ++k)
					{
						uint32_t c = cps[j + 2 + k];
						if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
							throw runtime_error("invalid universal character name");
						cp = (cp << 4) | static_cast<uint32_t>(HexCharToValue(c));
					}
					j += 2 + digits;
					return;
				}
				if (next >= '0' && next <= '7')
				{
					size_t k = j + 1;
					int count = 0;
					while (k < cps.size() && cps[k] >= '0' && cps[k] <= '7' && count < 3)
					{
						++k;
						++count;
					}
					j = k;
					return;
				}
				if (next == '8' || next == '9')
					throw runtime_error("invalid escape sequence");
				throw runtime_error("invalid escape sequence");
			};

			bool closed = false;
			while (i < cps.size())
			{
				if (cps[i] == '\n')
				{
					++current_line;
					throw runtime_error("unterminated string literal");
				}
				if (cps[i] == '\\' && i + 1 < cps.size())
				{
					consume_escape(i);
					continue;
				}
				if ((is_char && cps[i] == '\'') || (!is_char && cps[i] == '"'))
				{
					++i;
					closed = true;
					break;
				}
				++i;
			}
			if (!closed)
				throw runtime_error("unterminated string literal");
		}

		size_t suffix_start = i;
		if (i < cps.size() && is_identifier_start(cps[i]))
		{
			++i;
			while (i < cps.size() && is_identifier_continue(cps[i]))
				++i;
		}

		string data = utf8_encode_slice(cps, start, i);
		if (suffix_start != i)
		{
			output.emit_line_number(current_line);
			if (is_char)
				output.emit_user_defined_character_literal(data);
			else
				output.emit_user_defined_string_literal(data);
		}
		else
		{
			output.emit_line_number(current_line);
			if (is_char)
				output.emit_character_literal(data);
			else
				output.emit_string_literal(data);
		}

		expect_header_name = false;
		at_line_start = false;
	}

	bool match_punctuator(const vector<uint32_t>& cps, size_t i, string& punct)
	{
		static const char* const ordered[] =
		{
			"%:%:",
			">>=", "<<=", "->*", "and_eq", "bitand", "bitor", "compl", "not_eq", "or_eq", "xor_eq",
			"delete",
			"and", "new", "not", "xor",
			"::", "->", "...", "##", "++", "--", "&&", "||", "<<", ">>", "<=", ">=", "==", "!=", "+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=", ".*",
			"<:", ":>", "<%", "%>", "%:",
			",", ";", ":", "?", ".", "+", "-", "*", "/", "%", "^", "&", "|", "~", "!", "=", "<", ">", "(", ")", "{", "}", "[", "]", "#"
		};

		for (size_t idx = 0; idx < sizeof(ordered) / sizeof(ordered[0]); ++idx)
		{
			string candidate = ordered[idx];
			if (isalpha(candidate[0]))
			{
				if (match_ascii_word(cps, i, candidate))
				{
					punct = candidate;
					return true;
				}
				continue;
			}
			if (match_ascii_punct(cps, i, candidate))
			{
				punct = candidate;
				return true;
			}
		}
		return false;
	}
};

#ifndef CPPGM_PPTOKEN_LIBRARY
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
#endif
