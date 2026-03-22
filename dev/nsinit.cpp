// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

#define CPPGM_PPTOKEN_LIBRARY
#define CPPGM_MACRO_LIBRARY
#include "macro.cpp"

namespace pa8nsinit
{
	enum
	{
		TK_EOF = -1,
		TK_IDENTIFIER = -2,
		TK_LITERAL = -3
	};

	struct Token
	{
		string file;
		size_t line = 1;
		int kind = TK_EOF;
		Kind raw_kind = Kind::EOFK;
		string data;
	};

	struct ParseError : runtime_error
	{
		string file;
		size_t line = 1;

		ParseError(const string& f, size_t l, const string& what)
			: runtime_error(what), file(f), line(l)
		{
		}
	};

	struct Type;
	struct Namespace;
	struct Entity;
	typedef shared_ptr<Type> TypePtr;
	typedef shared_ptr<Entity> EntityPtr;

	struct Type
	{
		enum Kind
		{
			Fundamental,
			Cv,
			Pointer,
			LRef,
			RRef,
			ArrayUnknown,
			ArrayBound,
			Function
		};

		Kind kind = Fundamental;
		EFundamentalType fundamental = FT_INT;
		bool is_const = false;
		bool is_volatile = false;
		size_t bound = 0;
		bool variadic = false;
		vector<TypePtr> params;
		TypePtr inner;
	};

	struct PrefixOp
	{
		enum Kind
		{
			Pointer,
			LRef,
			RRef
		};

		Kind kind = Pointer;
		bool is_const = false;
		bool is_volatile = false;
	};

	struct SuffixOp
	{
		enum Kind
		{
			Array,
			Function
		};

		Kind kind = Array;
		bool unknown_bound = true;
		size_t bound = 0;
		vector<TypePtr> params;
		bool variadic = false;
	};

	struct Fragment
	{
		string name;
		bool has_name = false;
		Namespace* qualified_scope = 0;
		vector<PrefixOp> before_ops;
		vector<SuffixOp> after_ops;
		vector<string> qualified_parts;
		bool grouped = false;
		Kind name_token_kind = Kind::IDENT;
		string name_token_data;
		size_t name_line = 1;
		string name_file;
	};

	using ExprLit = Lit;

	struct Expr
	{
		enum Kind
		{
			Literal,
			Id,
			Paren
		};

		Kind kind = Literal;
		Token tok;
		vector<string> parts;
		bool leading_global = false;
		shared_ptr<Expr> inner;
	};

	struct FunctionEntity;

	struct Symbol
	{
		enum Kind
		{
			NamespaceKind,
			VariableKind,
			FunctionGroupKind,
			TypeAliasKind,
			AliasKind
		};

		string name;
		Kind kind = TypeAliasKind;
		Namespace* owner = 0;
		Namespace* ns = 0;
		TypePtr type;
		shared_ptr<Symbol> target;
		EntityPtr entity;
		vector<shared_ptr<FunctionEntity> > functions;
	};

	struct Namespace
	{
		string name;
		bool unnamed = true;
		bool inline_ns = false;
		Namespace* parent = 0;
		unordered_map<string, shared_ptr<Symbol> > symbols;
		vector<Namespace*> child_order;
		vector<Namespace*> using_directives;
		unordered_map<size_t, Namespace*> unnamed_children;
	};

	struct Entity
	{
		enum Kind
		{
			Variable,
			Function,
			Temp,
			StringLiteral
		};

		Kind kind = Variable;
		string name;
		string file;
		size_t line = 1;
		Namespace* scope = 0;
		TypePtr type;
		bool is_static = false;
		bool is_thread_local = false;
		bool is_extern = false;
		bool is_inline = false;
		bool is_constexpr = false;
		bool defined = false;
		bool is_implicit = false;
		bool is_constant = false;
		bool is_emittable = true;
		bool is_internal = false;
		bool has_initializer = false;
		bool was_forward_declared = false;
		EntityPtr address_target;
		shared_ptr<Expr> init_expr;
		vector<unsigned char> bytes;
		size_t size = 0;
		size_t align = 1;
		size_t offset = 0;
		bool offset_assigned = false;
		string debug_spec;
		string debug_init;
		string debug_value;
	};

	struct FunctionEntity : enable_shared_from_this<FunctionEntity>
	{
		Entity entity;
		TypePtr type;

		EntityPtr entity_ptr()
		{
			return EntityPtr(&entity, [](Entity*) {});
		}
	};

	struct Program
	{
		Namespace root;
		vector<EntityPtr> block1;
		vector<EntityPtr> block2;
		vector<EntityPtr> block3;
		vector<string> logs;
		unordered_set<string> once_files;
		vector<shared_ptr<Namespace> > owned_namespaces;
		size_t tu_index = 0;
	};

	struct TUContext
	{
		size_t tu_index = 0;
		string current_file;
		unordered_map<Namespace*, unordered_map<string, shared_ptr<Symbol> > > local_symbols;
		vector<EntityPtr> declared_this_tu;
		vector<string> logs;
	};

	bool is_ident_start(unsigned char c)
	{
		return c == '_' || isalpha(c);
	}

	bool is_ident_continue(unsigned char c)
	{
		return is_ident_start(c) || isdigit(c);
	}

	bool is_keyword_ident(const string& s)
	{
		return StringToTokenTypeMap.find(s) != StringToTokenTypeMap.end();
	}

	string trim_left(const string& s)
	{
		size_t i = 0;
		while (i < s.size() && isspace(static_cast<unsigned char>(s[i])))
			++i;
		return s.substr(i);
	}

	string trim_right(const string& s)
	{
		size_t i = s.size();
		while (i > 0 && isspace(static_cast<unsigned char>(s[i - 1])))
			--i;
		return s.substr(0, i);
	}

	string trim(const string& s)
	{
		return trim_right(trim_left(s));
	}

	string join_path(const string& base, const string& rel)
	{
		if (rel.empty() || rel[0] == '/')
			return rel;
		size_t slash = base.rfind('/');
		if (slash == string::npos)
			return rel;
		return base.substr(0, slash + 1) + rel;
	}

	string read_file(const string& path)
	{
		ifstream in(path.c_str(), ios::in | ios::binary);
		if (!in)
			throw runtime_error(string("include file not found: ") + path);
		ostringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	TypePtr make_type()
	{
		return TypePtr(new Type());
	}

	TypePtr make_fundamental(EFundamentalType ft)
	{
		TypePtr t(new Type());
		t->kind = Type::Fundamental;
		t->fundamental = ft;
		return t;
	}

	TypePtr make_cv(TypePtr inner, bool is_const, bool is_volatile)
	{
		if (!inner)
			return inner;
		if (!is_const && !is_volatile)
			return inner;
		if (inner->kind == Type::Cv)
		{
			inner->is_const = inner->is_const || is_const;
			inner->is_volatile = inner->is_volatile || is_volatile;
			return inner;
		}
		TypePtr t(new Type());
		t->kind = Type::Cv;
		t->is_const = is_const;
		t->is_volatile = is_volatile;
		t->inner = inner;
		return t;
	}

	TypePtr make_pointer(TypePtr inner)
	{
		if (inner && (inner->kind == Type::LRef || inner->kind == Type::RRef))
			throw runtime_error("pointer to that type not allowed");
		TypePtr t(new Type());
		t->kind = Type::Pointer;
		t->inner = inner;
		return t;
	}

	TypePtr make_lref(TypePtr inner)
	{
		if (inner && (inner->kind == Type::LRef || inner->kind == Type::RRef))
			throw runtime_error("reference to reference in declarator");
		if (inner && inner->kind == Type::Fundamental && inner->fundamental == FT_VOID)
			throw runtime_error("invalid type for reference to");
		TypePtr t(new Type());
		t->kind = Type::LRef;
		t->inner = inner;
		return t;
	}

	TypePtr make_rref(TypePtr inner)
	{
		if (inner && (inner->kind == Type::LRef || inner->kind == Type::RRef))
			throw runtime_error("reference to reference in declarator");
		if (inner && inner->kind == Type::Fundamental && inner->fundamental == FT_VOID)
			throw runtime_error("invalid type for reference to");
		TypePtr t(new Type());
		t->kind = Type::RRef;
		t->inner = inner;
		return t;
	}

	TypePtr make_array_unknown(TypePtr inner)
	{
		TypePtr t(new Type());
		t->kind = Type::ArrayUnknown;
		t->inner = inner;
		return t;
	}

	TypePtr make_array_bound(TypePtr inner, size_t bound)
	{
		TypePtr t(new Type());
		t->kind = Type::ArrayBound;
		t->inner = inner;
		t->bound = bound;
		return t;
	}

	TypePtr make_function(TypePtr ret, const vector<TypePtr>& params, bool variadic)
	{
		TypePtr t(new Type());
		t->kind = Type::Function;
		t->inner = ret;
		t->params = params;
		t->variadic = variadic;
		return t;
	}

	bool is_reference_type(const TypePtr& t)
	{
		return t && (t->kind == Type::LRef || t->kind == Type::RRef);
	}

	TypePtr strip_top_cv(TypePtr t)
	{
		while (t && t->kind == Type::Cv)
			t = t->inner;
		return t;
	}

	TypePtr type_decay(TypePtr t)
	{
		t = strip_top_cv(t);
		if (!t)
			return t;
		if (t->kind == Type::ArrayUnknown || t->kind == Type::ArrayBound)
			return make_pointer(t->inner);
		if (t->kind == Type::Function)
			return make_pointer(t);
		return t;
	}

	size_t type_align(const TypePtr& t);

	size_t type_size(const TypePtr& t)
	{
		if (!t)
			return 0;
		switch (t->kind)
		{
		case Type::Cv:
			return type_size(t->inner);
		case Type::Fundamental:
			switch (t->fundamental)
			{
			case FT_SIGNED_CHAR:
			case FT_UNSIGNED_CHAR:
			case FT_CHAR:
			case FT_BOOL:
				return 1;
			case FT_SHORT_INT:
			case FT_UNSIGNED_SHORT_INT:
			case FT_CHAR16_T:
				return 2;
			case FT_INT:
			case FT_UNSIGNED_INT:
			case FT_WCHAR_T:
			case FT_CHAR32_T:
			case FT_FLOAT:
				return 4;
			case FT_LONG_INT:
			case FT_UNSIGNED_LONG_INT:
			case FT_LONG_LONG_INT:
			case FT_UNSIGNED_LONG_LONG_INT:
			case FT_DOUBLE:
			case FT_NULLPTR_T:
				return 8;
			case FT_LONG_DOUBLE:
				return 16;
			case FT_VOID:
				return 0;
			}
			return 0;
		case Type::Pointer:
		case Type::LRef:
		case Type::RRef:
			return 8;
		case Type::ArrayUnknown:
			return 0;
		case Type::ArrayBound:
			return t->bound * type_size(t->inner);
		case Type::Function:
			return 4;
		}
		return 0;
	}

	size_t type_align(const TypePtr& t)
	{
		if (!t)
			return 0;
		switch (t->kind)
		{
		case Type::Cv:
			return type_align(t->inner);
		case Type::Fundamental:
			switch (t->fundamental)
			{
			case FT_SIGNED_CHAR:
			case FT_UNSIGNED_CHAR:
			case FT_CHAR:
			case FT_BOOL:
				return 1;
			case FT_SHORT_INT:
			case FT_UNSIGNED_SHORT_INT:
			case FT_CHAR16_T:
				return 2;
			case FT_INT:
			case FT_UNSIGNED_INT:
			case FT_WCHAR_T:
			case FT_CHAR32_T:
			case FT_FLOAT:
				return 4;
			case FT_LONG_INT:
			case FT_UNSIGNED_LONG_INT:
			case FT_LONG_LONG_INT:
			case FT_UNSIGNED_LONG_LONG_INT:
			case FT_DOUBLE:
			case FT_NULLPTR_T:
				return 8;
			case FT_LONG_DOUBLE:
				return 16;
			case FT_VOID:
				return 0;
			}
			return 0;
		case Type::Pointer:
		case Type::LRef:
		case Type::RRef:
			return 8;
		case Type::ArrayUnknown:
			return 0;
		case Type::ArrayBound:
			return type_align(t->inner);
		case Type::Function:
			return 4;
		}
		return 0;
	}

	string hex_bytes(const vector<unsigned char>& bytes)
	{
		ostringstream oss;
		oss << uppercase << hex << setfill('0');
		for (size_t i = 0; i < bytes.size(); ++i)
			oss << setw(2) << static_cast<unsigned>(bytes[i]);
		return oss.str();
	}

	string fundamental_name(EFundamentalType ft)
	{
		map<EFundamentalType, string>::const_iterator it = FundamentalTypeToStringMap.find(ft);
		if (it != FundamentalTypeToStringMap.end())
			return it->second;
		return string();
	}

	string type_to_string(const TypePtr& t)
	{
		if (!t)
			return string();
		switch (t->kind)
		{
		case Type::Fundamental:
			return fundamental_name(t->fundamental);
		case Type::Cv:
		{
			string inner = type_to_string(t->inner);
			if (t->is_const && t->is_volatile)
				return string("const volatile ") + inner;
			if (t->is_const)
				return string("const ") + inner;
			if (t->is_volatile)
				return string("volatile ") + inner;
			return inner;
		}
		case Type::Pointer:
			return string("pointer to ") + type_to_string(t->inner);
		case Type::LRef:
			return string("lvalue-reference to ") + type_to_string(t->inner);
		case Type::RRef:
			return string("rvalue-reference to ") + type_to_string(t->inner);
		case Type::ArrayUnknown:
			return string("array of unknown bound of ") + type_to_string(t->inner);
		case Type::ArrayBound:
			return string("array of ") + to_string(t->bound) + " " + type_to_string(t->inner);
		case Type::Function:
		{
			string s = "function of (";
			for (size_t i = 0; i < t->params.size(); ++i)
			{
				if (i)
					s += ", ";
				s += type_to_string(t->params[i]);
			}
			if (t->variadic)
			{
				if (!t->params.empty())
					s += ", ";
				s += "...";
			}
			s += ") returning ";
			s += type_to_string(t->inner);
			return s;
		}
		}
		return string();
	}

	string type_spec_name(const TypePtr& t)
	{
		if (!t)
			return string();
		TypePtr base = strip_top_cv(t);
		if (!base)
			return string();
		if (base->kind == Type::Fundamental)
			return fundamental_name(base->fundamental);
		return type_to_string(base);
	}

	string specifier_string(bool is_inline, bool is_constexpr, bool is_static, bool is_thread_local, bool is_extern,
	                       bool is_const, bool is_volatile, const TypePtr& base)
	{
		vector<string> parts;
		if (is_inline)
			parts.push_back("SP_INLINE");
		if (is_constexpr)
			parts.push_back("SP_CONSTEXPR");
		if (is_static)
			parts.push_back("SP_STATIC");
		if (is_thread_local)
			parts.push_back("SP_THREAD_LOCAL");
		if (is_extern)
			parts.push_back("SP_EXTERN");
		if (is_const)
			parts.push_back("SP_CONST");
		if (is_volatile)
			parts.push_back("SP_VOLATILE");
		if (base && base->kind == Type::Fundamental)
		{
			string n = fundamental_name(base->fundamental);
			if (base->fundamental == FT_LONG_INT)
				parts.push_back("SP_LONG_1");
			else if (base->fundamental == FT_LONG_LONG_INT)
				parts.push_back("SP_LONG_2");
			else if (base->fundamental == FT_UNSIGNED_LONG_INT)
				parts.push_back("SP_LONG_1");
			else if (base->fundamental == FT_UNSIGNED_LONG_LONG_INT)
				parts.push_back("SP_LONG_2");
			if (base->fundamental == FT_CHAR)
				parts.push_back("SP_CHAR");
			else if (base->fundamental == FT_SHORT_INT)
				parts.push_back("SP_SHORT");
			else if (base->fundamental == FT_INT)
				parts.push_back("SP_INT");
			else if (base->fundamental == FT_BOOL)
				parts.push_back("SP_BOOL");
			else if (base->fundamental == FT_FLOAT)
				parts.push_back("SP_FLOAT");
			else if (base->fundamental == FT_DOUBLE)
				parts.push_back("SP_DOUBLE");
			else if (base->fundamental == FT_LONG_DOUBLE)
				parts.push_back("SP_LONG_1|SP_DOUBLE");
			else if (base->fundamental == FT_VOID)
				parts.push_back("SP_VOID");
			else if (base->fundamental == FT_CHAR16_T)
				parts.push_back("SP_CHAR16_T");
			else if (base->fundamental == FT_CHAR32_T)
				parts.push_back("SP_CHAR32_T");
			else if (base->fundamental == FT_WCHAR_T)
				parts.push_back("SP_WCHAR_T");
			else if (base->fundamental == FT_SIGNED_CHAR)
				parts.push_back("SP_CHAR|SP_SIGNED");
			else if (base->fundamental == FT_UNSIGNED_CHAR)
				parts.push_back("SP_CHAR|SP_UNSIGNED");
			else if (base->fundamental == FT_UNSIGNED_SHORT_INT)
				parts.push_back("SP_SHORT|SP_INT|SP_UNSIGNED");
			else if (base->fundamental == FT_UNSIGNED_INT)
				parts.push_back("SP_INT|SP_UNSIGNED");
			else if (base->fundamental == FT_UNSIGNED_LONG_INT)
				parts.push_back("SP_LONG_1|SP_INT|SP_UNSIGNED");
			else if (base->fundamental == FT_UNSIGNED_LONG_LONG_INT)
				parts.push_back("SP_LONG_2|SP_INT|SP_UNSIGNED");
			else if (base->fundamental == FT_NULLPTR_T)
				parts.push_back("SP_NULLPTR");
			else if (base->fundamental == FT_LONG_INT)
				parts.push_back("SP_LONG_1|SP_INT");
			else if (base->fundamental == FT_LONG_LONG_INT)
				parts.push_back("SP_LONG_2|SP_INT");
		}
		if (parts.empty())
			return string();
		string out = parts[0];
		for (size_t i = 1; i < parts.size(); ++i)
			out += "|" + parts[i];
		return out;
	}

	struct TokenStream
	{
		vector<Token> toks;
		size_t pos = 0;

		int peek_kind(size_t off = 0) const
		{
			if (pos + off >= toks.size())
				return TK_EOF;
			return toks[pos + off].kind;
		}

		const Token& peek(size_t off = 0) const
		{
			static Token eof;
			return pos + off < toks.size() ? toks[pos + off] : eof;
		}

		void consume()
		{
			if (pos < toks.size())
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
			if (peek_kind() == TK_IDENTIFIER && peek().data == s)
			{
				consume();
				return true;
			}
			return false;
		}

		bool at_end() const
		{
			return peek_kind() == TK_EOF;
		}
	};

	Namespace* global_namespace(Namespace* ns)
	{
		while (ns && ns->parent)
			ns = ns->parent;
		return ns;
	}

	Namespace* find_namespace_path_from_root(Namespace* root, const vector<string>& parts)
	{
		Namespace* cur = root;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			if (!cur)
				return 0;
			unordered_map<string, shared_ptr<Symbol> >::iterator it = cur->symbols.find(parts[i]);
			if (it == cur->symbols.end() || !it->second || it->second->kind != Symbol::NamespaceKind)
				return 0;
			cur = it->second->ns;
		}
		return cur;
	}

