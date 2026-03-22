struct Value
{
	bool is_unsigned;
	uint64_t bits;
};

Value MakeSigned(int64_t x)
{
	return {false, static_cast<uint64_t>(x)};
}

Value MakeUnsigned(uint64_t x)
{
	return {true, x};
}

int64_t AsSigned(Value v)
{
	return static_cast<int64_t>(v.bits);
}

uint64_t AsUnsigned(Value v)
{
	return v.bits;
}

bool ToBool(Value v)
{
	if (v.is_unsigned)
		return AsUnsigned(v) != 0;
	else
		return AsSigned(v) != 0;
}

Value ConvertTo(Value v, bool want_unsigned)
{
	if (want_unsigned)
	{
		if (v.is_unsigned) return v;
		return MakeUnsigned(static_cast<uint64_t>(AsSigned(v)));
	}
	else
	{
		if (!v.is_unsigned) return v;
		return MakeSigned(static_cast<int64_t>(AsUnsigned(v)));
	}
}

bool FundamentalTypeIsSigned(EFundamentalType t)
{
	switch (t)
	{
	case FT_BOOL:
	case FT_WCHAR_T:
	case FT_CHAR:
	case FT_SIGNED_CHAR:
	case FT_SHORT_INT:
	case FT_INT:
	case FT_LONG_INT:
	case FT_LONG_LONG_INT:
		return true;
	case FT_UNSIGNED_CHAR:
	case FT_UNSIGNED_SHORT_INT:
	case FT_UNSIGNED_INT:
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT:
	case FT_CHAR16_T:
	case FT_CHAR32_T:
		return false;
	default:
		return false;
	}
}

bool ParseIntegralPPNumber(const string& source, Value& out)
{
	string digits;
	string rest;
	int base = 10;
	bool decimal = false;
	if (!ParseIntegerCore(source, digits, rest, base, decimal))
		return false;

	if (!rest.empty() && rest[0] == '_')
		return false;

	EIntegerSuffixKind suffix;
	if (!ParseIntegerSuffix(rest, suffix))
		return false;

	unsigned __int128 value128 = 0;
	bool fits_u64 = true;
	if (!ParseUnsignedValue(digits, base, value128, fits_u64))
		return false;
	if (!fits_u64)
		return false;
	uint64_t value = static_cast<uint64_t>(value128);

	vector<EFundamentalType> candidates = IntegerCandidates(decimal, suffix);
	for (EFundamentalType t : candidates)
	{
		if (value <= IntegerTypeMax(t))
		{
			if (FundamentalTypeIsSigned(t))
			{
				out = MakeSigned(static_cast<int64_t>(value));
			}
			else
			{
				out = MakeUnsigned(value);
			}
			return true;
		}
	}
	return false;
}

bool ParseIntegralCharLiteral(const PPToken& tok, Value& out)
{
	if (tok.kind == PPTOK_USER_DEFINED_CHARACTER_LITERAL)
		return false;

	if (tok.kind != PPTOK_CHARACTER_LITERAL)
		return false;

	ParsedCharLiteral parsed = ParseCharLiteralToken(tok.source, false);
	if (!parsed.ok || parsed.is_ud)
		return false;

	if (!FundamentalTypeIsSigned(parsed.type))
	{
		out = MakeUnsigned(parsed.value);
	}
	else
	{
		out = MakeSigned(static_cast<int64_t>(parsed.value));
	}
	return true;
}

struct Node
{
	enum Kind
	{
		N_LITERAL,
		N_IDENTIFIER,
		N_DEFINED,
		N_UNARY,
		N_BINARY,
		N_TERNARY
	};

	enum UnaryOp
	{
		U_PLUS,
		U_MINUS,
		U_NOT,
		U_COMPL
	};

	enum BinaryOp
	{
		B_MUL,
		B_DIV,
		B_MOD,
		B_ADD,
		B_SUB,
		B_SHL,
		B_SHR,
		B_LT,
		B_GT,
		B_LE,
		B_GE,
		B_EQ,
		B_NE,
		B_BAND,
		B_BXOR,
		B_BOR,
		B_LAND,
		B_LOR
	};

