// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace std;

#define CPPGM_EMBED_NSDECL 1
#include "nsdecl.cpp"

struct PA8Variable
{
	enum RelocKind
	{
		RK_NONE,
		RK_VARIABLE,
		RK_FUNCTION,
		RK_TEMP
	};

	string key;
	TypePtr type;
	vector<char> bytes;
	bool is_extern_only = false;
	RelocKind reloc_kind = RK_NONE;
	string reloc_key;
	size_t reloc_index = 0;
	bool has_constant_integer = false;
	unsigned long long constant_integer = 0;
	bool has_constant_address = false;
	RelocKind constant_address_kind = RK_NONE;
	string constant_address_key;
	size_t constant_address_index = 0;
};

struct PA8Function
{
	string key;
	TypePtr type;
	bool defined = false;
	bool is_inline = false;
};

struct PA8String
{
	TypePtr type;
	vector<char> bytes;
};

struct PA8Temp
{
	TypePtr type;
	vector<char> bytes;
};

struct PA8Item
{
	bool is_function = false;
	size_t index = 0;

	PA8Item()
	{}

	PA8Item(bool is_function, size_t index)
		: is_function(is_function), index(index)
	{}
};

struct PA8InitValue
{
	vector<char> bytes;
	PA8Variable::RelocKind reloc_kind = PA8Variable::RK_NONE;
	string reloc_key;
	size_t reloc_index = 0;
	bool has_constant_integer = false;
	unsigned long long constant_integer = 0;
	bool has_constant_address = false;
	PA8Variable::RelocKind constant_address_kind = PA8Variable::RK_NONE;
	string constant_address_key;
	size_t constant_address_index = 0;
};

bool ParseSmallIntegerLiteral(const string& source, long long& value)
{
	string prefix;
	string ud_suffix;
	IntegerSuffix suffix;
	bool is_udl = false;
	bool is_hex = false;
	bool is_octal = false;
	if (!ParseIntegerLiteralParts(source, prefix, ud_suffix, suffix, is_udl, is_hex, is_octal) || is_udl)
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
	if (!ParseUnsignedIntegerValue(digits, base, parsed))
	{
		return false;
	}

	value = static_cast<long long>(parsed);
	return static_cast<unsigned __int128>(value) == parsed;
}

size_t TypeSize(TypePtr type)
{
	type = StripTopLevelCV(type);
	if (type->kind == Type::TK_FUNDAMENTAL)
	{
		switch (type->fundamental)
		{
		case FT_CHAR:
		case FT_SIGNED_CHAR:
		case FT_UNSIGNED_CHAR:
		case FT_BOOL:
			return 1;
		case FT_CHAR16_T:
		case FT_SHORT_INT:
		case FT_UNSIGNED_SHORT_INT:
			return 2;
		case FT_CHAR32_T:
		case FT_WCHAR_T:
		case FT_INT:
		case FT_UNSIGNED_INT:
		case FT_FLOAT:
			return 4;
		case FT_LONG_INT:
		case FT_LONG_LONG_INT:
		case FT_UNSIGNED_LONG_INT:
		case FT_UNSIGNED_LONG_LONG_INT:
		case FT_DOUBLE:
		case FT_NULLPTR_T:
			return 8;
		case FT_LONG_DOUBLE:
			return 16;
		case FT_VOID:
			return 0;
		}
	}
	if (type->kind == Type::TK_POINTER || type->kind == Type::TK_LVALUE_REF || type->kind == Type::TK_RVALUE_REF)
	{
		return 8;
	}
	if (type->kind == Type::TK_FUNCTION)
	{
		return 4;
	}
	if (type->kind == Type::TK_ARRAY)
	{
		return type->array_unknown ? 0 : type->array_bound * TypeSize(type->child);
	}
	return TypeSize(type->child);
}

size_t TypeAlign(TypePtr type)
{
	type = StripTopLevelCV(type);
	if (type->kind == Type::TK_ARRAY)
	{
		return TypeAlign(type->child);
	}
	size_t size = TypeSize(type);
	return size ? size : 1;
}

vector<char> ZeroBytes(size_t n)
{
	return vector<char>(n, 0);
}

vector<char> EncodeIntegralBytes(TypePtr type, long long value)
{
	size_t size = TypeSize(type);
	vector<char> bytes(size, 0);
	for (size_t i = 0; i < size; ++i)
	{
		bytes[i] = static_cast<char>((static_cast<unsigned long long>(value) >> (8 * i)) & 0xFF);
	}
	return bytes;
}

template <typename T>
vector<char> EncodeObjectBytes(const T& value)
{
	vector<char> bytes(sizeof(T), 0);
	memcpy(bytes.data(), &value, sizeof(T));
	return bytes;
}

bool ParseCharacterLiteralBytes(TypePtr type, const string& source, vector<char>& bytes)
{
	CharacterLiteralInfo info;
	if (!ParseCharacterLiteralSource(source, info) || !info.valid || info.user_defined)
	{
		return false;
	}

	bytes = EncodeIntegralBytes(type, static_cast<long long>(info.value));
	return true;
}

