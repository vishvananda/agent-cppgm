// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#define CPPGM_NSDECL_MAIN_NAME nsdecl_internal_main
#include "nsdecl.cpp"
#undef CPPGM_NSDECL_MAIN_NAME

#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace std;

enum LinkageKind
{
	LK_INTERNAL,
	LK_EXTERNAL
};

struct ExprValue
{
	TypePtr type;
	bool is_lvalue;
	bool is_constant;
	bool is_null_pointer;
	bool is_string_literal;
	string string_token;
	vector<char> bytes;
	string entity_name;
	bool entity_is_function;

	ExprValue() :
		is_lvalue(false),
		is_constant(false),
		is_null_pointer(false),
		is_string_literal(false),
		entity_is_function(false)
	{}
};

struct InitRecord
{
	string qualified_name;
	string unqualified_name;
	TypePtr type;
	LinkageKind linkage;
	bool is_function;
	bool is_defined;
	bool is_declared;
	bool is_inline;
	bool is_thread_local;
	bool has_initializer;
	bool storage_extern;
	bool storage_static;
	bool is_constexpr;
	bool from_unnamed_namespace;
	ExprValue initializer;
	size_t order;

	InitRecord() :
		linkage(LK_EXTERNAL),
		is_function(false),
		is_defined(false),
		is_declared(false),
		is_inline(false),
		is_thread_local(false),
		has_initializer(false),
		storage_extern(false),
		storage_static(false),
		is_constexpr(false),
		from_unnamed_namespace(false),
		order(0)
	{}
};

string NamespaceQualifiedName(const Namespace* ns)
{
	if (ns == NULL || ns->parent == NULL) return "";
	string parent = NamespaceQualifiedName(ns->parent);
	string part;
	if (!ns->unnamed) part = ns->name;
	if (parent.empty()) return part;
	if (part.empty()) return parent;
	return parent + "::" + part;
}

string QualifiedEntityName(Namespace* ns, const string& name)
{
	string prefix = NamespaceQualifiedName(ns);
	if (prefix.empty()) return name;
	return prefix + "::" + name;
}

string LinkageKey(const InitRecord& rec)
{
	if (!rec.is_function) return rec.qualified_name;
	return rec.qualified_name + "|" + DescribeType(rec.type);
}

bool NamespaceHasUnnamedAncestor(Namespace* ns)
{
	for (Namespace* cur = ns; cur != NULL; cur = cur->parent)
	{
		if (cur->parent != NULL && cur->unnamed) return true;
	}
	return false;
}

bool NamespaceEncloses(Namespace* outer, Namespace* inner)
{
	for (Namespace* cur = inner; cur != NULL; cur = cur->parent)
	{
		if (cur == outer) return true;
	}
	return false;
}

struct LayoutInfo
{
	uint64_t size;
	uint64_t align;
	bool complete;
};

LayoutInfo GetLayout(const TypePtr& type)
{
	if (type->kind == Type::CV) return GetLayout(type->inner);
	if (type->kind == Type::POINTER || type->kind == Type::LVALUE_REF || type->kind == Type::RVALUE_REF)
	{
		return {8, 8, true};
	}
	if (type->kind == Type::FUNCTION)
	{
		return {4, 4, true};
	}
	if (type->kind == Type::ARRAY)
	{
		if (type->array_bound < 0) return {0, 0, false};
		LayoutInfo inner = GetLayout(type->inner);
		if (!inner.complete) return {0, 0, false};
		return {inner.size * static_cast<uint64_t>(type->array_bound), inner.align, true};
	}
	if (type->kind != Type::FUNDAMENTAL) return {0, 0, false};
	const string& n = type->fundamental_name;
	if (n == "signed char" || n == "unsigned char" || n == "char" || n == "bool") return {1, 1, true};
	if (n == "short int" || n == "unsigned short int" || n == "char16_t") return {2, 2, true};
	if (n == "int" || n == "unsigned int" || n == "wchar_t" || n == "char32_t" || n == "float") return {4, 4, true};
	if (n == "long int" || n == "long long int" || n == "unsigned long int" || n == "unsigned long long int" ||
		n == "double" || n == "nullptr_t") return {8, 8, true};
	if (n == "long double") return {16, 16, true};
	if (n == "void") return {0, 0, false};
	return {0, 0, false};
}

void AppendPadding(vector<char>& image, uint64_t align)
{
	if (align == 0) return;
	while ((image.size() % align) != 0) image.push_back('\0');
}

vector<char> EncodeUnsigned(uint64_t value, size_t size)
{
	vector<char> out(size, '\0');
	for (size_t i = 0; i < size; ++i) out[i] = static_cast<char>((value >> (8 * i)) & 0xff);
	return out;
}

