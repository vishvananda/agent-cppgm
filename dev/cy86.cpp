// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <ctime>

using namespace std;

#define CPPGM_PREPROC_LIBRARY
#include "preproc.cpp"

struct ElfHeader
{
	unsigned char ident[16] =
	{
		0x7f, 'E', 'L', 'F',
		2,
		1,
		1,
		0,
		0,
		0, 0, 0, 0, 0, 0, 0
	};

	short int type = 2;
	short int machine = 0x3E;
	int version = 1;
	long int entry = 0;
	long int phoff = 64;
	long int shoff = 0;
	int processor_flags = 0;
	short int ehsize = 64;
	short int phentsize = 56;
	short int phnum = 1;
	short int shentsize = 0;
	short int shnum = 0;
	short int shstrndx = 0;
};

struct ProgramSegmentHeader
{
	int type = 1;
	static constexpr int executable = 1 << 0;
	static constexpr int writable = 1 << 1;
	static constexpr int readable = 1 << 2;
	int flags = executable | writable | readable;
	long int offset = 0;
	long int vaddr = 0x400000;
	long int paddr = 0;
	long int filesz = 0;
	long int memsz = 0;
	long int align = 0;
};

namespace pa9cy86
{
	static const uint64_t kBaseAddr = 0x400000ULL;
	static const size_t kElfHeaderSize = 64 + 56;

	struct Token
	{
		enum Kind
		{
			Identifier,
			Literal,
			Simple,
			Eof
		};

		Kind kind = Eof;
		string text;
		vector<unsigned char> bytes;
	};

	struct OperandExpr
	{
		bool grouped = false;
		char delim = 0;
		vector<Token> toks;
		bool negated = false;
	};

	struct Statement
	{
		string label;
		bool has_label = false;
		bool is_literal = false;
		Token literal;
		bool literal_negated = false;
		string opcode;
		vector<OperandExpr> operands;
		size_t file_index = 0;
	};

	struct RegSpec
	{
		int code;
		int width;
		RegSpec(int c = -1, int w = 0) : code(c), width(w) {}
	};

	struct MemRef
	{
		bool absolute;
		uint64_t abs_addr;
		RegSpec base;
		int64_t disp;
		MemRef(bool abs = false, uint64_t addr = 0, RegSpec b = RegSpec(), int64_t d = 0)
			: absolute(abs), abs_addr(addr), base(b), disp(d) {}
	};

	struct ValueRef
	{
		enum Kind
		{
			Register,
			Immediate,
			Memory
		};

		Kind kind = Immediate;
		RegSpec reg;
		uint64_t imm = 0;
		MemRef mem;
	};

	struct Fixup
	{
		size_t offset;
		string label;
		enum Kind { Rel32 } kind;
		Fixup(size_t o = 0, string l = string(), Kind k = Rel32)
			: offset(o), label(l), kind(k) {}
	};

	struct Emitter
	{
		vector<unsigned char> bytes;
		vector<Fixup> fixups;

		size_t pos() const { return bytes.size(); }
		void emit8(unsigned char v) { bytes.push_back(v); }
		void emit16(uint16_t v) { emit8(v & 0xff); emit8((v >> 8) & 0xff); }
		void emit32(uint32_t v) { for (int i = 0; i < 4; ++i) emit8((v >> (8 * i)) & 0xff); }
		void emit64(uint64_t v) { for (int i = 0; i < 8; ++i) emit8((v >> (8 * i)) & 0xff); }
		void emit_bytes(const vector<unsigned char>& v) { bytes.insert(bytes.end(), v.begin(), v.end()); }
	};

	static bool starts_with(const string& s, const string& p)
	{
		return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
	}

	static bool ends_with(const string& s, const string& p)
	{
		return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
	}

	static size_t align_up(size_t n, size_t a)
	{
		if (a == 0)
			return n;
		size_t r = n % a;
		return r ? n + (a - r) : n;
	}

	static RegSpec make_reg(int code, int width)
	{
		return RegSpec(code, width);
	}

	static int hex_value(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
		if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
		throw runtime_error("invalid hex digit");
	}

	static vector<unsigned char> parse_hex_bytes(const string& hex)
	{
		if (hex.size() % 2 != 0)
			throw runtime_error("bad hex byte string");
		vector<unsigned char> out;
		for (size_t i = 0; i < hex.size(); i += 2)
			out.push_back(static_cast<unsigned char>((hex_value(hex[i]) << 4) | hex_value(hex[i + 1])));
		return out;
	}

	static vector<unsigned char> encode_uint(uint64_t v, size_t size)
	{
		vector<unsigned char> out(size, 0);
		for (size_t i = 0; i < size && i < 8; ++i)
			out[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff);
		return out;
	}

	static uint64_t bytes_to_u64(const vector<unsigned char>& b)
	{
		uint64_t v = 0;
		for (size_t i = 0; i < b.size() && i < 8; ++i)
			v |= static_cast<uint64_t>(b[i]) << (8 * i);
		return v;
	}

	static uint64_t sign_extend_from_bytes(const vector<unsigned char>& b)
	{
		if (b.empty())
			return 0;
		uint64_t v = bytes_to_u64(b);
		if (b.size() < 8 && (b.back() & 0x80))
		{
			for (size_t i = b.size(); i < 8; ++i)
				v |= 0xffULL << (8 * i);
		}
		return v;
	}

	static uint64_t eval_literal_as_int(const Token& lit, bool negated)
	{
		uint64_t v = bytes_to_u64(lit.bytes);
		if (!negated)
			return v;
		unsigned __int128 n = v;
		n = 0 - n;
		return static_cast<uint64_t>(n);
	}

	static bool is_register_name(const string& s)
	{
		static const unordered_map<string, RegSpec> regs =
		{
			{"al", make_reg(0, 8)}, {"cl", make_reg(1, 8)}, {"dl", make_reg(2, 8)}, {"bl", make_reg(3, 8)},
			{"ah", make_reg(4, 8)}, {"ch", make_reg(5, 8)}, {"dh", make_reg(6, 8)}, {"bh", make_reg(7, 8)},
			{"spl", make_reg(4, 8)}, {"bpl", make_reg(5, 8)}, {"sil", make_reg(6, 8)}, {"dil", make_reg(7, 8)},
			{"rax", make_reg(0, 64)}, {"rcx", make_reg(1, 64)}, {"rdx", make_reg(2, 64)}, {"rbx", make_reg(3, 64)},
			{"rsp", make_reg(4, 64)}, {"rbp", make_reg(5, 64)}, {"rsi", make_reg(6, 64)}, {"rdi", make_reg(7, 64)},
			{"r8", make_reg(8, 64)}, {"r9", make_reg(9, 64)}, {"r10", make_reg(10, 64)}, {"r11", make_reg(11, 64)},
			{"r12", make_reg(12, 64)}, {"r13", make_reg(13, 64)}, {"r14", make_reg(14, 64)}, {"r15", make_reg(15, 64)},
			{"sp", make_reg(4, 64)},
			{"bp", make_reg(5, 64)},
			{"x8", make_reg(12, 8)}, {"x16", make_reg(12, 16)}, {"x32", make_reg(12, 32)}, {"x64", make_reg(12, 64)},
			{"y8", make_reg(13, 8)}, {"y16", make_reg(13, 16)}, {"y32", make_reg(13, 32)}, {"y64", make_reg(13, 64)},
			{"z8", make_reg(14, 8)}, {"z16", make_reg(14, 16)}, {"z32", make_reg(14, 32)}, {"z64", make_reg(14, 64)},
			{"t8", make_reg(15, 8)}, {"t16", make_reg(15, 16)}, {"t32", make_reg(15, 32)}, {"t64", make_reg(15, 64)}
		};
		return regs.find(s) != regs.end();
	}

	static RegSpec reg_spec(const string& s)
	{
		static const unordered_map<string, RegSpec> regs =
		{
			{"al", make_reg(0, 8)}, {"cl", make_reg(1, 8)}, {"dl", make_reg(2, 8)}, {"bl", make_reg(3, 8)},
			{"ah", make_reg(4, 8)}, {"ch", make_reg(5, 8)}, {"dh", make_reg(6, 8)}, {"bh", make_reg(7, 8)},
			{"spl", make_reg(4, 8)}, {"bpl", make_reg(5, 8)}, {"sil", make_reg(6, 8)}, {"dil", make_reg(7, 8)},
			{"rax", make_reg(0, 64)}, {"rcx", make_reg(1, 64)}, {"rdx", make_reg(2, 64)}, {"rbx", make_reg(3, 64)},
			{"rsp", make_reg(4, 64)}, {"rbp", make_reg(5, 64)}, {"rsi", make_reg(6, 64)}, {"rdi", make_reg(7, 64)},
			{"r8", make_reg(8, 64)}, {"r9", make_reg(9, 64)}, {"r10", make_reg(10, 64)}, {"r11", make_reg(11, 64)},
			{"r12", make_reg(12, 64)}, {"r13", make_reg(13, 64)}, {"r14", make_reg(14, 64)}, {"r15", make_reg(15, 64)},
			{"sp", make_reg(4, 64)},
			{"bp", make_reg(5, 64)},
			{"x8", make_reg(12, 8)}, {"x16", make_reg(12, 16)}, {"x32", make_reg(12, 32)}, {"x64", make_reg(12, 64)},
			{"y8", make_reg(13, 8)}, {"y16", make_reg(13, 16)}, {"y32", make_reg(13, 32)}, {"y64", make_reg(13, 64)},
			{"z8", make_reg(14, 8)}, {"z16", make_reg(14, 16)}, {"z32", make_reg(14, 32)}, {"z64", make_reg(14, 64)},
			{"t8", make_reg(15, 8)}, {"t16", make_reg(15, 16)}, {"t32", make_reg(15, 32)}, {"t64", make_reg(15, 64)}
		};
		auto it = regs.find(s);
		if (it == regs.end())
			throw runtime_error("unknown register: " + s);
		return it->second;
	}