bool ParseFloatingLiteralBytes(TypePtr type, const string& source, vector<char>& bytes)
{
	string prefix;
	string ud_suffix;
	char float_suffix = 0;
	bool is_udl = false;
	if (!ParseFloatingLiteralParts(source, prefix, ud_suffix, float_suffix, is_udl) || is_udl)
	{
		return false;
	}

	const char* begin = prefix.c_str();
	char* end = nullptr;
	type = StripTopLevelCV(type);
	switch (type->fundamental)
	{
	case FT_FLOAT:
	{
		float value = strtof(begin, &end);
		if (!end || *end)
		{
			return false;
		}
		bytes = EncodeObjectBytes(value);
		return true;
	}
	case FT_DOUBLE:
	{
		double value = strtod(begin, &end);
		if (!end || *end)
		{
			return false;
		}
		bytes = EncodeObjectBytes(value);
		return true;
	}
	case FT_LONG_DOUBLE:
	{
		long double value = strtold(begin, &end);
		if (!end || *end)
		{
			return false;
		}
		bytes = EncodeObjectBytes(value);
		return true;
	}
	default:
		return false;
	}
}

bool ParseLiteralBytes(TypePtr type, const string& source, vector<char>& bytes)
{
	long long integer_value = 0;
	if (ParseSmallIntegerLiteral(source, integer_value))
	{
		bytes = EncodeIntegralBytes(type, integer_value);
		return true;
	}

	if (ParseCharacterLiteralBytes(type, source, bytes))
	{
		return true;
	}

	if (StripTopLevelCV(type)->kind == Type::TK_FUNDAMENTAL && ParseFloatingLiteralBytes(type, source, bytes))
	{
		return true;
	}

	return false;
}

bool ParseStringLiteralBytes(const string& source, TypePtr& literal_type, vector<char>& bytes, size_t& count)
{
	StringLiteralPiece piece;
	if (!ParseStringLiteralPiece(source, piece) || !piece.valid || piece.user_defined)
	{
		return false;
	}

	switch (piece.encoding)
	{
	case SE_ORDINARY:
	case SE_UTF8:
	{
		if (!EncodeCodePointsUtf8(piece.code_points, bytes))
		{
			return false;
		}
		count = bytes.size();
		literal_type = MakeArrayType(MakeFundamentalType(FT_CHAR), false, count);
		return true;
	}
	case SE_UTF16:
	{
		vector<char16_t> units;
		if (!EncodeCodePointsUtf16(piece.code_points, units))
		{
			return false;
		}
		bytes.resize(units.size() * sizeof(char16_t));
		memcpy(bytes.data(), units.data(), bytes.size());
		count = units.size();
		literal_type = MakeArrayType(MakeFundamentalType(FT_CHAR16_T), false, count);
		return true;
	}
	case SE_UTF32:
	{
		vector<char32_t> units;
		if (!EncodeCodePointsUtf32(piece.code_points, units))
		{
			return false;
		}
		bytes.resize(units.size() * sizeof(char32_t));
		memcpy(bytes.data(), units.data(), bytes.size());
		count = units.size();
		literal_type = MakeArrayType(MakeFundamentalType(FT_CHAR32_T), false, count);
		return true;
	}
	case SE_WIDE:
	{
		vector<char32_t> units;
		if (!EncodeCodePointsUtf32(piece.code_points, units))
		{
			return false;
		}
		bytes.resize(units.size() * sizeof(char32_t));
		memcpy(bytes.data(), units.data(), bytes.size());
		count = units.size();
		literal_type = MakeArrayType(MakeFundamentalType(FT_WCHAR_T), false, count);
		return true;
	}
	}

	return false;
}

bool ParseIntegralLiteralConstant(const string& source, unsigned long long& value)
{
	long long signed_value = 0;
	if (ParseSmallIntegerLiteral(source, signed_value))
	{
		value = static_cast<unsigned long long>(signed_value);
		return true;
	}

	CharacterLiteralInfo info;
	if (ParseCharacterLiteralSource(source, info) && info.valid && !info.user_defined)
	{
		value = info.value;
		return true;
	}

	return false;
}

bool TypeHasTopLevelConst(TypePtr type)
{
	return type->kind == Type::TK_CV && type->is_const;
}

bool TypeIsReference(TypePtr type)
{
	return type->kind == Type::TK_LVALUE_REF || type->kind == Type::TK_RVALUE_REF;
}

bool TypeRequiresInitializer(TypePtr type)
{
	if (TypeHasTopLevelConst(type) || TypeIsReference(type))
	{
		return true;
	}

	type = StripTopLevelCV(type);
	if (type->kind == Type::TK_ARRAY)
	{
		return TypeRequiresInitializer(type->child);
	}
	return false;
}

bool TypeIsComplete(TypePtr type)
{
	type = StripTopLevelCV(type);
	if (type->kind == Type::TK_FUNDAMENTAL)
	{
		return type->fundamental != FT_VOID;
	}
	if (type->kind == Type::TK_ARRAY)
	{
		return !type->array_unknown && TypeIsComplete(type->child);
	}
	if (type->kind == Type::TK_FUNCTION)
	{
		return true;
	}
	if (type->kind == Type::TK_POINTER || type->kind == Type::TK_LVALUE_REF || type->kind == Type::TK_RVALUE_REF)
	{
		return true;
	}
	return TypeIsComplete(type->child);
}

