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
#include <vector>
#include <cstdlib>

using namespace std;

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

float PA2Decode_float(const string& s);
double PA2Decode_double(const string& s);
long double PA2Decode_long_double(const string& s);

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
	case 'a': case 'A': return 10;
	case 'b': case 'B': return 11;
	case 'c': case 'C': return 12;
	case 'd': case 'D': return 13;
	case 'e': case 'E': return 14;
	case 'f': case 'F': return 15;
	default: throw logic_error("HexCharToValue of nonhex value");
	}
}

string EncodeUTF8(int cp)
{
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
		if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		{
			throw runtime_error("utf8 invalid code point");
		}
		out.push_back(cp);
		i += need + 1;
	}
	return out;
}

bool IsHexDigit(int c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool IsOctDigit(int c)
{
	return c >= '0' && c <= '7';
}

bool IsIdStart(int c)
{
	return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80;
}

bool IsIdContinue(int c)
{
	return IsIdStart(c) || (c >= '0' && c <= '9');
}

bool IsWSNoNL(int c)
{
	return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

struct PPCursor
{
	const vector<int>& cps;
	size_t pos;

	explicit PPCursor(const vector<int>& cps) : cps(cps), pos(0) {}

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

size_t DecodeUCNAt(const vector<int>& cps, size_t pos, int& cp, bool synthetic_backslash = false)
{
	size_t start = pos + (synthetic_backslash ? 0 : 1);
	if (!synthetic_backslash && (pos >= cps.size() || cps[pos] != '\\'))
	{
		return 0;
	}
	if (start >= cps.size() || (cps[start] != 'u' && cps[start] != 'U'))
	{
		return 0;
	}
	size_t digits = cps[start] == 'u' ? 4u : 8u;
	if (start + 1 + digits > cps.size())
	{
		throw runtime_error("incomplete universal-character-name");
	}
	cp = 0;
	for (size_t i = 0; i < digits; ++i)
	{
		int c = cps[start + 1 + i];
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
	return (synthetic_backslash ? 0 : 1) + 1 + digits;
}

bool MatchRawPrefix(const vector<int>& cps, size_t pos, size_t& prefix_len)
{
	prefix_len = 0;
	if (pos + 3 < cps.size() && cps[pos] == 'u' && cps[pos + 1] == '8' && cps[pos + 2] == 'R' && cps[pos + 3] == '"')
	{
		prefix_len = 3;
		return true;
	}
	if (pos + 2 < cps.size() && (cps[pos] == 'u' || cps[pos] == 'U' || cps[pos] == 'L') && cps[pos + 1] == 'R' && cps[pos + 2] == '"')
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

size_t FindRawEnd(const vector<int>& cps, size_t pos)
{
	size_t prefix_len = 0;
	if (!MatchRawPrefix(cps, pos, prefix_len))
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
		if (cps[i] == ' ' || cps[i] == '\\' || cps[i] == ')' || cps[i] == '\t' || cps[i] == '\v' || cps[i] == '\f')
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
				if (i + 1 + j >= cps.size() || cps[i + 1 + j] != static_cast<unsigned char>(delim[j]))
				{
					match = false;
					break;
				}
			}
			if (match && i + 1 + delim.size() < cps.size() && cps[i + 1 + delim.size()] == '"')
			{
				return i + 2 + delim.size();
			}
		}
		++i;
	}
	throw runtime_error("unterminated raw string literal");
}

vector<int> TransformSource(const vector<int>& cps)
{
	vector<int> out;
	for (size_t i = 0; i < cps.size(); )
	{
		size_t raw_end = i;
		if (i == 0 || (!IsIdContinue(cps[i - 1]) && cps[i - 1] != '"' && cps[i - 1] != '\''))
		{
			raw_end = FindRawEnd(cps, i);
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
			int cp = 0;
			size_t ucn_len = consumed == 1 ? DecodeUCNAt(cps, i, cp, false) : DecodeUCNAt(cps, i + consumed, cp, true);
			if (ucn_len != 0)
			{
				out.push_back(cp);
				i += consumed == 1 ? ucn_len : consumed + ucn_len;
				continue;
			}
		}
		out.push_back(c);
		i += consumed;
	}
	return out;
}

enum PPTokenKind
{
	PP_WS,
	PP_NL,
	PP_HEADER,
	PP_IDENTIFIER,
	PP_NUMBER,
	PP_CHAR,
	PP_UD_CHAR,
	PP_STRING,
	PP_UD_STRING,
	PP_OP,
	PP_NONWS
};

struct PPToken
{
	PPTokenKind kind;
	string data;
};

struct PPCollector
{
	vector<PPToken> tokens;

	void emit_whitespace_sequence() { tokens.push_back({PP_WS, ""}); }
	void emit_new_line() { tokens.push_back({PP_NL, ""}); }
	void emit_header_name(const string& s) { tokens.push_back({PP_HEADER, s}); }
	void emit_identifier(const string& s) { tokens.push_back({PP_IDENTIFIER, s}); }
	void emit_pp_number(const string& s) { tokens.push_back({PP_NUMBER, s}); }
	void emit_character_literal(const string& s) { tokens.push_back({PP_CHAR, s}); }
	void emit_user_defined_character_literal(const string& s) { tokens.push_back({PP_UD_CHAR, s}); }
	void emit_string_literal(const string& s) { tokens.push_back({PP_STRING, s}); }
	void emit_user_defined_string_literal(const string& s) { tokens.push_back({PP_UD_STRING, s}); }
	void emit_preprocessing_op_or_punc(const string& s) { tokens.push_back({PP_OP, s}); }
	void emit_non_whitespace_char(const string& s) { tokens.push_back({PP_NONWS, s}); }
	void emit_eof() {}
};

struct PPTokenizer
{
	PPCollector& output;
	explicit PPTokenizer(PPCollector& o) : output(o) {}

	bool StartsComment(const PPCursor& cur) const
	{
		return (cur.peek() == '/' && cur.peek(1) == '/') || (cur.peek() == '/' && cur.peek(1) == '*');
	}

	void ConsumeWhitespace(PPCursor& cur)
	{
		while (true)
		{
			while (IsWSNoNL(cur.peek()))
			{
				cur.get();
			}
			if (cur.peek() == '/' && cur.peek(1) == '/')
			{
				cur.get();
				cur.get();
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
	}

	string ParseIdentifier(PPCursor& cur)
	{
		size_t begin = cur.pos;
		cur.get();
		while (IsIdContinue(cur.peek()))
		{
			cur.get();
		}
		return EncodeRange(cur.cps, begin, cur.pos);
	}

	bool TryPPNumber(PPCursor& cur)
	{
		if (!(cur.peek() >= '0' && cur.peek() <= '9') && !(cur.peek() == '.' && cur.peek(1) >= '0' && cur.peek(1) <= '9'))
		{
			return false;
		}
		size_t begin = cur.pos;
		cur.get();
		while (true)
		{
			int c = cur.peek();
			if ((c >= '0' && c <= '9') || c == '.' || IsIdContinue(c))
			{
				cur.get();
				continue;
			}
			int prev = cur.pos > begin ? cur.cps[cur.pos - 1] : EndOfFile;
			if ((prev == 'e' || prev == 'E' || prev == 'p' || prev == 'P') && (c == '+' || c == '-'))
			{
				cur.get();
				continue;
			}
			break;
		}
		output.emit_pp_number(EncodeRange(cur.cps, begin, cur.pos));
		return true;
	}

	void ParseNormalLiteral(PPCursor& cur, int quote, size_t prefix_len)
	{
		for (size_t i = 0; i < prefix_len; ++i) cur.get();
		cur.get();
		while (true)
		{
			int c = cur.peek();
			if (c == EndOfFile || c == '\n')
			{
				throw runtime_error(quote == '"' ? "unterminated string literal" : "unterminated character literal");
			}
			if (c == quote)
			{
				cur.get();
				return;
			}
			if (c == '\\')
			{
				cur.get();
				if (cur.peek() == EndOfFile || cur.peek() == '\n')
				{
					throw runtime_error(quote == '"' ? "unterminated string literal" : "unterminated character literal");
				}
				cur.get();
				while (IsHexDigit(cur.peek()) || IsOctDigit(cur.peek()))
				{
					cur.get();
				}
				continue;
			}
			cur.get();
		}
	}

	void ParseRawLiteral(PPCursor& cur, size_t prefix_len)
	{
		for (size_t i = 0; i < prefix_len; ++i) cur.get();
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
			delim.push_back(static_cast<char>(c));
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
			if (c == ')')
			{
				bool match = true;
				for (size_t i = 0; i < delim.size(); ++i)
				{
					if (cur.peek(1 + i) != static_cast<unsigned char>(delim[i]))
					{
						match = false;
						break;
					}
				}
				if (match && cur.peek(1 + delim.size()) == '"')
				{
					cur.get();
					for (size_t i = 0; i < delim.size(); ++i) cur.get();
					cur.get();
					return;
				}
			}
			cur.get();
		}
	}

	bool TryLiteral(PPCursor& cur)
	{
		size_t prefix_len = 0;
		bool raw = false;
		if (cur.starts_with("u8R\"")) { prefix_len = 3; raw = true; }
		else if (cur.starts_with("uR\"") || cur.starts_with("UR\"") || cur.starts_with("LR\"")) { prefix_len = 2; raw = true; }
		else if (cur.starts_with("R\"")) { prefix_len = 1; raw = true; }
		else if (cur.starts_with("u8\"")) { prefix_len = 2; }
		else if ((cur.peek() == 'u' || cur.peek() == 'U' || cur.peek() == 'L') && (cur.peek(1) == '"' || cur.peek(1) == '\'')) { prefix_len = 1; }
		else if (cur.peek() != '"' && cur.peek() != '\'') { return false; }

		int quote = cur.peek(prefix_len);
		if (quote != '"' && quote != '\'')
		{
			return false;
		}
		size_t begin = cur.pos;
		if (raw) ParseRawLiteral(cur, prefix_len);
		else ParseNormalLiteral(cur, quote, prefix_len);
		size_t lit_end = cur.pos;
		while (IsIdContinue(cur.peek()))
		{
			cur.get();
		}
		string data = EncodeRange(cur.cps, begin, cur.pos);
		if (quote == '"')
		{
			if (cur.pos != lit_end) output.emit_user_defined_string_literal(data);
			else output.emit_string_literal(data);
		}
		else
		{
			if (cur.pos != lit_end) output.emit_user_defined_character_literal(data);
			else output.emit_character_literal(data);
		}
		return true;
	}

	string ParseOp(PPCursor& cur)
	{
		if (cur.peek() == '<' && cur.peek(1) == ':' && cur.peek(2) == ':' && cur.peek(3) != ':' && cur.peek(3) != '>')
		{
			cur.get();
			return "<";
		}
		static const vector<string> ops = {"%:%:","<<=",">>=","...","->*","->",".*","++","--","<<",">>","<=",">=","==","!=","&&","||","+=","-=","*=","/=","%=","^=","&=","|=","::","##","<:",":>","<%","%>","%:","{","}","[","]","#","(",")",";",":","?",".","+","-","*","/","%","^","&","|","~","!","=","<",">",","};
		for (size_t i = 0; i < ops.size(); ++i)
		{
			if (cur.starts_with(ops[i]))
			{
				for (size_t j = 0; j < ops[i].size(); ++j) cur.get();
				return ops[i];
			}
		}
		return "";
	}

	vector<PPToken> tokenize(const vector<int>& cps)
	{
		PPCursor cur(cps);
		while (cur.peek() != EndOfFile)
		{
			if (cur.peek() == '\n')
			{
				cur.get();
				output.emit_new_line();
			}
			else if (IsWSNoNL(cur.peek()) || StartsComment(cur))
			{
				ConsumeWhitespace(cur);
				output.emit_whitespace_sequence();
			}
			else if (TryLiteral(cur))
			{
			}
			else if (TryPPNumber(cur))
			{
			}
			else if (IsIdStart(cur.peek()))
			{
				output.emit_identifier(ParseIdentifier(cur));
			}
			else
			{
				string op = ParseOp(cur);
				if (!op.empty()) output.emit_preprocessing_op_or_punc(op);
				else output.emit_non_whitespace_char(EncodeUTF8(cur.get()));
			}
		}
		return output.tokens;
	}
};

struct LiteralValue
{
	EFundamentalType type;
	vector<unsigned char> bytes;
	size_t array_elements;
	bool is_array;

	LiteralValue() : type(FT_INT), array_elements(0), is_array(false) {}
};

template<typename T>
void AppendPOD(vector<unsigned char>& out, T value)
{
	const unsigned char* p = reinterpret_cast<const unsigned char*>(&value);
	out.insert(out.end(), p, p + sizeof(T));
}

void AppendCodePoint(vector<unsigned char>& out, EFundamentalType type, int cp)
{
	if (type == FT_CHAR)
	{
		string utf8 = EncodeUTF8(cp);
		out.insert(out.end(), utf8.begin(), utf8.end());
	}
	else if (type == FT_CHAR16_T)
	{
		if (cp <= 0xFFFF)
		{
			char16_t v = static_cast<char16_t>(cp);
			AppendPOD(out, v);
		}
		else
		{
			cp -= 0x10000;
			char16_t hi = static_cast<char16_t>(0xD800 + (cp >> 10));
			char16_t lo = static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
			AppendPOD(out, hi);
			AppendPOD(out, lo);
		}
	}
	else
	{
		uint32_t v = static_cast<uint32_t>(cp);
		AppendPOD(out, v);
	}
}

bool DecodeEscape(const vector<int>& cps, size_t& i, int& cp)
{
	if (cps[i] != '\\')
	{
		cp = cps[i++];
		return true;
	}
	++i;
	if (i >= cps.size()) return false;
	int c = cps[i++];
	switch (c)
	{
	case '\'': cp = '\''; return true;
	case '"': cp = '"'; return true;
	case '?': cp = '?'; return true;
	case '\\': cp = '\\'; return true;
	case 'a': cp = '\a'; return true;
	case 'b': cp = '\b'; return true;
	case 'f': cp = '\f'; return true;
	case 'n': cp = '\n'; return true;
	case 'r': cp = '\r'; return true;
	case 't': cp = '\t'; return true;
	case 'v': cp = '\v'; return true;
	case 'x':
		cp = 0;
		while (i < cps.size() && IsHexDigit(cps[i]))
		{
			cp = (cp << 4) | HexCharToValue(cps[i]);
			++i;
		}
		return true;
	default:
		if (c >= '0' && c <= '7')
		{
			cp = c - '0';
			for (int n = 0; n < 2 && i < cps.size() && IsOctDigit(cps[i]); ++n)
			{
				cp = (cp << 3) | (cps[i] - '0');
				++i;
			}
			return true;
		}
		return false;
	}
}

string StripUDSuffix(const string& source, string& suffix)
{
	size_t pos = source.find_last_of('_');
	if (pos == string::npos)
	{
		suffix.clear();
		return source;
	}
	suffix = source.substr(pos);
	return source.substr(0, pos);
}

bool IsValidUDSuffix(const string& s)
{
	if (s.empty() || s[0] != '_')
	{
		return false;
	}
	vector<int> cps = DecodeUTF8(s);
	if (cps.empty() || cps[0] != '_')
	{
		return false;
	}
	for (size_t i = 1; i < cps.size(); ++i)
	{
		if (!IsIdContinue(cps[i]))
		{
			return false;
		}
	}
	return true;
}

bool ParseIntegerLiteral(const string& source, DebugPostTokenOutputStream& output)
{
	size_t ud_pos = source.find('_');
	string core = ud_pos == string::npos ? source : source.substr(0, ud_pos);
	string suffix = ud_pos == string::npos ? "" : source.substr(ud_pos);

	size_t pos = 0;
	int base = 10;
	if (core.size() >= 2 && core[0] == '0' && (core[1] == 'x' || core[1] == 'X'))
	{
		base = 16;
		pos = 2;
	}
	else if (core.size() > 1 && core[0] == '0' && core[1] >= '0' && core[1] <= '7')
	{
		base = 8;
		pos = 1;
	}

	size_t digit_end = pos;
	while (digit_end < core.size())
	{
		int c = core[digit_end];
		bool ok = base == 16 ? IsHexDigit(c) : (base == 8 ? IsOctDigit(c) : (c >= '0' && c <= '9'));
		if (!ok) break;
		++digit_end;
	}
	if (digit_end == pos) return false;
	string digits = core.substr(0, digit_end);
	string std_suffix = core.substr(digit_end);

	auto emit_ud = [&](const string& ud) {
		output.emit_user_defined_literal_integer(source, ud, digits);
	};

	if (!suffix.empty())
	{
		if (!IsValidUDSuffix(suffix)) return false;
		if (!std_suffix.empty()) return false;
		emit_ud(suffix);
		return true;
	}

	struct SuffixInfo
	{
		bool valid;
		bool is_unsigned;
		int long_count;
	};
	SuffixInfo info = {true, false, 0};
	if (std_suffix == "") {}
	else if (std_suffix == "u" || std_suffix == "U") info.is_unsigned = true;
	else if (std_suffix == "l" || std_suffix == "L") info.long_count = 1;
	else if (std_suffix == "ll" || std_suffix == "LL") info.long_count = 2;
	else if (std_suffix == "ul" || std_suffix == "uL" || std_suffix == "Ul" || std_suffix == "UL" ||
		std_suffix == "lu" || std_suffix == "lU" || std_suffix == "Lu" || std_suffix == "LU")
	{
		info.is_unsigned = true; info.long_count = 1;
	}
	else if (std_suffix == "ull" || std_suffix == "uLL" || std_suffix == "Ull" || std_suffix == "ULL" ||
		std_suffix == "llu" || std_suffix == "llU" || std_suffix == "LLu" || std_suffix == "LLU")
	{
		info.is_unsigned = true; info.long_count = 2;
	}
	else info.valid = false;
	if (!info.valid) return false;

	unsigned __int128 value = 0;
	for (size_t i = pos; i < digit_end; ++i)
	{
		int d = base == 16 ? HexCharToValue(core[i]) : core[i] - '0';
		value = value * base + d;
	}

	struct Candidate { EFundamentalType type; unsigned __int128 max; };
	vector<Candidate> candidates;
	const unsigned __int128 I_MAX = 0x7FFFFFFFu;
	const unsigned __int128 UI_MAX = 0xFFFFFFFFu;
	const unsigned __int128 L_MAX = 0x7FFFFFFFFFFFFFFFull;
	const unsigned __int128 UL_MAX = 0xFFFFFFFFFFFFFFFFull;
	if (!info.is_unsigned && info.long_count == 0)
	{
		candidates.push_back({FT_INT, I_MAX});
		if (base != 10) candidates.push_back({FT_UNSIGNED_INT, UI_MAX});
		candidates.push_back({FT_LONG_INT, L_MAX});
		if (base != 10) candidates.push_back({FT_UNSIGNED_LONG_INT, UL_MAX});
		candidates.push_back({FT_LONG_LONG_INT, L_MAX});
		if (base != 10) candidates.push_back({FT_UNSIGNED_LONG_LONG_INT, UL_MAX});
	}
	else if (!info.is_unsigned && info.long_count == 1)
	{
		candidates.push_back({FT_LONG_INT, L_MAX});
		if (base != 10) candidates.push_back({FT_UNSIGNED_LONG_INT, UL_MAX});
		candidates.push_back({FT_LONG_LONG_INT, L_MAX});
		if (base != 10) candidates.push_back({FT_UNSIGNED_LONG_LONG_INT, UL_MAX});
	}
	else if (!info.is_unsigned && info.long_count == 2)
	{
		candidates.push_back({FT_LONG_LONG_INT, L_MAX});
		if (base != 10) candidates.push_back({FT_UNSIGNED_LONG_LONG_INT, UL_MAX});
	}
	else if (info.is_unsigned && info.long_count == 0)
	{
		candidates.push_back({FT_UNSIGNED_INT, UI_MAX});
		candidates.push_back({FT_UNSIGNED_LONG_INT, UL_MAX});
		candidates.push_back({FT_UNSIGNED_LONG_LONG_INT, UL_MAX});
	}
	else if (info.is_unsigned && info.long_count == 1)
	{
		candidates.push_back({FT_UNSIGNED_LONG_INT, UL_MAX});
		candidates.push_back({FT_UNSIGNED_LONG_LONG_INT, UL_MAX});
	}
	else
	{
		candidates.push_back({FT_UNSIGNED_LONG_LONG_INT, UL_MAX});
	}

	for (size_t i = 0; i < candidates.size(); ++i)
	{
		if (value <= candidates[i].max)
		{
			switch (candidates[i].type)
			{
			case FT_INT: { int v = static_cast<int>(value); output.emit_literal(source, FT_INT, &v, sizeof(v)); return true; }
			case FT_UNSIGNED_INT: { unsigned int v = static_cast<unsigned int>(value); output.emit_literal(source, FT_UNSIGNED_INT, &v, sizeof(v)); return true; }
			case FT_LONG_INT: { long v = static_cast<long>(value); output.emit_literal(source, FT_LONG_INT, &v, sizeof(v)); return true; }
			case FT_UNSIGNED_LONG_INT: { unsigned long v = static_cast<unsigned long>(value); output.emit_literal(source, FT_UNSIGNED_LONG_INT, &v, sizeof(v)); return true; }
			case FT_LONG_LONG_INT: { long long v = static_cast<long long>(value); output.emit_literal(source, FT_LONG_LONG_INT, &v, sizeof(v)); return true; }
			case FT_UNSIGNED_LONG_LONG_INT: { unsigned long long v = static_cast<unsigned long long>(value); output.emit_literal(source, FT_UNSIGNED_LONG_LONG_INT, &v, sizeof(v)); return true; }
			default: break;
			}
		}
	}
	return false;
}

bool ParseFloatingLiteral(const string& source, DebugPostTokenOutputStream& output)
{
	if (source.size() >= 2 && source[0] == '0' && (source[1] == 'x' || source[1] == 'X'))
	{
		return false;
	}
	if (source.find('.') == string::npos && source.find('e') == string::npos && source.find('E') == string::npos)
	{
		return false;
	}
	auto fully_consumes = [](const string& text) {
		char* end = nullptr;
		strtold(text.c_str(), &end);
		return end != text.c_str() && end != nullptr && *end == '\0';
	};
	string s = source;
	size_t underscore = s.find('_');
	if (underscore != string::npos)
	{
		string prefix = s.substr(0, underscore);
		string suffix = s.substr(underscore);
		if (!IsValidUDSuffix(suffix))
		{
			return false;
		}
		if (prefix.empty() || prefix.back() == '+' || prefix.back() == '-' || !fully_consumes(prefix)) return false;
		output.emit_user_defined_literal_floating(source, suffix, prefix);
		return true;
	}
	if (s.back() == '+' || s.back() == '-') return false;
	char suffix = 0;
	if (!s.empty() && (s.back() == 'f' || s.back() == 'F' || s.back() == 'l' || s.back() == 'L'))
	{
		suffix = s.back();
		s.pop_back();
	}
	if (s.empty() || s.back() == '+' || s.back() == '-' || !fully_consumes(s)) return false;
	if (suffix == 'f' || suffix == 'F')
	{
		float v = PA2Decode_float(s);
		output.emit_literal(source, FT_FLOAT, &v, sizeof(v));
	}
	else if (suffix == 'l' || suffix == 'L')
	{
		long double v = PA2Decode_long_double(s);
		output.emit_literal(source, FT_LONG_DOUBLE, &v, sizeof(v));
	}
	else
	{
		double v = PA2Decode_double(s);
		output.emit_literal(source, FT_DOUBLE, &v, sizeof(v));
	}
	return true;
}

bool ParseCharacterLiteralCore(const string& source, EFundamentalType& type, vector<unsigned char>& bytes)
{
	size_t prefix_len = 0;
	if (!source.empty() && (source[0] == 'u' || source[0] == 'U' || source[0] == 'L')) prefix_len = 1;
	if (source.size() < prefix_len + 2 || source[prefix_len] != '\'' || source.back() != '\'') return false;
	vector<int> cps = DecodeUTF8(source.substr(prefix_len + 1, source.size() - prefix_len - 2));
	size_t i = 0;
	vector<int> vals;
	while (i < cps.size())
	{
		int cp = 0;
		if (!DecodeEscape(cps, i, cp)) return false;
		vals.push_back(cp);
	}
	if (vals.size() != 1) return false;
	int cp = vals[0];
	type = FT_CHAR;
	bytes.clear();
	if (prefix_len == 0)
	{
		if (cp <= 0x7F) { char v = static_cast<char>(cp); AppendPOD(bytes, v); type = FT_CHAR; }
		else { int v = cp; AppendPOD(bytes, v); type = FT_INT; }
		return true;
	}
	if (source[0] == 'u')
	{
		if (cp > 0xFFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
		char16_t v = static_cast<char16_t>(cp);
		AppendPOD(bytes, v);
		type = FT_CHAR16_T;
		return true;
	}
	uint32_t v = static_cast<uint32_t>(cp);
	AppendPOD(bytes, v);
	type = source[0] == 'U' ? FT_CHAR32_T : FT_WCHAR_T;
	return true;
}

bool ExtractStringCodePoints(const string& source, vector<int>& vals)
{
	size_t prefix_len = 0;
	if (source.rfind("u8", 0) == 0) prefix_len = 2;
	else if (!source.empty() && (source[0] == 'u' || source[0] == 'U' || source[0] == 'L')) prefix_len = 1;
	bool raw = false;
	if (source.compare(prefix_len, 1, "R") == 0) raw = true;
	size_t quote_pos = prefix_len + (raw ? 1 : 0);
	if (quote_pos >= source.size() || source[quote_pos] != '"') return false;

	vals.clear();
	if (!raw)
	{
		vector<int> cps = DecodeUTF8(source.substr(prefix_len + 1, source.size() - prefix_len - 2));
		size_t i = 0;
		while (i < cps.size())
		{
			int cp = 0;
			if (!DecodeEscape(cps, i, cp)) return false;
			vals.push_back(cp);
		}
	}
	else
	{
		size_t open = source.find('(', quote_pos + 1);
		size_t close = source.rfind(')');
		if (open == string::npos || close == string::npos || close < open) return false;
		vals = DecodeUTF8(source.substr(open + 1, close - open - 1));
	}
	return true;
}

EFundamentalType StringLiteralTypeOf(const string& source)
{
	if (source.rfind("u8", 0) == 0) return FT_CHAR;
	if (source.rfind("u", 0) == 0) return FT_CHAR16_T;
	if (source.rfind("U", 0) == 0) return FT_CHAR32_T;
	if (source.rfind("L", 0) == 0) return FT_WCHAR_T;
	return FT_CHAR;
}

bool ParseStringLiteralCore(const string& source, EFundamentalType& type, vector<unsigned char>& bytes, size_t& elements)
{
	vector<int> vals;
	if (!ExtractStringCodePoints(source, vals))
	{
		return false;
	}
	type = StringLiteralTypeOf(source);

	bytes.clear();
	elements = 0;
	for (size_t i = 0; i < vals.size(); ++i)
	{
		if (type == FT_CHAR)
		{
			elements += EncodeUTF8(vals[i]).size();
		}
		else if (type == FT_CHAR16_T && vals[i] > 0xFFFF)
		{
			elements += 2;
		}
		else
		{
			++elements;
		}
		AppendCodePoint(bytes, type, vals[i]);
	}
	if (type == FT_CHAR) bytes.push_back(0);
	else if (type == FT_CHAR16_T) { char16_t z = 0; AppendPOD(bytes, z); }
	else { uint32_t z = 0; AppendPOD(bytes, z); }
	++elements;
	return true;
}


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

int main()
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

		DebugPostTokenOutputStream output;
		vector<PPToken> flat;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].kind != PP_WS && tokens[i].kind != PP_NL)
			{
				flat.push_back(tokens[i]);
			}
		}

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
				if (it == StringToTokenTypeMap.end())
				{
					output.emit_identifier(tok.data);
				}
				else
				{
					output.emit_simple(tok.data, it->second);
				}
				continue;
			}
			if (tok.kind == PP_STRING || tok.kind == PP_UD_STRING)
			{
				vector<string> group;
				group.push_back(tok.data);
				size_t j = i + 1;
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
			}
			if (tok.kind == PP_NUMBER)
			{
				if (ParseFloatingLiteral(tok.data, output) || ParseIntegerLiteral(tok.data, output))
				{
					continue;
				}
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
					if (!suffix.empty() && ParseCharacterLiteralCore(core, type, bytes))
					{
						output.emit_user_defined_literal_character(tok.data, suffix, type, bytes.data(), bytes.size());
					}
					else
					{
						output.emit_invalid(tok.data);
					}
				}
				else
				{
					EFundamentalType type;
					vector<unsigned char> bytes;
					if (ParseCharacterLiteralCore(tok.data, type, bytes))
					{
						output.emit_literal(tok.data, type, bytes.data(), bytes.size());
					}
					else
					{
						output.emit_invalid(tok.data);
					}
				}
				continue;
			}
			if (tok.kind == PP_STRING || tok.kind == PP_UD_STRING)
			{
				if (tok.kind == PP_UD_STRING)
				{
					string suffix;
					string core = StripUDSuffix(tok.data, suffix);
					EFundamentalType type;
					vector<unsigned char> bytes;
					size_t elems = 0;
					if (!suffix.empty() && ParseStringLiteralCore(core, type, bytes, elems))
					{
						output.emit_user_defined_literal_string_array(tok.data, suffix, elems, type, bytes.data(), bytes.size());
					}
					else
					{
						output.emit_invalid(tok.data);
					}
				}
				else
				{
					EFundamentalType type;
					vector<unsigned char> bytes;
					size_t elems = 0;
					if (ParseStringLiteralCore(tok.data, type, bytes, elems))
					{
						output.emit_literal_array(tok.data, elems, type, bytes.data(), bytes.size());
					}
					else
					{
						output.emit_invalid(tok.data);
					}
				}
				continue;
			}
			output.emit_invalid(tok.data);
		}
		output.emit_eof();
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