	static RegSpec reg_for_width(const string& s, int width)
	{
		RegSpec r = reg_spec(s);
		r.width = width;
		return r;
	}

	static string reg_name(const RegSpec& r, int width)
	{
		static const char* low8[16] = {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"};
		static const char* low16[16] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"};
		static const char* low32[16] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
		static const char* low64[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
		if (r.code < 0 || r.code > 15)
			throw runtime_error("bad register");
		if (width == 8) return low8[r.code];
		if (width == 16) return low16[r.code];
		if (width == 32) return low32[r.code];
		if (width == 64) return low64[r.code];
		throw runtime_error("bad register width");
	}

	static bool is_cy_reg(const string& s)
	{
		return is_register_name(s);
	}

	static bool is_simple(const Token& t, const string& s)
	{
		return t.kind == Token::Simple && t.text == s;
	}

	static bool is_identifier(const Token& t)
	{
		return t.kind == Token::Identifier;
	}

	static bool is_literal(const Token& t)
	{
		return t.kind == Token::Literal;
	}

	static vector<Token> tokenize_posttoken_output(const string& text)
	{
		vector<Token> toks;
		istringstream in(text);
		string line;
		while (getline(in, line))
		{
			if (line.empty())
				continue;
			if (line == "eof")
			{
				Token t;
				t.kind = Token::Eof;
				toks.push_back(t);
				continue;
			}
			if (starts_with(line, "identifier "))
			{
				Token t;
				t.kind = Token::Identifier;
				t.text = line.substr(11);
				toks.push_back(t);
				continue;
			}
			if (starts_with(line, "simple "))
			{
				size_t p = line.find(' ', 7);
				Token t;
				t.kind = Token::Simple;
				t.text = line.substr(7, p == string::npos ? string::npos : p - 7);
				toks.push_back(t);
				continue;
			}
			if (starts_with(line, "literal "))
			{
				size_t p = line.rfind(' ');
				if (p == string::npos || p + 1 >= line.size())
					throw runtime_error("bad literal token line");
				Token t;
				t.kind = Token::Literal;
				t.bytes = parse_hex_bytes(line.substr(p + 1));
				toks.push_back(t);
				continue;
			}
		}
		return toks;
	}

	static string preprocess_file(const string& srcfile)
	{
		time_t now = time(0);
		tm* tmv = localtime(&now);
		char date_buf[32];
		char time_buf[32];
		strftime(date_buf, sizeof(date_buf), "%b %e %Y", tmv);
		strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tmv);

		pa5preproc::State st;
		st.build_date = date_buf;
		st.build_time = time_buf;
		const char* author = getenv("CPPGM_AUTHOR");
		if (author != 0 && *author != '\0')
			st.author = author;
		else
			st.author = "Jesse Andrews";
		pa5preproc::install_builtins(st, srcfile, 1);

		string expanded = pa5preproc::process_file(st, srcfile, srcfile);
		return pa5preproc::run_posttoken(expanded);
	}

	static vector<Token> load_tokens(const vector<string>& srcfiles)
	{
		vector<Token> toks;
		for (size_t i = 0; i < srcfiles.size(); ++i)
		{
			string token_text = preprocess_file(srcfiles[i]);
			vector<Token> one = tokenize_posttoken_output(token_text);
			toks.insert(toks.end(), one.begin(), one.end());
		}
		return toks;
	}

	static OperandExpr parse_operand_expr(const vector<Token>& toks, size_t& i)
	{
		if (i >= toks.size())
			throw runtime_error("unexpected end of operands");
		OperandExpr out;
		if (is_simple(toks[i], "(") || is_simple(toks[i], "["))
		{
			out.grouped = true;
			out.delim = toks[i].text[0];
			char close = (out.delim == '(') ? ')' : ']';
			++i;
			while (i < toks.size() && !(toks[i].kind == Token::Simple && toks[i].text == string(1, close)))
			{
				out.toks.push_back(toks[i]);
				++i;
			}
			if (i >= toks.size())
				throw runtime_error("unterminated grouped operand");
			++i;
			return out;
		}
		if (is_simple(toks[i], "-") && i + 1 < toks.size() && is_literal(toks[i + 1]))
		{
			out.negated = true;
			out.toks.push_back(toks[i + 1]);
			i += 2;
			return out;
		}
		out.toks.push_back(toks[i]);
		++i;
		return out;
	}

	static vector<Statement> parse_program(const vector<Token>& toks)
	{
		vector<Statement> out;
		size_t i = 0;
		while (i < toks.size())
		{
			if (toks[i].kind == Token::Eof)
				break;
			Statement st;
			if (is_identifier(toks[i]) && i + 1 < toks.size() && is_simple(toks[i + 1], ":"))
			{
				st.label = toks[i].text;
				st.has_label = true;
				i += 2;
			}
			if (i >= toks.size() || toks[i].kind == Token::Eof)
				break;
			if (is_literal(toks[i]) || (is_simple(toks[i], "-") && i + 1 < toks.size() && is_literal(toks[i + 1])))
			{
				st.is_literal = true;
				st.literal = toks[i];
				st.literal_negated = false;
				if (is_simple(toks[i], "-"))
				{
					st.literal = toks[i + 1];
					st.literal_negated = true;
					i += 2;
				}
				else
				{
					++i;
				}
				if (i >= toks.size() || !is_simple(toks[i], ";"))
					throw runtime_error("expected semicolon after literal statement");
				++i;
				out.push_back(st);
				continue;
			}
			if (!is_identifier(toks[i]))
				throw runtime_error("expected opcode");
			st.opcode = toks[i].text;
			++i;
			while (i < toks.size() && !is_simple(toks[i], ";") && toks[i].kind != Token::Eof)
				st.operands.push_back(parse_operand_expr(toks, i));
			if (i >= toks.size() || !is_simple(toks[i], ";"))
				throw runtime_error("expected semicolon after statement");
			++i;
			out.push_back(st);
		}
		return out;
	}

	static uint64_t resolve_atom_as_value(const Token& t, const unordered_map<string, uint64_t>& labels)
	{
		if (t.kind == Token::Literal)
			return bytes_to_u64(t.bytes);
		if (t.kind == Token::Identifier)
		{
			auto it = labels.find(t.text);
			if (it != labels.end())
				return it->second;
			return 0;
		}
		throw runtime_error("expected atom");
	}

	static uint64_t eval_operand_as_int(const OperandExpr& op, const unordered_map<string, uint64_t>& labels)
	{
		if (op.toks.empty())
			throw runtime_error("empty operand");
		if (op.grouped)
		{
			if (op.toks.size() == 1)
				return resolve_atom_as_value(op.toks[0], labels);
			if (op.toks.size() == 2 && is_simple(op.toks[0], "-") && is_literal(op.toks[1]))
			{
				unsigned __int128 v = bytes_to_u64(op.toks[1].bytes);
				v = 0 - v;
				return static_cast<uint64_t>(v);
			}
			if (op.toks.size() == 3 && is_identifier(op.toks[0]) && (is_simple(op.toks[1], "+") || is_simple(op.toks[1], "-")) && is_literal(op.toks[2]))
			{
				uint64_t base = resolve_atom_as_value(op.toks[0], labels);
				uint64_t delta = bytes_to_u64(op.toks[2].bytes);
				if (is_simple(op.toks[1], "-"))
					return base - delta;
				return base + delta;
			}
		}
		if (op.toks.size() == 1)
			return resolve_atom_as_value(op.toks[0], labels);
		if (op.negated && op.toks.size() == 1)
		{
			unsigned __int128 v = bytes_to_u64(op.toks[0].bytes);
			v = 0 - v;
			return static_cast<uint64_t>(v);
		}
		if (op.toks.size() == 2 && is_simple(op.toks[0], "-") && is_literal(op.toks[1]))
		{
			unsigned __int128 v = bytes_to_u64(op.toks[1].bytes);
			v = 0 - v;
			return static_cast<uint64_t>(v);
		}
		throw runtime_error("unsupported immediate expression");
	}

	static MemRef eval_memory_operand(const OperandExpr& op, const unordered_map<string, uint64_t>& labels)
	{
		if (!op.grouped)
			throw runtime_error("expected memory operand");
		MemRef m;
		if (op.toks.size() == 1)
		{
			const Token& t = op.toks[0];
			if (t.kind == Token::Identifier && is_register_name(t.text))
			{
				m.base = reg_spec(t.text);
				m.disp = 0;
				return m;
			}
			m.absolute = true;
			m.abs_addr = resolve_atom_as_value(t, labels);
			return m;
		}
		if (op.toks.size() == 3 && is_identifier(op.toks[0]) && (is_simple(op.toks[1], "+") || is_simple(op.toks[1], "-")) && is_literal(op.toks[2]))
		{
			const Token& t = op.toks[0];
			int64_t delta = static_cast<int64_t>(bytes_to_u64(op.toks[2].bytes));
			if (is_simple(op.toks[1], "-"))
				delta = -delta;
			if (t.kind == Token::Identifier && is_register_name(t.text))
			{
				m.base = reg_spec(t.text);
				m.disp = delta;
				return m;
			}
			m.absolute = true;
			m.abs_addr = resolve_atom_as_value(t, labels) + delta;
			return m;
		}
		throw runtime_error("unsupported memory expression");
	}

	static RegSpec resolve_reg_operand(const OperandExpr& op)
	{
		if (op.toks.size() != 1 || op.grouped)
			throw runtime_error("expected register");
		if (op.toks[0].kind != Token::Identifier || !is_register_name(op.toks[0].text))
			throw runtime_error("expected register");
		return reg_spec(op.toks[0].text);
	}

	static bool is_op(const string& s, const string& p)
	{
		return starts_with(s, p);
	}

	static int width_from_opcode(const string& opcode, const string& prefix)
	{
		if (!starts_with(opcode, prefix))
			throw runtime_error("bad opcode");
		string tail = opcode.substr(prefix.size());
		if (tail == "8") return 8;
		if (tail == "16") return 16;
		if (tail == "32") return 32;
		if (tail == "64") return 64;
		if (tail == "80") return 80;
		throw runtime_error("bad opcode width");
	}

	static int width_before_suffix(const string& opcode, const string& suffix)
	{
		size_t p = opcode.find(suffix);
		if (p == string::npos || p < 2)
			throw runtime_error("bad opcode width");
		return stoi(opcode.substr(1, p - 1));
	}

	static int width_after_prefix(const string& opcode, const string& prefix)
	{
		if (!starts_with(opcode, prefix))
			throw runtime_error("bad opcode width");
		string tail = opcode.substr(prefix.size());
		if (tail.empty())
			throw runtime_error("bad opcode width");
		return stoi(tail);
	}

	static bool is_signed_int_opcode(const string& s)
	{
		return starts_with(s, "i") || starts_with(s, "s");
	}

	static bool is_unsigned_int_opcode(const string& s)
	{
		return starts_with(s, "u");
	}

	static bool is_float_opcode(const string& s)
	{
		return s.size() >= 2 && s[0] == 'f';
	}

	static int reg_width_of(const RegSpec& r, int width)
	{
		RegSpec x = r;
		x.width = width;
		return x.width;
	}

	static void emit_rex(Emitter& e, bool w, int reg, int index, int rm)
	{
		unsigned char rex = 0x40;
		if (w) rex |= 0x08;
		if (reg >= 8) rex |= 0x04;
		if (index >= 8) rex |= 0x02;
		if (rm >= 8) rex |= 0x01;
		if (rex != 0x40)
			e.emit8(rex);
	}

	static void emit_modrm(Emitter& e, int mod, int reg, int rm)
	{
		e.emit8(static_cast<unsigned char>((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
	}

	static void emit_sib(Emitter& e, int scale, int index, int base)
	{
		e.emit8(static_cast<unsigned char>((scale << 6) | ((index & 7) << 3) | (base & 7)));
	}

	static void emit_disp32(Emitter& e, int32_t disp)
	{
		e.emit32(static_cast<uint32_t>(disp));
	}

	static void emit_abs_addr_load(Emitter& e, uint64_t addr, int scratch = 11)
	{
		emit_rex(e, true, 0, 0, scratch);
		e.emit8(static_cast<unsigned char>(0xB8 + (scratch & 7)));
		e.emit64(addr);
	}

	static MemRef materialize_absolute_mem(Emitter& e, const MemRef& m)
	{
		if (m.absolute)
		{
			emit_abs_addr_load(e, m.abs_addr, 11);
		}
		MemRef out = m;
		if (m.absolute)
		{
			out.absolute = false;
			out.base = reg_spec("r11");
			out.disp = 0;
		}
		return out;
	}

	static MemRef materialize_absolute_mem_x87(Emitter& e, const MemRef& m)
	{
		if (m.absolute)
		{
			emit_abs_addr_load(e, m.abs_addr, 3);
			MemRef out = m;
			out.absolute = false;
			out.base = reg_spec("rbx");
			out.disp = 0;
			return out;
		}
		return m;
	}

	static void emit_rm_mem(Emitter& e, int regfield, const MemRef& m)
	{
		MemRef mm = materialize_absolute_mem(e, m);
		int base = mm.base.code;
		emit_rex(e, false, regfield, 0, base);
		emit_modrm(e, 2, regfield, (base & 7) == 4 ? 4 : base);
		if ((base & 7) == 4)
			emit_sib(e, 0, 4, base);
		emit_disp32(e, static_cast<int32_t>(mm.disp));
	}

	static void emit_rm_mem_no_rex(Emitter& e, int regfield, const MemRef& mm)
	{
		int base = mm.base.code;
		emit_modrm(e, 2, regfield, (base & 7) == 4 ? 4 : base);
		if ((base & 7) == 4)
			emit_sib(e, 0, 4, base);
		emit_disp32(e, static_cast<int32_t>(mm.disp));
	}

	static void emit_mov_reg_reg(Emitter& e, int width, const RegSpec& dst, const RegSpec& src)
	{
		bool w = width == 64;
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, w, src.code, 0, dst.code);
		e.emit8(width == 8 ? 0x88 : 0x89);
		emit_modrm(e, 3, src.code, dst.code);
	}

	static void emit_mov_reg_imm(Emitter& e, int width, const RegSpec& dst, uint64_t imm)
	{
		if (width == 8)
		{
			emit_rex(e, false, 0, 0, dst.code);
			e.emit8(static_cast<unsigned char>(0xB0 + (dst.code & 7)));
			e.emit8(static_cast<unsigned char>(imm & 0xff));
			return;
		}
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, 0, 0, dst.code);
		e.emit8(static_cast<unsigned char>(0xB8 + (dst.code & 7)));
		if (width == 16)
			e.emit16(static_cast<uint16_t>(imm));
		else if (width == 32)
			e.emit32(static_cast<uint32_t>(imm));
		else
			e.emit64(imm);
	}

	static void emit_mov_reg_mem(Emitter& e, int width, const RegSpec& dst, const MemRef& src)
	{
		MemRef m = materialize_absolute_mem(e, src);
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, dst.code, 0, m.base.code);
		e.emit8(width == 8 ? 0x8A : 0x8B);
		emit_modrm(e, 2, dst.code, (m.base.code & 7) == 4 ? 4 : m.base.code);
		if ((m.base.code & 7) == 4)
			emit_sib(e, 0, 4, m.base.code);
		emit_disp32(e, static_cast<int32_t>(m.disp));
	}

	static void emit_mov_mem_reg(Emitter& e, int width, const MemRef& dst, const RegSpec& src)
	{
		MemRef m = materialize_absolute_mem(e, dst);
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, src.code, 0, m.base.code);
		e.emit8(width == 8 ? 0x88 : 0x89);
		emit_modrm(e, 2, src.code, (m.base.code & 7) == 4 ? 4 : m.base.code);
		if ((m.base.code & 7) == 4)
			emit_sib(e, 0, 4, m.base.code);
		emit_disp32(e, static_cast<int32_t>(m.disp));
	}

