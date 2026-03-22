// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#define CPPGM_POSTTOKEN_INTERNAL_MAIN nsdecl_posttoken_internal_main
#define CPPGM_MACRO_MAIN_NAME nsdecl_macro_internal_main
#define CPPGM_PREPROC_MAIN_NAME nsdecl_preproc_internal_main
#include "preproc.cpp"
#undef CPPGM_PREPROC_MAIN_NAME
#undef CPPGM_MACRO_MAIN_NAME
#undef CPPGM_POSTTOKEN_INTERNAL_MAIN

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

struct Token
{
	string term;
	string spell;
	bool is_identifier;
	bool is_literal;
	bool is_eof;
};

vector<Token> LexForPA7(const vector<PPToken>& tokens)
{
	vector<Token> out;
	for (size_t i = 0; i < tokens.size(); ++i)
	{
		const PPToken& tok = tokens[i];
		if (tok.kind == PP_IDENTIFIER || tok.kind == PP_OP)
		{
			auto it = StringToTokenTypeMap.find(tok.data);
			if (it != StringToTokenTypeMap.end())
			{
				out.push_back({TokenTypeToStringMap.at(it->second), tok.data, tok.kind == PP_IDENTIFIER, false, false});
			}
			else if (tok.kind == PP_IDENTIFIER)
			{
				out.push_back({"TT_IDENTIFIER", tok.data, true, false, false});
			}
			else
			{
				throw runtime_error("bad token");
			}
			continue;
		}
		if (tok.kind == PP_NUMBER || tok.kind == PP_CHAR || tok.kind == PP_UD_CHAR || tok.kind == PP_STRING || tok.kind == PP_UD_STRING)
		{
			out.push_back({"TT_LITERAL", tok.data, false, true, false});
			continue;
		}
		throw runtime_error("bad token");
	}
	out.push_back({"ST_EOF", "", false, false, true});
	return out;
}

struct Type;
typedef shared_ptr<Type> TypePtr;

struct Type
{
	enum Kind
	{
		FUNDAMENTAL,
		CV,
		POINTER,
		LVALUE_REF,
		RVALUE_REF,
		ARRAY,
		FUNCTION
	} kind;

	string fundamental_name;
	bool is_const;
	bool is_volatile;
	TypePtr inner;
	long long array_bound;
	vector<TypePtr> params;
	bool variadic;

	Type(Kind k) : kind(k), is_const(false), is_volatile(false), array_bound(-1), variadic(false) {}
};

TypePtr MakeFundamental(const string& name)
{
	TypePtr out(new Type(Type::FUNDAMENTAL));
	out->fundamental_name = name;
	return out;
}

TypePtr MakePointer(const TypePtr& inner)
{
	TypePtr out(new Type(Type::POINTER));
	out->inner = inner;
	return out;
}

TypePtr MakeArray(const TypePtr& inner, long long bound)
{
	TypePtr out(new Type(Type::ARRAY));
	out->inner = inner;
	out->array_bound = bound;
	return out;
}

TypePtr MakeFunction(const vector<TypePtr>& params, bool variadic, const TypePtr& ret)
{
	TypePtr out(new Type(Type::FUNCTION));
	out->params = params;
	out->variadic = variadic;
	out->inner = ret;
	return out;
}

TypePtr AddCV(const TypePtr& type, bool add_const, bool add_volatile)
{
	if (!add_const && !add_volatile) return type;
	if (type->kind == Type::LVALUE_REF || type->kind == Type::RVALUE_REF) return type;
	if (type->kind == Type::ARRAY)
	{
		return MakeArray(AddCV(type->inner, add_const, add_volatile), type->array_bound);
	}
	if (type->kind == Type::CV)
	{
		TypePtr out(new Type(Type::CV));
		out->inner = type->inner;
		out->is_const = type->is_const || add_const;
		out->is_volatile = type->is_volatile || add_volatile;
		return out;
	}
	TypePtr out(new Type(Type::CV));
	out->inner = type;
	out->is_const = add_const;
	out->is_volatile = add_volatile;
	return out;
}

TypePtr MakeLValueRef(const TypePtr& type)
{
	if (type->kind == Type::LVALUE_REF || type->kind == Type::RVALUE_REF) return MakeLValueRef(type->inner);
	TypePtr out(new Type(Type::LVALUE_REF));
	out->inner = type;
	return out;
}

TypePtr MakeRValueRef(const TypePtr& type)
{
	if (type->kind == Type::LVALUE_REF) return MakeLValueRef(type->inner);
	if (type->kind == Type::RVALUE_REF) return MakeRValueRef(type->inner);
	TypePtr out(new Type(Type::RVALUE_REF));
	out->inner = type;
	return out;
}

