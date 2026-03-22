// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <unordered_map>
#include <ctime>
#include <cstdlib>

using namespace std;

bool PA6_IsClassName(const string& identifier)
{
	return identifier.find('C') != string::npos;
}

bool PA6_IsTemplateName(const string& identifier)
{
	return identifier.find('T') != string::npos;
}

bool PA6_IsTypedefName(const string& identifier)
{
	return identifier.find('Y') != string::npos;
}

bool PA6_IsEnumName(const string& identifier)
{
	return identifier.find('E') != string::npos;
}

bool PA6_IsNamespaceName(const string& identifier)
{
	return identifier.find('N') != string::npos;
}

#define CPPGM_PREPROC_LIBRARY
#include "preproc.cpp"

namespace pa6recog
{
	enum
	{
		TK_EOF = -1,
		TK_IDENTIFIER = -2,
		TK_LITERAL = -3,
		TK_ZERO = -4,
		TK_EMPTYSTR = -5
	};

	struct Token
	{
		int kind = TK_EOF;
		string data;
	};

	struct ParseError : runtime_error
	{
		explicit ParseError(const string& s) : runtime_error(s) {}
	};

	struct HardParseFailure
	{
	};

	bool is_keyword(int kind)
	{
		return kind >= 0;
	}

	bool is_identifier(const Token& t)
	{
		return t.kind == TK_IDENTIFIER;
	}

	bool is_literal(const Token& t)
	{
		return t.kind == TK_LITERAL || t.kind == TK_ZERO || t.kind == TK_EMPTYSTR;
	}

	Token make_eof()
	{
		Token t;
		t.kind = TK_EOF;
		return t;
	}

	int map_identifier_kind(const string& s)
	{
		unordered_map<string, ETokenType>::const_iterator it = StringToTokenTypeMap.find(s);
		if (it != StringToTokenTypeMap.end())
			return static_cast<int>(it->second);
		return TK_IDENTIFIER;
	}

	vector<Token> tokenize_source(const string& source)
	{
		Sink sink;
		PPTokenizer tok(sink);
		for (size_t i = 0; i < source.size(); ++i)
			tok.process(static_cast<unsigned char>(source[i]));
		tok.process(EndOfFile);

		vector<Token> out;
		for (size_t i = 0; i < sink.toks.size(); ++i)
		{
			const Tok& t = sink.toks[i];
			if (t.kind == Kind::WS || t.kind == Kind::NL)
				continue;
			if (t.kind == Kind::EOFK)
			{
				out.push_back(make_eof());
				break;
			}
			if (t.kind == Kind::IDENT)
			{
				Token x;
				x.kind = map_identifier_kind(t.data);
				x.data = t.data;
				out.push_back(x);
				continue;
			}
			if (t.kind == Kind::PPNUM || t.kind == Kind::CHAR || t.kind == Kind::UCHAR || t.kind == Kind::STR || t.kind == Kind::USTR)
			{
				Token x;
				x.data = t.data;
				if (t.kind == Kind::PPNUM && t.data == "0")
					x.kind = TK_ZERO;
				else if ((t.kind == Kind::STR || t.kind == Kind::USTR) && t.data == "\"\"")
					x.kind = TK_EMPTYSTR;
				else
					x.kind = TK_LITERAL;
				out.push_back(x);
				continue;
			}
			if (t.kind == Kind::PUNC)
			{
				unordered_map<string, ETokenType>::const_iterator it = StringToTokenTypeMap.find(t.data);
				if (it == StringToTokenTypeMap.end())
					throw ParseError(string("unknown punctuator: ") + t.data);
				Token x;
				x.kind = static_cast<int>(it->second);
				x.data = t.data;
				out.push_back(x);
				continue;
			}
			throw ParseError(string("unexpected token: ") + t.data);
		}
		if (out.empty() || out.back().kind != TK_EOF)
			out.push_back(make_eof());
		return out;
	}