	static void emit_movsx_reg_mem(Emitter& e, int srcw, const RegSpec& dst, const MemRef& src)
	{
		MemRef m = materialize_absolute_mem(e, src);
		emit_rex(e, true, dst.code, 0, m.base.code);
		e.emit8(0x0f);
		if (srcw == 8)
			e.emit8(0xbe);
		else if (srcw == 16)
			e.emit8(0xbf);
		else if (srcw == 32)
			e.emit8(0x63);
		else
			throw runtime_error("bad movsx width");
		emit_modrm(e, 2, dst.code, (m.base.code & 7) == 4 ? 4 : m.base.code);
		if ((m.base.code & 7) == 4)
			emit_sib(e, 0, 4, m.base.code);
		emit_disp32(e, static_cast<int32_t>(m.disp));
	}

	static void emit_movzx_reg_mem(Emitter& e, int srcw, const RegSpec& dst, const MemRef& src)
	{
		MemRef m = materialize_absolute_mem(e, src);
		emit_rex(e, true, dst.code, 0, m.base.code);
		e.emit8(0x0f);
		if (srcw == 8)
			e.emit8(0xb6);
		else if (srcw == 16)
			e.emit8(0xb7);
		else
			throw runtime_error("bad movzx width");
		emit_modrm(e, 2, dst.code, (m.base.code & 7) == 4 ? 4 : m.base.code);
		if ((m.base.code & 7) == 4)
			emit_sib(e, 0, 4, m.base.code);
		emit_disp32(e, static_cast<int32_t>(m.disp));
	}

	static void emit_movzx_reg_reg(Emitter& e, const RegSpec& dst, const RegSpec& src)
	{
		emit_rex(e, true, dst.code, 0, src.code);
		e.emit8(0x0f);
		e.emit8(0xb6);
		emit_modrm(e, 3, dst.code, src.code);
	}

	static void emit_btr_reg_imm8(Emitter& e, const RegSpec& r, unsigned char bit)
	{
		emit_rex(e, true, 0, 0, r.code);
		e.emit8(0x0f);
		e.emit8(0xba);
		emit_modrm(e, 3, 6, r.code);
		e.emit8(bit);
	}

	static void emit_btc_reg_imm8(Emitter& e, const RegSpec& r, unsigned char bit)
	{
		emit_rex(e, true, 0, 0, r.code);
		e.emit8(0x0f);
		e.emit8(0xba);
		emit_modrm(e, 3, 7, r.code);
		e.emit8(bit);
	}

	static void emit_mov_mem_imm(Emitter& e, int width, const MemRef& dst, uint64_t imm)
	{
		RegSpec tmp = reg_spec("r11");
		emit_mov_reg_imm(e, width, tmp, imm);
		emit_mov_mem_reg(e, width, dst, tmp);
	}