string DescribeType(const TypePtr& type)
{
	switch (type->kind)
	{
	case Type::FUNDAMENTAL:
		return type->fundamental_name;
	case Type::CV:
	{
		string prefix;
		if (type->is_const) prefix += "const";
		if (type->is_const && type->is_volatile) prefix += " ";
		if (type->is_volatile) prefix += "volatile";
		return prefix + " " + DescribeType(type->inner);
	}
	case Type::POINTER:
		return "pointer to " + DescribeType(type->inner);
	case Type::LVALUE_REF:
		return "lvalue-reference to " + DescribeType(type->inner);
	case Type::RVALUE_REF:
		return "rvalue-reference to " + DescribeType(type->inner);
	case Type::ARRAY:
		if (type->array_bound < 0) return "array of unknown bound of " + DescribeType(type->inner);
		return "array of " + to_string(type->array_bound) + " " + DescribeType(type->inner);
	case Type::FUNCTION:
	{
		string out = "function of (";
		for (size_t i = 0; i < type->params.size(); ++i)
		{
			if (i != 0) out += ", ";
			out += DescribeType(type->params[i]);
		}
		if (type->variadic)
		{
			if (!type->params.empty()) out += ", ";
			out += "...";
		}
		out += ") returning ";
		out += DescribeType(type->inner);
		return out;
	}
	}
	throw runtime_error("bad type");
}

TypePtr StripTopLevelCV(const TypePtr& type)
{
	if (type->kind == Type::CV) return type->inner;
	return type;
}

TypePtr AdjustParameterType(const TypePtr& type)
{
	if (type->kind == Type::ARRAY) return MakePointer(type->inner);
	if (type->kind == Type::FUNCTION) return MakePointer(type);
	return StripTopLevelCV(type);
}

struct Namespace;

struct NamedType
{
	string name;
	TypePtr type;
};

struct Variable
{
	string name;
	TypePtr type;
};

struct Function
{
	string name;
	TypePtr type;
};

struct Namespace
{
	string name;
	bool unnamed;
	bool is_inline;
	Namespace* parent;
	vector< unique_ptr<Namespace> > storage;
	vector<Namespace*> ordered_children;
	vector<Variable> ordered_variables;
	vector<Function> ordered_functions;
	map<string, size_t> variable_index;
	map<string, size_t> function_index;
	map<string, TypePtr> typedefs;
	map<string, Namespace*> named_children;
	Namespace* unnamed_child;
	map<string, Namespace*> namespace_aliases;
	map<string, TypePtr> using_types;
	set<string> using_member_names;
	vector<Namespace*> using_directives;

	Namespace() : unnamed(false), is_inline(false), parent(NULL), unnamed_child(NULL) {}
};

Namespace* AddNamespaceChild(Namespace* parent, const string& name, bool unnamed, bool is_inline)
{
	if (unnamed)
	{
		if (parent->unnamed_child != NULL)
		{
			if (is_inline) parent->unnamed_child->is_inline = true;
			return parent->unnamed_child;
		}
	}
	else
	{
		map<string, Namespace*>::iterator it = parent->named_children.find(name);
		if (it != parent->named_children.end())
		{
			if (is_inline) it->second->is_inline = true;
			return it->second;
		}
	}

	parent->storage.emplace_back(new Namespace());
	Namespace* out = parent->storage.back().get();
	out->name = name;
	out->unnamed = unnamed;
	out->is_inline = is_inline;
	out->parent = parent;
	parent->ordered_children.push_back(out);

	if (unnamed)
	{
		parent->unnamed_child = out;
	}
	else
	{
		parent->named_children[name] = out;
	}
	return out;
}

void AddVariable(Namespace* ns, const string& name, const TypePtr& type)
{
	string desc = DescribeType(type);
	map<string, size_t>::iterator it = ns->variable_index.find(name);
	if (it != ns->variable_index.end())
	{
		if (DescribeType(ns->ordered_variables[it->second].type) == desc) return;
		ns->ordered_variables[it->second].type = type;
		return;
	}
	ns->variable_index[name] = ns->ordered_variables.size();
	ns->ordered_variables.push_back({name, type});
}

void AddFunction(Namespace* ns, const string& name, const TypePtr& type)
{
	string desc = DescribeType(type);
	map<string, size_t>::iterator it = ns->function_index.find(name);
	if (it != ns->function_index.end())
	{
		if (DescribeType(ns->ordered_functions[it->second].type) == desc) return;
		ns->ordered_functions[it->second].type = type;
		return;
	}
	ns->function_index[name] = ns->ordered_functions.size();
	ns->ordered_functions.push_back({name, type});
}

bool IsInlineVisibleChild(const Namespace* child)
{
	return child->unnamed || child->is_inline;
}

void CollectDirectTypes(Namespace* ns, const string& name, vector<TypePtr>& out)
{
	map<string, TypePtr>::const_iterator it = ns->typedefs.find(name);
	if (it != ns->typedefs.end()) out.push_back(it->second);
	it = ns->using_types.find(name);
	if (it != ns->using_types.end()) out.push_back(it->second);
	for (size_t i = 0; i < ns->ordered_children.size(); ++i)
	{
		Namespace* child = ns->ordered_children[i];
		if (!IsInlineVisibleChild(child)) continue;
		CollectDirectTypes(child, name, out);
	}
}

TypePtr LookupTypeQualified(Namespace* ns, const string& name, set<Namespace*>& visited);