	Namespace* find_namespace_path_from_root(Namespace* root, const vector<string>& parts);
	void append_block1_entity(Program* prog, const EntityPtr& e);

	Namespace* resolve_decl_qualifier(Namespace* scope, const vector<string>& parts, bool leading_global)
	{
		if (parts.empty())
			return scope;
		Namespace* root = global_namespace(scope);
		Namespace* candidate = find_namespace_path_from_root(root, parts);
		if (!candidate)
			return 0;
		if (scope == root)
			return candidate;

		for (Namespace* cur = scope; cur && cur != root; cur = cur->parent)
		{
			if (cur == candidate)
				return candidate;
		}
		return 0;
	}

	bool is_enclosing_namespace(Namespace* outer, Namespace* inner)
	{
		for (Namespace* cur = inner; cur; cur = cur->parent)
		{
			if (cur == outer)
				return true;
		}
		return false;
	}

	struct ParsedEntity
	{
		EntityPtr entity;
		bool new_entity = false;
	};

	struct Parser
	{
		TokenStream ts;
		Program* prog = 0;
		TUContext* ctx = 0;
		Namespace* root = 0;
		Namespace* cur = 0;
		size_t tu_index = 0;

		Parser(const vector<Token>& toks, Program* p, TUContext* c, Namespace* root_ns)
		{
			ts.toks = toks;
			prog = p;
			ctx = c;
			root = root_ns;
			cur = root_ns;
			tu_index = c->tu_index;
		}

		string current_file() const
		{
			return ts.at_end() ? ctx->current_file : ts.peek().file;
		}

		size_t current_line() const
		{
			return ts.at_end() ? 1 : ts.peek().line;
		}

		void error_here(const string& msg) const
		{
			throw ParseError(current_file(), current_line(), msg);
		}

		bool accept(int kind)
		{
			return ts.accept(kind);
		}

		bool accept_ident(const string& s)
		{
			return ts.accept_ident(s);
		}

		bool is_decl_start() const
		{
			int k = ts.peek_kind();
			return k == OP_SEMICOLON || k == KW_INLINE || k == KW_NAMESPACE || k == KW_USING ||
			       k == KW_TYPEDEF || k == KW_STATIC || k == KW_EXTERN || k == KW_THREAD_LOCAL ||
			       k == KW_CONST || k == KW_VOLATILE || k == KW_SIGNED || k == KW_UNSIGNED ||
			       k == KW_SHORT || k == KW_LONG || k == KW_CHAR || k == KW_CHAR16_T ||
			       k == KW_CHAR32_T || k == KW_WCHAR_T || k == KW_BOOL || k == KW_FLOAT ||
			       k == KW_DOUBLE || k == KW_VOID || k == KW_CONSTEXPR || k == TK_IDENTIFIER;
		}

		bool is_expr_start() const
		{
			int k = ts.peek_kind();
			return k == TK_IDENTIFIER || k == OP_COLON2 || k == OP_LPAREN || k == KW_TRUE ||
			       k == KW_FALSE || k == KW_NULLPTR || k == TK_LITERAL;
		}

		void parse_translation_unit()
		{
			while (!ts.at_end())
				parse_declaration(cur);
		}

		void parse_declaration(Namespace* scope)
		{
			if (accept(OP_SEMICOLON))
				return;
			if (ts.peek_kind() == KW_NAMESPACE && ts.peek_kind(1) == TK_IDENTIFIER && ts.peek_kind(2) == OP_ASS)
			{
				parse_namespace_alias_definition(scope);
				return;
			}
			if (ts.peek_kind() == KW_NAMESPACE || (ts.peek_kind() == KW_INLINE && ts.peek_kind(1) == KW_NAMESPACE))
			{
				parse_namespace_definition(scope);
				return;
			}
			if (ts.peek_kind() == KW_USING)
			{
				parse_using_or_alias(scope);
				return;
			}
			if (ts.peek_kind() == KW_STATIC_ASSERT)
			{
				parse_static_assert(scope);
				return;
			}
			parse_simple_or_function_declaration(scope);
		}

		void parse_namespace_definition(Namespace* scope)
		{
			bool is_inline = false;
			if (accept(KW_INLINE))
				is_inline = true;
			if (!accept(KW_NAMESPACE))
				error_here("expected namespace");

			string name;
			bool has_name = false;
			Token name_tok;
			if (ts.peek_kind() == TK_IDENTIFIER)
			{
				name = ts.peek().data;
				name_tok = ts.peek();
				ts.consume();
				has_name = true;
			}

			Namespace* ns = 0;
			if (has_name)
			{
				unordered_map<string, shared_ptr<Symbol> >::iterator it = scope->symbols.find(name);
				if (it != scope->symbols.end())
				{
					shared_ptr<Symbol> sym = it->second;
					if (!sym || sym->kind != Symbol::NamespaceKind)
						throw ParseError(name_tok.file, name_tok.line, string(name) + " already exists");
					ns = sym->ns;
					if (is_inline && !ns->inline_ns)
						throw ParseError(name_tok.file, name_tok.line, "extension namespace cannot be inline");
				}
				else
				{
					shared_ptr<Namespace> child(new Namespace());
					child->name = name;
					child->unnamed = false;
					child->inline_ns = is_inline;
					child->parent = scope;
					ns = child.get();
					prog->owned_namespaces.push_back(child);
					scope->child_order.push_back(ns);

					shared_ptr<Symbol> sym(new Symbol());
					sym->name = name;
					sym->kind = Symbol::NamespaceKind;
					sym->owner = scope;
					sym->ns = ns;
					scope->symbols[name] = sym;
				}
			}
			else
			{
				Namespace*& child = scope->unnamed_children[tu_index];
				if (!child)
				{
					shared_ptr<Namespace> owned(new Namespace());
					owned->unnamed = true;
					owned->inline_ns = is_inline;
					owned->parent = scope;
					ns = owned.get();
					prog->owned_namespaces.push_back(owned);
					child = ns;
					scope->child_order.push_back(ns);
					// keep alive through parent symbol table
					shared_ptr<Symbol> sym(new Symbol());
					sym->name = string();
					sym->kind = Symbol::NamespaceKind;
					sym->owner = scope;
					sym->ns = ns;
					string key = string("<unnamed-") + to_string(tu_index) + ">";
					scope->symbols[key] = sym;
				}
				else
				{
					ns = child;
				}
			}

			if (!accept(OP_LBRACE))
				error_here("expected {");

			Namespace* save = cur;
			cur = ns;
			while (ts.peek_kind() != OP_RBRACE && !ts.at_end())
				parse_declaration(cur);
			if (!accept(OP_RBRACE))
				error_here("expected }");
			cur = save;
			accept(OP_SEMICOLON);
		}

		void parse_namespace_alias_definition(Namespace* scope)
		{
			if (!accept(KW_NAMESPACE))
				error_here("expected namespace");
			if (ts.peek_kind() != TK_IDENTIFIER)
				error_here("expected identifier");
			Token alias_tok = ts.peek();
			string alias_name = ts.peek().data;
			ts.consume();
			if (!accept(OP_ASS))
				error_here("expected =");
			bool leading_global = false;
			vector<string> parts;
			if (accept(OP_COLON2))
				leading_global = true;
			while (true)
			{
				if (ts.peek_kind() != TK_IDENTIFIER)
					error_here("expected namespace name");
				string part = ts.peek().data;
				ts.consume();
				if (!accept(OP_COLON2))
				{
					parts.push_back(part);
					break;
				}
				parts.push_back(part);
			}
			Namespace* target = lookup_namespace_path(scope, parts, leading_global);
			if (!target)
				throw ParseError(alias_tok.file, alias_tok.line, alias_name + " not found");
			if (!accept(OP_SEMICOLON))
				error_here("expected ;");

			unordered_map<string, shared_ptr<Symbol> >::iterator it = scope->symbols.find(alias_name);
			if (it != scope->symbols.end())
				throw ParseError(alias_tok.file, alias_tok.line, alias_name + " already exists");
			if (target->name == alias_name)
				throw ParseError(alias_tok.file, alias_tok.line, alias_name + " is an original-namespace-name");

			shared_ptr<Symbol> sym(new Symbol());
			sym->name = alias_name;
			sym->kind = Symbol::AliasKind;
			sym->owner = scope;
			shared_ptr<Symbol> tgt(new Symbol());
			tgt->kind = Symbol::NamespaceKind;
			tgt->ns = target;
			sym->target = tgt;
			scope->symbols[alias_name] = sym;
		}

