// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <iostream>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <climits>

using namespace std;

#define CPPGM_PREPROC_LIBRARY
#include "preproc.cpp"

namespace pa7nsdecl
{
	enum
	{
		TK_EOF = -1,
		TK_IDENTIFIER = -2,
		TK_LITERAL = -3
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

	struct Type;
	typedef shared_ptr<Type> TypePtr;
	struct Namespace;

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
		bool grouped = false;
	};

	struct Symbol;

	struct Namespace
	{
		string name;
		bool unnamed = true;
		bool inline_ns = false;
		Namespace* parent = 0;
		Namespace* unnamed_child = 0;
		unordered_map<string, shared_ptr<Symbol> > symbols;
		vector<Namespace*> child_order;
		vector<Namespace*> using_directives;
		vector<shared_ptr<Namespace> > owned_children;
		vector<Symbol*> variables;
		vector<Symbol*> functions;
	};

	struct Symbol
	{
		enum Kind
		{
			NamespaceKind,
			VariableKind,
			FunctionKind,
			TypeAliasKind,
			AliasKind
		};

		string name;
		Kind kind = TypeAliasKind;
		Namespace* owner = 0;
		Namespace* ns = 0;
		TypePtr type;
		shared_ptr<Symbol> target;
		bool listed = false;
	};

	struct ParamResult
	{
		TypePtr type;
		bool empty_void = false;
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
		if (inner->kind == Type::Cv)
		{
			inner->is_const = inner->is_const || is_const;
			inner->is_volatile = inner->is_volatile || is_volatile;
			return inner;
		}
		if (inner->kind == Type::LRef || inner->kind == Type::RRef)
			return inner;
		if (inner->kind == Type::ArrayUnknown || inner->kind == Type::ArrayBound)
		{
			TypePtr elem = make_cv(inner->inner, is_const, is_volatile);
			TypePtr t(new Type());
			t->kind = inner->kind;
			t->inner = elem;
			t->bound = inner->bound;
			return t;
		}
		if (!is_const && !is_volatile)
			return inner;
		TypePtr t(new Type());
		t->kind = Type::Cv;
		t->is_const = is_const;
		t->is_volatile = is_volatile;
		t->inner = inner;
		return t;
	}

	TypePtr make_pointer(TypePtr inner)
	{
		TypePtr t(new Type());
		t->kind = Type::Pointer;
		t->inner = inner;
		return t;
	}

	TypePtr make_lref(TypePtr inner)
	{
		if (inner && (inner->kind == Type::LRef || inner->kind == Type::RRef))
			return make_lref(inner->inner);
		TypePtr t(new Type());
		t->kind = Type::LRef;
		t->inner = inner;
		return t;
	}

