// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#ifndef CPPGM_POSTTOKEN_IMPLEMENTATION
#define CPPGM_POSTTOKEN_IMPLEMENTATION

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
#include <limits>
#include <map>

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
	PPT_WHITESPACE,
	PPT_NEWLINE,
	PPT_HEADER_NAME,
	PPT_IDENTIFIER,
	PPT_PP_NUMBER,
	PPT_CHARACTER_LITERAL,
	PPT_USER_DEFINED_CHARACTER_LITERAL,
	PPT_STRING_LITERAL,
	PPT_USER_DEFINED_STRING_LITERAL,
	PPT_PREPROCESSING_OP_OR_PUNC,
	PPT_NON_WHITESPACE_CHARACTER,
	PPT_EOF
};

struct PPToken
{
	EPPTokenKind kind;
	string source;
};

struct CollectingPPTokenStream : IPPTokenStream
{
	vector<PPToken> tokens;

	void emit_whitespace_sequence() override { tokens.push_back(PPToken{PPT_WHITESPACE, ""}); }
	void emit_new_line() override { tokens.push_back(PPToken{PPT_NEWLINE, ""}); }
	void emit_header_name(const string& data) override { tokens.push_back(PPToken{PPT_HEADER_NAME, data}); }
	void emit_identifier(const string& data) override { tokens.push_back(PPToken{PPT_IDENTIFIER, data}); }
	void emit_pp_number(const string& data) override { tokens.push_back(PPToken{PPT_PP_NUMBER, data}); }
	void emit_character_literal(const string& data) override { tokens.push_back(PPToken{PPT_CHARACTER_LITERAL, data}); }
	void emit_user_defined_character_literal(const string& data) override { tokens.push_back(PPToken{PPT_USER_DEFINED_CHARACTER_LITERAL, data}); }
	void emit_string_literal(const string& data) override { tokens.push_back(PPToken{PPT_STRING_LITERAL, data}); }
	void emit_user_defined_string_literal(const string& data) override { tokens.push_back(PPToken{PPT_USER_DEFINED_STRING_LITERAL, data}); }
	void emit_preprocessing_op_or_punc(const string& data) override { tokens.push_back(PPToken{PPT_PREPROCESSING_OP_OR_PUNC, data}); }
	void emit_non_whitespace_char(const string& data) override { tokens.push_back(PPToken{PPT_NON_WHITESPACE_CHARACTER, data}); }
	void emit_eof() override { tokens.push_back(PPToken{PPT_EOF, ""}); }
};

struct IntegerSuffix
{
	bool is_unsigned = false;
	int long_count = 0;
	string text;
};

bool IsAsciiIdentifierStart(unsigned char c)
{
	return c == '_' || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z');
}

bool IsAsciiIdentifierContinue(unsigned char c)
{
	return IsAsciiIdentifierStart(c) || ('0' <= c && c <= '9');
}

bool IsDecimalDigit(char c)
{
	return '0' <= c && c <= '9';
}

bool IsOctalDigitChar(char c)
{
	return '0' <= c && c <= '7';
}

bool IsHexDigitChar(char c)
{
	return ('0' <= c && c <= '9') ||
		('a' <= c && c <= 'f') ||
		('A' <= c && c <= 'F');
}

bool ParseIdentifierSuffix(const string& s, size_t start, string& suffix)
{
	if (start >= s.size() || !IsAsciiIdentifierStart(static_cast<unsigned char>(s[start])))
	{
		return false;
	}

	size_t pos = start + 1;
	while (pos < s.size() && IsAsciiIdentifierContinue(static_cast<unsigned char>(s[pos])))
	{
		++pos;
	}

	if (pos != s.size())
	{
		return false;
	}

	suffix = s.substr(start);
	return true;
}

bool ParseNumericUdSuffix(const string& s, size_t start, string& suffix)
{
	if (start >= s.size() || s[start] != '_')
	{
		return false;
	}

	return ParseIdentifierSuffix(s, start, suffix);
}