		void parse_using_or_alias(Namespace* scope)
		{
			if (!accept(KW_USING))
				error_here("expected using");
			if (accept(KW_NAMESPACE))
			{
				bool leading_global = false;
				vector<string> parts;
				if (accept(OP_COLON2))
					leading_global = true;
				while (true)
				{
					if (ts.peek_kind() != TK_IDENTIFIER)
						error_here("expected namespace name");
					string part = ts.peek().data;
					ts.consume();
					if (!accept(OP_COLON2))
					{
						parts.push_back(part);
						break;
					}
					parts.push_back(part);
				}
				Namespace* ns = lookup_namespace_path(scope, parts, leading_global);
				if (!ns)
					error_here("expected namespace name");
				scope->using_directives.push_back(ns);
				if (!accept(OP_SEMICOLON))
					error_here("expected ;");
				return;
			}

			if (ts.peek_kind() == TK_IDENTIFIER && ts.peek_kind(1) == OP_ASS)
			{
				string alias_name = ts.peek().data;
				Token alias_tok = ts.peek();
				ts.consume();
				ts.consume();
				TypePtr aliased = parse_type_id(scope);
				if (!accept(OP_SEMICOLON))
					error_here("expected ;");
				shared_ptr<Symbol> sym(new Symbol());
				sym->name = alias_name;
				sym->kind = Symbol::TypeAliasKind;
				sym->owner = scope;
				sym->type = aliased;
				scope->symbols[alias_name] = sym;
				return;
			}

			bool leading_global = false;
			vector<string> parts;
			if (accept(OP_COLON2))
				leading_global = true;
			while (true)
			{
				if (ts.peek_kind() != TK_IDENTIFIER)
					error_here("expected identifier");
				string part = ts.peek().data;
				ts.consume();
				if (!accept(OP_COLON2))
				{
					parts.push_back(part);
					break;
				}
				parts.push_back(part);
			}
			if (parts.empty())
				error_here("expected identifier");
			string member = parts.back();
			parts.pop_back();
			Namespace* ns = lookup_namespace_path(scope, parts, leading_global);
			if (!ns)
				error_here("expected namespace name");
			shared_ptr<Symbol> target = lookup_name(ns, member, false, false, false);
			if (!target || target->kind == Symbol::NamespaceKind)
				throw ParseError(current_file(), current_line(), member + " not found");
			shared_ptr<Symbol> sym(new Symbol());
			sym->name = member;
			sym->kind = Symbol::AliasKind;
			sym->owner = scope;
			sym->target = target;
			scope->symbols[member] = sym;
			if (!accept(OP_SEMICOLON))
				error_here("expected ;");
		}

		void parse_static_assert(Namespace* scope)
		{
			Token kw = ts.peek();
			if (!accept(KW_STATIC_ASSERT))
				error_here("expected static_assert");
			if (!accept(OP_LPAREN))
				error_here("expected (");
			shared_ptr<Expr> cond = parse_expression(scope);
			if (!accept(OP_COMMA))
				error_here("expected ,");
			if (ts.peek_kind() != TK_LITERAL)
				error_here("expected literal");
			Token msg = ts.peek();
			ts.consume();
			if (!accept(OP_RPAREN))
				error_here("expected )");
			if (!accept(OP_SEMICOLON))
				error_here("expected ;");
			ExprValue v = eval_constant_expression(scope, cond, true);
			if (!is_truthy(v))
				throw ParseError(kw.file, kw.line, "static_assert on non-constant expression");
		}

		struct DeclSpec
		{
			bool is_typedef = false;
			bool is_inline = false;
			bool is_static = false;
			bool is_thread_local = false;
			bool is_extern = false;
			bool is_constexpr = false;
			bool has_c = false;
			bool has_v = false;
			bool has_unsigned = false;
			bool has_signed = false;
			int long_count = 0;
			bool has_short = false;
			bool has_char = false;
			bool has_wchar = false;
			bool has_char16 = false;
			bool has_char32 = false;
			bool has_bool = false;
			bool has_float = false;
			bool has_double = false;
			bool has_void = false;
			bool has_alias = false;
			TypePtr alias_type;
			bool has_primary = false;
			Token first_tok;
			bool has_first_tok = false;
		};