TypePtr LookupTypeViaDirectives(Namespace* ns, const string& name, set<Namespace*>& visited)
{
	if (visited.count(ns) != 0) return TypePtr();
	visited.insert(ns);

	vector<TypePtr> direct;
	CollectDirectTypes(ns, name, direct);
	if (!direct.empty()) return direct[0];

	for (size_t i = 0; i < ns->using_directives.size(); ++i)
	{
		TypePtr found = LookupTypeQualified(ns->using_directives[i], name, visited);
		if (found) return found;
	}
	return TypePtr();
}

TypePtr LookupTypeQualified(Namespace* ns, const string& name, set<Namespace*>& visited)
{
	TypePtr found = LookupTypeViaDirectives(ns, name, visited);
	if (found) return found;
	return TypePtr();
}

TypePtr LookupTypeUnqualified(Namespace* ns, const string& name)
{
	for (Namespace* cur = ns; cur != NULL; cur = cur->parent)
	{
		set<Namespace*> visited;
		TypePtr found = LookupTypeViaDirectives(cur, name, visited);
		if (found) return found;
	}
	return TypePtr();
}

Namespace* LookupNamespaceDirect(Namespace* ns, const string& name)
{
	map<string, Namespace*>::const_iterator it = ns->named_children.find(name);
	if (it != ns->named_children.end()) return it->second;
	it = ns->namespace_aliases.find(name);
	if (it != ns->namespace_aliases.end()) return it->second;
	for (size_t i = 0; i < ns->ordered_children.size(); ++i)
	{
		Namespace* child = ns->ordered_children[i];
		if (!IsInlineVisibleChild(child)) continue;
		Namespace* found = LookupNamespaceDirect(child, name);
		if (found) return found;
	}
	return NULL;
}

Namespace* LookupNamespaceQualified(Namespace* ns, const string& name, set<Namespace*>& visited)
{
	if (visited.count(ns) != 0) return NULL;
	visited.insert(ns);

	Namespace* direct = LookupNamespaceDirect(ns, name);
	if (direct) return direct;

	for (size_t i = 0; i < ns->using_directives.size(); ++i)
	{
		Namespace* found = LookupNamespaceQualified(ns->using_directives[i], name, visited);
		if (found) return found;
	}
	return NULL;
}

Namespace* LookupNamespaceUnqualified(Namespace* ns, const string& name)
{
	for (Namespace* cur = ns; cur != NULL; cur = cur->parent)
	{
		set<Namespace*> visited;
		Namespace* found = LookupNamespaceQualified(cur, name, visited);
		if (found) return found;
	}
	return NULL;
}

struct DeclaratorOp
{
	enum Kind
	{
		POINTER,
		LVALUE_REF,
		RVALUE_REF,
		ARRAY,
		FUNCTION
	} kind;

	bool is_const;
	bool is_volatile;
	long long array_bound;
	vector<TypePtr> params;
	bool variadic;
};

struct NameRef
{
	bool global;
	vector<string> qualifiers;
	string name;

	NameRef() : global(false) {}
};

struct DeclaratorInfo
{
	bool has_name;
	NameRef name;
	vector<DeclaratorOp> ops;

	DeclaratorInfo() : has_name(false) {}
};

struct Parser
{
	const vector<Token>& tokens;
	size_t pos;
	Namespace root;

	Parser(const vector<Token>& t) : tokens(t), pos(0) {}

	const Token& Peek(size_t offset = 0) const
	{
		if (pos + offset >= tokens.size()) return tokens.back();
		return tokens[pos + offset];
	}

	bool AcceptTerm(const string& term)
	{
		if (Peek().term != term) return false;
		++pos;
		return true;
	}

	string ExpectIdentifier()
	{
		if (!Peek().is_identifier) throw runtime_error("expected identifier at token " + Peek().term + " " + Peek().spell);
		return tokens[pos++].spell;
	}

	void ExpectTerm(const string& term)
	{
		if (!AcceptTerm(term)) throw runtime_error("expected " + term);
	}

	bool IsDeclSpecifierStart() const
	{
		const string& term = Peek().term;
		if (term == "KW_TYPEDEF" || term == "KW_STATIC" || term == "KW_THREAD_LOCAL" || term == "KW_EXTERN") return true;
		if (term == "KW_CONST" || term == "KW_VOLATILE") return true;
		if (term == "KW_CHAR" || term == "KW_CHAR16_T" || term == "KW_CHAR32_T" || term == "KW_WCHAR_T" ||
			term == "KW_BOOL" || term == "KW_SHORT" || term == "KW_INT" || term == "KW_LONG" ||
			term == "KW_SIGNED" || term == "KW_UNSIGNED" || term == "KW_FLOAT" || term == "KW_DOUBLE" ||
			term == "KW_VOID") return true;
		if (term == "OP_COLON2" || Peek().is_identifier) return true;
		return false;
	}

	bool IsTypeNameSpecifierStart(Namespace* current)
	{
		size_t saved = pos;
		try
		{
			NameRef ref = ParseTypeNameReference();
			TypePtr type = ResolveTypeReference(current, ref);
			pos = saved;
			return !!type;
		}
		catch (...)
		{
			pos = saved;
			return false;
		}
	}

