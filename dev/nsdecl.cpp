// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

#define CPPGM_EMBED_RECOG 1
#include "recog.cpp"

struct Type;
struct NamespaceDecl;

typedef shared_ptr<Type> TypePtr;
typedef shared_ptr<NamespaceDecl> NamespacePtr;

TypePtr StripTopLevelCV(TypePtr type);
struct PA7Parser;
typedef bool (*PA7ArrayBoundHook)(PA7Parser*, size_t&);
PA7ArrayBoundHook CPPGM_PA7_ARRAY_BOUND_HOOK = nullptr;

struct Type
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

	Kind kind = TK_FUNDAMENTAL;
	EFundamentalType fundamental = FT_INT;
	bool is_const = false;
	bool is_volatile = false;
	TypePtr child;
	size_t array_bound = 0;
	bool array_unknown = false;
	vector<TypePtr> params;
	bool varargs = false;
};

TypePtr MakeFundamentalType(EFundamentalType type)
{
	TypePtr out(new Type);
	out->kind = Type::TK_FUNDAMENTAL;
	out->fundamental = type;
	return out;
}

TypePtr MakeCVType(TypePtr child, bool is_const, bool is_volatile)
{
	if (!is_const && !is_volatile)
	{
		return child;
	}

	TypePtr out(new Type);
	out->kind = Type::TK_CV;
	out->child = child;
	out->is_const = is_const;
	out->is_volatile = is_volatile;
	return out;
}

TypePtr MakePointerType(TypePtr child)
{
	TypePtr base = StripTopLevelCV(child);
	if (base->kind == Type::TK_LVALUE_REF || base->kind == Type::TK_RVALUE_REF)
	{
		throw runtime_error("pointer to that type not allowed");
	}

	TypePtr out(new Type);
	out->kind = Type::TK_POINTER;
	out->child = child;
	return out;
}

TypePtr MakeLValueReferenceType(TypePtr child)
{
	TypePtr base = StripTopLevelCV(child);
	if (base->kind == Type::TK_LVALUE_REF || base->kind == Type::TK_RVALUE_REF)
	{
		return MakeLValueReferenceType(base->child);
	}
	if (base->kind == Type::TK_FUNDAMENTAL && base->fundamental == FT_VOID)
	{
		throw runtime_error("invalid type for reference to");
	}

	TypePtr out(new Type);
	out->kind = Type::TK_LVALUE_REF;
	out->child = child;
	return out;
}

TypePtr MakeRValueReferenceType(TypePtr child)
{
	TypePtr base = StripTopLevelCV(child);
	if (base->kind == Type::TK_LVALUE_REF)
	{
		return MakeLValueReferenceType(base->child);
	}
	if (base->kind == Type::TK_RVALUE_REF)
	{
		return MakeRValueReferenceType(base->child);
	}
	if (base->kind == Type::TK_FUNDAMENTAL && base->fundamental == FT_VOID)
	{
		throw runtime_error("invalid type for reference to");
	}

	TypePtr out(new Type);
	out->kind = Type::TK_RVALUE_REF;
	out->child = child;
	return out;
}

TypePtr MakeArrayType(TypePtr child, bool unknown, size_t bound)
{
	TypePtr out(new Type);
	out->kind = Type::TK_ARRAY;
	out->child = child;
	out->array_unknown = unknown;
	out->array_bound = bound;
	return out;
}

TypePtr MakeFunctionType(TypePtr ret, const vector<TypePtr>& params, bool varargs)
{
	TypePtr out(new Type);
	out->kind = Type::TK_FUNCTION;
	out->child = ret;
	out->params = params;
	out->varargs = varargs;
	return out;
}

TypePtr StripTopLevelCV(TypePtr type)
{
	if (type->kind == Type::TK_CV)
	{
		return type->child;
	}
	return type;
}

TypePtr ApplyCV(TypePtr type, bool is_const, bool is_volatile)
{
	if (!is_const && !is_volatile)
	{
		return type;
	}

	if (type->kind == Type::TK_LVALUE_REF || type->kind == Type::TK_RVALUE_REF)
	{
		return type;
	}

	if (type->kind == Type::TK_ARRAY)
	{
		return MakeArrayType(ApplyCV(type->child, is_const, is_volatile), type->array_unknown, type->array_bound);
	}

	if (type->kind == Type::TK_CV)
	{
		return MakeCVType(type->child, type->is_const || is_const, type->is_volatile || is_volatile);
	}

	return MakeCVType(type, is_const, is_volatile);
}

bool TypeEquals(TypePtr lhs, TypePtr rhs)
{
	if (lhs->kind != rhs->kind)
	{
		return false;
	}

	switch (lhs->kind)
	{
	case Type::TK_FUNDAMENTAL:
		return lhs->fundamental == rhs->fundamental;
	case Type::TK_CV:
		return lhs->is_const == rhs->is_const &&
			lhs->is_volatile == rhs->is_volatile &&
			TypeEquals(lhs->child, rhs->child);
	case Type::TK_POINTER:
	case Type::TK_LVALUE_REF:
	case Type::TK_RVALUE_REF:
		return TypeEquals(lhs->child, rhs->child);
	case Type::TK_ARRAY:
		return lhs->array_unknown == rhs->array_unknown &&
			lhs->array_bound == rhs->array_bound &&
			TypeEquals(lhs->child, rhs->child);
	case Type::TK_FUNCTION:
		if (!TypeEquals(lhs->child, rhs->child) ||
			lhs->varargs != rhs->varargs ||
			lhs->params.size() != rhs->params.size())
		{
			return false;
		}
		for (size_t i = 0; i < lhs->params.size(); ++i)
		{
			if (!TypeEquals(lhs->params[i], rhs->params[i]))
			{
				return false;
			}
		}
		return true;
	}

	return false;
}