		DeclSpec parse_decl_specifiers(Namespace* scope, bool forced_typedef)
		{
			DeclSpec ds;
			ds.is_typedef = forced_typedef;
			bool saw_any = false;
			while (true)
			{
				int k = ts.peek_kind();
				if (k == KW_TYPEDEF)
				{
					ds.is_typedef = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_STATIC)
				{
					ds.is_static = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_THREAD_LOCAL)
				{
					ds.is_thread_local = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_EXTERN)
				{
					ds.is_extern = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_INLINE)
				{
					ds.is_inline = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_CONSTEXPR)
				{
					ds.is_constexpr = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_CONST)
				{
					ds.has_c = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_VOLATILE)
				{
					ds.has_v = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_UNSIGNED)
				{
					ds.has_unsigned = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_SIGNED)
				{
					ds.has_signed = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_SHORT)
				{
					ds.has_short = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_LONG)
				{
					++ds.long_count;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_CHAR)
				{
					ds.has_char = true;
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_CHAR16_T)
				{
					ds.has_char16 = true;
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_CHAR32_T)
				{
					ds.has_char32 = true;
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_WCHAR_T)
				{
					ds.has_wchar = true;
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_BOOL)
				{
					ds.has_bool = true;
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_FLOAT)
				{
					ds.has_float = true;
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_DOUBLE)
				{
					ds.has_double = true;
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_VOID)
				{
					ds.has_void = true;
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (k == KW_INT)
				{
					ds.has_primary = true;
					saw_any = true;
					ts.consume();
					continue;
				}
				if (!ds.has_primary)
				{
					size_t save = ts.pos;
					TypePtr alias = try_parse_type_name(scope);
					if (alias)
					{
						ds.has_alias = true;
						ds.alias_type = alias;
						ds.has_primary = true;
						saw_any = true;
						continue;
					}
					ts.pos = save;
				}
				break;
			}
			if (!saw_any)
				error_here("expected declaration specifiers");
			return ds;
		}

		TypePtr try_parse_type_name(Namespace* scope)
		{
			size_t save = ts.pos;
			bool leading_global = false;
			vector<string> parts;
			if (accept(OP_COLON2))
				leading_global = true;
			if (ts.peek_kind() != TK_IDENTIFIER)
			{
				ts.pos = save;
				return TypePtr();
			}
			while (true)
			{
				string part = ts.peek().data;
				ts.consume();
				if (!accept(OP_COLON2))
				{
					parts.push_back(part);
					break;
				}
				parts.push_back(part);
				if (ts.peek_kind() != TK_IDENTIFIER)
				{
					ts.pos = save;
					return TypePtr();
				}
			}
			TypePtr aliased;
			if (parts.size() == 1 && !leading_global)
			{
				shared_ptr<Symbol> sym = lookup_name(scope, parts[0], false, true, true);
				if (sym && sym->kind == Symbol::TypeAliasKind)
					aliased = sym->type;
			}
			else
			{
				Namespace* ns = lookup_namespace_path(scope, vector<string>(parts.begin(), parts.end() - 1), leading_global);
				if (ns)
				{
					shared_ptr<Symbol> sym = lookup_name(ns, parts.back(), false, true, true);
					if (sym && sym->kind == Symbol::TypeAliasKind)
						aliased = sym->type;
				}
			}
			if (!aliased)
			{
				ts.pos = save;
				return TypePtr();
			}
			return aliased;
		}

		TypePtr build_fundamental_type(const DeclSpec& ds)
		{
			if (ds.has_alias)
				return ds.alias_type;
			if (ds.has_char16)
				return make_fundamental(FT_CHAR16_T);
			if (ds.has_char32)
				return make_fundamental(FT_CHAR32_T);
			if (ds.has_wchar)
				return make_fundamental(FT_WCHAR_T);
			if (ds.has_bool)
				return make_fundamental(FT_BOOL);
			if (ds.has_float)
				return make_fundamental(FT_FLOAT);
			if (ds.has_double)
			{
				if (ds.long_count > 0)
					return make_fundamental(FT_LONG_DOUBLE);
				return make_fundamental(FT_DOUBLE);
			}
			if (ds.has_void)
				return make_fundamental(FT_VOID);
			if (ds.has_char)
			{
				if (ds.has_unsigned)
					return make_fundamental(FT_UNSIGNED_CHAR);
				if (ds.has_signed)
					return make_fundamental(FT_SIGNED_CHAR);
				return make_fundamental(FT_CHAR);
			}
			if (ds.has_short)
			{
				if (ds.has_unsigned)
					return make_fundamental(FT_UNSIGNED_SHORT_INT);
				return make_fundamental(FT_SHORT_INT);
			}
			if (ds.long_count >= 2)
			{
				if (ds.has_unsigned)
					return make_fundamental(FT_UNSIGNED_LONG_LONG_INT);
				return make_fundamental(FT_LONG_LONG_INT);
			}
			if (ds.long_count == 1)
			{
				if (ds.has_unsigned)
					return make_fundamental(FT_UNSIGNED_LONG_INT);
				return make_fundamental(FT_LONG_INT);
			}
			if (ds.has_unsigned)
				return make_fundamental(FT_UNSIGNED_INT);
			return make_fundamental(FT_INT);
		}

		vector<PrefixOp> parse_prefix_ops()
		{
			vector<PrefixOp> prefixes;
			while (true)
			{
				if (ts.peek_kind() == OP_STAR)
				{
					PrefixOp op;
					op.kind = PrefixOp::Pointer;
					ts.consume();
					while (ts.peek_kind() == KW_CONST || ts.peek_kind() == KW_VOLATILE)
					{
						if (ts.peek_kind() == KW_CONST)
							op.is_const = true;
						if (ts.peek_kind() == KW_VOLATILE)
							op.is_volatile = true;
						ts.consume();
					}
					prefixes.push_back(op);
					continue;
				}
				if (ts.peek_kind() == OP_AMP)
				{
					PrefixOp op;
					op.kind = PrefixOp::LRef;
					ts.consume();
					prefixes.push_back(op);
					continue;
				}
				if (ts.peek_kind() == OP_LAND)
				{
					PrefixOp op;
					op.kind = PrefixOp::RRef;
					ts.consume();
					prefixes.push_back(op);
					continue;
				}
				break;
			}
			return prefixes;
		}

		void parse_id_expression(Namespace* scope, Fragment& frag)
		{
			bool leading_global = false;
			vector<string> parts;
			if (accept(OP_COLON2))
				leading_global = true;
			while (true)
			{
				if (ts.peek_kind() != TK_IDENTIFIER)
					error_here("expected identifier");
				Token tok = ts.peek();
				string part = tok.data;
				ts.consume();
				if (!accept(OP_COLON2))
				{
					parts.push_back(part);
					frag.name_token_kind = tok.raw_kind;
					frag.name_token_data = tok.data;
					frag.name_line = tok.line;
					frag.name_file = tok.file;
					break;
				}
				parts.push_back(part);
			}
			if (parts.empty())
				error_here("expected identifier");
			frag.name = parts.back();
			frag.has_name = true;
			parts.pop_back();
			frag.qualified_parts = parts;
			if (parts.empty())
			{
				frag.qualified_scope = leading_global ? global_namespace(scope) : scope;
				return;
			}
			Namespace* ns = resolve_decl_qualifier(scope, parts, leading_global);
			if (!ns)
				throw ParseError(frag.name_file, frag.name_line, "qualified name not from enclosed namespace");
			frag.qualified_scope = ns;
		}

		vector<SuffixOp> parse_suffixes(Namespace* scope)
		{
			vector<SuffixOp> suffixes;
			while (true)
			{
				if (ts.peek_kind() == OP_LPAREN)
				{
					SuffixOp op;
					op.kind = SuffixOp::Function;
					ts.consume();
					parse_parameter_clause(scope, op.params, op.variadic);
					if (!accept(OP_RPAREN))
						error_here("expected )");
					suffixes.push_back(op);
					continue;
				}
				if (ts.peek_kind() == OP_LSQUARE)
				{
					SuffixOp op;
					op.kind = SuffixOp::Array;
					ts.consume();
					if (ts.peek_kind() != OP_RSQUARE)
					{
						shared_ptr<Expr> bound_expr = parse_expression(scope);
						ExprValue bound_value = eval_constant_expression(scope, bound_expr);
						if (bound_value.bytes.empty())
							error_here("expected literal");
						size_t bound = 0;
						memcpy(&bound, &bound_value.bytes[0], min(sizeof(bound), bound_value.bytes.size()));
						op.unknown_bound = false;
						op.bound = bound;
					}
					if (!accept(OP_RSQUARE))
						error_here("expected ]");
					suffixes.push_back(op);
					continue;
				}
				break;
			}
			return suffixes;
		}

		Fragment parse_declarator_fragment(Namespace* scope, bool allow_empty_name)
		{
			vector<PrefixOp> prefixes = parse_prefix_ops();
			Fragment frag;
			frag.before_ops = prefixes;

			bool looks_like_suffix_only = allow_empty_name && ts.peek_kind() == OP_LPAREN &&
				ts.peek_kind(1) != OP_STAR && ts.peek_kind(1) != OP_AMP && ts.peek_kind(1) != OP_LAND &&
				ts.peek_kind(1) != OP_LPAREN;

			if (looks_like_suffix_only)
			{
				// no name; suffixes attached to an abstract declarator
			}
			else if (ts.peek_kind() == OP_LPAREN)
			{
				ts.consume();
				frag = parse_declarator_fragment(scope, true);
				if (!accept(OP_RPAREN))
					error_here("expected )");
				frag.grouped = true;
				if (!prefixes.empty())
				{
					vector<PrefixOp> merged;
					merged.insert(merged.end(), prefixes.begin(), prefixes.end());
					merged.insert(merged.end(), frag.before_ops.begin(), frag.before_ops.end());
					frag.before_ops.swap(merged);
				}
			}
			else if (ts.peek_kind() == TK_IDENTIFIER || ts.peek_kind() == OP_COLON2)
			{
				parse_id_expression(scope, frag);
				frag.before_ops = prefixes;
			}
			else if (!allow_empty_name)
			{
				error_here("expected declarator");
			}

			Namespace* suffix_scope = frag.qualified_scope ? frag.qualified_scope : scope;
			vector<SuffixOp> suffixes = parse_suffixes(suffix_scope);
			frag.after_ops.insert(frag.after_ops.end(), suffixes.begin(), suffixes.end());
			return frag;
		}

		TypePtr apply_prefix(const PrefixOp& op, TypePtr t)
		{
			switch (op.kind)
			{
			case PrefixOp::Pointer:
				t = make_pointer(t);
				return make_cv(t, op.is_const, op.is_volatile);
			case PrefixOp::LRef:
				return make_lref(t);
			case PrefixOp::RRef:
				return make_rref(t);
			}
			return t;
		}

		TypePtr apply_suffix(const SuffixOp& op, TypePtr t)
		{
			if (op.kind == SuffixOp::Array)
			{
				return op.unknown_bound ? make_array_unknown(t) : make_array_bound(t, op.bound);
			}
			return make_function(t, op.params, op.variadic);
		}

		TypePtr apply_prefixes(TypePtr t, const vector<PrefixOp>& prefixes)
		{
			for (size_t i = 0; i < prefixes.size(); ++i)
				t = apply_prefix(prefixes[i], t);
			return t;
		}

		TypePtr apply_suffixes(TypePtr t, const vector<SuffixOp>& suffixes)
		{
			for (size_t i = suffixes.size(); i > 0; --i)
				t = apply_suffix(suffixes[i - 1], t);
			return t;
		}

		TypePtr eval_fragment(const Fragment& frag, TypePtr base)
		{
			if (frag.grouped)
			{
				base = apply_suffixes(base, frag.after_ops);
				base = apply_prefixes(base, frag.before_ops);
			}
			else
			{
				base = apply_prefixes(base, frag.before_ops);
				base = apply_suffixes(base, frag.after_ops);
			}
			return base;
		}

		TypePtr parse_type_id(Namespace* scope)
		{
			DeclSpec ds = parse_decl_specifiers(scope, false);
			TypePtr t = ds.has_alias ? ds.alias_type : build_fundamental_type(ds);
			t = make_cv(t, ds.has_c, ds.has_v);
			if (ts.peek_kind() == OP_LPAREN || ts.peek_kind() == OP_STAR || ts.peek_kind() == OP_AMP ||
			    ts.peek_kind() == OP_LAND || ts.peek_kind() == TK_IDENTIFIER || ts.peek_kind() == OP_COLON2)
			{
				Fragment frag = parse_declarator_fragment(scope, true);
				t = eval_fragment(frag, t);
			}
			return t;
		}

		TypePtr adjust_parameter_type(TypePtr t)
		{
			if (!t)
				return t;
			t = strip_top_cv(t);
			if (t->kind == Type::ArrayUnknown || t->kind == Type::ArrayBound)
				return make_pointer(t->inner);
			if (t->kind == Type::Function)
				return make_pointer(t);
			return t;
		}

		struct ParamResult
		{
			TypePtr type;
			bool empty_void = false;
		};

		ParamResult parse_parameter_declaration(Namespace* scope)
		{
			DeclSpec ds = parse_decl_specifiers(scope, false);
			ParamResult pr;
			pr.type = ds.has_alias ? ds.alias_type : build_fundamental_type(ds);
			pr.type = make_cv(pr.type, ds.has_c, ds.has_v);
			bool has_decl = false;
			if (ts.peek_kind() == OP_LPAREN || ts.peek_kind() == OP_STAR || ts.peek_kind() == OP_AMP ||
			    ts.peek_kind() == OP_LAND || ts.peek_kind() == TK_IDENTIFIER || ts.peek_kind() == OP_COLON2)
			{
				size_t save = ts.pos;
				try
				{
					Fragment frag = parse_declarator_fragment(scope, true);
					pr.type = eval_fragment(frag, pr.type);
					has_decl = true;
				}
				catch (exception&)
				{
					ts.pos = save;
				}
			}
			if (!has_decl && pr.type && pr.type->kind == Type::Fundamental && pr.type->fundamental == FT_VOID)
				pr.empty_void = true;
			if (has_decl || !pr.empty_void)
				pr.type = adjust_parameter_type(pr.type);
			return pr;
		}

		void parse_parameter_clause(Namespace* scope, vector<TypePtr>& params, bool& variadic)
		{
			variadic = false;
			if (ts.peek_kind() == OP_RPAREN)
				return;
			if (ts.peek_kind() == OP_DOTS)
			{
				variadic = true;
				ts.consume();
				return;
			}
			vector<TypePtr> tmp_params;
			bool saw_void_only = false;
			while (true)
			{
				if (ts.peek_kind() == OP_DOTS)
				{
					variadic = true;
					ts.consume();
					break;
				}
				ParamResult pr = parse_parameter_declaration(scope);
				if (pr.empty_void && tmp_params.empty() && ts.peek_kind() == OP_RPAREN)
				{
					saw_void_only = true;
					tmp_params.clear();
					break;
				}
				tmp_params.push_back(pr.type);
				if (ts.peek_kind() == OP_DOTS)
				{
					variadic = true;
					ts.consume();
					break;
				}
				if (!accept(OP_COMMA))
					break;
				if (ts.peek_kind() == OP_DOTS)
				{
					variadic = true;
					ts.consume();
					break;
				}
			}
			if (!saw_void_only)
				params.swap(tmp_params);
		}

		shared_ptr<Expr> parse_expression(Namespace* scope)
		{
			if (ts.peek_kind() == OP_LPAREN)
			{
				ts.consume();
				shared_ptr<Expr> e = parse_expression(scope);
				if (!accept(OP_RPAREN))
					error_here("expected )");
				shared_ptr<Expr> n(new Expr());
				n->kind = Expr::Paren;
				n->inner = e;
				return n;
			}
			if (ts.peek_kind() == TK_IDENTIFIER || ts.peek_kind() == OP_COLON2)
			{
				shared_ptr<Expr> e(new Expr());
				e->kind = Expr::Id;
				if (accept(OP_COLON2))
					e->leading_global = true;
				while (true)
				{
					if (ts.peek_kind() != TK_IDENTIFIER)
						error_here("expected identifier");
					e->parts.push_back(ts.peek().data);
					ts.consume();
					if (!accept(OP_COLON2))
						break;
				}
				return e;
			}
			if (ts.peek_kind() == KW_TRUE || ts.peek_kind() == KW_FALSE || ts.peek_kind() == KW_NULLPTR || ts.peek_kind() == TK_LITERAL)
			{
				shared_ptr<Expr> e(new Expr());
				e->kind = Expr::Literal;
				e->tok = ts.peek();
				ts.consume();
				return e;
			}
			error_here("expected identifier or literal");
			return shared_ptr<Expr>();
		}

		bool token_is_string_literal(const Token& t) const
		{
			return t.raw_kind == Kind::STR || t.raw_kind == Kind::USTR;
		}

		bool token_is_char_literal(const Token& t) const
		{
			return t.raw_kind == Kind::CHAR || t.raw_kind == Kind::UCHAR;
		}

		bool token_is_pp_number(const Token& t) const
		{
			return t.raw_kind == Kind::PPNUM;
		}

		struct ExprValue
		{
			enum Kind
			{
				Immediate,
				LValue,
				FunctionGroup
			};

			Kind kind = Immediate;
			TypePtr type;
			EntityPtr entity;
			vector<shared_ptr<Entity> > temp_entities;
			vector<shared_ptr<Entity> > string_entities;
			vector<shared_ptr<FunctionEntity> > functions;
			vector<unsigned char> bytes;
			bool is_constant = false;
		};

		EntityPtr make_temp_entity(TypePtr t, const vector<unsigned char>& bytes, const string& init_dbg, const string& value_dbg)
		{
			EntityPtr e(new Entity());
			e->kind = Entity::Temp;
			e->name = "<temp>";
			e->scope = cur;
			e->type = t;
			e->bytes = bytes;
			e->size = type_size(t);
			e->align = max<size_t>(1, type_align(t));
			e->is_implicit = true;
			e->is_emittable = true;
			e->is_constant = false;
			e->debug_init = init_dbg;
			e->debug_value = value_dbg;
			prog->block2.push_back(e);
			return e;
		}

		EntityPtr make_string_literal_entity(TypePtr t, const vector<unsigned char>& bytes, const string& lit_dbg)
		{
			EntityPtr e(new Entity());
			e->kind = Entity::StringLiteral;
			e->name = "<string>";
			e->scope = cur;
			e->type = t;
			e->bytes = bytes;
			e->size = bytes.size();
			e->align = max<size_t>(1, type_align(t));
			e->is_implicit = true;
			e->is_emittable = true;
			e->is_constant = true;
			e->debug_value = lit_dbg;
			prog->block3.push_back(e);
			return e;
		}

		ExprValue eval_expr2(Namespace* scope, const shared_ptr<Expr>& expr)
		{
			if (!expr)
				error_here("expected expression");
			if (expr->kind == Expr::Paren)
				return eval_expr2(scope, expr->inner);
			if (expr->kind == Expr::Literal)
			{
				ExprValue v;
				v.is_constant = true;
				Token tok = expr->tok;
				if (tok.kind == KW_TRUE)
				{
					v.type = make_fundamental(FT_BOOL);
					v.bytes.assign(1, 1);
					return v;
				}
				if (tok.kind == KW_FALSE)
				{
					v.type = make_fundamental(FT_BOOL);
					v.bytes.assign(1, 0);
					return v;
				}
				if (tok.kind == KW_NULLPTR)
				{
					v.type = make_fundamental(FT_NULLPTR_T);
					v.bytes.assign(8, 0);
					return v;
				}
				if (tok.kind == TK_LITERAL)
				{
					if (!tok.data.empty() && (tok.data[0] == '"' || tok.data[0] == 'u' || tok.data[0] == 'U' || tok.data[0] == 'L' || tok.data[0] == 'R'))
					{
						// string literal; only used in initialization contexts
						v.kind = ExprValue::LValue;
						string src = tok.data;
						bool raw = false;
						EFundamentalType ft = FT_CHAR;
						size_t p = 0;
						if (src.compare(0, 2, "u8") == 0)
						{
							p = 2;
							ft = FT_CHAR;
						}
						else if (!src.empty() && src[0] == 'u')
						{
							p = 1;
							ft = FT_CHAR16_T;
						}
						else if (!src.empty() && src[0] == 'U')
						{
							p = 1;
							ft = FT_CHAR32_T;
						}
						else if (!src.empty() && src[0] == 'L')
						{
							p = 1;
							ft = FT_WCHAR_T;
						}
						if (p < src.size() && src[p] == 'R')
						{
							raw = true;
							++p;
						}
						if (p >= src.size() || (src[p] != '"' && src[p] != '\''))
							error_here("expected literal");
						char q = src[p];
						++p;
						string body;
						if (raw)
						{
							size_t j = p;
							while (j < src.size() && src[j] != '(')
								++j;
							if (j >= src.size())
								error_here("expected literal");
							string delim = src.substr(p, j - p);
							j++;
							size_t end = src.find(")" + delim + "\"", j);
							if (end == string::npos)
								error_here("expected literal");
							body = src.substr(j, end - j);
						}
						else
						{
							size_t end = src.find(q, p);
							if (end == string::npos)
								error_here("expected literal");
							body = src.substr(p, end - p);
						}
						vector<uint32_t> cps;
						if (raw)
						{
							for (size_t i = 0; i < body.size(); ++i)
								cps.push_back(static_cast<unsigned char>(body[i]));
						}
						else
						{
							// rely on the literal parser from posttoken.cpp
							Lit lit;
							if (!parse_literal(src, false, lit))
								error_here("expected literal");
							cps = lit.cps;
							if (lit.enc == Enc::Ordinary)
								ft = FT_CHAR;
							else if (lit.enc == Enc::U16)
								ft = FT_CHAR16_T;
							else if (lit.enc == Enc::U32)
								ft = FT_CHAR32_T;
							else if (lit.enc == Enc::Wide)
								ft = FT_WCHAR_T;
						}
						// The expression itself is an lvalue of array type.  The actual bytes are created on demand.
						vector<unsigned char> bytes;
						if (ft == FT_CHAR)
						{
							string s;
							for (size_t i = 0; i < cps.size(); ++i)
								s.push_back(static_cast<char>(cps[i]));
							s.push_back('\0');
							bytes.assign(s.begin(), s.end());
							v.type = make_array_bound(make_fundamental(FT_CHAR), bytes.size());
						}
						else if (ft == FT_CHAR16_T)
						{
							vector<unsigned char> b = encode_utf16(cps);
							bytes = b;
							v.type = make_array_bound(make_fundamental(FT_CHAR16_T), bytes.size() / 2);
						}
						else if (ft == FT_CHAR32_T || ft == FT_WCHAR_T)
						{
							vector<unsigned char> b = encode_utf32(cps);
							bytes = b;
							v.type = make_array_bound(make_fundamental(ft), bytes.size() / 4);
						}
						EntityPtr e = make_string_literal_entity(v.type, bytes, tok.data);
						v.kind = ExprValue::LValue;
						v.entity = e;
						v.bytes = bytes;
						return v;
					}

					bool is_float = tok.data.find_first_of(".eEpP") != string::npos;
					if (is_float)
					{
						string s = tok.data;
						size_t p = s.size();
						while (p > 0 && (s[p - 1] == 'f' || s[p - 1] == 'F' || s[p - 1] == 'l' || s[p - 1] == 'L'))
							--p;
						string prefix = s.substr(0, p);
						string tail = s.substr(p);
						if (tail == "f" || tail == "F")
						{
							float fv = PA2Decode_float(prefix);
							v.type = make_fundamental(FT_FLOAT);
							v.bytes.resize(sizeof(fv));
							memcpy(&v.bytes[0], &fv, sizeof(fv));
							return v;
						}
						if (tail == "l" || tail == "L")
						{
							long double lv = PA2Decode_long_double(prefix);
							v.type = make_fundamental(FT_LONG_DOUBLE);
							v.bytes.resize(sizeof(lv));
							memcpy(&v.bytes[0], &lv, sizeof(lv));
							return v;
						}
						double dv = PA2Decode_double(prefix);
						v.type = make_fundamental(FT_DOUBLE);
						v.bytes.resize(sizeof(dv));
						memcpy(&v.bytes[0], &dv, sizeof(dv));
						return v;
					}

					// integer or character literal
					ExprLit lit;
					if (!parse_literal(tok.data, true, lit))
						error_here("expected literal");
					if (lit.cps.empty())
						error_here("expected literal");
					uint32_t cp = lit.cps[0];
					if (tok.data.find('"') != string::npos)
					{
						// not expected here
						error_here("expected literal");
					}
					if (tok.data[0] == '\'')
					{
						if (cp <= 127)
						{
							char v0 = static_cast<char>(cp);
							v.type = make_fundamental(FT_CHAR);
							v.bytes.assign(&v0, &v0 + 1);
							return v;
						}
						v.type = make_fundamental(FT_INT);
						int iv = static_cast<int>(cp);
						v.bytes.resize(sizeof(iv));
						memcpy(&v.bytes[0], &iv, sizeof(iv));
						return v;
					}

					size_t value = 0;
					if (!parse_integer_literal(tok.data, value))
						error_here("expected literal");
					v.type = make_fundamental(FT_INT);
					int iv = static_cast<int>(value);
					v.bytes.resize(sizeof(iv));
					memcpy(&v.bytes[0], &iv, sizeof(iv));
					return v;
				}
			}

			// id-expression
			Namespace* ns = scope;
			if (expr->leading_global)
				ns = global_namespace(scope);
			for (size_t i = 0; i + 1 < expr->parts.size(); ++i)
			{
				shared_ptr<Symbol> sym = lookup_name(ns, expr->parts[i], true, false, true);
				if (!sym || sym->kind != Symbol::NamespaceKind)
					error_here("expected namespace name");
				ns = sym->ns;
			}
			string name = expr->parts.back();
			shared_ptr<Symbol> sym = lookup_name(ns, name, false, false, true);
			if (sym && sym->kind == Symbol::AliasKind)
				sym = resolve_alias(sym);
			if (!sym)
				error_here("expected identifier");
			ExprValue v;
			if (sym->kind == Symbol::VariableKind)
			{
				vv_from_variable(sym->entity, v);
				return v;
			}
			if (sym->kind == Symbol::FunctionGroupKind)
			{
				v.kind = ExprValue::FunctionGroup;
				v.functions = sym->functions;
				return v;
			}
			error_here("expected identifier");
			return v;
		}

		struct InitResult
		{
			vector<unsigned char> bytes;
			bool is_constant = false;
			EntityPtr temp_entity;
			EntityPtr string_entity;
			string initializer_dbg;
			string value_dbg;
		};

		void vv_from_variable(const EntityPtr& e, ExprValue& v)
		{
			if (!e)
				error_here("expected identifier");
			v.kind = ExprValue::LValue;
			v.entity = e;
			v.type = e->type;
			v.is_constant = e->is_constant;
			if (e->kind == Entity::Variable || e->kind == Entity::Temp || e->kind == Entity::StringLiteral)
				v.bytes = e->bytes;
			else
				v.bytes = vector<unsigned char>();
		}

		EntityPtr find_entity_by_offset(size_t addr)
		{
			vector<EntityPtr> all;
			all.insert(all.end(), prog->block1.begin(), prog->block1.end());
			all.insert(all.end(), prog->block2.begin(), prog->block2.end());
			all.insert(all.end(), prog->block3.begin(), prog->block3.end());
			for (size_t i = 0; i < all.size(); ++i)
			{
				if (all[i] && all[i]->offset_assigned && all[i]->offset == addr)
					return all[i];
			}
			return EntityPtr();
		}

		ExprValue collapse_reference_value(const ExprValue& v)
		{
			ExprValue cur = v;
			while (cur.kind == ExprValue::LValue)
			{
				TypePtr t = strip_top_cv(cur.type);
				if (!t || (t->kind != Type::LRef && t->kind != Type::RRef))
					break;
				size_t addr = 0;
				if (cur.bytes.size() >= sizeof(addr))
					memcpy(&addr, &cur.bytes[0], sizeof(addr));
				EntityPtr target = find_entity_by_offset(addr);
				if (!target)
					break;
				cur.kind = ExprValue::LValue;
				cur.entity = target;
				cur.type = target->type;
				cur.bytes = target->bytes;
				cur.is_constant = target->is_constant;
			}
			return cur;
		}

		ExprValue eval_constant_expression(Namespace* scope, const shared_ptr<Expr>& expr, bool static_assert_ctx = false)
		{
			ExprValue v = collapse_reference_value(eval_expr(scope, expr));
			if (static_assert_ctx)
			{
				if (!v.is_constant)
					throw ParseError(current_file(), current_line(), "static_assert on non-constant expression");
			}
			return v;
		}

		ExprValue eval_id_to_value(Namespace* scope, const shared_ptr<Expr>& expr)
		{
			return eval_expr(scope, expr);
		}

		bool same_function_signature(const TypePtr& a, const TypePtr& b)
		{
			if (!a || !b)
				return false;
			if (a->kind != Type::Function || b->kind != Type::Function)
				return false;
			if (type_to_string(a) != type_to_string(b))
				return false;
			return true;
		}

		Namespace* lookup_namespace_component(Namespace* scope, const string& name)
		{
			shared_ptr<Symbol> sym = lookup_name(scope, name, true, false, true);
			if (!sym || sym->kind != Symbol::NamespaceKind)
				return 0;
			return sym->ns;
		}

		Namespace* lookup_namespace_path(Namespace* scope, const vector<string>& parts, bool leading_global)
		{
			Namespace* cur = leading_global ? global_namespace(scope) : scope;
			for (size_t i = 0; i < parts.size(); ++i)
			{
				cur = lookup_namespace_component(cur, parts[i]);
				if (!cur)
					return 0;
			}
			return cur;
		}

		TypePtr resolve_type_name(Namespace* scope, const vector<string>& parts, bool leading_global)
		{
			if (parts.empty())
				return TypePtr();
			Namespace* cur = leading_global ? global_namespace(scope) : scope;
			if (parts.size() > 1)
			{
				for (size_t i = 0; i + 1 < parts.size(); ++i)
				{
					cur = lookup_namespace_component(cur, parts[i]);
					if (!cur)
						return TypePtr();
				}
			}
			shared_ptr<Symbol> sym = lookup_name(cur, parts.back(), false, true, true);
			if (!sym || sym->kind != Symbol::TypeAliasKind)
				return TypePtr();
			return sym->type;
		}

		bool parse_integer_literal(const string& src, size_t& out_value)
		{
			if (src.empty())
				return false;
			string s = src;
			while (!s.empty() && (s.back() == 'u' || s.back() == 'U' || s.back() == 'l' || s.back() == 'L'))
				s.pop_back();
			if (s.empty())
				return false;
			int base = 10;
			size_t start = 0;
			if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
			{
				base = 16;
				start = 2;
			}
			else if (s.size() > 1 && s[0] == '0')
			{
				base = 8;
				start = 1;
			}
			unsigned long long value = 0;
			for (size_t i = start; i < s.size(); ++i)
			{
				char c = s[i];
				int digit = -1;
				if (c >= '0' && c <= '9')
					digit = c - '0';
				else if (c >= 'a' && c <= 'f')
					digit = 10 + (c - 'a');
				else if (c >= 'A' && c <= 'F')
					digit = 10 + (c - 'A');
				else
					return false;
				if (digit >= base)
					return false;
				value = value * static_cast<unsigned>(base) + static_cast<unsigned>(digit);
			}
			out_value = static_cast<size_t>(value);
			return true;
		}

		struct DeclResult
		{
			EntityPtr entity;
			bool new_entity = false;
		};

		TypePtr adjust_type_for_decl(TypePtr t, const Fragment& frag)
		{
			return eval_fragment(frag, t);
		}

		TypePtr apply_parameter_adjustments(TypePtr t)
		{
			return adjust_type_for_parameter(t);
		}

		TypePtr adjust_type_for_parameter(TypePtr t)
		{
			return adjust_parameter_type(t);
		}

		bool is_incomplete_type(const TypePtr& t)
		{
			if (!t)
				return true;
			TypePtr s = strip_top_cv(t);
			if (!s)
				return true;
			if (s->kind == Type::Fundamental && s->fundamental == FT_VOID)
				return true;
			if (s->kind == Type::ArrayUnknown)
				return true;
			return false;
		}

		bool is_const_top_array(TypePtr t)
		{
			return t && t->kind == Type::Cv && t->inner && (t->inner->kind == Type::ArrayUnknown || t->inner->kind == Type::ArrayBound) && t->is_const;
		}

		EntityPtr ensure_symbol_entity(Namespace* scope, const string& name, const TypePtr& type, bool is_function, const DeclSpec& ds, bool& new_entity)
		{
			Namespace* owner = scope;
			shared_ptr<Symbol> sym;
			unordered_map<string, shared_ptr<Symbol> >::iterator it = owner->symbols.find(name);
			if (it != owner->symbols.end())
				sym = it->second;
			if (ds.is_static && scope == root)
			{
				unordered_map<string, shared_ptr<Symbol> >& local = ctx->local_symbols[owner];
				unordered_map<string, shared_ptr<Symbol> >::iterator lit = local.find(name);
				if (lit != local.end())
					sym = lit->second;
			}
			if (!sym && ds.is_static)
			{
				sym.reset(new Symbol());
				sym->name = name;
				sym->owner = owner;
				sym->kind = is_function ? Symbol::FunctionGroupKind : Symbol::VariableKind;
				if (is_function)
					sym->functions.clear();
				else
					sym->entity.reset(new Entity());
				ctx->local_symbols[owner][name] = sym;
			}
			else if (!sym)
			{
				sym.reset(new Symbol());
				sym->name = name;
				sym->owner = owner;
				sym->kind = is_function ? Symbol::FunctionGroupKind : Symbol::VariableKind;
				if (is_function)
					sym->functions.clear();
				else
					sym->entity.reset(new Entity());
				owner->symbols[name] = sym;
			}
			else
			{
				if (is_function)
				{
					if (sym->kind != Symbol::FunctionGroupKind)
						throw ParseError(current_file(), current_line(), name + " already exists");
				}
				else
				{
					if (sym->kind != Symbol::VariableKind)
						throw ParseError(current_file(), current_line(), name + " already exists");
				}
			}
			if (is_function)
			{
				// function entities are handled separately
			}
			new_entity = false;
			if (!is_function)
			{
				if (!sym->entity)
				{
					sym->entity.reset(new Entity());
					new_entity = true;
				}
				return sym->entity;
			}
			return EntityPtr();
		}

		void link_function_entity(Namespace* scope, const string& name, const TypePtr& type, const DeclSpec& ds, const Token& tok, bool is_definition)
		{
			Namespace* owner = scope;
			shared_ptr<Symbol> sym;
			if (ds.is_static && scope == root)
				sym = ctx->local_symbols[owner][name];
			else
				sym = owner->symbols[name];
			if (!sym)
			{
				sym.reset(new Symbol());
				sym->name = name;
				sym->owner = owner;
				sym->kind = Symbol::FunctionGroupKind;
				owner->symbols[name] = sym;
			}
			if (sym->kind != Symbol::FunctionGroupKind)
				throw ParseError(tok.file, tok.line, name + " already exists");

			shared_ptr<FunctionEntity> func;
			for (size_t i = 0; i < sym->functions.size(); ++i)
			{
				if (same_function_signature(sym->functions[i]->type, type))
				{
					func = sym->functions[i];
					break;
				}
			}
			if (!func)
			{
				func.reset(new FunctionEntity());
				func->entity.kind = Entity::Function;
				func->entity.name = name;
				func->entity.file = tok.file;
				func->entity.line = tok.line;
				func->entity.scope = owner;
				func->entity.type = type;
				func->type = type;
				func->entity.size = type_size(type);
				func->entity.align = max<size_t>(1, type_align(type));
				func->entity.is_static = ds.is_static;
				func->entity.is_thread_local = ds.is_thread_local;
				func->entity.is_extern = ds.is_extern;
				func->entity.is_inline = ds.is_inline;
				func->entity.is_constexpr = ds.is_constexpr;
				func->entity.defined = is_definition;
				func->entity.debug_spec = specifier_debug(ds, type->inner);
				sym->functions.push_back(func);
				append_block1_entity(prog, func->entity_ptr());
			}
			else if (is_definition && func->entity.defined)
			{
				throw ParseError(tok.file, tok.line, string("function ") + name + " already defined");
			}
			else if (is_definition)
			{
				func->entity.defined = true;
			}
			log_function(func->entity, is_definition);
		}

		string specifier_debug(const DeclSpec& ds, const TypePtr& base)
		{
			TypePtr base_simple = base;
			while (base_simple && base_simple->kind == Type::Cv)
				base_simple = base_simple->inner;
			vector<string> bits;
			if (ds.is_inline)
				bits.push_back("SP_INLINE");
			if (ds.is_constexpr)
				bits.push_back("SP_CONSTEXPR");
			if (ds.is_static)
				bits.push_back("SP_STATIC");
			if (ds.is_thread_local)
				bits.push_back("SP_THREAD_LOCAL");
			if (ds.is_extern)
				bits.push_back("SP_EXTERN");
			if (ds.has_c)
				bits.push_back("SP_CONST");
			if (ds.has_v)
				bits.push_back("SP_VOLATILE");
			if (base_simple && base_simple->kind == Type::Fundamental)
			{
				switch (base_simple->fundamental)
				{
				case FT_CHAR: bits.push_back("SP_CHAR"); break;
				case FT_SIGNED_CHAR: bits.push_back("SP_CHAR|SP_SIGNED"); break;
				case FT_UNSIGNED_CHAR: bits.push_back("SP_CHAR|SP_UNSIGNED"); break;
				case FT_SHORT_INT: bits.push_back("SP_SHORT|SP_INT"); break;
				case FT_UNSIGNED_SHORT_INT: bits.push_back("SP_SHORT|SP_INT|SP_UNSIGNED"); break;
				case FT_INT: bits.push_back("SP_INT"); break;
				case FT_UNSIGNED_INT: bits.push_back("SP_INT|SP_UNSIGNED"); break;
				case FT_LONG_INT: bits.push_back("SP_LONG_1|SP_INT"); break;
				case FT_UNSIGNED_LONG_INT: bits.push_back("SP_LONG_1|SP_INT|SP_UNSIGNED"); break;
				case FT_LONG_LONG_INT: bits.push_back("SP_LONG_2|SP_INT"); break;
				case FT_UNSIGNED_LONG_LONG_INT: bits.push_back("SP_LONG_2|SP_INT|SP_UNSIGNED"); break;
				case FT_WCHAR_T: bits.push_back("SP_WCHAR_T"); break;
				case FT_CHAR16_T: bits.push_back("SP_CHAR16_T"); break;
				case FT_CHAR32_T: bits.push_back("SP_CHAR32_T"); break;
				case FT_BOOL: bits.push_back("SP_BOOL"); break;
				case FT_FLOAT: bits.push_back("SP_FLOAT"); break;
				case FT_DOUBLE: bits.push_back("SP_DOUBLE"); break;
				case FT_LONG_DOUBLE: bits.push_back("SP_LONG_1|SP_DOUBLE"); break;
				case FT_VOID: bits.push_back("SP_VOID"); break;
				case FT_NULLPTR_T: bits.push_back("SP_NULLPTR"); break;
				}
			}
			string out;
			for (size_t i = 0; i < bits.size(); ++i)
			{
				if (i)
					out += "|";
				out += bits[i];
			}
			return out;
		}

		void log_function(const Entity& e, bool is_definition)
		{
			ostringstream oss;
			oss << "LINKING: function " << e.name << " " << e.debug_spec << " ";
			oss << type_to_string(e.type);
			oss << " defined=" << (is_definition ? 1 : 0);
			ctx->logs.push_back(oss.str());
		}

		string specifier_debug_for_entity(const Entity& e)
		{
			vector<string> bits;
			if (e.is_inline)
				bits.push_back("SP_INLINE");
			if (e.is_constexpr)
				bits.push_back("SP_CONSTEXPR");
			if (e.is_static)
				bits.push_back("SP_STATIC");
			if (e.is_thread_local)
				bits.push_back("SP_THREAD_LOCAL");
			if (e.is_extern)
				bits.push_back("SP_EXTERN");
			if (e.type && e.type->kind == Type::Cv && e.type->is_const)
				bits.push_back("SP_CONST");
			if (e.type && e.type->kind == Type::Cv && e.type->is_volatile)
				bits.push_back("SP_VOLATILE");
			return join_bits(bits);
		}

		string join_bits(const vector<string>& bits)
		{
			string out;
			for (size_t i = 0; i < bits.size(); ++i)
			{
				if (i)
					out += "|";
				out += bits[i];
			}
			return out;
		}

		string type_bytes_string(const TypePtr& t, const vector<unsigned char>& bytes)
		{
			return hex_bytes(bytes);
		}

		ExprValue value_from_bytes(TypePtr t, const vector<unsigned char>& bytes)
		{
			ExprValue v;
			v.kind = ExprValue::Immediate;
			v.type = t;
			v.bytes = bytes;
			v.is_constant = true;
			return v;
		}

		EntityPtr create_temp_from_value(TypePtr t, const vector<unsigned char>& bytes, const string& init_dbg, const string& value_dbg)
		{
			EntityPtr e(new Entity());
			e->kind = Entity::Temp;
			e->name = "<temp>";
			e->scope = cur;
			e->type = t;
			e->bytes = bytes;
			e->size = type_size(t);
			e->align = max<size_t>(1, type_align(t));
			e->is_constant = false;
			e->is_emittable = true;
			e->debug_init = init_dbg;
			e->debug_value = value_dbg;
			prog->block2.push_back(e);
			return e;
		}

		EntityPtr create_string_literal(TypePtr t, const vector<unsigned char>& bytes)
		{
			EntityPtr e(new Entity());
			e->kind = Entity::StringLiteral;
			e->name = "<string>";
			e->scope = cur;
			e->type = t;
			e->bytes = bytes;
			e->size = bytes.size();
			e->align = max<size_t>(1, type_align(t));
			e->is_constant = true;
			e->is_emittable = true;
			prog->block3.push_back(e);
			return e;
		}

		ExprValue eval_id_lookup(Namespace* scope, const vector<string>& parts, bool leading_global)
		{
			Namespace* ns = leading_global ? global_namespace(scope) : scope;
			for (size_t i = 0; i + 1 < parts.size(); ++i)
			{
				shared_ptr<Symbol> sym = lookup_name(ns, parts[i], true, false, true);
				if (!sym || sym->kind != Symbol::NamespaceKind)
					error_here("expected namespace name");
				ns = sym->ns;
			}
			string name = parts.back();
			shared_ptr<Symbol> sym = lookup_name(ns, name, false, false, true);
			if (!sym)
				error_here("expected identifier");
			if (sym->kind == Symbol::AliasKind)
				sym = resolve_alias(sym);
			ExprValue v;
			if (sym->kind == Symbol::VariableKind)
			{
				v.kind = ExprValue::LValue;
				v.entity = sym->entity;
				v.type = sym->entity->type;
				v.bytes = sym->entity->bytes;
				v.is_constant = sym->entity->is_constant;
				return v;
			}
			if (sym->kind == Symbol::FunctionGroupKind)
			{
				v.kind = ExprValue::FunctionGroup;
				v.functions = sym->functions;
				return v;
			}
			error_here("expected identifier");
			return v;
		}

		ExprValue eval_expr(Namespace* scope, const shared_ptr<Expr>& expr)
		{
			if (!expr)
				error_here("expected expression");
			if (expr->kind == Expr::Paren)
				return eval_expr(scope, expr->inner);
			if (expr->kind == Expr::Id)
				return eval_id_lookup(scope, expr->parts, expr->leading_global);
			Token tok = expr->tok;
			if (tok.kind == KW_TRUE)
				return value_from_bytes(make_fundamental(FT_BOOL), vector<unsigned char>(1, 1));
			if (tok.kind == KW_FALSE)
				return value_from_bytes(make_fundamental(FT_BOOL), vector<unsigned char>(1, 0));
			if (tok.kind == KW_NULLPTR)
				return value_from_bytes(make_fundamental(FT_NULLPTR_T), vector<unsigned char>(8, 0));
			if (tok.kind == TK_LITERAL)
			{
				if (token_is_string_literal(tok))
				{
					ExprLit lit;
					if (!parse_literal(tok.data, false, lit))
						error_here("expected literal");
					vector<uint32_t> cps = lit.cps;
					if (lit.enc == Enc::Ordinary || lit.enc == Enc::U8)
					{
						string s;
						for (size_t i = 0; i < cps.size(); ++i)
							s.push_back(static_cast<char>(cps[i]));
						s.push_back('\0');
						vector<unsigned char> bytes(s.begin(), s.end());
						TypePtr t = make_array_bound(make_fundamental(FT_CHAR), bytes.size());
						EntityPtr e = create_string_literal(t, bytes);
						ExprValue v;
						v.kind = ExprValue::LValue;
						v.entity = e;
						v.type = e->type;
						v.bytes = bytes;
						v.is_constant = true;
						return v;
					}
					else if (lit.enc == Enc::U16)
					{
						vector<unsigned char> bytes = encode_utf16(cps);
						TypePtr t = make_array_bound(make_fundamental(FT_CHAR16_T), bytes.size() / 2);
						EntityPtr e = create_string_literal(t, bytes);
						ExprValue v;
						v.kind = ExprValue::LValue;
						v.entity = e;
						v.type = e->type;
						v.bytes = bytes;
						v.is_constant = true;
						return v;
					}
					else
					{
						vector<unsigned char> bytes = encode_utf32(cps);
						EFundamentalType ft = (lit.enc == Enc::Wide) ? FT_WCHAR_T : FT_CHAR32_T;
						TypePtr t = make_array_bound(make_fundamental(ft), bytes.size() / 4);
						EntityPtr e = create_string_literal(t, bytes);
						ExprValue v;
						v.kind = ExprValue::LValue;
						v.entity = e;
						v.type = e->type;
						v.bytes = bytes;
						v.is_constant = true;
						return v;
					}
				}
				if (token_is_char_literal(tok))
				{
					ExprLit lit;
					if (!parse_literal(tok.data, true, lit) || lit.cps.empty())
						error_here("expected literal");
					uint32_t cp = lit.cps[0];
					ExprValue v;
					if (lit.enc == Enc::Ordinary)
					{
						if (cp <= 127)
						{
							char c = static_cast<char>(cp);
							v = value_from_bytes(make_fundamental(FT_CHAR), vector<unsigned char>(1, static_cast<unsigned char>(c)));
						}
						else
						{
							int iv = static_cast<int>(cp);
							vector<unsigned char> bytes(sizeof(iv));
							memcpy(&bytes[0], &iv, sizeof(iv));
							v = value_from_bytes(make_fundamental(FT_INT), bytes);
						}
					}
					else if (lit.enc == Enc::U16)
					{
						char16_t c = static_cast<char16_t>(cp);
						vector<unsigned char> bytes(sizeof(c));
						memcpy(&bytes[0], &c, sizeof(c));
						v = value_from_bytes(make_fundamental(FT_CHAR16_T), bytes);
					}
					else if (lit.enc == Enc::U32)
					{
						char32_t c = static_cast<char32_t>(cp);
						vector<unsigned char> bytes(sizeof(c));
						memcpy(&bytes[0], &c, sizeof(c));
						v = value_from_bytes(make_fundamental(FT_CHAR32_T), bytes);
					}
					else
					{
						wchar_t c = static_cast<wchar_t>(cp);
						vector<unsigned char> bytes(sizeof(c));
						memcpy(&bytes[0], &c, sizeof(c));
						v = value_from_bytes(make_fundamental(FT_WCHAR_T), bytes);
					}
					return v;
				}
				if (token_is_pp_number(tok))
				{
					string s = tok.data;
					if (s.find_first_of(".eEpP") != string::npos)
					{
						size_t p = s.size();
						while (p > 0 && (s[p - 1] == 'f' || s[p - 1] == 'F' || s[p - 1] == 'l' || s[p - 1] == 'L'))
							--p;
						string prefix = s.substr(0, p);
						string tail = s.substr(p);
						if (tail == "f" || tail == "F")
						{
							float fv = PA2Decode_float(prefix);
							vector<unsigned char> bytes(sizeof(fv));
							memcpy(&bytes[0], &fv, sizeof(fv));
							return value_from_bytes(make_fundamental(FT_FLOAT), bytes);
						}
						if (tail == "l" || tail == "L")
						{
							long double lv = PA2Decode_long_double(prefix);
							vector<unsigned char> bytes(sizeof(lv));
							memcpy(&bytes[0], &lv, sizeof(lv));
							return value_from_bytes(make_fundamental(FT_LONG_DOUBLE), bytes);
						}
						double dv = PA2Decode_double(prefix);
						vector<unsigned char> bytes(sizeof(dv));
						memcpy(&bytes[0], &dv, sizeof(dv));
						return value_from_bytes(make_fundamental(FT_DOUBLE), bytes);
					}
					size_t value = 0;
					if (!parse_integer_literal(tok.data, value))
						error_here("expected literal");
					int iv = static_cast<int>(value);
					vector<unsigned char> bytes(sizeof(iv));
					memcpy(&bytes[0], &iv, sizeof(iv));
					return value_from_bytes(make_fundamental(FT_INT), bytes);
				}
			}
			error_here("expected expression");
			return ExprValue();
		}

		bool is_truthy(const ExprValue& v, bool require_integer = false)
		{
			TypePtr t = strip_top_cv(v.type);
			if (!t)
				return false;
			if (require_integer)
			{
				if (t->kind != Type::Fundamental)
					return false;
				switch (t->fundamental)
				{
				case FT_BOOL:
				case FT_CHAR:
				case FT_SIGNED_CHAR:
				case FT_UNSIGNED_CHAR:
				case FT_SHORT_INT:
				case FT_UNSIGNED_SHORT_INT:
				case FT_INT:
				case FT_UNSIGNED_INT:
				case FT_LONG_INT:
				case FT_UNSIGNED_LONG_INT:
				case FT_LONG_LONG_INT:
				case FT_UNSIGNED_LONG_LONG_INT:
					break;
				default:
					return false;
				}
			}
			if (v.bytes.empty())
				return false;
			unsigned long long x = 0;
			memcpy(&x, &v.bytes[0], min(sizeof(x), v.bytes.size()));
			return x != 0;
		}

		ExprValue convert_lvalue_to_pointer(const ExprValue& src, const TypePtr& dst)
		{
			ExprValue target = collapse_reference_value(src);
			ExprValue out;
			out.kind = ExprValue::Immediate;
			out.type = dst;
			size_t addr = 0;
			if (target.entity)
				addr = target.entity->offset;
			vector<unsigned char> bytes(8, 0);
			memcpy(&bytes[0], &addr, 8);
			out.bytes = bytes;
			out.is_constant = true;
			return out;
		}

		ExprValue convert_to_type(Namespace* scope, const TypePtr& dst, const ExprValue& src, const Token& loc, bool& used_temp, string& init_dbg, string& value_dbg)
		{
			used_temp = false;
			if (!dst)
				error_here("expected declaration");
			auto set_immediate_dbg = [&](const TypePtr& t, const vector<unsigned char>& bytes)
			{
				value_dbg = string("Immediate (VC_PRVALUE ") + type_to_string(strip_top_cv(t)) + " " + hex_bytes(bytes) + ")";
			};
			auto decode_numeric = [&](const ExprValue& v) -> long double
			{
				TypePtr vt = strip_top_cv(v.type);
				if (!vt || vt->kind != Type::Fundamental || v.bytes.empty())
					return 0;
				switch (vt->fundamental)
				{
				case FT_FLOAT:
				{
					float x = 0;
					memcpy(&x, &v.bytes[0], min(sizeof(x), v.bytes.size()));
					return static_cast<long double>(x);
				}
				case FT_DOUBLE:
				{
					double x = 0;
					memcpy(&x, &v.bytes[0], min(sizeof(x), v.bytes.size()));
					return static_cast<long double>(x);
				}
				case FT_LONG_DOUBLE:
				{
					long double x = 0;
					memcpy(&x, &v.bytes[0], min(sizeof(x), v.bytes.size()));
					return x;
				}
				case FT_BOOL:
				case FT_CHAR:
				case FT_SIGNED_CHAR:
				case FT_UNSIGNED_CHAR:
				case FT_SHORT_INT:
				case FT_UNSIGNED_SHORT_INT:
				case FT_INT:
				case FT_UNSIGNED_INT:
				case FT_LONG_INT:
				case FT_UNSIGNED_LONG_INT:
				case FT_LONG_LONG_INT:
				case FT_UNSIGNED_LONG_LONG_INT:
				{
					unsigned long long u = 0;
					memcpy(&u, &v.bytes[0], min(sizeof(u), v.bytes.size()));
					return static_cast<long double>(u);
				}
				default:
					return 0;
				}
			};
			auto encode_floating = [&](long double num, EFundamentalType ft, vector<unsigned char>& bytes)
			{
				if (ft == FT_FLOAT)
				{
					float x = static_cast<float>(num);
					bytes.resize(sizeof(x));
					memcpy(&bytes[0], &x, sizeof(x));
					return;
				}
				if (ft == FT_DOUBLE)
				{
					double x = static_cast<double>(num);
					bytes.resize(sizeof(x));
					memcpy(&bytes[0], &x, sizeof(x));
					return;
				}
				long double x = static_cast<long double>(num);
				bytes.resize(sizeof(x));
				memcpy(&bytes[0], &x, sizeof(x));
			};

			TypePtr ddst = strip_top_cv(dst);
			if (!ddst)
				error_here("expected declaration");

			if (ddst->kind == Type::Cv)
				ddst = ddst->inner;

			if (ddst->kind == Type::LRef || ddst->kind == Type::RRef)
			{
				TypePtr ref_to = ddst->inner;
				if (src.kind == ExprValue::LValue)
				{
					ExprValue out = convert_lvalue_to_pointer(src, make_pointer(ref_to));
					out.type = dst;
					value_dbg = string("VariableExpression (VC_LVALUE ") + type_to_string(src.type) + " " + (src.entity ? src.entity->name : string("<temp>")) + ")";
					return out;
				}
				if (src.kind == ExprValue::FunctionGroup)
				{
					shared_ptr<FunctionEntity> chosen;
					for (size_t i = 0; i < src.functions.size(); ++i)
					{
						if (same_function_signature(src.functions[i]->entity.type, ref_to))
						{
							chosen = src.functions[i];
							break;
						}
					}
					if (!chosen)
						error_here("expected identifier");
					ExprValue out;
					out.kind = ExprValue::LValue;
					out.type = dst;
					out.entity = chosen->entity_ptr();
					value_dbg = string("FunctionAddress (VC_PRVALUE pointer to ") + type_to_string(make_function(ref_to, vector<TypePtr>(), false)) + " " + chosen->entity.name + ")";
					return out;
				}
				if (src.kind == ExprValue::Immediate)
				{
					if (is_incomplete_type(ref_to))
						error_here("type cannot be default initialized");
					vector<unsigned char> bytes = src.bytes;
					TypePtr temp_type = strip_top_cv(ref_to);
					EntityPtr temp = create_temp_from_value(temp_type, bytes, "CopyInitializer", "Immediate");
					used_temp = true;
					ExprValue out;
					out.kind = ExprValue::LValue;
					out.type = dst;
					out.entity = temp;
					out.bytes = vector<unsigned char>(8, 0);
					vector<unsigned char> addr_bytes(8, 0);
					size_t addr = temp->offset << 32;
					memcpy(&addr_bytes[0], &addr, 8);
					out.bytes = addr_bytes;
					value_dbg = string("VariableExpression (VC_LVALUE ") + type_to_string(temp_type) + " <temp>)";
					return out;
				}
				error_here("invalid type for reference to");
			}

			if (ddst->kind == Type::Pointer)
			{
				ExprValue out;
				out.kind = ExprValue::Immediate;
				out.type = dst;
				vector<unsigned char> bytes(8, 0);
				if (src.kind == ExprValue::Immediate)
				{
					if (!src.bytes.empty())
						memcpy(&bytes[0], &src.bytes[0], min<size_t>(8, src.bytes.size()));
					out.bytes = bytes;
					set_immediate_dbg(dst, out.bytes);
					return out;
				}
				if (src.kind == ExprValue::LValue)
				{
					size_t addr = 0;
					if (src.entity)
						addr = src.entity->offset;
					memcpy(&bytes[0], &addr, 8);
					out.bytes = bytes;
					if (src.entity && src.entity->kind == Entity::StringLiteral)
						value_dbg = string("ArrayVariablePointer (VC_PRVALUE ") + type_to_string(dst) + " " + src.entity->name + ")";
					else if (src.entity && src.entity->kind == Entity::Function)
						value_dbg = string("FunctionAddress (VC_PRVALUE ") + type_to_string(dst) + " " + src.entity->name + ")";
					else
						value_dbg = string("VariableExpression (VC_LVALUE ") + type_to_string(src.type) + " " + (src.entity ? src.entity->name : string("<temp>")) + ")";
					return out;
				}
				if (src.kind == ExprValue::FunctionGroup)
				{
					shared_ptr<FunctionEntity> chosen;
					for (size_t i = 0; i < src.functions.size(); ++i)
					{
						if (same_function_signature(src.functions[i]->entity.type, ddst->inner))
						{
							chosen = src.functions[i];
							break;
						}
					}
					if (!chosen)
						error_here("expected identifier");
					size_t addr = chosen->entity.offset;
					memcpy(&bytes[0], &addr, 8);
					out.bytes = bytes;
					value_dbg = string("FunctionAddress (VC_PRVALUE ") + type_to_string(dst) + " " + chosen->entity.name + ")";
					return out;
				}
				return out;
			}

			if (ddst->kind == Type::ArrayUnknown || ddst->kind == Type::ArrayBound)
			{
				TypePtr elem = ddst->inner;
				if (src.kind == ExprValue::LValue && src.entity && src.entity->kind == Entity::StringLiteral)
				{
					vector<unsigned char> bytes = src.entity->bytes;
					if (ddst->kind == Type::ArrayBound && ddst->bound < bytes.size() / max<size_t>(1, type_size(elem)))
						error_here("type cannot be default initialized");
					ExprValue out;
					out.kind = ExprValue::Immediate;
					if (ddst->kind == Type::ArrayUnknown)
						out.type = make_array_bound(elem, bytes.size() / max<size_t>(1, type_size(elem)));
					else
						out.type = dst;
					out.bytes.assign(type_size(out.type), 0);
					memcpy(&out.bytes[0], bytes.data(), min(out.bytes.size(), bytes.size()));
					set_immediate_dbg(out.type, out.bytes);
					return out;
				}
				if (src.kind == ExprValue::Immediate && src.bytes.empty())
				{
					ExprValue out;
					out.kind = ExprValue::Immediate;
					out.type = dst;
					out.bytes.assign(type_size(dst), 0);
					set_immediate_dbg(dst, out.bytes);
					return out;
				}
				error_here("type cannot be default initialized");
			}

			if (ddst->kind == Type::Fundamental)
			{
				ExprValue out;
				out.kind = ExprValue::Immediate;
				out.type = dst;
				size_t n = type_size(ddst);
				out.bytes.assign(n, 0);
				if (ddst->fundamental == FT_FLOAT || ddst->fundamental == FT_DOUBLE || ddst->fundamental == FT_LONG_DOUBLE)
				{
					if (src.kind == ExprValue::Immediate || (src.kind == ExprValue::LValue && src.entity))
					{
						long double num = decode_numeric(src);
						encode_floating(num, ddst->fundamental, out.bytes);
						set_immediate_dbg(dst, out.bytes);
						return out;
					}
				}
				if (src.kind == ExprValue::Immediate)
				{
					if (n == 0)
						error_here("variable defined with incomplete type");
					memcpy(&out.bytes[0], src.bytes.data(), min(n, src.bytes.size()));
					set_immediate_dbg(dst, out.bytes);
					return out;
				}
				if (src.kind == ExprValue::LValue && src.entity)
				{
					if (src.entity->kind == Entity::StringLiteral)
						error_here("type cannot be default initialized");
					if (!src.bytes.empty())
					{
						memcpy(&out.bytes[0], src.bytes.data(), min(n, src.bytes.size()));
						set_immediate_dbg(dst, out.bytes);
						return out;
					}
				}
				if (src.kind == ExprValue::FunctionGroup)
					error_here("type cannot be default initialized");
			}

			error_here("type cannot be default initialized");
			return ExprValue();
		}

		void declare_variable(Namespace* scope, const Fragment& frag, const TypePtr& base, const TypePtr& type, const DeclSpec& ds,
		                      const Token& loc, const shared_ptr<Expr>& init_expr, bool is_definition)
		{
			Namespace* owner = frag.qualified_scope ? frag.qualified_scope : scope;
			if (frag.has_name == false)
				return;
			if (is_reference_type(type) && !init_expr)
				throw ParseError(loc.file, loc.line, "type cannot be default initialized");
			bool is_internal = ds.is_static || owner->unnamed;
			EntityPtr entity;
			bool new_entity = false;
			if (is_internal && owner == root)
			{
				unordered_map<string, shared_ptr<Symbol> >& locmap = ctx->local_symbols[owner];
				unordered_map<string, shared_ptr<Symbol> >::iterator it = locmap.find(frag.name);
				if (it != locmap.end())
					entity = it->second->entity;
				else
				{
					shared_ptr<Symbol> sym(new Symbol());
					sym->name = frag.name;
					sym->kind = Symbol::VariableKind;
					sym->owner = owner;
					sym->entity.reset(new Entity());
					locmap[frag.name] = sym;
					entity = sym->entity;
					new_entity = true;
				}
			}
			else
			{
				unordered_map<string, shared_ptr<Symbol> >::iterator it = owner->symbols.find(frag.name);
				if (it != owner->symbols.end())
					entity = it->second->entity;
				else
				{
					shared_ptr<Symbol> sym(new Symbol());
					sym->name = frag.name;
					sym->kind = Symbol::VariableKind;
					sym->owner = owner;
					sym->entity.reset(new Entity());
					owner->symbols[frag.name] = sym;
					entity = sym->entity;
					new_entity = true;
				}
			}
			if (!entity)
				error_here("expected declaration");

			if (entity->type && type_to_string(entity->type) != type_to_string(type))
			{
				// Allow unknown bound arrays to become bounded.
				if (!(entity->type->kind == Type::ArrayUnknown && type->kind == Type::ArrayBound &&
				      type_to_string(entity->type->inner) == type_to_string(type->inner)))
					error_here(frag.name + " already exists");
			}

			entity->kind = Entity::Variable;
			entity->name = frag.name;
			entity->file = loc.file;
			entity->line = loc.line;
			entity->scope = owner;
			entity->type = type;
			entity->is_static = ds.is_static;
			entity->is_thread_local = ds.is_thread_local;
			entity->is_extern = ds.is_extern;
			entity->is_inline = ds.is_inline;
			entity->is_constexpr = ds.is_constexpr;
			entity->is_internal = is_internal;
			bool entity_is_constant = ds.is_constexpr ||
			                          (type && type->kind == Type::Cv && type->is_const && !is_reference_type(type)) ||
			                          is_const_top_array(type);
			if (is_definition)
				entity->defined = true;

			bool extern_forward_declaration = !init_expr && ds.is_extern &&
				((type && type->kind == Type::Cv && type->is_const) || is_incomplete_type(type));
			if (!init_expr && !extern_forward_declaration &&
			    (is_incomplete_type(type) || is_reference_type(type) || is_const_top_array(type) ||
			     (type && type->kind == Type::Cv && type->is_const)))
			{
				if (is_incomplete_type(type))
					throw ParseError(loc.file, loc.line, "variable defined with incomplete type");
				throw ParseError(loc.file, loc.line, "type cannot be default initialized");
			}

			ExprValue srcv;
			string init_dbg = "DefaultInitializer";
			string value_dbg = "null";
			if (init_expr)
			{
				srcv = eval_expr(owner, init_expr);
				init_dbg = expr_debug_string(init_expr);
				bool used_temp = false;
				ExprValue dstv = convert_to_type(owner, type, srcv, loc, used_temp, init_dbg, value_dbg);
				entity->bytes = dstv.bytes;
				if (is_reference_type(type) && dstv.entity && dstv.entity->kind == Entity::Temp)
					entity->address_target = dstv.entity;
				entity->type = dstv.type ? dstv.type : type;
				entity->has_initializer = true;
				entity->init_expr = init_expr;
				if (srcv.kind == ExprValue::LValue &&
				    ((srcv.type && srcv.type->kind == Type::Cv && srcv.type->is_const) ||
				     (srcv.entity && srcv.entity->is_constant)))
					entity_is_constant = true;
				if (is_reference_type(type) && srcv.kind == ExprValue::Immediate)
					entity_is_constant = false;
			}
			else
			{
				entity->bytes.assign(type_size(type), 0);
				entity->has_initializer = false;
			}
			entity->is_constant = entity_is_constant;
			entity->size = type_size(entity->type);
			entity->align = max<size_t>(1, type_align(entity->type));
			entity->debug_spec = specifier_debug(ds, base);
			entity->debug_init = init_dbg;
			entity->debug_value = value_dbg;
			if (extern_forward_declaration)
			{
				entity->was_forward_declared = true;
				return;
			}
			if (!entity->defined && !entity->is_extern && !entity->is_static && !entity->is_thread_local && !is_reference_type(type))
				entity->defined = true;
			if (!entity->offset_assigned)
				append_block1_entity(prog, entity);
			log_variable(*entity, init_expr != 0);
		}

		string expr_debug_string(const shared_ptr<Expr>& e)
		{
			if (!e)
				return string("DefaultInitializer");
			if (e->kind == Expr::Paren)
				return expr_debug_string(e->inner);
			if (e->kind == Expr::Id)
			{
				string s = "IdExpression (";
				if (e->leading_global)
					s += "::";
				for (size_t i = 0; i < e->parts.size(); ++i)
				{
					if (i)
						s += "::";
					s += e->parts[i];
				}
				s += ")";
				return s;
			}
			Token tok = e->tok;
			if (tok.kind == KW_TRUE)
				return "LiteralExpression (KW_TRUE)";
			if (tok.kind == KW_FALSE)
				return "LiteralExpression (KW_FALSE)";
			if (tok.kind == KW_NULLPTR)
				return "LiteralExpression (KW_NULLPTR)";
			if (tok.kind == TK_LITERAL)
				return string("LiteralExpression (TT_LITERAL:") + tok.data + ")";
			return string("Expression");
		}

		void log_variable(const Entity& e, bool has_initializer)
		{
			ostringstream oss;
			oss << "LINKING: variable " << e.name << " ";
			oss << e.debug_spec << " ";
			oss << linkage_string(e);
			oss << " " << type_to_string(e.type) << " ";
			if (!has_initializer)
			{
				oss << "initializer=(DefaultInitializer) initializer_expression=(null) is_constant = 0";
			}
			else
			{
				oss << "initializer=(CopyInitializer (" << e.debug_init << ")) ";
				oss << "initializer_expression=(" << e.debug_value << ") is_constant = " << (e.is_constant ? 1 : 0);
			}
			ctx->logs.push_back(oss.str());
		}

		string linkage_string(const Entity& e)
		{
			if (e.is_internal)
				return "LK_INTERNAL";
			return "LK_EXTERNAL";
		}

		void parse_function_or_variable_declaration(Namespace* scope, const DeclSpec& ds, TypePtr base, const Token& first_tok)
		{
			bool first = true;
			bool saw_function_definition = false;
			while (true)
			{
				Fragment frag = parse_declarator_fragment(scope, false);
				if (frag.qualified_scope && scope != root && !is_enclosing_namespace(frag.qualified_scope, scope))
					throw ParseError(first_tok.file, first_tok.line, "qualified name not from enclosed namespace");
				TypePtr t = eval_fragment(frag, base);
				if (!frag.has_name)
					error_here("expected declaration");

				if (t && t->kind == Type::Function)
				{
					if (ts.peek_kind() == OP_LBRACE)
					{
						ts.consume();
						if (!accept(OP_RBRACE))
							error_here("expected }");
						link_function_entity(frag.qualified_scope ? frag.qualified_scope : scope, frag.name, t, ds, first_tok, true);
						saw_function_definition = true;
					}
					else
					{
						link_function_entity(frag.qualified_scope ? frag.qualified_scope : scope, frag.name, t, ds, first_tok, false);
					}
				}
				else
				{
					shared_ptr<Expr> init;
					if (accept(OP_ASS))
						init = parse_expression(scope);
					declare_variable(scope, frag, base, t, ds, first_tok, init, true);
				}
				if (!accept(OP_COMMA))
					break;
			}
			if (!accept(OP_SEMICOLON) && !saw_function_definition)
			{
				if (ts.peek_kind() != OP_RBRACE)
					error_here("expected ;");
			}
		}

		void parse_simple_or_function_declaration(Namespace* scope)
		{
			DeclSpec ds = parse_decl_specifiers(scope, false);
			if (ts.peek_kind() == OP_SEMICOLON)
			{
				ts.consume();
				return;
			}
			TypePtr base = ds.has_alias ? ds.alias_type : build_fundamental_type(ds);
			base = make_cv(base, ds.has_c, ds.has_v);
			if (ts.peek_kind() == OP_LPAREN || ts.peek_kind() == OP_STAR || ts.peek_kind() == OP_AMP ||
			    ts.peek_kind() == OP_LAND || ts.peek_kind() == TK_IDENTIFIER || ts.peek_kind() == OP_COLON2)
			{
				parse_function_or_variable_declaration(scope, ds, base, ts.peek());
				return;
			}
			error_here("expected declaration");
		}

		shared_ptr<Symbol> lookup_name(Namespace* scope, const string& name, bool want_namespace, bool want_type, bool allow_parents)
		{
			unordered_set<const Namespace*> seen;
			vector<Namespace*> stack;
			stack.push_back(scope);
			while (!stack.empty())
			{
				Namespace* cur = stack.back();
				stack.pop_back();
				if (!cur || seen.count(cur))
					continue;
				seen.insert(cur);

				unordered_map<string, shared_ptr<Symbol> >::iterator lit = ctx->local_symbols[cur].find(name);
				if (lit != ctx->local_symbols[cur].end())
				{
					shared_ptr<Symbol> sym = lit->second;
					if (symbol_matches(sym, want_namespace, want_type))
						return sym;
				}

				unordered_map<string, shared_ptr<Symbol> >::iterator it = cur->symbols.find(name);
				if (it != cur->symbols.end())
				{
					shared_ptr<Symbol> sym = it->second;
					if (symbol_matches(sym, want_namespace, want_type))
						return sym;
				}

				if (allow_parents && cur->parent)
					stack.push_back(cur->parent);
				for (size_t i = 0; i < cur->using_directives.size(); ++i)
					stack.push_back(cur->using_directives[i]);
				for (size_t i = 0; i < cur->child_order.size(); ++i)
				{
					Namespace* child = cur->child_order[i];
					if (child->unnamed || child->inline_ns)
						stack.push_back(child);
				}
			}
			return shared_ptr<Symbol>();
		}

		bool symbol_matches(const shared_ptr<Symbol>& sym, bool want_namespace, bool want_type)
		{
			if (!sym)
				return false;
			shared_ptr<Symbol> resolved = resolve_alias(sym);
			if (!resolved)
				return false;
			if (want_namespace)
				return resolved->kind == Symbol::NamespaceKind;
			if (want_type)
				return resolved->kind == Symbol::TypeAliasKind;
			return true;
		}

		shared_ptr<Symbol> resolve_alias(const shared_ptr<Symbol>& sym)
		{
			shared_ptr<Symbol> cur = sym;
			unordered_set<const Symbol*> seen;
			while (cur && cur->kind == Symbol::AliasKind)
			{
				if (seen.count(cur.get()))
					break;
				seen.insert(cur.get());
				cur = cur->target;
			}
			return cur;
		}

		ExprValue convert_initializer_to_entity_type(const TypePtr& dst, const ExprValue& src, const Token& loc, bool& used_temp)
		{
			string init_dbg, value_dbg;
			return convert_to_type(cur, dst, src, loc, used_temp, init_dbg, value_dbg);
		}

		void finalize_function_groups()
		{
		}
	};