	NameRef ParseNestedNameSpecifier()
	{
		NameRef ref;
		if (AcceptTerm("OP_COLON2"))
		{
			ref.global = true;
			if (Peek().is_identifier && Peek(1).term == "OP_COLON2")
			{
				ref.qualifiers.push_back(ExpectIdentifier());
				ExpectTerm("OP_COLON2");
			}
		}
		else
		{
			ref.qualifiers.push_back(ExpectIdentifier());
			ExpectTerm("OP_COLON2");
		}
		while (Peek().is_identifier && Peek(1).term == "OP_COLON2")
		{
			ref.qualifiers.push_back(ExpectIdentifier());
			ExpectTerm("OP_COLON2");
		}
		return ref;
	}

	NameRef ParseIdExpression()
	{
		NameRef ref;
		if (Peek().term == "OP_COLON2")
		{
			ref.global = true;
			ExpectTerm("OP_COLON2");
			while (Peek().is_identifier && Peek(1).term == "OP_COLON2")
			{
				ref.qualifiers.push_back(ExpectIdentifier());
				ExpectTerm("OP_COLON2");
			}
			ref.name = ExpectIdentifier();
			return ref;
		}
		if (Peek().is_identifier && Peek(1).term == "OP_COLON2")
		{
			ref.qualifiers.push_back(ExpectIdentifier());
			ExpectTerm("OP_COLON2");
			while (Peek().is_identifier && Peek(1).term == "OP_COLON2")
			{
				ref.qualifiers.push_back(ExpectIdentifier());
				ExpectTerm("OP_COLON2");
			}
			ref.name = ExpectIdentifier();
			return ref;
		}
		ref.name = ExpectIdentifier();
		return ref;
	}

	NameRef ParseTypeNameReference()
	{
		NameRef ref;
		if (Peek().term == "OP_COLON2")
		{
			ref.global = true;
			ExpectTerm("OP_COLON2");
			while (Peek().is_identifier && Peek(1).term == "OP_COLON2")
			{
				ref.qualifiers.push_back(ExpectIdentifier());
				ExpectTerm("OP_COLON2");
			}
			ref.name = ExpectIdentifier();
			return ref;
		}
		if (Peek().is_identifier && Peek(1).term == "OP_COLON2")
		{
			while (Peek().is_identifier && Peek(1).term == "OP_COLON2")
			{
				ref.qualifiers.push_back(ExpectIdentifier());
				ExpectTerm("OP_COLON2");
			}
			ref.name = ExpectIdentifier();
			return ref;
		}
		ref.name = ExpectIdentifier();
		return ref;
	}

	Namespace* ResolveQualifier(Namespace* current, const NameRef& ref)
	{
		Namespace* ns = ref.global ? &root : current;
		if (!ref.global && !ref.qualifiers.empty())
		{
			ns = LookupNamespaceUnqualified(current, ref.qualifiers[0]);
			if (ns == NULL) throw runtime_error("unknown namespace " + ref.qualifiers[0]);
			for (size_t i = 1; i < ref.qualifiers.size(); ++i)
			{
				set<Namespace*> visited;
				ns = LookupNamespaceQualified(ns, ref.qualifiers[i], visited);
				if (ns == NULL) throw runtime_error("unknown namespace " + ref.qualifiers[i]);
			}
			return ns;
		}
		for (size_t i = 0; i < ref.qualifiers.size(); ++i)
		{
			set<Namespace*> visited;
			ns = LookupNamespaceQualified(ns, ref.qualifiers[i], visited);
			if (ns == NULL) throw runtime_error("unknown namespace " + ref.qualifiers[i]);
		}
		return ns;
	}

	TypePtr ResolveTypeReference(Namespace* current, const NameRef& ref)
	{
		if (ref.qualifiers.empty() && !ref.global)
		{
			return LookupTypeUnqualified(current, ref.name);
		}
		Namespace* ns = ResolveQualifier(current, ref);
		set<Namespace*> visited;
		return LookupTypeQualified(ns, ref.name, visited);
	}

	Namespace* ResolveNamespaceReference(Namespace* current, const NameRef& ref)
	{
		if (ref.qualifiers.empty() && !ref.global)
		{
			Namespace* ns = LookupNamespaceUnqualified(current, ref.name);
			if (ns) return ns;
			throw runtime_error("unknown namespace " + ref.name);
		}
		Namespace* ns = ResolveQualifier(current, ref);
		set<Namespace*> visited;
		Namespace* found = LookupNamespaceQualified(ns, ref.name, visited);
		if (found) return found;
		throw runtime_error("unknown namespace " + ref.name);
	}

	long long ParseArrayBound()
	{
		if (!Peek().is_literal) throw runtime_error("expected literal bound");
		string spell = tokens[pos++].spell;
		size_t end = 0;
		while (end < spell.size() && (isalnum(static_cast<unsigned char>(spell[end])) || spell[end] == 'x' || spell[end] == 'X')) ++end;
		string core = spell.substr(0, end);
		if (core.empty()) throw runtime_error("bad bound");
		long long value = stoll(core, NULL, 0);
		if (value <= 0) throw runtime_error("bad bound");
		return value;
	}

	struct DeclSpecInfo
	{
		TypePtr base;
		bool is_typedef;
	};