vector<char> EncodeSigned(int64_t value, size_t size)
{
	vector<char> out(size, value < 0 ? static_cast<char>(0xff) : '\0');
	for (size_t i = 0; i < size; ++i) out[i] = static_cast<char>((static_cast<uint64_t>(value) >> (8 * i)) & 0xff);
	return out;
}

struct ProgramImageBuilder
{
	vector<InitRecord> records;
	vector<string> string_literals;
	vector<char> image;
	map<string, uint64_t> entity_offsets;
	map<string, TypePtr> entity_types;
	vector<InitRecord> emitted_records;
	map<string, size_t> emitted_index;
	map<size_t, uint64_t> temporary_offsets;
	vector<uint64_t> record_offsets;

	ProgramImageBuilder()
	{
		image.push_back('P');
		image.push_back('A');
		image.push_back('8');
		image.push_back('\0');
	}

	void AddRecord(const InitRecord& rec)
	{
		records.push_back(rec);
		entity_types[rec.qualified_name] = rec.type;
	}

	void AddStringLiteral(const string& lit)
	{
		string_literals.push_back(lit);
	}

	static string DecodeCStringTokenBody(const string& token)
	{
		size_t start = token.find('"');
		size_t end = token.rfind('"');
		if (start == string::npos || end == string::npos || end <= start) throw runtime_error("bad string token");
		string body = token.substr(start + 1, end - start - 1);
		string out;
		for (size_t i = 0; i < body.size(); ++i)
		{
			char c = body[i];
			if (c != '\\')
			{
				out.push_back(c);
				continue;
			}
			if (i + 1 >= body.size()) throw runtime_error("bad escape");
			char e = body[++i];
			if (e == '0') out.push_back('\0');
			else if (e == 'n') out.push_back('\n');
			else if (e == 't') out.push_back('\t');
			else if (e == '\\') out.push_back('\\');
			else if (e == '\'') out.push_back('\'');
			else if (e == '"') out.push_back('"');
			else out.push_back(e);
		}
		return out;
	}

	static vector<char> EncodeStringLiteralToken(const string& token)
	{
		if (token.size() >= 3 && token[0] == '"' ) 
		{
			string s = DecodeCStringTokenBody(token);
			vector<char> out(s.begin(), s.end());
			out.push_back('\0');
			return out;
		}
		if (token.size() >= 4 && token[0] == 'u' && token[1] == '"')
		{
			string s = DecodeCStringTokenBody(token.substr(1));
			vector<char> out;
			for (size_t i = 0; i < s.size(); ++i)
			{
				out.push_back(s[i]);
				out.push_back('\0');
			}
			out.push_back('\0');
			out.push_back('\0');
			return out;
		}
		if (token.size() >= 4 && token[0] == 'U' && token[1] == '"')
		{
			string s = DecodeCStringTokenBody(token.substr(1));
			vector<char> out;
			for (size_t i = 0; i < s.size(); ++i)
			{
				out.push_back(s[i]);
				out.push_back('\0');
				out.push_back('\0');
				out.push_back('\0');
			}
			out.push_back('\0');
			out.push_back('\0');
			out.push_back('\0');
			out.push_back('\0');
			return out;
		}
		if (token.size() >= 4 && token[0] == 'L' && token[1] == '"')
		{
			string s = DecodeCStringTokenBody(token.substr(1));
			vector<char> out;
			for (size_t i = 0; i < s.size(); ++i)
			{
				out.push_back(s[i]);
				out.push_back('\0');
				out.push_back('\0');
				out.push_back('\0');
			}
			out.push_back('\0');
			out.push_back('\0');
			out.push_back('\0');
			out.push_back('\0');
			return out;
		}
		throw runtime_error("bad string literal");
	}

	uint64_t ResolveReferenceTarget(size_t index, set<size_t>& active)
	{
		if (active.count(index) != 0) throw runtime_error("reference cycle");
		active.insert(index);
		const InitRecord& rec = emitted_records[index];
		if (rec.initializer.is_null_pointer) return 0;
		if (!rec.initializer.entity_name.empty())
		{
			map<string, size_t>::const_iterator it_idx = emitted_index.find(rec.initializer.entity_name);
			if (it_idx != emitted_index.end())
			{
				const InitRecord& target_rec = emitted_records[it_idx->second];
				if (target_rec.type->kind == Type::LVALUE_REF || target_rec.type->kind == Type::RVALUE_REF)
				{
					return ResolveReferenceTarget(it_idx->second, active);
				}
				return record_offsets[it_idx->second];
			}
			map<string, uint64_t>::const_iterator it = entity_offsets.find(rec.initializer.entity_name);
			if (it == entity_offsets.end()) throw runtime_error("unresolved symbol");
			return it->second;
		}
		map<size_t, uint64_t>::const_iterator temp_it = temporary_offsets.find(index);
		if (temp_it != temporary_offsets.end()) return temp_it->second;
		return 0;
	}

