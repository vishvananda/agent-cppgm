// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <memory>
#include <cstring>
#include <cstdint>
#include <climits>
#include <map>

using namespace std;

#define CPPGM_PPTOKEN_LIBRARY
#define CPPGM_PPTOKEN_SKIP_TRIGRAPHS_IN_STRING_LITERALS
#include "pptoken.cpp"

// See 3.9.1: Fundamental Types
enum EFundamentalType
{
	// 3.9.1.2
	FT_SIGNED_CHAR,
	FT_SHORT_INT,
	FT_INT,
	FT_LONG_INT,
	FT_LONG_LONG_INT,

	// 3.9.1.3
	FT_UNSIGNED_CHAR,
	FT_UNSIGNED_SHORT_INT,
	FT_UNSIGNED_INT,
	FT_UNSIGNED_LONG_INT,
	FT_UNSIGNED_LONG_LONG_INT,

	// 3.9.1.1 / 3.9.1.5
	FT_WCHAR_T,
	FT_CHAR,
	FT_CHAR16_T,
	FT_CHAR32_T,

	// 3.9.1.6
	FT_BOOL,

	// 3.9.1.8
	FT_FLOAT,
	FT_DOUBLE,
	FT_LONG_DOUBLE,

	// 3.9.1.9
	FT_VOID,

	// 3.9.1.10
	FT_NULLPTR_T
};