	Kind kind;
	Value literal_value;
	string ident;
	UnaryOp unary_op;
	BinaryOp binary_op;
	unique_ptr<Node> a;
	unique_ptr<Node> b;
	unique_ptr<Node> c;
};

struct ParseResult
{
	unique_ptr<Node> node;
	bool ok;
};

struct Parser
{
	const vector<PPToken>& toks;
	size_t pos;
	bool failed;

	Parser(const vector<PPToken>& toks)
		: toks(toks), pos(0), failed(false)
	{}

	bool eof() const
	{
		return pos >= toks.size();
	}

	const PPToken* peek() const
	{
		if (eof()) return nullptr;
		return &toks[pos];
	}

	bool match_op(const string& s)
	{
		const PPToken* t = peek();
		if (!t) return false;
		if (t->kind != PPTOK_PREPROCESSING_OP_OR_PUNC) return false;
		if (t->source != s) return false;
		pos++;
		return true;
	}

	bool match_any_op(const vector<string>& ss, string* matched = nullptr)
	{
		const PPToken* t = peek();
		if (!t) return false;
		if (t->kind != PPTOK_PREPROCESSING_OP_OR_PUNC) return false;
		for (const string& s : ss)
		{
			if (t->source == s)
			{
				if (matched) *matched = s;
				pos++;
				return true;
			}
		}
		return false;
	}

	unique_ptr<Node> parse_primary()
	{
		if (match_op("("))
		{
			unique_ptr<Node> e = parse_controlling();
			if (!e) return nullptr;
			if (!match_op(")")) return fail();
			return e;
		}

		const PPToken* t = peek();
		if (!t) return fail();

		if (t->kind == PPTOK_IDENTIFIER && t->source == "defined")
		{
			pos++;
			string id;

			if (match_op("("))
			{
				const PPToken* idtok = peek();
				if (!idtok || idtok->kind != PPTOK_IDENTIFIER) return fail();
				id = idtok->source;
				pos++;
				if (!match_op(")")) return fail();
			}
			else
			{
				const PPToken* idtok = peek();
				if (!idtok || idtok->kind != PPTOK_IDENTIFIER) return fail();
				id = idtok->source;
				pos++;
			}

			unique_ptr<Node> n(new Node());
			n->kind = Node::N_DEFINED;
			n->ident = id;
			return n;
		}

		if (t->kind == PPTOK_IDENTIFIER)
		{
			unique_ptr<Node> n(new Node());
			n->kind = Node::N_IDENTIFIER;
			n->ident = t->source;
			pos++;
			return n;
		}

		Value v;
		if (t->kind == PPTOK_PP_NUMBER && ParseIntegralPPNumber(t->source, v))
		{
			unique_ptr<Node> n(new Node());
			n->kind = Node::N_LITERAL;
			n->literal_value = v;
			pos++;
			return n;
		}

		if ((t->kind == PPTOK_CHARACTER_LITERAL || t->kind == PPTOK_USER_DEFINED_CHARACTER_LITERAL) &&
			ParseIntegralCharLiteral(*t, v))
		{
			unique_ptr<Node> n(new Node());
			n->kind = Node::N_LITERAL;
			n->literal_value = v;
			pos++;
			return n;
		}

		return fail();
	}