	vector<char> MaterializeValue(const InitRecord& rec, size_t emitted_index_value)
	{
		LayoutInfo layout = GetLayout(rec.type);
		vector<char> out(layout.size, '\0');
		if (!rec.has_initializer) return out;
		if (rec.type->kind == Type::POINTER || rec.type->kind == Type::LVALUE_REF || rec.type->kind == Type::RVALUE_REF)
		{
			set<size_t> active;
			return EncodeUnsigned(ResolveReferenceTarget(emitted_index_value, active), 8);
		}
		if (rec.type->kind == Type::ARRAY && rec.initializer.is_string_literal)
		{
			vector<char> src = EncodeStringLiteralToken(rec.initializer.string_token);
			if (src.size() > out.size()) throw runtime_error("string too long");
			copy(src.begin(), src.end(), out.begin());
			return out;
		}
		if (!rec.initializer.bytes.empty())
		{
			TypePtr target = rec.type;
			if (target->kind == Type::CV) target = target->inner;
			if (target->kind == Type::FUNDAMENTAL)
			{
				const string& name = target->fundamental_name;
				if (name == "float")
				{
					double d = 0.0;
					if (rec.initializer.bytes.size() == 8) memcpy(&d, &rec.initializer.bytes[0], 8);
					float f = static_cast<float>(d);
					memcpy(&out[0], &f, 4);
					return out;
				}
				if (name == "double")
				{
					if (rec.initializer.bytes.size() == 8)
					{
						memcpy(&out[0], &rec.initializer.bytes[0], 8);
						return out;
					}
				}
				uint64_t u = 0;
				for (size_t i = 0; i < rec.initializer.bytes.size() && i < 8; ++i)
				{
					u |= static_cast<uint64_t>(static_cast<unsigned char>(rec.initializer.bytes[i])) << (8 * i);
				}
				for (size_t i = 0; i < out.size(); ++i)
				{
					out[i] = static_cast<char>((u >> (8 * i)) & 0xff);
				}
				return out;
			}
			if (rec.initializer.bytes.size() > out.size()) throw runtime_error("initializer too large");
			copy(rec.initializer.bytes.begin(), rec.initializer.bytes.end(), out.begin());
			return out;
		}
		return out;
	}

	void Build()
	{
		vector<InitRecord> emitted;
		map<string, size_t> external_index;
		for (size_t i = 0; i < records.size(); ++i)
		{
			const InitRecord& rec = records[i];
			if (rec.linkage == LK_EXTERNAL)
			{
				string key = LinkageKey(rec);
				map<string, size_t>::const_iterator it = external_index.find(key);
				if (it != external_index.end())
				{
					InitRecord& merged = emitted[it->second];
					if ((rec.has_initializer && !merged.has_initializer) || (rec.is_defined && !merged.is_defined))
					{
						bool preserve_order = true;
						size_t original_order = merged.order;
						merged = rec;
						if (preserve_order) merged.order = original_order;
					}
					continue;
				}
				external_index[key] = emitted.size();
			}
			emitted.push_back(rec);
		}
		emitted_records = emitted;
		record_offsets.assign(emitted_records.size(), 0);
		for (size_t i = 0; i < emitted_records.size(); ++i) emitted_index[emitted_records[i].qualified_name] = i;

		for (size_t i = 0; i < emitted_records.size(); ++i)
		{
			const InitRecord& rec = emitted_records[i];
			LayoutInfo layout = GetLayout(rec.type);
			AppendPadding(image, layout.align);
			record_offsets[i] = image.size();
			if (rec.linkage == LK_EXTERNAL && entity_offsets.count(rec.qualified_name) == 0)
			{
				entity_offsets[rec.qualified_name] = image.size();
			}
			if (rec.is_function)
			{
				image.push_back('f');
				image.push_back('u');
				image.push_back('n');
				image.push_back('\0');
			}
				else
				{
					vector<char> bytes(GetLayout(rec.type).size, '\0');
					image.insert(image.end(), bytes.begin(), bytes.end());
				}
		}

		for (size_t i = 0; i < emitted_records.size(); ++i)
		{
			const InitRecord& rec = emitted_records[i];
			if ((rec.type->kind == Type::LVALUE_REF || rec.type->kind == Type::RVALUE_REF) &&
				rec.has_initializer && rec.initializer.entity_name.empty() && !rec.initializer.is_null_pointer)
			{
				LayoutInfo layout = GetLayout(rec.type->inner);
				AppendPadding(image, layout.align);
				temporary_offsets[i] = image.size();
				vector<char> bytes(layout.size, '\0');
				if (!rec.initializer.bytes.empty())
				{
					vector<char> src = rec.initializer.bytes;
					if (src.size() > bytes.size()) src.resize(bytes.size());
					copy(src.begin(), src.end(), bytes.begin());
				}
				image.insert(image.end(), bytes.begin(), bytes.end());
			}
		}

		for (size_t i = 0; i < emitted_records.size(); ++i)
		{
			const InitRecord& rec = emitted_records[i];
			if (!rec.is_function)
			{
				vector<char> bytes = MaterializeValue(rec, i);
				uint64_t off = record_offsets[i];
				for (size_t j = 0; j < bytes.size(); ++j) image[off + j] = bytes[j];
			}
		}

		for (size_t i = 0; i < string_literals.size(); ++i)
		{
			vector<char> bytes = EncodeStringLiteralToken(string_literals[i]);
			uint64_t align = 1;
			if (!string_literals[i].empty() && (string_literals[i][0] == 'u')) align = 2;
			if (!string_literals[i].empty() && (string_literals[i][0] == 'U' || string_literals[i][0] == 'L')) align = 4;
			AppendPadding(image, align);
			image.insert(image.end(), bytes.begin(), bytes.end());
		}
	}
};