	TypePtr CanonicalizeFundamental(const vector<string>& terms)
	{
		int n_signed = 0;
		int n_unsigned = 0;
		int n_short = 0;
		int n_long = 0;
		int n_int = 0;
		int n_char = 0;
		int n_char16 = 0;
		int n_char32 = 0;
		int n_wchar = 0;
		int n_bool = 0;
		int n_float = 0;
		int n_double = 0;
		int n_void = 0;
		for (size_t i = 0; i < terms.size(); ++i)
		{
			const string& term = terms[i];
			if (term == "KW_SIGNED") ++n_signed;
			else if (term == "KW_UNSIGNED") ++n_unsigned;
			else if (term == "KW_SHORT") ++n_short;
			else if (term == "KW_LONG") ++n_long;
			else if (term == "KW_INT") ++n_int;
			else if (term == "KW_CHAR") ++n_char;
			else if (term == "KW_CHAR16_T") ++n_char16;
			else if (term == "KW_CHAR32_T") ++n_char32;
			else if (term == "KW_WCHAR_T") ++n_wchar;
			else if (term == "KW_BOOL") ++n_bool;
			else if (term == "KW_FLOAT") ++n_float;
			else if (term == "KW_DOUBLE") ++n_double;
			else if (term == "KW_VOID") ++n_void;
		}
		if (n_char16) return MakeFundamental("char16_t");
		if (n_char32) return MakeFundamental("char32_t");
		if (n_wchar) return MakeFundamental("wchar_t");
		if (n_bool) return MakeFundamental("bool");
		if (n_void) return MakeFundamental("void");
		if (n_float) return MakeFundamental("float");
		if (n_char)
		{
			if (n_unsigned) return MakeFundamental("unsigned char");
			if (n_signed) return MakeFundamental("signed char");
			return MakeFundamental("char");
		}
		if (n_double)
		{
			if (n_long) return MakeFundamental("long double");
			return MakeFundamental("double");
		}
		if (n_unsigned)
		{
			if (n_short) return MakeFundamental("unsigned short int");
			if (n_long >= 2) return MakeFundamental("unsigned long long int");
			if (n_long == 1) return MakeFundamental("unsigned long int");
			return MakeFundamental("unsigned int");
		}
		if (n_short) return MakeFundamental("short int");
		if (n_long >= 2) return MakeFundamental("long long int");
		if (n_long == 1) return MakeFundamental("long int");
		return MakeFundamental("int");
	}

	DeclSpecInfo ParseDeclSpecifierSeq(Namespace* current)
	{
		bool saw_any = false;
		bool is_typedef = false;
		bool add_const = false;
		bool add_volatile = false;
		vector<string> fundamental_terms;
		TypePtr resolved;

		while (true)
		{
			const string& term = Peek().term;
			if (term == "KW_TYPEDEF")
			{
				is_typedef = true;
				saw_any = true;
				++pos;
				continue;
			}
			if (term == "KW_STATIC" || term == "KW_THREAD_LOCAL" || term == "KW_EXTERN")
			{
				saw_any = true;
				++pos;
				continue;
			}
			if (term == "KW_CONST")
			{
				add_const = true;
				saw_any = true;
				++pos;
				continue;
			}
			if (term == "KW_VOLATILE")
			{
				add_volatile = true;
				saw_any = true;
				++pos;
				continue;
			}
			if (term == "KW_CHAR" || term == "KW_CHAR16_T" || term == "KW_CHAR32_T" || term == "KW_WCHAR_T" ||
				term == "KW_BOOL" || term == "KW_SHORT" || term == "KW_INT" || term == "KW_LONG" ||
				term == "KW_SIGNED" || term == "KW_UNSIGNED" || term == "KW_FLOAT" || term == "KW_DOUBLE" ||
				term == "KW_VOID")
			{
				fundamental_terms.push_back(term);
				saw_any = true;
				++pos;
				continue;
			}
			if (resolved == NULL && fundamental_terms.empty() &&
				(term == "OP_COLON2" || Peek().is_identifier) && IsTypeNameSpecifierStart(current))
			{
				NameRef ref = ParseTypeNameReference();
				resolved = ResolveTypeReference(current, ref);
				if (!resolved) throw runtime_error("unknown type " + ref.name);
				saw_any = true;
				continue;
			}
			break;
		}

		if (!saw_any) throw runtime_error("expected decl-specifier-seq");
		if (resolved && !fundamental_terms.empty()) throw runtime_error("bad type specifier");
		TypePtr base = resolved ? resolved : CanonicalizeFundamental(fundamental_terms);
		base = AddCV(base, add_const, add_volatile);
		return {base, is_typedef};
	}