TypePtr AdjustParameterType(TypePtr type)
{
	type = StripTopLevelCV(type);
	if (type->kind == Type::TK_ARRAY)
	{
		return MakePointerType(type->child);
	}
	if (type->kind == Type::TK_FUNCTION)
	{
		return MakePointerType(type);
	}
	return type;
}

bool MergeVariableType(TypePtr& slot, TypePtr incoming)
{
	if (TypeEquals(slot, incoming))
	{
		return true;
	}

	if (slot->kind == Type::TK_ARRAY &&
		incoming->kind == Type::TK_ARRAY &&
		TypeEquals(slot->child, incoming->child))
	{
		if (slot->array_unknown && !incoming->array_unknown)
		{
			slot = incoming;
			return true;
		}
		if (!slot->array_unknown && incoming->array_unknown)
		{
			return true;
		}
	}

	return false;
}

string DescribeType(TypePtr type)
{
	switch (type->kind)
	{
	case Type::TK_FUNDAMENTAL:
		return FundamentalTypeToStringMap.at(type->fundamental);
	case Type::TK_CV:
		if (type->is_const && type->is_volatile)
		{
			return "const volatile " + DescribeType(type->child);
		}
		if (type->is_const)
		{
			return "const " + DescribeType(type->child);
		}
		return "volatile " + DescribeType(type->child);
	case Type::TK_POINTER:
		return "pointer to " + DescribeType(type->child);
	case Type::TK_LVALUE_REF:
		return "lvalue-reference to " + DescribeType(type->child);
	case Type::TK_RVALUE_REF:
		return "rvalue-reference to " + DescribeType(type->child);
	case Type::TK_ARRAY:
		if (type->array_unknown)
		{
			return "array of unknown bound of " + DescribeType(type->child);
		}
		return "array of " + to_string(type->array_bound) + " " + DescribeType(type->child);
	case Type::TK_FUNCTION:
	{
		string params = "(";
		for (size_t i = 0; i < type->params.size(); ++i)
		{
			if (i)
			{
				params += ", ";
			}
			params += DescribeType(type->params[i]);
		}
		if (type->varargs)
		{
			if (!type->params.empty())
			{
				params += ", ";
			}
			params += "...";
		}
		params += ")";
		return "function of " + params + " returning " + DescribeType(type->child);
	}
	}

	return "";
}

struct VariableDecl
{
	string name;
	TypePtr type;
};

struct FunctionDecl
{
	string name;
	TypePtr type;
};

struct NamespaceDecl
{
	NamespaceDecl* parent = nullptr;
	string name;
	bool unnamed = false;
	bool inline_namespace = false;

	vector<shared_ptr<VariableDecl>> variables_in_order;
	vector<shared_ptr<FunctionDecl>> functions_in_order;
	vector<NamespacePtr> namespaces_in_order;

	unordered_map<string, shared_ptr<VariableDecl>> variables;
	unordered_map<string, shared_ptr<FunctionDecl>> functions;
	unordered_map<string, TypePtr> types;
	unordered_map<string, NamespacePtr> named_namespaces;
	unordered_map<string, NamespaceDecl*> namespace_aliases;
	unordered_set<string> using_value_names;
	vector<NamespaceDecl*> using_directives;
	NamespacePtr unnamed_namespace;
};

NamespacePtr CreateNamespace(NamespaceDecl* parent, const string& name, bool unnamed, bool inline_namespace)
{
	NamespacePtr out(new NamespaceDecl);
	out->parent = parent;
	out->name = name;
	out->unnamed = unnamed;
	out->inline_namespace = inline_namespace;
	return out;
}

void AddUsingDirective(NamespaceDecl* owner, NamespaceDecl* target)
{
	for (NamespaceDecl* existing : owner->using_directives)
	{
		if (existing == target)
		{
			return;
		}
	}
	owner->using_directives.push_back(target);
}

NamespaceDecl* GetOrCreateNamedNamespace(NamespaceDecl* owner, const string& name, bool inline_namespace)
{
	if (owner->types.find(name) != owner->types.end() ||
		owner->variables.find(name) != owner->variables.end() ||
		owner->functions.find(name) != owner->functions.end() ||
		owner->using_value_names.find(name) != owner->using_value_names.end() ||
		owner->namespace_aliases.find(name) != owner->namespace_aliases.end())
	{
		throw runtime_error("namespace conflicts with existing declaration");
	}

	auto it = owner->named_namespaces.find(name);
	if (it != owner->named_namespaces.end())
	{
		if (inline_namespace)
		{
			it->second->inline_namespace = true;
		}
		return it->second.get();
	}

	NamespacePtr created = CreateNamespace(owner, name, false, inline_namespace);
	owner->named_namespaces[name] = created;
	owner->namespaces_in_order.push_back(created);
	if (inline_namespace)
	{
		AddUsingDirective(owner, created.get());
	}
	return created.get();
}

NamespaceDecl* CreateUnnamedNamespace(NamespaceDecl* owner, bool inline_namespace)
{
	if (owner->unnamed_namespace)
	{
		if (inline_namespace)
		{
			owner->unnamed_namespace->inline_namespace = true;
		}
		return owner->unnamed_namespace.get();
	}

	NamespacePtr created = CreateNamespace(owner, "", true, inline_namespace);
	owner->unnamed_namespace = created;
	owner->namespaces_in_order.push_back(created);
	AddUsingDirective(owner, created.get());
	return created.get();
}