// FundamentalTypeOf: convert fundamental type T to EFundamentalType
// for example: `FundamentalTypeOf<long int>()` will return `FT_LONG_INT`
template<typename T> constexpr EFundamentalType FundamentalTypeOf();
template<> constexpr EFundamentalType FundamentalTypeOf<signed char>() { return FT_SIGNED_CHAR; }
template<> constexpr EFundamentalType FundamentalTypeOf<short int>() { return FT_SHORT_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<int>() { return FT_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<long int>() { return FT_LONG_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<long long int>() { return FT_LONG_LONG_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned char>() { return FT_UNSIGNED_CHAR; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned short int>() { return FT_UNSIGNED_SHORT_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned int>() { return FT_UNSIGNED_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned long int>() { return FT_UNSIGNED_LONG_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<unsigned long long int>() { return FT_UNSIGNED_LONG_LONG_INT; }
template<> constexpr EFundamentalType FundamentalTypeOf<wchar_t>() { return FT_WCHAR_T; }
template<> constexpr EFundamentalType FundamentalTypeOf<char>() { return FT_CHAR; }
template<> constexpr EFundamentalType FundamentalTypeOf<char16_t>() { return FT_CHAR16_T; }
template<> constexpr EFundamentalType FundamentalTypeOf<char32_t>() { return FT_CHAR32_T; }
template<> constexpr EFundamentalType FundamentalTypeOf<bool>() { return FT_BOOL; }
template<> constexpr EFundamentalType FundamentalTypeOf<float>() { return FT_FLOAT; }
template<> constexpr EFundamentalType FundamentalTypeOf<double>() { return FT_DOUBLE; }
template<> constexpr EFundamentalType FundamentalTypeOf<long double>() { return FT_LONG_DOUBLE; }
template<> constexpr EFundamentalType FundamentalTypeOf<void>() { return FT_VOID; }
template<> constexpr EFundamentalType FundamentalTypeOf<nullptr_t>() { return FT_NULLPTR_T; }

// convert EFundamentalType to a source code
const map<EFundamentalType, string> FundamentalTypeToStringMap
{
	{FT_SIGNED_CHAR, "signed char"},
	{FT_SHORT_INT, "short int"},
	{FT_INT, "int"},
	{FT_LONG_INT, "long int"},
	{FT_LONG_LONG_INT, "long long int"},
	{FT_UNSIGNED_CHAR, "unsigned char"},
	{FT_UNSIGNED_SHORT_INT, "unsigned short int"},
	{FT_UNSIGNED_INT, "unsigned int"},
	{FT_UNSIGNED_LONG_INT, "unsigned long int"},
	{FT_UNSIGNED_LONG_LONG_INT, "unsigned long long int"},
	{FT_WCHAR_T, "wchar_t"},
	{FT_CHAR, "char"},
	{FT_CHAR16_T, "char16_t"},
	{FT_CHAR32_T, "char32_t"},
	{FT_BOOL, "bool"},
	{FT_FLOAT, "float"},
	{FT_DOUBLE, "double"},
	{FT_LONG_DOUBLE, "long double"},
	{FT_VOID, "void"},
	{FT_NULLPTR_T, "nullptr_t"}
};

// token type enum for `simples`
enum ETokenType
{
	// keywords
	KW_ALIGNAS,
	KW_ALIGNOF,
	KW_ASM,
	KW_AUTO,
	KW_BOOL,
	KW_BREAK,
	KW_CASE,
	KW_CATCH,
	KW_CHAR,
	KW_CHAR16_T,
	KW_CHAR32_T,
	KW_CLASS,
	KW_CONST,
	KW_CONSTEXPR,
	KW_CONST_CAST,
	KW_CONTINUE,
	KW_DECLTYPE,
	KW_DEFAULT,
	KW_DELETE,
	KW_DO,
	KW_DOUBLE,
	KW_DYNAMIC_CAST,
	KW_ELSE,
	KW_ENUM,
	KW_EXPLICIT,
	KW_EXPORT,
	KW_EXTERN,
	KW_FALSE,
	KW_FLOAT,
	KW_FOR,
	KW_FRIEND,
	KW_GOTO,
	KW_IF,
	KW_INLINE,
	KW_INT,
	KW_LONG,
	KW_MUTABLE,
	KW_NAMESPACE,
	KW_NEW,
	KW_NOEXCEPT,
	KW_NULLPTR,
	KW_OPERATOR,
	KW_PRIVATE,
	KW_PROTECTED,
	KW_PUBLIC,
	KW_REGISTER,
	KW_REINTERPET_CAST,
	KW_RETURN,
	KW_SHORT,
	KW_SIGNED,
	KW_SIZEOF,
	KW_STATIC,
	KW_STATIC_ASSERT,
	KW_STATIC_CAST,
	KW_STRUCT,
	KW_SWITCH,
	KW_TEMPLATE,
	KW_THIS,
	KW_THREAD_LOCAL,
	KW_THROW,
	KW_TRUE,
	KW_TRY,
	KW_TYPEDEF,
	KW_TYPEID,
	KW_TYPENAME,
	KW_UNION,
	KW_UNSIGNED,
	KW_USING,
	KW_VIRTUAL,
	KW_VOID,
	KW_VOLATILE,
	KW_WCHAR_T,
	KW_WHILE,

	// operators/punctuation
	OP_LBRACE,
	OP_RBRACE,
	OP_LSQUARE,
	OP_RSQUARE,
	OP_LPAREN,
	OP_RPAREN,
	OP_BOR,
	OP_XOR,
	OP_COMPL,
	OP_AMP,
	OP_LNOT,
	OP_SEMICOLON,
	OP_COLON,
	OP_DOTS,
	OP_QMARK,
	OP_COLON2,
	OP_DOT,
	OP_DOTSTAR,
	OP_PLUS,
	OP_MINUS,
	OP_STAR,
	OP_DIV,
	OP_MOD,
	OP_ASS,
	OP_LT,
	OP_GT,
	OP_PLUSASS,
	OP_MINUSASS,
	OP_STARASS,
	OP_DIVASS,
	OP_MODASS,
	OP_XORASS,
	OP_BANDASS,
	OP_BORASS,
	OP_LSHIFT,
	OP_RSHIFT,
	OP_RSHIFTASS,
	OP_LSHIFTASS,
	OP_EQ,
	OP_NE,
	OP_LE,
	OP_GE,
	OP_LAND,
	OP_LOR,
	OP_INC,
	OP_DEC,
	OP_COMMA,
	OP_ARROWSTAR,
	OP_ARROW,
};

// StringToETokenTypeMap map of `simple` `preprocessing-tokens` to ETokenType
const unordered_map<string, ETokenType> StringToTokenTypeMap =
{
	// keywords
	{"alignas", KW_ALIGNAS},
	{"alignof", KW_ALIGNOF},
	{"asm", KW_ASM},
	{"auto", KW_AUTO},
	{"bool", KW_BOOL},
	{"break", KW_BREAK},
	{"case", KW_CASE},
	{"catch", KW_CATCH},
	{"char", KW_CHAR},
	{"char16_t", KW_CHAR16_T},
	{"char32_t", KW_CHAR32_T},
	{"class", KW_CLASS},
	{"const", KW_CONST},
	{"constexpr", KW_CONSTEXPR},
	{"const_cast", KW_CONST_CAST},
	{"continue", KW_CONTINUE},
	{"decltype", KW_DECLTYPE},
	{"default", KW_DEFAULT},
	{"delete", KW_DELETE},
	{"do", KW_DO},
	{"double", KW_DOUBLE},
	{"dynamic_cast", KW_DYNAMIC_CAST},
	{"else", KW_ELSE},
	{"enum", KW_ENUM},
	{"explicit", KW_EXPLICIT},
	{"export", KW_EXPORT},
	{"extern", KW_EXTERN},
	{"false", KW_FALSE},
	{"float", KW_FLOAT},
	{"for", KW_FOR},
	{"friend", KW_FRIEND},
	{"goto", KW_GOTO},
	{"if", KW_IF},
	{"inline", KW_INLINE},
	{"int", KW_INT},
	{"long", KW_LONG},
	{"mutable", KW_MUTABLE},
	{"namespace", KW_NAMESPACE},
	{"new", KW_NEW},
	{"noexcept", KW_NOEXCEPT},
	{"nullptr", KW_NULLPTR},
	{"operator", KW_OPERATOR},
	{"private", KW_PRIVATE},
	{"protected", KW_PROTECTED},
	{"public", KW_PUBLIC},
	{"register", KW_REGISTER},
	{"reinterpret_cast", KW_REINTERPET_CAST},
	{"return", KW_RETURN},
	{"short", KW_SHORT},
	{"signed", KW_SIGNED},
	{"sizeof", KW_SIZEOF},
	{"static", KW_STATIC},
	{"static_assert", KW_STATIC_ASSERT},
	{"static_cast", KW_STATIC_CAST},
	{"struct", KW_STRUCT},
	{"switch", KW_SWITCH},
	{"template", KW_TEMPLATE},
	{"this", KW_THIS},
	{"thread_local", KW_THREAD_LOCAL},
	{"throw", KW_THROW},
	{"true", KW_TRUE},
	{"try", KW_TRY},
	{"typedef", KW_TYPEDEF},
	{"typeid", KW_TYPEID},
	{"typename", KW_TYPENAME},
	{"union", KW_UNION},
	{"unsigned", KW_UNSIGNED},
	{"using", KW_USING},
	{"virtual", KW_VIRTUAL},
	{"void", KW_VOID},
	{"volatile", KW_VOLATILE},
	{"wchar_t", KW_WCHAR_T},
	{"while", KW_WHILE},

	// operators/punctuation
	{"{", OP_LBRACE},
	{"<%", OP_LBRACE},
	{"}", OP_RBRACE},
	{"%>", OP_RBRACE},
	{"[", OP_LSQUARE},
	{"<:", OP_LSQUARE},
	{"]", OP_RSQUARE},
	{":>", OP_RSQUARE},
	{"(", OP_LPAREN},
	{")", OP_RPAREN},
	{"|", OP_BOR},
	{"bitor", OP_BOR},
	{"^", OP_XOR},
	{"xor", OP_XOR},
	{"~", OP_COMPL},
	{"compl", OP_COMPL},
	{"&", OP_AMP},
	{"bitand", OP_AMP},
	{"!", OP_LNOT},
	{"not", OP_LNOT},
	{";", OP_SEMICOLON},
	{":", OP_COLON},
	{"...", OP_DOTS},
	{"?", OP_QMARK},
	{"::", OP_COLON2},
	{".", OP_DOT},
	{".*", OP_DOTSTAR},
	{"+", OP_PLUS},
	{"-", OP_MINUS},
	{"*", OP_STAR},
	{"/", OP_DIV},
	{"%", OP_MOD},
	{"=", OP_ASS},
	{"<", OP_LT},
	{">", OP_GT},
	{"+=", OP_PLUSASS},
	{"-=", OP_MINUSASS},
	{"*=", OP_STARASS},
	{"/=", OP_DIVASS},
	{"%=", OP_MODASS},
	{"^=", OP_XORASS},
	{"xor_eq", OP_XORASS},
	{"&=", OP_BANDASS},
	{"and_eq", OP_BANDASS},
	{"|=", OP_BORASS},
	{"or_eq", OP_BORASS},
	{"<<", OP_LSHIFT},
	{">>", OP_RSHIFT},
	{">>=", OP_RSHIFTASS},
	{"<<=", OP_LSHIFTASS},
	{"==", OP_EQ},
	{"!=", OP_NE},
	{"not_eq", OP_NE},
	{"<=", OP_LE},
	{">=", OP_GE},
	{"&&", OP_LAND},
	{"and", OP_LAND},
	{"||", OP_LOR},
	{"or", OP_LOR},
	{"++", OP_INC},
	{"--", OP_DEC},
	{",", OP_COMMA},
	{"->*", OP_ARROWSTAR},
	{"->", OP_ARROW}
};

// map of enum to string
const map<ETokenType, string> TokenTypeToStringMap =
{
	{KW_ALIGNAS, "KW_ALIGNAS"},
	{KW_ALIGNOF, "KW_ALIGNOF"},
	{KW_ASM, "KW_ASM"},
	{KW_AUTO, "KW_AUTO"},
	{KW_BOOL, "KW_BOOL"},
	{KW_BREAK, "KW_BREAK"},
	{KW_CASE, "KW_CASE"},
	{KW_CATCH, "KW_CATCH"},
	{KW_CHAR, "KW_CHAR"},
	{KW_CHAR16_T, "KW_CHAR16_T"},
	{KW_CHAR32_T, "KW_CHAR32_T"},
	{KW_CLASS, "KW_CLASS"},
	{KW_CONST, "KW_CONST"},
	{KW_CONSTEXPR, "KW_CONSTEXPR"},
	{KW_CONST_CAST, "KW_CONST_CAST"},
	{KW_CONTINUE, "KW_CONTINUE"},
	{KW_DECLTYPE, "KW_DECLTYPE"},
	{KW_DEFAULT, "KW_DEFAULT"},
	{KW_DELETE, "KW_DELETE"},
	{KW_DO, "KW_DO"},
	{KW_DOUBLE, "KW_DOUBLE"},
	{KW_DYNAMIC_CAST, "KW_DYNAMIC_CAST"},
	{KW_ELSE, "KW_ELSE"},
	{KW_ENUM, "KW_ENUM"},
	{KW_EXPLICIT, "KW_EXPLICIT"},
	{KW_EXPORT, "KW_EXPORT"},
	{KW_EXTERN, "KW_EXTERN"},
	{KW_FALSE, "KW_FALSE"},
	{KW_FLOAT, "KW_FLOAT"},
	{KW_FOR, "KW_FOR"},
	{KW_FRIEND, "KW_FRIEND"},
	{KW_GOTO, "KW_GOTO"},
	{KW_IF, "KW_IF"},
	{KW_INLINE, "KW_INLINE"},
	{KW_INT, "KW_INT"},
	{KW_LONG, "KW_LONG"},
	{KW_MUTABLE, "KW_MUTABLE"},
	{KW_NAMESPACE, "KW_NAMESPACE"},
	{KW_NEW, "KW_NEW"},
	{KW_NOEXCEPT, "KW_NOEXCEPT"},
	{KW_NULLPTR, "KW_NULLPTR"},
	{KW_OPERATOR, "KW_OPERATOR"},
	{KW_PRIVATE, "KW_PRIVATE"},
	{KW_PROTECTED, "KW_PROTECTED"},
	{KW_PUBLIC, "KW_PUBLIC"},
	{KW_REGISTER, "KW_REGISTER"},
	{KW_REINTERPET_CAST, "KW_REINTERPET_CAST"},
	{KW_RETURN, "KW_RETURN"},
	{KW_SHORT, "KW_SHORT"},
	{KW_SIGNED, "KW_SIGNED"},
	{KW_SIZEOF, "KW_SIZEOF"},
	{KW_STATIC, "KW_STATIC"},
	{KW_STATIC_ASSERT, "KW_STATIC_ASSERT"},
	{KW_STATIC_CAST, "KW_STATIC_CAST"},
	{KW_STRUCT, "KW_STRUCT"},
	{KW_SWITCH, "KW_SWITCH"},
	{KW_TEMPLATE, "KW_TEMPLATE"},
	{KW_THIS, "KW_THIS"},
	{KW_THREAD_LOCAL, "KW_THREAD_LOCAL"},
	{KW_THROW, "KW_THROW"},
	{KW_TRUE, "KW_TRUE"},
	{KW_TRY, "KW_TRY"},
	{KW_TYPEDEF, "KW_TYPEDEF"},
	{KW_TYPEID, "KW_TYPEID"},
	{KW_TYPENAME, "KW_TYPENAME"},
	{KW_UNION, "KW_UNION"},
	{KW_UNSIGNED, "KW_UNSIGNED"},
	{KW_USING, "KW_USING"},
	{KW_VIRTUAL, "KW_VIRTUAL"},
	{KW_VOID, "KW_VOID"},
	{KW_VOLATILE, "KW_VOLATILE"},
	{KW_WCHAR_T, "KW_WCHAR_T"},
	{KW_WHILE, "KW_WHILE"},
	{OP_LBRACE, "OP_LBRACE"},
	{OP_RBRACE, "OP_RBRACE"},
	{OP_LSQUARE, "OP_LSQUARE"},
	{OP_RSQUARE, "OP_RSQUARE"},
	{OP_LPAREN, "OP_LPAREN"},
	{OP_RPAREN, "OP_RPAREN"},
	{OP_BOR, "OP_BOR"},
	{OP_XOR, "OP_XOR"},
	{OP_COMPL, "OP_COMPL"},
	{OP_AMP, "OP_AMP"},
	{OP_LNOT, "OP_LNOT"},
	{OP_SEMICOLON, "OP_SEMICOLON"},
	{OP_COLON, "OP_COLON"},
	{OP_DOTS, "OP_DOTS"},
	{OP_QMARK, "OP_QMARK"},
	{OP_COLON2, "OP_COLON2"},
	{OP_DOT, "OP_DOT"},
	{OP_DOTSTAR, "OP_DOTSTAR"},
	{OP_PLUS, "OP_PLUS"},
	{OP_MINUS, "OP_MINUS"},
	{OP_STAR, "OP_STAR"},
	{OP_DIV, "OP_DIV"},
	{OP_MOD, "OP_MOD"},
	{OP_ASS, "OP_ASS"},
	{OP_LT, "OP_LT"},
	{OP_GT, "OP_GT"},
	{OP_PLUSASS, "OP_PLUSASS"},
	{OP_MINUSASS, "OP_MINUSASS"},
	{OP_STARASS, "OP_STARASS"},
	{OP_DIVASS, "OP_DIVASS"},
	{OP_MODASS, "OP_MODASS"},
	{OP_XORASS, "OP_XORASS"},
	{OP_BANDASS, "OP_BANDASS"},
	{OP_BORASS, "OP_BORASS"},
	{OP_LSHIFT, "OP_LSHIFT"},
	{OP_RSHIFT, "OP_RSHIFT"},
	{OP_RSHIFTASS, "OP_RSHIFTASS"},
	{OP_LSHIFTASS, "OP_LSHIFTASS"},
	{OP_EQ, "OP_EQ"},
	{OP_NE, "OP_NE"},
	{OP_LE, "OP_LE"},
	{OP_GE, "OP_GE"},
	{OP_LAND, "OP_LAND"},
	{OP_LOR, "OP_LOR"},
	{OP_INC, "OP_INC"},
	{OP_DEC, "OP_DEC"},
	{OP_COMMA, "OP_COMMA"},
	{OP_ARROWSTAR, "OP_ARROWSTAR"},
	{OP_ARROW, "OP_ARROW"}
};

// convert integer [0,15] to hexadecimal digit
char ValueToHexChar(int c)
{
	switch (c)
	{
	case 0: return '0';
	case 1: return '1';
	case 2: return '2';
	case 3: return '3';
	case 4: return '4';
	case 5: return '5';
	case 6: return '6';
	case 7: return '7';
	case 8: return '8';
	case 9: return '9';
	case 10: return 'A';
	case 11: return 'B';
	case 12: return 'C';
	case 13: return 'D';
	case 14: return 'E';
	case 15: return 'F';
	default: throw logic_error("ValueToHexChar of nonhex value");
	}
}

// hex dump memory range
string HexDump(const void* pdata, size_t nbytes)
{
	unsigned char* p = (unsigned char*) pdata;

	string s(nbytes*2, '?');

	for (size_t i = 0; i < nbytes; i++)
	{
		s[2*i+0] = ValueToHexChar((p[i] & 0xF0) >> 4);
		s[2*i+1] = ValueToHexChar((p[i] & 0x0F) >> 0);
	}

	return s;
}

// DebugPostTokenOutputStream: helper class to produce PA2 output format
struct DebugPostTokenOutputStream
{
	// output: invalid <source>
	void emit_invalid(const string& source)
	{
		cout << "invalid " << source << endl;
	}

	// output: simple <source> <token_type>
	void emit_simple(const string& source, ETokenType token_type)
	{
		cout << "simple " << source << " " << TokenTypeToStringMap.at(token_type) << endl;
	}

	// output: identifier <source>
	void emit_identifier(const string& source)
	{
		cout << "identifier " << source << endl;
	}

	// output: literal <source> <type> <hexdump(data,nbytes)>
	void emit_literal(const string& source, EFundamentalType type, const void* data, size_t nbytes)
	{
		cout << "literal " << source << " " << FundamentalTypeToStringMap.at(type) << " " << HexDump(data, nbytes) << endl;
	}

	// output: literal <source> array of <num_elements> <type> <hexdump(data,nbytes)>
	void emit_literal_array(const string& source, size_t num_elements, EFundamentalType type, const void* data, size_t nbytes)
	{
		cout << "literal " << source << " array of " << num_elements << " " << FundamentalTypeToStringMap.at(type) << " " << HexDump(data, nbytes) << endl;
	}

	// output: user-defined-literal <source> <ud_suffix> character <type> <hexdump(data,nbytes)>
	void emit_user_defined_literal_character(const string& source, const string& ud_suffix, EFundamentalType type, const void* data, size_t nbytes)
	{
		cout << "user-defined-literal " << source << " " << ud_suffix << " character " << FundamentalTypeToStringMap.at(type) << " " << HexDump(data, nbytes) << endl;
	}

	// output: user-defined-literal <source> <ud_suffix> string array of <num_elements> <type> <hexdump(data, nbytes)>
	void emit_user_defined_literal_string_array(const string& source, const string& ud_suffix, size_t num_elements, EFundamentalType type, const void* data, size_t nbytes)
	{
		cout << "user-defined-literal " << source << " " << ud_suffix << " string array of " << num_elements << " " << FundamentalTypeToStringMap.at(type) << " " << HexDump(data, nbytes) << endl;
	}

	// output: user-defined-literal <source> <ud_suffix> <prefix>
	void emit_user_defined_literal_integer(const string& source, const string& ud_suffix, const string& prefix)
	{
		cout << "user-defined-literal " << source << " " << ud_suffix << " integer " << prefix << endl;
	}

	// output: user-defined-literal <source> <ud_suffix> <prefix>
	void emit_user_defined_literal_floating(const string& source, const string& ud_suffix, const string& prefix)
	{
		cout << "user-defined-literal " << source << " " << ud_suffix << " floating " << prefix << endl;
	}

	// output : eof
	void emit_eof()
	{
		cout << "eof" << endl;
	}
};


// use these 3 functions to scan `floating-literals` (see PA2)
// for example PA2Decode_float("12.34") returns "12.34" as a `float` type
float PA2Decode_float(const string& s)
{
	istringstream iss(s);
	float x;
	iss >> x;
	return x;
}

double PA2Decode_double(const string& s)
{
	istringstream iss(s);
	double x;
	iss >> x;
	return x;
}

long double PA2Decode_long_double(const string& s)
{
	istringstream iss(s);
	long double x;
	iss >> x;
	return x;
}

namespace
{
	enum class Kind
	{
		WS, NL, HEADER, IDENT, PPNUM, CHAR, UCHAR, STR, USTR, PUNC, NONWS, EOFK
	};

	struct Tok
	{
		Kind kind;
		string data;
		size_t line = 1;
	};

	struct Sink : IPPTokenStream
	{
		vector<Tok> toks;
		size_t pending_line = 1;
		Tok make_tok(Kind kind, const string& data)
		{
			Tok t;
			t.kind = kind;
			t.data = data;
			t.line = pending_line;
			return t;
		}
		void emit_line_number(size_t line) { pending_line = line; }
		void emit_whitespace_sequence() { toks.push_back(make_tok(Kind::WS, string())); }
		void emit_new_line() { toks.push_back(make_tok(Kind::NL, string())); }
		void emit_header_name(const string& data) { toks.push_back(make_tok(Kind::HEADER, data)); }
		void emit_identifier(const string& data) { toks.push_back(make_tok(Kind::IDENT, data)); }
		void emit_pp_number(const string& data) { toks.push_back(make_tok(Kind::PPNUM, data)); }
		void emit_character_literal(const string& data) { toks.push_back(make_tok(Kind::CHAR, data)); }
		void emit_user_defined_character_literal(const string& data) { toks.push_back(make_tok(Kind::UCHAR, data)); }
		void emit_string_literal(const string& data) { toks.push_back(make_tok(Kind::STR, data)); }
		void emit_user_defined_string_literal(const string& data) { toks.push_back(make_tok(Kind::USTR, data)); }
		void emit_preprocessing_op_or_punc(const string& data) { toks.push_back(make_tok(Kind::PUNC, data)); }
		void emit_non_whitespace_char(const string& data) { toks.push_back(make_tok(Kind::NONWS, data)); }
		void emit_eof() { toks.push_back(make_tok(Kind::EOFK, string())); }
	};

	enum class Enc { Ordinary, U8, U16, U32, Wide };

	struct Lit
	{
		bool ok = false;
		bool raw = false;
		bool is_char = false;
		bool has_ud = false;
		bool bad_ud_suffix = false;
		string ud;
		Enc enc = Enc::Ordinary;
		vector<uint32_t> cps;
	};

	bool is_ident_start(unsigned char c) { return c == '_' || isalpha(c); }
	bool is_ident_continue(unsigned char c) { return is_ident_start(c) || isdigit(c); }

	void err(const string& s) { cerr << "ERROR: " << s << endl; }

	string join(const vector<Tok>& toks, size_t a, size_t b)
	{
		string s;
		for (size_t i = a; i < b; ++i)
		{
			if (toks[i].data.empty()) continue;
			if (!s.empty()) s.push_back(' ');
			s += toks[i].data;
		}
		return s;
	}

	unsigned __int128 parse_u128(const string& s, int base, bool& ok)
	{
		ok = true;
		unsigned __int128 v = 0;
		for (size_t i = 0; i < s.size(); ++i)
		{
			int d = -1; unsigned char c = static_cast<unsigned char>(s[i]);
			if (c >= '0' && c <= '9') d = c - '0';
			else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
			else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
			if (d < 0 || d >= base) { ok = false; return 0; }
			v = v * static_cast<unsigned>(base) + static_cast<unsigned>(d);
		}
		return v;
	}

	bool ud_suffix_ok(const string& s)
	{
		vector<uint32_t> cps;
		try
		{
			cps = decode_utf8_bytes(vector<unsigned char>(s.begin(), s.end()));
		}
		catch (exception&)
		{
			return false;
		}
		if (cps.size() < 2 || cps[0] != '_') return false;
		for (size_t i = 1; i < cps.size(); ++i) if (!is_identifier_continue(cps[i])) return false;
		return true;
	}

	vector<unsigned char> bytes_of(const void* p, size_t n)
	{
		const unsigned char* b = static_cast<const unsigned char*>(p);
		return vector<unsigned char>(b, b + n);
	}

	vector<unsigned char> encode_utf16(const vector<uint32_t>& cps)
	{
		vector<unsigned char> out;
		for (size_t i = 0; i < cps.size(); ++i)
		{
			uint32_t cp = cps[i];
			if (cp <= 0xFFFF)
			{
				uint16_t u = static_cast<uint16_t>(cp);
				auto b = bytes_of(&u, sizeof(u));
				out.insert(out.end(), b.begin(), b.end());
			}
			else
			{
				cp -= 0x10000;
				uint16_t hi = static_cast<uint16_t>(0xD800 + ((cp >> 10) & 0x3FF));
				uint16_t lo = static_cast<uint16_t>(0xDC00 + (cp & 0x3FF));
				auto a = bytes_of(&hi, sizeof(hi));
				auto c = bytes_of(&lo, sizeof(lo));
				out.insert(out.end(), a.begin(), a.end());
				out.insert(out.end(), c.begin(), c.end());
			}
		}
		uint16_t z = 0; auto b = bytes_of(&z, sizeof(z)); out.insert(out.end(), b.begin(), b.end());
		return out;
	}

	vector<unsigned char> encode_utf32(const vector<uint32_t>& cps)
	{
		vector<unsigned char> out;
		for (size_t i = 0; i < cps.size(); ++i)
		{
			uint32_t cp = cps[i];
			auto b = bytes_of(&cp, sizeof(cp));
			out.insert(out.end(), b.begin(), b.end());
		}
		uint32_t z = 0; auto b = bytes_of(&z, sizeof(z)); out.insert(out.end(), b.begin(), b.end());
		return out;
	}

	vector<unsigned char> encode_utf8nul(const vector<uint32_t>& cps)
	{
		string s;
		for (size_t i = 0; i < cps.size(); ++i) s += utf8_encode(cps[i]);
		s.push_back('\0');
		return vector<unsigned char>(s.begin(), s.end());
	}

	bool parse_literal(const string& src, bool is_char, Lit& lit)
	{
		lit = Lit();
		size_t p = 0;
		if (src.compare(0, 2, "u8") == 0) { lit.enc = Enc::U8; p = 2; }
		else if (!src.empty() && src[0] == 'u') { lit.enc = Enc::U16; p = 1; }
		else if (!src.empty() && src[0] == 'U') { lit.enc = Enc::U32; p = 1; }
		else if (!src.empty() && src[0] == 'L') { lit.enc = Enc::Wide; p = 1; }
		if (p < src.size() && src[p] == 'R') { lit.raw = true; ++p; }
		if (p >= src.size()) return false;
		char q = src[p];
		if (q != '"' && q != '\'') return false;
		lit.is_char = (q == '\'');
		++p;
		size_t body_begin = p, body_end = string::npos;
		if (lit.raw)
		{
			size_t delim_begin = p;
			while (p < src.size() && src[p] != '(') ++p;
			if (p >= src.size()) return false;
			string delim = src.substr(delim_begin, p - delim_begin);
			if (delim.size() > 16) return false;
			++p; body_begin = p;
			while (p < src.size())
			{
				if (src[p] == ')' && p + delim.size() + 1 < src.size() && src.compare(p + 1, delim.size(), delim) == 0 && src[p + 1 + delim.size()] == '"')
				{ body_end = p; p += delim.size() + 2; break; }
				++p;
			}
			if (body_end == string::npos) return false;
		}
		else
		{
			while (p < src.size())
			{
				char c = src[p];
				if (c == '\\')
				{
					if (p + 1 >= src.size()) return false;
					char n = src[p + 1];
					if (n == 'x')
					{
						p += 2; bool any = false;
						while (p < src.size())
						{
							unsigned char d = static_cast<unsigned char>(src[p]);
							if (!((d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F'))) break;
							any = true; ++p;
						}
						if (!any) return false;
						continue;
					}
					if (n >= '0' && n <= '7')
					{
						p += 2; int count = 1;
						while (p < src.size() && count < 3 && src[p] >= '0' && src[p] <= '7') { ++p; ++count; }
						continue;
					}
					p += 2; continue;
				}
				if (c == q) { body_end = p; ++p; break; }
				++p;
			}
			if (body_end == string::npos) return false;
		}
		if (p < src.size())
		{
			string suf = src.substr(p);
			if (suf[0] != '_') { lit.bad_ud_suffix = true; return false; }
			for (size_t i = 1; i < suf.size(); ++i) if (!is_ident_continue(static_cast<unsigned char>(suf[i]))) return false;
			lit.has_ud = true; lit.ud = suf;
		}
		vector<unsigned char> body(src.begin() + body_begin, src.begin() + body_end);
		vector<uint32_t> cps = decode_utf8_bytes(body);
		if (lit.raw)
		{
			lit.cps = cps;
			lit.ok = true;
			return true;
		}
		for (size_t i = 0; i < cps.size(); ++i)
		{
			uint32_t c = cps[i];
			if (c != '\\') { lit.cps.push_back(c); continue; }
			if (i + 1 >= cps.size()) return false;
			uint32_t n = cps[i + 1];
			if (SimpleEscapeSequence_CodePoints.count(static_cast<int>(n)))
			{
				switch (n)
				{
				case '\'': lit.cps.push_back('\''); break;
				case '"': lit.cps.push_back('"'); break;
				case '?': lit.cps.push_back('?'); break;
				case '\\': lit.cps.push_back('\\'); break;
				case 'a': lit.cps.push_back(0x07); break;
				case 'b': lit.cps.push_back(0x08); break;
				case 'f': lit.cps.push_back(0x0C); break;
				case 'n': lit.cps.push_back(0x0A); break;
				case 'r': lit.cps.push_back(0x0D); break;
				case 't': lit.cps.push_back(0x09); break;
				case 'v': lit.cps.push_back(0x0B); break;
				default: return false;
				}
				++i; continue;
			}
			if (n == 'x')
			{
				size_t j = i + 2; unsigned __int128 v = 0; bool any = false;
				while (j < cps.size())
				{
					uint32_t d = cps[j]; int dig = -1;
					if (d >= '0' && d <= '9') dig = d - '0';
					else if (d >= 'a' && d <= 'f') dig = 10 + (d - 'a');
					else if (d >= 'A' && d <= 'F') dig = 10 + (d - 'A');
					else break;
					any = true; v = v * 16 + static_cast<unsigned>(dig); ++j;
				}
				if (!any) return false;
				lit.cps.push_back(static_cast<uint32_t>(v)); i = j - 1; continue;
			}
			if (n == 'u' || n == 'U')
			{
				size_t digits = (n == 'u') ? 4 : 8;
				if (i + 2 + digits > cps.size()) return false;
				unsigned __int128 v = 0;
				for (size_t j = 0; j < digits; ++j)
				{
					uint32_t d = cps[i + 2 + j];
					if (!((d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F'))) return false;
					int dig = -1;
					if (d >= '0' && d <= '9') dig = d - '0';
					else if (d >= 'a' && d <= 'f') dig = 10 + (d - 'a');
					else dig = 10 + (d - 'A');
					v = v * 16 + static_cast<unsigned>(dig);
				}
				lit.cps.push_back(static_cast<uint32_t>(v)); i += digits + 1; continue;
			}
			if (n >= '0' && n <= '7')
			{
				size_t j = i + 1; unsigned __int128 v = 0; int count = 0;
				while (j < cps.size() && cps[j] >= '0' && cps[j] <= '7' && count < 3)
				{ v = v * 8 + static_cast<unsigned>(cps[j] - '0'); ++j; ++count; }
				lit.cps.push_back(static_cast<uint32_t>(v)); i = j - 1; continue;
			}
			return false;
		}
		lit.ok = true;
		return true;
	}

	template<typename T>
	void emit_scalar(DebugPostTokenOutputStream& out, const string& src, EFundamentalType type, T value)
	{
		out.emit_literal(src, type, &value, sizeof(value));
	}

	size_t element_size(EFundamentalType type)
	{
		switch (type)
		{
		case FT_CHAR: return 1;
		case FT_CHAR16_T: return 2;
		case FT_CHAR32_T:
		case FT_WCHAR_T: return 4;
		default: return 1;
		}
	}
}

int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		string input = oss.str();

		Sink sink;
		PPTokenizer tok(sink);
		for (size_t i = 0; i < input.size(); ++i) tok.process(static_cast<unsigned char>(input[i]));
		tok.process(EndOfFile);

		DebugPostTokenOutputStream out;
		for (size_t i = 0; i < sink.toks.size(); )
		{
			Kind k = sink.toks[i].kind;
			if (k == Kind::WS || k == Kind::NL) { ++i; continue; }
			if (k == Kind::EOFK) { out.emit_eof(); break; }
			if (k == Kind::HEADER || k == Kind::NONWS) { out.emit_invalid(sink.toks[i].data); ++i; continue; }
			if (k == Kind::IDENT)
			{
				auto it = StringToTokenTypeMap.find(sink.toks[i].data);
				if (it != StringToTokenTypeMap.end()) out.emit_simple(sink.toks[i].data, it->second);
				else out.emit_identifier(sink.toks[i].data);
				++i; continue;
			}
			if (k == Kind::PUNC)
			{
				const string& s = sink.toks[i].data;
				if (s == "#" || s == "##" || s == "%:" || s == "%:%:") out.emit_invalid(s);
				else
				{
					auto it = StringToTokenTypeMap.find(s);
					if (it != StringToTokenTypeMap.end()) out.emit_simple(s, it->second);
					else out.emit_invalid(s);
				}
				++i; continue;
			}
				if (k == Kind::CHAR || k == Kind::UCHAR)
				{
					Lit lit;
					if (!parse_literal(sink.toks[i].data, true, lit))
					{
						if (lit.bad_ud_suffix) err(string("ud_suffix does not start with _: ") + sink.toks[i].data);
						out.emit_invalid(sink.toks[i].data);
						++i; continue;
					}
					if (lit.cps.size() != 1)
					{
						err(string("multi code point character literals not supported: ") + sink.toks[i].data);
						out.emit_invalid(sink.toks[i].data);
						++i; continue;
					}
					uint32_t cp = lit.cps[0];
					if (lit.has_ud)
					{
						if (lit.enc == Enc::Ordinary)
						{
							char v = static_cast<char>(cp);
							out.emit_user_defined_literal_character(sink.toks[i].data, lit.ud, FT_CHAR, &v, sizeof(v));
						}
						else if (lit.enc == Enc::U16)
						{
							if (cp > 0xFFFF) { err(string("UTF-16 char literal out of range: ") + sink.toks[i].data + " " + to_string(cp)); out.emit_invalid(sink.toks[i].data); }
							else { char16_t v = static_cast<char16_t>(cp); out.emit_user_defined_literal_character(sink.toks[i].data, lit.ud, FT_CHAR16_T, &v, sizeof(v)); }
						}
						else if (lit.enc == Enc::U32)
						{
							if (cp > 0x10FFFF) { err(string("UTF-32 char literal out of range: ") + sink.toks[i].data + " " + to_string(cp)); out.emit_invalid(sink.toks[i].data); }
							else { char32_t v = static_cast<char32_t>(cp); out.emit_user_defined_literal_character(sink.toks[i].data, lit.ud, FT_CHAR32_T, &v, sizeof(v)); }
						}
						else
						{
							if (cp > 0x10FFFF) { err(string("wchar_t char literal out of range: ") + sink.toks[i].data + " " + to_string(cp)); out.emit_invalid(sink.toks[i].data); }
							else { wchar_t v = static_cast<wchar_t>(cp); out.emit_user_defined_literal_character(sink.toks[i].data, lit.ud, FT_WCHAR_T, &v, sizeof(v)); }
						}
					}
					else if (lit.enc == Enc::Ordinary)
					{
						if (cp <= 127) { char v = static_cast<char>(cp); out.emit_literal(sink.toks[i].data, FT_CHAR, &v, sizeof(v)); }
						else { int v = static_cast<int>(cp); out.emit_literal(sink.toks[i].data, FT_INT, &v, sizeof(v)); }
					}
				else if (lit.enc == Enc::U16)
				{
					if (cp > 0xFFFF) { err(string("UTF-16 char literal out of range: ") + sink.toks[i].data + " " + to_string(cp)); out.emit_invalid(sink.toks[i].data); }
					else { char16_t v = static_cast<char16_t>(cp); out.emit_literal(sink.toks[i].data, FT_CHAR16_T, &v, sizeof(v)); }
				}
				else if (lit.enc == Enc::U32)
				{
					if (cp > 0x10FFFF) { err(string("UTF-32 char literal out of range: ") + sink.toks[i].data + " " + to_string(cp)); out.emit_invalid(sink.toks[i].data); }
					else { char32_t v = static_cast<char32_t>(cp); out.emit_literal(sink.toks[i].data, FT_CHAR32_T, &v, sizeof(v)); }
				}
				else
				{
					if (cp > 0x10FFFF) { err(string("wchar_t char literal out of range: ") + sink.toks[i].data + " " + to_string(cp)); out.emit_invalid(sink.toks[i].data); }
					else { wchar_t v = static_cast<wchar_t>(cp); out.emit_literal(sink.toks[i].data, FT_WCHAR_T, &v, sizeof(v)); }
				}
				++i; continue;
			}
			if (k == Kind::STR || k == Kind::USTR)
			{
				vector<size_t> idx;
				while (i < sink.toks.size())
				{
					if (sink.toks[i].kind == Kind::WS || sink.toks[i].kind == Kind::NL) { ++i; continue; }
					if (sink.toks[i].kind != Kind::STR && sink.toks[i].kind != Kind::USTR) break;
					idx.push_back(i); ++i;
				}
				string source = join(sink.toks, idx.front(), idx.back() + 1);
				vector<Lit> parts;
					for (size_t j = 0; j < idx.size(); ++j)
					{
						Lit lit;
						if (!parse_literal(sink.toks[idx[j]].data, false, lit))
						{
							if (lit.bad_ud_suffix) err(string("ud_suffix does not start with _: ") + sink.toks[idx[j]].data);
							out.emit_invalid(source);
							goto next_token;
						}
						parts.push_back(lit);
					}
				Enc enc = Enc::Ordinary; bool have_enc = false;
				for (size_t j = 0; j < parts.size(); ++j)
				{
					if (parts[j].enc == Enc::Ordinary) continue;
					if (!have_enc) { enc = parts[j].enc; have_enc = true; }
					else if (enc != parts[j].enc) { err("mismatched encoding prefix in string literal sequence"); out.emit_invalid(source); goto next_token; }
				}
				string ud;
				bool have_ud = false;
				for (size_t j = 0; j < parts.size(); ++j)
				{
					if (!parts[j].has_ud) continue;
					if (!have_ud) { ud = parts[j].ud; have_ud = true; }
					else if (ud != parts[j].ud) { err("mismatched ud_suffix in string literal sequence"); out.emit_invalid(source); goto next_token; }
				}
				vector<uint32_t> cps;
				for (size_t j = 0; j < parts.size(); ++j) cps.insert(cps.end(), parts[j].cps.begin(), parts[j].cps.end());
				vector<unsigned char> bytes;
				EFundamentalType type = FT_CHAR;
				try
				{
					if (enc == Enc::Ordinary || enc == Enc::U8) { type = FT_CHAR; bytes = encode_utf8nul(cps); }
					else if (enc == Enc::U16) { type = FT_CHAR16_T; bytes = encode_utf16(cps); }
					else { type = (enc == Enc::U32 ? FT_CHAR32_T : FT_WCHAR_T); bytes = encode_utf32(cps); }
				}
				catch (exception&) { out.emit_invalid(source); goto next_token; }
					size_t n = bytes.size() / element_size(type);
					if (have_ud) out.emit_user_defined_literal_string_array(source, ud, n, type, bytes.data(), bytes.size());
					else out.emit_literal_array(source, n, type, bytes.data(), bytes.size());
					continue;
				}
				if (k == Kind::PPNUM)
				{
					const string& src = sink.toks[i].data;
					size_t ud_pos = src.find('_');
					string ud_suffix;
					if (ud_pos != string::npos)
					{
						ud_suffix = src.substr(ud_pos);
						if (!ud_suffix_ok(ud_suffix)) { err(string("invalid character in ud-suffix: ") + src); out.emit_invalid(src); ++i; continue; }
					}
					string main = (ud_pos == string::npos) ? src : src.substr(0, ud_pos);
					bool is_hex = main.compare(0, 2, "0x") == 0 || main.compare(0, 2, "0X") == 0;
					bool is_oct = !is_hex && !main.empty() && main[0] == '0';
					bool is_float = is_hex ? (main.find_first_of(".pP") != string::npos) : (main.find_first_of(".eEpP") != string::npos);
					if (is_float)
					{
						size_t p = main.size();
						while (p > 0 && (main[p-1] == 'f' || main[p-1] == 'F' || main[p-1] == 'l' || main[p-1] == 'L')) --p;
						string tail = main.substr(p);
						string prefix = main.substr(0, p);
						char* end = 0;
						::strtod(prefix.c_str(), &end);
						if (end == 0 || *end != '\0') { out.emit_invalid(src); ++i; continue; }
							if (!ud_suffix.empty() && !tail.empty()) { err(string("malformed number (#2): ") + src); out.emit_invalid(src); ++i; continue; }
							if (!ud_suffix.empty())
							{
								out.emit_user_defined_literal_floating(src, ud_suffix, prefix);
							}
						else if (!tail.empty() && (tail == "f" || tail == "F"))
						{
							float v = PA2Decode_float(prefix);
							out.emit_literal(src, FT_FLOAT, &v, sizeof(v));
						}
						else if (!tail.empty() && (tail == "l" || tail == "L"))
						{
							long double v = PA2Decode_long_double(prefix);
							out.emit_literal(src, FT_LONG_DOUBLE, &v, sizeof(v));
						}
						else
						{
							double v = PA2Decode_double(prefix);
							out.emit_literal(src, FT_DOUBLE, &v, sizeof(v));
						}
						++i; continue;
					}
					size_t p = 0;
					if (is_hex) { p = 2; while (p < main.size() && isxdigit(static_cast<unsigned char>(main[p]))) ++p; }
					else if (is_oct) { p = 1; while (p < main.size() && main[p] >= '0' && main[p] <= '7') ++p; }
					else { while (p < main.size() && isdigit(static_cast<unsigned char>(main[p]))) ++p; }
						string digits;
						if (is_hex) digits = main.substr(2, p - 2);
						else if (is_oct) digits = (p > 1 ? main.substr(1, p - 1) : string("0"));
						else digits = main.substr(0, p);
					string suffix = main.substr(p);
					if (!digits.empty())
					{
						bool ok = false; unsigned __int128 v = parse_u128(digits, is_hex ? 16 : (is_oct ? 8 : 10), ok);
						if (!ok) { out.emit_invalid(src); ++i; continue; }
						if (!ud_suffix.empty() && !suffix.empty()) { err(string("malformed number (#2): ") + src); out.emit_invalid(src); ++i; continue; }
						if (!ud_suffix.empty())
						{
							out.emit_user_defined_literal_integer(src, ud_suffix, main.substr(0, p));
							++i; continue;
						}
						if (!suffix.empty())
						{
							bool valid = false;
							bool u = false, l = false, ll = false;
							int uc = 0, lc = 0;
							bool mixed_l = false;
							char first_l = 0;
							bool bad_char = false;
							for (size_t j = 0; j < suffix.size(); ++j)
							{
								char c = suffix[j];
								if (c == 'u' || c == 'U') ++uc;
								else if (c == 'l' || c == 'L')
								{
									++lc;
									if (first_l == 0) first_l = c;
									else if (c != first_l) mixed_l = true;
								}
								else bad_char = true;
							}
							if (bad_char) { out.emit_invalid(src); ++i; continue; }
							if (uc == 1 && lc == 0) { valid = u = true; }
							else if (uc == 0 && lc == 1) { valid = l = true; }
							else if (uc == 0 && lc == 2 && !mixed_l) { valid = ll = true; }
							else if (uc == 1 && lc == 1) { valid = u = l = true; }
							else if (uc == 1 && lc == 2 && !mixed_l) { valid = u = ll = true; }
							if (!valid) { out.emit_invalid(src); ++i; continue; }
							if (is_hex || is_oct)
							{
								if (v > static_cast<unsigned __int128>(ULLONG_MAX)) { err(string(is_hex ? "hex integer literal out of range (#1): " : "octal integer literal out of range (#2): ") + digits); out.emit_invalid(src); ++i; continue; }
								if (u && l) { emit_scalar(out, src, FT_UNSIGNED_LONG_INT, static_cast<unsigned long>(v)); }
								else if (u && ll) { emit_scalar(out, src, FT_UNSIGNED_LONG_LONG_INT, static_cast<unsigned long long>(v)); }
								else if (u) { if (v <= UINT_MAX) emit_scalar(out, src, FT_UNSIGNED_INT, static_cast<unsigned>(v)); else if (v <= ULONG_MAX) emit_scalar(out, src, FT_UNSIGNED_LONG_INT, static_cast<unsigned long>(v)); else emit_scalar(out, src, FT_UNSIGNED_LONG_LONG_INT, static_cast<unsigned long long>(v)); }
								else if (l) { if (v <= LONG_MAX) emit_scalar(out, src, FT_LONG_INT, static_cast<long>(v)); else if (v <= ULONG_MAX) emit_scalar(out, src, FT_UNSIGNED_LONG_INT, static_cast<unsigned long>(v)); else { err(string(is_hex ? "hex integer literal out of range (#1): " : "octal integer literal out of range (#2): ") + digits); out.emit_invalid(src); } }
								else if (ll) { if (v <= LLONG_MAX) emit_scalar(out, src, FT_LONG_LONG_INT, static_cast<long long>(v)); else emit_scalar(out, src, FT_UNSIGNED_LONG_LONG_INT, static_cast<unsigned long long>(v)); }
							}
							else
							{
								if (v > static_cast<unsigned __int128>(ULLONG_MAX)) { err(string("decimal integer literal out of range(#2): ") + digits); out.emit_invalid(src); ++i; continue; }
								if (u && l) { if (v <= ULONG_MAX) emit_scalar(out, src, FT_UNSIGNED_LONG_INT, static_cast<unsigned long>(v)); else { err(string("decimal integer literal out of range(#4): ") + digits); out.emit_invalid(src); } }
								else if (u && ll) { if (v <= ULLONG_MAX) emit_scalar(out, src, FT_UNSIGNED_LONG_LONG_INT, static_cast<unsigned long long>(v)); else { err(string("decimal integer literal out of range(#4): ") + digits); out.emit_invalid(src); } }
								else if (u) { if (v <= UINT_MAX) emit_scalar(out, src, FT_UNSIGNED_INT, static_cast<unsigned>(v)); else if (v <= ULONG_MAX) emit_scalar(out, src, FT_UNSIGNED_LONG_INT, static_cast<unsigned long>(v)); else emit_scalar(out, src, FT_UNSIGNED_LONG_LONG_INT, static_cast<unsigned long long>(v)); }
								else if (l) { if (v <= LONG_MAX) emit_scalar(out, src, FT_LONG_INT, static_cast<long>(v)); else { err(string("decimal integer literal out of range(#4): ") + digits); out.emit_invalid(src); } }
								else if (ll) { if (v <= LLONG_MAX) emit_scalar(out, src, FT_LONG_LONG_INT, static_cast<long long>(v)); else { err(string("decimal integer literal out of range(#4): ") + digits); out.emit_invalid(src); } }
							}
							++i; continue;
						}
						if (ud_suffix.empty())
						{
							if (suffix.empty())
							{
								if (is_hex)
								{
									if (v > ULLONG_MAX) { err(string("hex integer literal out of range (#1): ") + digits); out.emit_invalid(src); ++i; continue; }
									if (v <= INT_MAX) emit_scalar(out, src, FT_INT, static_cast<int>(v));
									else if (v <= UINT_MAX) emit_scalar(out, src, FT_UNSIGNED_INT, static_cast<unsigned>(v));
									else if (v <= LONG_MAX) emit_scalar(out, src, FT_LONG_INT, static_cast<long>(v));
									else if (v <= ULONG_MAX) emit_scalar(out, src, FT_UNSIGNED_LONG_INT, static_cast<unsigned long>(v));
									else if (v <= LLONG_MAX) emit_scalar(out, src, FT_LONG_LONG_INT, static_cast<long long>(v));
									else emit_scalar(out, src, FT_UNSIGNED_LONG_LONG_INT, static_cast<unsigned long long>(v));
								}
								else if (is_oct)
								{
									if (v > ULLONG_MAX) { err(string("octal integer literal out of range (#2): ") + digits); out.emit_invalid(src); ++i; continue; }
									if (v <= INT_MAX) emit_scalar(out, src, FT_INT, static_cast<int>(v));
									else if (v <= UINT_MAX) emit_scalar(out, src, FT_UNSIGNED_INT, static_cast<unsigned>(v));
									else if (v <= LONG_MAX) emit_scalar(out, src, FT_LONG_INT, static_cast<long>(v));
									else if (v <= ULONG_MAX) emit_scalar(out, src, FT_UNSIGNED_LONG_INT, static_cast<unsigned long>(v));
									else if (v <= LLONG_MAX) emit_scalar(out, src, FT_LONG_LONG_INT, static_cast<long long>(v));
									else emit_scalar(out, src, FT_UNSIGNED_LONG_LONG_INT, static_cast<unsigned long long>(v));
								}
								else
								{
									if (v <= INT_MAX) emit_scalar(out, src, FT_INT, static_cast<int>(v));
									else if (v <= LONG_MAX) emit_scalar(out, src, FT_LONG_INT, static_cast<long>(v));
									else if (v <= LLONG_MAX) emit_scalar(out, src, FT_LONG_LONG_INT, static_cast<long long>(v));
									else { err(string("decimal integer literal out of range(#4): ") + digits); out.emit_invalid(src); }
								}
								++i; continue;
							}
							out.emit_invalid(src);
							++i; continue;
						}
						out.emit_invalid(src); ++i; continue;
					}
				}
			if (k == Kind::PPNUM)
			{
				out.emit_invalid(sink.toks[i].data);
				++i;
				continue;
			}
			out.emit_invalid(sink.toks[i].data);
			++i;
		next_token:
			;
		}
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