	DeclaratorOp ParsePtrOperator()
	{
		if (AcceptTerm("OP_STAR"))
		{
			bool add_const = false;
			bool add_volatile = false;
			while (Peek().term == "KW_CONST" || Peek().term == "KW_VOLATILE")
			{
				if (AcceptTerm("KW_CONST")) add_const = true;
				else if (AcceptTerm("KW_VOLATILE")) add_volatile = true;
			}
			DeclaratorOp op;
			op.kind = DeclaratorOp::POINTER;
			op.is_const = add_const;
			op.is_volatile = add_volatile;
			op.array_bound = -1;
			op.variadic = false;
			return op;
		}
		if (AcceptTerm("OP_AMP"))
		{
			DeclaratorOp op;
			op.kind = DeclaratorOp::LVALUE_REF;
			op.is_const = false;
			op.is_volatile = false;
			op.array_bound = -1;
			op.variadic = false;
			return op;
		}
		ExpectTerm("OP_LAND");
		DeclaratorOp op;
		op.kind = DeclaratorOp::RVALUE_REF;
		op.is_const = false;
		op.is_volatile = false;
		op.array_bound = -1;
		op.variadic = false;
		return op;
	}

	vector<TypePtr> ParseParameterDeclarationList(Namespace* current)
	{
		vector<TypePtr> params;
		params.push_back(ParseParameterDeclaration(current));
		while (AcceptTerm("OP_COMMA"))
		{
			if (Peek().term == "OP_DOTS")
			{
				--pos;
				break;
			}
			params.push_back(ParseParameterDeclaration(current));
		}
		return params;
	}

	TypePtr ApplyDeclaratorOps(const TypePtr& base, const vector<DeclaratorOp>& ops)
	{
		TypePtr out = base;
		for (size_t i = ops.size(); i > 0; --i)
		{
			const DeclaratorOp& op = ops[i - 1];
			if (op.kind == DeclaratorOp::POINTER)
			{
				out = MakePointer(AddCV(out, op.is_const, op.is_volatile));
			}
			else if (op.kind == DeclaratorOp::LVALUE_REF)
			{
				out = MakeLValueRef(out);
			}
			else if (op.kind == DeclaratorOp::RVALUE_REF)
			{
				out = MakeRValueRef(out);
			}
			else if (op.kind == DeclaratorOp::ARRAY)
			{
				out = MakeArray(out, op.array_bound);
			}
			else if (op.kind == DeclaratorOp::FUNCTION)
			{
				out = MakeFunction(op.params, op.variadic, out);
			}
		}
		return out;
	}

	TypePtr ParseParameterDeclaration(Namespace* current)
	{
		DeclSpecInfo spec = ParseDeclSpecifierSeq(current);
		DeclaratorInfo info;
		if (Peek().term != "OP_COMMA" && Peek().term != "OP_RPAREN" && Peek().term != "OP_DOTS")
		{
			info = ParseMaybeAbstractDeclarator(current);
		}
		TypePtr type = ApplyDeclaratorOps(spec.base, info.ops);
		return AdjustParameterType(type);
	}

	DeclaratorOp ParseFunctionSuffix(Namespace* current)
	{
		ExpectTerm("OP_LPAREN");
		vector<TypePtr> params;
		bool variadic = false;
		if (Peek().term != "OP_RPAREN")
		{
			if (Peek().term == "OP_DOTS")
			{
				variadic = true;
				++pos;
			}
			else
			{
				params = ParseParameterDeclarationList(current);
				if (AcceptTerm("OP_COMMA")) ExpectTerm("OP_DOTS"), variadic = true;
				else if (AcceptTerm("OP_DOTS")) variadic = true;
			}
		}
		ExpectTerm("OP_RPAREN");

		if (!variadic && params.size() == 1 && DescribeType(params[0]) == "void")
		{
			params.clear();
		}

		DeclaratorOp op;
		op.kind = DeclaratorOp::FUNCTION;
		op.params = params;
		op.variadic = variadic;
		op.is_const = false;
		op.is_volatile = false;
		op.array_bound = -1;
		return op;
	}

	vector<DeclaratorOp> ParseSuffixes(Namespace* current)
	{
		vector<DeclaratorOp> ops;
		while (true)
		{
			if (Peek().term == "OP_LPAREN")
			{
				ops.push_back(ParseFunctionSuffix(current));
				continue;
			}
			if (AcceptTerm("OP_LSQUARE"))
			{
				long long bound = -1;
				if (Peek().term != "OP_RSQUARE") bound = ParseArrayBound();
				ExpectTerm("OP_RSQUARE");
				DeclaratorOp op;
				op.kind = DeclaratorOp::ARRAY;
				op.array_bound = bound;
				op.is_const = false;
				op.is_volatile = false;
				op.variadic = false;
				ops.push_back(op);
				continue;
			}
			break;
		}
		return ops;
	}

	DeclaratorInfo ParseDeclarator(Namespace* current)
	{
		vector<DeclaratorOp> prefix;
		while (Peek().term == "OP_STAR" || Peek().term == "OP_AMP" || Peek().term == "OP_LAND")
		{
			prefix.push_back(ParsePtrOperator());
		}

		DeclaratorInfo info;
		if (AcceptTerm("OP_LPAREN"))
		{
			info = ParseDeclarator(current);
			ExpectTerm("OP_RPAREN");
		}
		else
		{
			if (Peek().is_identifier || Peek().term == "OP_COLON2")
			{
				info.has_name = true;
				info.name = ParseIdExpression();
			}
		}

		vector<DeclaratorOp> suffix = ParseSuffixes(current);
		info.ops.insert(info.ops.end(), suffix.begin(), suffix.end());
		for (size_t i = prefix.size(); i > 0; --i) info.ops.push_back(prefix[i - 1]);
		return info;
	}