	vector<Token> tokenize_file_recursive(const string& path, size_t tu_index, Program* prog, unordered_set<string>& once_files)
	{
		if (once_files.count(path))
			return vector<Token>();

		string source = read_file(path);
		vector<string> retained_lines;
		vector<size_t> line_map;
		vector<Token> included_tokens;
		istringstream iss(source);
		string line;
		size_t line_no = 0;
		bool saw_once = false;
		while (getline(iss, line))
		{
			++line_no;
			string trimmed = trim_left(line);
			if (trimmed.compare(0, 13, "#pragma once") == 0)
			{
				saw_once = true;
				continue;
			}
			if (trimmed.compare(0, 8, "#include") == 0)
			{
				size_t q1 = trimmed.find('"');
				if (q1 != string::npos)
				{
					size_t q2 = trimmed.find('"', q1 + 1);
					if (q2 != string::npos)
					{
						string inc = trimmed.substr(q1 + 1, q2 - q1 - 1);
						string inc_path = join_path(path, inc);
						vector<Token> included = tokenize_file_recursive(inc_path, tu_index, prog, once_files);
						included_tokens.insert(included_tokens.end(), included.begin(), included.end());
						continue;
					}
				}
			}
			retained_lines.push_back(line);
			line_map.push_back(line_no);
		}
		if (saw_once)
			once_files.insert(path);

		string filtered;
		for (size_t i = 0; i < retained_lines.size(); ++i)
		{
			filtered += retained_lines[i];
			filtered.push_back('\n');
		}

		Sink sink;
		PPTokenizer tok(sink);
		for (size_t i = 0; i < filtered.size(); ++i)
			tok.process(static_cast<unsigned char>(filtered[i]));
		tok.process(EndOfFile);

		vector<Token> out;
		for (size_t i = 0; i < sink.toks.size(); ++i)
		{
			const Tok& rt = sink.toks[i];
			if (rt.kind == Kind::WS || rt.kind == Kind::NL)
				continue;
			if (rt.kind == Kind::EOFK)
				break;
			Token t;
			t.file = path;
			t.line = (rt.line >= 1 && rt.line <= line_map.size()) ? line_map[rt.line - 1] : rt.line;
			t.raw_kind = rt.kind;
			if (rt.kind == Kind::IDENT)
			{
				unordered_map<string, ETokenType>::const_iterator it = StringToTokenTypeMap.find(rt.data);
				if (it != StringToTokenTypeMap.end())
					t.kind = static_cast<int>(it->second);
				else
					t.kind = TK_IDENTIFIER;
				t.raw_kind = Kind::IDENT;
				t.data = rt.data;
			}
			else if (rt.kind == Kind::PUNC)
			{
				unordered_map<string, ETokenType>::const_iterator it = StringToTokenTypeMap.find(rt.data);
				if (it == StringToTokenTypeMap.end())
					throw runtime_error(string("unexpected punctuator: ") + rt.data);
				t.kind = static_cast<int>(it->second);
				t.raw_kind = Kind::PUNC;
				t.data = rt.data;
			}
			else
			{
				if (rt.kind == Kind::CHAR || rt.kind == Kind::UCHAR || rt.kind == Kind::STR ||
				    rt.kind == Kind::USTR || rt.kind == Kind::PPNUM || rt.kind == Kind::HEADER)
					t.kind = TK_LITERAL;
				else
					t.kind = static_cast<int>(rt.kind);
				t.data = rt.data;
			}
			out.push_back(t);
		}
		out.insert(out.begin(), included_tokens.begin(), included_tokens.end());
		return out;
	}

