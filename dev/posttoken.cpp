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
#include <set>
#include <limits>

using namespace std;

#define main pptoken_internal_main
#include "pptoken.cpp"
#undef main

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

enum EPPTokenKind
{
	PPTOK_HEADER_NAME,
	PPTOK_IDENTIFIER,
	PPTOK_PP_NUMBER,
	PPTOK_CHARACTER_LITERAL,
	PPTOK_USER_DEFINED_CHARACTER_LITERAL,
	PPTOK_STRING_LITERAL,
	PPTOK_USER_DEFINED_STRING_LITERAL,
	PPTOK_PREPROCESSING_OP_OR_PUNC,
	PPTOK_NON_WHITESPACE_CHAR
};

struct PPToken
{
	EPPTokenKind kind;
	string source;
};

struct CollectPPTokenStream : IPPTokenStream
{
	vector<PPToken> tokens;

	void emit_whitespace_sequence() {}
	void emit_new_line() {}

	void emit_header_name(const string& data)
	{
		tokens.push_back({PPTOK_HEADER_NAME, data});
	}

	void emit_identifier(const string& data)
	{
		tokens.push_back({PPTOK_IDENTIFIER, data});
	}

	void emit_pp_number(const string& data)
	{
		tokens.push_back({PPTOK_PP_NUMBER, data});
	}

	void emit_character_literal(const string& data)
	{
		tokens.push_back({PPTOK_CHARACTER_LITERAL, data});
	}

	void emit_user_defined_character_literal(const string& data)
	{
		tokens.push_back({PPTOK_USER_DEFINED_CHARACTER_LITERAL, data});
	}

	void emit_string_literal(const string& data)
	{
		tokens.push_back({PPTOK_STRING_LITERAL, data});
	}

	void emit_user_defined_string_literal(const string& data)
	{
		tokens.push_back({PPTOK_USER_DEFINED_STRING_LITERAL, data});
	}

	void emit_preprocessing_op_or_punc(const string& data)
	{
		tokens.push_back({PPTOK_PREPROCESSING_OP_OR_PUNC, data});
	}

	void emit_non_whitespace_char(const string& data)
	{
		tokens.push_back({PPTOK_NON_WHITESPACE_CHAR, data});
	}

	void emit_eof() {}
};

bool IsHexDigitChar(char c)
{
	return
		(c >= '0' && c <= '9') ||
		(c >= 'a' && c <= 'f') ||
		(c >= 'A' && c <= 'F');
}

int HexDigitValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	throw logic_error("bad hex digit");
}

vector<int> UTF8Decode(const string& s)
{
	vector<int> out;
	size_t i = 0;
	while (i < s.size())
	{
		unsigned char b0 = static_cast<unsigned char>(s[i]);
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
		else
		{
			throw runtime_error("utf8 decode failure");
		}

		if (i + need >= s.size())
		{
			throw runtime_error("utf8 decode failure");
		}

		for (size_t j = 0; j < need; j++)
		{
			unsigned char bx = static_cast<unsigned char>(s[i + 1 + j]);
			if ((bx & 0xC0) != 0x80)
			{
				throw runtime_error("utf8 decode failure");
			}
			cp = (cp << 6) | (bx & 0x3F);
		}

		if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			throw runtime_error("utf8 decode failure");
		}

		out.push_back(cp);
		i += 1 + need;
	}
	return out;
}