	unique_ptr<Node> parse_unary()
	{
		if (match_op("+"))
		{
			unique_ptr<Node> x = parse_unary();
			if (!x) return nullptr;
			unique_ptr<Node> n(new Node());
			n->kind = Node::N_UNARY;
			n->unary_op = Node::U_PLUS;
			n->a = move(x);
			return n;
		}
		if (match_any_op({"-",}))
		{
			unique_ptr<Node> x = parse_unary();
			if (!x) return nullptr;
			unique_ptr<Node> n(new Node());
			n->kind = Node::N_UNARY;
			n->unary_op = Node::U_MINUS;
			n->a = move(x);
			return n;
		}
		if (match_any_op({"!", "not"}))
		{
			unique_ptr<Node> x = parse_unary();
			if (!x) return nullptr;
			unique_ptr<Node> n(new Node());
			n->kind = Node::N_UNARY;
			n->unary_op = Node::U_NOT;
			n->a = move(x);
			return n;
		}
		if (match_any_op({"~", "compl"}))
		{
			unique_ptr<Node> x = parse_unary();
			if (!x) return nullptr;
			unique_ptr<Node> n(new Node());
			n->kind = Node::N_UNARY;
			n->unary_op = Node::U_COMPL;
			n->a = move(x);
			return n;
		}

		return parse_primary();
	}

	unique_ptr<Node> make_bin(Node::BinaryOp op, unique_ptr<Node> lhs, unique_ptr<Node> rhs)
	{
		unique_ptr<Node> n(new Node());
		n->kind = Node::N_BINARY;
		n->binary_op = op;
		n->a = move(lhs);
		n->b = move(rhs);
		return n;
	}

	unique_ptr<Node> parse_multiplicative()
	{
		unique_ptr<Node> lhs = parse_unary();
		if (!lhs) return nullptr;
		while (true)
		{
			if (match_op("*"))
			{
				unique_ptr<Node> rhs = parse_unary();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_MUL, move(lhs), move(rhs));
			}
			else if (match_op("/"))
			{
				unique_ptr<Node> rhs = parse_unary();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_DIV, move(lhs), move(rhs));
			}
			else if (match_op("%"))
			{
				unique_ptr<Node> rhs = parse_unary();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_MOD, move(lhs), move(rhs));
			}
			else
			{
				break;
			}
		}
		return lhs;
	}

	unique_ptr<Node> parse_additive()
	{
		unique_ptr<Node> lhs = parse_multiplicative();
		if (!lhs) return nullptr;
		while (true)
		{
			if (match_op("+"))
			{
				unique_ptr<Node> rhs = parse_multiplicative();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_ADD, move(lhs), move(rhs));
			}
			else if (match_op("-"))
			{
				unique_ptr<Node> rhs = parse_multiplicative();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_SUB, move(lhs), move(rhs));
			}
			else
			{
				break;
			}
		}
		return lhs;
	}

	unique_ptr<Node> parse_shift()
	{
		unique_ptr<Node> lhs = parse_additive();
		if (!lhs) return nullptr;
		while (true)
		{
			if (match_op("<<"))
			{
				unique_ptr<Node> rhs = parse_additive();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_SHL, move(lhs), move(rhs));
			}
			else if (match_op(">>"))
			{
				unique_ptr<Node> rhs = parse_additive();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_SHR, move(lhs), move(rhs));
			}
			else
			{
				break;
			}
		}
		return lhs;
	}

	unique_ptr<Node> parse_relational()
	{
		unique_ptr<Node> lhs = parse_shift();
		if (!lhs) return nullptr;
		while (true)
		{
			if (match_op("<"))
			{
				unique_ptr<Node> rhs = parse_shift();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_LT, move(lhs), move(rhs));
			}
			else if (match_op(">"))
			{
				unique_ptr<Node> rhs = parse_shift();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_GT, move(lhs), move(rhs));
			}
			else if (match_op("<="))
			{
				unique_ptr<Node> rhs = parse_shift();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_LE, move(lhs), move(rhs));
			}
			else if (match_op(">="))
			{
				unique_ptr<Node> rhs = parse_shift();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_GE, move(lhs), move(rhs));
			}
			else
			{
				break;
			}
		}
		return lhs;
	}

	unique_ptr<Node> parse_equality()
	{
		unique_ptr<Node> lhs = parse_relational();
		if (!lhs) return nullptr;
		while (true)
		{
			if (match_op("=="))
			{
				unique_ptr<Node> rhs = parse_relational();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_EQ, move(lhs), move(rhs));
			}
			else if (match_any_op({"!=", "not_eq"}))
			{
				unique_ptr<Node> rhs = parse_relational();
				if (!rhs) return nullptr;
				lhs = make_bin(Node::B_NE, move(lhs), move(rhs));
			}
			else
			{
				break;
			}
		}
		return lhs;
	}

	unique_ptr<Node> parse_and()
	{
		unique_ptr<Node> lhs = parse_equality();
		if (!lhs) return nullptr;
		while (match_any_op({"&", "bitand"}))
		{
			unique_ptr<Node> rhs = parse_equality();
			if (!rhs) return nullptr;
			lhs = make_bin(Node::B_BAND, move(lhs), move(rhs));
		}
		return lhs;
	}

	unique_ptr<Node> parse_xor()
	{
		unique_ptr<Node> lhs = parse_and();
		if (!lhs) return nullptr;
		while (match_any_op({"^", "xor"}))
		{
			unique_ptr<Node> rhs = parse_and();
			if (!rhs) return nullptr;
			lhs = make_bin(Node::B_BXOR, move(lhs), move(rhs));
		}
		return lhs;
	}

	unique_ptr<Node> parse_or()
	{
		unique_ptr<Node> lhs = parse_xor();
		if (!lhs) return nullptr;
		while (match_any_op({"|", "bitor"}))
		{
			unique_ptr<Node> rhs = parse_xor();
			if (!rhs) return nullptr;
			lhs = make_bin(Node::B_BOR, move(lhs), move(rhs));
		}
		return lhs;
	}

	unique_ptr<Node> parse_logical_and()
	{
		unique_ptr<Node> lhs = parse_or();
		if (!lhs) return nullptr;
		while (match_any_op({"&&", "and"}))
		{
			unique_ptr<Node> rhs = parse_or();
			if (!rhs) return nullptr;
			lhs = make_bin(Node::B_LAND, move(lhs), move(rhs));
		}
		return lhs;
	}

	unique_ptr<Node> parse_logical_or()
	{
		unique_ptr<Node> lhs = parse_logical_and();
		if (!lhs) return nullptr;
		while (match_any_op({"||", "or"}))
		{
			unique_ptr<Node> rhs = parse_logical_and();
			if (!rhs) return nullptr;
			lhs = make_bin(Node::B_LOR, move(lhs), move(rhs));
		}
		return lhs;
	}

	unique_ptr<Node> parse_controlling()
	{
		unique_ptr<Node> cond = parse_logical_or();
		if (!cond) return nullptr;
		if (match_op("?"))
		{
			unique_ptr<Node> t = parse_controlling();
			if (!t) return nullptr;
			if (!match_op(":")) return fail();
			unique_ptr<Node> f = parse_controlling();
			if (!f) return nullptr;
			unique_ptr<Node> n(new Node());
			n->kind = Node::N_TERNARY;
			n->a = move(cond);
			n->b = move(t);
			n->c = move(f);
			return n;
		}
		return cond;
	}

	unique_ptr<Node> fail()
	{
		failed = true;
		return unique_ptr<Node>();
	}
};