	size_t align_up(size_t v, size_t a)
	{
		if (a == 0)
			return v;
		size_t r = v % a;
		if (r == 0)
			return v;
		return v + (a - r);
	}

	void append_block1_entity(Program* prog, const EntityPtr& e)
	{
		size_t pos = 4;
		if (!prog->block1.empty())
		{
			EntityPtr prev = prog->block1.back();
			pos = prev->offset + prev->size;
		}
		pos = align_up(pos, max<size_t>(1, e->align));
		e->offset = pos;
		e->offset_assigned = true;
		prog->block1.push_back(e);
	}

	void write_u64(vector<unsigned char>& out, size_t v)
	{
		for (size_t i = 0; i < 8; ++i)
			out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
	}

	void emit_entity_bytes(vector<unsigned char>& image, EntityPtr e)
	{
		size_t a = max<size_t>(1, e->align);
		size_t pos = image.size();
		size_t aligned = e->offset_assigned ? e->offset : align_up(pos, a);
		if (aligned > pos)
			image.insert(image.end(), aligned - pos, 0);
		if (e->kind == Entity::Function)
		{
			image.push_back('f');
			image.push_back('u');
			image.push_back('n');
			image.push_back('\0');
			return;
		}
		vector<unsigned char> bytes = e->bytes;
		if (e->address_target)
		{
			size_t addr = e->address_target->offset;
			bytes.assign(8, 0);
			memcpy(&bytes[0], &addr, 8);
		}
		image.insert(image.end(), bytes.begin(), bytes.end());
	}