bool NamespaceEncloses(NamespaceDecl* scope, NamespaceDecl* target)
{
	for (NamespaceDecl* it = target; it; it = it->parent)
	{
		if (it == scope)
		{
			return true;
		}
	}
	return false;
}

struct PA8BootstrapParser : PA7Parser
{
	string internal_scope_tag;
	vector<PA8Variable> variables;
	vector<PA8Function> functions;
	vector<PA8Temp> temps;
	vector<PA8String> strings;
	vector<PA8Item> order;
	unordered_map<string, size_t> variable_index;
	unordered_map<string, size_t> function_index;
	unordered_map<string, size_t> local_variable_index;
	unordered_map<string, size_t> local_function_index;

	static NamespaceDecl* CreateBootstrapGlobal()
	{
		NamespaceDecl* ns = new NamespaceDecl;
		ns->unnamed = true;
		return ns;
	}

	static bool ParseArrayBoundHook(PA7Parser* parser, size_t& value)
	{
		PA8BootstrapParser* self = static_cast<PA8BootstrapParser*>(parser);
		PA8InitValue bound = self->ParseConstantExpressionValue();
		if (!bound.has_constant_integer || bound.constant_integer == 0)
		{
			return false;
		}
		value = static_cast<size_t>(bound.constant_integer);
		return static_cast<unsigned long long>(value) == bound.constant_integer;
	}

	explicit PA8BootstrapParser(const vector<RecogToken>& tokens, const string& source_tag)
		: PA7Parser(tokens, CreateBootstrapGlobal()),
		  internal_scope_tag(source_tag)
	{
		CPPGM_PA7_ARRAY_BOUND_HOOK = &ParseArrayBoundHook;
	}

	string InternalScopeKey(const NameRef& ref) const
	{
		return internal_scope_tag + "@" + to_string(reinterpret_cast<uintptr_t>(ref.owner));
	}

	string LocalScopeKey(const NameRef& ref) const
	{
		return to_string(reinterpret_cast<uintptr_t>(ref.owner));
	}

	string ExternalScopeKey(NamespaceDecl* scope) const
	{
		vector<string> parts;
		for (NamespaceDecl* it = scope; it; it = it->parent)
		{
			if (!it->name.empty())
			{
				parts.push_back(it->name);
			}
		}

		string out = "::";
		for (vector<string>::reverse_iterator it = parts.rbegin(); it != parts.rend(); ++it)
		{
			if (out.size() > 2)
			{
				out += "::";
			}
			out += *it;
		}
		return out;
	}

	bool NamespaceHasInternalLinkage(NamespaceDecl* scope) const
	{
		for (NamespaceDecl* it = scope; it; it = it->parent)
		{
			if (it->unnamed && it->parent)
			{
				return true;
			}
		}
		return false;
	}

	string GlobalVariableKey(const NameRef& ref, const ParsedSpecifiers& spec)
	{
		if (spec.is_static || NamespaceHasInternalLinkage(ref.owner))
		{
			return InternalScopeKey(ref) + "::" + ref.name;
		}
		return ExternalScopeKey(ref.owner) + "::" + ref.name;
	}

	string GlobalFunctionKey(const NameRef& ref, const ParsedSpecifiers& spec, TypePtr type)
	{
		string signature = DescribeType(type);
		if (spec.is_static || NamespaceHasInternalLinkage(ref.owner))
		{
			return InternalScopeKey(ref) + "::" + ref.name + "|" + signature;
		}
		return ExternalScopeKey(ref.owner) + "::" + ref.name + "|" + signature;
	}

	string LocalVariableKey(const NameRef& ref) const
	{
		return LocalScopeKey(ref) + "::" + ref.name;
	}

	string LocalFunctionKey(const NameRef& ref, TypePtr type) const
	{
		return LocalScopeKey(ref) + "::" + ref.name + "|" + DescribeType(type);
	}

	PA8InitValue ParseConstantExpressionValue()
	{
		if (MatchSimple(KW_TRUE))
		{
			PA8InitValue value;
			value.has_constant_integer = true;
			value.constant_integer = 1;
			return value;
		}
		if (MatchSimple(KW_FALSE) || MatchSimple(KW_NULLPTR))
		{
			PA8InitValue value;
			value.has_constant_integer = true;
			value.constant_integer = 0;
			return value;
		}
		if (AtLiteral())
		{
			unsigned long long literal = 0;
			if (ParseIntegralLiteralConstant(tokens[pos].source, literal))
			{
				++pos;
				PA8InitValue value;
				value.has_constant_integer = true;
				value.constant_integer = literal;
				return value;
			}
			throw runtime_error("unsupported constant expression");
		}
		if (MatchSimple(OP_LPAREN))
		{
			PA8InitValue value = ParseConstantExpressionValue();
			ExpectSimple(OP_RPAREN);
			return value;
		}
		if (AtIdentifier() || AtSimple(OP_COLON2))
		{
			NameRef ref = ParseIdExpressionTarget();
			auto vit = local_variable_index.find(LocalVariableKey(ref));
			if (vit == local_variable_index.end())
			{
				throw runtime_error("unsupported constant expression");
			}
			const PA8Variable& var = variables[vit->second];
			PA8InitValue value;
			if (var.has_constant_integer)
			{
				value.has_constant_integer = true;
				value.constant_integer = var.constant_integer;
				return value;
			}
			if (var.has_constant_address)
			{
				value.has_constant_address = true;
				value.constant_address_kind = var.constant_address_kind;
				value.constant_address_key = var.constant_address_key;
				value.constant_address_index = var.constant_address_index;
				return value;
			}
			throw runtime_error("unsupported constant expression");
		}
		throw runtime_error("unsupported constant expression");
	}