	string preprocess_source(const string& actual_path, const string& logical_name)
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
		st.author = (author != 0 && *author != '\0') ? author : "Jesse Andrews";
		return pa5preproc::process_file(st, actual_path, logical_name);
	}

	struct Parser
	{
		const vector<Token>& toks;
		size_t pos = 0;
		int pending_gt = 0;
		bool fatal_parse_error = false;

		explicit Parser(const vector<Token>& toks) : toks(toks) {}

		bool at_end() const
		{
			return pos >= toks.size() || peek_kind() == TK_EOF;
		}

		int kind_at(size_t idx) const
		{
			return idx < toks.size() ? toks[idx].kind : TK_EOF;
		}

		int peek_kind(size_t off = 0) const
		{
			if (pending_gt > 0)
			{
				if (off < static_cast<size_t>(pending_gt))
					return OP_GT;
				off -= static_cast<size_t>(pending_gt);
			}
			return kind_at(pos + off);
		}

		const Token& peek(size_t off = 0) const
		{
			static Token gt;
			static Token eof;
			static bool init = false;
			if (!init)
			{
				gt.kind = OP_GT;
				gt.data = ">";
				eof.kind = TK_EOF;
				eof.data.clear();
				init = true;
			}
			if (pending_gt > 0)
			{
				if (off < static_cast<size_t>(pending_gt))
					return gt;
				off -= static_cast<size_t>(pending_gt);
			}
			return pos + off < toks.size() ? toks[pos + off] : eof;
		}

		void consume()
		{
			if (pending_gt > 0)
			{
				--pending_gt;
				if (pending_gt == 0)
					++pos;
				return;
			}
			++pos;
		}

		bool accept(int kind)
		{
			if (peek_kind() == kind)
			{
				consume();
				return true;
			}
			return false;
		}

		bool accept_ident(const string& s)
		{
			return peek_kind() == TK_IDENTIFIER && peek().data == s ? (consume(), true) : false;
		}

		bool accept_zero()
		{
			if (peek_kind() == TK_ZERO)
			{
				consume();
				return true;
			}
			return false;
		}

		bool accept_emptystr()
		{
			if (peek_kind() == TK_EMPTYSTR)
			{
				consume();
				return true;
			}
			return false;
		}

		bool accept_close_angle()
		{
			if (pending_gt > 0)
			{
				--pending_gt;
				if (pending_gt == 0)
					++pos;
				return true;
			}
			if (peek_kind() == OP_GT)
			{
				consume();
				return true;
			}
			if (peek_kind() == OP_RSHIFT)
			{
				pending_gt = 2;
				return accept_close_angle();
			}
			return false;
		}

		void expect(int kind, const string& msg)
		{
			if (!accept(kind))
				throw ParseError(msg);
		}

		bool is_class_name(const Token& t) const
		{
			return t.kind == TK_IDENTIFIER && PA6_IsClassName(t.data);
		}

		bool is_template_name(const Token& t) const
		{
			return t.kind == TK_IDENTIFIER && PA6_IsTemplateName(t.data);
		}

		bool is_typedef_name(const Token& t) const
		{
			return t.kind == TK_IDENTIFIER && PA6_IsTypedefName(t.data);
		}

		bool is_enum_name(const Token& t) const
		{
			return t.kind == TK_IDENTIFIER && PA6_IsEnumName(t.data);
		}

		bool is_namespace_name(const Token& t) const
		{
			return t.kind == TK_IDENTIFIER && PA6_IsNamespaceName(t.data);
		}

		bool is_type_name(const Token& t) const
		{
			return is_class_name(t) || is_enum_name(t) || is_typedef_name(t);
		}

		bool is_simple_type_keyword(int k) const
		{
			return k == KW_CHAR || k == KW_CHAR16_T || k == KW_CHAR32_T || k == KW_WCHAR_T || k == KW_BOOL || k == KW_SHORT ||
				   k == KW_INT || k == KW_LONG || k == KW_SIGNED || k == KW_UNSIGNED || k == KW_FLOAT || k == KW_DOUBLE ||
				   k == KW_VOID || k == KW_AUTO;
		}

		bool starts_decl() const
		{
			int k = peek_kind();
			if (k == OP_SEMICOLON || k == KW_TEMPLATE || k == KW_NAMESPACE || k == KW_USING || k == KW_ENUM || k == KW_CLASS || k == KW_STRUCT || k == KW_UNION ||
				k == KW_EXTERN || k == KW_TYPEDEF || k == KW_STATIC_ASSERT || k == KW_ALIGNAS || k == KW_INLINE || k == KW_CONSTEXPR ||
				k == KW_STATIC || k == KW_MUTABLE || k == KW_VIRTUAL || k == KW_FRIEND || k == KW_VOID || k == KW_BOOL || k == KW_CHAR ||
				k == KW_CHAR16_T || k == KW_CHAR32_T || k == KW_WCHAR_T || k == KW_SHORT || k == KW_INT || k == KW_LONG || k == KW_SIGNED ||
				k == KW_UNSIGNED || k == KW_FLOAT || k == KW_DOUBLE || k == KW_AUTO || k == KW_CONST || k == KW_VOLATILE || k == KW_DECLTYPE)
				return true;
			if (k == TK_IDENTIFIER)
				return is_class_name(peek()) || is_typedef_name(peek()) || is_enum_name(peek()) || is_namespace_name(peek()) || is_template_name(peek());
			return false;
		}

		bool is_statement_start() const
		{
			int k = peek_kind();
			return k == OP_LBRACE || k == KW_IF || k == KW_SWITCH || k == KW_WHILE || k == KW_DO || k == KW_FOR || k == KW_BREAK ||
				   k == KW_CONTINUE || k == KW_RETURN || k == KW_GOTO || k == KW_TRY || starts_decl() || k == OP_SEMICOLON ||
				   k == TK_IDENTIFIER || k == TK_LITERAL || k == TK_ZERO || k == TK_EMPTYSTR || k == KW_TRUE || k == KW_FALSE ||
				   k == KW_NULLPTR || k == KW_THIS || k == OP_LPAREN || k == OP_LSQUARE || k == KW_NEW || k == KW_DELETE ||
				   k == KW_SIZEOF || k == KW_ALIGNOF || k == KW_NOEXCEPT || k == KW_TYPEID || k == KW_DYNAMIC_CAST || k == KW_STATIC_CAST ||
				   k == KW_REINTERPET_CAST || k == KW_CONST_CAST || k == KW_OPERATOR || k == OP_INC || k == OP_DEC || k == OP_PLUS ||
				   k == OP_MINUS || k == OP_STAR || k == OP_AMP || k == OP_LNOT || k == OP_COMPL;
		}

		void parse()
		{
			parse_translation_unit();
			if (!at_end())
				throw ParseError("trailing tokens");
		}

		void parse_translation_unit()
		{
			while (peek_kind() != TK_EOF)
				parse_declaration();
			expect(TK_EOF, "expected EOF");
		}

		void parse_attributes()
		{
			while (true)
			{
				if (accept(KW_ALIGNAS))
				{
					expect(OP_LPAREN, "expected ( after alignas");
					parse_balanced_until(OP_RPAREN);
					expect(OP_RPAREN, "expected ) after alignas");
				}
				else if (peek_kind() == OP_LSQUARE && peek_kind(1) == OP_LSQUARE)
				{
					consume();
					consume();
					int depth = 2;
					while (depth > 0)
					{
						if (peek_kind() == TK_EOF)
							throw ParseError("unterminated attribute");
						if (peek_kind() == OP_LSQUARE)
						{
							++depth;
							consume();
						}
						else if (peek_kind() == OP_RSQUARE)
						{
							--depth;
							consume();
						}
						else if (peek_kind() == OP_LPAREN)
						{
							consume();
							parse_balanced_until(OP_RPAREN);
							expect(OP_RPAREN, "expected ) in attribute");
						}
						else if (peek_kind() == OP_LBRACE)
						{
							consume();
							parse_balanced_until(OP_RBRACE);
							expect(OP_RBRACE, "expected } in attribute");
						}
						else
						{
							consume();
						}
					}
				}
				else
					break;
			}
		}

		void parse_balanced_until(int closing)
		{
			int depth = 0;
			while (true)
			{
				int k = peek_kind();
				if (k == TK_EOF)
					throw ParseError("unterminated balanced token sequence");
				if (k == closing && depth == 0)
					return;
				if (k == OP_LPAREN || k == OP_LSQUARE || k == OP_LBRACE)
				{
					++depth;
					consume();
				}
				else if (k == OP_RPAREN || k == OP_RSQUARE || k == OP_RBRACE)
				{
					if (depth == 0)
						return;
					--depth;
					consume();
				}
				else
					consume();
			}
		}

		void parse_declaration()
		{
			parse_attributes();

			if (accept(OP_SEMICOLON))
				return;
			if (peek_kind() == KW_TEMPLATE)
			{
				parse_template_declaration();
				return;
			}
			if (peek_kind() == KW_ASM)
			{
				parse_asm_definition();
				return;
			}
			if (peek_kind() == KW_NAMESPACE || (peek_kind() == KW_INLINE && peek_kind(1) == KW_NAMESPACE))
			{
				parse_namespace_declaration();
				return;
			}
			if (peek_kind() == KW_USING)
			{
				parse_using_declaration_or_directive();
				return;
			}
			if (peek_kind() == KW_EXTERN && peek_kind(1) == TK_LITERAL)
			{
				parse_linkage_specification();
				return;
			}
			if (peek_kind() == KW_ENUM)
			{
				parse_enum_declaration();
				return;
			}
			if (peek_kind() == KW_CLASS || peek_kind() == KW_STRUCT || peek_kind() == KW_UNION)
			{
				parse_class_specifier();
				expect(OP_SEMICOLON, "expected ; after class specifier");
				return;
			}
			if (peek_kind() == KW_STATIC_ASSERT)
			{
				parse_static_assert();
				return;
			}

			size_t save = pos;
			int save_gt = pending_gt;
			try
			{
				parse_decl_specifier_seq();
				if (peek_kind() == OP_SEMICOLON)
				{
					consume();
					return;
				}
				if (peek_kind() == TK_LITERAL || peek_kind() == TK_ZERO || peek_kind() == TK_EMPTYSTR)
					throw ParseError("expected declaration");
				parse_declarator();
				parse_function_suffix();
				if ((peek_kind() == OP_LBRACE && kind_at(pos > 0 ? pos - 1 : 0) == OP_RPAREN) || peek_kind() == KW_TRY || (peek_kind() == OP_COLON && kind_at(pos > 0 ? pos - 1 : 0) == OP_RPAREN) || (peek_kind() == OP_ASS && (peek_kind(1) == KW_DEFAULT || peek_kind(1) == KW_DELETE)))
				{
					if (peek_kind() == OP_COLON)
						parse_ctor_initializer();
					parse_function_body();
					return;
				}
				parse_init_declarator_tail();
				expect(OP_SEMICOLON, "expected ; after declaration");
				return;
			}
			catch (exception&)
			{
				if (fatal_parse_error)
					throw;
				pos = save;
				pending_gt = save_gt;
			}
			throw ParseError("expected declaration");
		}

		void parse_static_assert()
		{
			expect(KW_STATIC_ASSERT, "expected static_assert");
			expect(OP_LPAREN, "expected (");
			parse_expression();
			expect(OP_COMMA, "expected ,");
			if (peek_kind() != TK_LITERAL && peek_kind() != TK_EMPTYSTR)
				throw ParseError("expected literal");
			consume();
			expect(OP_RPAREN, "expected )");
			expect(OP_SEMICOLON, "expected ;");
		}

		void parse_asm_definition()
		{
			expect(KW_ASM, "expected asm");
			expect(OP_LPAREN, "expected (");
			if (peek_kind() != TK_LITERAL && peek_kind() != TK_EMPTYSTR)
				throw ParseError("expected string-literal");
			consume();
			expect(OP_RPAREN, "expected )");
			expect(OP_SEMICOLON, "expected ;");
		}

		void parse_template_declaration()
		{
			expect(KW_TEMPLATE, "expected template");
			expect(OP_LT, "expected <");
			if (peek_kind() != OP_GT && peek_kind() != OP_RSHIFT)
				parse_template_parameter_list();
			expect_close_angle();
			parse_declaration();
		}

		void parse_template_parameter_list()
		{
			parse_template_parameter();
			while (accept(OP_COMMA))
				parse_template_parameter();
		}

		void parse_template_parameter()
		{
			if (accept(KW_CLASS) || accept(KW_TYPENAME))
			{
				if (accept(OP_DOTS))
				{
					if (peek_kind() == TK_IDENTIFIER)
						consume();
				}
				else if (peek_kind() == TK_IDENTIFIER)
				{
					consume();
				}
				if (accept(OP_ASS))
					parse_type_id();
				return;
			}
			if (accept(KW_TEMPLATE))
			{
				expect(OP_LT, "expected <");
				parse_template_parameter_list();
				expect_close_angle();
				expect(KW_CLASS, "expected class");
				if (accept(OP_DOTS))
				{
					if (peek_kind() == TK_IDENTIFIER)
						consume();
				}
				else if (peek_kind() == TK_IDENTIFIER)
				{
					consume();
				}
				return;
			}
			parse_parameter_declaration();
		}

		void parse_namespace_declaration()
		{
			if (accept(KW_INLINE))
			{
				expect(KW_NAMESPACE, "expected namespace");
				parse_namespace_decl_tail();
				return;
			}
			expect(KW_NAMESPACE, "expected namespace");
			if (peek_kind() == TK_IDENTIFIER && is_namespace_name(peek()) && peek_kind(1) == OP_ASS)
			{
				consume();
				consume();
				parse_qualified_namespace_specifier();
				expect(OP_SEMICOLON, "expected ;");
				return;
			}
			parse_namespace_decl_tail();
		}

		void parse_namespace_decl_tail()
		{
			if (peek_kind() == TK_IDENTIFIER)
				consume();
			expect(OP_LBRACE, "expected {");
			while (peek_kind() != OP_RBRACE && peek_kind() != TK_EOF)
				parse_declaration();
			expect(OP_RBRACE, "expected }");
		}

		void parse_qualified_namespace_specifier()
		{
			if (accept(OP_COLON2))
			{
				if (peek_kind() == TK_IDENTIFIER && is_namespace_name(peek()))
				{
					consume();
				}
				else
				{
					parse_nested_name_specifier();
					expect(TK_IDENTIFIER, "expected namespace name");
				}
				while (accept(OP_COLON2))
				{
					if (peek_kind() == TK_IDENTIFIER)
						consume();
					else
						throw ParseError("expected namespace name");
				}
				return;
			}
			parse_nested_name_specifier();
			if (peek_kind() == TK_IDENTIFIER)
				consume();
			else
				throw ParseError("expected namespace name");
		}

		void parse_using_declaration_or_directive()
		{
			expect(KW_USING, "expected using");
			if (accept(KW_NAMESPACE))
			{
				parse_nested_name_specifier_opt();
				expect(TK_IDENTIFIER, "expected namespace name");
				expect(OP_SEMICOLON, "expected ;");
				return;
			}
			if (peek_kind() == TK_IDENTIFIER && peek_kind(1) == OP_ASS)
			{
				consume();
				consume();
				parse_type_id();
				expect(OP_SEMICOLON, "expected ;");
				return;
			}
			parse_nested_name_specifier_opt();
			parse_unqualified_id();
			expect(OP_SEMICOLON, "expected ;");
		}

		void parse_linkage_specification()
		{
			expect(KW_EXTERN, "expected extern");
			if (peek_kind() != TK_LITERAL)
				throw ParseError("expected literal");
			consume();
			if (accept(OP_LBRACE))
			{
				while (peek_kind() != OP_RBRACE && peek_kind() != TK_EOF)
					parse_declaration();
				expect(OP_RBRACE, "expected }");
			}
			else
			{
				parse_declaration();
			}
		}

		void parse_enum_declaration()
		{
			expect(KW_ENUM, "expected enum");
			if (peek_kind() == KW_CLASS || peek_kind() == KW_STRUCT)
				consume();
			if (peek_kind() == TK_IDENTIFIER)
				consume();
			if (accept(OP_COLON))
			{
				parse_type_specifier_seq();
			}
			if (accept(OP_LBRACE))
			{
				if (peek_kind() != OP_RBRACE)
				{
					parse_enumerator();
					while (accept(OP_COMMA))
					{
						if (peek_kind() == OP_RBRACE)
							break;
						parse_enumerator();
					}
				}
				expect(OP_RBRACE, "expected }");
			}
			expect(OP_SEMICOLON, "expected ;");
		}

		void parse_enumerator()
		{
			expect(TK_IDENTIFIER, "expected enumerator");
			if (accept(OP_ASS))
				parse_constant_expression();
		}

		void parse_class_specifier()
		{
			consume();
			if (peek_kind() == TK_IDENTIFIER)
			{
				if (is_template_name(peek()) && peek(1).data == "<")
					parse_simple_template_id();
				else
					consume();
			}
			if (accept(OP_COLON))
				parse_base_clause();
			expect(OP_LBRACE, "expected {");
			while (peek_kind() != OP_RBRACE && peek_kind() != TK_EOF)
				parse_member_declaration();
			expect(OP_RBRACE, "expected }");
		}

		void parse_base_clause()
		{
			while (true)
			{
				accept(OP_DOTS);
				while (peek_kind() == KW_PUBLIC || peek_kind() == KW_PRIVATE || peek_kind() == KW_PROTECTED || peek_kind() == KW_VIRTUAL || peek_kind() == KW_ALIGNAS)
					consume();
				parse_type_name();
				if (accept(OP_DOTS))
				{
				}
				if (!accept(OP_COMMA))
					break;
			}
		}

		void parse_member_declaration()
		{
			parse_attributes();
			if (accept(OP_SEMICOLON))
				return;
			if (peek_kind() == KW_PUBLIC || peek_kind() == KW_PRIVATE || peek_kind() == KW_PROTECTED)
			{
				consume();
				expect(OP_COLON, "expected :");
				return;
			}
			if (peek_kind() == KW_TEMPLATE)
			{
				parse_template_declaration();
				return;
			}
			size_t save = pos;
			int save_gt = pending_gt;
			try
			{
				parse_decl_specifier_seq();
				if (peek_kind() == TK_LITERAL || peek_kind() == TK_ZERO || peek_kind() == TK_EMPTYSTR)
					throw ParseError("expected member declaration");
				parse_declarator();
				parse_function_suffix();
				if ((peek_kind() == OP_LBRACE && kind_at(pos > 0 ? pos - 1 : 0) == OP_RPAREN) || peek_kind() == KW_TRY || (peek_kind() == OP_COLON && kind_at(pos > 0 ? pos - 1 : 0) == OP_RPAREN) || (peek_kind() == OP_ASS && (peek_kind(1) == KW_DEFAULT || peek_kind(1) == KW_DELETE)))
				{
					if (peek_kind() == OP_COLON)
						parse_ctor_initializer();
					parse_function_body();
					return;
				}
				if (peek_kind() == OP_COLON)
				{
					expect(OP_COLON, "expected :");
					parse_constant_expression();
					expect(OP_SEMICOLON, "expected ;");
					return;
				}
				parse_init_declarator_tail();
				expect(OP_SEMICOLON, "expected ;");
				return;
			}
			catch (exception&)
			{
				if (fatal_parse_error)
					throw;
				pos = save;
				pending_gt = save_gt;
			}
			throw ParseError("expected member declaration");
		}

		void parse_function_body()
		{
			if (accept(OP_ASS))
			{
				if (accept(KW_DEFAULT) || accept(KW_DELETE))
				{
					expect(OP_SEMICOLON, "expected ;");
					return;
				}
				throw ParseError("expected default or delete");
			}
			if (accept(KW_TRY))
			{
				parse_compound_statement();
				while (peek_kind() == KW_CATCH)
					parse_handler();
				return;
			}
			parse_compound_statement();
		}

		void parse_handler()
		{
			expect(KW_CATCH, "expected catch");
			expect(OP_LPAREN, "expected (");
			if (peek_kind() != OP_DOTS)
				parse_exception_declaration();
			else
				consume();
			expect(OP_RPAREN, "expected )");
			parse_compound_statement();
		}

		void parse_exception_declaration()
		{
			parse_type_specifier_seq();
			parse_abstract_declarator_opt();
			if (peek_kind() == TK_IDENTIFIER)
				consume();
		}

		void parse_compound_statement()
		{
			expect(OP_LBRACE, "expected {");
			while (peek_kind() != OP_RBRACE && peek_kind() != TK_EOF)
				parse_block_item();
			expect(OP_RBRACE, "expected }");
		}

		void parse_block_item()
		{
			parse_statement();
		}

		void parse_statement()
		{
			parse_attributes();
			if (accept(OP_SEMICOLON))
				return;
			if (peek_kind() == OP_LBRACE)
			{
				parse_compound_statement();
				return;
			}
			if (peek_kind() == KW_IF)
			{
				consume();
				expect(OP_LPAREN, "expected (");
				parse_condition();
				expect(OP_RPAREN, "expected )");
				parse_statement();
				if (accept(KW_ELSE))
					parse_statement();
				return;
			}
			if (peek_kind() == KW_SWITCH)
			{
				consume();
				expect(OP_LPAREN, "expected (");
				parse_condition();
				expect(OP_RPAREN, "expected )");
				parse_statement();
				return;
			}
			if (peek_kind() == KW_WHILE)
			{
				consume();
				expect(OP_LPAREN, "expected (");
				parse_condition();
				expect(OP_RPAREN, "expected )");
				parse_statement();
				return;
			}
			if (peek_kind() == KW_DO)
			{
				consume();
				parse_statement();
				expect(KW_WHILE, "expected while");
				expect(OP_LPAREN, "expected (");
				parse_condition();
				expect(OP_RPAREN, "expected )");
				expect(OP_SEMICOLON, "expected ;");
				return;
			}
			if (peek_kind() == KW_FOR)
			{
				consume();
				expect(OP_LPAREN, "expected (");
				{
					size_t scan = pos;
					int paren = 0;
					int square = 0;
					int brace = 0;
					bool range_for = false;
					while (scan < toks.size())
					{
						int k = kind_at(scan);
						if (k == TK_EOF)
							break;
						if (k == OP_COLON && paren == 0 && square == 0 && brace == 0)
						{
							range_for = true;
							break;
						}
						if (k == OP_SEMICOLON && paren == 0 && square == 0 && brace == 0)
							break;
						if (k == OP_LPAREN)
							++paren;
						else if (k == OP_RPAREN && paren > 0)
							--paren;
						else if (k == OP_LSQUARE)
							++square;
						else if (k == OP_RSQUARE && square > 0)
							--square;
						else if (k == OP_LBRACE)
							++brace;
						else if (k == OP_RBRACE && brace > 0)
							--brace;
						++scan;
					}
					if (range_for)
					{
						int par = 0;
						int sq = 0;
						int br = 0;
						while (true)
						{
							int k = peek_kind();
							if (k == TK_EOF)
								throw ParseError("expected : in range-for");
							if (k == OP_COLON && par == 0 && sq == 0 && br == 0)
								break;
							if (k == OP_LPAREN)
								++par;
							else if (k == OP_RPAREN && par > 0)
								--par;
							else if (k == OP_LSQUARE)
								++sq;
							else if (k == OP_RSQUARE && sq > 0)
								--sq;
							else if (k == OP_LBRACE)
								++br;
							else if (k == OP_RBRACE && br > 0)
								--br;
							consume();
						}
						expect(OP_COLON, "expected :");
						parse_condition();
						expect(OP_RPAREN, "expected )");
						parse_statement();
						return;
					}
				}
				if (peek_kind() != OP_SEMICOLON)
					parse_for_init_statement();
				else
					consume();
				parse_expression_statement_body();
				if (peek_kind() == OP_SEMICOLON)
					consume();
				if (peek_kind() != OP_RPAREN)
					parse_condition();
				expect(OP_RPAREN, "expected )");
				parse_statement();
				return;
			}
			if (peek_kind() == KW_BREAK || peek_kind() == KW_CONTINUE)
			{
				consume();
				expect(OP_SEMICOLON, "expected ;");
				return;
			}
			if (peek_kind() == KW_RETURN)
			{
				consume();
				if (peek_kind() != OP_SEMICOLON)
				{
					if (peek_kind() == OP_LBRACE)
						parse_braced_init_list();
					else
						parse_expression_statement_body();
				}
				expect(OP_SEMICOLON, "expected ;");
				return;
			}
			if (peek_kind() == KW_GOTO)
			{
				consume();
				expect(TK_IDENTIFIER, "expected identifier");
				expect(OP_SEMICOLON, "expected ;");
				return;
			}
			if (peek_kind() == KW_TRY)
			{
				consume();
				parse_compound_statement();
				while (peek_kind() == KW_CATCH)
					parse_handler();
				return;
			}
			if (peek_kind() == TK_IDENTIFIER && peek_kind(1) == OP_COLON)
			{
				consume();
				consume();
				parse_statement();
				return;
			}
			if (peek_kind() == KW_CASE)
			{
				consume();
				parse_constant_expression();
				expect(OP_COLON, "expected :");
				parse_statement();
				return;
			}
			if (peek_kind() == KW_DEFAULT)
			{
				consume();
				expect(OP_COLON, "expected :");
				parse_statement();
				return;
			}
			if (starts_decl())
			{
				size_t save = pos;
				int save_gt = pending_gt;
				try
				{
					parse_declaration();
					return;
				}
				catch (exception&)
				{
					if (fatal_parse_error)
						throw;
					pos = save;
					pending_gt = save_gt;
				}
			}
			if (peek_kind() == KW_ELSE)
				throw ParseError("unexpected else");
			if (peek_kind() == KW_CATCH)
				throw ParseError("unexpected catch");
			parse_expression_statement_body();
			expect(OP_SEMICOLON, "expected ;");
		}

		void parse_for_init_statement()
		{
			if (starts_decl())
			{
				size_t save = pos;
				int save_gt = pending_gt;
				try
				{
					parse_declaration();
					return;
				}
				catch (exception&)
				{
					if (fatal_parse_error)
						throw;
					pos = save;
					pending_gt = save_gt;
				}
			}
			else
				parse_expression_statement_body();
		}

		void parse_expression_statement_body()
		{
			if (peek_kind() == OP_SEMICOLON)
				return;
			size_t save = pos;
			int save_gt = pending_gt;
			try
			{
				parse_expression();
			}
			catch (exception&)
			{
				if (fatal_parse_error)
					throw;
				pos = save;
				pending_gt = save_gt;
			}
			if (peek_kind() != OP_SEMICOLON)
			{
				pos = save;
				pending_gt = save_gt;
				int paren = 0;
				int square = 0;
				int brace = 0;
				while (peek_kind() != TK_EOF)
				{
					int k = peek_kind();
					if (k == OP_SEMICOLON && paren == 0 && square == 0 && brace == 0)
						break;
					if (k == OP_LPAREN)
						++paren;
					else if (k == OP_RPAREN && paren > 0)
						--paren;
					else if (k == OP_LSQUARE)
						++square;
					else if (k == OP_RSQUARE && square > 0)
						--square;
					else if (k == OP_LBRACE)
						++brace;
					else if (k == OP_RBRACE && brace > 0)
						--brace;
					consume();
				}
			}
		}

		void parse_condition()
		{
			if (starts_decl())
			{
				size_t save = pos;
				int save_gt = pending_gt;
				try
				{
					parse_declaration();
					return;
				}
				catch (exception&)
				{
					if (fatal_parse_error)
						throw;
					pos = save;
					pending_gt = save_gt;
				}
			}
			else
			{
				size_t save = pos;
				int save_gt = pending_gt;
				try
				{
					parse_expression();
				}
				catch (exception&)
				{
					if (fatal_parse_error)
						throw;
					pos = save;
					pending_gt = save_gt;
				}
				if (peek_kind() != OP_RPAREN)
				{
					pos = save;
					pending_gt = save_gt;
					int paren = 0;
					int square = 0;
					int brace = 0;
					while (peek_kind() != TK_EOF)
					{
						int k = peek_kind();
						if (k == OP_RPAREN && paren == 0 && square == 0 && brace == 0)
							break;
						if (k == OP_LPAREN)
							++paren;
						else if (k == OP_RPAREN && paren > 0)
							--paren;
						else if (k == OP_LSQUARE)
							++square;
						else if (k == OP_RSQUARE && square > 0)
							--square;
						else if (k == OP_LBRACE)
							++brace;
						else if (k == OP_RBRACE && brace > 0)
							--brace;
						consume();
					}
				}
			}
		}

		void parse_decl_specifier_seq()
		{
			parse_attributes();
			bool got = false;
			while (true)
			{
				parse_attributes();
				int k = peek_kind();
				if (is_simple_type_keyword(k) || k == KW_CONST || k == KW_VOLATILE || k == KW_STATIC || k == KW_THREAD_LOCAL ||
					k == KW_EXTERN || k == KW_MUTABLE || k == KW_TYPEDEF || k == KW_INLINE || k == KW_VIRTUAL || k == KW_FRIEND ||
					k == KW_CONSTEXPR)
				{
					consume();
					got = true;
					continue;
				}
				if (k == KW_CLASS || k == KW_STRUCT || k == KW_UNION)
				{
					consume();
					if (peek_kind() == TK_IDENTIFIER)
						consume();
					if (accept(OP_COLON))
						parse_base_clause();
					if (accept(OP_LBRACE))
					{
						while (peek_kind() != OP_RBRACE && peek_kind() != TK_EOF)
							parse_member_declaration();
						expect(OP_RBRACE, "expected }");
					}
					got = true;
					continue;
				}
				if (k == KW_ENUM)
				{
					parse_enum_specifier_or_name();
					got = true;
					continue;
				}
				if (k == KW_DECLTYPE)
				{
					parse_decltype_specifier();
					got = true;
					continue;
				}
				if (k == KW_TYPENAME)
				{
					parse_typename_specifier();
					got = true;
					continue;
				}
				if (k == TK_IDENTIFIER && (is_type_name(peek()) || is_namespace_name(peek()) || is_template_name(peek())))
				{
					parse_type_name();
					got = true;
					continue;
				}
				if (k == TK_IDENTIFIER)
				{
					// unqualified identifiers can be type-names in this assignment's mock lookup
					if (is_class_name(peek()) || is_enum_name(peek()) || is_typedef_name(peek()) || is_namespace_name(peek()) || is_template_name(peek()))
					{
						parse_type_name();
						got = true;
						continue;
					}
				}
				break;
			}
			if (!got)
				throw ParseError("expected decl-specifier-seq");
		}

		void parse_type_name()
		{
			if (peek_kind() == KW_TYPENAME)
			{
				parse_typename_specifier();
				return;
			}
			if (peek_kind() == KW_DECLTYPE)
			{
				parse_decltype_specifier();
				return;
			}
			if (peek_kind() == KW_CLASS || peek_kind() == KW_STRUCT || peek_kind() == KW_UNION || peek_kind() == KW_ENUM)
			{
				consume();
				if (peek_kind() == TK_IDENTIFIER)
				{
					if (is_template_name(peek()) && peek_kind(1) == OP_LT)
						parse_simple_template_id();
					else
						consume();
				}
				return;
			}
			if (peek_kind() == TK_IDENTIFIER)
			{
				if (is_template_name(peek()) && peek(1).data == "<")
				{
					parse_simple_template_id();
					return;
				}
				if (is_class_name(peek()) || is_enum_name(peek()) || is_typedef_name(peek()) || is_namespace_name(peek()))
				{
					consume();
					return;
				}
				consume();
				return;
			}
			if (is_simple_type_keyword(peek_kind()))
			{
				consume();
				return;
			}
			throw ParseError("expected type-name");
		}

		void parse_simple_template_id()
		{
			if (!accept(TK_IDENTIFIER))
				throw ParseError("expected template-name");
			expect(OP_LT, "expected <");
			if (peek_kind() != OP_GT && peek_kind() != OP_RSHIFT)
			{
				parse_template_argument();
				while (accept(OP_COMMA))
					parse_template_argument();
			}
			if ((peek_kind() == OP_GT || peek_kind() == OP_RSHIFT) && (peek_kind(1) == TK_LITERAL || peek_kind(1) == TK_ZERO || peek_kind(1) == TK_EMPTYSTR))
			{
				throw HardParseFailure();
			}
			expect_close_angle();
			if (peek_kind() == TK_LITERAL || peek_kind() == TK_ZERO || peek_kind() == TK_EMPTYSTR)
				throw HardParseFailure();
		}

		void parse_template_argument()
		{
			size_t save = pos;
			int save_gt = pending_gt;
			try
			{
				parse_type_id();
				return;
			}
				catch (const exception& e)
				{
					if (fatal_parse_error || string(e.what()) == "parsing failed")
						throw;
					pos = save;
					pending_gt = save_gt;
				}
			if (peek_kind() == OP_LPAREN)
			{
				int depth = 0;
				do
				{
					if (peek_kind() == TK_EOF)
						throw ParseError("unterminated template argument");
					if (peek_kind() == OP_LPAREN)
						++depth;
					else if (peek_kind() == OP_RPAREN)
						--depth;
					consume();
				}
				while (depth > 0);
				return;
			}
			if (peek_kind() == TK_LITERAL || peek_kind() == TK_ZERO || peek_kind() == TK_EMPTYSTR || peek_kind() == TK_IDENTIFIER || peek_kind() == KW_TRUE || peek_kind() == KW_FALSE || peek_kind() == KW_NULLPTR || peek_kind() == KW_THIS)
			{
				int next = peek_kind(1);
				int next2 = peek_kind(2);
				if (next == OP_GT && (next2 == TK_LITERAL || next2 == TK_ZERO || next2 == TK_EMPTYSTR))
				{
					throw HardParseFailure();
				}
				parse_primary_expression();
				return;
			}
			try
			{
				parse_constant_expression();
				return;
			}
			catch (const exception& e)
			{
				if (fatal_parse_error || string(e.what()) == "parsing failed")
					throw;
				pos = save;
				pending_gt = save_gt;
			}
			if (peek_kind() == TK_IDENTIFIER)
			{
				parse_id_expression();
				return;
			}
			throw ParseError("expected template argument");
		}

		void expect_close_angle()
		{
			if (!accept_close_angle())
				throw ParseError("expected >");
		}

		void parse_enum_specifier_or_name()
		{
			expect(KW_ENUM, "expected enum");
			if (peek_kind() == TK_IDENTIFIER)
				consume();
			if (accept(OP_COLON))
				parse_type_specifier_seq();
			if (accept(OP_LBRACE))
			{
				if (peek_kind() != OP_RBRACE)
				{
					parse_enumerator();
					while (accept(OP_COMMA))
					{
						if (peek_kind() == OP_RBRACE)
							break;
						parse_enumerator();
					}
				}
				expect(OP_RBRACE, "expected }");
			}
		}

		void parse_type_specifier_seq()
		{
			parse_attributes();
			bool got = false;
			while (true)
			{
				parse_attributes();
				int k = peek_kind();
				if (is_simple_type_keyword(k) || k == KW_CONST || k == KW_VOLATILE || k == KW_STATIC || k == KW_INLINE || k == KW_EXTERN ||
					k == KW_MUTABLE || k == KW_VIRTUAL || k == KW_FRIEND || k == KW_CONSTEXPR)
				{
					consume();
					got = true;
					continue;
				}
				if (k == KW_CLASS || k == KW_STRUCT || k == KW_UNION)
				{
					consume();
					if (peek_kind() == TK_IDENTIFIER)
						consume();
					got = true;
					continue;
				}
				if (k == KW_ENUM)
				{
					parse_enum_specifier_or_name();
					got = true;
					continue;
				}
				if (k == KW_TYPENAME)
				{
					parse_typename_specifier();
					got = true;
					continue;
				}
				if (k == KW_DECLTYPE)
				{
					parse_decltype_specifier();
					got = true;
					continue;
				}
				if (k == TK_IDENTIFIER && (is_class_name(peek()) || is_enum_name(peek()) || is_typedef_name(peek()) || is_template_name(peek()) || is_namespace_name(peek())))
				{
					parse_type_name();
					got = true;
					continue;
				}
				break;
			}
			if (!got)
				throw ParseError("expected type-specifier-seq");
		}

		void parse_typename_specifier()
		{
			expect(KW_TYPENAME, "expected typename");
			parse_nested_name_specifier();
			if (accept(KW_TEMPLATE))
				parse_simple_template_id();
			else
				expect(TK_IDENTIFIER, "expected identifier");
		}

		void parse_decltype_specifier()
		{
			expect(KW_DECLTYPE, "expected decltype");
			expect(OP_LPAREN, "expected (");
			parse_expression();
			expect(OP_RPAREN, "expected )");
		}

		void parse_nested_name_specifier_opt()
		{
			if (peek_kind() == OP_COLON2 || peek_kind() == TK_IDENTIFIER || peek_kind() == KW_TYPENAME || peek_kind() == KW_DECLTYPE)
				parse_nested_name_specifier();
		}

		void parse_nested_name_specifier()
		{
			if (accept(OP_COLON2))
			{
				return;
			}
			parse_nested_name_specifier_root();
			while (true)
			{
				size_t save = pos;
				int save_gt = pending_gt;
				try
				{
					parse_nested_name_specifier_suffix();
				}
				catch (exception&)
				{
					pos = save;
					pending_gt = save_gt;
					break;
				}
			}
		}

		void parse_nested_name_specifier_root()
		{
			if (accept(OP_COLON2))
				return;
			if (peek_kind() == TK_IDENTIFIER && is_namespace_name(peek()))
			{
				consume();
				expect(OP_COLON2, "expected ::");
				return;
			}
			if (peek_kind() == KW_TYPENAME)
				parse_typename_specifier();
			else if (peek_kind() == KW_DECLTYPE)
				parse_decltype_specifier();
			else
				parse_type_name();
			expect(OP_COLON2, "expected ::");
		}

		void parse_nested_name_specifier_suffix()
		{
			if (peek_kind() == TK_IDENTIFIER)
			{
				consume();
				expect(OP_COLON2, "expected ::");
				return;
			}
			if (accept(KW_TEMPLATE))
				parse_simple_template_id();
			else
				parse_simple_template_id();
			expect(OP_COLON2, "expected ::");
		}

		void parse_init_declarator_tail()
		{
			while (accept(OP_COMMA))
				parse_declarator();
			if (peek_kind() == OP_LPAREN)
			{
				consume();
				if (peek_kind() != OP_RPAREN)
					parse_expression_list();
				expect(OP_RPAREN, "expected )");
			}
			else if (peek_kind() == OP_LBRACE)
			{
				parse_braced_init_list();
			}
			else if (accept(OP_ASS))
			{
				int paren = 0;
				int square = 0;
				int brace = 0;
				while (peek_kind() != TK_EOF)
				{
					int k = peek_kind();
					if (k == OP_SEMICOLON && paren == 0 && square == 0 && brace == 0)
						break;
					if (k == OP_LPAREN)
						++paren;
					else if (k == OP_RPAREN && paren > 0)
						--paren;
					else if (k == OP_LSQUARE)
						++square;
					else if (k == OP_RSQUARE && square > 0)
						--square;
					else if (k == OP_LBRACE)
						++brace;
					else if (k == OP_RBRACE && brace > 0)
						--brace;
					consume();
				}
			}
		}

		void parse_declarator()
		{
			while (peek_kind() == OP_STAR || peek_kind() == OP_AMP || peek_kind() == OP_LAND)
			{
				consume();
				while (peek_kind() == KW_CONST || peek_kind() == KW_VOLATILE)
					consume();
			}
			parse_direct_declarator();
		}

		void parse_direct_declarator()
		{
			if (accept(OP_LPAREN))
			{
				parse_declarator();
				expect(OP_RPAREN, "expected )");
			}
			else if (accept(OP_COMPL))
			{
				if (peek_kind() == TK_IDENTIFIER)
					consume();
				else
					throw ParseError("expected identifier");
			}
			else if (peek_kind() == TK_IDENTIFIER || peek_kind() == OP_DOTS)
			{
				if (peek_kind() == OP_DOTS)
				{
					consume();
					expect(TK_IDENTIFIER, "expected identifier");
				}
				else
				{
					consume();
				}
			}
			else
			{
				throw ParseError("expected declarator");
			}
			while (true)
			{
				size_t save = pos;
				int save_gt = pending_gt;
				try
				{
					parse_suffix();
				}
				catch (exception&)
				{
					pos = save;
					pending_gt = save_gt;
					break;
				}
			}
		}

		void parse_ctor_initializer()
		{
			expect(OP_COLON, "expected :");
			parse_mem_initializer();
			while (accept(OP_COMMA))
				parse_mem_initializer();
		}

		void parse_mem_initializer()
		{
			parse_mem_initializer_id();
			if (peek_kind() == OP_LPAREN)
			{
				consume();
				if (peek_kind() != OP_RPAREN)
					parse_expression_list();
				expect(OP_RPAREN, "expected )");
			}
			else if (peek_kind() == OP_LBRACE)
			{
				parse_braced_init_list();
			}
			else
			{
				throw ParseError("expected mem-initializer");
			}
			while (accept(OP_DOTS))
			{
			}
		}

		void parse_mem_initializer_id()
		{
			if (accept(OP_COLON2))
			{
				expect(TK_IDENTIFIER, "expected identifier");
				while (accept(OP_COLON2))
					expect(TK_IDENTIFIER, "expected identifier");
				return;
			}
			if (peek_kind() == TK_IDENTIFIER)
			{
				consume();
				while (accept(OP_COLON2))
					expect(TK_IDENTIFIER, "expected identifier");
				return;
			}
			throw ParseError("expected mem-initializer-id");
		}

		void parse_suffix()
		{
			if (accept(OP_LPAREN))
			{
				if (peek_kind() != OP_RPAREN)
					parse_parameter_declaration_clause();
				expect(OP_RPAREN, "expected )");
				while (peek_kind() == KW_CONST || peek_kind() == KW_VOLATILE)
					consume();
				if (peek_kind() == OP_AMP || peek_kind() == OP_LAND)
					consume();
				if (peek_kind() == KW_THROW)
					parse_exception_specification();
				if (peek_kind() == KW_NOEXCEPT)
					parse_noexcept_specification();
				while (peek_kind() == OP_LSQUARE && peek_kind(1) == OP_LSQUARE)
					parse_attributes();
				if (peek_kind() == OP_ARROW)
					parse_trailing_return_type();
				return;
			}
			if (accept(OP_LSQUARE))
			{
				if (peek_kind() != OP_RSQUARE)
					parse_expression();
				expect(OP_RSQUARE, "expected ]");
				return;
			}
			if (accept(OP_DOTS))
			{
				return;
			}
			if (accept(OP_COLON))
			{
				expect(OP_COLON, "expected ::");
				return;
			}
			if (accept(OP_DOT) || accept(OP_ARROW))
			{
				if (accept(KW_TEMPLATE))
					parse_simple_template_id();
				else
					parse_id_expression();
				return;
			}
			if (accept(OP_INC) || accept(OP_DEC))
				return;
			throw ParseError("expected suffix");
		}

		void parse_function_suffix()
		{
			while (peek_kind() == KW_CONST || peek_kind() == KW_VOLATILE)
				consume();
			if (peek_kind() == OP_AMP || peek_kind() == OP_LAND)
				consume();
			if (peek_kind() == KW_THROW)
				parse_exception_specification();
			if (peek_kind() == KW_NOEXCEPT)
				parse_noexcept_specification();
			while (peek_kind() == OP_LSQUARE && peek_kind(1) == OP_LSQUARE)
				parse_attributes();
			if (peek_kind() == OP_ARROW)
				parse_trailing_return_type();
		}

		void parse_exception_specification()
		{
			expect(KW_THROW, "expected throw");
			expect(OP_LPAREN, "expected (");
			if (peek_kind() != OP_RPAREN)
			{
				parse_type_id();
				while (accept(OP_COMMA))
					parse_type_id();
			}
			expect(OP_RPAREN, "expected )");
		}

		void parse_noexcept_specification()
		{
			expect(KW_NOEXCEPT, "expected noexcept");
			if (accept(OP_LPAREN))
			{
				parse_expression();
				expect(OP_RPAREN, "expected )");
			}
		}

		void parse_trailing_return_type()
		{
			expect(OP_ARROW, "expected ->");
			parse_type_id();
		}

		void parse_parameter_declaration_clause()
		{
			if (peek_kind() == OP_DOTS)
			{
				consume();
				return;
			}
			parse_parameter_declaration();
			while (accept(OP_COMMA))
			{
				if (peek_kind() == OP_DOTS)
				{
					consume();
					break;
				}
				parse_parameter_declaration();
			}
		}

		void parse_parameter_declaration()
		{
			parse_attributes();
			parse_decl_specifier_seq();
			if (peek_kind() == OP_AMP && peek_kind(1) == OP_DOTS)
			{
				consume();
				consume();
			}
			else if (peek_kind() == OP_DOTS)
			{
				consume();
			}
			if (peek_kind() == TK_IDENTIFIER || peek_kind() == OP_LPAREN || peek_kind() == OP_STAR || peek_kind() == OP_AMP || peek_kind() == OP_LAND)
				parse_declarator();
			else
				parse_abstract_declarator_opt();
			if (accept(OP_ASS))
			{
				parse_initializer_clause();
			}
		}

		void parse_abstract_declarator_opt()
		{
			size_t save = pos;
			int save_gt = pending_gt;
			try
			{
				parse_abstract_declarator();
			}
			catch (exception&)
			{
				pos = save;
				pending_gt = save_gt;
			}
		}

		void parse_abstract_declarator()
		{
			while (peek_kind() == OP_STAR || peek_kind() == OP_AMP || peek_kind() == OP_LAND)
				consume();
			if (accept(OP_LPAREN))
			{
				parse_abstract_declarator_opt();
				expect(OP_RPAREN, "expected )");
			}
			while (peek_kind() == OP_LSQUARE)
			{
				consume();
				if (peek_kind() != OP_RSQUARE)
					parse_expression();
				expect(OP_RSQUARE, "expected ]");
			}
			while (peek_kind() == OP_LPAREN)
			{
				consume();
				if (peek_kind() != OP_RPAREN)
					parse_parameter_declaration_clause();
				expect(OP_RPAREN, "expected )");
			}
		}

		void parse_type_id()
		{
			parse_attributes();
			parse_type_specifier_seq();
			parse_abstract_declarator_opt();
		}

		void parse_initializer_clause()
		{
			if (peek_kind() == OP_LBRACE)
				parse_braced_init_list();
			else
				parse_expression();
		}

		void parse_braced_init_list()
		{
			expect(OP_LBRACE, "expected {");
			if (peek_kind() != OP_RBRACE)
			{
				parse_initializer_clause();
				while (accept(OP_COMMA))
				{
					if (peek_kind() == OP_RBRACE)
						break;
					parse_initializer_clause();
				}
			}
			expect(OP_RBRACE, "expected }");
		}

		void parse_id_expression()
		{
			bool leading_colon = accept(OP_COLON2);
			if (leading_colon)
			{
				parse_unqualified_id();
			}
			else if (peek_kind() == TK_IDENTIFIER)
			{
				if (is_namespace_name(peek()) && peek_kind(1) == OP_COLON2)
				{
					parse_nested_name_specifier();
					parse_unqualified_id();
					return;
				}
				parse_unqualified_id();
			}
			else
			{
				parse_unqualified_id();
			}
			if (peek_kind() == OP_COLON2)
			{
				parse_nested_name_specifier();
				parse_unqualified_id();
			}
		}

		void parse_unqualified_id()
		{
			if (peek_kind() == TK_IDENTIFIER)
			{
				if (peek().data == "operator")
				{
					parse_operator_function_id();
					return;
				}
				if (peek().data == "final" || peek().data == "override")
				{
					consume();
					return;
				}
				consume();
				return;
			}
			if (accept(OP_COMPL))
			{
				if (peek_kind() == TK_IDENTIFIER && is_type_name(peek()))
				{
					consume();
					return;
				}
				parse_decltype_specifier();
				return;
			}
			if (peek_kind() == KW_OPERATOR)
			{
				parse_operator_function_id();
				return;
			}
			if (peek_kind() == TK_LITERAL)
			{
				consume();
				return;
			}
			throw ParseError("expected unqualified-id");
		}

		void parse_operator_function_id()
		{
			expect(KW_OPERATOR, "expected operator");
			if (peek_kind() == TK_LITERAL || peek_kind() == TK_EMPTYSTR)
			{
				consume();
				if (peek_kind() == TK_IDENTIFIER)
					consume();
				return;
			}
			if (peek_kind() == OP_LPAREN && peek_kind(1) == OP_RPAREN)
			{
				consume();
				consume();
				return;
			}
			if (peek_kind() == OP_LSQUARE && peek_kind(1) == OP_RSQUARE)
			{
				consume();
				consume();
				return;
			}
			if (peek_kind() == KW_NEW)
			{
				consume();
				if (peek_kind() == OP_LSQUARE)
				{
					consume();
					expect(OP_RSQUARE, "expected ]");
				}
				return;
			}
			if (peek_kind() == KW_DELETE)
			{
				consume();
				if (peek_kind() == OP_LSQUARE)
				{
					consume();
					expect(OP_RSQUARE, "expected ]");
				}
				return;
			}
			{
				size_t save = pos;
				int save_gt = pending_gt;
				try
				{
					parse_type_id();
					return;
				}
				catch (exception&)
				{
					pos = save;
					pending_gt = save_gt;
				}
			}
			if (peek_kind() == OP_LSHIFT || peek_kind() == OP_RSHIFT || peek_kind() == OP_LSHIFTASS || peek_kind() == OP_RSHIFTASS ||
				peek_kind() == OP_PLUS || peek_kind() == OP_MINUS || peek_kind() == OP_PLUSASS || peek_kind() == OP_MINUSASS ||
				peek_kind() == OP_STAR || peek_kind() == OP_DIV || peek_kind() == OP_MOD || peek_kind() == OP_STARASS ||
				peek_kind() == OP_DIVASS || peek_kind() == OP_MODASS || peek_kind() == OP_AMP || peek_kind() == OP_BOR ||
				peek_kind() == OP_XOR || peek_kind() == OP_BANDASS || peek_kind() == OP_BORASS || peek_kind() == OP_XORASS ||
				peek_kind() == OP_ASS || peek_kind() == OP_NE || peek_kind() == OP_EQ || peek_kind() == OP_LT ||
				peek_kind() == OP_GT || peek_kind() == OP_LE || peek_kind() == OP_GE || peek_kind() == OP_LAND ||
				peek_kind() == OP_LOR || peek_kind() == OP_INC || peek_kind() == OP_DEC || peek_kind() == OP_LNOT ||
				peek_kind() == OP_COMPL || peek_kind() == OP_DOT || peek_kind() == OP_ARROW || peek_kind() == OP_COLON2)
			{
				consume();
				return;
			}
			if (peek_kind() == KW_SIZEOF || peek_kind() == KW_ALIGNOF || peek_kind() == KW_NOEXCEPT || peek_kind() == KW_TYPEID ||
				peek_kind() == KW_NEW || peek_kind() == KW_DELETE)
			{
				consume();
				return;
			}
			throw ParseError("expected operator-function-id");
		}

		void parse_primary_expression()
		{
			if (peek_kind() == KW_TRUE || peek_kind() == KW_FALSE || peek_kind() == KW_NULLPTR || peek_kind() == KW_THIS || is_literal(peek()))
			{
				consume();
				return;
			}
			if (peek_kind() == OP_LPAREN)
			{
				consume();
				parse_expression();
				expect(OP_RPAREN, "expected )");
				return;
			}
			if (peek_kind() == OP_LSQUARE)
			{
				parse_lambda_expression();
				return;
			}
			if (peek_kind() == TK_IDENTIFIER || peek_kind() == KW_OPERATOR || peek_kind() == OP_COMPL || peek_kind() == OP_COLON2)
			{
				parse_id_expression();
				return;
			}
			throw ParseError("expected primary-expression");
		}

		void parse_lambda_expression()
		{
			expect(OP_LSQUARE, "expected [");
			if (peek_kind() != OP_RSQUARE)
			{
				parse_lambda_capture();
			}
			expect(OP_RSQUARE, "expected ]");
			if (peek_kind() == OP_LPAREN)
			{
				consume();
				if (peek_kind() != OP_RPAREN)
					parse_parameter_declaration_clause();
				expect(OP_RPAREN, "expected )");
				while (peek_kind() == KW_MUTABLE || peek_kind() == KW_CONSTEXPR)
					consume();
				if (peek_kind() == KW_THROW)
					parse_exception_specification();
				if (peek_kind() == KW_NOEXCEPT)
					parse_noexcept_specification();
				while (peek_kind() == OP_LSQUARE && peek_kind(1) == OP_LSQUARE)
					parse_attributes();
				if (peek_kind() == OP_ARROW)
					parse_trailing_return_type();
			}
			parse_compound_statement();
		}

		void parse_lambda_capture()
		{
			if (peek_kind() == OP_AMP || peek_kind() == OP_ASS)
			{
				consume();
				if (accept(OP_COMMA))
					parse_lambda_capture_list();
				return;
			}
			parse_lambda_capture_list();
		}

		void parse_lambda_capture_list()
		{
			parse_lambda_capture_item();
			while (accept(OP_COMMA))
				parse_lambda_capture_item();
		}

		void parse_lambda_capture_item()
		{
			if (peek_kind() == KW_THIS)
			{
				consume();
				return;
			}
			if (accept(OP_AMP))
			{
				expect(TK_IDENTIFIER, "expected identifier");
				if (accept(OP_DOTS))
					return;
				return;
			}
			expect(TK_IDENTIFIER, "expected identifier");
			if (accept(OP_DOTS))
				return;
		}

		void parse_postfix_expression()
		{
			parse_primary_expression();
			while (true)
			{
				if (accept(OP_LSQUARE))
				{
					if (peek_kind() == OP_LBRACE)
					{
						int depth = 0;
						do
						{
							if (peek_kind() == TK_EOF)
								throw ParseError("unterminated braced subscript");
							if (peek_kind() == OP_LBRACE)
								++depth;
							else if (peek_kind() == OP_RBRACE)
								--depth;
							consume();
						}
						while (depth > 0);
					}
					else
						parse_expression();
					expect(OP_RSQUARE, "expected ]");
				}
				else if (accept(OP_LPAREN))
				{
					if (peek_kind() != OP_RPAREN)
						parse_expression_list();
					expect(OP_RPAREN, "expected )");
				}
				else if (accept(OP_DOT))
				{
					if (accept(KW_TEMPLATE))
						parse_template_id_suffix();
					else
						parse_postfix_member_name();
				}
				else if (accept(OP_ARROW))
				{
					if (accept(KW_TEMPLATE))
						parse_template_id_suffix();
					else
						parse_postfix_member_name();
				}
				else if (accept(OP_INC) || accept(OP_DEC))
				{
				}
				else
					break;
			}
		}

		void parse_template_id_suffix()
		{
			if (peek_kind() == TK_IDENTIFIER && is_template_name(peek()))
				parse_simple_template_id();
			else
				parse_postfix_member_name();
		}

		void parse_postfix_member_name()
		{
			if (peek_kind() == OP_COMPL)
			{
				consume();
				parse_type_name();
				return;
			}
			parse_id_expression();
		}

		void parse_unary_expression()
		{
			int k = peek_kind();
			if (k == OP_INC || k == OP_DEC || k == OP_AMP || k == OP_STAR || k == OP_PLUS || k == OP_MINUS || k == OP_LNOT || k == OP_COMPL)
			{
				consume();
				parse_cast_expression();
				return;
			}
			if (k == KW_SIZEOF)
			{
				consume();
				if (accept(OP_DOTS))
				{
					expect(OP_LPAREN, "expected (");
					expect(TK_IDENTIFIER, "expected identifier");
					expect(OP_RPAREN, "expected )");
					return;
				}
				if (accept(OP_LPAREN))
				{
					size_t save = pos;
					int save_gt = pending_gt;
					try
					{
						parse_type_id();
						expect(OP_RPAREN, "expected )");
						return;
					}
					catch (exception&)
					{
						pos = save;
						pending_gt = save_gt;
					}
					parse_expression();
					expect(OP_RPAREN, "expected )");
					return;
				}
				parse_unary_expression();
				return;
			}
			if (k == KW_ALIGNOF)
			{
				consume();
				expect(OP_LPAREN, "expected (");
				parse_type_id();
				expect(OP_RPAREN, "expected )");
				return;
			}
			if (k == KW_NOEXCEPT)
			{
				parse_noexcept_expression();
				return;
			}
			if (k == KW_NEW)
			{
				parse_new_expression();
				return;
			}
			if (k == KW_DELETE)
			{
				parse_delete_expression();
				return;
			}
			parse_postfix_expression();
		}

		void parse_noexcept_expression()
		{
			expect(KW_NOEXCEPT, "expected noexcept");
			expect(OP_LPAREN, "expected (");
			parse_expression();
			expect(OP_RPAREN, "expected )");
		}

		void parse_new_expression()
		{
			accept(OP_COLON2);
			expect(KW_NEW, "expected new");
			if (accept(OP_LPAREN))
			{
				parse_expression_list();
				expect(OP_RPAREN, "expected )");
			}
			if (accept(OP_LPAREN))
			{
				parse_type_id();
				expect(OP_RPAREN, "expected )");
				if (peek_kind() == OP_LPAREN || peek_kind() == OP_LBRACE)
					parse_initializer_clause();
				return;
			}
			parse_type_id();
			if (peek_kind() == OP_LPAREN || peek_kind() == OP_LBRACE)
				parse_initializer_clause();
		}

		void parse_delete_expression()
		{
			accept(OP_COLON2);
			expect(KW_DELETE, "expected delete");
			if (accept(OP_LSQUARE))
			{
				expect(OP_RSQUARE, "expected ]");
			}
			parse_cast_expression();
		}

		void parse_cast_expression()
		{
			if (peek_kind() == OP_LPAREN)
			{
				size_t save = pos;
				int save_gt = pending_gt;
				try
				{
					consume();
					parse_type_id();
					expect(OP_RPAREN, "expected )");
					parse_cast_expression();
					return;
				}
				catch (exception&)
				{
					pos = save;
					pending_gt = save_gt;
				}
			}
			parse_unary_expression();
		}

		void parse_binary_chain(int next_prec)
		{
			(void)next_prec;
		}

		void parse_pm_expression()
		{
			parse_cast_expression();
			while (peek_kind() == OP_DOTSTAR || peek_kind() == OP_ARROWSTAR)
			{
				consume();
				parse_cast_expression();
			}
		}

		void parse_multiplicative_expression()
		{
			parse_pm_expression();
			while (peek_kind() == OP_STAR || peek_kind() == OP_DIV || peek_kind() == OP_MOD)
			{
				consume();
				parse_pm_expression();
			}
		}

		void parse_additive_expression()
		{
			parse_multiplicative_expression();
			while (peek_kind() == OP_PLUS || peek_kind() == OP_MINUS)
			{
				consume();
				parse_multiplicative_expression();
			}
		}

		void parse_shift_expression()
		{
			parse_additive_expression();
			while (peek_kind() == OP_LSHIFT || peek_kind() == OP_RSHIFT)
			{
				consume();
				parse_additive_expression();
			}
		}

		void parse_relational_expression()
		{
			parse_shift_expression();
			while (peek_kind() == OP_LT || peek_kind() == OP_GT || peek_kind() == OP_LE || peek_kind() == OP_GE)
			{
				consume();
				parse_shift_expression();
			}
		}

		void parse_equality_expression()
		{
			parse_relational_expression();
			while (peek_kind() == OP_EQ || peek_kind() == OP_NE)
			{
				consume();
				parse_relational_expression();
			}
		}

		void parse_and_expression()
		{
			parse_equality_expression();
			while (peek_kind() == OP_AMP)
			{
				consume();
				parse_equality_expression();
			}
		}

		void parse_exclusive_or_expression()
		{
			parse_and_expression();
			while (peek_kind() == OP_XOR)
			{
				consume();
				parse_and_expression();
			}
		}

		void parse_inclusive_or_expression()
		{
			parse_exclusive_or_expression();
			while (peek_kind() == OP_BOR)
			{
				consume();
				parse_exclusive_or_expression();
			}
		}

		void parse_logical_and_expression()
		{
			parse_inclusive_or_expression();
			while (peek_kind() == OP_LAND)
			{
				consume();
				parse_inclusive_or_expression();
			}
		}

		void parse_logical_or_expression()
		{
			parse_logical_and_expression();
			while (peek_kind() == OP_LOR)
			{
				consume();
				parse_logical_and_expression();
			}
		}

		void parse_conditional_expression()
		{
			parse_logical_or_expression();
			if (accept(OP_QMARK))
			{
				parse_expression();
				expect(OP_COLON, "expected :");
				parse_assignment_expression();
			}
		}

		void parse_assignment_expression()
		{
			if (peek_kind() == KW_THROW)
			{
				consume();
				parse_expression();
				return;
			}
			size_t save = pos;
			int save_gt = pending_gt;
			try
			{
				parse_conditional_expression();
				if (peek_kind() == OP_ASS || peek_kind() == OP_PLUSASS || peek_kind() == OP_MINUSASS || peek_kind() == OP_STARASS ||
					peek_kind() == OP_DIVASS || peek_kind() == OP_MODASS || peek_kind() == OP_LSHIFTASS || peek_kind() == OP_RSHIFTASS ||
					peek_kind() == OP_BANDASS || peek_kind() == OP_BORASS || peek_kind() == OP_XORASS)
				{
					consume();
					parse_initializer_clause();
				}
				return;
			}
			catch (exception&)
			{
				pos = save;
				pending_gt = save_gt;
			}
			parse_conditional_expression();
		}

		void parse_expression()
		{
			parse_assignment_expression();
			while (accept(OP_COMMA))
				parse_assignment_expression();
		}

		void parse_constant_expression()
		{
			parse_expression();
		}

		void parse_expression_list()
		{
			parse_initializer_clause();
			while (accept(OP_COMMA))
				parse_initializer_clause();
		}
	};
}

void DoRecog(const string& srcfile, istream& in)
{
	ostringstream oss;
	oss << in.rdbuf();
	string input = oss.str();
	string expanded = pa6recog::preprocess_source(srcfile, srcfile);
	vector<pa6recog::Token> toks = pa6recog::tokenize_source(expanded);
	pa6recog::Parser parser(toks);
	parser.parse();
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
		size_t nsrcfiles = args.size() - 2;

		ofstream out(outfile.c_str());
		out << "recog " << nsrcfiles << '\n';

		for (size_t i = 0; i < nsrcfiles; ++i)
		{
			const string& srcfile = args[i + 2];
			try
			{
				ifstream in(srcfile.c_str());
				if (!in.good())
					throw runtime_error("cannot open source file");
				DoRecog(srcfile, in);
				out << srcfile << " OK\n";
			}
			catch (exception& e)
			{
				cerr << e.what() << endl;
				out << srcfile << " BAD\n";
			}
			catch (...)
			{
				cerr << "parsing failed" << endl;
				out << srcfile << " BAD\n";
			}
		}
		return EXIT_SUCCESS;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