bool DeducesUnsigned(const Node* n)
{
	switch (n->kind)
	{
	case Node::N_LITERAL:
		return n->literal_value.is_unsigned;
	case Node::N_IDENTIFIER:
	case Node::N_DEFINED:
		return false;
	case Node::N_UNARY:
		if (n->unary_op == Node::U_NOT) return false;
		return DeducesUnsigned(n->a.get());
	case Node::N_BINARY:
		switch (n->binary_op)
		{
		case Node::B_LT:
		case Node::B_GT:
		case Node::B_LE:
		case Node::B_GE:
		case Node::B_EQ:
		case Node::B_NE:
		case Node::B_LAND:
		case Node::B_LOR:
			return false;
		case Node::B_SHL:
		case Node::B_SHR:
			return DeducesUnsigned(n->a.get());
		default:
			return DeducesUnsigned(n->a.get()) || DeducesUnsigned(n->b.get());
		}
	case Node::N_TERNARY:
		return DeducesUnsigned(n->b.get()) || DeducesUnsigned(n->c.get());
	}
	return false;
}

struct EvalResult
{
	bool ok;
	Value value;
};

EvalResult Eval(const Node* n);

EvalResult EvalBinaryArithmetic(Node::BinaryOp op, Value a, Value b)
{
	bool u = a.is_unsigned || b.is_unsigned;
	a = ConvertTo(a, u);
	b = ConvertTo(b, u);

	if (u)
	{
		uint64_t x = AsUnsigned(a);
		uint64_t y = AsUnsigned(b);
		switch (op)
		{
		case Node::B_MUL: return {true, MakeUnsigned(x * y)};
		case Node::B_DIV: if (y == 0) return {false, MakeSigned(0)}; return {true, MakeUnsigned(x / y)};
		case Node::B_MOD: if (y == 0) return {false, MakeSigned(0)}; return {true, MakeUnsigned(x % y)};
		case Node::B_ADD: return {true, MakeUnsigned(x + y)};
		case Node::B_SUB: return {true, MakeUnsigned(x - y)};
		case Node::B_BAND: return {true, MakeUnsigned(x & y)};
		case Node::B_BXOR: return {true, MakeUnsigned(x ^ y)};
		case Node::B_BOR: return {true, MakeUnsigned(x | y)};
		default: break;
		}
	}
	else
	{
		int64_t x = AsSigned(a);
		int64_t y = AsSigned(b);
		switch (op)
		{
		case Node::B_MUL: return {true, MakeSigned(x * y)};
		case Node::B_DIV:
			if (y == 0) return {false, MakeSigned(0)};
			if (x == numeric_limits<int64_t>::min() && y == -1) return {false, MakeSigned(0)};
			return {true, MakeSigned(x / y)};
		case Node::B_MOD:
			if (y == 0) return {false, MakeSigned(0)};
			if (x == numeric_limits<int64_t>::min() && y == -1) return {false, MakeSigned(0)};
			return {true, MakeSigned(x % y)};
		case Node::B_ADD: return {true, MakeSigned(x + y)};
		case Node::B_SUB: return {true, MakeSigned(x - y)};
		case Node::B_BAND: return {true, MakeSigned(x & y)};
		case Node::B_BXOR: return {true, MakeSigned(x ^ y)};
		case Node::B_BOR: return {true, MakeSigned(x | y)};
		default: break;
		}
	}

	return {false, MakeSigned(0)};
}