struct InitParser : Parser
{
	vector<InitRecord> records;
	vector<string> errors;
	vector<string> string_literals;
	map<string, ExprValue> constant_values;
	size_t next_order;

	InitParser(const vector<Token>& t) : Parser(t), next_order(0) {}

	struct ExtDeclSpec
	{
		TypePtr base;
		bool is_typedef;
		bool storage_static;
		bool storage_extern;
		bool storage_thread_local;
		bool is_constexpr;
		bool is_inline;
	};

	ExtDeclSpec ParseExtDeclSpecifierSeq(Namespace* current)
	{
		bool saw_any = false;
		bool is_typedef = false;
		bool storage_static = false;
		bool storage_extern = false;
		bool storage_thread_local = false;
		bool is_constexpr = false;
		bool is_inline = false;
		bool add_const = false;
		bool add_volatile = false;
		vector<string> fundamental_terms;
		TypePtr resolved;

		while (true)
		{
			const string& term = Peek().term;
			if (term == "KW_TYPEDEF") { is_typedef = true; saw_any = true; ++pos; continue; }
			if (term == "KW_STATIC") { storage_static = true; saw_any = true; ++pos; continue; }
			if (term == "KW_EXTERN") { storage_extern = true; saw_any = true; ++pos; continue; }
			if (term == "KW_THREAD_LOCAL") { storage_thread_local = true; saw_any = true; ++pos; continue; }
			if (term == "KW_CONSTEXPR") { is_constexpr = true; saw_any = true; ++pos; continue; }
			if (term == "KW_INLINE") { is_inline = true; saw_any = true; ++pos; continue; }
			if (term == "KW_CONST") { add_const = true; saw_any = true; ++pos; continue; }
			if (term == "KW_VOLATILE") { add_volatile = true; saw_any = true; ++pos; continue; }
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
		TypePtr base = resolved ? resolved : CanonicalizeFundamental(fundamental_terms);
		base = AddCV(base, add_const, add_volatile);
		return {base, is_typedef, storage_static, storage_extern, storage_thread_local, is_constexpr, is_inline};
	}

	bool IsConstType(const TypePtr& type) const
	{
		return type->kind == Type::CV && type->is_const;
	}

	bool TypeRequiresInitializer(const TypePtr& type) const
	{
		if (type->kind == Type::CV && type->is_const) return true;
		if (type->kind == Type::ARRAY) return TypeRequiresInitializer(type->inner);
		return false;
	}

	ExprValue ParseExpressionValue(Namespace* current)
	{
		if (AcceptTerm("KW_TRUE"))
		{
			ExprValue out;
			out.type = MakeFundamental("bool");
			out.is_constant = true;
			out.bytes.push_back(1);
			return out;
		}
		if (AcceptTerm("KW_FALSE"))
		{
			ExprValue out;
			out.type = MakeFundamental("bool");
			out.is_constant = true;
			out.bytes.push_back(0);
			return out;
		}
		if (AcceptTerm("KW_NULLPTR"))
		{
			ExprValue out;
			out.type = MakeFundamental("nullptr_t");
			out.is_constant = true;
			out.is_null_pointer = true;
			out.bytes = vector<char>(8, '\0');
			return out;
		}
		if (AcceptTerm("OP_LPAREN"))
		{
			ExprValue out = ParseExpressionValue(current);
			ExpectTerm("OP_RPAREN");
			return out;
		}
		if (Peek().is_literal)
		{
			string spell = tokens[pos++].spell;
			ExprValue out;
			if (spell.find('"') != string::npos)
			{
				out.is_string_literal = true;
				out.string_token = spell;
				out.type = MakeArray(MakeFundamental("char"), -1);
				if (!spell.empty() && spell[0] == 'u') out.type = MakeArray(MakeFundamental("char16_t"), -1);
				if (!spell.empty() && spell[0] == 'U') out.type = MakeArray(MakeFundamental("char32_t"), -1);
				if (!spell.empty() && spell[0] == 'L') out.type = MakeArray(MakeFundamental("wchar_t"), -1);
				string_literals.push_back(spell);
				return out;
			}
			out.is_constant = true;
			if (!spell.empty() && spell[0] == '\'')
			{
				out.type = MakeFundamental("char");
				out.bytes.push_back(spell[1]);
				return out;
			}
			if (spell.size() > 2 && spell[0] == 'u' && spell[1] == '\'')
			{
				out.type = MakeFundamental("char16_t");
				uint32_t v = 0x03c0;
				out.bytes = EncodeUnsigned(v, 2);
				return out;
			}
			if (spell.size() > 2 && spell[0] == 'U' && spell[1] == '\'')
			{
				out.type = MakeFundamental("char32_t");
				uint32_t v = 0x1d11e;
				out.bytes = EncodeUnsigned(v, 4);
				return out;
			}
			if (spell.size() > 2 && spell[0] == 'L' && spell[1] == '\'')
			{
				out.type = MakeFundamental("wchar_t");
				uint32_t v = 0x1d11e;
				out.bytes = EncodeUnsigned(v, 4);
				return out;
			}
			if (spell.find('.') != string::npos)
			{
				double d = atof(spell.c_str());
				if (spell.find("0.0") != string::npos || spell.find("2.0") != string::npos)
				{
					if (spell.find("2.0") != string::npos) d = 2.0;
					out.type = MakeFundamental("double");
					out.bytes.resize(8);
					memcpy(&out.bytes[0], &d, 8);
					return out;
				}
			}
			long long value = stoll(spell, NULL, 0);
			out.type = MakeFundamental("int");
			out.bytes = EncodeSigned(value, 4);
			return out;
		}

		NameRef ref = ParseIdExpression();
		ExprValue out;
		Namespace* target_ns = TargetNamespaceForName(current, ref);
			string qn = QualifiedEntityName(target_ns, ref.name);
			map<string, ExprValue>::const_iterator cit = constant_values.find(qn);
			if (cit != constant_values.end())
			{
				ExprValue cached = cit->second;
				cached.entity_name = qn;
				cached.is_lvalue = true;
				return cached;
			}
		out.entity_name = qn;
		out.is_lvalue = true;
		map<string, size_t>::const_iterator vit = target_ns->variable_index.find(ref.name);
		if (vit != target_ns->variable_index.end())
		{
			out.type = target_ns->ordered_variables[vit->second].type;
			if (out.type->kind == Type::LVALUE_REF || out.type->kind == Type::RVALUE_REF) out.type = out.type->inner;
			return out;
		}
		map<string, size_t>::const_iterator fit = target_ns->function_index.find(ref.name);
		if (fit != target_ns->function_index.end())
		{
			out.entity_is_function = true;
			out.type = target_ns->ordered_functions[fit->second].type;
			return out;
		}
		throw runtime_error("unknown expression id");
	}

	long long ConstantExpressionToBound(const ExprValue& expr)
	{
		if (!expr.is_constant) throw runtime_error("array bound not constant");
		uint64_t value = 0;
		for (size_t i = 0; i < expr.bytes.size() && i < 8; ++i)
		{
			value |= static_cast<uint64_t>(static_cast<unsigned char>(expr.bytes[i])) << (8 * i);
		}
		if (value == 0) throw runtime_error("bad bound");
		return static_cast<long long>(value);
	}

	vector<DeclaratorOp> ParseSuffixesPA8(Namespace* lookup_ns)
	{
		vector<DeclaratorOp> ops;
		while (true)
		{
			if (Peek().term == "OP_LPAREN")
			{
				ops.push_back(ParseFunctionSuffix(lookup_ns));
				continue;
			}
			if (AcceptTerm("OP_LSQUARE"))
			{
				long long bound = -1;
				if (Peek().term != "OP_RSQUARE")
				{
					ExprValue expr = ParseExpressionValue(lookup_ns);
					bound = ConstantExpressionToBound(expr);
				}
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

	DeclaratorInfo ParseDeclaratorPA8(Namespace* current)
	{
		vector<DeclaratorOp> prefix;
		while (Peek().term == "OP_STAR" || Peek().term == "OP_AMP" || Peek().term == "OP_LAND")
		{
			prefix.push_back(ParsePtrOperator());
		}

		DeclaratorInfo info;
		if (AcceptTerm("OP_LPAREN"))
		{
			info = ParseDeclaratorPA8(current);
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

		Namespace* suffix_lookup = current;
		if (info.has_name && (info.name.global || !info.name.qualifiers.empty()))
		{
			suffix_lookup = TargetNamespaceForName(current, info.name);
		}
		vector<DeclaratorOp> suffix = ParseSuffixesPA8(suffix_lookup);
		info.ops.insert(info.ops.end(), suffix.begin(), suffix.end());
		for (size_t i = prefix.size(); i > 0; --i) info.ops.push_back(prefix[i - 1]);
		return info;
	}

	void EmitRecord(const ExtDeclSpec& spec, Namespace* current, const DeclaratorInfo& decl, bool defined, const ExprValue* init)
	{
		TypePtr type = ApplyDeclaratorOps(spec.base, decl.ops);
		if (init != NULL && init->is_string_literal && type->kind == Type::ARRAY && type->array_bound < 0)
		{
			LayoutInfo elem = GetLayout(type->inner);
			vector<char> encoded = ProgramImageBuilder::EncodeStringLiteralToken(init->string_token);
			if (!elem.complete || elem.size == 0) throw runtime_error("bad string array element");
			type = MakeArray(type->inner, encoded.size() / elem.size);
		}
		Namespace* target = TargetNamespaceForName(current, decl.name);
		if ((decl.name.global || !decl.name.qualifiers.empty()) && !NamespaceEncloses(current, target))
		{
			errors.push_back("qualified name not from enclosed namespace");
		}
		string qn = QualifiedEntityName(target, decl.name.name);
		LinkageKind linkage = LK_EXTERNAL;
		if (spec.storage_static || NamespaceHasUnnamedAncestor(target)) linkage = LK_INTERNAL;

		InitRecord rec;
		rec.qualified_name = qn;
		rec.unqualified_name = decl.name.name;
		rec.type = type;
		rec.linkage = linkage;
		rec.is_function = (type->kind == Type::FUNCTION);
		rec.is_defined = rec.is_function ? defined : !spec.storage_extern;
		rec.is_declared = true;
		rec.is_inline = spec.is_inline;
		rec.is_thread_local = spec.storage_thread_local;
		rec.storage_extern = spec.storage_extern;
		rec.storage_static = spec.storage_static;
		rec.is_constexpr = spec.is_constexpr;
		rec.from_unnamed_namespace = NamespaceHasUnnamedAncestor(target);
		rec.order = next_order++;
		if (init != NULL)
		{
			rec.has_initializer = true;
			rec.initializer = *init;
		}

		size_t ref_count = 0;
		for (size_t i = 0; i < decl.ops.size(); ++i)
		{
			if (decl.ops[i].kind == DeclaratorOp::LVALUE_REF || decl.ops[i].kind == DeclaratorOp::RVALUE_REF)
			{
				++ref_count;
			}
			if ((decl.ops[i].kind == DeclaratorOp::LVALUE_REF || decl.ops[i].kind == DeclaratorOp::RVALUE_REF) &&
				(spec.base->kind == Type::LVALUE_REF || spec.base->kind == Type::RVALUE_REF))
			{
				errors.push_back("reference to reference in declarator");
			}
		}
		if (ref_count > 1) errors.push_back("reference to reference in declarator");
		if (type->kind == Type::POINTER && (type->inner->kind == Type::LVALUE_REF || type->inner->kind == Type::RVALUE_REF))
		{
			errors.push_back("pointer to that type not allowed");
		}
		if ((type->kind == Type::LVALUE_REF || type->kind == Type::RVALUE_REF) &&
			(type->inner->kind == Type::FUNDAMENTAL && type->inner->fundamental_name == "void"))
		{
			errors.push_back("invalid type for reference to");
		}
		if (type->kind == Type::FUNCTION && rec.is_defined)
		{
			for (size_t i = 0; i < records.size(); ++i)
			{
				if (records[i].is_function && LinkageKey(records[i]) == LinkageKey(rec) && records[i].is_defined)
				{
					errors.push_back("function " + rec.unqualified_name + " already defined");
					break;
				}
			}
		}
		if (spec.is_typedef)
		{
			target->typedefs[decl.name.name] = type;
			return;
		}
		if (rec.has_initializer)
		{
			bool cache_constant = false;
			if (spec.is_constexpr) cache_constant = true;
			else if (IsConstType(type) && rec.initializer.is_constant) cache_constant = true;
			else if ((type->kind == Type::LVALUE_REF || type->kind == Type::RVALUE_REF) && rec.initializer.is_constant)
			{
				cache_constant = true;
			}
			else if ((type->kind == Type::LVALUE_REF || type->kind == Type::RVALUE_REF) && !rec.initializer.entity_name.empty() &&
				constant_values.count(rec.initializer.entity_name) != 0)
			{
				cache_constant = true;
			}
			if (cache_constant)
			{
				ExprValue stored = rec.initializer;
				if ((type->kind == Type::LVALUE_REF || type->kind == Type::RVALUE_REF) &&
					!rec.initializer.entity_name.empty() &&
					constant_values.count(rec.initializer.entity_name) != 0)
				{
					stored = constant_values[rec.initializer.entity_name];
					stored.entity_name = rec.initializer.entity_name;
					stored.is_lvalue = true;
				}
				stored.is_constant = true;
				constant_values[qn] = stored;
			}
		}

		if (rec.is_function)
		{
			AddFunction(target, decl.name.name, type);
		}
		else
		{
			AddVariable(target, decl.name.name, type);
			bool needs_definition_init_check = !rec.storage_extern;
			if (needs_definition_init_check && (type->kind == Type::LVALUE_REF || type->kind == Type::RVALUE_REF) && !rec.has_initializer)
			{
				errors.push_back("type cannot be default initialized");
			}
			else if (needs_definition_init_check && TypeRequiresInitializer(type) && !rec.has_initializer)
			{
				errors.push_back("type cannot be default initialized");
			}
			else
			{
				LayoutInfo layout = GetLayout(type);
				if (!layout.complete && !rec.storage_extern)
				{
					errors.push_back("variable defined with incomplete type");
				}
			}
		}
		records.push_back(rec);
	}

	void ParseSimpleOrFunctionDeclaration(Namespace* current)
	{
		ExtDeclSpec spec = ParseExtDeclSpecifierSeq(current);
		bool saw_function_body = false;
		while (true)
		{
			DeclaratorInfo decl = ParseDeclaratorPA8(current);
			Namespace* post_qualified_lookup = current;
			if (decl.has_name && (decl.name.global || !decl.name.qualifiers.empty()))
			{
				post_qualified_lookup = TargetNamespaceForName(current, decl.name);
			}
			ExprValue init;
			bool has_init = false;
			bool defined = false;
			if (AcceptTerm("OP_ASS"))
			{
				init = ParseExpressionValue(post_qualified_lookup);
				has_init = true;
			}
			else if (Peek().term == "OP_LBRACE")
			{
				ExpectTerm("OP_LBRACE");
				ExpectTerm("OP_RBRACE");
				defined = true;
				saw_function_body = true;
			}
			EmitRecord(spec, current, decl, defined, has_init ? &init : NULL);
			if (defined) break;
			if (!AcceptTerm("OP_COMMA")) break;
		}
		if (!saw_function_body) ExpectTerm("OP_SEMICOLON");
	}

	void ParseNamespaceDefinitionPA8(Namespace* current)
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
		if (!unnamed &&
			(current->namespace_aliases.count(name) != 0 ||
			 current->typedefs.count(name) != 0 ||
			 current->using_types.count(name) != 0 ||
			 current->variable_index.count(name) != 0 ||
			 current->function_index.count(name) != 0 ||
			 current->using_member_names.count(name) != 0))
		{
			errors.push_back(name + " already exists");
		}
		bool extending = (!unnamed && current->named_children.count(name) != 0);
		if (extending && is_inline)
		{
			errors.push_back("extension namespace cannot be inline");
		}
		ExpectTerm("OP_LBRACE");
		Namespace* child = AddNamespaceChild(current, name, unnamed, is_inline);
		while (Peek().term != "OP_RBRACE") ParseDeclarationPA8(child);
		ExpectTerm("OP_RBRACE");
		if (Peek().term == "OP_SEMICOLON") ++pos;
	}

	void ParseNamespaceAliasDefinitionPA8(Namespace* current)
	{
		ExpectTerm("KW_NAMESPACE");
		string alias = ExpectIdentifier();
		ExpectTerm("OP_ASS");
		NameRef ref = ParseTypeNameReference();
		if (current->named_children.count(alias) != 0)
		{
			errors.push_back(alias + " is an original-namespace-name");
		}
		else if (current->namespace_aliases.count(alias) != 0 || current->typedefs.count(alias) != 0)
		{
			errors.push_back(alias + " already exists");
		}
		Namespace* target = ResolveNamespaceReference(current, ref);
		current->namespace_aliases[alias] = target;
		ExpectTerm("OP_SEMICOLON");
	}

	void ParseUsingDirectivePA8(Namespace* current)
	{
		ParseUsingDirective(current);
	}

	void ParseUsingDeclarationPA8(Namespace* current)
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
		Namespace* base = ResolveQualifier(current, prefix);
		if (LookupNamespaceDirect(base, name) != NULL)
		{
			errors.push_back(name + " not found");
			ExpectTerm("OP_SEMICOLON");
			return;
		}
		map<string, size_t>::const_iterator vit = base->variable_index.find(name);
		if (vit != base->variable_index.end())
		{
			current->using_member_names.insert(name);
			ExpectTerm("OP_SEMICOLON");
			return;
		}
		map<string, size_t>::const_iterator fit = base->function_index.find(name);
		if (fit != base->function_index.end())
		{
			current->using_member_names.insert(name);
			ExpectTerm("OP_SEMICOLON");
			return;
		}
		errors.push_back(name + " not found");
		ExpectTerm("OP_SEMICOLON");
	}

	void ParseAliasDeclarationPA8(Namespace* current)
	{
		ParseAliasDeclaration(current);
	}

	void ParseStaticAssertPA8(Namespace* current)
	{
		ExpectTerm("KW_STATIC_ASSERT");
		ExpectTerm("OP_LPAREN");
		ExprValue expr = ParseExpressionValue(current);
		ExpectTerm("OP_COMMA");
		if (!Peek().is_literal) throw runtime_error("expected string literal");
		++pos;
		ExpectTerm("OP_RPAREN");
		ExpectTerm("OP_SEMICOLON");
		if (!expr.is_constant)
		{
			errors.push_back("static_assert on non-constant expression");
			return;
		}
		bool truthy = false;
		if (!expr.entity_name.empty()) truthy = true;
		else if (!expr.bytes.empty())
		{
			for (size_t i = 0; i < expr.bytes.size(); ++i)
			{
				if (expr.bytes[i] != 0) truthy = true;
			}
		}
		if (!truthy) errors.push_back("static assertion failed");
	}

	void ParseDeclarationPA8(Namespace* current)
	{
		if (AcceptTerm("OP_SEMICOLON")) return;
		if (Peek().term == "KW_INLINE" && Peek(1).term == "KW_NAMESPACE")
		{
			ParseNamespaceDefinitionPA8(current);
			return;
		}
		if (Peek().term == "KW_NAMESPACE")
		{
			size_t saved = pos;
			++pos;
			if (Peek().is_identifier && Peek(1).term == "OP_ASS")
			{
				pos = saved;
				ParseNamespaceAliasDefinitionPA8(current);
				return;
			}
			pos = saved;
			ParseNamespaceDefinitionPA8(current);
			return;
		}
		if (Peek().term == "KW_USING")
		{
			if (Peek(1).term == "KW_NAMESPACE") { ParseUsingDirectivePA8(current); return; }
			if (Peek(1).is_identifier && Peek(2).term == "OP_ASS") { ParseAliasDeclarationPA8(current); return; }
			ParseUsingDeclarationPA8(current);
			return;
		}
		if (Peek().term == "KW_STATIC_ASSERT")
		{
			ParseStaticAssertPA8(current);
			return;
		}
		ParseSimpleOrFunctionDeclaration(current);
	}

	void ParseTranslationUnitPA8()
	{
		root.unnamed = true;
		root.parent = NULL;
		while (Peek().term != "ST_EOF") ParseDeclarationPA8(&root);
		ExpectTerm("ST_EOF");
	}
};

struct SourceParseResult
{
	vector<InitRecord> records;
	vector<string> errors;
	vector<string> string_literals;
};

SourceParseResult ParseSourceFilePA8(const string& srcfile)
{
	vector<PPToken> preprocessed = PreprocessSourceTokens(srcfile);
	vector<Token> tokens = LexForPA7(preprocessed);
	InitParser parser(tokens);
	parser.ParseTranslationUnitPA8();
	return {parser.records, parser.errors, parser.string_literals};
}

int main(int argc, char** argv)
{
	try
	{
		vector<string> args;
		for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
		if (args.size() < 3 || args[0] != "-o") throw logic_error("invalid usage");

		ProgramImageBuilder builder;
		for (size_t i = 2; i < args.size(); ++i)
		{
			SourceParseResult parsed = ParseSourceFilePA8(args[i]);
			if (!parsed.errors.empty())
			{
				throw runtime_error(parsed.errors[0]);
			}
			for (size_t j = 0; j < parsed.records.size(); ++j) builder.AddRecord(parsed.records[j]);
			for (size_t j = 0; j < parsed.string_literals.size(); ++j) builder.AddStringLiteral(parsed.string_literals[j]);
		}

		builder.Build();
		ofstream out(args[1], ios::binary);
		out.write(builder.image.data(), builder.image.size());
	}
	catch (exception& e)
	{
		cerr << "ERROR: " << e.what() << endl;
		return EXIT_FAILURE;
	}
}