bool ParseIntegerSuffixExact(const string& text, IntegerSuffix& suffix)
{
	suffix = IntegerSuffix{};
	suffix.text = text;

	if (text.empty())
	{
		return true;
	}

	vector<bool> used(text.size(), false);
	size_t pos = 0;

	if (pos < text.size() && (text[pos] == 'u' || text[pos] == 'U'))
	{
		suffix.is_unsigned = true;
		used[pos] = true;
		++pos;
	}

	size_t remain = text.size() - pos;
	if (remain >= 2 &&
		((text[pos] == 'l' && text[pos + 1] == 'l') ||
		 (text[pos] == 'L' && text[pos + 1] == 'L')))
	{
		suffix.long_count = 2;
		used[pos] = used[pos + 1] = true;
		pos += 2;
	}
	else if (remain >= 1 && (text[pos] == 'l' || text[pos] == 'L'))
	{
		suffix.long_count = 1;
		used[pos] = true;
		++pos;
	}

	if (!suffix.is_unsigned && pos < text.size() && (text[pos] == 'u' || text[pos] == 'U'))
	{
		suffix.is_unsigned = true;
		used[pos] = true;
		++pos;
	}

	if (pos != text.size())
	{
		return false;
	}

	for (bool mark : used)
	{
		if (!mark)
		{
			return false;
		}
	}

	return true;
}

bool ParseFloatingSuffixExact(const string& text, char& suffix)
{
	if (text.empty())
	{
		suffix = '\0';
		return true;
	}

	if (text.size() == 1 &&
		(text[0] == 'f' || text[0] == 'F' || text[0] == 'l' || text[0] == 'L'))
	{
		suffix = text[0];
		return true;
	}

	return false;
}

bool ParseFloatingPrefix(const string& s, size_t& prefix_end)
{
	size_t pos = 0;
	bool saw_dot = false;
	bool saw_exp = false;
	bool digits_before_dot = false;
	bool digits_after_dot = false;

	while (pos < s.size() && IsDecimalDigit(s[pos]))
	{
		digits_before_dot = true;
		++pos;
	}

	if (pos < s.size() && s[pos] == '.')
	{
		saw_dot = true;
		++pos;
		while (pos < s.size() && IsDecimalDigit(s[pos]))
		{
			digits_after_dot = true;
			++pos;
		}
	}

	if (!digits_before_dot && !digits_after_dot)
	{
		return false;
	}

	if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E'))
	{
		saw_exp = true;
		++pos;
		if (pos < s.size() && (s[pos] == '+' || s[pos] == '-'))
		{
			++pos;
		}
		size_t exp_start = pos;
		while (pos < s.size() && IsDecimalDigit(s[pos]))
		{
			++pos;
		}
		if (exp_start == pos)
		{
			return false;
		}
	}

	if (!saw_dot && !saw_exp)
	{
		return false;
	}

	char float_suffix = '\0';
	if (pos < s.size() && ParseFloatingSuffixExact(string(1, s[pos]), float_suffix))
	{
		++pos;
	}

	prefix_end = pos;
	return true;
}

bool ParseIntegerCore(const string& s, size_t& digits_end, bool& is_hex, bool& is_octal)
{
	size_t pos = 0;
	is_hex = false;
	is_octal = false;

	if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
	{
		is_hex = true;
		pos = 2;
		size_t start = pos;
		while (pos < s.size() && IsHexDigitChar(s[pos]))
		{
			++pos;
		}
		if (start == pos)
		{
			return false;
		}
		digits_end = pos;
		return true;
	}

	if (s.empty() || !IsDecimalDigit(s[0]))
	{
		return false;
	}

	pos = 1;
	if (s[0] == '0')
	{
		is_octal = true;
		while (pos < s.size() && IsOctalDigitChar(s[pos]))
		{
			++pos;
		}
		if (pos < s.size() && IsDecimalDigit(s[pos]))
		{
			return false;
		}
	}
	else
	{
		while (pos < s.size() && IsDecimalDigit(s[pos]))
		{
			++pos;
		}
	}

	digits_end = pos;
	return true;
}

bool ParseIntegerLiteralParts(const string& s, string& prefix, string& ud_suffix, IntegerSuffix& suffix, bool& is_udl, bool& is_hex, bool& is_octal)
{
	size_t digits_end = 0;
	if (!ParseIntegerCore(s, digits_end, is_hex, is_octal))
	{
		return false;
	}

	prefix = s.substr(0, digits_end);
	ud_suffix.clear();
	is_udl = false;

	if (ParseNumericUdSuffix(s, digits_end, ud_suffix))
	{
		suffix = IntegerSuffix{};
		is_udl = true;
		return true;
	}

	if (!ParseIntegerSuffixExact(s.substr(digits_end), suffix))
	{
		return false;
	}

	return true;
}