EvalResult Eval(const Node* n)
{
	switch (n->kind)
	{
	case Node::N_LITERAL:
		return {true, n->literal_value};
	case Node::N_IDENTIFIER:
		if (n->ident == "true") return {true, MakeSigned(1)};
		if (n->ident == "false") return {true, MakeSigned(0)};
		return {true, MakeSigned(0)};
	case Node::N_DEFINED:
		return {true, MakeSigned(PA5IsDefinedIdentifier(n->ident) ? 1 : 0)};
	case Node::N_UNARY:
	{
		EvalResult a = Eval(n->a.get());
		if (!a.ok) return a;
		switch (n->unary_op)
		{
		case Node::U_PLUS:
			return a;
		case Node::U_MINUS:
			if (a.value.is_unsigned) return {true, MakeUnsigned(0 - AsUnsigned(a.value))};
			return {true, MakeSigned(-AsSigned(a.value))};
		case Node::U_NOT:
			return {true, MakeSigned(ToBool(a.value) ? 0 : 1)};
		case Node::U_COMPL:
			if (a.value.is_unsigned) return {true, MakeUnsigned(~AsUnsigned(a.value))};
			return {true, MakeSigned(~AsSigned(a.value))};
		}
		return {false, MakeSigned(0)};
	}
	case Node::N_BINARY:
	{
		if (n->binary_op == Node::B_LAND)
		{
			EvalResult lhs = Eval(n->a.get());
			if (!lhs.ok) return lhs;
			if (!ToBool(lhs.value)) return {true, MakeSigned(0)};
			EvalResult rhs = Eval(n->b.get());
			if (!rhs.ok) return rhs;
			return {true, MakeSigned(ToBool(rhs.value) ? 1 : 0)};
		}
		if (n->binary_op == Node::B_LOR)
		{
			EvalResult lhs = Eval(n->a.get());
			if (!lhs.ok) return lhs;
			if (ToBool(lhs.value)) return {true, MakeSigned(1)};
			EvalResult rhs = Eval(n->b.get());
			if (!rhs.ok) return rhs;
			return {true, MakeSigned(ToBool(rhs.value) ? 1 : 0)};
		}

		EvalResult lhs = Eval(n->a.get());
		if (!lhs.ok) return lhs;
		EvalResult rhs = Eval(n->b.get());
		if (!rhs.ok) return rhs;

		if (n->binary_op == Node::B_SHL || n->binary_op == Node::B_SHR)
		{
			if (rhs.value.is_unsigned)
			{
				if (AsUnsigned(rhs.value) >= 64) return {false, MakeSigned(0)};
			}
			else
			{
				int64_t r = AsSigned(rhs.value);
				if (r < 0 || r >= 64) return {false, MakeSigned(0)};
			}

			uint64_t sh = rhs.value.is_unsigned ? AsUnsigned(rhs.value) : static_cast<uint64_t>(AsSigned(rhs.value));
			if (lhs.value.is_unsigned)
			{
				uint64_t x = AsUnsigned(lhs.value);
				if (n->binary_op == Node::B_SHL) return {true, MakeUnsigned(x << sh)};
				return {true, MakeUnsigned(x >> sh)};
			}
			else
			{
				int64_t x = AsSigned(lhs.value);
				if (n->binary_op == Node::B_SHL) return {true, MakeSigned(x << sh)};
				return {true, MakeSigned(x >> sh)};
			}
		}

		if (n->binary_op == Node::B_LT || n->binary_op == Node::B_GT ||
			n->binary_op == Node::B_LE || n->binary_op == Node::B_GE ||
			n->binary_op == Node::B_EQ || n->binary_op == Node::B_NE)
		{
			bool u = lhs.value.is_unsigned || rhs.value.is_unsigned;
			Value a = ConvertTo(lhs.value, u);
			Value b = ConvertTo(rhs.value, u);
			bool result = false;
			if (u)
			{
				uint64_t x = AsUnsigned(a);
				uint64_t y = AsUnsigned(b);
				switch (n->binary_op)
				{
				case Node::B_LT: result = (x < y); break;
				case Node::B_GT: result = (x > y); break;
				case Node::B_LE: result = (x <= y); break;
				case Node::B_GE: result = (x >= y); break;
				case Node::B_EQ: result = (x == y); break;
				case Node::B_NE: result = (x != y); break;
				default: break;
				}
			}
			else
			{
				int64_t x = AsSigned(a);
				int64_t y = AsSigned(b);
				switch (n->binary_op)
				{
				case Node::B_LT: result = (x < y); break;
				case Node::B_GT: result = (x > y); break;
				case Node::B_LE: result = (x <= y); break;
				case Node::B_GE: result = (x >= y); break;
				case Node::B_EQ: result = (x == y); break;
				case Node::B_NE: result = (x != y); break;
				default: break;
				}
			}
			return {true, MakeSigned(result ? 1 : 0)};
		}

		return EvalBinaryArithmetic(n->binary_op, lhs.value, rhs.value);
	}
	case Node::N_TERNARY:
	{
		EvalResult cond = Eval(n->a.get());
		if (!cond.ok) return cond;

		bool want_unsigned = DeducesUnsigned(n->b.get()) || DeducesUnsigned(n->c.get());
		if (ToBool(cond.value))
		{
			EvalResult tv = Eval(n->b.get());
			if (!tv.ok) return tv;
			return {true, ConvertTo(tv.value, want_unsigned)};
		}
		else
		{
			EvalResult fv = Eval(n->c.get());
			if (!fv.ok) return fv;
			return {true, ConvertTo(fv.value, want_unsigned)};
		}
	}
	}

	return {false, MakeSigned(0)};
}
