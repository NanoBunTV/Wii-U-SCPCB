// bb2cpp : C++ code generator for the semantically analysed Blitz3D program (ProgNode after semant()).
//
// Every node is emitted so that the generated C++ makes the same runtime calls as the node's original
// translate() (x86 code generator) did : same string ownership (_bbStrLoad / _bbStrStore / _bbStrConcat...),
// same object reference counting (_bbObjStore...), same array / type / data layouts. See Docs/ARCHITECTURE.md.
#ifndef CPPGEN_H
#define CPPGEN_H

#include "nodes.h"

// A runtime command (from tools/bbapi.txt) : C++ function and its C++ types
struct RuntimeFunc{
	string cpp;
	string ret;					// C++ return type
	vector<string> params;		// C++ parameter types
};

class CppGen{
public:
	CppGen( Environ *runtime_env,const map<FuncType*,RuntimeFunc> &rt_funcs );

	// Writes the program to <out_dir> : bbgame.h, bbgame_data.cpp, bbgame_main.cpp, bbgame_funcs_<n>.cpp
	void generate( ProgNode *prog,const string &out_dir,int funcs_per_file );

	// Userlib (.decls) functions the program calls : C++ prototypes for bbgame.h
	set<Decl*> used_userlibs;

private:
	Environ *runtime_env;
	map<FuncType*,RuntimeFunc> rt_funcs;
	ProgNode *prog;

	// current function
	Environ *env;
	bool in_main;
	string ret_type;
	int tmp_cnt;
	ostream *out;
	int indent;

	// collected program info
	vector<StructType*> structs;
	vector<VectorType*> vectors;
	vector<pair<string,ArrayType*> > arrays;	// Dim'd arrays (name, type)
	vector<Decl*> globals;
	map<string,int> data_labels;				// label -> index in __DATA

	// names
	static string cppType( Type *t );
	static string varName( Decl *d );
	static string fieldMember( Type *t );
	string typeDescriptor( Type *t );
	static string labelName( const string &s );
	static string quote( const string &s );
	static string floatLiteral( float f );

	// expressions
	string expr( ExprNode *e );
	string callExpr( CallNode *c );
	string binaryCall( const string &func,ExprNode *l,ExprNode *r );
	string orderedArgs( const string &func,const vector<string> &args,const vector<bool> &calls );
	string binary( ExprNode *l,ExprNode *r,const string &op,const string &lhs_wrap="",const string &rhs_wrap="" );
	static bool hasCall( ExprNode *e );
	static bool varHasCall( VarNode *v );
	string lvalue( VarNode *v );
	string load( VarNode *v );
	string store( VarNode *v,const string &value );
	string compare( int op,const string &l,const string &r,Type *ty );

	// statements
	void line( const string &s );
	void stmts( StmtSeqNode *s );
	void stmt( StmtNode *s );

	// functions / program parts
	void collect( ProgNode *p );
	void collectEnv( Environ *e );
	void functionBody( Environ *e,StmtSeqNode *body,const string &header,bool is_main );
	void writeHeader( const string &path );
	void writeData( const string &path );
};

#endif