bool ParseFloatingLiteralParts(const string& s, string& prefix, string& ud_suffix, char& float_suffix, bool& is_udl)
{
	size_t core_end = 0;
	if (!ParseFloatingPrefix(s, core_end))
	{
		return false;
	}

	prefix = s.substr(0, core_end);
	ud_suffix.clear();
	is_udl = false;
	float_suffix = '\0';
	if (!prefix.empty())
	{
		char maybe = prefix.back();
		if (maybe == 'f' || maybe == 'F' || maybe == 'l' || maybe == 'L')
		{
			float_suffix = maybe;
		}
	}

	if (core_end == s.size())
	{
		return true;
	}

	if (!ParseNumericUdSuffix(s, core_end, ud_suffix))
	{
		return false;
	}

	is_udl = true;
	return true;
}

bool ParseUnsignedIntegerValue(const string& digits, int base, unsigned __int128& value)
{
	value = 0;
	for (char ch : digits)
	{
		int digit = 0;
		if ('0' <= ch && ch <= '9')
		{
			digit = ch - '0';
		}
		else if ('a' <= ch && ch <= 'f')
		{
			digit = 10 + ch - 'a';
		}
		else if ('A' <= ch && ch <= 'F')
		{
			digit = 10 + ch - 'A';
		}
		else
		{
			return false;
		}

		value = value * static_cast<unsigned>(base) + static_cast<unsigned>(digit);
		if (value > static_cast<unsigned __int128>(numeric_limits<unsigned long long>::max()))
		{
			return false;
		}
	}
	return true;
}

bool FitsSigned(unsigned __int128 value, EFundamentalType type)
{
	switch (type)
	{
	case FT_INT: return value <= static_cast<unsigned __int128>(numeric_limits<int>::max());
	case FT_LONG_INT: return value <= static_cast<unsigned __int128>(numeric_limits<long>::max());
	case FT_LONG_LONG_INT: return value <= static_cast<unsigned __int128>(numeric_limits<long long>::max());
	default: return false;
	}
}

bool FitsUnsigned(unsigned __int128 value, EFundamentalType type)
{
	switch (type)
	{
	case FT_UNSIGNED_INT: return value <= static_cast<unsigned __int128>(numeric_limits<unsigned int>::max());
	case FT_UNSIGNED_LONG_INT: return value <= static_cast<unsigned __int128>(numeric_limits<unsigned long>::max());
	case FT_UNSIGNED_LONG_LONG_INT: return value <= static_cast<unsigned __int128>(numeric_limits<unsigned long long>::max());
	default: return false;
	}
}