	PA8InitValue ParseInitializerValue(TypePtr& type)
	{
		if (TypeIsReference(type))
		{
			TypePtr referred = type->child;
			if (AtLiteral())
			{
				vector<char> bytes;
				if (!ParseLiteralBytes(referred, tokens[pos].source, bytes))
				{
					throw runtime_error("unsupported initializer");
				}
				PA8Temp temp;
				temp.type = referred;
				temp.bytes = bytes;
				temps.push_back(temp);
				++pos;

				PA8InitValue init;
				init.bytes = ZeroBytes(TypeSize(type));
				init.reloc_kind = PA8Variable::RK_TEMP;
				init.reloc_index = temps.size() - 1;
				unsigned long long literal = 0;
				if (ParseIntegralLiteralConstant(tokens[pos - 1].source, literal))
				{
					init.has_constant_integer = true;
					init.constant_integer = literal;
				}
				return init;
			}
			if (AtIdentifier() || AtSimple(OP_COLON2))
			{
				NameRef ref = ParseIdExpressionTarget();
				auto it = local_variable_index.find(LocalVariableKey(ref));
				if (it == local_variable_index.end())
				{
					throw runtime_error("unsupported initializer");
				}
				const PA8Variable& target = variables[it->second];
				PA8InitValue init;
				init.bytes = ZeroBytes(TypeSize(type));
				if (TypeIsReference(target.type))
				{
					init.reloc_kind = target.reloc_kind;
					init.reloc_key = target.reloc_key;
					init.reloc_index = target.reloc_index;
				}
				else
				{
					init.reloc_kind = PA8Variable::RK_VARIABLE;
					init.reloc_key = target.key;
				}
				if (target.has_constant_integer)
				{
					init.has_constant_integer = true;
					init.constant_integer = target.constant_integer;
				}
				if (target.has_constant_address)
				{
					init.has_constant_address = true;
					init.constant_address_kind = target.constant_address_kind;
					init.constant_address_key = target.constant_address_key;
					init.constant_address_index = target.constant_address_index;
				}
				return init;
			}
			throw runtime_error("unsupported initializer");
		}

		if (MatchSimple(KW_NULLPTR))
		{
			TypePtr value_type = StripTopLevelCV(type);
			if (value_type->kind != Type::TK_POINTER &&
				value_type->kind != Type::TK_LVALUE_REF &&
				value_type->kind != Type::TK_RVALUE_REF)
			{
				throw runtime_error("unsupported initializer");
			}
			PA8InitValue init;
			init.bytes = ZeroBytes(TypeSize(type));
			init.has_constant_integer = true;
			init.constant_integer = 0;
			return init;
		}
		if (MatchSimple(KW_TRUE))
		{
			PA8InitValue init;
			init.bytes = EncodeIntegralBytes(type, 1);
			init.has_constant_integer = true;
			init.constant_integer = 1;
			return init;
		}
		if (MatchSimple(KW_FALSE))
		{
			PA8InitValue init;
			init.bytes = EncodeIntegralBytes(type, 0);
			init.has_constant_integer = true;
			init.constant_integer = 0;
			return init;
		}
		if (AtLiteral())
		{
			TypePtr value_type = StripTopLevelCV(type);
			if (value_type->kind == Type::TK_ARRAY)
			{
				TypePtr literal_type;
				vector<char> literal_bytes;
				size_t literal_count = 0;
				if (ParseStringLiteralBytes(tokens[pos].source, literal_type, literal_bytes, literal_count))
				{
					PA8String str;
					str.type = literal_type;
					str.bytes = literal_bytes;
					strings.push_back(str);

					TypePtr elem = StripTopLevelCV(value_type->child);
					TypePtr literal_elem = StripTopLevelCV(literal_type->child);
					if (elem->kind != Type::TK_FUNDAMENTAL ||
						literal_elem->kind != Type::TK_FUNDAMENTAL)
					{
						throw runtime_error("unsupported initializer");
					}

					bool compatible = false;
					if (literal_elem->fundamental == FT_CHAR)
					{
						compatible =
							elem->fundamental == FT_CHAR ||
							elem->fundamental == FT_SIGNED_CHAR ||
							elem->fundamental == FT_UNSIGNED_CHAR;
					}
					else
					{
						compatible = elem->fundamental == literal_elem->fundamental;
					}

					if (!compatible)
					{
						throw runtime_error("unsupported initializer");
					}

					size_t bound = value_type->array_bound;
					if (value_type->array_unknown)
					{
						type = MakeArrayType(value_type->child, false, literal_count);
						bound = literal_count;
					}
					if (bound < literal_count)
					{
						throw runtime_error("unsupported initializer");
					}

					PA8InitValue init;
					init.bytes.assign(bound * TypeSize(value_type->child), 0);
					copy(literal_bytes.begin(), literal_bytes.end(), init.bytes.begin());
					++pos;
					return init;
				}
			}

			vector<char> bytes;
			if (!ParseLiteralBytes(type, tokens[pos].source, bytes))
			{
				throw runtime_error("unsupported initializer");
			}
			++pos;
			PA8InitValue init;
			init.bytes = bytes;
			unsigned long long literal = 0;
			if (ParseIntegralLiteralConstant(tokens[pos - 1].source, literal))
			{
				init.has_constant_integer = true;
				init.constant_integer = literal;
			}
			return init;
		}
		if ((AtIdentifier() || AtSimple(OP_COLON2)) &&
			type->kind == Type::TK_POINTER &&
			StripTopLevelCV(type->child)->kind != Type::TK_FUNCTION)
		{
			NameRef ref = ParseIdExpressionTarget();
			auto vit = local_variable_index.find(LocalVariableKey(ref));
			if (vit != local_variable_index.end())
			{
				const PA8Variable& target = variables[vit->second];
				TypePtr pointee = StripTopLevelCV(type->child);
				TypePtr target_type = StripTopLevelCV(target.type);
				if (target_type->kind == Type::TK_ARRAY &&
					TypeEquals(target_type->child, pointee))
				{
					PA8InitValue init;
					init.bytes = ZeroBytes(TypeSize(type));
					init.reloc_kind = PA8Variable::RK_VARIABLE;
					init.reloc_key = target.key;
					init.has_constant_address = true;
					init.constant_address_kind = PA8Variable::RK_VARIABLE;
					init.constant_address_key = target.key;
					return init;
				}
				if (target.has_constant_integer &&
					StripTopLevelCV(type)->kind == Type::TK_FUNDAMENTAL)
				{
					PA8InitValue init;
					init.bytes = EncodeIntegralBytes(type, static_cast<long long>(target.constant_integer));
					init.has_constant_integer = true;
					init.constant_integer = target.constant_integer;
					return init;
				}
			}
		}
		if ((AtIdentifier() || AtSimple(OP_COLON2)) &&
			type->kind == Type::TK_POINTER &&
			StripTopLevelCV(type->child)->kind == Type::TK_FUNCTION)
		{
			NameRef ref = ParseIdExpressionTarget();
			string key = LocalFunctionKey(ref, type->child);
			if (local_function_index.find(key) == local_function_index.end())
			{
				throw runtime_error("unsupported initializer");
			}
			PA8InitValue init;
			init.bytes = ZeroBytes(TypeSize(type));
			init.reloc_kind = PA8Variable::RK_FUNCTION;
			init.reloc_key = functions[local_function_index[key]].key;
			init.has_constant_address = true;
			init.constant_address_kind = PA8Variable::RK_FUNCTION;
			init.constant_address_key = init.reloc_key;
			return init;
		}
		if (AtIdentifier() || AtSimple(OP_COLON2))
		{
			NameRef ref = ParseIdExpressionTarget();
			auto vit = local_variable_index.find(LocalVariableKey(ref));
			if (vit != local_variable_index.end() && variables[vit->second].has_constant_integer)
			{
				const PA8Variable& target = variables[vit->second];
				PA8InitValue init;
				init.bytes = EncodeIntegralBytes(type, static_cast<long long>(target.constant_integer));
				init.has_constant_integer = true;
				init.constant_integer = target.constant_integer;
				return init;
			}
		}
		throw runtime_error("unsupported initializer");
	}