void DeclareTypeAlias(NamespaceDecl* owner, const string& name, TypePtr type)
{
	auto it = owner->types.find(name);
	if (it == owner->types.end())
	{
		owner->types[name] = type;
		return;
	}
	if (!TypeEquals(it->second, type))
	{
		throw runtime_error("conflicting type alias");
	}
}

void DeclareVariable(NamespaceDecl* owner, const string& name, TypePtr type)
{
	auto it = owner->variables.find(name);
	if (it == owner->variables.end())
	{
		shared_ptr<VariableDecl> decl(new VariableDecl);
		decl->name = name;
		decl->type = type;
		owner->variables[name] = decl;
		owner->variables_in_order.push_back(decl);
		return;
	}
	if (!MergeVariableType(it->second->type, type))
	{
		throw runtime_error("conflicting variable declaration");
	}
}

void DeclareFunction(NamespaceDecl* owner, const string& name, TypePtr type)
{
	auto it = owner->functions.find(name);
	if (it == owner->functions.end())
	{
		shared_ptr<FunctionDecl> decl(new FunctionDecl);
		decl->name = name;
		decl->type = type;
		owner->functions[name] = decl;
		owner->functions_in_order.push_back(decl);
		return;
	}
	if (TypeEquals(it->second->type, type))
	{
		return;
	}
}

TypePtr LookupTypeQualified(NamespaceDecl* scope, const string& name);
bool HasValueQualified(NamespaceDecl* scope, const string& name);

NamespaceDecl* LookupNamespaceQualified(NamespaceDecl* scope, const string& name)
{
	auto named = scope->named_namespaces.find(name);
	if (named != scope->named_namespaces.end())
	{
		return named->second.get();
	}

	auto alias = scope->namespace_aliases.find(name);
	if (alias != scope->namespace_aliases.end())
	{
		return alias->second;
	}

	for (const NamespacePtr& child : scope->namespaces_in_order)
	{
		if (child->unnamed || child->inline_namespace)
		{
			NamespaceDecl* nested = LookupNamespaceQualified(child.get(), name);
			if (nested)
			{
				return nested;
			}
		}
	}

	return nullptr;
}

TypePtr LookupTypeQualified(NamespaceDecl* scope, const string& name)
{
	auto it = scope->types.find(name);
	if (it != scope->types.end())
	{
		return it->second;
	}

	for (const NamespacePtr& child : scope->namespaces_in_order)
	{
		if (child->unnamed || child->inline_namespace)
		{
			TypePtr nested = LookupTypeQualified(child.get(), name);
			if (nested)
			{
				return nested;
			}
		}
	}

	return TypePtr();
}

bool HasValueQualified(NamespaceDecl* scope, const string& name)
{
	if (scope->using_value_names.find(name) != scope->using_value_names.end())
	{
		return true;
	}
	if (scope->variables.find(name) != scope->variables.end() ||
		scope->functions.find(name) != scope->functions.end())
	{
		return true;
	}

	for (const NamespacePtr& child : scope->namespaces_in_order)
	{
		if (child->unnamed || child->inline_namespace)
		{
			if (HasValueQualified(child.get(), name))
			{
				return true;
			}
		}
	}

	return false;
}

TypePtr LookupTypeFromNamespaceSet(NamespaceDecl* scope, const string& name, unordered_set<NamespaceDecl*>& seen)
{
	if (!seen.insert(scope).second)
	{
		return TypePtr();
	}

	auto it = scope->types.find(name);
	if (it != scope->types.end())
	{
		return it->second;
	}

	for (NamespaceDecl* target : scope->using_directives)
	{
		TypePtr found = LookupTypeFromNamespaceSet(target, name, seen);
		if (found)
		{
			return found;
		}
	}

	return TypePtr();
}

TypePtr LookupTypeUnqualified(NamespaceDecl* scope, const string& name)
{
	for (NamespaceDecl* current = scope; current; current = current->parent)
	{
		unordered_set<NamespaceDecl*> seen;
		TypePtr found = LookupTypeFromNamespaceSet(current, name, seen);
		if (found)
		{
			return found;
		}
	}

	return TypePtr();
}

NamespaceDecl* LookupNamespaceFromNamespaceSet(NamespaceDecl* scope, const string& name, unordered_set<NamespaceDecl*>& seen)
{
	if (!seen.insert(scope).second)
	{
		return nullptr;
	}

	NamespaceDecl* found = LookupNamespaceQualified(scope, name);
	if (found)
	{
		return found;
	}

	for (NamespaceDecl* target : scope->using_directives)
	{
		found = LookupNamespaceFromNamespaceSet(target, name, seen);
		if (found)
		{
			return found;
		}
	}

	return nullptr;
}

NamespaceDecl* LookupNamespaceUnqualified(NamespaceDecl* scope, const string& name)
{
	for (NamespaceDecl* current = scope; current; current = current->parent)
	{
		unordered_set<NamespaceDecl*> seen;
		NamespaceDecl* found = LookupNamespaceFromNamespaceSet(current, name, seen);
		if (found)
		{
			return found;
		}
	}

	return nullptr;
}

struct NameRef
{
	NamespaceDecl* owner = nullptr;
	string name;
	bool has_name = false;
};

struct DeclaratorNode
{
	enum Kind
	{
		DK_HOLE,
		DK_NAME,
		DK_POINTER,
		DK_LVALUE_REF,
		DK_RVALUE_REF,
		DK_ARRAY,
		DK_FUNCTION
	};

	Kind kind = DK_HOLE;
	shared_ptr<DeclaratorNode> child;
	NameRef name;
	size_t array_bound = 0;
	bool array_unknown = false;
	vector<TypePtr> params;
	bool varargs = false;
};