	TypePtr make_rref(TypePtr inner)
	{
		if (inner && inner->kind == Type::LRef)
			return make_lref(inner->inner);
		if (inner && inner->kind == Type::RRef)
			return make_rref(inner->inner);
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

	bool is_reference_type(TypePtr t)
	{
		return t && (t->kind == Type::LRef || t->kind == Type::RRef);
	}

	TypePtr strip_top_cv(TypePtr t)
	{
		while (t && t->kind == Type::Cv)
			t = t->inner;
		return t;
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
		if (frag.grouped && frag.before_ops.size() == 1 && frag.before_ops[0].kind == PrefixOp::Pointer &&
		    frag.after_ops.size() == 2 && frag.after_ops[0].kind == SuffixOp::Function && frag.after_ops[1].kind == SuffixOp::Function)
		{
			TypePtr t = apply_suffix(frag.after_ops[0], base);
			t = apply_prefix(frag.before_ops[0], t);
			t = apply_suffix(frag.after_ops[1], t);
			return t;
		}
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

	string type_to_string(const TypePtr& t)
	{
		if (!t)
			return string();
		switch (t->kind)
		{
		case Type::Fundamental:
			return FundamentalTypeToStringMap.find(t->fundamental)->second;
		case Type::Cv:
		{
			string inner = type_to_string(t->inner);
			if (t->is_const && t->is_volatile)
				return string("const volatile ") + inner;
			if (t->is_const)
				return string("const ") + inner;
			return string("volatile ") + inner;
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

	shared_ptr<Symbol> resolve_symbol(const shared_ptr<Symbol>& sym)
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

	bool symbol_matches_kind(const shared_ptr<Symbol>& sym, bool want_namespace, bool want_type)
	{
		if (!sym)
			return false;
		shared_ptr<Symbol> resolved = resolve_symbol(sym);
		if (!resolved)
			return false;
		if (want_namespace)
			return resolved->kind == Symbol::NamespaceKind;
		if (want_type)
			return resolved->kind == Symbol::TypeAliasKind;
		return true;
	}

	shared_ptr<Symbol> lookup_name(Namespace* scope, const string& name, bool want_namespace, bool want_type, bool allow_parents = true)
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

			unordered_map<string, shared_ptr<Symbol> >::iterator it = cur->symbols.find(name);
			if (it != cur->symbols.end())
			{
				if (symbol_matches_kind(it->second, want_namespace, want_type))
					return resolve_symbol(it->second);
				return shared_ptr<Symbol>();
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

	Namespace* lookup_namespace_component(Namespace* scope, const string& name)
	{
		shared_ptr<Symbol> sym = lookup_name(scope, name, true, false, true);
		if (!sym)
			return 0;
		return sym->ns;
	}

	TypePtr lookup_type_name(Namespace* scope, const vector<string>& parts)
	{
		if (parts.empty())
			return TypePtr();

		Namespace* cur = scope;
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
		if (!sym)
			return TypePtr();
		sym = resolve_symbol(sym);
		if (!sym || sym->kind != Symbol::TypeAliasKind)
			return TypePtr();
		return sym->type;
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
		return out_value > 0;
	}

	struct DeclSpec
	{
		bool is_typedef = false;
		bool has_primary = false;
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
	};

	struct Parser
	{
		TokenStream ts;
		Namespace* root = 0;
		Namespace* cur = 0;

		explicit Parser(const vector<Token>& toks, Namespace* root_ns)
		{
			ts.toks = toks;
			root = root_ns;
			cur = root_ns;
		}

		bool accept_simple(int kind)
		{
			if (ts.peek_kind() == kind)
			{
				ts.consume();
				return true;
			}
			return false;
		}

		bool accept_ident(const string& s)
		{
			return ts.accept_ident(s);
		}

		bool peek_is_name_start() const
		{
			return ts.peek_kind() == TK_IDENTIFIER || ts.peek_kind() == OP_COLON2;
		}

		bool is_decl_start() const
		{
			int k = ts.peek_kind();
			return k == OP_SEMICOLON || k == KW_INLINE || k == KW_NAMESPACE || k == KW_USING ||
			       k == KW_TYPEDEF || k == KW_STATIC || k == KW_EXTERN || k == KW_THREAD_LOCAL ||
			       k == KW_CONST || k == KW_VOLATILE || k == KW_SIGNED || k == KW_UNSIGNED ||
			       k == KW_SHORT || k == KW_LONG || k == KW_CHAR || k == KW_CHAR16_T || k == KW_CHAR32_T ||
			       k == KW_WCHAR_T || k == KW_BOOL || k == KW_FLOAT || k == KW_DOUBLE || k == KW_VOID ||
			       k == TK_IDENTIFIER;
		}

		void parse_translation_unit()
		{
			while (!ts.at_end())
				parse_declaration(cur);
		}

		void parse_declaration(Namespace* scope)
		{
			if (accept_simple(OP_SEMICOLON))
				return;
			if (ts.peek_kind() == KW_NAMESPACE && ts.peek_kind(1) == TK_IDENTIFIER && ts.peek_kind(2) == OP_ASS)
			{
				parse_namespace_alias_definition(scope);
				return;
			}
			if (ts.peek_kind() == KW_INLINE || ts.peek_kind() == KW_NAMESPACE)
			{
				parse_namespace_definition(scope);
				return;
			}
			if (ts.peek_kind() == KW_USING)
			{
				parse_using_or_alias(scope);
				return;
			}
			if (ts.peek_kind() == KW_TYPEDEF)
			{
				parse_simple_declaration(scope, true);
				return;
			}
			parse_simple_declaration(scope, false);
		}

		void parse_namespace_definition(Namespace* scope)
		{
			bool is_inline = false;
			if (accept_simple(KW_INLINE))
				is_inline = true;
			if (!accept_simple(KW_NAMESPACE))
				throw ParseError("expected namespace");

			string name;
			bool has_name = false;
			if (ts.peek_kind() == TK_IDENTIFIER)
			{
				name = ts.peek().data;
				ts.consume();
				has_name = true;
			}

			Namespace* ns = 0;
			if (has_name)
			{
				unordered_map<string, shared_ptr<Symbol> >::iterator it = scope->symbols.find(name);
				if (it != scope->symbols.end() && it->second && it->second->kind == Symbol::NamespaceKind)
				{
					ns = it->second->ns;
				}
				else
				{
					shared_ptr<Namespace> child(new Namespace());
					child->name = name;
					child->unnamed = false;
					child->inline_ns = is_inline;
					child->parent = scope;
					ns = child.get();
					scope->owned_children.push_back(child);
					scope->child_order.push_back(ns);

					shared_ptr<Symbol> sym(new Symbol());
					sym->name = name;
					sym->kind = Symbol::NamespaceKind;
					sym->owner = scope;
					sym->ns = ns;
					scope->symbols[name] = sym;
				}
				ns->inline_ns = ns->inline_ns || is_inline;
			}
			else
			{
				if (scope->unnamed_child)
				{
					ns = scope->unnamed_child;
					ns->inline_ns = ns->inline_ns || is_inline;
				}
				else
				{
					shared_ptr<Namespace> child(new Namespace());
					child->unnamed = true;
					child->inline_ns = is_inline;
					child->parent = scope;
					ns = child.get();
					scope->owned_children.push_back(child);
					scope->child_order.push_back(ns);
					scope->unnamed_child = ns;
				}
			}

			if (!accept_simple(OP_LBRACE))
				throw ParseError("expected {");

			Namespace* save = cur;
			cur = ns;
			while (ts.peek_kind() != OP_RBRACE && !ts.at_end())
				parse_declaration(cur);
			if (!accept_simple(OP_RBRACE))
				throw ParseError("expected }");
			cur = save;
			accept_simple(OP_SEMICOLON);
		}

		void parse_using_or_alias(Namespace* scope)
		{
			if (!accept_simple(KW_USING))
				throw ParseError("expected using");
			if (accept_simple(KW_NAMESPACE))
			{
				bool leading_global = false;
				vector<string> parts;
				if (accept_simple(OP_COLON2))
					leading_global = true;
				while (true)
				{
					if (ts.peek_kind() != TK_IDENTIFIER)
						throw ParseError("expected namespace name");
					string part = ts.peek().data;
					ts.consume();
					if (!accept_simple(OP_COLON2))
					{
						parts.push_back(part);
						break;
					}
					Namespace* next = lookup_namespace_component(leading_global ? global_namespace(scope) : scope, part);
					if (!next)
						throw ParseError("expected namespace name");
					leading_global = false;
					scope = next;
				}
				Namespace* ns = lookup_namespace_path(scope, parts, leading_global);
				if (!ns)
					throw ParseError("expected namespace name");
				scope->using_directives.push_back(ns);
				if (!accept_simple(OP_SEMICOLON))
					throw ParseError("expected ;");
				return;
			}

			if (ts.peek_kind() == TK_IDENTIFIER && ts.peek_kind(1) == OP_ASS)
			{
				string alias_name = ts.peek().data;
				ts.consume();
				ts.consume();
				TypePtr aliased = parse_type_id(scope);
				if (!accept_simple(OP_SEMICOLON))
					throw ParseError("expected ;");
				shared_ptr<Symbol> sym(new Symbol());
				sym->name = alias_name;
				sym->kind = Symbol::TypeAliasKind;
				sym->owner = scope;
				sym->type = aliased;
				scope->symbols[alias_name] = sym;
				return;
			}

			vector<string> parts;
			bool leading_global = false;
			if (accept_simple(OP_COLON2))
				leading_global = true;
			while (true)
			{
				if (ts.peek_kind() != TK_IDENTIFIER)
					throw ParseError("expected identifier");
				string part = ts.peek().data;
				ts.consume();
				if (!accept_simple(OP_COLON2))
				{
					parts.push_back(part);
					break;
				}
				parts.push_back(part);
			}
			if (parts.empty())
				throw ParseError("expected identifier");
			string member = parts.back();
			parts.pop_back();
			Namespace* ns = lookup_namespace_path(scope, parts, leading_global);
			if (!ns)
				throw ParseError("expected namespace name");
			shared_ptr<Symbol> target = lookup_name(ns, member, false, false, true);
			if (!target)
				throw ParseError("expected identifier");
			shared_ptr<Symbol> sym(new Symbol());
			sym->name = member;
			sym->kind = Symbol::AliasKind;
			sym->owner = scope;
			sym->target = target;
			scope->symbols[member] = sym;
			if (!accept_simple(OP_SEMICOLON))
				throw ParseError("expected ;");
		}

		void parse_namespace_alias_definition(Namespace* scope)
		{
			if (!accept_simple(KW_NAMESPACE))
				throw ParseError("expected namespace");
			if (ts.peek_kind() != TK_IDENTIFIER)
				throw ParseError("expected identifier");
			string alias_name = ts.peek().data;
			ts.consume();
			if (!accept_simple(OP_ASS))
				throw ParseError("expected =");

			bool leading_global = false;
			vector<string> parts;
			if (accept_simple(OP_COLON2))
				leading_global = true;
			while (true)
			{
				if (ts.peek_kind() != TK_IDENTIFIER)
					throw ParseError("expected namespace name");
				string part = ts.peek().data;
				ts.consume();
				if (!accept_simple(OP_COLON2))
				{
					parts.push_back(part);
					break;
				}
				parts.push_back(part);
			}
			Namespace* target = lookup_namespace_path(scope, parts, leading_global);
			if (!target)
				throw ParseError("expected namespace name");
			if (!accept_simple(OP_SEMICOLON))
				throw ParseError("expected ;");

			shared_ptr<Symbol> sym(new Symbol());
			sym->name = alias_name;
			sym->kind = Symbol::AliasKind;
			sym->owner = scope;
			shared_ptr<Symbol> target_sym(new Symbol());
			target_sym->kind = Symbol::NamespaceKind;
			target_sym->ns = target;
			sym->target = target_sym;
			scope->symbols[alias_name] = sym;
		}

		void parse_simple_declaration(Namespace* scope, bool forced_typedef)
		{
			DeclSpec ds = parse_decl_specifiers(scope, forced_typedef);
			if (ts.peek_kind() == OP_SEMICOLON)
			{
				ts.consume();
				return;
			}

			while (true)
			{
				Fragment frag = parse_declarator_fragment(scope, false);
				TypePtr base = ds.has_alias ? ds.alias_type : build_fundamental_type(ds);
				base = make_cv(base, ds.has_c, ds.has_v);
				TypePtr final_type = eval_fragment(frag, base);
				if (!final_type)
					throw ParseError("expected declaration");
				declare_entity(scope, frag, final_type, ds.is_typedef);
				if (!accept_simple(OP_COMMA))
					break;
			}
			if (!accept_simple(OP_SEMICOLON))
				throw ParseError("expected ;");
		}

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
				if (k == KW_STATIC || k == KW_EXTERN || k == KW_THREAD_LOCAL)
				{
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
					Namespace* save_scope = scope;
					size_t save_pos = ts.pos;
					TypePtr alias = try_parse_type_name(scope);
					if (alias)
					{
						ds.has_alias = true;
						ds.alias_type = alias;
						ds.has_primary = true;
						saw_any = true;
						continue;
					}
					ts.pos = save_pos;
					(void)save_scope;
				}
				break;
			}
			if (!saw_any)
				throw ParseError("expected declaration specifiers");
			return ds;
		}

		TypePtr try_parse_type_name(Namespace* scope)
		{
			size_t save = ts.pos;
			bool leading_global = false;
			vector<string> parts;
			if (accept_simple(OP_COLON2))
				leading_global = true;
			if (ts.peek_kind() != TK_IDENTIFIER)
			{
				ts.pos = save;
				return TypePtr();
			}

			// Parse as namespace path + typedef name if there is at least one ::
			while (true)
			{
				string part = ts.peek().data;
				ts.consume();
				if (!accept_simple(OP_COLON2))
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
				if (sym)
				{
					sym = resolve_symbol(sym);
					if (sym && sym->kind == Symbol::TypeAliasKind)
						aliased = sym->type;
				}
			}
			else
			{
				Namespace* ns = lookup_namespace_path(scope, vector<string>(parts.begin(), parts.end() - 1), leading_global);
				if (ns)
				{
					shared_ptr<Symbol> sym = lookup_name(ns, parts.back(), false, true, true);
					sym = resolve_symbol(sym);
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

		vector<string> parse_namespace_path_tokens()
		{
			vector<string> out;
			while (ts.peek_kind() == TK_IDENTIFIER)
			{
				string part = ts.peek().data;
				ts.consume();
				if (!accept_simple(OP_COLON2))
				{
					out.push_back(part);
					break;
				}
				out.push_back(part);
			}
			return out;
		}

		bool can_start_declarator() const
		{
			int k = ts.peek_kind();
			return k == TK_IDENTIFIER || k == OP_COLON2 || k == OP_LPAREN || k == OP_STAR || k == OP_AMP || k == OP_LAND;
		}

		Fragment parse_declarator_fragment(Namespace* scope, bool allow_empty_name)
		{
			vector<PrefixOp> prefixes = parse_prefix_ops();
			Fragment frag;

			bool looks_like_suffix_only = allow_empty_name && ts.peek_kind() == OP_LPAREN &&
				ts.peek_kind(1) != OP_STAR && ts.peek_kind(1) != OP_AMP && ts.peek_kind(1) != OP_LAND &&
				ts.peek_kind(1) != OP_LPAREN;

			if (looks_like_suffix_only)
			{
				frag.before_ops = prefixes;
			}
			else if (ts.peek_kind() == OP_LPAREN)
			{
				ts.consume();
				frag = parse_declarator_fragment(scope, true);
				if (!accept_simple(OP_RPAREN))
					throw ParseError("expected )");
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
			else if (allow_empty_name)
			{
				frag.before_ops = prefixes;
			}
			else
			{
				throw ParseError("expected declarator");
			}

			vector<SuffixOp> suffixes = parse_suffixes(scope);
			frag.after_ops.insert(frag.after_ops.end(), suffixes.begin(), suffixes.end());
			return frag;
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
			if (accept_simple(OP_COLON2))
				leading_global = true;
			while (true)
			{
				if (ts.peek_kind() != TK_IDENTIFIER)
					throw ParseError("expected identifier");
				string part = ts.peek().data;
				ts.consume();
				if (!accept_simple(OP_COLON2))
				{
					parts.push_back(part);
					break;
				}
				parts.push_back(part);
			}

			if (parts.empty())
				throw ParseError("expected identifier");
			frag.name = parts.back();
			frag.has_name = true;
			parts.pop_back();
			if (parts.empty())
			{
				frag.qualified_scope = leading_global ? global_namespace(scope) : scope;
				return;
			}
			Namespace* ns = lookup_namespace_path(scope, parts, leading_global);
			if (!ns)
				throw ParseError("expected namespace name");
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
					if (!accept_simple(OP_RPAREN))
						throw ParseError("expected )");
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
						if (ts.peek_kind() != TK_LITERAL)
							throw ParseError("expected literal");
						size_t bound = 0;
						if (!parse_integer_literal(ts.peek().data, bound))
							throw ParseError("expected literal");
						op.unknown_bound = false;
						op.bound = bound;
						ts.consume();
					}
					if (!accept_simple(OP_RSQUARE))
						throw ParseError("expected ]");
					suffixes.push_back(op);
					continue;
				}
				break;
			}
			return suffixes;
		}

		ParamResult parse_parameter_declaration(Namespace* scope)
		{
			DeclSpec ds = parse_decl_specifiers(scope, false);
			ParamResult pr;
			pr.type = ds.has_alias ? ds.alias_type : build_fundamental_type(ds);
			pr.type = make_cv(pr.type, ds.has_c, ds.has_v);

			bool has_decl = false;
			if (can_start_declarator())
			{
				size_t save = ts.pos;
				try
				{
					Fragment frag = parse_declarator_fragment(scope, true);
					has_decl = true;
					pr.type = eval_fragment(frag, pr.type);
				}
				catch (exception&)
				{
					ts.pos = save;
				}
			}

			if (!has_decl && pr.type && pr.type->kind == Type::Fundamental && pr.type->fundamental == FT_VOID)
				pr.empty_void = true;

			if (has_decl)
				pr.type = adjust_parameter_type(pr.type);
			else if (!pr.empty_void)
				pr.type = adjust_parameter_type(pr.type);
			return pr;
		}

		TypePtr adjust_parameter_type(TypePtr t)
		{
			if (!t)
				return t;
			if (t->kind == Type::ArrayUnknown || t->kind == Type::ArrayBound)
				t = make_pointer(t->inner);
			else if (t->kind == Type::Function)
				t = make_pointer(t);
			t = strip_top_cv(t);
			return t;
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
				if (!accept_simple(OP_COMMA))
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

		TypePtr parse_type_id(Namespace* scope)
		{
			DeclSpec ds = parse_decl_specifiers(scope, false);
			TypePtr t = ds.has_alias ? ds.alias_type : build_fundamental_type(ds);
			t = make_cv(t, ds.has_c, ds.has_v);
			if (can_start_declarator())
			{
				Fragment frag = parse_declarator_fragment(scope, true);
				t = eval_fragment(frag, t);
			}
			return t;
		}

		TypePtr make_cv(TypePtr t, bool c, bool v)
		{
			return pa7nsdecl::make_cv(t, c, v);
		}

		void declare_entity(Namespace* scope, const Fragment& frag, TypePtr t, bool is_typedef)
		{
			if (!frag.has_name)
				return;
			Namespace* target_scope = frag.qualified_scope ? frag.qualified_scope : scope;
			string name = frag.name;
			shared_ptr<Symbol> sym;
			unordered_map<string, shared_ptr<Symbol> >::iterator it = target_scope->symbols.find(name);
			if (it != target_scope->symbols.end())
				sym = it->second;
			if (!sym)
			{
				sym.reset(new Symbol());
				sym->name = name;
				sym->owner = target_scope;
				target_scope->symbols[name] = sym;
			}

			if (is_typedef)
			{
				sym->kind = Symbol::TypeAliasKind;
				sym->type = t;
				return;
			}

			if (t && t->kind == Type::Function)
			{
				sym->kind = Symbol::FunctionKind;
				sym->type = t;
				if (!sym->listed)
				{
					target_scope->functions.push_back(sym.get());
					sym->listed = true;
				}
			}
			else
			{
				sym->kind = Symbol::VariableKind;
				sym->type = t;
				if (!sym->listed)
				{
					target_scope->variables.push_back(sym.get());
					sym->listed = true;
				}
			}
		}
	};

	void print_namespace(ostream& out, Namespace* ns)
	{
		if (ns->unnamed)
			out << "start unnamed namespace" << '\n';
		else
			out << "start namespace " << ns->name << '\n';

		if (ns->inline_ns)
			out << "inline namespace" << '\n';

		for (size_t i = 0; i < ns->variables.size(); ++i)
		{
			Symbol* s = ns->variables[i];
			if (s && s->type)
				out << "variable " << s->name << " " << type_to_string(s->type) << '\n';
		}
		for (size_t i = 0; i < ns->functions.size(); ++i)
		{
			Symbol* s = ns->functions[i];
			if (s && s->type)
				out << "function " << s->name << " " << type_to_string(s->type) << '\n';
		}
		for (size_t i = 0; i < ns->child_order.size(); ++i)
			print_namespace(out, ns->child_order[i]);

		out << "end namespace" << '\n';
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
				Token eof;
				eof.kind = TK_EOF;
				out.push_back(eof);
				break;
			}
			if (t.kind == Kind::IDENT)
			{
				Token x;
				unordered_map<string, ETokenType>::const_iterator it = StringToTokenTypeMap.find(t.data);
				x.kind = (it == StringToTokenTypeMap.end()) ? TK_IDENTIFIER : static_cast<int>(it->second);
				x.data = t.data;
				out.push_back(x);
				continue;
			}
			if (t.kind == Kind::PPNUM || t.kind == Kind::CHAR || t.kind == Kind::UCHAR || t.kind == Kind::STR || t.kind == Kind::USTR)
			{
				Token x;
				x.kind = TK_LITERAL;
				x.data = t.data;
				out.push_back(x);
				continue;
			}
			if (t.kind == Kind::PUNC)
			{
				unordered_map<string, ETokenType>::const_iterator it = StringToTokenTypeMap.find(t.data);
				if (it == StringToTokenTypeMap.end())
					throw ParseError(string("unexpected punctuator: ") + t.data);
				Token x;
				x.kind = static_cast<int>(it->second);
				x.data = t.data;
				out.push_back(x);
				continue;
			}
			throw ParseError(string("unexpected token: ") + t.data);
		}
		if (out.empty() || out.back().kind != TK_EOF)
		{
			Token eof;
			eof.kind = TK_EOF;
			out.push_back(eof);
		}
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
		if (author != 0 && *author != '\0')
			st.author = author;
		else
			st.author = "Jesse Andrews";
		return pa5preproc::process_file(st, actual_path, logical_name);
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
		ofstream out(outfile.c_str());
		out << nsrcfiles << " translation units" << '\n';

		for (size_t i = 0; i < nsrcfiles; ++i)
		{
			string srcfile = args[i + 2];
			string expanded = pa7nsdecl::preprocess_source(srcfile, srcfile);
			vector<pa7nsdecl::Token> toks = pa7nsdecl::tokenize_source(expanded);

			shared_ptr<pa7nsdecl::Namespace> root(new pa7nsdecl::Namespace());
			root->unnamed = true;
			root->name.clear();
			root->parent = 0;

			pa7nsdecl::Parser parser(toks, root.get());
			parser.parse_translation_unit();

			out << "start translation unit " << srcfile << '\n';
			pa7nsdecl::print_namespace(out, root.get());
			out << "end translation unit" << '\n';
		}
		return EXIT_SUCCESS;
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