	void RecordVariable(const ParsedSpecifiers& spec, const AppliedDeclarator& applied, bool has_initializer, const PA8InitValue& init)
	{
		if (!spec.is_extern && !has_initializer)
		{
			if (TypeRequiresInitializer(applied.type))
			{
				throw runtime_error("type cannot be default initialized");
			}
			if (!TypeIsComplete(applied.type))
			{
				throw runtime_error("variable defined with incomplete type");
			}
		}

		string key = GlobalVariableKey(applied.name, spec);
		size_t size = TypeSize(applied.type);
		vector<char> bytes = has_initializer ? init.bytes : ZeroBytes(size);

		auto it = variable_index.find(key);
		if (it == variable_index.end())
		{
			PA8Variable var;
			var.key = key;
			var.type = applied.type;
			var.bytes = bytes;
			var.is_extern_only = spec.is_extern && !has_initializer;
			var.reloc_kind = has_initializer ? init.reloc_kind : PA8Variable::RK_NONE;
			var.reloc_key = has_initializer ? init.reloc_key : "";
			var.reloc_index = has_initializer ? init.reloc_index : 0;
			if (has_initializer)
			{
				if ((spec.is_constexpr || TypeIsReference(applied.type)) && init.has_constant_address)
				{
					var.has_constant_address = true;
					var.constant_address_kind = init.constant_address_kind;
					var.constant_address_key = init.constant_address_key;
					var.constant_address_index = init.constant_address_index;
				}
				if ((spec.is_constexpr || TypeIsReference(applied.type) || TypeHasTopLevelConst(applied.type)) && init.has_constant_integer)
				{
					var.has_constant_integer = true;
					var.constant_integer = init.constant_integer;
				}
			}
			variables.push_back(var);
			variable_index[key] = variables.size() - 1;
			local_variable_index[LocalVariableKey(applied.name)] = variables.size() - 1;
			order.push_back(PA8Item{false, variables.size() - 1});
		}
		else if (!spec.is_extern || has_initializer)
		{
			variables[it->second].type = applied.type;
			variables[it->second].bytes = bytes;
			variables[it->second].is_extern_only = false;
			variables[it->second].reloc_kind = has_initializer ? init.reloc_kind : PA8Variable::RK_NONE;
			variables[it->second].reloc_key = has_initializer ? init.reloc_key : "";
			variables[it->second].reloc_index = has_initializer ? init.reloc_index : 0;
			variables[it->second].has_constant_integer = false;
			variables[it->second].has_constant_address = false;
			if (has_initializer)
			{
				if ((spec.is_constexpr || TypeIsReference(applied.type)) && init.has_constant_address)
				{
					variables[it->second].has_constant_address = true;
					variables[it->second].constant_address_kind = init.constant_address_kind;
					variables[it->second].constant_address_key = init.constant_address_key;
					variables[it->second].constant_address_index = init.constant_address_index;
				}
				if ((spec.is_constexpr || TypeIsReference(applied.type) || TypeHasTopLevelConst(applied.type)) && init.has_constant_integer)
				{
					variables[it->second].has_constant_integer = true;
					variables[it->second].constant_integer = init.constant_integer;
				}
			}
		}
	}

