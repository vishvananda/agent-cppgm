// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <algorithm>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

#define CPPGM_PREPROC_NO_MAIN
#include "preproc.cpp"

struct PA7Token
{
	string kind;
	string source;
};

string PA7TrimRight(const string& s)
{
	size_t e = s.size();
	while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
	return s.substr(0, e);
}

bool PA7StartsWith(const string& s, const string& prefix)
{
	return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

string PA7SecondField(const string& s)
{
	size_t p = s.find(' ');
	if (p == string::npos) return s;
	return s.substr(0, p);
}

vector<PA7Token> PA7ToTokens(const vector<PPToken>& preprocessed)
{
	ostringstream cap;
	EmitPostTokensOrThrow(cap, preprocessed);

	vector<PA7Token> out;
	istringstream iss(cap.str());
	string line;
	while (getline(iss, line))
	{
		line = PA7TrimRight(line);
		if (line.empty()) continue;

		if (PA7StartsWith(line, "simple "))
		{
			size_t last_space = line.find_last_of(' ');
			if (last_space == string::npos || last_space <= 7)
				throw runtime_error("bad posttoken simple line");
			string source = line.substr(7, last_space - 7);
			string kind = line.substr(last_space + 1);
			out.push_back({kind, source});
			continue;
		}

		if (PA7StartsWith(line, "identifier "))
		{
			out.push_back({"TT_IDENTIFIER", line.substr(11)});
			continue;
		}

		if (PA7StartsWith(line, "literal "))
		{
			string src = PA7SecondField(line.substr(8));
			out.push_back({"TT_LITERAL", src});
			continue;
		}

		if (PA7StartsWith(line, "user-defined-literal "))
		{
			string src = PA7SecondField(line.substr(21));
			out.push_back({"TT_LITERAL", src});
			continue;
		}

		if (PA7StartsWith(line, "invalid "))
			throw runtime_error("invalid token in phase 7");

		if (line == "eof")
			continue;

		throw runtime_error("bad posttoken output");
	}

	out.push_back({"ST_EOF", ""});
	return out;
}

struct PA7Type;
typedef shared_ptr<PA7Type> PA7TypePtr;

struct PA7Type
{
	enum Kind
	{
		TK_FUNDAMENTAL,
		TK_CV,
		TK_POINTER,
		TK_LVALUE_REF,
		TK_RVALUE_REF,
		TK_ARRAY,
		TK_FUNCTION
	};

	Kind kind;

	// TK_FUNDAMENTAL
	string fundamental;

	// TK_CV
	bool cv_const = false;
	bool cv_volatile = false;
	PA7TypePtr sub;

	// TK_ARRAY
	bool array_known_bound = false;
	unsigned long long array_bound = 0;

	// TK_FUNCTION
	vector<PA7TypePtr> params;
	bool variadic = false;
};

PA7TypePtr PA7MakeFundamental(const string& f)
{
	PA7TypePtr t = make_shared<PA7Type>();
	t->kind = PA7Type::TK_FUNDAMENTAL;
	t->fundamental = f;
	return t;
}

PA7TypePtr PA7MakeCv(const PA7TypePtr& sub, bool c, bool v)
{
	if (!c && !v) return sub;

	if (sub->kind == PA7Type::TK_LVALUE_REF || sub->kind == PA7Type::TK_RVALUE_REF)
		return sub;

	if (sub->kind == PA7Type::TK_ARRAY)
	{
		PA7TypePtr t = make_shared<PA7Type>(*sub);
		t->sub = PA7MakeCv(sub->sub, c, v);
		return t;
	}

	if (sub->kind == PA7Type::TK_CV)
	{
		PA7TypePtr t = make_shared<PA7Type>(*sub);
		t->cv_const = t->cv_const || c;
		t->cv_volatile = t->cv_volatile || v;
		return t;
	}

	PA7TypePtr t = make_shared<PA7Type>();
	t->kind = PA7Type::TK_CV;
	t->cv_const = c;
	t->cv_volatile = v;
	t->sub = sub;
	return t;
}

PA7TypePtr PA7MakePointer(const PA7TypePtr& sub, bool c = false, bool v = false)
{
	PA7TypePtr p = make_shared<PA7Type>();
	p->kind = PA7Type::TK_POINTER;
	p->sub = sub;
	return PA7MakeCv(p, c, v);
}

PA7TypePtr PA7MakeLRef(const PA7TypePtr& sub)
{
	if (sub->kind == PA7Type::TK_LVALUE_REF) return sub;
	if (sub->kind == PA7Type::TK_RVALUE_REF)
	{
		PA7TypePtr t = make_shared<PA7Type>();
		t->kind = PA7Type::TK_LVALUE_REF;
		t->sub = sub->sub;
		return t;
	}
	PA7TypePtr t = make_shared<PA7Type>();
	t->kind = PA7Type::TK_LVALUE_REF;
	t->sub = sub;
	return t;
}

PA7TypePtr PA7MakeRRef(const PA7TypePtr& sub)
{
	if (sub->kind == PA7Type::TK_LVALUE_REF) return sub;
	if (sub->kind == PA7Type::TK_RVALUE_REF) return sub;
	PA7TypePtr t = make_shared<PA7Type>();
	t->kind = PA7Type::TK_RVALUE_REF;
	t->sub = sub;
	return t;
}

PA7TypePtr PA7MakeArray(const PA7TypePtr& elem, bool known, unsigned long long bound)
{
	PA7TypePtr t = make_shared<PA7Type>();
	t->kind = PA7Type::TK_ARRAY;
	t->sub = elem;
	t->array_known_bound = known;
	t->array_bound = bound;
	return t;
}

PA7TypePtr PA7MakeFunction(const PA7TypePtr& ret, const vector<PA7TypePtr>& params, bool variadic)
{
	PA7TypePtr t = make_shared<PA7Type>();
	t->kind = PA7Type::TK_FUNCTION;
	t->sub = ret;
	t->params = params;
	t->variadic = variadic;
	return t;
}

bool PA7TypeEquals(const PA7TypePtr& a, const PA7TypePtr& b)
{
	if (a->kind != b->kind) return false;
	switch (a->kind)
	{
	case PA7Type::TK_FUNDAMENTAL:
		return a->fundamental == b->fundamental;
	case PA7Type::TK_CV:
		return a->cv_const == b->cv_const && a->cv_volatile == b->cv_volatile && PA7TypeEquals(a->sub, b->sub);
	case PA7Type::TK_POINTER:
	case PA7Type::TK_LVALUE_REF:
	case PA7Type::TK_RVALUE_REF:
		return PA7TypeEquals(a->sub, b->sub);
	case PA7Type::TK_ARRAY:
		return a->array_known_bound == b->array_known_bound &&
			(!a->array_known_bound || a->array_bound == b->array_bound) &&
			PA7TypeEquals(a->sub, b->sub);
	case PA7Type::TK_FUNCTION:
		if (!PA7TypeEquals(a->sub, b->sub)) return false;
		if (a->variadic != b->variadic) return false;
		if (a->params.size() != b->params.size()) return false;
		for (size_t i = 0; i < a->params.size(); i++)
			if (!PA7TypeEquals(a->params[i], b->params[i])) return false;
		return true;
	}
	return false;
}

string PA7TypeToString(const PA7TypePtr& t)
{
	switch (t->kind)
	{
	case PA7Type::TK_FUNDAMENTAL:
		return t->fundamental;
	case PA7Type::TK_CV:
	{
		string p;
		if (t->cv_const && t->cv_volatile) p = "const volatile ";
		else if (t->cv_const) p = "const ";
		else p = "volatile ";
		return p + PA7TypeToString(t->sub);
	}
	case PA7Type::TK_POINTER:
		return string("pointer to ") + PA7TypeToString(t->sub);
	case PA7Type::TK_LVALUE_REF:
		return string("lvalue-reference to ") + PA7TypeToString(t->sub);
	case PA7Type::TK_RVALUE_REF:
		return string("rvalue-reference to ") + PA7TypeToString(t->sub);
	case PA7Type::TK_ARRAY:
		if (t->array_known_bound)
		{
			ostringstream oss;
			oss << "array of " << t->array_bound << " " << PA7TypeToString(t->sub);
			return oss.str();
		}
		return string("array of unknown bound of ") + PA7TypeToString(t->sub);
	case PA7Type::TK_FUNCTION:
	{
		ostringstream oss;
		oss << "function of (";
		if (t->params.empty())
		{
			if (t->variadic) oss << "...";
		}
		else
		{
			for (size_t i = 0; i < t->params.size(); i++)
			{
				if (i) oss << ", ";
				oss << PA7TypeToString(t->params[i]);
			}
			if (t->variadic) oss << ", ...";
		}
		oss << ") returning " << PA7TypeToString(t->sub);
		return oss.str();
	}
	}
	return "<?>"; // unreachable
}

bool PA7IsVoidType(const PA7TypePtr& t)
{
	if (t->kind == PA7Type::TK_FUNDAMENTAL) return t->fundamental == "void";
	if (t->kind == PA7Type::TK_CV) return PA7IsVoidType(t->sub);
	return false;
}

PA7TypePtr PA7AdjustFunctionParamType(PA7TypePtr t)
{
	// drop top-level cv qualifiers for non-reference parameter types
	if (t->kind == PA7Type::TK_CV)
		t = t->sub;

	// array/function parameter adjustments
	if (t->kind == PA7Type::TK_ARRAY)
		return PA7MakePointer(t->sub);
	if (t->kind == PA7Type::TK_FUNCTION)
		return PA7MakePointer(t);

	return t;
}

struct PA7Variable
{
	string name;
	PA7TypePtr type;
};

struct PA7Function
{
	string name;
	PA7TypePtr type;
};

struct PA7Namespace
{
	PA7Namespace* parent = nullptr;
	bool named = false;
	string name;
	bool is_inline = false;

	vector<unique_ptr<PA7Namespace>> owned_children;
	vector<PA7Namespace*> namespaces_in_order;
	unordered_map<string, PA7Namespace*> named_children;
	PA7Namespace* unnamed_child = nullptr;

	vector<PA7Variable> vars_in_order;
	unordered_map<string, size_t> var_idx;
	vector<PA7Function> funcs_in_order;
	unordered_map<string, size_t> func_idx;

	unordered_map<string, PA7TypePtr> type_aliases;
	unordered_map<string, PA7Namespace*> namespace_aliases;
	vector<PA7Namespace*> using_directives;
};

void PA7AddUsingDirective(PA7Namespace* ns, PA7Namespace* target)
{
	for (size_t i = 0; i < ns->using_directives.size(); i++)
	{
		if (ns->using_directives[i] == target) return;
	}
	ns->using_directives.push_back(target);
}

PA7Namespace* PA7CreateUnnamedNamespace(PA7Namespace* parent)
{
	if (parent->unnamed_child)
		return parent->unnamed_child;

	unique_ptr<PA7Namespace> p(new PA7Namespace());
	p->parent = parent;
	p->named = false;
	PA7Namespace* raw = p.get();
	parent->owned_children.push_back(move(p));
	parent->namespaces_in_order.push_back(raw);
	parent->unnamed_child = raw;
	return raw;
}

PA7Namespace* PA7GetOrCreateNamedNamespace(PA7Namespace* parent, const string& name)
{
	auto it = parent->named_children.find(name);
	if (it != parent->named_children.end())
		return it->second;

	unique_ptr<PA7Namespace> p(new PA7Namespace());
	p->parent = parent;
	p->named = true;
	p->name = name;
	PA7Namespace* raw = p.get();
	parent->owned_children.push_back(move(p));
	parent->named_children[name] = raw;
	parent->namespaces_in_order.push_back(raw);
	return raw;
}

PA7TypePtr PA7LookupTypeInNamespace(PA7Namespace* ns, const string& name, unordered_set<PA7Namespace*>& visited);
PA7Namespace* PA7LookupNamespaceInNamespace(PA7Namespace* ns, const string& name, unordered_set<PA7Namespace*>& visited);

PA7TypePtr PA7LookupTypeInNamespace(PA7Namespace* ns, const string& name, unordered_set<PA7Namespace*>& visited)
{
	if (!ns) return nullptr;
	if (visited.find(ns) != visited.end()) return nullptr;
	visited.insert(ns);

	auto it = ns->type_aliases.find(name);
	if (it != ns->type_aliases.end()) return it->second;

	for (size_t i = 0; i < ns->using_directives.size(); i++)
	{
		PA7TypePtr t = PA7LookupTypeInNamespace(ns->using_directives[i], name, visited);
		if (t) return t;
	}
	return nullptr;
}

PA7TypePtr PA7LookupTypeQualified(PA7Namespace* ns, const string& name)
{
	unordered_set<PA7Namespace*> visited;
	return PA7LookupTypeInNamespace(ns, name, visited);
}

PA7TypePtr PA7LookupTypeUnqualified(PA7Namespace* from, const string& name)
{
	for (PA7Namespace* ns = from; ns != nullptr; ns = ns->parent)
	{
		unordered_set<PA7Namespace*> visited;
		PA7TypePtr t = PA7LookupTypeInNamespace(ns, name, visited);
		if (t) return t;
	}
	return nullptr;
}

PA7Namespace* PA7LookupNamespaceInNamespace(PA7Namespace* ns, const string& name, unordered_set<PA7Namespace*>& visited)
{
	if (!ns) return nullptr;
	if (visited.find(ns) != visited.end()) return nullptr;
	visited.insert(ns);

	auto itc = ns->named_children.find(name);
	if (itc != ns->named_children.end()) return itc->second;

	auto ita = ns->namespace_aliases.find(name);
	if (ita != ns->namespace_aliases.end()) return ita->second;

	for (size_t i = 0; i < ns->using_directives.size(); i++)
	{
		PA7Namespace* x = PA7LookupNamespaceInNamespace(ns->using_directives[i], name, visited);
		if (x) return x;
	}
	return nullptr;
}

PA7Namespace* PA7LookupNamespaceQualified(PA7Namespace* ns, const string& name)
{
	unordered_set<PA7Namespace*> visited;
	return PA7LookupNamespaceInNamespace(ns, name, visited);
}

PA7Namespace* PA7LookupNamespaceUnqualified(PA7Namespace* from, const string& name)
{
	for (PA7Namespace* ns = from; ns != nullptr; ns = ns->parent)
	{
		unordered_set<PA7Namespace*> visited;
		PA7Namespace* x = PA7LookupNamespaceInNamespace(ns, name, visited);
		if (x) return x;
	}
	return nullptr;
}

void PA7DeclareVariable(PA7Namespace* ns, const string& name, const PA7TypePtr& type)
{
	auto it = ns->var_idx.find(name);
	if (it == ns->var_idx.end())
	{
		size_t idx = ns->vars_in_order.size();
		ns->var_idx[name] = idx;
		ns->vars_in_order.push_back({name, type});
		return;
	}

	PA7Variable& v = ns->vars_in_order[it->second];
	if (PA7TypeEquals(v.type, type)) return;

	// permit unknown-bound -> known-bound update for the same array element type
	if (v.type->kind == PA7Type::TK_ARRAY && type->kind == PA7Type::TK_ARRAY)
	{
		if (!v.type->array_known_bound && type->array_known_bound && PA7TypeEquals(v.type->sub, type->sub))
		{
			v.type = type;
			return;
		}
		if (v.type->array_known_bound && !type->array_known_bound && PA7TypeEquals(v.type->sub, type->sub))
			return;
	}

	// undefined behavior for conflicts; keep first declaration
}

void PA7DeclareFunction(PA7Namespace* ns, const string& name, const PA7TypePtr& type)
{
	auto it = ns->func_idx.find(name);
	if (it == ns->func_idx.end())
	{
		size_t idx = ns->funcs_in_order.size();
		ns->func_idx[name] = idx;
		ns->funcs_in_order.push_back({name, type});
		return;
	}

	PA7Function& f = ns->funcs_in_order[it->second];
	if (PA7TypeEquals(f.type, type)) return;
	// undefined behavior for overload set size > 1
}

struct PA7NamePrefix
{
	bool has_prefix = false;
	bool global = false;
	vector<string> components;
};

struct PA7Name
{
	bool qualified = false;
	PA7NamePrefix prefix;
	string name;
};

struct PA7PtrOp
{
	enum Kind
	{
		PO_POINTER,
		PO_LREF,
		PO_RREF
	};

	Kind kind;
	bool cv_const = false;
	bool cv_volatile = false;
};

struct PA7Suffix
{
	enum Kind
	{
		SF_FUNCTION,
		SF_ARRAY
	};

	Kind kind;
	vector<PA7TypePtr> params;
	bool variadic = false;
	bool array_has_bound = false;
	unsigned long long array_bound = 0;
};

typedef function<PA7TypePtr(const PA7TypePtr&)> PA7TypeTransform;

PA7TypeTransform PA7IdentityTransform()
{
	return [](const PA7TypePtr& b){ return b; };
}

PA7TypePtr PA7ApplyPtrOp(const PA7TypePtr& base, const PA7PtrOp& op)
{
	if (op.kind == PA7PtrOp::PO_POINTER)
		return PA7MakePointer(base, op.cv_const, op.cv_volatile);
	if (op.kind == PA7PtrOp::PO_LREF)
		return PA7MakeLRef(base);
	return PA7MakeRRef(base);
}

PA7TypePtr PA7ApplySuffix(const PA7TypePtr& base, const PA7Suffix& sfx)
{
	if (sfx.kind == PA7Suffix::SF_ARRAY)
		return PA7MakeArray(base, sfx.array_has_bound, sfx.array_bound);
	return PA7MakeFunction(base, sfx.params, sfx.variadic);
}

struct PA7Declarator
{
	bool has_name = false;
	PA7Name name;
	PA7TypeTransform transform;
};

struct PA7SpecSeq
{
	bool is_typedef = false;
	bool storage_static = false;
	bool storage_thread_local = false;
	bool storage_extern = false;

	int kw_signed = 0;
	int kw_unsigned = 0;
	int kw_short = 0;
	int kw_long = 0;
	int kw_int = 0;
	int kw_char = 0;
	int kw_char16 = 0;
	int kw_char32 = 0;
	int kw_wchar = 0;
	int kw_bool = 0;
	int kw_float = 0;
	int kw_double = 0;
	int kw_void = 0;

	bool cv_const = false;
	bool cv_volatile = false;

	bool has_named_type = false;
	PA7TypePtr named_type;

	bool saw_type_specifier = false;
};

struct PA7Parser
{
	vector<PA7Token> toks;
	size_t pos = 0;
	PA7Namespace* global_ns = nullptr;
	PA7Namespace* cur_ns = nullptr;

	PA7Parser(const vector<PA7Token>& toks, PA7Namespace* global_ns)
		: toks(toks), global_ns(global_ns), cur_ns(global_ns)
	{
	}

	const PA7Token& Peek(size_t off = 0) const
	{
		size_t p = pos + off;
		if (p >= toks.size()) return toks.back();
		return toks[p];
	}

	bool PeekKind(const string& kind, size_t off = 0) const
	{
		return Peek(off).kind == kind;
	}

	bool Match(const string& kind)
	{
		if (!PeekKind(kind)) return false;
		pos++;
		return true;
	}

	const PA7Token& Expect(const string& kind)
	{
		if (!PeekKind(kind))
			throw runtime_error("parse error: expected " + kind);
		return toks[pos++];
	}

	template<typename Fn>
	bool Try(Fn fn)
	{
		size_t save = pos;
		try
		{
			fn();
			return true;
		}
		catch (...)
		{
			pos = save;
			return false;
		}
	}

	void ParseTranslationUnit()
	{
		while (!PeekKind("ST_EOF"))
		{
			ParseDeclaration();
		}
		Expect("ST_EOF");
	}

	void ParseDeclaration()
	{
		if (PeekKind("OP_SEMICOLON"))
		{
			Match("OP_SEMICOLON");
			return;
		}

		if (PeekKind("KW_INLINE") && PeekKind("KW_NAMESPACE", 1))
		{
			ParseNamespaceDefinition();
			return;
		}

		if (PeekKind("KW_NAMESPACE"))
		{
			if (PeekKind("TT_IDENTIFIER", 1) && PeekKind("OP_ASS", 2))
				ParseNamespaceAliasDefinition();
			else
				ParseNamespaceDefinition();
			return;
		}

		if (PeekKind("KW_USING"))
		{
			if (PeekKind("TT_IDENTIFIER", 1) && PeekKind("OP_ASS", 2))
			{
				ParseAliasDeclaration();
				return;
			}

			if (PeekKind("KW_NAMESPACE", 1))
			{
				ParseUsingDirective();
				return;
			}

			ParseUsingDeclaration();
			return;
		}

		ParseSimpleDeclaration();
	}

	void ParseNamespaceDefinition()
	{
		bool is_inline = Match("KW_INLINE");
		Expect("KW_NAMESPACE");

		bool named = false;
		string name;
		if (PeekKind("TT_IDENTIFIER"))
		{
			named = true;
			name = Expect("TT_IDENTIFIER").source;
		}

		Expect("OP_LBRACE");

		PA7Namespace* child = nullptr;
		if (named)
		{
			child = PA7GetOrCreateNamedNamespace(cur_ns, name);
		}
		else
		{
			child = PA7CreateUnnamedNamespace(cur_ns);
		}

		if (is_inline) child->is_inline = true;
		if (!child->named || child->is_inline)
			PA7AddUsingDirective(cur_ns, child);

		PA7Namespace* old = cur_ns;
		cur_ns = child;
		while (!PeekKind("OP_RBRACE"))
			ParseDeclaration();
		Expect("OP_RBRACE");
		cur_ns = old;
	}

	PA7NamePrefix ParseNestedNameSpecifier(bool required)
	{
		PA7NamePrefix pfx;
		size_t save = pos;

		if (Match("OP_COLON2"))
		{
			pfx.has_prefix = true;
			pfx.global = true;
		}
		else if (PeekKind("TT_IDENTIFIER") && PeekKind("OP_COLON2", 1))
		{
			pfx.has_prefix = true;
			pfx.global = false;
			pfx.components.push_back(Expect("TT_IDENTIFIER").source);
			Expect("OP_COLON2");
		}
		else
		{
			if (required) throw runtime_error("expected nested-name-specifier");
			return pfx;
		}

		while (PeekKind("TT_IDENTIFIER") && PeekKind("OP_COLON2", 1))
		{
			pfx.components.push_back(Expect("TT_IDENTIFIER").source);
			Expect("OP_COLON2");
		}

		if (!pfx.has_prefix)
			pos = save;
		return pfx;
	}

	PA7Namespace* ResolveNamespacePrefix(const PA7NamePrefix& pfx)
	{
		if (!pfx.has_prefix) return cur_ns;

		PA7Namespace* ns = nullptr;
		if (pfx.global)
		{
			ns = global_ns;
			for (size_t i = 0; i < pfx.components.size(); i++)
			{
				ns = PA7LookupNamespaceQualified(ns, pfx.components[i]);
				if (!ns) throw runtime_error("unknown namespace in qualified prefix");
			}
			return ns;
		}

		if (pfx.components.empty())
			throw runtime_error("bad non-global nested-name-specifier");

		ns = PA7LookupNamespaceUnqualified(cur_ns, pfx.components[0]);
		if (!ns) throw runtime_error("unknown namespace in prefix");
		for (size_t i = 1; i < pfx.components.size(); i++)
		{
			ns = PA7LookupNamespaceQualified(ns, pfx.components[i]);
			if (!ns) throw runtime_error("unknown namespace in suffix");
		}
		return ns;
	}

	PA7Namespace* ResolveQualifiedNamespaceSpecifier(const PA7NamePrefix& pfx, const string& last)
	{
		if (!pfx.has_prefix)
		{
			PA7Namespace* ns = PA7LookupNamespaceUnqualified(cur_ns, last);
			if (!ns) throw runtime_error("unknown namespace");
			return ns;
		}
		PA7Namespace* root = ResolveNamespacePrefix(pfx);
		PA7Namespace* ns = PA7LookupNamespaceQualified(root, last);
		if (!ns) throw runtime_error("unknown namespace");
		return ns;
	}

	void ParseNamespaceAliasDefinition()
	{
		Expect("KW_NAMESPACE");
		string alias = Expect("TT_IDENTIFIER").source;
		Expect("OP_ASS");

		PA7NamePrefix pfx = ParseNestedNameSpecifier(false);
		string name = Expect("TT_IDENTIFIER").source;
		PA7Namespace* target = ResolveQualifiedNamespaceSpecifier(pfx, name);

		cur_ns->namespace_aliases[alias] = target;
		Expect("OP_SEMICOLON");
	}

	void ParseUsingDirective()
	{
		Expect("KW_USING");
		Expect("KW_NAMESPACE");

		PA7NamePrefix pfx = ParseNestedNameSpecifier(false);
		string name = Expect("TT_IDENTIFIER").source;
		PA7Namespace* target = ResolveQualifiedNamespaceSpecifier(pfx, name);
		PA7AddUsingDirective(cur_ns, target);

		Expect("OP_SEMICOLON");
	}

	void ParseUsingDeclaration()
	{
		Expect("KW_USING");
		PA7NamePrefix pfx = ParseNestedNameSpecifier(true);
		string name = Expect("TT_IDENTIFIER").source;
		Expect("OP_SEMICOLON");

		PA7Namespace* root = ResolveNamespacePrefix(pfx);
		PA7TypePtr t = PA7LookupTypeQualified(root, name);
		if (t) cur_ns->type_aliases[name] = t;
	}

	PA7TypePtr BuildFundamentalType(const PA7SpecSeq& s)
	{
		int total_noncv = s.kw_signed + s.kw_unsigned + s.kw_short + s.kw_long + s.kw_int + s.kw_char +
			s.kw_char16 + s.kw_char32 + s.kw_wchar + s.kw_bool + s.kw_float + s.kw_double + s.kw_void;

		if (total_noncv == 0)
			throw runtime_error("missing type specifier");

		if (s.kw_char16) return PA7MakeFundamental("char16_t");
		if (s.kw_char32) return PA7MakeFundamental("char32_t");
		if (s.kw_wchar) return PA7MakeFundamental("wchar_t");
		if (s.kw_bool) return PA7MakeFundamental("bool");
		if (s.kw_float) return PA7MakeFundamental("float");
		if (s.kw_void) return PA7MakeFundamental("void");

		if (s.kw_char)
		{
			if (s.kw_unsigned) return PA7MakeFundamental("unsigned char");
			if (s.kw_signed) return PA7MakeFundamental("signed char");
			return PA7MakeFundamental("char");
		}

		if (s.kw_double)
		{
			if (s.kw_long) return PA7MakeFundamental("long double");
			return PA7MakeFundamental("double");
		}

		if (s.kw_short)
		{
			if (s.kw_unsigned) return PA7MakeFundamental("unsigned short int");
			return PA7MakeFundamental("short int");
		}

		if (s.kw_long >= 2)
		{
			if (s.kw_unsigned) return PA7MakeFundamental("unsigned long long int");
			return PA7MakeFundamental("long long int");
		}

		if (s.kw_long == 1)
		{
			if (s.kw_unsigned) return PA7MakeFundamental("unsigned long int");
			return PA7MakeFundamental("long int");
		}

		if (s.kw_unsigned) return PA7MakeFundamental("unsigned int");
		return PA7MakeFundamental("int");
	}

	bool ParseSimpleTypeSpecifier(PA7SpecSeq& out, bool allow_named_lookup)
	{
		if (Match("KW_CHAR")) { out.kw_char++; out.saw_type_specifier = true; return true; }
		if (Match("KW_CHAR16_T")) { out.kw_char16++; out.saw_type_specifier = true; return true; }
		if (Match("KW_CHAR32_T")) { out.kw_char32++; out.saw_type_specifier = true; return true; }
		if (Match("KW_WCHAR_T")) { out.kw_wchar++; out.saw_type_specifier = true; return true; }
		if (Match("KW_BOOL")) { out.kw_bool++; out.saw_type_specifier = true; return true; }
		if (Match("KW_SHORT")) { out.kw_short++; out.saw_type_specifier = true; return true; }
		if (Match("KW_INT")) { out.kw_int++; out.saw_type_specifier = true; return true; }
		if (Match("KW_LONG")) { out.kw_long++; out.saw_type_specifier = true; return true; }
		if (Match("KW_SIGNED")) { out.kw_signed++; out.saw_type_specifier = true; return true; }
		if (Match("KW_UNSIGNED")) { out.kw_unsigned++; out.saw_type_specifier = true; return true; }
		if (Match("KW_FLOAT")) { out.kw_float++; out.saw_type_specifier = true; return true; }
		if (Match("KW_DOUBLE")) { out.kw_double++; out.saw_type_specifier = true; return true; }
		if (Match("KW_VOID")) { out.kw_void++; out.saw_type_specifier = true; return true; }

		if (allow_named_lookup)
		{
			size_t save = pos;
			PA7NamePrefix pfx = ParseNestedNameSpecifier(false);
			if (PeekKind("TT_IDENTIFIER"))
			{
				string name = Expect("TT_IDENTIFIER").source;
				PA7TypePtr t;
				if (!pfx.has_prefix)
				{
					t = PA7LookupTypeUnqualified(cur_ns, name);
				}
				else
				{
					PA7Namespace* root = ResolveNamespacePrefix(pfx);
					t = PA7LookupTypeQualified(root, name);
				}
				if (t)
				{
					out.has_named_type = true;
					out.named_type = t;
					out.saw_type_specifier = true;
					return true;
				}
			}
			pos = save;
		}
		return false;
	}

	bool ParseTypeSpecifier(PA7SpecSeq& out, bool allow_named_lookup, bool& noncv_type)
	{
		noncv_type = false;
		if (Match("KW_CONST"))
		{
			out.cv_const = true;
			out.saw_type_specifier = true;
			return true;
		}
		if (Match("KW_VOLATILE"))
		{
			out.cv_volatile = true;
			out.saw_type_specifier = true;
			return true;
		}
		bool ok = ParseSimpleTypeSpecifier(out, allow_named_lookup);
		if (ok) noncv_type = true;
		return ok;
	}

	PA7TypePtr FinalizeSpecSeqType(const PA7SpecSeq& s)
	{
		PA7TypePtr base;
		if (s.has_named_type)
		{
			base = s.named_type;
		}
		else
		{
			base = BuildFundamentalType(s);
		}
		return PA7MakeCv(base, s.cv_const, s.cv_volatile);
	}

	PA7SpecSeq ParseDeclSpecifierSeq()
	{
		PA7SpecSeq s;
		bool any = false;
		bool seen_noncv_type = false;
		while (true)
		{
			if (Match("KW_STATIC"))
			{
				s.storage_static = true;
				any = true;
				continue;
			}
			if (Match("KW_THREAD_LOCAL"))
			{
				s.storage_thread_local = true;
				any = true;
				continue;
			}
			if (Match("KW_EXTERN"))
			{
				s.storage_extern = true;
				any = true;
				continue;
			}
			if (Match("KW_TYPEDEF"))
			{
				s.is_typedef = true;
				any = true;
				continue;
			}
			size_t save = pos;
			bool noncv = false;
			if (ParseTypeSpecifier(s, !seen_noncv_type, noncv))
			{
				any = true;
				if (noncv) seen_noncv_type = true;
				continue;
			}
			pos = save;
			break;
		}
		if (!any) throw runtime_error("expected decl-specifier-seq");
		if (!s.saw_type_specifier) throw runtime_error("missing type-specifier");
		return s;
	}

	PA7TypePtr ParseTypeSpecifierSeqOnly()
	{
		PA7SpecSeq s;
		bool any = false;
		bool seen_noncv_type = false;
		while (true)
		{
			size_t save = pos;
			bool noncv = false;
			if (ParseTypeSpecifier(s, !seen_noncv_type, noncv))
			{
				any = true;
				if (noncv) seen_noncv_type = true;
				continue;
			}
			pos = save;
			break;
		}
		if (!any) throw runtime_error("expected type-specifier-seq");
		return FinalizeSpecSeqType(s);
	}

	bool ParsePtrOperator(PA7PtrOp& out)
	{
		if (Match("OP_STAR"))
		{
			out.kind = PA7PtrOp::PO_POINTER;
			out.cv_const = false;
			out.cv_volatile = false;
			while (true)
			{
				if (Match("KW_CONST")) { out.cv_const = true; continue; }
				if (Match("KW_VOLATILE")) { out.cv_volatile = true; continue; }
				break;
			}
			return true;
		}
		if (Match("OP_AMP"))
		{
			out.kind = PA7PtrOp::PO_LREF;
			return true;
		}
		if (Match("OP_LAND"))
		{
			out.kind = PA7PtrOp::PO_RREF;
			return true;
		}
		return false;
	}

	PA7Name ParseIdExpression()
	{
		PA7Name n;
		size_t save = pos;
		if (Try([&]{
			PA7NamePrefix pfx = ParseNestedNameSpecifier(true);
			string id = Expect("TT_IDENTIFIER").source;
			n.qualified = true;
			n.prefix = pfx;
			n.name = id;
		}))
		{
			return n;
		}
		pos = save;

		n.qualified = false;
		n.name = Expect("TT_IDENTIFIER").source;
		return n;
	}

	PA7Suffix ParseArraySuffix()
	{
		PA7Suffix s;
		s.kind = PA7Suffix::SF_ARRAY;
		s.array_has_bound = false;
		s.array_bound = 0;
		Expect("OP_LSQUARE");
		if (PeekKind("TT_LITERAL"))
		{
			string src = Expect("TT_LITERAL").source;
			char* endp = nullptr;
			unsigned long long v = strtoull(src.c_str(), &endp, 0);
			if (endp != src.c_str() && *endp == '\0' && v > 0)
			{
				s.array_has_bound = true;
				s.array_bound = v;
			}
		}
		Expect("OP_RSQUARE");
		return s;
	}

	PA7TypePtr ParseParameterDeclaration()
	{
		PA7SpecSeq s = ParseDeclSpecifierSeq();
		PA7TypePtr base = FinalizeSpecSeqType(s);

		PA7Declarator d;
		d.has_name = false;
		d.transform = PA7IdentityTransform();

		size_t save = pos;
		if (!Try([&]{ d = ParseDeclaratorInternal(true, true); }))
			pos = save;

		PA7TypePtr t = d.transform(base);
		t = PA7AdjustFunctionParamType(t);
		return t;
	}

	void ParseParameterDeclarationClause(vector<PA7TypePtr>& params, bool& variadic)
	{
		params.clear();
		variadic = false;

		if (Match("OP_DOTS"))
		{
			variadic = true;
			return;
		}

		if (PeekKind("OP_RPAREN"))
			return;

		params.push_back(ParseParameterDeclaration());
		while (Match("OP_COMMA"))
		{
			if (Match("OP_DOTS"))
			{
				variadic = true;
				return;
			}
			params.push_back(ParseParameterDeclaration());
		}

		if (Match("OP_DOTS"))
			variadic = true;
	}

	PA7Suffix ParseFunctionSuffix()
	{
		PA7Suffix s;
		s.kind = PA7Suffix::SF_FUNCTION;
		s.variadic = false;
		Expect("OP_LPAREN");
		ParseParameterDeclarationClause(s.params, s.variadic);
		Expect("OP_RPAREN");

		if (s.params.size() == 1 && !s.variadic && PA7IsVoidType(s.params[0]))
			s.params.clear();

		return s;
	}

	bool ParseOneSuffix(PA7Suffix& out)
	{
		if (PeekKind("OP_LPAREN"))
		{
			out = ParseFunctionSuffix();
			return true;
		}
		if (PeekKind("OP_LSQUARE"))
		{
			out = ParseArraySuffix();
			return true;
		}
		return false;
	}

	PA7Declarator ParseDeclaratorInternal(bool allow_named, bool allow_abstract)
	{
		vector<PA7PtrOp> ptr_ops;
		while (true)
		{
			PA7PtrOp p;
			if (!ParsePtrOperator(p)) break;
			ptr_ops.push_back(p);
		}

		PA7Declarator core;
		core.has_name = false;
		core.transform = PA7IdentityTransform();
		bool has_core = false;

		if (PeekKind("OP_LPAREN"))
		{
			size_t save = pos;
			Match("OP_LPAREN");
			if (Try([&]{
				core = ParseDeclaratorInternal(allow_named, allow_abstract);
				Expect("OP_RPAREN");
			}))
			{
				has_core = true;
			}
			else
			{
				pos = save;
			}
		}

		if (!has_core && allow_named)
		{
			if (PeekKind("TT_IDENTIFIER") || PeekKind("OP_COLON2") || (PeekKind("TT_IDENTIFIER") && PeekKind("OP_COLON2", 1)))
			{
				if (Try([&]{
					core.name = ParseIdExpression();
					core.has_name = true;
					core.transform = PA7IdentityTransform();
					has_core = true;
				}))
				{
					// done
				}
			}
		}

		vector<PA7Suffix> suffixes;
		while (true)
		{
			PA7Suffix sfx;
			if (!ParseOneSuffix(sfx)) break;
			suffixes.push_back(sfx);
		}

		if (!has_core)
		{
			if (!allow_abstract)
				throw runtime_error("expected declarator-id");
			if (ptr_ops.empty() && suffixes.empty())
				throw runtime_error("expected abstract declarator");
			core.transform = PA7IdentityTransform();
		}

		PA7TypeTransform tr = core.transform;
		for (size_t i = 0; i < suffixes.size(); i++)
		{
			PA7Suffix sfx = suffixes[i];
			PA7TypeTransform prev = tr;
			tr = [prev, sfx](const PA7TypePtr& b) -> PA7TypePtr
			{
				return prev(PA7ApplySuffix(b, sfx));
			};
		}
		for (size_t i = 0; i < ptr_ops.size(); i++)
		{
			PA7PtrOp p = ptr_ops[i];
			PA7TypeTransform prev = tr;
			tr = [prev, p](const PA7TypePtr& b) -> PA7TypePtr
			{
				return prev(PA7ApplyPtrOp(b, p));
			};
		}

		PA7Declarator out;
		out.has_name = core.has_name;
		out.name = core.name;
		out.transform = tr;
		return out;
	}

	PA7Declarator ParseDeclarator()
	{
		PA7Declarator d = ParseDeclaratorInternal(true, false);
		if (!d.has_name) throw runtime_error("expected named declarator");
		return d;
	}

	PA7TypePtr ParseTypeId()
	{
		PA7TypePtr base = ParseTypeSpecifierSeqOnly();

		size_t save = pos;
		PA7Declarator d;
		d.has_name = false;
		d.transform = PA7IdentityTransform();
		if (!Try([&]{ d = ParseDeclaratorInternal(false, true); }))
			pos = save;

		return d.transform(base);
	}

	PA7Namespace* ResolveDeclaratorTargetNamespace(const PA7Name& n)
	{
		if (!n.qualified) return cur_ns;
		return ResolveNamespacePrefix(n.prefix);
	}

	void ParseAliasDeclaration()
	{
		Expect("KW_USING");
		string name = Expect("TT_IDENTIFIER").source;
		Expect("OP_ASS");
		PA7TypePtr t = ParseTypeId();
		Expect("OP_SEMICOLON");
		cur_ns->type_aliases[name] = t;
	}

	void ParseSimpleDeclaration()
	{
		PA7SpecSeq s = ParseDeclSpecifierSeq();
		PA7TypePtr base = FinalizeSpecSeqType(s);

		vector<PA7Declarator> decls;
		decls.push_back(ParseDeclarator());
		while (Match("OP_COMMA"))
			decls.push_back(ParseDeclarator());
		Expect("OP_SEMICOLON");

		for (size_t i = 0; i < decls.size(); i++)
		{
			PA7Declarator& d = decls[i];
			PA7Namespace* target = ResolveDeclaratorTargetNamespace(d.name);
			PA7TypePtr t = d.transform(base);

			if (s.is_typedef)
			{
				target->type_aliases[d.name.name] = t;
				continue;
			}

			if (t->kind == PA7Type::TK_FUNCTION)
				PA7DeclareFunction(target, d.name.name, t);
			else
				PA7DeclareVariable(target, d.name.name, t);
		}
	}
};

void PA7DumpNamespace(ostream& out, PA7Namespace* ns, bool is_global)
{
	if (is_global || !ns->named)
		out << "start unnamed namespace\n";
	else
		out << "start namespace " << ns->name << "\n";

	if (!is_global && ns->is_inline)
		out << "inline namespace\n";

	for (size_t i = 0; i < ns->vars_in_order.size(); i++)
	{
		out << "variable " << ns->vars_in_order[i].name << " " << PA7TypeToString(ns->vars_in_order[i].type) << "\n";
	}
	for (size_t i = 0; i < ns->funcs_in_order.size(); i++)
	{
		out << "function " << ns->funcs_in_order[i].name << " " << PA7TypeToString(ns->funcs_in_order[i].type) << "\n";
	}
	for (size_t i = 0; i < ns->namespaces_in_order.size(); i++)
	{
		PA7DumpNamespace(out, ns->namespaces_in_order[i], false);
	}

	out << "end namespace\n";
}

void PA7AnalyzeTranslationUnit(const string& srcfile, ostream& out, const string& date_lit, const string& time_lit)
{
	PA5Engine engine(date_lit, time_lit);
	vector<PPToken> preprocessed;
	engine.ProcessFile(srcfile, srcfile, preprocessed);

	vector<PA7Token> toks = PA7ToTokens(preprocessed);

	PA7Namespace global_ns;
	global_ns.parent = nullptr;
	global_ns.named = false;
	global_ns.name.clear();
	global_ns.is_inline = false;

	PA7Parser parser(toks, &global_ns);
	parser.ParseTranslationUnit();

	out << "start translation unit " << srcfile << "\n";
	PA7DumpNamespace(out, &global_ns, true);
	out << "end translation unit\n";
}

#ifndef CPPGM_NSDECL_NO_MAIN
int main(int argc, char** argv)
{
	try
	{
		vector<string> args;

		for (int i = 1; i < argc; i++)
			args.emplace_back(argv[i]);

		if (args.size() < 3 || args[0] != "-o")
			throw logic_error("invalid usage");

		string outfile = args[1];
		size_t nsrcfiles = args.size() - 2;

		time_t now = time(nullptr);
		string now_s = asctime(localtime(&now));
		string date_lit = BuildDateLiteralFromAsctime(now_s);
		string time_lit = BuildTimeLiteralFromAsctime(now_s);

		ofstream out(outfile.c_str(), ios::binary);
		out << nsrcfiles << " translation units\n";

		for (size_t i = 0; i < nsrcfiles; i++)
		{
			string srcfile = args[i + 2];
			PA7AnalyzeTranslationUnit(srcfile, out, date_lit, time_lit);
		}
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