	static void emit_binop_reg(Emitter& e, int width, unsigned char op8, unsigned char op16, unsigned char op32, unsigned char op64, const RegSpec& dst, const RegSpec& src)
	{
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, src.code, 0, dst.code);
		e.emit8(width == 8 ? op8 : width == 16 ? op16 : width == 32 ? op32 : op64);
		emit_modrm(e, 3, src.code, dst.code);
	}

	static void emit_unary_reg(Emitter& e, int width, unsigned char op8, unsigned char op16, unsigned char op32, unsigned char op64, const RegSpec& dst)
	{
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, 0, 0, dst.code);
		e.emit8(width == 8 ? op8 : width == 16 ? op16 : width == 32 ? op32 : op64);
		emit_modrm(e, 3, 2, dst.code);
	}

	static void emit_cmp_reg(Emitter& e, int width, const RegSpec& lhs, const RegSpec& rhs)
	{
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, rhs.code, 0, lhs.code);
		e.emit8(width == 8 ? 0x38 : 0x39);
		emit_modrm(e, 3, rhs.code, lhs.code);
	}

	static void emit_test_reg(Emitter& e, int width, const RegSpec& r)
	{
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, r.code, 0, r.code);
		e.emit8(width == 8 ? 0x84 : 0x85);
		emit_modrm(e, 3, r.code, r.code);
	}

	static void emit_setcc_reg8(Emitter& e, unsigned char cc, const RegSpec& dst)
	{
		emit_rex(e, false, 0, 0, dst.code);
		e.emit8(0x0F);
		e.emit8(static_cast<unsigned char>(0x90 + cc));
		emit_modrm(e, 3, 0, dst.code);
	}

	static void emit_shift_reg(Emitter& e, int width, unsigned char op8, unsigned char op16, unsigned char op32, unsigned char op64, const RegSpec& dst, const RegSpec& count)
	{
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, count.code, 0, dst.code);
		e.emit8(width == 8 ? op8 : width == 16 ? op16 : width == 32 ? op32 : op64);
		emit_modrm(e, 3, 1, dst.code);
	}

	static void emit_shift_reg_cl(Emitter& e, int width, unsigned char op8, unsigned char op16, unsigned char op32, unsigned char op64, const RegSpec& dst)
	{
		RegSpec cl = reg_spec("cl");
		emit_shift_reg(e, width, op8, op16, op32, op64, dst, cl);
	}

	static void emit_syscall(Emitter& e)
	{
		e.emit8(0x0f);
		e.emit8(0x05);
	}

	static vector<unsigned char> literal_to_bytes(const Token& t, bool negated)
	{
		uint64_t v = bytes_to_u64(t.bytes);
		if (negated)
		{
			unsigned __int128 n = v;
			n = 0 - n;
			v = static_cast<uint64_t>(n);
		}
		return encode_uint(v, t.bytes.size());
	}

	static void emit_data_bytes(Emitter& e, const Statement& st)
	{
		if (st.is_literal)
		{
			if (!st.literal_negated)
				e.emit_bytes(st.literal.bytes);
			else
				e.emit_bytes(literal_to_bytes(st.literal, true));
			return;
		}
		if (starts_with(st.opcode, "data"))
		{
			int width = width_from_opcode(st.opcode, "data");
			if (st.operands.size() != 1)
				throw runtime_error("bad data opcode");
			uint64_t v = eval_operand_as_int(st.operands[0], {});
			e.emit_bytes(encode_uint(v, static_cast<size_t>(width / 8)));
			return;
		}
		throw runtime_error("not a data statement");
	}

	static size_t data_align_of(const Statement& st)
	{
		if (st.is_literal)
			return st.literal.bytes.size();
		if (starts_with(st.opcode, "data"))
			return static_cast<size_t>(width_from_opcode(st.opcode, "data") / 8);
		return 1;
	}

	static size_t data_size_of(const Statement& st)
	{
		if (st.is_literal)
			return st.literal.bytes.size();
		if (starts_with(st.opcode, "data"))
			return static_cast<size_t>(width_from_opcode(st.opcode, "data") / 8);
		return 0;
	}

	static bool is_data_statement(const Statement& st)
	{
		return st.is_literal || starts_with(st.opcode, "data");
	}

	static MemRef mem_from_operand(const OperandExpr& op, const unordered_map<string, uint64_t>& labels)
	{
		return eval_memory_operand(op, labels);
	}

	static ValueRef value_from_operand(const OperandExpr& op, const unordered_map<string, uint64_t>& labels)
	{
		ValueRef v;
		if (op.grouped && !op.toks.empty() && op.delim == '[')
		{
			v.kind = ValueRef::Memory;
			v.mem = mem_from_operand(op, labels);
			return v;
		}
		if (op.toks.size() == 1 && op.toks[0].kind == Token::Identifier && is_register_name(op.toks[0].text))
		{
			v.kind = ValueRef::Register;
			v.reg = reg_spec(op.toks[0].text);
			return v;
		}
		v.kind = ValueRef::Immediate;
		v.imm = eval_operand_as_int(op, labels);
		return v;
	}

	static bool operand_mentions_reg(const OperandExpr& op, int code)
	{
		for (const Token& t : op.toks)
		{
			if (t.kind == Token::Identifier && is_register_name(t.text) && reg_spec(t.text).code == code)
				return true;
		}
		if (!op.toks.empty() && op.toks[0].kind == Token::Identifier && is_register_name(op.toks[0].text))
			return reg_spec(op.toks[0].text).code == code;
		return false;
	}

	static vector<int> referenced_reg_codes(const vector<OperandExpr>& ops)
	{
		vector<int> out;
		for (const OperandExpr& op : ops)
		{
			if (op.toks.size() == 1 && op.toks[0].kind == Token::Identifier && is_register_name(op.toks[0].text))
				out.push_back(reg_spec(op.toks[0].text).code);
			else if (op.toks.size() == 3 && op.toks[0].kind == Token::Identifier && is_register_name(op.toks[0].text))
				out.push_back(reg_spec(op.toks[0].text).code);
		}
		sort(out.begin(), out.end());
		out.erase(unique(out.begin(), out.end()), out.end());
		return out;
	}

	static bool code_is_in(const vector<int>& codes, int code)
	{
		return find(codes.begin(), codes.end(), code) != codes.end();
	}

	static RegSpec pick_scratch_reg(int width, const vector<int>& used, size_t index)
	{
		static const char* names[] = {"z64", "t64", "y64", "x64"};
		vector<RegSpec> choices;
		for (const char* name : names)
		{
			RegSpec r = reg_for_width(name, width);
			if (!code_is_in(used, r.code))
				choices.push_back(r);
		}
		if (choices.empty())
		{
			for (const char* name : names)
				choices.push_back(reg_for_width(name, width));
		}
		return choices[index % choices.size()];
	}

	static void load_to_reg(Emitter& e, const ValueRef& v, const RegSpec& dst)
	{
		if (v.kind == ValueRef::Register)
		{
			emit_mov_reg_reg(e, dst.width, dst, v.reg);
			return;
		}
		if (v.kind == ValueRef::Immediate)
		{
			emit_mov_reg_imm(e, dst.width, dst, v.imm);
			return;
		}
		if (v.kind == ValueRef::Memory)
		{
			emit_mov_reg_mem(e, dst.width, dst, v.mem);
			return;
		}
		throw runtime_error("bad value");
	}

	static void store_from_reg(Emitter& e, const RegSpec& src, const ValueRef& dst)
	{
		if (dst.kind == ValueRef::Register)
		{
			emit_mov_reg_reg(e, dst.reg.width, dst.reg, src);
			return;
		}
		if (dst.kind == ValueRef::Memory)
		{
			emit_mov_mem_reg(e, src.width, dst.mem, src);
			return;
		}
		throw runtime_error("cannot store to immediate");
	}

	static void emit_int_binary(Emitter& e, const string& opcode, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels)
	{
		int width = width_from_opcode(opcode, opcode.substr(0, opcode.find_first_of("0123456789")));
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef a = value_from_operand(ops[1], labels);
		ValueRef b = value_from_operand(ops[2], labels);
		vector<int> used = referenced_reg_codes(ops);
		if (dst.kind == ValueRef::Register)
			used.push_back(dst.reg.code);
		sort(used.begin(), used.end());
		used.erase(unique(used.begin(), used.end()), used.end());
		RegSpec ra = reg_for_width("rax", width);
		RegSpec rb = reg_for_width("rbx", width);
		load_to_reg(e, a, ra);
		load_to_reg(e, b, rb);
		if (starts_with(opcode, "iadd"))
			emit_binop_reg(e, width, 0x00, 0x01, 0x01, 0x01, ra, rb);
		else if (starts_with(opcode, "isub"))
			emit_binop_reg(e, width, 0x28, 0x29, 0x29, 0x29, ra, rb);
		else if (starts_with(opcode, "and"))
			emit_binop_reg(e, width, 0x20, 0x21, 0x21, 0x21, ra, rb);
		else if (starts_with(opcode, "or"))
			emit_binop_reg(e, width, 0x08, 0x09, 0x09, 0x09, ra, rb);
		else if (starts_with(opcode, "xor"))
			emit_binop_reg(e, width, 0x30, 0x31, 0x31, 0x31, ra, rb);
		else
			throw runtime_error("unknown binary int opcode");
		store_from_reg(e, ra, dst);
	}

	static void emit_int_unary(Emitter& e, const string& opcode, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels)
	{
		int width = width_from_opcode(opcode, opcode.substr(0, opcode.find_first_of("0123456789")));
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef a = value_from_operand(ops[1], labels);
		vector<int> used = referenced_reg_codes(ops);
		if (dst.kind == ValueRef::Register)
			used.push_back(dst.reg.code);
		sort(used.begin(), used.end());
		used.erase(unique(used.begin(), used.end()), used.end());
		RegSpec ra = reg_for_width("rax", width);
		load_to_reg(e, a, ra);
		if (starts_with(opcode, "not"))
			emit_unary_reg(e, width, 0xf6, 0xf7, 0xf7, 0xf7, ra);
		else
			throw runtime_error("unknown unary int opcode");
		store_from_reg(e, ra, dst);
	}

	static void emit_int_shift(Emitter& e, const string& opcode, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels)
	{
		int width = width_from_opcode(opcode, opcode.substr(0, opcode.find_first_of("0123456789")));
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef a = value_from_operand(ops[1], labels);
		ValueRef b = value_from_operand(ops[2], labels);
		RegSpec ra = reg_for_width("rax", width);
		RegSpec rc = reg_for_width("rcx", 64);
		load_to_reg(e, a, ra);
		load_to_reg(e, b, rc);
		int group = 0;
		if (starts_with(opcode, "lshift"))
			group = 4;
		else if (starts_with(opcode, "urshift"))
			group = 5;
		else if (starts_with(opcode, "srshift"))
			group = 7;
		else
			throw runtime_error("unknown shift opcode");
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, 0, 0, ra.code);
		e.emit8(width == 8 ? 0xd2 : 0xd3);
		emit_modrm(e, 3, group, ra.code);
		store_from_reg(e, ra, dst);
	}

	static void emit_int_cmp(Emitter& e, const string& opcode, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels)
	{
		int width = width_from_opcode(opcode, opcode.substr(0, opcode.find_first_of("0123456789")));
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef a = value_from_operand(ops[1], labels);
		ValueRef b = value_from_operand(ops[2], labels);
		vector<int> used = referenced_reg_codes(ops);
		if (dst.kind == ValueRef::Register)
			used.push_back(dst.reg.code);
		sort(used.begin(), used.end());
		used.erase(unique(used.begin(), used.end()), used.end());
		RegSpec ra = reg_for_width("rax", width);
		RegSpec rb = reg_for_width("rbx", width);
		load_to_reg(e, a, ra);
		load_to_reg(e, b, rb);
		emit_cmp_reg(e, width, ra, rb);
		unsigned char cc = 0;
		if (starts_with(opcode, "ieq")) cc = 0x04;
		else if (starts_with(opcode, "ine")) cc = 0x05;
		else if (starts_with(opcode, "slt")) cc = 0x0c;
		else if (starts_with(opcode, "ult")) cc = 0x02;
		else if (starts_with(opcode, "sgt")) cc = 0x0f;
		else if (starts_with(opcode, "ugt")) cc = 0x07;
		else if (starts_with(opcode, "sle")) cc = 0x0e;
		else if (starts_with(opcode, "ule")) cc = 0x06;
		else if (starts_with(opcode, "sge")) cc = 0x0d;
		else if (starts_with(opcode, "uge")) cc = 0x03;
		else
			throw runtime_error("unknown cmp opcode");
		RegSpec rbool = reg_spec("al");
		emit_setcc_reg8(e, cc, rbool);
		store_from_reg(e, rbool, dst);
	}

	static void emit_int_divmod(Emitter& e, const string& opcode, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels)
	{
		int width = width_from_opcode(opcode, opcode.substr(0, opcode.find_first_of("0123456789")));
		if (width != 8 && width != 16 && width != 32 && width != 64)
		{
			ostringstream oss;
			oss << "unsupported div width: " << opcode << " -> " << width;
			throw runtime_error(oss.str());
		}
		if (ops.size() != 3)
			throw runtime_error("bad div/mod operand count");
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef a = value_from_operand(ops[1], labels);
		ValueRef b = value_from_operand(ops[2], labels);
		RegSpec rax = reg_for_width("rax", width);
		RegSpec rbx = reg_for_width("rbx", width);
		RegSpec rdx = reg_for_width("rdx", width);
		load_to_reg(e, a, rax);
		load_to_reg(e, b, rbx);
		if (width == 8)
		{
			if (starts_with(opcode, "s"))
			{
				e.emit8(0x66);
				e.emit8(0x98);
			}
			else
			{
				emit_mov_reg_imm(e, 8, reg_spec("ah"), 0);
			}
		}
		else if (width == 16)
		{
			if (starts_with(opcode, "s"))
			{
				e.emit8(0x66);
				e.emit8(0x99);
			}
			else
			{
				emit_mov_reg_imm(e, 16, rdx, 0);
			}
		}
		else if (width == 32)
		{
			if (starts_with(opcode, "s"))
			{
				e.emit8(0x99);
			}
			else
			{
				emit_mov_reg_imm(e, 32, rdx, 0);
			}
		}
		else
		{
			if (starts_with(opcode, "s"))
			{
				e.emit8(0x48);
				e.emit8(0x99);
			}
			else
			{
				emit_mov_reg_imm(e, 64, rdx, 0);
			}
		}
		if (width == 8)
			e.emit8(0xf6);
		else
		{
			if (width == 64)
				e.emit8(0x48);
			else if (width == 16)
				e.emit8(0x66);
			e.emit8(0xf7);
		}
		emit_modrm(e, 3, starts_with(opcode, "s") ? 7 : 6, rbx.code);
		if (starts_with(opcode, "umod") || starts_with(opcode, "smod"))
		{
			if (width == 8)
			{
				RegSpec rtmp = reg_spec("al");
				emit_mov_reg_reg(e, 8, rtmp, reg_spec("ah"));
				store_from_reg(e, rtmp, dst);
			}
			else
				store_from_reg(e, rdx, dst);
		}
		else
			store_from_reg(e, rax, dst);
	}

	static void emit_int_mul(Emitter& e, const string& opcode, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels)
	{
		int width = width_from_opcode(opcode, opcode.substr(0, opcode.find_first_of("0123456789")));
		if (width != 8 && width != 16 && width != 32 && width != 64)
		{
			ostringstream oss;
			oss << "unsupported mul width: " << opcode << " -> " << width;
			throw runtime_error(oss.str());
		}
		if (ops.size() != 3)
			throw runtime_error("bad mul operand count");
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef a = value_from_operand(ops[1], labels);
		ValueRef b = value_from_operand(ops[2], labels);
		RegSpec rax = reg_spec("rax");
		RegSpec rbx = reg_spec("rbx");
		load_to_reg(e, a, rax);
		load_to_reg(e, b, rbx);
		if (width == 16)
			e.emit8(0x66);
		emit_rex(e, width == 64, 0, 0, rbx.code);
		e.emit8(width == 8 ? 0xf6 : 0xf7);
		emit_modrm(e, 3, starts_with(opcode, "s") ? 5 : 4, rbx.code);
		store_from_reg(e, rax, dst);
	}

	static void emit_move(Emitter& e, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels, int width)
	{
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef src = value_from_operand(ops[1], labels);
		vector<int> used = referenced_reg_codes(ops);
		if (dst.kind == ValueRef::Register)
			used.push_back(dst.reg.code);
		sort(used.begin(), used.end());
		used.erase(unique(used.begin(), used.end()), used.end());
		RegSpec tmp = reg_for_width("rax", width);
		load_to_reg(e, src, tmp);
		store_from_reg(e, tmp, dst);
	}

	static void emit_move80(Emitter& e, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels)
	{
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef src = value_from_operand(ops[1], labels);
		MemRef d = dst.mem;
		MemRef s = src.mem;
		if (dst.kind != ValueRef::Memory || src.kind != ValueRef::Memory)
			throw runtime_error("move80 expects memory operands");
		if (s.absolute)
			emit_abs_addr_load(e, s.abs_addr, 11);
		if (s.absolute)
		{
			e.emit8(0xdb);
			e.emit8(0x2d);
			e.emit32(0);
			throw runtime_error("absolute move80 not implemented");
		}
		if (d.absolute)
			emit_abs_addr_load(e, d.abs_addr, 11);
		e.emit8(0xdb);
		e.emit8(0x2e);
	}

	static void emit_syscall(Emitter& e, const string& opcode, const vector<OperandExpr>& ops, const unordered_map<string, uint64_t>& labels)
	{
		size_t nargs = static_cast<size_t>(opcode.back() - '0');
		if (ops.size() != nargs + 2)
			throw runtime_error("bad syscall operand count");
		ValueRef dst = value_from_operand(ops[0], labels);
		ValueRef nr = value_from_operand(ops[1], labels);
		RegSpec rax = reg_spec("x64");
		rax.code = 0;
		load_to_reg(e, nr, rax);
		static const char* arg_regs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
		for (size_t i = 0; i < nargs; ++i)
		{
			ValueRef arg = value_from_operand(ops[2 + i], labels);
			RegSpec rr = reg_spec(arg_regs[i]);
			rr.width = 64;
			load_to_reg(e, arg, rr);
		}
		emit_syscall(e);
		store_from_reg(e, rax, dst);
	}

	static void emit_jump_fixup(Emitter& e, const string& label)
	{
		e.emit8(0xe9);
		e.fixups.push_back({e.pos(), label, Fixup::Rel32});
		e.emit32(0);
	}

	static void emit_call_fixup(Emitter& e, const string& label)
	{
		e.emit8(0xe8);
		e.fixups.push_back({e.pos(), label, Fixup::Rel32});
		e.emit32(0);
	}

	static void emit_jcc_fixup(Emitter& e, unsigned char cc, const string& label)
	{
		e.emit8(0x0f);
		e.emit8(static_cast<unsigned char>(0x80 + cc));
		e.fixups.push_back({e.pos(), label, Fixup::Rel32});
		e.emit32(0);
	}

	static void emit_fld_mem(Emitter& e, int width, const MemRef& m)
	{
		MemRef mm = materialize_absolute_mem_x87(e, m);
		if (width == 32)
		{
			e.emit8(0xd9);
			emit_rm_mem_no_rex(e, 0, mm);
		}
		else if (width == 64)
		{
			e.emit8(0xdd);
			emit_rm_mem_no_rex(e, 0, mm);
		}
		else if (width == 80)
		{
			e.emit8(0xdb);
			emit_rm_mem_no_rex(e, 5, mm);
		}
		else
			throw runtime_error("bad fld width");
	}

	static void emit_fstp_mem(Emitter& e, int width, const MemRef& m)
	{
		MemRef mm = materialize_absolute_mem_x87(e, m);
		if (width == 32)
		{
			e.emit8(0xd9);
			emit_rm_mem_no_rex(e, 3, mm);
		}
		else if (width == 64)
		{
			e.emit8(0xdd);
			emit_rm_mem_no_rex(e, 3, mm);
		}
		else if (width == 80)
		{
			e.emit8(0xdb);
			emit_rm_mem_no_rex(e, 7, mm);
		}
		else
			throw runtime_error("bad fstp width");
	}

	static void emit_fild_mem(Emitter& e, int width, const MemRef& m)
	{
		MemRef mm = materialize_absolute_mem_x87(e, m);
		if (width == 16)
		{
			e.emit8(0xdf);
			emit_rm_mem_no_rex(e, 0, mm);
		}
		else if (width == 32)
		{
			e.emit8(0xdb);
			emit_rm_mem_no_rex(e, 0, mm);
		}
		else if (width == 64)
		{
			e.emit8(0xdf);
			emit_rm_mem_no_rex(e, 5, mm);
		}
		else
			throw runtime_error("bad fild width");
	}

	static void emit_fistp_mem(Emitter& e, int width, const MemRef& m)
	{
		MemRef mm = materialize_absolute_mem_x87(e, m);
		if (width == 16)
		{
			e.emit8(0xdf);
			emit_rm_mem_no_rex(e, 3, mm);
		}
		else if (width == 32)
		{
			e.emit8(0xdb);
			emit_rm_mem_no_rex(e, 3, mm);
		}
		else if (width == 64)
		{
			e.emit8(0xdf);
			emit_rm_mem_no_rex(e, 7, mm);
		}
		else
			throw runtime_error("bad fistp width");
	}

	static void emit_fpu_binop(Emitter& e, const string& opcode, int width, const MemRef& dst, const MemRef& a, const MemRef& b)
	{
		emit_fld_mem(e, width, a);
		emit_fld_mem(e, width, b);
		if (starts_with(opcode, "fadd"))
			e.emit8(0xde), e.emit8(0xc1);
		else if (starts_with(opcode, "fsub"))
			e.emit8(0xde), e.emit8(0xe9);
		else if (starts_with(opcode, "fmul"))
			e.emit8(0xde), e.emit8(0xc9);
		else if (starts_with(opcode, "fdiv"))
			e.emit8(0xde), e.emit8(0xf9);
		else
			throw runtime_error("bad fpu binop");
		emit_fstp_mem(e, width, dst);
	}

	static void emit_fpu_cmp(Emitter& e, int width, const MemRef& a, const MemRef& b, unsigned char cc, const MemRef& dst)
	{
		emit_fld_mem(e, width, b);
		emit_fld_mem(e, width, a);
		e.emit8(0xde); e.emit8(0xd9);
		e.emit8(0xdf); e.emit8(0xe0);
		e.emit8(0x9e);
		RegSpec al = reg_spec("al");
		emit_setcc_reg8(e, cc, al);
		emit_mov_mem_reg(e, 8, dst, al);
	}

	static void emit_fpu_conv_from_int(Emitter& e, const string& opcode, int srcw, const MemRef& dst, const MemRef& src, const MemRef& const2p63)
	{
		if (starts_with(opcode, "u64convf80"))
		{
			RegSpec rax = reg_spec("x64");
			RegSpec rbx = reg_spec("y64");
			emit_mov_reg_mem(e, 64, rax, src);
			emit_test_reg(e, 64, rax);
			emit_jcc_fixup(e, 0x08, "__cy86_u64convf80_hi");
			emit_mov_mem_reg(e, 64, MemRef{false, 0, rbx, 0}, rax);
			emit_fild_mem(e, 64, MemRef{false, 0, rbx, 0});
			emit_fstp_mem(e, 80, dst);
			return;
		}
		MemRef tmp = MemRef{false, 0, reg_spec("rbx"), 0};
		if (srcw == 8)
		{
			RegSpec rax = reg_spec("x64");
			emit_mov_reg_mem(e, 8, rax, src);
			if (starts_with(opcode, "u8"))
				emit_unary_reg(e, 64, 0,0,0,0, rax);
			emit_mov_mem_reg(e, 32, tmp, rax);
			emit_fild_mem(e, 32, tmp);
		}
		else if (srcw == 16)
		{
			RegSpec rax = reg_spec("x64");
			emit_mov_reg_mem(e, 16, rax, src);
			if (starts_with(opcode, "u16"))
				emit_mov_mem_reg(e, 16, tmp, rax);
			else
				emit_mov_mem_reg(e, 16, tmp, rax);
			emit_fild_mem(e, 16, tmp);
		}
		else if (srcw == 32)
		{
			RegSpec rax = reg_spec("x64");
			emit_mov_reg_mem(e, 32, rax, src);
			emit_mov_mem_reg(e, 32, tmp, rax);
			emit_fild_mem(e, 32, tmp);
		}
		else if (srcw == 64)
		{
			RegSpec rax = reg_spec("x64");
			emit_mov_reg_mem(e, 64, rax, src);
			if (starts_with(opcode, "u64"))
			{
				emit_test_reg(e, 64, rax);
				emit_jcc_fixup(e, 0x09, "__cy86_u64convf80_hi");
				emit_mov_mem_reg(e, 64, tmp, rax);
				emit_fild_mem(e, 64, tmp);
				emit_fstp_mem(e, 80, dst);
				return;
			}
			emit_mov_mem_reg(e, 64, tmp, rax);
			emit_fild_mem(e, 64, tmp);
		}
		else
			throw runtime_error("bad int conv width");
		if (starts_with(opcode, "s") || starts_with(opcode, "u"))
		{
			if (starts_with(opcode, "u64"))
			{
				emit_fld_mem(e, 80, const2p63);
				emit_fadd_mem:
				;
			}
		}
		emit_fstp_mem(e, 80, dst);
	}

	static void emit_fpu_conv_to_int_or_float(Emitter& e, const string& opcode, int dstw, const MemRef& dst, const MemRef& src, const MemRef& const2p63)
	{
		if (starts_with(opcode, "f80convf32"))
		{
			emit_fld_mem(e, 80, src);
			emit_fstp_mem(e, 32, dst);
			return;
		}
		if (starts_with(opcode, "f80convf64"))
		{
			emit_fld_mem(e, 80, src);
			emit_fstp_mem(e, 64, dst);
			return;
		}
		if (starts_with(opcode, "f80convu64"))
		{
			RegSpec rax = reg_spec("x64");
			RegSpec rbx = reg_spec("y64");
			MemRef tmp = MemRef{false, 0, rbx, 0};
			emit_fld_mem(e, 80, src);
			emit_fld_mem(e, 80, const2p63);
			e.emit8(0xde); e.emit8(0xe9);
			e.emit8(0xdf); e.emit8(0xe0);
			e.emit8(0x9e);
			emit_jcc_fixup(e, 0x03, "__cy86_f80convu64_hi");
			emit_fistp_mem(e, 64, tmp);
			emit_mov_reg_mem(e, 64, rax, tmp);
			emit_mov_mem_reg(e, 64, dst, rax);
			return;
		}
		emit_fld_mem(e, 80, src);
		if (dstw == 8)
			emit_fistp_mem(e, 64, dst);
		else if (dstw == 16)
			emit_fistp_mem(e, 16, dst);
		else if (dstw == 32)
			emit_fistp_mem(e, 32, dst);
		else if (dstw == 64)
			emit_fistp_mem(e, 64, dst);
		else
			throw runtime_error("bad float-to-int width");
	}

	static size_t estimate_opcode_size(const Statement& st, const unordered_map<string, uint64_t>& labels)
	{
		Emitter e;
		// Dummy labels are enough for fixed-size instruction selection.
		(void)labels;
		if (starts_with(st.opcode, "move") && st.opcode != "move80")
			emit_move(e, st.operands, labels, width_from_opcode(st.opcode, "move"));
		else if (st.opcode == "move80")
			emit_move80(e, st.operands, labels);
		else if (starts_with(st.opcode, "not"))
			emit_int_unary(e, st.opcode, st.operands, labels);
		else if (starts_with(st.opcode, "and") || starts_with(st.opcode, "or") || starts_with(st.opcode, "xor") || starts_with(st.opcode, "iadd") || starts_with(st.opcode, "isub"))
			emit_int_binary(e, st.opcode, st.operands, labels);
		else if (starts_with(st.opcode, "lshift") || starts_with(st.opcode, "urshift") || starts_with(st.opcode, "srshift"))
			emit_int_shift(e, st.opcode, st.operands, labels);
		else if (starts_with(st.opcode, "ieq") || starts_with(st.opcode, "ine") || starts_with(st.opcode, "slt") || starts_with(st.opcode, "ult") || starts_with(st.opcode, "sgt") || starts_with(st.opcode, "ugt") || starts_with(st.opcode, "sle") || starts_with(st.opcode, "ule") || starts_with(st.opcode, "sge") || starts_with(st.opcode, "uge"))
			emit_int_cmp(e, st.opcode, st.operands, labels);
		else if (starts_with(st.opcode, "umul") || starts_with(st.opcode, "smul"))
			emit_int_mul(e, st.opcode, st.operands, labels);
		else if (starts_with(st.opcode, "udiv") || starts_with(st.opcode, "umod") || starts_with(st.opcode, "sdiv") || starts_with(st.opcode, "smod"))
			emit_int_divmod(e, st.opcode, st.operands, labels);
		else if (starts_with(st.opcode, "jumpif"))
		{
			e.emit8(0x84);
			(void)e;
		}
		else if (starts_with(st.opcode, "jump") || starts_with(st.opcode, "call") || st.opcode == "ret")
		{
			(void)e;
		}
		else if (starts_with(st.opcode, "syscall"))
			emit_syscall(e, st.opcode, st.operands, labels);
		else if (starts_with(st.opcode, "f") && !starts_with(st.opcode, "f80"))
			(void)e;
		else if (starts_with(st.opcode, "f80"))
			(void)e;
		else if (starts_with(st.opcode, "data"))
			(void)e;
		else
			(void)e;
		return e.pos();
	}

	static void emit_statement(Emitter& e, const Statement& st, const unordered_map<string, uint64_t>& labels)
	{
		if (st.is_literal || starts_with(st.opcode, "data"))
		{
			emit_data_bytes(e, st);
			return;
		}
		if (starts_with(st.opcode, "move") && st.opcode != "move80")
		{
			emit_move(e, st.operands, labels, width_from_opcode(st.opcode, "move"));
			return;
		}
		if (st.opcode == "move80")
		{
			emit_move80(e, st.operands, labels);
			return;
		}
		if (starts_with(st.opcode, "not"))
		{
			emit_int_unary(e, st.opcode, st.operands, labels);
			return;
		}
		if (starts_with(st.opcode, "and") || starts_with(st.opcode, "or") || starts_with(st.opcode, "xor") || starts_with(st.opcode, "iadd") || starts_with(st.opcode, "isub"))
		{
			emit_int_binary(e, st.opcode, st.operands, labels);
			return;
		}
		if (starts_with(st.opcode, "lshift") || starts_with(st.opcode, "urshift") || starts_with(st.opcode, "srshift"))
		{
			emit_int_shift(e, st.opcode, st.operands, labels);
			return;
		}
		if (starts_with(st.opcode, "ieq") || starts_with(st.opcode, "ine") || starts_with(st.opcode, "slt") || starts_with(st.opcode, "ult") || starts_with(st.opcode, "sgt") || starts_with(st.opcode, "ugt") || starts_with(st.opcode, "sle") || starts_with(st.opcode, "ule") || starts_with(st.opcode, "sge") || starts_with(st.opcode, "uge"))
		{
			emit_int_cmp(e, st.opcode, st.operands, labels);
			return;
		}
		if (starts_with(st.opcode, "umul") || starts_with(st.opcode, "smul"))
		{
			emit_int_mul(e, st.opcode, st.operands, labels);
			return;
		}
		if (starts_with(st.opcode, "udiv") || starts_with(st.opcode, "umod") || starts_with(st.opcode, "sdiv") || starts_with(st.opcode, "smod"))
		{
			emit_int_divmod(e, st.opcode, st.operands, labels);
				return;
			}
			if (starts_with(st.opcode, "jumpif"))
		{
			ValueRef cond = value_from_operand(st.operands[0], labels);
			ValueRef target = value_from_operand(st.operands[1], labels);
			if (cond.kind == ValueRef::Register)
				emit_test_reg(e, cond.reg.width, cond.reg);
			else
			{
				RegSpec ra = reg_spec("al");
				load_to_reg(e, cond, ra);
				emit_test_reg(e, 8, ra);
			}
			if (target.kind != ValueRef::Immediate)
				throw runtime_error("jumpif target must be label");
			emit_jcc_fixup(e, 0x05, st.operands[1].toks[0].text);
			return;
		}
		if (st.opcode == "jump")
		{
			if (st.operands.size() != 1)
				throw runtime_error("bad jump");
			if (st.operands[0].toks.size() == 1 && st.operands[0].toks[0].kind == Token::Identifier && !is_register_name(st.operands[0].toks[0].text))
			{
				emit_jump_fixup(e, st.operands[0].toks[0].text);
				return;
			}
			ValueRef target = value_from_operand(st.operands[0], labels);
			RegSpec rax = reg_spec("x64");
			load_to_reg(e, target, rax);
			e.emit8(0xff);
			emit_modrm(e, 3, 4, rax.code);
			return;
		}
		if (st.opcode == "call")
		{
			if (st.operands.size() != 1)
				throw runtime_error("bad call");
			if (st.operands[0].toks.size() == 1 && st.operands[0].toks[0].kind == Token::Identifier && !is_register_name(st.operands[0].toks[0].text))
			{
				emit_call_fixup(e, st.operands[0].toks[0].text);
				return;
			}
			ValueRef target = value_from_operand(st.operands[0], labels);
			RegSpec rax = reg_spec("x64");
			load_to_reg(e, target, rax);
			e.emit8(0xff);
			emit_modrm(e, 3, 2, rax.code);
			return;
		}
		if (st.opcode == "ret")
		{
			e.emit8(0xc3);
			return;
		}
		if (starts_with(st.opcode, "syscall"))
		{
			emit_syscall(e, st.opcode, st.operands, labels);
			return;
		}
		if (starts_with(st.opcode, "fadd") || starts_with(st.opcode, "fsub") || starts_with(st.opcode, "fmul") || starts_with(st.opcode, "fdiv"))
		{
			int width = width_after_prefix(st.opcode, st.opcode.substr(0, 4));
			ValueRef dst = value_from_operand(st.operands[0], labels);
			ValueRef a = value_from_operand(st.operands[1], labels);
			ValueRef b = value_from_operand(st.operands[2], labels);
			if (dst.kind != ValueRef::Memory || a.kind != ValueRef::Memory || b.kind != ValueRef::Memory)
				throw runtime_error("float ops expect memory operands");
			emit_fpu_binop(e, st.opcode, width, dst.mem, a.mem, b.mem);
			return;
		}
		if (starts_with(st.opcode, "feq") || starts_with(st.opcode, "fne") || starts_with(st.opcode, "flt") || starts_with(st.opcode, "fgt") || starts_with(st.opcode, "fle") || starts_with(st.opcode, "fge"))
		{
			int width = width_from_opcode(st.opcode, st.opcode.substr(0, 3));
			ValueRef dst = value_from_operand(st.operands[0], labels);
			ValueRef a = value_from_operand(st.operands[1], labels);
			ValueRef b = value_from_operand(st.operands[2], labels);
			if (dst.kind != ValueRef::Memory || a.kind != ValueRef::Memory || b.kind != ValueRef::Memory)
				throw runtime_error("float cmp expects memory operands");
			unsigned char cc = 0x04;
			if (starts_with(st.opcode, "fne")) cc = 0x05;
			else if (starts_with(st.opcode, "flt")) cc = 0x02;
			else if (starts_with(st.opcode, "fgt")) cc = 0x07;
			else if (starts_with(st.opcode, "fle")) cc = 0x06;
			else if (starts_with(st.opcode, "fge")) cc = 0x03;
			emit_fpu_cmp(e, width, a.mem, b.mem, cc, dst.mem);
			return;
		}
		if (starts_with(st.opcode, "s") && ends_with(st.opcode, "convf80"))
		{
			int srcw = width_before_suffix(st.opcode, "convf80");
			ValueRef dst = value_from_operand(st.operands[0], labels);
			ValueRef src = value_from_operand(st.operands[1], labels);
			if (dst.kind != ValueRef::Memory || src.kind != ValueRef::Memory)
				throw runtime_error("int->f80 expects memory operands");
			RegSpec rax = reg_spec("x64");
			if (srcw == 8)
			{
				emit_movsx_reg_mem(e, 8, rax, src.mem);
				emit_mov_mem_reg(e, 64, dst.mem, rax);
				emit_fild_mem(e, 64, dst.mem);
			}
			else if (srcw == 16)
			{
				emit_fild_mem(e, 16, src.mem);
			}
			else if (srcw == 32)
			{
				emit_fild_mem(e, 32, src.mem);
			}
			else if (srcw == 64)
			{
				emit_fild_mem(e, 64, src.mem);
			}
			else
				throw runtime_error("bad int conv width");
			emit_fstp_mem(e, 80, dst.mem);
			return;
		}
		if (starts_with(st.opcode, "u") && ends_with(st.opcode, "convf80"))
		{
			int srcw = width_before_suffix(st.opcode, "convf80");
			ValueRef dst = value_from_operand(st.operands[0], labels);
			ValueRef src = value_from_operand(st.operands[1], labels);
			if (dst.kind != ValueRef::Memory || src.kind != ValueRef::Memory)
				throw runtime_error("int->f80 expects memory operands");
			RegSpec rax = reg_spec("x64");
			if (srcw == 64)
			{
				RegSpec rbx = reg_spec("rbx");
				MemRef const2p32 = MemRef{true, labels.at("__cy86_const_2pow32"), reg_spec("rbx"), 0};
				emit_mov_reg_mem(e, 64, rax, src.mem);
				emit_mov_reg_reg(e, 64, rbx, rax);
				e.emit8(0x48);
				e.emit8(0xc1);
				e.emit8(0xeb);
				e.emit8(0x20);
				emit_mov_reg_mem(e, 32, rax, src.mem);
				emit_mov_mem_reg(e, 64, src.mem, rbx);
				emit_mov_mem_reg(e, 64, dst.mem, rax);
				emit_fild_mem(e, 64, src.mem);
				emit_fld_mem(e, 80, const2p32);
				e.emit8(0xde);
				e.emit8(0xc9);
				emit_fild_mem(e, 64, dst.mem);
				e.emit8(0xde);
				e.emit8(0xc1);
				emit_fstp_mem(e, 80, dst.mem);
				return;
			}
			else if (srcw == 8)
			{
				emit_movzx_reg_mem(e, 8, rax, src.mem);
				emit_mov_mem_reg(e, 64, dst.mem, rax);
				emit_fild_mem(e, 64, dst.mem);
			}
			else if (srcw == 16)
			{
				emit_movzx_reg_mem(e, 16, rax, src.mem);
				emit_mov_mem_reg(e, 64, dst.mem, rax);
				emit_fild_mem(e, 64, dst.mem);
			}
			else if (srcw == 32)
			{
				emit_mov_reg_mem(e, 32, rax, src.mem);
				emit_mov_mem_reg(e, 64, dst.mem, rax);
				emit_fild_mem(e, 64, dst.mem);
			}
			else
				throw runtime_error("bad int conv width");
			emit_fstp_mem(e, 80, dst.mem);
			return;
		}
		if (starts_with(st.opcode, "f32convf80") || starts_with(st.opcode, "f64convf80"))
		{
			int srcw = starts_with(st.opcode, "f32") ? 32 : 64;
			ValueRef dst = value_from_operand(st.operands[0], labels);
			ValueRef src = value_from_operand(st.operands[1], labels);
			if (dst.kind != ValueRef::Memory || src.kind != ValueRef::Memory)
				throw runtime_error("float->f80 expects memory operands");
			emit_fld_mem(e, srcw, src.mem);
			emit_fstp_mem(e, 80, dst.mem);
			return;
		}
		if (starts_with(st.opcode, "f80convf32") || starts_with(st.opcode, "f80convf64"))
		{
			int dstw = starts_with(st.opcode, "f80convf32") ? 32 : 64;
			ValueRef dst = value_from_operand(st.operands[0], labels);
			ValueRef src = value_from_operand(st.operands[1], labels);
			if (dst.kind != ValueRef::Memory || src.kind != ValueRef::Memory)
				throw runtime_error("f80->float expects memory operands");
			emit_fld_mem(e, 80, src.mem);
			emit_fstp_mem(e, dstw, dst.mem);
			return;
		}
		if (starts_with(st.opcode, "f80convs") || starts_with(st.opcode, "f80convu"))
		{
			bool is_unsigned = starts_with(st.opcode, "f80convu");
			int dstw = width_after_prefix(st.opcode, is_unsigned ? "f80convu" : "f80convs");
			ValueRef dst = value_from_operand(st.operands[0], labels);
			ValueRef src = value_from_operand(st.operands[1], labels);
			if (dst.kind != ValueRef::Memory || src.kind != ValueRef::Memory)
				throw runtime_error("f80->int expects memory operands");
			RegSpec rax = reg_spec("x64");
			if (!is_unsigned)
			{
				emit_fld_mem(e, 80, src.mem);
				emit_fistp_mem(e, 64, dst.mem);
				return;
			}

			const char* bias_label = 0;
			unsigned char bit = 0;
			if (dstw == 8)
			{
				bias_label = "__cy86_const_2pow7";
				bit = 7;
			}
			else if (dstw == 16)
			{
				bias_label = "__cy86_const_2pow15";
				bit = 15;
			}
			else if (dstw == 32)
			{
				bias_label = "__cy86_const_2pow31";
				bit = 31;
			}
			else if (dstw == 64)
			{
				bias_label = "__cy86_const_2pow63";
				bit = 63;
			}
			else
				throw runtime_error("bad float-to-int width");

			MemRef const_bias = MemRef{true, labels.at(bias_label), reg_spec("r11"), 0};
			emit_fld_mem(e, 80, src.mem);
			emit_fld_mem(e, 80, const_bias);
			e.emit8(0xde);
			e.emit8(0xe9);
			emit_fistp_mem(e, 64, dst.mem);
			emit_mov_reg_mem(e, 64, rax, dst.mem);
			emit_btc_reg_imm8(e, rax, bit);
			emit_mov_mem_reg(e, 64, dst.mem, rax);
			return;
		}
		throw runtime_error("unsupported opcode: " + st.opcode);
	}

	static void fixup_branches(Emitter& e, uint64_t base, size_t header_size, const unordered_map<string, uint64_t>& labels)
	{
		for (const Fixup& f : e.fixups)
		{
			auto it = labels.find(f.label);
			if (it == labels.end())
				throw runtime_error("unresolved label: " + f.label);
			uint64_t target = it->second;
			uint64_t next = base + header_size + f.offset + 4;
			int64_t rel = static_cast<int64_t>(target) - static_cast<int64_t>(next);
			uint32_t v = static_cast<uint32_t>(rel);
			for (int i = 0; i < 4; ++i)
				e.bytes[f.offset + i] = static_cast<unsigned char>((v >> (8 * i)) & 0xff);
		}
	}

	static vector<unsigned char> make_2pow63()
	{
		long double v = 9223372036854775808.0L;
		vector<unsigned char> out(10);
		memcpy(&out[0], &v, 10);
		return out;
	}

	static vector<unsigned char> make_pow2_constant(int exp)
	{
		long double v = 1.0L;
		for (int i = 0; i < exp; ++i)
			v *= 2.0L;
		vector<unsigned char> out(10);
		memcpy(&out[0], &v, 10);
		return out;
	}

	static void append_padding(vector<unsigned char>& body, size_t align)
	{
		size_t off = kElfHeaderSize + body.size();
		size_t aligned = align_up(off, align);
		body.insert(body.end(), aligned - off, 0);
	}

	static unordered_map<string, uint64_t> assign_labels(const vector<Statement>& stmts, uint64_t& first_stmt_addr)
	{
		unordered_map<string, uint64_t> labels;
		size_t off = kElfHeaderSize;
		first_stmt_addr = 0;
		bool first = true;
		for (const Statement& st : stmts)
		{
			if (is_data_statement(st))
				off = align_up(off, data_align_of(st));
			if (first)
			{
				first_stmt_addr = kBaseAddr + off;
				first = false;
			}
			if (st.has_label)
				labels[st.label] = kBaseAddr + off;
			off += is_data_statement(st) ? data_size_of(st) : 1;
		}
		return labels;
	}

	static size_t statement_size(const Statement& st, const unordered_map<string, uint64_t>& labels)
	{
		Emitter e;
		emit_statement(e, st, labels);
		return e.bytes.size();
	}

	static vector<unsigned char> compile(const vector<string>& srcfiles)
	{
		vector<Token> toks = load_tokens(srcfiles);
		vector<Statement> stmts = parse_program(toks);

		Statement const_stmt;
		const_stmt.is_literal = true;
		const_stmt.literal.kind = Token::Literal;
		const_stmt.literal.bytes = make_2pow63();
		const_stmt.literal_negated = false;
		const_stmt.has_label = true;
		const_stmt.label = "__cy86_const_2pow63";
		stmts.push_back(const_stmt);

		Statement const_stmt7;
		const_stmt7.is_literal = true;
		const_stmt7.literal.kind = Token::Literal;
		const_stmt7.literal.bytes = make_pow2_constant(7);
		const_stmt7.literal_negated = false;
		const_stmt7.has_label = true;
		const_stmt7.label = "__cy86_const_2pow7";
		stmts.push_back(const_stmt7);

		Statement const_stmt15;
		const_stmt15.is_literal = true;
		const_stmt15.literal.kind = Token::Literal;
		const_stmt15.literal.bytes = make_pow2_constant(15);
		const_stmt15.literal_negated = false;
		const_stmt15.has_label = true;
		const_stmt15.label = "__cy86_const_2pow15";
		stmts.push_back(const_stmt15);

		Statement const_stmt31;
		const_stmt31.is_literal = true;
		const_stmt31.literal.kind = Token::Literal;
		const_stmt31.literal.bytes = make_pow2_constant(31);
		const_stmt31.literal_negated = false;
		const_stmt31.has_label = true;
		const_stmt31.label = "__cy86_const_2pow31";
		stmts.push_back(const_stmt31);

		Statement const_stmt32;
		const_stmt32.is_literal = true;
		const_stmt32.literal.kind = Token::Literal;
		const_stmt32.literal.bytes = make_pow2_constant(32);
		const_stmt32.literal_negated = false;
		const_stmt32.has_label = true;
		const_stmt32.label = "__cy86_const_2pow32";
		stmts.push_back(const_stmt32);

		uint64_t first_stmt_addr = 0;
		unordered_map<string, uint64_t> labels = assign_labels(stmts, first_stmt_addr);

		size_t off = kElfHeaderSize;
		for (const Statement& st : stmts)
		{
			if (is_data_statement(st))
				off = align_up(off, data_align_of(st));
			if (st.has_label)
				labels[st.label] = kBaseAddr + off;
			off += is_data_statement(st) ? data_size_of(st) : statement_size(st, labels);
		}

		Emitter body;
		body.bytes.reserve(off - kElfHeaderSize);
		uint64_t entry_addr = labels.count("start") ? labels["start"] : first_stmt_addr;
		for (const Statement& st : stmts)
		{
			if (is_data_statement(st))
				append_padding(body.bytes, data_align_of(st));
			emit_statement(body, st, labels);
		}
		fixup_branches(body, kBaseAddr, kElfHeaderSize, labels);

		ElfHeader elf;
		ProgramSegmentHeader ph;
		elf.entry = static_cast<long int>(entry_addr);
		ph.filesz = static_cast<long int>(kElfHeaderSize + body.bytes.size());
		ph.memsz = ph.filesz;

		vector<unsigned char> out(kElfHeaderSize, 0);
		memcpy(&out[0], &elf, sizeof(elf));
		memcpy(&out[64], &ph, sizeof(ph));
		out.insert(out.end(), body.bytes.begin(), body.bytes.end());
		return out;
	}
}

extern "C" long int syscall(long int n, ...) throw ();

static bool PA9SetFileExecutable(const string& path)
{
	int res = syscall(90, path.c_str(), 0755);
	return res == 0;
}

int main(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int i = 1; i < argc; ++i)
			args.emplace_back(argv[i]);

		if (args.size() < 3 || args[0] != "-o")
			throw logic_error("invalid usage");

		string outfile = args[1];
		vector<string> srcfiles(args.begin() + 2, args.end());

		vector<unsigned char> image = pa9cy86::compile(srcfiles);
		ofstream out(outfile.c_str(), ios::binary);
		out.write(reinterpret_cast<const char*>(&image[0]), image.size());
		PA9SetFileExecutable(outfile);
		return EXIT_SUCCESS;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