	void RecordFunction(const ParsedSpecifiers& spec, const AppliedDeclarator& applied, bool is_definition)
	{
		string key = GlobalFunctionKey(applied.name, spec, applied.type);
		auto it = function_index.find(key);
		if (it == function_index.end())
		{
			PA8Function fn;
			fn.key = key;
			fn.type = applied.type;
			fn.defined = is_definition;
			fn.is_inline = spec.is_inline;
			functions.push_back(fn);
			function_index[key] = functions.size() - 1;
			local_function_index[LocalFunctionKey(applied.name, applied.type)] = functions.size() - 1;
			order.push_back(PA8Item{true, functions.size() - 1});
			return;
		}

		PA8Function& fn = functions[it->second];
		if (is_definition && fn.defined && !(fn.is_inline && spec.is_inline))
		{
			throw runtime_error("function " + applied.name.name + " already defined");
		}
		fn.defined = fn.defined || is_definition;
		fn.is_inline = fn.is_inline || spec.is_inline;
	}

	void ParseTopLevelDecl()
	{
		if (MatchSimple(OP_SEMICOLON))
		{
			return;
		}

		if (AtSimple(KW_INLINE) && AtSimple(KW_NAMESPACE, 1))
		{
			bool inline_namespace = MatchSimple(KW_INLINE);
			ExpectSimple(KW_NAMESPACE);
			NamespaceDecl* saved = current;
			NamespaceDecl* child = nullptr;
			if (AtSimple(OP_LBRACE))
			{
				child = CreateUnnamedNamespace(current, inline_namespace);
			}
			else
			{
				string name = ConsumeIdentifier();
				auto existing = current->named_namespaces.find(name);
				if (existing != current->named_namespaces.end() && !existing->second->inline_namespace)
				{
					throw runtime_error("extension namespace cannot be inline");
				}
				child = GetOrCreateNamedNamespace(current, name, inline_namespace);
			}
			ExpectSimple(OP_LBRACE);
			current = child;
			while (!AtSimple(OP_RBRACE))
			{
				ParseTopLevelDecl();
			}
			ExpectSimple(OP_RBRACE);
			current = saved;
			return;
		}

		if (AtSimple(KW_STATIC_ASSERT))
		{
			ExpectSimple(KW_STATIC_ASSERT);
			ExpectSimple(OP_LPAREN);
			PA8InitValue cond = ParseConstantExpressionValue();
			ExpectSimple(OP_COMMA);
			if (!AtLiteral())
			{
				throw runtime_error("unsupported static_assert");
			}
			++pos;
			ExpectSimple(OP_RPAREN);
			ExpectSimple(OP_SEMICOLON);
			bool truthy = (cond.has_constant_integer && cond.constant_integer != 0) ||
				(cond.has_constant_address &&
					(cond.constant_address_kind != PA8Variable::RK_NONE ||
					 !cond.constant_address_key.empty() ||
					 cond.constant_address_index != 0));
			if (!truthy)
			{
				throw runtime_error("static_assert on non-constant expression");
			}
			return;
		}

		if (AtSimple(KW_NAMESPACE))
		{
			if (AtIdentifier(1) && AtSimple(OP_ASS, 2))
			{
				ParseNamespaceAliasDefinition();
				return;
			}

			ExpectSimple(KW_NAMESPACE);
			NamespaceDecl* saved = current;
			NamespaceDecl* child = nullptr;
			if (AtSimple(OP_LBRACE))
			{
				child = CreateUnnamedNamespace(current, false);
			}
			else
			{
				string name = ConsumeIdentifier();
				child = GetOrCreateNamedNamespace(current, name, false);
			}
			ExpectSimple(OP_LBRACE);
			current = child;
			while (!AtSimple(OP_RBRACE))
			{
				ParseTopLevelDecl();
			}
			ExpectSimple(OP_RBRACE);
			current = saved;
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

		ParsedSpecifiers spec = ParseDeclSpecifierSeq(true, true);
		TypePtr base = BuildFundamentalType(spec);
		DeclaratorPtr declarator = ParseDeclarator(false);
		AppliedDeclarator applied = ApplyDeclarator(declarator, base);
		if (applied.name.has_name &&
			applied.name.owner &&
			applied.name.owner != current &&
			!NamespaceEncloses(current, applied.name.owner))
		{
			throw runtime_error("qualified name not from enclosed namespace");
		}
		NamespaceDecl* saved_scope = current;
		if (applied.name.has_name && applied.name.owner)
		{
			current = applied.name.owner;
		}

		if (MatchSimple(OP_LBRACE))
		{
			ExpectSimple(OP_RBRACE);
			if (applied.type->kind != Type::TK_FUNCTION)
			{
				current = saved_scope;
				throw runtime_error("expected function");
			}
			DeclareFunction(applied.name.owner, applied.name.name, applied.type);
			RecordFunction(spec, applied, true);
			current = saved_scope;
			return;
		}

		bool has_initializer = false;
		PA8InitValue init;
		if (MatchSimple(OP_ASS))
		{
			has_initializer = true;
			init = ParseInitializerValue(applied.type);
		}

		ExpectSimple(OP_SEMICOLON);
		current = saved_scope;
		if (spec.is_typedef)
		{
			DeclareTypeAlias(applied.name.owner, applied.name.name, applied.type);
			return;
		}

		if (applied.type->kind == Type::TK_FUNCTION)
		{
			DeclareFunction(applied.name.owner, applied.name.name, applied.type);
			RecordFunction(spec, applied, false);
		}
		else
		{
			DeclareVariable(applied.name.owner, applied.name.name, applied.type);
			RecordVariable(spec, applied, has_initializer, init);
		}
	}

	void ParseAll()
	{
		while (!AtEOF())
		{
			ParseTopLevelDecl();
		}
	}
};

vector<char> BuildImage(const vector<PA8Variable>& variables,
	const vector<PA8Function>& functions,
	const vector<PA8Temp>& temps,
	const vector<PA8Item>& order,
	const vector<PA8String>& strings)
{
	vector<size_t> variable_offsets(variables.size(), static_cast<size_t>(-1));
	vector<size_t> function_offsets(functions.size(), static_cast<size_t>(-1));
	vector<size_t> temp_offsets(temps.size(), static_cast<size_t>(-1));
	vector<size_t> string_offsets(strings.size(), static_cast<size_t>(-1));
	size_t size = 4;

	for (const PA8Item& item : order)
	{
		if (item.is_function)
		{
			while (size % 4)
			{
				++size;
			}
			function_offsets[item.index] = size;
			size += 4;
			continue;
		}

		const PA8Variable& var = variables[item.index];
		if (var.is_extern_only)
		{
			continue;
		}

		size_t align = TypeAlign(var.type);
		while (size % align)
		{
			++size;
		}
		variable_offsets[item.index] = size;
		size += var.bytes.size();
	}

	for (size_t i = 0; i < strings.size(); ++i)
	{
	}

	for (size_t i = 0; i < temps.size(); ++i)
	{
		const PA8Temp& temp = temps[i];
		size_t align = TypeAlign(temp.type);
		while (size % align)
		{
			++size;
		}
		temp_offsets[i] = size;
		size += temp.bytes.size();
	}

	for (size_t i = 0; i < strings.size(); ++i)
	{
		const PA8String& str = strings[i];
		size_t align = TypeAlign(str.type);
		while (size % align)
		{
			++size;
		}
		string_offsets[i] = size;
		size += str.bytes.size();
	}

	vector<char> image(size, 0);
	image[0] = 'P';
	image[1] = 'A';
	image[2] = '8';
	image[3] = 0;

	for (size_t i = 0; i < functions.size(); ++i)
	{
		if (function_offsets[i] == static_cast<size_t>(-1))
		{
			continue;
		}
		size_t off = function_offsets[i];
		image[off + 0] = 'f';
		image[off + 1] = 'u';
		image[off + 2] = 'n';
		image[off + 3] = 0;
	}

	for (size_t i = 0; i < variables.size(); ++i)
	{
		if (variable_offsets[i] == static_cast<size_t>(-1))
		{
			continue;
		}
		const PA8Variable& var = variables[i];
		size_t off = variable_offsets[i];
		copy(var.bytes.begin(), var.bytes.end(), image.begin() + off);
		if (var.reloc_kind == PA8Variable::RK_VARIABLE)
		{
			size_t target = 0;
			for (size_t j = 0; j < variables.size(); ++j)
			{
				if (variables[j].key == var.reloc_key)
				{
					target = variable_offsets[j];
					break;
				}
			}
			for (size_t b = 0; b < var.bytes.size(); ++b)
			{
				image[off + b] = static_cast<char>((static_cast<unsigned long long>(target) >> (8 * b)) & 0xFF);
			}
			continue;
		}
		if (var.reloc_kind == PA8Variable::RK_FUNCTION)
		{
			size_t target = 0;
			for (size_t j = 0; j < functions.size(); ++j)
			{
				if (functions[j].key == var.reloc_key)
				{
					target = function_offsets[j];
					break;
				}
			}
			for (size_t b = 0; b < var.bytes.size(); ++b)
			{
				image[off + b] = static_cast<char>((static_cast<unsigned long long>(target) >> (8 * b)) & 0xFF);
			}
			continue;
		}
		if (var.reloc_kind == PA8Variable::RK_TEMP)
		{
			size_t target = temp_offsets[var.reloc_index];
			for (size_t b = 0; b < var.bytes.size(); ++b)
			{
				image[off + b] = static_cast<char>((static_cast<unsigned long long>(target) >> (8 * b)) & 0xFF);
			}
		}
	}

	for (size_t i = 0; i < temps.size(); ++i)
	{
		copy(temps[i].bytes.begin(), temps[i].bytes.end(), image.begin() + temp_offsets[i]);
	}

	for (size_t i = 0; i < strings.size(); ++i)
	{
		copy(strings[i].bytes.begin(), strings[i].bytes.end(), image.begin() + string_offsets[i]);
	}

	return image;
}

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