	DeclaratorInfo ParseMaybeAbstractDeclarator(Namespace* current)
	{
		if (Peek().term != "OP_STAR" && Peek().term != "OP_AMP" && Peek().term != "OP_LAND" &&
			Peek().term != "OP_LPAREN" && !Peek().is_identifier && Peek().term != "OP_COLON2")
		{
			return DeclaratorInfo();
		}
		if (UpcomingDeclaratorHasIdentifier()) return ParseDeclarator(current);
		return ParseAbstractDeclarator(current);
	}

	bool UpcomingDeclaratorHasIdentifier() const
	{
		int paren = 0;
		int square = 0;
		for (size_t i = pos; i < tokens.size(); ++i)
		{
			const Token& tok = tokens[i];
			if (tok.term == "OP_LPAREN") ++paren;
			else if (tok.term == "OP_RPAREN")
			{
				if (paren == 0 && square == 0) break;
				--paren;
			}
			else if (tok.term == "OP_LSQUARE") ++square;
			else if (tok.term == "OP_RSQUARE" && square > 0) --square;
			else if (paren == 0 && square == 0 &&
				(tok.term == "OP_COMMA" || tok.term == "OP_SEMICOLON" || tok.term == "OP_DOTS"))
			{
				break;
			}
			if (tok.is_identifier) return true;
		}
		return false;
	}

	DeclaratorInfo ParseAbstractDeclarator(Namespace* current)
	{
		vector<DeclaratorOp> prefix;
		while (Peek().term == "OP_STAR" || Peek().term == "OP_AMP" || Peek().term == "OP_LAND")
		{
			prefix.push_back(ParsePtrOperator());
		}

		DeclaratorInfo info;
		if (Peek().term == "OP_LPAREN" &&
			(Peek(1).term == "OP_STAR" || Peek(1).term == "OP_AMP" ||
			 Peek(1).term == "OP_LAND" || Peek(1).term == "OP_LPAREN"))
		{
			ExpectTerm("OP_LPAREN");
			info = ParseAbstractDeclarator(current);
			ExpectTerm("OP_RPAREN");
		}

		vector<DeclaratorOp> suffix = ParseSuffixes(current);
		if (!info.has_name && info.ops.empty() && suffix.empty() && prefix.empty()) throw runtime_error("not abstract");
		info.ops.insert(info.ops.end(), suffix.begin(), suffix.end());
		for (size_t i = prefix.size(); i > 0; --i) info.ops.push_back(prefix[i - 1]);
		return info;
	}

	TypePtr ParseTypeId(Namespace* current)
	{
		DeclSpecInfo spec = ParseDeclSpecifierSeq(current);
		DeclaratorInfo info;
		if (Peek().term != "OP_SEMICOLON" && Peek().term != "OP_COMMA" && Peek().term != "OP_RPAREN")
		{
			info = ParseMaybeAbstractDeclarator(current);
		}
		return ApplyDeclaratorOps(spec.base, info.ops);
	}

	Namespace* TargetNamespaceForName(Namespace* current, const NameRef& ref)
	{
		if (ref.qualifiers.empty() && !ref.global) return current;
		Namespace* ns = ResolveQualifier(current, ref);
		return ns;
	}

	void ParseSimpleDeclaration(Namespace* current)
	{
		DeclSpecInfo spec = ParseDeclSpecifierSeq(current);
		while (true)
		{
			DeclaratorInfo decl = ParseDeclarator(current);
			if (!decl.has_name) throw runtime_error("missing declarator name");
			TypePtr type = ApplyDeclaratorOps(spec.base, decl.ops);
			Namespace* target = TargetNamespaceForName(current, decl.name);
			if (spec.is_typedef)
			{
				target->typedefs[decl.name.name] = type;
			}
			else if (type->kind == Type::FUNCTION)
			{
				AddFunction(target, decl.name.name, type);
			}
			else
			{
				AddVariable(target, decl.name.name, type);
			}

			if (!AcceptTerm("OP_COMMA")) break;
		}
		ExpectTerm("OP_SEMICOLON");
	}

	void ParseNamespaceDefinition(Namespace* current)
	{
		bool is_inline = AcceptTerm("KW_INLINE");
		ExpectTerm("KW_NAMESPACE");
		bool unnamed = true;
		string name;
		if (Peek().is_identifier)
		{
			unnamed = false;
			name = ExpectIdentifier();
		}
		ExpectTerm("OP_LBRACE");
		Namespace* child = AddNamespaceChild(current, name, unnamed, is_inline);
		while (Peek().term != "OP_RBRACE")
		{
			ParseDeclaration(child);
		}
		ExpectTerm("OP_RBRACE");
	}

	void ParseNamespaceAliasDefinition(Namespace* current)
	{
		ExpectTerm("KW_NAMESPACE");
		string alias = ExpectIdentifier();
		ExpectTerm("OP_ASS");
		NameRef ref = ParseTypeNameReference();
		Namespace* target = ResolveNamespaceReference(current, ref);
		current->namespace_aliases[alias] = target;
		ExpectTerm("OP_SEMICOLON");
	}