bool EmitIntegerLiteral(DebugPostTokenOutputStream& output, const string& source)
{
	string prefix;
	string ud_suffix;
	IntegerSuffix suffix;
	bool is_udl = false;
	bool is_hex = false;
	bool is_octal = false;
	if (!ParseIntegerLiteralParts(source, prefix, ud_suffix, suffix, is_udl, is_hex, is_octal))
	{
		return false;
	}

	if (is_udl)
	{
		output.emit_user_defined_literal_integer(source, ud_suffix, prefix);
		return true;
	}

	string digits = prefix;
	int base = 10;
	if (is_hex)
	{
		digits = prefix.substr(2);
		base = 16;
	}
	else if (is_octal && prefix.size() > 1)
	{
		base = 8;
	}

	unsigned __int128 value = 0;
	if (!ParseUnsignedIntegerValue(digits, base, value))
	{
		return false;
	}

	vector<EFundamentalType> candidates;
	const bool decimal = !is_hex && !is_octal;
	if (decimal)
	{
		if (!suffix.is_unsigned && suffix.long_count == 0) candidates = {FT_INT, FT_LONG_INT, FT_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 0) candidates = {FT_UNSIGNED_INT, FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (!suffix.is_unsigned && suffix.long_count == 1) candidates = {FT_LONG_INT, FT_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 1) candidates = {FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (!suffix.is_unsigned && suffix.long_count == 2) candidates = {FT_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 2) candidates = {FT_UNSIGNED_LONG_LONG_INT};
	}
	else
	{
		if (!suffix.is_unsigned && suffix.long_count == 0) candidates = {FT_INT, FT_UNSIGNED_INT, FT_LONG_INT, FT_UNSIGNED_LONG_INT, FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 0) candidates = {FT_UNSIGNED_INT, FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (!suffix.is_unsigned && suffix.long_count == 1) candidates = {FT_LONG_INT, FT_UNSIGNED_LONG_INT, FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 1) candidates = {FT_UNSIGNED_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (!suffix.is_unsigned && suffix.long_count == 2) candidates = {FT_LONG_LONG_INT, FT_UNSIGNED_LONG_LONG_INT};
		else if (suffix.is_unsigned && suffix.long_count == 2) candidates = {FT_UNSIGNED_LONG_LONG_INT};
	}

	for (EFundamentalType candidate : candidates)
	{
		switch (candidate)
		{
		case FT_INT:
		{
			if (!FitsSigned(value, candidate)) break;
			int x = static_cast<int>(value);
			output.emit_literal(source, candidate, &x, sizeof(x));
			return true;
		}
		case FT_LONG_INT:
		{
			if (!FitsSigned(value, candidate)) break;
			long x = static_cast<long>(value);
			output.emit_literal(source, candidate, &x, sizeof(x));
			return true;
		}
		case FT_LONG_LONG_INT:
		{
			if (!FitsSigned(value, candidate)) break;
			long long x = static_cast<long long>(value);
			output.emit_literal(source, candidate, &x, sizeof(x));
			return true;
		}
		case FT_UNSIGNED_INT:
		{
			if (!FitsUnsigned(value, candidate)) break;
			unsigned int x = static_cast<unsigned int>(value);
			output.emit_literal(source, candidate, &x, sizeof(x));
			return true;
		}
		case FT_UNSIGNED_LONG_INT:
		{
			if (!FitsUnsigned(value, candidate)) break;
			unsigned long x = static_cast<unsigned long>(value);
			output.emit_literal(source, candidate, &x, sizeof(x));
			return true;
		}
		case FT_UNSIGNED_LONG_LONG_INT:
		{
			if (!FitsUnsigned(value, candidate)) break;
			unsigned long long x = static_cast<unsigned long long>(value);
			output.emit_literal(source, candidate, &x, sizeof(x));
			return true;
		}
		default:
			break;
		}
	}

	return false;
}

bool EmitFloatingLiteral(DebugPostTokenOutputStream& output, const string& source)
{
	string prefix;
	string ud_suffix;
	char suffix = '\0';
	bool is_udl = false;
	if (!ParseFloatingLiteralParts(source, prefix, ud_suffix, suffix, is_udl))
	{
		return false;
	}

	if (is_udl)
	{
		output.emit_user_defined_literal_floating(source, ud_suffix, prefix);
		return true;
	}

	if (suffix == 'f' || suffix == 'F')
	{
		float x = PA2Decode_float(prefix);
		output.emit_literal(source, FT_FLOAT, &x, sizeof(x));
		return true;
	}

	if (suffix == 'l' || suffix == 'L')
	{
		long double x = PA2Decode_long_double(prefix);
		output.emit_literal(source, FT_LONG_DOUBLE, &x, sizeof(x));
		return true;
	}

	double x = PA2Decode_double(prefix);
	output.emit_literal(source, FT_DOUBLE, &x, sizeof(x));
	return true;
}

enum EStringEncoding
{
	SE_ORDINARY,
	SE_UTF8,
	SE_UTF16,
	SE_UTF32,
	SE_WIDE
};

struct CharacterLiteralInfo
{
	bool valid = false;
	bool user_defined = false;
	string ud_suffix;
	EFundamentalType type = FT_CHAR;
	uint32_t value = 0;
	size_t nbytes = 0;
};

struct StringLiteralPiece
{
	string source;
	bool valid = false;
	bool user_defined = false;
	bool raw = false;
	string ud_suffix;
	EStringEncoding encoding = SE_ORDINARY;
	vector<int> code_points;
};

bool IsValidUnicodeCodePoint(int value)
{
	return 0 <= value && value < 0x110000 && !(0xD800 <= value && value <= 0xDFFF);
}

bool ParseLiteralUdSuffix(const string& source, size_t start, string& suffix)
{
	if (start == source.size())
	{
		suffix.clear();
		return true;
	}
	if (!ParseNumericUdSuffix(source, start, suffix))
	{
		return false;
	}
	return true;
}

bool DecodeLiteralEscape(const string& text, size_t& pos, int& value)
{
	if (text[pos] != '\\' || pos + 1 >= text.size())
	{
		return false;
	}

	char next = text[pos + 1];
	switch (next)
	{
	case '\'':
	case '"':
	case '?':
	case '\\':
		value = static_cast<unsigned char>(next);
		pos += 2;
		return true;
	case 'a':
		value = '\a';
		pos += 2;
		return true;
	case 'b':
		value = '\b';
		pos += 2;
		return true;
	case 'f':
		value = '\f';
		pos += 2;
		return true;
	case 'n':
		value = '\n';
		pos += 2;
		return true;
	case 'r':
		value = '\r';
		pos += 2;
		return true;
	case 't':
		value = '\t';
		pos += 2;
		return true;
	case 'v':
		value = '\v';
		pos += 2;
		return true;
	case 'x':
	{
		size_t at = pos + 2;
		if (at >= text.size() || !IsHexDigitChar(text[at]))
		{
			return false;
		}
		unsigned value_acc = 0;
		while (at < text.size() && IsHexDigitChar(text[at]))
		{
			value_acc = value_acc * 16 + HexCharToValue(text[at]);
			++at;
		}
		value = static_cast<int>(value_acc);
		pos = at;
		return true;
	}
	default:
		if (IsOctalDigitChar(next))
		{
			size_t at = pos + 1;
			unsigned value_acc = 0;
			int count = 0;
			while (at < text.size() && count < 3 && IsOctalDigitChar(text[at]))
			{
				value_acc = value_acc * 8 + static_cast<unsigned>(text[at] - '0');
				++at;
				++count;
			}
			value = static_cast<int>(value_acc);
			pos = at;
			return true;
		}
		return false;
	}
}

bool DecodeLiteralContent(const string& text, size_t start, size_t end, vector<int>& out)
{
	out.clear();
	size_t pos = start;
	while (pos < end)
	{
		if (text[pos] == '\\')
		{
			int value = 0;
			if (!DecodeLiteralEscape(text, pos, value))
			{
				return false;
			}
			if (!IsValidUnicodeCodePoint(value))
			{
				return false;
			}
			out.push_back(value);
			continue;
		}

		size_t next = pos;
		int value = DecodeUtf8CodePoint(text, next);
		if (!IsValidUnicodeCodePoint(value))
		{
			return false;
		}
		out.push_back(value);
		pos = next;
	}
	return true;
}

bool DecodeRawContent(const string& text, size_t start, size_t end, vector<int>& out)
{
	out.clear();
	size_t pos = start;
	while (pos < end)
	{
		size_t next = pos;
		int value = DecodeUtf8CodePoint(text, next);
		if (!IsValidUnicodeCodePoint(value))
		{
			return false;
		}
		out.push_back(value);
		pos = next;
	}
	return true;
}

bool EncodeCodePointsUtf8(const vector<int>& code_points, vector<char>& bytes)
{
	bytes.clear();
	for (int cp : code_points)
	{
		string encoded = EncodeUtf8(cp);
		bytes.insert(bytes.end(), encoded.begin(), encoded.end());
	}
	bytes.push_back(0);
	return true;
}

bool EncodeCodePointsUtf16(const vector<int>& code_points, vector<char16_t>& units)
{
	units.clear();
	for (int cp : code_points)
	{
		if (!IsValidUnicodeCodePoint(cp))
		{
			return false;
		}
		if (cp <= 0xFFFF)
		{
			units.push_back(static_cast<char16_t>(cp));
		}
		else
		{
			int adjusted = cp - 0x10000;
			units.push_back(static_cast<char16_t>(0xD800 + (adjusted >> 10)));
			units.push_back(static_cast<char16_t>(0xDC00 + (adjusted & 0x3FF)));
		}
	}
	units.push_back(0);
	return true;
}

bool EncodeCodePointsUtf32(const vector<int>& code_points, vector<char32_t>& units)
{
	units.clear();
	for (int cp : code_points)
	{
		if (!IsValidUnicodeCodePoint(cp))
		{
			return false;
		}
		units.push_back(static_cast<char32_t>(cp));
	}
	units.push_back(0);
	return true;
}

bool ParseCharacterLiteralSource(const string& source, CharacterLiteralInfo& info)
{
	info = CharacterLiteralInfo{};
	size_t pos = 0;
	EFundamentalType prefix_type = FT_CHAR;

	if (source.size() >= 2 && source[0] == 'u' && source[1] == '\'')
	{
		prefix_type = FT_CHAR16_T;
		pos = 1;
	}
	else if (source.size() >= 2 && source[0] == 'U' && source[1] == '\'')
	{
		prefix_type = FT_CHAR32_T;
		pos = 1;
	}
	else if (source.size() >= 2 && source[0] == 'L' && source[1] == '\'')
	{
		prefix_type = FT_WCHAR_T;
		pos = 1;
	}

	if (pos >= source.size() || source[pos] != '\'')
	{
		return false;
	}

	size_t content_start = pos + 1;
	size_t at = content_start;
	while (at < source.size())
	{
		if (source[at] == '\\')
		{
			if (at + 1 >= source.size())
			{
				return false;
			}
			at += 2;
			if (at < source.size() && source[at - 1] == 'x')
			{
				while (at < source.size() && IsHexDigitChar(source[at]))
				{
					++at;
				}
			}
			else if (at < source.size() && IsOctalDigitChar(source[at - 1]))
			{
				int count = 1;
				while (at < source.size() && count < 3 && IsOctalDigitChar(source[at]))
				{
					++at;
					++count;
				}
			}
			continue;
		}
		if (source[at] == '\'')
		{
			break;
		}
		size_t next = at;
		DecodeUtf8CodePoint(source, next);
		at = next;
	}

	if (at >= source.size() || source[at] != '\'')
	{
		return false;
	}

	string ud_suffix;
	if (!ParseLiteralUdSuffix(source, at + 1, ud_suffix))
	{
		return false;
	}

	vector<int> code_points;
	if (!DecodeLiteralContent(source, content_start, at, code_points) || code_points.size() != 1)
	{
		return false;
	}

	int cp = code_points[0];
	if (!IsValidUnicodeCodePoint(cp))
	{
		return false;
	}

	info.user_defined = !ud_suffix.empty();
	info.ud_suffix = ud_suffix;
	info.value = static_cast<uint32_t>(cp);

	if (prefix_type == FT_CHAR)
	{
		if (cp <= 0x7F)
		{
			info.type = FT_CHAR;
			info.nbytes = sizeof(char);
		}
		else
		{
			info.type = FT_INT;
			info.nbytes = sizeof(int);
		}
	}
	else if (prefix_type == FT_CHAR16_T)
	{
		if (cp > 0xFFFF)
		{
			return false;
		}
		info.type = FT_CHAR16_T;
		info.nbytes = sizeof(char16_t);
	}
	else
	{
		info.type = prefix_type;
		info.nbytes = sizeof(char32_t);
	}

	info.valid = true;
	return true;
}

bool EmitCharacterLiteral(DebugPostTokenOutputStream& output, const string& source)
{
	CharacterLiteralInfo info;
	if (!ParseCharacterLiteralSource(source, info))
	{
		return false;
	}

	if (info.type == FT_CHAR)
	{
		char value = static_cast<char>(info.value);
		if (info.user_defined)
		{
			output.emit_user_defined_literal_character(source, info.ud_suffix, info.type, &value, sizeof(value));
		}
		else
		{
			output.emit_literal(source, info.type, &value, sizeof(value));
		}
		return true;
	}

	if (info.type == FT_INT)
	{
		int value = static_cast<int>(info.value);
		if (info.user_defined)
		{
			output.emit_user_defined_literal_character(source, info.ud_suffix, info.type, &value, sizeof(value));
		}
		else
		{
			output.emit_literal(source, info.type, &value, sizeof(value));
		}
		return true;
	}

	if (info.type == FT_CHAR16_T)
	{
		char16_t value = static_cast<char16_t>(info.value);
		if (info.user_defined)
		{
			output.emit_user_defined_literal_character(source, info.ud_suffix, info.type, &value, sizeof(value));
		}
		else
		{
			output.emit_literal(source, info.type, &value, sizeof(value));
		}
		return true;
	}

	char32_t value = static_cast<char32_t>(info.value);
	if (info.user_defined)
	{
		output.emit_user_defined_literal_character(source, info.ud_suffix, info.type, &value, sizeof(value));
	}
	else
	{
		output.emit_literal(source, info.type, &value, sizeof(value));
	}
	return true;
}

bool ParseStringLiteralPiece(const string& source, StringLiteralPiece& piece)
{
	piece = StringLiteralPiece{};
	piece.source = source;

	size_t pos = 0;
	if (source.compare(0, 2, "u8") == 0)
	{
		piece.encoding = SE_UTF8;
		pos = 2;
	}
	else if (!source.empty() && source[0] == 'u')
	{
		piece.encoding = SE_UTF16;
		pos = 1;
	}
	else if (!source.empty() && source[0] == 'U')
	{
		piece.encoding = SE_UTF32;
		pos = 1;
	}
	else if (!source.empty() && source[0] == 'L')
	{
		piece.encoding = SE_WIDE;
		pos = 1;
	}

	if (pos < source.size() && source[pos] == 'R')
	{
		piece.raw = true;
		++pos;
	}

	if (pos >= source.size() || source[pos] != '"')
	{
		return false;
	}

	if (piece.raw)
	{
		size_t delim_start = pos + 1;
		size_t open = source.find('(', delim_start);
		if (open == string::npos)
		{
			return false;
		}
		string delimiter = source.substr(delim_start, open - delim_start);
		string terminator = ")" + delimiter + "\"";
		size_t close = source.rfind(terminator);
		if (close == string::npos || close < open + 1)
		{
			return false;
		}
		if (!ParseLiteralUdSuffix(source, close + terminator.size(), piece.ud_suffix))
		{
			return false;
		}
		if (!DecodeRawContent(source, open + 1, close, piece.code_points))
		{
			return false;
		}
	}
	else
	{
		size_t at = pos + 1;
		while (at < source.size())
		{
			if (source[at] == '\\')
			{
				if (at + 1 >= source.size())
				{
					return false;
				}
				at += 2;
				if (at < source.size() && source[at - 1] == 'x')
				{
					while (at < source.size() && IsHexDigitChar(source[at]))
					{
						++at;
					}
				}
				else if (at < source.size() && IsOctalDigitChar(source[at - 1]))
				{
					int count = 1;
					while (at < source.size() && count < 3 && IsOctalDigitChar(source[at]))
					{
						++at;
						++count;
					}
				}
				continue;
			}
			if (source[at] == '"')
			{
				break;
			}
			size_t next = at;
			DecodeUtf8CodePoint(source, next);
			at = next;
		}
		if (at >= source.size() || source[at] != '"')
		{
			return false;
		}
		if (!ParseLiteralUdSuffix(source, at + 1, piece.ud_suffix))
		{
			return false;
		}
		if (!DecodeLiteralContent(source, pos + 1, at, piece.code_points))
		{
			return false;
		}
	}

	piece.user_defined = !piece.ud_suffix.empty();
	piece.valid = true;
	return true;
}

void EmitInvalidStringSequence(DebugPostTokenOutputStream& output, const vector<PPToken>& tokens, size_t begin, size_t end)
{
	string source;
	for (size_t i = begin; i < end; ++i)
	{
		if (!source.empty())
		{
			source += ' ';
		}
		source += tokens[i].source;
	}
	output.emit_invalid(source);
}

bool EmitStringSequence(DebugPostTokenOutputStream& output, const vector<PPToken>& tokens, size_t begin, size_t end)
{
	vector<StringLiteralPiece> pieces;
	string source;
	for (size_t i = begin; i < end; ++i)
	{
		if (!source.empty())
		{
			source += ' ';
		}
		source += tokens[i].source;
	}

	for (size_t i = begin; i < end; ++i)
	{
		StringLiteralPiece piece;
		if (!ParseStringLiteralPiece(tokens[i].source, piece))
		{
			output.emit_invalid(source);
			return true;
		}
		pieces.push_back(piece);
	}

	EStringEncoding final_encoding = SE_ORDINARY;
	bool saw_prefixed = false;
	string ud_suffix;
	for (const StringLiteralPiece& piece : pieces)
	{
		if (piece.encoding != SE_ORDINARY)
		{
			if (!saw_prefixed)
			{
				final_encoding = piece.encoding;
				saw_prefixed = true;
			}
			else if (final_encoding != piece.encoding)
			{
				output.emit_invalid(source);
				return true;
			}
		}

		if (piece.user_defined)
		{
			if (ud_suffix.empty())
			{
				ud_suffix = piece.ud_suffix;
			}
			else if (ud_suffix != piece.ud_suffix)
			{
				output.emit_invalid(source);
				return true;
			}
		}
	}

	vector<int> code_points;
	for (const StringLiteralPiece& piece : pieces)
	{
		code_points.insert(code_points.end(), piece.code_points.begin(), piece.code_points.end());
	}

	if (final_encoding == SE_ORDINARY || final_encoding == SE_UTF8)
	{
		vector<char> bytes;
		if (!EncodeCodePointsUtf8(code_points, bytes))
		{
			output.emit_invalid(source);
			return true;
		}
		if (ud_suffix.empty())
		{
			output.emit_literal_array(source, bytes.size(), FT_CHAR, bytes.data(), bytes.size());
		}
		else
		{
			output.emit_user_defined_literal_string_array(source, ud_suffix, bytes.size(), FT_CHAR, bytes.data(), bytes.size());
		}
		return true;
	}

	if (final_encoding == SE_UTF16)
	{
		vector<char16_t> units;
		if (!EncodeCodePointsUtf16(code_points, units))
		{
			output.emit_invalid(source);
			return true;
		}
		if (ud_suffix.empty())
		{
			output.emit_literal_array(source, units.size(), FT_CHAR16_T, units.data(), units.size() * sizeof(char16_t));
		}
		else
		{
			output.emit_user_defined_literal_string_array(source, ud_suffix, units.size(), FT_CHAR16_T, units.data(), units.size() * sizeof(char16_t));
		}
		return true;
	}

	vector<char32_t> units;
	if (!EncodeCodePointsUtf32(code_points, units))
	{
		output.emit_invalid(source);
		return true;
	}

	EFundamentalType type = final_encoding == SE_WIDE ? FT_WCHAR_T : FT_CHAR32_T;
	if (ud_suffix.empty())
	{
		output.emit_literal_array(source, units.size(), type, units.data(), units.size() * sizeof(char32_t));
	}
	else
	{
		output.emit_user_defined_literal_string_array(source, ud_suffix, units.size(), type, units.data(), units.size() * sizeof(char32_t));
	}
	return true;
}

#ifndef CPPGM_EMBED_POSTTOKEN
int main()
{
	try
	{
		ostringstream oss;
		oss << cin.rdbuf();
		string input = oss.str();

		CollectingPPTokenStream pp_output;
		PPTokenizer tokenizer(pp_output);
		for (char c : input)
		{
			unsigned char unit = c;
			tokenizer.process(unit);
		}
		tokenizer.process(EndOfFile);

		vector<PPToken> tokens;
		for (const PPToken& token : pp_output.tokens)
		{
			if (token.kind == PPT_WHITESPACE || token.kind == PPT_NEWLINE || token.kind == PPT_EOF)
			{
				continue;
			}
			tokens.push_back(token);
		}

		DebugPostTokenOutputStream output;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			const PPToken& token = tokens[i];
			switch (token.kind)
			{
			case PPT_HEADER_NAME:
			case PPT_NON_WHITESPACE_CHARACTER:
				output.emit_invalid(token.source);
				break;
			case PPT_IDENTIFIER:
			case PPT_PREPROCESSING_OP_OR_PUNC:
			{
				auto it = StringToTokenTypeMap.find(token.source);
				if (it != StringToTokenTypeMap.end() &&
					!(token.source == "#" || token.source == "##" || token.source == "%:" || token.source == "%:%:"))
				{
					output.emit_simple(token.source, it->second);
				}
				else if (token.kind == PPT_IDENTIFIER)
				{
					output.emit_identifier(token.source);
				}
				else
				{
					output.emit_invalid(token.source);
				}
				break;
			}
			case PPT_PP_NUMBER:
				if (!EmitFloatingLiteral(output, token.source) &&
					!EmitIntegerLiteral(output, token.source))
				{
					output.emit_invalid(token.source);
				}
				break;
			case PPT_CHARACTER_LITERAL:
			case PPT_USER_DEFINED_CHARACTER_LITERAL:
				if (!EmitCharacterLiteral(output, token.source))
				{
					output.emit_invalid(token.source);
				}
				break;
			case PPT_STRING_LITERAL:
			case PPT_USER_DEFINED_STRING_LITERAL:
			{
				size_t end = i + 1;
				while (end < tokens.size() &&
					(tokens[end].kind == PPT_STRING_LITERAL ||
					 tokens[end].kind == PPT_USER_DEFINED_STRING_LITERAL))
				{
					++end;
				}
				EmitStringSequence(output, tokens, i, end);
				i = end - 1;
				break;
			}
			case PPT_WHITESPACE:
			case PPT_NEWLINE:
			case PPT_EOF:
				break;
			}
		}

		output.emit_eof();
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}

#endif
#endif