		vector<PA8Variable> all_variables;
		vector<PA8Function> all_functions;
		vector<PA8Temp> all_temps;
		vector<PA8String> all_strings;
		vector<PA8Item> all_order;
		unordered_map<string, size_t> global_var_index;
		unordered_map<string, size_t> global_fn_index;

		for (size_t i = 0; i < nsrcfiles; ++i)
		{
			vector<RecogToken> tokens = PreprocessAndTokenize(args[i + 2]);
			PA8BootstrapParser parser(tokens, args[i + 2]);
			parser.ParseAll();

			vector<size_t> var_map(parser.variables.size(), 0);
			for (size_t j = 0; j < parser.variables.size(); ++j)
			{
				const PA8Variable& var = parser.variables[j];
				auto it = global_var_index.find(var.key);
				if (it == global_var_index.end())
				{
					all_variables.push_back(var);
					size_t idx = all_variables.size() - 1;
					global_var_index[var.key] = idx;
					var_map[j] = idx;
				}
				else
				{
					size_t idx = it->second;
					var_map[j] = idx;
					if (!var.is_extern_only)
					{
						all_variables[idx] = var;
					}
				}
			}

			vector<size_t> fn_map(parser.functions.size(), 0);
			for (size_t j = 0; j < parser.functions.size(); ++j)
			{
				const PA8Function& fn = parser.functions[j];
				auto it = global_fn_index.find(fn.key);
				if (it == global_fn_index.end())
				{
					all_functions.push_back(fn);
					size_t idx = all_functions.size() - 1;
					global_fn_index[fn.key] = idx;
					fn_map[j] = idx;
				}
				else
				{
					size_t idx = it->second;
					fn_map[j] = idx;
					if (fn.defined && all_functions[idx].defined && !(fn.is_inline && all_functions[idx].is_inline))
					{
						throw runtime_error("function already defined");
					}
					all_functions[idx].defined = all_functions[idx].defined || fn.defined;
					all_functions[idx].is_inline = all_functions[idx].is_inline || fn.is_inline;
				}
			}

			vector<size_t> temp_map(parser.temps.size(), 0);
			for (size_t j = 0; j < parser.temps.size(); ++j)
			{
				all_temps.push_back(parser.temps[j]);
				temp_map[j] = all_temps.size() - 1;
			}

			for (const PA8Item& item : parser.order)
			{
				if (item.is_function)
				{
					if (global_fn_index[parser.functions[item.index].key] == fn_map[item.index])
					{
						bool already = false;
						for (const PA8Item& existing : all_order)
						{
							if (existing.is_function && existing.index == fn_map[item.index])
							{
								already = true;
								break;
							}
						}
						if (!already)
						{
							all_order.push_back(PA8Item(true, fn_map[item.index]));
						}
					}
				}
				else
				{
					bool already = false;
					for (const PA8Item& existing : all_order)
					{
						if (!existing.is_function && existing.index == var_map[item.index])
						{
							already = true;
							break;
						}
					}
					if (!already)
					{
						all_order.push_back(PA8Item(false, var_map[item.index]));
					}
				}
			}

			for (size_t j = 0; j < parser.variables.size(); ++j)
			{
				const PA8Variable& var = parser.variables[j];
				if (var.reloc_kind == PA8Variable::RK_TEMP)
				{
					all_variables[var_map[j]].reloc_index = temp_map[var.reloc_index];
				}
			}

			all_strings.insert(all_strings.end(), parser.strings.begin(), parser.strings.end());
		}

		vector<char> image = BuildImage(all_variables, all_functions, all_temps, all_order, all_strings);
		ofstream out(outfile.c_str(), ios::binary);
		out.write(image.data(), image.size());
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