typedef shared_ptr<DeclaratorNode> DeclaratorPtr;

DeclaratorPtr MakeDeclaratorNode(DeclaratorNode::Kind kind)
{
	DeclaratorPtr out(new DeclaratorNode);
	out->kind = kind;
	return out;
}

struct AppliedDeclarator
{
	NameRef name;
	TypePtr type;
};

AppliedDeclarator ApplyDeclarator(const DeclaratorPtr& node, TypePtr base)
{
	switch (node->kind)
	{
	case DeclaratorNode::DK_HOLE:
		return AppliedDeclarator{NameRef(), base};
	case DeclaratorNode::DK_NAME:
		return AppliedDeclarator{node->name, base};
	case DeclaratorNode::DK_POINTER:
		return ApplyDeclarator(node->child, MakePointerType(base));
	case DeclaratorNode::DK_LVALUE_REF:
		if (node->child->kind == DeclaratorNode::DK_LVALUE_REF || node->child->kind == DeclaratorNode::DK_RVALUE_REF)
		{
			throw runtime_error("reference to reference in declarator");
		}
		return ApplyDeclarator(node->child, MakeLValueReferenceType(base));
	case DeclaratorNode::DK_RVALUE_REF:
		if (node->child->kind == DeclaratorNode::DK_LVALUE_REF || node->child->kind == DeclaratorNode::DK_RVALUE_REF)
		{
			throw runtime_error("reference to reference in declarator");
		}
		return ApplyDeclarator(node->child, MakeRValueReferenceType(base));
	case DeclaratorNode::DK_ARRAY:
		return ApplyDeclarator(node->child, MakeArrayType(base, node->array_unknown, node->array_bound));
	case DeclaratorNode::DK_FUNCTION:
		return ApplyDeclarator(node->child, MakeFunctionType(base, node->params, node->varargs));
	}

	throw runtime_error("invalid declarator node");
}

struct ParsedSpecifiers
{
	bool is_typedef = false;
	bool is_extern = false;
	bool is_static = false;
	bool is_thread_local = false;
	bool is_constexpr = false;
	bool is_inline = false;
	bool is_const = false;
	bool is_volatile = false;
	bool has_non_cv_type = false;
	bool used_typedef_name = false;
	TypePtr typedef_type;
	vector<ETokenType> fundamental_tokens;
};

struct ParameterDecl
{
	TypePtr type;
	bool has_name = false;
};

struct ParameterClause
{
	vector<TypePtr> params;
	bool varargs = false;
};

struct NamespaceParse
{
	NamespaceDecl* scope = nullptr;
	bool rooted = false;
};

struct PA7Parser
{
	const vector<RecogToken>& tokens;
	size_t pos = 0;
	NamespaceDecl* global;
	NamespaceDecl* current;

	PA7Parser(const vector<RecogToken>& tokens, NamespaceDecl* global)
		: tokens(tokens), global(global), current(global)
	{}

	const RecogToken& Peek(size_t offset = 0) const
	{
		if (pos + offset >= tokens.size())
		{
			throw runtime_error("unexpected end of token stream");
		}
		return tokens[pos + offset];
	}

	bool AtEOF() const
	{
		return Peek().is_eof;
	}

	bool AtSimple(ETokenType type, size_t offset = 0) const
	{
		return pos + offset < tokens.size() &&
			tokens[pos + offset].has_simple &&
			tokens[pos + offset].simple == type;
	}

	bool AtIdentifier(size_t offset = 0) const
	{
		return pos + offset < tokens.size() && tokens[pos + offset].is_identifier;
	}

	bool AtLiteral(size_t offset = 0) const
	{
		return pos + offset < tokens.size() && tokens[pos + offset].is_literal;
	}

	bool MatchSimple(ETokenType type)
	{
		if (AtSimple(type))
		{
			++pos;
			return true;
		}
		return false;
	}

	void ExpectSimple(ETokenType type)
	{
		if (!MatchSimple(type))
		{
			throw runtime_error("unexpected token");
		}
	}

	string ConsumeIdentifier()
	{
		if (!AtIdentifier())
		{
			throw runtime_error("expected identifier");
		}
		return tokens[pos++].source;
	}

	size_t Mark() const
	{
		return pos;
	}

	void Reset(size_t mark)
	{
		pos = mark;
	}

	bool StartsDeclarator(bool allow_abstract) const
	{
		if (AtSimple(OP_STAR) || AtSimple(OP_AMP) || AtSimple(OP_LAND))
		{
			return true;
		}
		if (AtIdentifier() || AtSimple(OP_COLON2))
		{
			return true;
		}
		if (AtSimple(OP_LPAREN))
		{
			return true;
		}
		if (allow_abstract && AtSimple(OP_LSQUARE))
		{
			return true;
		}
		return false;
	}

