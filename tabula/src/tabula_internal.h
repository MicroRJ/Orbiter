#ifndef TABULA_INTERNAL_H
#define TABULA_INTERNAL_H

#include "tabula/tabula.h"

#include <stdbool.h>
#include <stdalign.h>

typedef struct Tabula_Block {
	struct Tabula_Block *next;
	max_align_t alignment;
	unsigned char data[];
} Tabula_Block;

struct Tabula_Context {
	Tabula_Allocator allocator;
	Tabula_Block *blocks;
	bool out_of_memory;
};

typedef enum Tabula_NodeKind {
	TABULA_NODE_PROGRAM,
	TABULA_NODE_DECLARE,
	TABULA_NODE_ASSIGN,
	TABULA_NODE_LITERAL,
	TABULA_NODE_NAME,
	TABULA_NODE_MEMBER,
	TABULA_NODE_TABLE,
	TABULA_NODE_CALL,
} Tabula_NodeKind;

typedef struct Tabula_Node Tabula_Node;

typedef struct Tabula_NodeList {
	Tabula_Node **items;
	size_t count;
	size_t capacity;
} Tabula_NodeList;

struct Tabula_Node {
	Tabula_NodeKind kind;
	Tabula_SourceRange range;
	union {
		Tabula_NodeList list;
		struct {
			Tabula_Node *target;
			Tabula_Node *value;
		} assign;
		Tabula_Value literal;
		Tabula_String name;
		struct {
			Tabula_Node *object;
			Tabula_String name;
			Tabula_SourceRange name_range;
		} member;
		struct {
			Tabula_Node *callee;
			Tabula_NodeList arguments;
		} call;
	} as;
};

struct Tabula_Ast {
	Tabula_Context *context;
	Tabula_Source source;
	Tabula_Node *program;
};

struct Tabula_Table {
	Tabula_Context *context;
	Tabula_TableEntry *entries;
	size_t count;
	size_t capacity;
	size_t *buckets;
	size_t bucket_count;
};

typedef enum Tabula_SymbolKind {
	TABULA_SYMBOL_CONSTANT,
	TABULA_SYMBOL_FUNCTION,
} Tabula_SymbolKind;

typedef struct Tabula_Symbol {
	Tabula_String name;
	Tabula_SymbolKind kind;
	union {
		Tabula_Value constant;
		Tabula_FunctionDesc function;
	} as;
} Tabula_Symbol;

struct Tabula_Environment {
	Tabula_Context *context;
	Tabula_Symbol *symbols;
	size_t count;
	size_t capacity;
	size_t *buckets;
	size_t bucket_count;
};

typedef struct Tabula_Evaluator Tabula_Evaluator;

struct Tabula_Call {
	Tabula_Context *context;
	Tabula_Evaluator *evaluator;
	void *user_data;
	Tabula_SourceRange range;
};

#endif