	void ParseUsingDirective(Namespace* current)
	{
		ExpectTerm("KW_USING");
		ExpectTerm("KW_NAMESPACE");
		NameRef ref = ParseTypeNameReference();
		Namespace* target = ResolveNamespaceReference(current, ref);
		current->using_directives.push_back(target);
		ExpectTerm("OP_SEMICOLON");
	}

	void ParseUsingDeclaration(Namespace* current)
	{
		ExpectTerm("KW_USING");
		NameRef prefix = ParseNestedNameSpecifier();
		string name = ExpectIdentifier();
		prefix.name = name;
		TypePtr type = ResolveTypeReference(current, prefix);
		if (type)
		{
			current->using_types[name] = type;
			ExpectTerm("OP_SEMICOLON");
			return;
		}
		Namespace* ns = ResolveQualifier(current, prefix);
		map<string, size_t>::const_iterator vit = ns->variable_index.find(name);
		if (vit != ns->variable_index.end())
		{
			ExpectTerm("OP_SEMICOLON");
			return;
		}
		map<string, size_t>::const_iterator fit = ns->function_index.find(name);
		if (fit != ns->function_index.end())
		{
			ExpectTerm("OP_SEMICOLON");
			return;
		}
		throw runtime_error("bad using declaration");
	}

	void ParseAliasDeclaration(Namespace* current)
	{
		ExpectTerm("KW_USING");
		string alias = ExpectIdentifier();
		ExpectTerm("OP_ASS");
		TypePtr type = ParseTypeId(current);
		current->typedefs[alias] = type;
		ExpectTerm("OP_SEMICOLON");
	}

	void ParseDeclaration(Namespace* current)
	{
		if (AcceptTerm("OP_SEMICOLON")) return;
		if (Peek().term == "KW_INLINE" && Peek(1).term == "KW_NAMESPACE")
		{
			ParseNamespaceDefinition(current);
			if (Peek().term == "OP_SEMICOLON") ++pos;
			return;
		}
		if (Peek().term == "KW_NAMESPACE")
		{
			size_t saved = pos;
			++pos;
			if (Peek().is_identifier && Peek(1).term == "OP_ASS")
			{
				pos = saved;
				ParseNamespaceAliasDefinition(current);
				return;
			}
			pos = saved;
			ParseNamespaceDefinition(current);
			if (Peek().term == "OP_SEMICOLON") ++pos;
			return;
		}
		if (Peek().term == "KW_USING")
		{
			if (Peek(1).term == "KW_NAMESPACE")
			{
				ParseUsingDirective(current);
				return;
			}
			if (Peek(1).is_identifier && Peek(2).term == "OP_ASS")
			{
				ParseAliasDeclaration(current);
				return;
			}
			ParseUsingDeclaration(current);
			return;
		}
		ParseSimpleDeclaration(current);
	}

	void ParseTranslationUnit()
	{
		root.unnamed = true;
		root.parent = NULL;
		while (Peek().term != "ST_EOF")
		{
			ParseDeclaration(&root);
		}
		ExpectTerm("ST_EOF");
	}
};

void PrintNamespace(ofstream& out, const Namespace* ns)
{
	if (ns->unnamed) out << "start unnamed namespace" << endl;
	else out << "start namespace " << ns->name << endl;
	if (ns->is_inline) out << "inline namespace" << endl;

	for (size_t i = 0; i < ns->ordered_variables.size(); ++i)
	{
		out << "variable " << ns->ordered_variables[i].name << " " << DescribeType(ns->ordered_variables[i].type) << endl;
	}
	for (size_t i = 0; i < ns->ordered_functions.size(); ++i)
	{
		out << "function " << ns->ordered_functions[i].name << " " << DescribeType(ns->ordered_functions[i].type) << endl;
	}
	for (size_t i = 0; i < ns->ordered_children.size(); ++i)
	{
		PrintNamespace(out, ns->ordered_children[i]);
	}
	out << "end namespace" << endl;
}

void ProcessTranslationUnit(ofstream& out, const string& srcfile)
{
	vector<PPToken> preprocessed = PreprocessSourceTokens(srcfile);
	vector<Token> tokens = LexForPA7(preprocessed);
	Parser parser(tokens);
	parser.ParseTranslationUnit();

	out << "start translation unit " << srcfile << endl;
	PrintNamespace(out, &parser.root);
	out << "end translation unit" << endl;
}

#ifndef CPPGM_NSDECL_MAIN_NAME
#define CPPGM_NSDECL_MAIN_NAME main
#endif

int CPPGM_NSDECL_MAIN_NAME(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
		if (args.size() < 3 || args[0] != "-o") throw logic_error("invalid usage");

		string outfile = args[1];
		size_t nsrcfiles = args.size() - 2;
		ofstream out(outfile);
		out << nsrcfiles << " translation units" << endl;
		for (size_t i = 0; i < nsrcfiles; ++i)
		{
			ProcessTranslationUnit(out, args[i + 2]);
		}
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