	bool ParsePositiveArrayBound(const string& source, size_t& value)
	{
		string prefix;
		string ud_suffix;
		IntegerSuffix suffix;
		bool is_udl = false;
		bool is_hex = false;
		bool is_octal = false;
		if (!ParseIntegerLiteralParts(source, prefix, ud_suffix, suffix, is_udl, is_hex, is_octal) ||
			is_udl ||
			suffix.is_unsigned ||
			suffix.long_count > 2)
		{
			return false;
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

		unsigned __int128 parsed = 0;
		if (!ParseUnsignedIntegerValue(digits, base, parsed) || parsed == 0)
		{
			return false;
		}

		value = static_cast<size_t>(parsed);
		return static_cast<unsigned __int128>(value) == parsed;
	}

	TypePtr BuildFundamentalType(const ParsedSpecifiers& spec)
	{
		if (spec.used_typedef_name)
		{
			if (!spec.fundamental_tokens.empty())
			{
				throw runtime_error("invalid mixed type specifier");
			}
			return ApplyCV(spec.typedef_type, spec.is_const, spec.is_volatile);
		}

		int long_count = 0;
		bool is_short = false;
		bool is_signed = false;
		bool is_unsigned = false;
		bool saw_int = false;
		bool saw_char = false;
		bool saw_wchar = false;
		bool saw_char16 = false;
		bool saw_char32 = false;
		bool saw_bool = false;
		bool saw_float = false;
		bool saw_double = false;
		bool saw_void = false;

		for (ETokenType token : spec.fundamental_tokens)
		{
			switch (token)
			{
			case KW_LONG: ++long_count; break;
			case KW_SHORT: is_short = true; break;
			case KW_SIGNED: is_signed = true; break;
			case KW_UNSIGNED: is_unsigned = true; break;
			case KW_INT: saw_int = true; break;
			case KW_CHAR: saw_char = true; break;
			case KW_WCHAR_T: saw_wchar = true; break;
			case KW_CHAR16_T: saw_char16 = true; break;
			case KW_CHAR32_T: saw_char32 = true; break;
			case KW_BOOL: saw_bool = true; break;
			case KW_FLOAT: saw_float = true; break;
			case KW_DOUBLE: saw_double = true; break;
			case KW_VOID: saw_void = true; break;
			default: throw runtime_error("unsupported fundamental token");
			}
		}

		EFundamentalType type = FT_INT;
		if (saw_char)
		{
			type = is_unsigned ? FT_UNSIGNED_CHAR : (is_signed ? FT_SIGNED_CHAR : FT_CHAR);
		}
		else if (saw_char16)
		{
			type = FT_CHAR16_T;
		}
		else if (saw_char32)
		{
			type = FT_CHAR32_T;
		}
		else if (saw_wchar)
		{
			type = FT_WCHAR_T;
		}
		else if (saw_bool)
		{
			type = FT_BOOL;
		}
		else if (saw_float)
		{
			type = FT_FLOAT;
		}
		else if (saw_double)
		{
			type = long_count ? FT_LONG_DOUBLE : FT_DOUBLE;
		}
		else if (saw_void)
		{
			type = FT_VOID;
		}
		else if (is_short)
		{
			type = is_unsigned ? FT_UNSIGNED_SHORT_INT : FT_SHORT_INT;
		}
		else if (long_count >= 2)
		{
			type = is_unsigned ? FT_UNSIGNED_LONG_LONG_INT : FT_LONG_LONG_INT;
		}
		else if (long_count == 1)
		{
			type = is_unsigned ? FT_UNSIGNED_LONG_INT : FT_LONG_INT;
		}
		else
		{
			type = is_unsigned ? FT_UNSIGNED_INT : FT_INT;
		}

		return ApplyCV(MakeFundamentalType(type), spec.is_const, spec.is_volatile);
	}

	bool TryParseQualifiedTypeName(NamespaceDecl* scope, TypePtr& type)
	{
		size_t mark = Mark();
		if (AtIdentifier())
		{
			string first = ConsumeIdentifier();
			if (MatchSimple(OP_COLON2))
			{
				NamespaceDecl* ns = LookupNamespaceUnqualified(scope, first);
				if (!ns)
				{
					Reset(mark);
					return false;
				}
				while (true)
				{
					string next = ConsumeIdentifier();
					if (MatchSimple(OP_COLON2))
					{
						ns = LookupNamespaceQualified(ns, next);
						if (!ns)
						{
							Reset(mark);
							return false;
						}
						continue;
					}

					type = LookupTypeQualified(ns, next);
					if (!type)
					{
						Reset(mark);
						return false;
					}
					return true;
				}
			}

			type = LookupTypeUnqualified(scope, first);
			if (!type)
			{
				Reset(mark);
				return false;
			}
			return true;
		}

		if (MatchSimple(OP_COLON2))
		{
			NamespaceDecl* ns = global;
			while (true)
			{
				string next = ConsumeIdentifier();
				if (MatchSimple(OP_COLON2))
				{
					ns = LookupNamespaceQualified(ns, next);
					if (!ns)
					{
						Reset(mark);
						return false;
					}
					continue;
				}

				type = LookupTypeQualified(ns, next);
				if (!type)
				{
					Reset(mark);
					return false;
				}
				return true;
			}
		}

		return false;
	}

	NameRef ParseIdExpressionTarget()
	{
		NameRef ref;
		ref.owner = current;
		ref.has_name = true;

		if (MatchSimple(OP_COLON2))
		{
			ref.owner = global;
			while (true)
			{
				string next = ConsumeIdentifier();
				if (MatchSimple(OP_COLON2))
				{
					NamespaceDecl* ns = LookupNamespaceQualified(ref.owner, next);
					if (!ns)
					{
						throw runtime_error("unknown namespace");
					}
					ref.owner = ns;
					continue;
				}
				ref.name = next;
				return ref;
			}
		}

		string first = ConsumeIdentifier();
		if (!MatchSimple(OP_COLON2))
		{
			ref.name = first;
			return ref;
		}

		NamespaceDecl* ns = LookupNamespaceUnqualified(current, first);
		if (!ns)
		{
			throw runtime_error("unknown namespace");
		}
		ref.owner = ns;
		while (true)
		{
			string next = ConsumeIdentifier();
			if (MatchSimple(OP_COLON2))
			{
				ns = LookupNamespaceQualified(ref.owner, next);
				if (!ns)
				{
					throw runtime_error("unknown namespace");
				}
				ref.owner = ns;
				continue;
			}
			ref.name = next;
			return ref;
		}
	}

	bool ParseTypeSpecifierInto(ParsedSpecifiers& spec)
	{
		if (MatchSimple(KW_CONST))
		{
			spec.is_const = true;
			return true;
		}
		if (MatchSimple(KW_VOLATILE))
		{
			spec.is_volatile = true;
			return true;
		}

		const vector<ETokenType> fundamental =
		{
			KW_CHAR, KW_CHAR16_T, KW_CHAR32_T, KW_WCHAR_T, KW_BOOL,
			KW_SHORT, KW_INT, KW_LONG, KW_SIGNED, KW_UNSIGNED,
			KW_FLOAT, KW_DOUBLE, KW_VOID
		};

		for (ETokenType token : fundamental)
		{
			if (MatchSimple(token))
			{
				spec.fundamental_tokens.push_back(token);
				spec.has_non_cv_type = true;
				return true;
			}
		}

		if (!spec.has_non_cv_type)
		{
			TypePtr looked_up;
			if (TryParseQualifiedTypeName(current, looked_up))
			{
				spec.has_non_cv_type = true;
				spec.used_typedef_name = true;
				spec.typedef_type = looked_up;
				return true;
			}
		}

		return false;
	}

	ParsedSpecifiers ParseDeclSpecifierSeq(bool allow_storage, bool allow_typedef)
	{
		ParsedSpecifiers spec;
		bool any = false;
		while (true)
		{
			if (allow_storage && MatchSimple(KW_STATIC))
			{
				spec.is_static = true;
				any = true;
				continue;
			}
			if (allow_storage && MatchSimple(KW_THREAD_LOCAL))
			{
				spec.is_thread_local = true;
				any = true;
				continue;
			}
			if (allow_storage && MatchSimple(KW_EXTERN))
			{
				spec.is_extern = true;
				any = true;
				continue;
			}
			if (MatchSimple(KW_CONSTEXPR))
			{
				spec.is_constexpr = true;
				any = true;
				continue;
			}
			if (MatchSimple(KW_INLINE))
			{
				spec.is_inline = true;
				any = true;
				continue;
			}
			if (allow_typedef && MatchSimple(KW_TYPEDEF))
			{
				spec.is_typedef = true;
				any = true;
				continue;
			}
			if (ParseTypeSpecifierInto(spec))
			{
				any = true;
				continue;
			}
			break;
		}

		if (!any)
		{
			throw runtime_error("expected decl-specifier-seq");
		}

		if (!spec.has_non_cv_type)
		{
			throw runtime_error("missing type specifier");
		}

		return spec;
	}

	TypePtr ParseTypeSpecifierSeq()
	{
		ParsedSpecifiers spec = ParseDeclSpecifierSeq(false, false);
		return BuildFundamentalType(spec);
	}

	ParameterClause ParseParameterDeclarationClause()
	{
		ParameterClause clause;

		if (MatchSimple(OP_DOTS))
		{
			clause.varargs = true;
			return clause;
		}

		if (AtSimple(OP_RPAREN))
		{
			return clause;
		}

		while (true)
		{
			ParsedSpecifiers spec = ParseDeclSpecifierSeq(false, false);
			TypePtr base = BuildFundamentalType(spec);

			DeclaratorPtr declarator;
			bool has_declarator = StartsDeclarator(true) && !AtSimple(OP_RPAREN);
			if (has_declarator)
			{
				declarator = ParseDeclarator(true);
			}
			else
			{
				declarator = MakeDeclaratorNode(DeclaratorNode::DK_HOLE);
			}

			AppliedDeclarator applied = ApplyDeclarator(declarator, base);
			TypePtr param_type = applied.type;

			if (!(clause.params.empty() &&
				!clause.varargs &&
				!applied.name.has_name &&
				!has_declarator &&
				param_type->kind == Type::TK_FUNDAMENTAL &&
				param_type->fundamental == FT_VOID))
			{
				clause.params.push_back(AdjustParameterType(param_type));
			}

			if (MatchSimple(OP_COMMA))
			{
				if (MatchSimple(OP_DOTS))
				{
					clause.varargs = true;
					break;
				}
				continue;
			}

			if (MatchSimple(OP_DOTS))
			{
				clause.varargs = true;
			}
			break;
		}

		return clause;
	}

	DeclaratorPtr ParseDeclarator(bool allow_abstract)
	{
		vector<DeclaratorNode::Kind> ptr_ops;
		while (true)
		{
			if (MatchSimple(OP_STAR))
			{
				while (MatchSimple(KW_CONST) || MatchSimple(KW_VOLATILE))
				{}
				ptr_ops.push_back(DeclaratorNode::DK_POINTER);
				continue;
			}
			if (MatchSimple(OP_AMP))
			{
				ptr_ops.push_back(DeclaratorNode::DK_LVALUE_REF);
				continue;
			}
			if (MatchSimple(OP_LAND))
			{
				ptr_ops.push_back(DeclaratorNode::DK_RVALUE_REF);
				continue;
			}
			break;
		}

		DeclaratorPtr node;
		bool consumed_root = false;

		size_t mark = Mark();
		if (MatchSimple(OP_LPAREN))
		{
			size_t inner_mark = Mark();
			try
			{
				DeclaratorPtr inner = ParseDeclarator(allow_abstract);
				if (Mark() != inner_mark && MatchSimple(OP_RPAREN))
				{
					node = inner;
					consumed_root = true;
				}
				else
				{
					Reset(mark);
				}
			}
			catch (...)
			{
				Reset(mark);
			}
		}

		if (!consumed_root)
		{
			if (AtIdentifier() || AtSimple(OP_COLON2))
			{
				node = MakeDeclaratorNode(DeclaratorNode::DK_NAME);
				node->name = ParseIdExpressionTarget();
				consumed_root = true;
			}
			else if (allow_abstract)
			{
				node = MakeDeclaratorNode(DeclaratorNode::DK_HOLE);
			}
			else
			{
				throw runtime_error("expected declarator");
			}
		}

		bool had_suffix = false;
		while (true)
		{
			if (MatchSimple(OP_LPAREN))
			{
				ParameterClause clause = ParseParameterDeclarationClause();
				ExpectSimple(OP_RPAREN);
				DeclaratorPtr fn = MakeDeclaratorNode(DeclaratorNode::DK_FUNCTION);
				fn->child = node;
				fn->params = clause.params;
				fn->varargs = clause.varargs;
				node = fn;
				had_suffix = true;
				continue;
			}

			if (MatchSimple(OP_LSQUARE))
			{
				bool unknown = true;
				size_t bound = 0;
				if (!AtSimple(OP_RSQUARE))
				{
					if (AtLiteral())
					{
						if (!ParsePositiveArrayBound(tokens[pos].source, bound))
						{
							throw runtime_error("invalid array bound");
						}
						++pos;
						unknown = false;
					}
					else if (CPPGM_PA7_ARRAY_BOUND_HOOK)
					{
						NamespaceDecl* saved_scope = current;
						if (node->kind == DeclaratorNode::DK_NAME && node->name.has_name && node->name.owner)
						{
							current = node->name.owner;
						}
						bool ok = CPPGM_PA7_ARRAY_BOUND_HOOK(this, bound);
						current = saved_scope;
						if (!ok)
						{
							throw runtime_error("invalid array bound");
						}
						unknown = false;
					}
					else
					{
						throw runtime_error("invalid array bound");
					}
				}
				ExpectSimple(OP_RSQUARE);
				DeclaratorPtr arr = MakeDeclaratorNode(DeclaratorNode::DK_ARRAY);
				arr->child = node;
				arr->array_unknown = unknown;
				arr->array_bound = bound;
				node = arr;
				had_suffix = true;
				continue;
			}

			break;
		}

		if (allow_abstract &&
			node->kind == DeclaratorNode::DK_HOLE &&
			!had_suffix &&
			ptr_ops.empty())
		{
			throw runtime_error("empty abstract declarator");
		}

		for (vector<DeclaratorNode::Kind>::reverse_iterator it = ptr_ops.rbegin(); it != ptr_ops.rend(); ++it)
		{
			DeclaratorPtr wrapper = MakeDeclaratorNode(*it);
			wrapper->child = node;
			node = wrapper;
		}

		return node;
	}

	TypePtr ParseTypeId()
	{
		TypePtr base = ParseTypeSpecifierSeq();
		if (!StartsDeclarator(true) || AtSimple(OP_RPAREN) || AtSimple(OP_SEMICOLON))
		{
			return base;
		}

		size_t mark = Mark();
		try
		{
			DeclaratorPtr decl = ParseDeclarator(true);
			return ApplyDeclarator(decl, base).type;
		}
		catch (...)
		{
			Reset(mark);
			return base;
		}
	}

	void ParseEmptyDeclaration()
	{
		ExpectSimple(OP_SEMICOLON);
	}

	void ParseAliasDeclaration()
	{
		ExpectSimple(KW_USING);
		string name = ConsumeIdentifier();
		ExpectSimple(OP_ASS);
		TypePtr type = ParseTypeId();
		ExpectSimple(OP_SEMICOLON);
		DeclareTypeAlias(current, name, type);
	}

	NamespaceDecl* ParseQualifiedNamespaceSpecifier()
	{
		if (MatchSimple(OP_COLON2))
		{
			NamespaceDecl* scope = global;
			while (true)
			{
				string name = ConsumeIdentifier();
				if (MatchSimple(OP_COLON2))
				{
					scope = LookupNamespaceQualified(scope, name);
					if (!scope)
					{
						throw runtime_error("unknown namespace");
					}
					continue;
				}

				NamespaceDecl* result = LookupNamespaceQualified(scope, name);
				if (!result)
				{
					throw runtime_error("unknown namespace");
				}
				return result;
			}
		}

		string first = ConsumeIdentifier();
		if (MatchSimple(OP_COLON2))
		{
			NamespaceDecl* scope = LookupNamespaceUnqualified(current, first);
			if (!scope)
			{
				throw runtime_error("unknown namespace");
			}
			while (true)
			{
				string name = ConsumeIdentifier();
				if (MatchSimple(OP_COLON2))
				{
					scope = LookupNamespaceQualified(scope, name);
					if (!scope)
					{
						throw runtime_error("unknown namespace");
					}
					continue;
				}

				NamespaceDecl* result = LookupNamespaceQualified(scope, name);
				if (!result)
				{
					throw runtime_error("unknown namespace");
				}
				return result;
			}
		}

		NamespaceDecl* result = LookupNamespaceUnqualified(current, first);
		if (!result)
		{
			throw runtime_error("unknown namespace");
		}
		return result;
	}

	void ParseNamespaceAliasDefinition()
	{
		ExpectSimple(KW_NAMESPACE);
		string alias = ConsumeIdentifier();
		ExpectSimple(OP_ASS);
		NamespaceDecl* target = ParseQualifiedNamespaceSpecifier();
		ExpectSimple(OP_SEMICOLON);

		if (current->named_namespaces.find(alias) != current->named_namespaces.end() ||
			current->types.find(alias) != current->types.end() ||
			current->variables.find(alias) != current->variables.end() ||
			current->functions.find(alias) != current->functions.end() ||
			current->using_value_names.find(alias) != current->using_value_names.end())
		{
			throw runtime_error("conflicting namespace alias");
		}

		auto it = current->namespace_aliases.find(alias);
		if (it == current->namespace_aliases.end())
		{
			current->namespace_aliases[alias] = target;
		}
		else if (it->second != target)
		{
			throw runtime_error("conflicting namespace alias");
		}
	}

	void ParseUsingDeclaration()
	{
		ExpectSimple(KW_USING);
		NameRef ref = ParseIdExpressionTarget();
		ExpectSimple(OP_SEMICOLON);
		TypePtr type = LookupTypeQualified(ref.owner, ref.name);
		if (type)
		{
			DeclareTypeAlias(current, ref.name, type);
			return;
		}
		if (!HasValueQualified(ref.owner, ref.name))
		{
			throw runtime_error("using target not found");
		}
		current->using_value_names.insert(ref.name);
	}

	void ParseUsingDirective()
	{
		ExpectSimple(KW_USING);
		ExpectSimple(KW_NAMESPACE);
		NamespaceDecl* target = ParseQualifiedNamespaceSpecifier();
		ExpectSimple(OP_SEMICOLON);
		AddUsingDirective(current, target);
	}

	void ParseNamespaceDefinition()
	{
		bool inline_namespace = MatchSimple(KW_INLINE);
		ExpectSimple(KW_NAMESPACE);

		NamespaceDecl* owner = current;
		NamespaceDecl* child = nullptr;
		if (AtSimple(OP_LBRACE))
		{
			child = CreateUnnamedNamespace(owner, inline_namespace);
		}
		else
		{
			string name = ConsumeIdentifier();
			child = GetOrCreateNamedNamespace(owner, name, inline_namespace);
		}

		ExpectSimple(OP_LBRACE);
		NamespaceDecl* saved = current;
		current = child;
		while (!AtSimple(OP_RBRACE))
		{
			ParseDeclaration();
		}
		ExpectSimple(OP_RBRACE);
		current = saved;
	}

	void ParseSimpleDeclaration()
	{
		ParsedSpecifiers spec = ParseDeclSpecifierSeq(true, true);
		TypePtr base = BuildFundamentalType(spec);

		while (true)
		{
			DeclaratorPtr declarator = ParseDeclarator(false);
			AppliedDeclarator applied = ApplyDeclarator(declarator, base);

			if (spec.is_typedef)
			{
				DeclareTypeAlias(applied.name.owner, applied.name.name, applied.type);
			}
			else if (applied.type->kind == Type::TK_FUNCTION)
			{
				DeclareFunction(applied.name.owner, applied.name.name, applied.type);
			}
			else
			{
				DeclareVariable(applied.name.owner, applied.name.name, applied.type);
			}

			if (!MatchSimple(OP_COMMA))
			{
				break;
			}
		}

		ExpectSimple(OP_SEMICOLON);
	}

	void ParseDeclaration()
	{
		if (AtSimple(OP_SEMICOLON))
		{
			ParseEmptyDeclaration();
			return;
		}

		if (AtSimple(KW_INLINE) && AtSimple(KW_NAMESPACE, 1))
		{
			ParseNamespaceDefinition();
			return;
		}

		if (AtSimple(KW_NAMESPACE))
		{
			if (AtIdentifier(1) && AtSimple(OP_ASS, 2))
			{
				ParseNamespaceAliasDefinition();
			}
			else
			{
				ParseNamespaceDefinition();
			}
			return;
		}

		if (AtSimple(KW_USING))
		{
			if (AtIdentifier(1) && AtSimple(OP_ASS, 2))
			{
				ParseAliasDeclaration();
			}
			else if (AtSimple(KW_NAMESPACE, 1))
			{
				ParseUsingDirective();
			}
			else
			{
				ParseUsingDeclaration();
			}
			return;
		}

		ParseSimpleDeclaration();
	}

	void ParseTranslationUnit()
	{
		while (!AtEOF())
		{
			ParseDeclaration();
		}
	}
};

void PrintNamespace(ostream& out, NamespaceDecl* ns)
{
	if (ns->unnamed)
	{
		out << "start unnamed namespace" << endl;
	}
	else
	{
		out << "start namespace " << ns->name << endl;
	}

	if (ns->inline_namespace)
	{
		out << "inline namespace" << endl;
	}

	for (const shared_ptr<VariableDecl>& var : ns->variables_in_order)
	{
		out << "variable " << var->name << " " << DescribeType(var->type) << endl;
	}

	for (const shared_ptr<FunctionDecl>& fn : ns->functions_in_order)
	{
		out << "function " << fn->name << " " << DescribeType(fn->type) << endl;
	}

	for (const NamespacePtr& child : ns->namespaces_in_order)
	{
		PrintNamespace(out, child.get());
	}

	out << "end namespace" << endl;
}

NamespacePtr AnalyzeTranslationUnit(const string& srcfile)
{
	vector<RecogToken> tokens = PreprocessAndTokenize(srcfile);
	NamespacePtr global = CreateNamespace(nullptr, "", true, false);
	PA7Parser parser(tokens, global.get());
	parser.ParseTranslationUnit();
	return global;
}

#ifndef CPPGM_EMBED_NSDECL
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

		ofstream out(outfile.c_str());

		out << nsrcfiles << " translation units" << endl;

		for (size_t i = 0; i < nsrcfiles; i++)
		{
			string srcfile = args[i+2];

			out << "start translation unit " << srcfile << endl;
			NamespacePtr global = AnalyzeTranslationUnit(srcfile);
			PrintNamespace(out, global.get());
			out << "end translation unit" << endl;
		}
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
#endif