string UTF8EncodeCodePoint(int cp)
{
	if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
	{
		throw runtime_error("utf8 encode failure");
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

string UTF8Encode(const vector<int>& cps)
{
	string out;
	for (int cp : cps)
	{
		out += UTF8EncodeCodePoint(cp);
	}
	return out;
}

bool InRanges(const vector<pair<int, int>>& ranges, int cp)
{
	int lo = 0;
	int hi = static_cast<int>(ranges.size()) - 1;
	while (lo <= hi)
	{
		int mid = lo + (hi - lo) / 2;
		if (cp < ranges[mid].first) hi = mid - 1;
		else if (cp > ranges[mid].second) lo = mid + 1;
		else return true;
	}
	return false;
}

bool IsIdentifierStartCP(int cp)
{
	bool ascii = (cp == '_') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z');
	if (ascii) return true;
	return InRanges(AnnexE1_Allowed_RangesSorted, cp) && !InRanges(AnnexE2_DisallowedInitially_RangesSorted, cp);
}

bool IsIdentifierContinueCP(int cp)
{
	if (cp >= '0' && cp <= '9') return true;
	if (IsIdentifierStartCP(cp)) return true;
	return InRanges(AnnexE1_Allowed_RangesSorted, cp);
}

bool IsIdentifierCPS(const vector<int>& cps)
{
	if (cps.empty()) return false;
	if (!IsIdentifierStartCP(cps[0])) return false;
	for (size_t i = 1; i < cps.size(); i++)
	{
		if (!IsIdentifierContinueCP(cps[i])) return false;
	}
	return true;
}

bool IsValidUDSuffix(const string& suffix)
{
	if (suffix.empty()) return false;
	vector<int> cps;
	try
	{
		cps = UTF8Decode(suffix);
	}
	catch (...)
	{
		return false;
	}
	if (!IsIdentifierCPS(cps)) return false;
	return cps[0] == '_';
}

void EmitIntegerLiteral(DebugPostTokenOutputStream& output, const string& source, EFundamentalType type, uint64_t v)
{
	switch (type)
	{
	case FT_INT: { int x = static_cast<int>(v); output.emit_literal(source, type, &x, sizeof(x)); return; }
	case FT_LONG_INT: { long x = static_cast<long>(v); output.emit_literal(source, type, &x, sizeof(x)); return; }
	case FT_LONG_LONG_INT: { long long x = static_cast<long long>(v); output.emit_literal(source, type, &x, sizeof(x)); return; }
	case FT_UNSIGNED_INT: { unsigned int x = static_cast<unsigned int>(v); output.emit_literal(source, type, &x, sizeof(x)); return; }
	case FT_UNSIGNED_LONG_INT: { unsigned long x = static_cast<unsigned long>(v); output.emit_literal(source, type, &x, sizeof(x)); return; }
	case FT_UNSIGNED_LONG_LONG_INT: { unsigned long long x = static_cast<unsigned long long>(v); output.emit_literal(source, type, &x, sizeof(x)); return; }
	default: throw logic_error("EmitIntegerLiteral bad type");
	}
}

enum EIntegerSuffixKind
{
	IS_NONE,
	IS_U,
	IS_L,
	IS_UL,
	IS_LL,
	IS_ULL
};

bool ParseIntegerSuffix(const string& s, EIntegerSuffixKind& out)
{
	if (s.empty()) { out = IS_NONE; return true; }
	if (s == "u" || s == "U") { out = IS_U; return true; }
	if (s == "l" || s == "L") { out = IS_L; return true; }
	if (s == "ul" || s == "uL" || s == "Ul" || s == "UL" ||
		s == "lu" || s == "lU" || s == "Lu" || s == "LU")
	{
		out = IS_UL;
		return true;
	}
	if (s == "ll" || s == "LL")
	{
		out = IS_LL;
		return true;
	}
	if (s == "ull" || s == "uLL" || s == "Ull" || s == "ULL" ||
		s == "llu" || s == "llU" || s == "LLu" || s == "LLU")
	{
		out = IS_ULL;
		return true;
	}
	return false;
}

bool ParseIntegerCore(const string& s, string& digits, string& rest, int& base, bool& decimal)
{
	if (s.empty() || !(s[0] >= '0' && s[0] <= '9'))
	{
		return false;
	}

	if (s.size() >= 3 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
	{
		size_t p = 2;
		while (p < s.size() && IsHexDigitChar(s[p])) p++;
		if (p > 2)
		{
			digits = s.substr(0, p);
			rest = s.substr(p);
			base = 16;
			decimal = false;
			return true;
		}
	}

	if (s[0] == '0')
	{
		size_t p = 1;
		while (p < s.size() && s[p] >= '0' && s[p] <= '7') p++;
		digits = s.substr(0, p);
		rest = s.substr(p);
		base = 8;
		decimal = false;
		return true;
	}

	size_t p = 0;
	while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
	digits = s.substr(0, p);
	rest = s.substr(p);
	base = 10;
	decimal = true;
	return true;
}

bool ParseUnsignedValue(const string& digits, int base, unsigned __int128& value, bool& fits_u64)
{
	value = 0;
	fits_u64 = true;
	size_t p = 0;
	if (base == 16 && digits.size() >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
	{
		p = 2;
	}

	for (; p < digits.size(); p++)
	{
		int d = 0;
		if (digits[p] >= '0' && digits[p] <= '9') d = digits[p] - '0';
		else if (digits[p] >= 'a' && digits[p] <= 'f') d = digits[p] - 'a' + 10;
		else if (digits[p] >= 'A' && digits[p] <= 'F') d = digits[p] - 'A' + 10;
		else return false;
		if (d >= base) return false;

		value = value * static_cast<unsigned>(base) + static_cast<unsigned>(d);
		if (value > static_cast<unsigned __int128>(numeric_limits<unsigned long long>::max()))
		{
			fits_u64 = false;
		}
	}
	return true;
}

vector<EFundamentalType> IntegerCandidates(bool decimal, EIntegerSuffixKind suffix)
{
	if (decimal)
	{
		switch (suffix)
		{
		case IS_NONE: return {FT_INT, FT_LONG_INT, FT_LONG_LONG_INT};
		case IS_U: return {FT_UNSIGNED_INT, FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		case IS_L: return {FT_LONG_INT, FT_LONG_LONG_INT};
		case IS_UL: return {FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		case IS_LL: return {FT_LONG_LONG_INT};
		case IS_ULL: return {FT_UNSIGNED_LONG_LONG_INT};
		}
	}
	else
	{
		switch (suffix)
		{
		case IS_NONE: return {FT_INT, FT_UNSIGNED_INT, FT_LONG_INT, FT_UNSIGNED_LONG_INT, FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		case IS_U: return {FT_UNSIGNED_INT, FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		case IS_L: return {FT_LONG_INT, FT_UNSIGNED_LONG_INT, FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		case IS_UL: return {FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		case IS_LL: return {FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		case IS_ULL: return {FT_UNSIGNED_LONG_LONG_INT};
		}
	}
	return {};
}

unsigned long long IntegerTypeMax(EFundamentalType t)
{
	switch (t)
	{
	case FT_INT: return static_cast<unsigned long long>(numeric_limits<int>::max());
	case FT_LONG_INT: return static_cast<unsigned long long>(numeric_limits<long>::max());
	case FT_LONG_LONG_INT: return static_cast<unsigned long long>(numeric_limits<long long>::max());
	case FT_UNSIGNED_INT: return static_cast<unsigned long long>(numeric_limits<unsigned int>::max());
	case FT_UNSIGNED_LONG_INT: return static_cast<unsigned long long>(numeric_limits<unsigned long>::max());
	case FT_UNSIGNED_LONG_LONG_INT: return static_cast<unsigned long long>(numeric_limits<unsigned long long>::max());
	default: throw logic_error("IntegerTypeMax bad type");
	}
}

bool ParseFloatingCore(const string& s, bool allow_suffix, size_t& pos, char& suffix, string& core)
{
	size_t p = 0;
	bool had_int = false;
	bool had_frac = false;
	bool had_dot = false;

	size_t int_start = p;
	while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
	had_int = (p > int_start);

	if (p < s.size() && s[p] == '.')
	{
		had_dot = true;
		p++;
		size_t frac_start = p;
		while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
		had_frac = (p > frac_start);
		if (!had_int && !had_frac) return false;
	}
	else
	{
		if (!had_int) return false;
	}

	bool has_exp = false;
	if (p < s.size() && (s[p] == 'e' || s[p] == 'E'))
	{
		has_exp = true;
		p++;
		if (p < s.size() && (s[p] == '+' || s[p] == '-')) p++;
		size_t exp_start = p;
		while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
		if (p == exp_start) return false;
	}
	else if (!had_dot)
	{
		return false;
	}

	(void)has_exp;
	suffix = 0;
	if (allow_suffix && p < s.size() && (s[p] == 'f' || s[p] == 'F' || s[p] == 'l' || s[p] == 'L'))
	{
		suffix = s[p];
		p++;
	}

	size_t core_end = suffix ? (p - 1) : p;
	core = s.substr(0, core_end);
	pos = p;
	return true;
}

int DecodeEscapedCodePoint(const vector<int>& cps, size_t& p, bool& ok)
{
	ok = true;
	if (p >= cps.size()) { ok = false; return 0; }
	int c = cps[p++];
	switch (c)
	{
	case '\'': return '\'';
	case '"': return '"';
	case '?': return '?';
	case '\\': return '\\';
	case 'a': return 0x07;
	case 'b': return 0x08;
	case 'f': return 0x0C;
	case 'n': return 0x0A;
	case 'r': return 0x0D;
	case 't': return 0x09;
	case 'v': return 0x0B;
	default: break;
	}

	if (c >= '0' && c <= '7')
	{
		int value = c - '0';
		for (int i = 0; i < 2; i++)
		{
			if (p < cps.size() && cps[p] >= '0' && cps[p] <= '7')
			{
				value = value * 8 + (cps[p] - '0');
				p++;
			}
			else
			{
				break;
			}
		}
		return value;
	}

	if (c == 'x')
	{
		if (p >= cps.size()) { ok = false; return 0; }
		if (!(cps[p] >= '0' && cps[p] <= '9') &&
			!(cps[p] >= 'a' && cps[p] <= 'f') &&
			!(cps[p] >= 'A' && cps[p] <= 'F'))
		{
			ok = false;
			return 0;
		}

		int value = 0;
		while (p < cps.size())
		{
			int x = cps[p];
			if ((x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F'))
			{
				value = value * 16 + HexDigitValue(static_cast<char>(x));
				p++;
			}
			else
			{
				break;
			}
		}
		return value;
	}

	ok = false;
	return 0;
}

bool IsValidUnicodeCodePoint(int cp)
{
	if (cp < 0 || cp > 0x10FFFF) return false;
	if (cp >= 0xD800 && cp <= 0xDFFF) return false;
	return true;
}

struct ParsedCharLiteral
{
	bool ok;
	bool is_ud;
	string ud_suffix;
	EFundamentalType type;
	uint32_t value;
};

ParsedCharLiteral ParseCharLiteralToken(const string& source, bool expect_ud)
{
	ParsedCharLiteral out = {false, false, "", FT_INT, 0};

	vector<int> cps;
	try
	{
		cps = UTF8Decode(source);
	}
	catch (...)
	{
		return out;
	}

	size_t p = 0;
	int prefix = 0;
	if (p + 1 < cps.size() && (cps[p] == 'u' || cps[p] == 'U' || cps[p] == 'L') && cps[p + 1] == '\'')
	{
		prefix = cps[p];
		p++;
	}

	if (p >= cps.size() || cps[p] != '\'')
	{
		return out;
	}
	p++;

	vector<int> chars;
	bool closed = false;
	while (p < cps.size())
	{
		int c = cps[p++];
		if (c == '\'')
		{
			closed = true;
			break;
		}
		if (c == '\\')
		{
			bool ok = true;
			int decoded = DecodeEscapedCodePoint(cps, p, ok);
			if (!ok) return out;
			chars.push_back(decoded);
		}
		else
		{
			chars.push_back(c);
		}
	}

	if (!closed) return out;
	vector<int> suffix_cps(cps.begin() + static_cast<long>(p), cps.end());
	string suffix = UTF8Encode(suffix_cps);
	bool has_ud = !suffix.empty();

	if (expect_ud)
	{
		if (!has_ud || !IsValidUDSuffix(suffix)) return out;
		out.is_ud = true;
		out.ud_suffix = suffix;
	}
	else
	{
		if (has_ud) return out;
		out.is_ud = false;
	}

	if (chars.size() != 1) return out;
	int cp = chars[0];
	if (!IsValidUnicodeCodePoint(cp)) return out;

	if (prefix == 0)
	{
		if (cp <= 127) out.type = FT_CHAR;
		else out.type = FT_INT;
	}
	else if (prefix == 'u')
	{
		if (cp > 0xFFFF) return out;
		out.type = FT_CHAR16_T;
	}
	else if (prefix == 'U')
	{
		out.type = FT_CHAR32_T;
	}
	else
	{
		out.type = FT_WCHAR_T;
	}

	out.value = static_cast<uint32_t>(cp);
	out.ok = true;
	return out;
}

enum EStringEncoding
{
	SE_ORDINARY,
	SE_U8,
	SE_U16,
	SE_U32,
	SE_WIDE
};

struct ParsedStringPiece
{
	bool ok;
	EStringEncoding encoding;
	vector<int> codepoints;
	bool has_ud;
	string ud_suffix;
};

ParsedStringPiece ParseStringPiece(const string& source, bool expect_ud)
{
	ParsedStringPiece out;
	out.ok = false;
	out.encoding = SE_ORDINARY;
	out.has_ud = false;

	vector<int> cps;
	try
	{
		cps = UTF8Decode(source);
	}
	catch (...)
	{
		return out;
	}

	size_t p = 0;
	bool raw = false;
	if (p + 3 < cps.size() && cps[p] == 'u' && cps[p + 1] == '8' && cps[p + 2] == 'R' && cps[p + 3] == '"')
	{
		out.encoding = SE_U8;
		raw = true;
		p += 4;
	}
	else if (p + 2 < cps.size() && cps[p] == 'u' && cps[p + 1] == 'R' && cps[p + 2] == '"')
	{
		out.encoding = SE_U16;
		raw = true;
		p += 3;
	}
	else if (p + 2 < cps.size() && cps[p] == 'U' && cps[p + 1] == 'R' && cps[p + 2] == '"')
	{
		out.encoding = SE_U32;
		raw = true;
		p += 3;
	}
	else if (p + 2 < cps.size() && cps[p] == 'L' && cps[p + 1] == 'R' && cps[p + 2] == '"')
	{
		out.encoding = SE_WIDE;
		raw = true;
		p += 3;
	}
	else if (p + 1 < cps.size() && cps[p] == 'R' && cps[p + 1] == '"')
	{
		out.encoding = SE_ORDINARY;
		raw = true;
		p += 2;
	}
	else if (p + 2 < cps.size() && cps[p] == 'u' && cps[p + 1] == '8' && cps[p + 2] == '"')
	{
		out.encoding = SE_U8;
		raw = false;
		p += 3;
	}
	else if (p + 1 < cps.size() && cps[p] == 'u' && cps[p + 1] == '"')
	{
		out.encoding = SE_U16;
		raw = false;
		p += 2;
	}
	else if (p + 1 < cps.size() && cps[p] == 'U' && cps[p + 1] == '"')
	{
		out.encoding = SE_U32;
		raw = false;
		p += 2;
	}
	else if (p + 1 < cps.size() && cps[p] == 'L' && cps[p + 1] == '"')
	{
		out.encoding = SE_WIDE;
		raw = false;
		p += 2;
	}
	else if (p < cps.size() && cps[p] == '"')
	{
		out.encoding = SE_ORDINARY;
		raw = false;
		p += 1;
	}
	else
	{
		return out;
	}

	if (raw)
	{
		vector<int> delim;
		while (p < cps.size() && cps[p] != '(')
		{
			delim.push_back(cps[p]);
			p++;
		}
		if (p >= cps.size() || cps[p] != '(') return out;
		p++;

		size_t content_start = p;
		size_t close_pos = static_cast<size_t>(-1);
		for (; p < cps.size(); p++)
		{
			if (cps[p] != ')') continue;

			bool match = true;
			for (size_t i = 0; i < delim.size(); i++)
			{
				if (p + 1 + i >= cps.size() || cps[p + 1 + i] != delim[i])
				{
					match = false;
					break;
				}
			}
			size_t q = p + 1 + delim.size();
			if (!match || q >= cps.size() || cps[q] != '"') continue;
			close_pos = p;
			p = q + 1;
			break;
		}
		if (close_pos == static_cast<size_t>(-1)) return out;

		out.codepoints.assign(cps.begin() + static_cast<long>(content_start), cps.begin() + static_cast<long>(close_pos));
	}
	else
	{
		vector<int> content;
		bool closed = false;
		while (p < cps.size())
		{
			int c = cps[p++];
			if (c == '"')
			{
				closed = true;
				break;
			}
			if (c == '\\')
			{
				bool ok = true;
				int decoded = DecodeEscapedCodePoint(cps, p, ok);
				if (!ok) return out;
				content.push_back(decoded);
			}
			else
			{
				content.push_back(c);
			}
		}
		if (!closed) return out;
		out.codepoints.swap(content);
	}

	vector<int> suffix_cps(cps.begin() + static_cast<long>(p), cps.end());
	if (!suffix_cps.empty())
	{
		string suffix = UTF8Encode(suffix_cps);
		if (!IsValidUDSuffix(suffix))
		{
			return out;
		}
		out.has_ud = true;
		out.ud_suffix = suffix;
	}

	if (expect_ud && !out.has_ud) return out;
	if (!expect_ud && out.has_ud) return out;

	out.ok = true;
	return out;
}

bool EncodeStringData(const vector<int>& cps, EStringEncoding enc, vector<unsigned char>& bytes, size_t& num_units, EFundamentalType& type)
{
	bytes.clear();
	num_units = 0;

	if (enc == SE_ORDINARY || enc == SE_U8)
	{
		type = FT_CHAR;
		for (int cp : cps)
		{
			if (!IsValidUnicodeCodePoint(cp)) return false;
			string x = UTF8EncodeCodePoint(cp);
			for (char c : x) bytes.push_back(static_cast<unsigned char>(c));
		}
		num_units = bytes.size();
		return true;
	}

	if (enc == SE_U16)
	{
		type = FT_CHAR16_T;
		vector<uint16_t> u16;
		for (int cp : cps)
		{
			if (!IsValidUnicodeCodePoint(cp)) return false;
			if (cp <= 0xFFFF)
			{
				u16.push_back(static_cast<uint16_t>(cp));
			}
			else
			{
				int x = cp - 0x10000;
				uint16_t hi = static_cast<uint16_t>(0xD800 + ((x >> 10) & 0x3FF));
				uint16_t lo = static_cast<uint16_t>(0xDC00 + (x & 0x3FF));
				u16.push_back(hi);
				u16.push_back(lo);
			}
		}
		num_units = u16.size();
		for (uint16_t v : u16)
		{
			bytes.push_back(static_cast<unsigned char>(v & 0xFF));
			bytes.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
		}
		return true;
	}

	type = (enc == SE_U32) ? FT_CHAR32_T : FT_WCHAR_T;
	vector<uint32_t> u32;
	for (int cp : cps)
	{
		if (!IsValidUnicodeCodePoint(cp)) return false;
		u32.push_back(static_cast<uint32_t>(cp));
	}
	num_units = u32.size();
	for (uint32_t v : u32)
	{
		bytes.push_back(static_cast<unsigned char>(v & 0xFF));
		bytes.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
		bytes.push_back(static_cast<unsigned char>((v >> 16) & 0xFF));
		bytes.push_back(static_cast<unsigned char>((v >> 24) & 0xFF));
	}
	return true;
}

void ProcessPPNumber(DebugPostTokenOutputStream& output, const string& source)
{
	string digits;
	string rest;
	int base = 10;
	bool decimal = false;

	if (ParseIntegerCore(source, digits, rest, base, decimal))
	{
		if (!rest.empty() && rest[0] == '_' && IsValidUDSuffix(rest))
		{
			output.emit_user_defined_literal_integer(source, rest, digits);
			return;
		}

		EIntegerSuffixKind suffix;
		if (ParseIntegerSuffix(rest, suffix))
		{
			unsigned __int128 value128 = 0;
			bool fits_u64 = true;
			if (!ParseUnsignedValue(digits, base, value128, fits_u64))
			{
				output.emit_invalid(source);
				return;
			}
			if (!fits_u64)
			{
				output.emit_invalid(source);
				return;
			}
			uint64_t value = static_cast<uint64_t>(value128);

			vector<EFundamentalType> candidates = IntegerCandidates(decimal, suffix);
			for (EFundamentalType t : candidates)
			{
				if (value <= IntegerTypeMax(t))
				{
					EmitIntegerLiteral(output, source, t, value);
					return;
				}
			}
		}
	}

	size_t pos = 0;
	char float_suffix = 0;
	string core;
	if (ParseFloatingCore(source, false, pos, float_suffix, core) &&
		pos < source.size() && source[pos] == '_' &&
		IsValidUDSuffix(source.substr(pos)))
	{
		output.emit_user_defined_literal_floating(source, source.substr(pos), core);
		return;
	}

	if (ParseFloatingCore(source, true, pos, float_suffix, core) && pos == source.size())
	{
		if (float_suffix == 'f' || float_suffix == 'F')
		{
			float v = PA2Decode_float(core);
			output.emit_literal(source, FT_FLOAT, &v, sizeof(v));
			return;
		}
		if (float_suffix == 'l' || float_suffix == 'L')
		{
			long double v = PA2Decode_long_double(core);
			output.emit_literal(source, FT_LONG_DOUBLE, &v, sizeof(v));
			return;
		}
		double v = PA2Decode_double(core);
		output.emit_literal(source, FT_DOUBLE, &v, sizeof(v));
		return;
	}

	output.emit_invalid(source);
}

void ProcessCharacterLiteral(DebugPostTokenOutputStream& output, const PPToken& tok)
{
	bool expect_ud = (tok.kind == PPTOK_USER_DEFINED_CHARACTER_LITERAL);
	ParsedCharLiteral parsed = ParseCharLiteralToken(tok.source, expect_ud);
	if (!parsed.ok)
	{
		output.emit_invalid(tok.source);
		return;
	}

	if (!parsed.is_ud)
	{
		switch (parsed.type)
		{
		case FT_CHAR: { char v = static_cast<char>(parsed.value); output.emit_literal(tok.source, parsed.type, &v, sizeof(v)); return; }
		case FT_INT: { int v = static_cast<int>(parsed.value); output.emit_literal(tok.source, parsed.type, &v, sizeof(v)); return; }
		case FT_CHAR16_T: { char16_t v = static_cast<char16_t>(parsed.value); output.emit_literal(tok.source, parsed.type, &v, sizeof(v)); return; }
		case FT_CHAR32_T: { char32_t v = static_cast<char32_t>(parsed.value); output.emit_literal(tok.source, parsed.type, &v, sizeof(v)); return; }
		case FT_WCHAR_T: { wchar_t v = static_cast<wchar_t>(parsed.value); output.emit_literal(tok.source, parsed.type, &v, sizeof(v)); return; }
		default: output.emit_invalid(tok.source); return;
		}
	}

	switch (parsed.type)
	{
	case FT_CHAR: { char v = static_cast<char>(parsed.value); output.emit_user_defined_literal_character(tok.source, parsed.ud_suffix, parsed.type, &v, sizeof(v)); return; }
	case FT_INT: { int v = static_cast<int>(parsed.value); output.emit_user_defined_literal_character(tok.source, parsed.ud_suffix, parsed.type, &v, sizeof(v)); return; }
	case FT_CHAR16_T: { char16_t v = static_cast<char16_t>(parsed.value); output.emit_user_defined_literal_character(tok.source, parsed.ud_suffix, parsed.type, &v, sizeof(v)); return; }
	case FT_CHAR32_T: { char32_t v = static_cast<char32_t>(parsed.value); output.emit_user_defined_literal_character(tok.source, parsed.ud_suffix, parsed.type, &v, sizeof(v)); return; }
	case FT_WCHAR_T: { wchar_t v = static_cast<wchar_t>(parsed.value); output.emit_user_defined_literal_character(tok.source, parsed.ud_suffix, parsed.type, &v, sizeof(v)); return; }
	default: output.emit_invalid(tok.source); return;
	}
}

void ProcessStringRun(DebugPostTokenOutputStream& output, const vector<PPToken>& tokens, size_t begin, size_t end)
{
	vector<ParsedStringPiece> pieces;
	string source_join;
	for (size_t i = begin; i < end; i++)
	{
		if (!source_join.empty()) source_join += " ";
		source_join += tokens[i].source;
	}

	for (size_t i = begin; i < end; i++)
	{
		bool expect_ud = (tokens[i].kind == PPTOK_USER_DEFINED_STRING_LITERAL);
		ParsedStringPiece p = ParseStringPiece(tokens[i].source, expect_ud);
		if (!p.ok)
		{
			output.emit_invalid(source_join);
			return;
		}
		pieces.push_back(p);
	}

	EStringEncoding final_enc = SE_ORDINARY;
	for (const auto& p : pieces)
	{
		if (p.encoding == SE_ORDINARY) continue;
		if (final_enc == SE_ORDINARY) final_enc = p.encoding;
		else if (final_enc != p.encoding)
		{
			output.emit_invalid(source_join);
			return;
		}
	}

	set<string> suffixes;
	for (const auto& p : pieces)
	{
		if (p.has_ud) suffixes.insert(p.ud_suffix);
	}
	if (suffixes.size() > 1)
	{
		output.emit_invalid(source_join);
		return;
	}

	vector<int> all_cps;
	for (const auto& p : pieces)
	{
		all_cps.insert(all_cps.end(), p.codepoints.begin(), p.codepoints.end());
	}
	all_cps.push_back(0);

	vector<unsigned char> bytes;
	size_t num_units = 0;
	EFundamentalType type = FT_CHAR;
	if (!EncodeStringData(all_cps, final_enc, bytes, num_units, type))
	{
		output.emit_invalid(source_join);
		return;
	}

	if (suffixes.empty())
	{
		output.emit_literal_array(source_join, num_units, type, bytes.data(), bytes.size());
	}
	else
	{
		output.emit_user_defined_literal_string_array(source_join, *suffixes.begin(), num_units, type, bytes.data(), bytes.size());
	}
}

void ProcessOneToken(DebugPostTokenOutputStream& output, const PPToken& tok)
{
	if (tok.kind == PPTOK_HEADER_NAME || tok.kind == PPTOK_NON_WHITESPACE_CHAR)
	{
		output.emit_invalid(tok.source);
		return;
	}

	if (tok.kind == PPTOK_IDENTIFIER)
	{
		auto it = StringToTokenTypeMap.find(tok.source);
		if (it != StringToTokenTypeMap.end())
		{
			output.emit_simple(tok.source, it->second);
		}
		else
		{
			output.emit_identifier(tok.source);
		}
		return;
	}

	if (tok.kind == PPTOK_PREPROCESSING_OP_OR_PUNC)
	{
		if (tok.source == "#" || tok.source == "##" || tok.source == "%:" || tok.source == "%:%:")
		{
			output.emit_invalid(tok.source);
			return;
		}

		auto it = StringToTokenTypeMap.find(tok.source);
		if (it != StringToTokenTypeMap.end())
		{
			output.emit_simple(tok.source, it->second);
		}
		else
		{
			output.emit_invalid(tok.source);
		}
		return;
	}

	if (tok.kind == PPTOK_PP_NUMBER)
	{
		ProcessPPNumber(output, tok.source);
		return;
	}

	if (tok.kind == PPTOK_CHARACTER_LITERAL || tok.kind == PPTOK_USER_DEFINED_CHARACTER_LITERAL)
	{
		ProcessCharacterLiteral(output, tok);
		return;
	}

	if (tok.kind == PPTOK_STRING_LITERAL || tok.kind == PPTOK_USER_DEFINED_STRING_LITERAL)
	{
		ProcessStringRun(output, vector<PPToken>{tok}, 0, 1);
		return;
	}

	output.emit_invalid(tok.source);
}

#ifndef CPPGM_POSTTOKEN_NO_MAIN
int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		string input = oss.str();

		CollectPPTokenStream pp_tokens;
		PPTokenizer tokenizer(pp_tokens);
		for (char c : input)
		{
			tokenizer.process(static_cast<unsigned char>(c));
		}
		tokenizer.process(EndOfFile);

		DebugPostTokenOutputStream output;
		size_t i = 0;
		while (i < pp_tokens.tokens.size())
		{
			if (pp_tokens.tokens[i].kind == PPTOK_STRING_LITERAL ||
				pp_tokens.tokens[i].kind == PPTOK_USER_DEFINED_STRING_LITERAL)
			{
				size_t j = i;
				while (j < pp_tokens.tokens.size() &&
					(pp_tokens.tokens[j].kind == PPTOK_STRING_LITERAL ||
					 pp_tokens.tokens[j].kind == PPTOK_USER_DEFINED_STRING_LITERAL))
				{
					j++;
				}
				ProcessStringRun(output, pp_tokens.tokens, i, j);
				i = j;
				continue;
			}

			ProcessOneToken(output, pp_tokens.tokens[i]);
			i++;
		}

		output.emit_eof();
		return EXIT_SUCCESS;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