	void assign_image_offsets(Program& prog)
	{
		size_t pos = 4;
		vector<EntityPtr> all;
		all.insert(all.end(), prog.block1.begin(), prog.block1.end());
		all.insert(all.end(), prog.block2.begin(), prog.block2.end());
		all.insert(all.end(), prog.block3.begin(), prog.block3.end());
		stable_sort(all.begin(), all.end(), [](const EntityPtr& a, const EntityPtr& b)
		{
			if (a->was_forward_declared != b->was_forward_declared)
				return a->was_forward_declared > b->was_forward_declared;
			return false;
		});
		for (size_t i = 0; i < all.size(); ++i)
		{
			EntityPtr e = all[i];
			pos = align_up(pos, max<size_t>(1, e->align));
			e->offset = pos;
			e->offset_assigned = true;
			pos += e->size;
		}
	}

	void emit_program_image(Program& prog, const string& outfile)
	{
		assign_image_offsets(prog);
		vector<unsigned char> image;
		image.push_back('P');
		image.push_back('A');
		image.push_back('8');
		image.push_back('\0');
		vector<EntityPtr> all;
		all.insert(all.end(), prog.block1.begin(), prog.block1.end());
		all.insert(all.end(), prog.block2.begin(), prog.block2.end());
		all.insert(all.end(), prog.block3.begin(), prog.block3.end());
		stable_sort(all.begin(), all.end(), [](const EntityPtr& a, const EntityPtr& b)
		{
			if (a->was_forward_declared != b->was_forward_declared)
				return a->was_forward_declared > b->was_forward_declared;
			return false;
		});
		for (size_t i = 0; i < all.size(); ++i)
			emit_entity_bytes(image, all[i]);

		ofstream out(outfile.c_str(), ios::out | ios::binary);
		out.write(reinterpret_cast<const char*>(image.data()), image.size());
	}
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

		pa8nsinit::Program prog;
		prog.root.unnamed = false;
		prog.root.name = string();
		prog.root.parent = 0;

		for (size_t i = 0; i < nsrcfiles; ++i)
		{
			string srcfile = args[i + 2];
			pa8nsinit::TUContext ctx;
			ctx.tu_index = i;
			ctx.current_file = srcfile;
			unordered_set<string> once_files = prog.once_files;
			vector<pa8nsinit::Token> toks = pa8nsinit::tokenize_file_recursive(srcfile, i, &prog, once_files);
			prog.once_files.insert(once_files.begin(), once_files.end());
			pa8nsinit::Parser parser(toks, &prog, &ctx, &prog.root);
			parser.parse_translation_unit();
			for (size_t j = 0; j < ctx.logs.size(); ++j)
				cout << ctx.logs[j] << '\n';
			prog.logs.insert(prog.logs.end(), ctx.logs.begin(), ctx.logs.end());
		}

		pa8nsinit::emit_program_image(prog, outfile);
		return EXIT_SUCCESS;
	}
	catch (const pa8nsinit::ParseError& e)
	{
		cerr << e.file << ":" << e.line << ":1: error: " << e.what() << endl;
		return EXIT_FAILURE;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
