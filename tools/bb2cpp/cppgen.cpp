#include "std.h"
#include "cppgen.h"

#include <sstream>
#include <cstdio>

//////////////////////////////
// names, types and literals //
//////////////////////////////

string CppGen::cppType( Type *t ){
	if( ConstType *c=t->constType() ) t=c->valueType;
	if( t==Type::int_type ) return "int";
	if( t==Type::float_type ) return "float";
	if( t==Type::string_type ) return "BBStr*";
	if( t==Type::void_type ) return "void";
	if( t->structType() ) return "BBObj*";
	if( t->vectorType() ) return "void*";
	return "int";
}

static string ident( const string &s ){
	string t;
	for( size_t k=0;k<s.size();++k ){
		char c=s[k];
		t+=(isalnum( (unsigned char)c ) || c=='_') ? c : '_';
	}
	return t;
}

string CppGen::varName( Decl *d ){
	if( d->kind & DECL_GLOBAL ) return "v_"+ident( d->name );
	if( d->kind & DECL_ARRAY ) return "a_"+ident( d->name );
	return "l_"+ident( d->name );
}

string CppGen::fieldMember( Type *t ){
	if( ConstType *c=t->constType() ) t=c->valueType;
	if( t==Type::int_type ) return "INT";
	if( t==Type::float_type ) return "FLT";
	if( t==Type::string_type ) return "STR";
	if( t->structType() ) return "OBJ";
	if( t->vectorType() ) return "VEC";
	return "INT";
}

string CppGen::typeDescriptor( Type *t ){
	if( t==Type::int_type ) return "&_bbIntType";
	if( t==Type::float_type ) return "&_bbFltType";
	if( t==Type::string_type ) return "&_bbStrType";
	if( StructType *s=t->structType() ) return "(BBType*)&t_"+ident( s->ident );
	if( VectorType *v=t->vectorType() ) return "(BBType*)&vt"+ident( v->label );
	return "&_bbIntType";
}

string CppGen::labelName( const string &s ){
	return "L"+ident( s );
}

string CppGen::quote( const string &s ){
	string t="\"";
	for( size_t k=0;k<s.size();++k ){
		unsigned char c=s[k];
		if( c=='\\' ) t+="\\\\";
		else if( c=='"' ) t+="\\\"";
		else if( c=='?' ) t+="\\?";		//no trigraphs
		else if( c>=32 && c<127 ) t+=c;
		else{
			char buf[8];
			sprintf( buf,"\\%03o",c );
			t+=buf;
		}
	}
	return t+"\"";
}

string CppGen::floatLiteral( float f ){
	char buf[64];
	if( f!=f ) return "bbNaN()";
	if( f==1.0f/0.0f ) return "bbInf()";
	if( f==-1.0f/0.0f ) return "(-bbInf())";
	sprintf( buf,"%af",f );		//exact hex float
	return buf;
}

////////////
// set up //
////////////

CppGen::CppGen( Environ *re,const map<FuncType*,RuntimeFunc> &rf ):
runtime_env(re),rt_funcs(rf),prog(0),env(0),in_main(false),tmp_cnt(0),out(0),indent(0){
}

void CppGen::line( const string &s ){
	for( int k=0;k<indent;++k ) *out<<'\t';
	*out<<s<<'\n';
}

/////////////////
// expressions //
/////////////////

bool CppGen::hasCall( ExprNode *e ){
	if( !e ) return false;
	if( dynamic_cast<CallNode*>(e) ) return true;
	if( dynamic_cast<ConstNode*>(e) || dynamic_cast<NullNode*>(e) ) return false;
	if( CastNode *c=dynamic_cast<CastNode*>(e) ) return hasCall( c->expr );
	if( VarExprNode *v=dynamic_cast<VarExprNode*>(e) ) return varHasCall( v->var );
	if( UniExprNode *u=dynamic_cast<UniExprNode*>(e) ) return hasCall( u->expr );
	if( BinExprNode *b=dynamic_cast<BinExprNode*>(e) ) return hasCall( b->lhs ) || hasCall( b->rhs );
	if( ArithExprNode *a=dynamic_cast<ArithExprNode*>(e) ) return hasCall( a->lhs ) || hasCall( a->rhs );
	if( RelExprNode *r=dynamic_cast<RelExprNode*>(e) ) return hasCall( r->lhs ) || hasCall( r->rhs );
	if( AfterNode *a=dynamic_cast<AfterNode*>(e) ) return hasCall( a->expr );
	if( BeforeNode *b=dynamic_cast<BeforeNode*>(e) ) return hasCall( b->expr );
	if( ObjectCastNode *o=dynamic_cast<ObjectCastNode*>(e) ) return hasCall( o->expr );
	if( ObjectHandleNode *o=dynamic_cast<ObjectHandleNode*>(e) ) return hasCall( o->expr );
	return true;	//New, First, Last : runtime calls
}

bool CppGen::varHasCall( VarNode *v ){
	if( ArrayVarNode *a=dynamic_cast<ArrayVarNode*>(v) ){
		for( size_t k=0;k<a->exprs->exprs.size();++k ) if( hasCall( a->exprs->exprs[k] ) ) return true;
		return false;
	}
	if( FieldVarNode *f=dynamic_cast<FieldVarNode*>(v) ) return hasCall( f->expr );
	if( VectorVarNode *vv=dynamic_cast<VectorVarNode*>(v) ){
		if( hasCall( vv->expr ) ) return true;
		for( size_t k=0;k<vv->exprs->exprs.size();++k ) if( hasCall( vv->exprs->exprs[k] ) ) return true;
		return false;
	}
	return false;
}

// Evaluates the operands left to right (C++ leaves the order unspecified) when both have side effects
string CppGen::binary( ExprNode *l,ExprNode *r,const string &op,const string &lw,const string &rw ){
	string a=expr( l ),b=expr( r );
	if( hasCall( l ) && hasCall( r ) ){
		int n=++tmp_cnt;
		string ta="_t"+itoa(n)+"a",tb="_t"+itoa(n)+"b";
		return "({ auto "+ta+"="+a+"; auto "+tb+"="+b+"; "+lw+"("+ta+")"+op+rw+"("+tb+"); })";
	}
	return "("+lw+"("+a+")"+op+rw+"("+b+"))";
}

// func( l,r ) with the operands evaluated left to right
string CppGen::binaryCall( const string &func,ExprNode *l,ExprNode *r ){
	vector<string> args;
	vector<bool> calls;
	args.push_back( expr( l ) );calls.push_back( hasCall( l ) );
	args.push_back( expr( r ) );calls.push_back( hasCall( r ) );
	return orderedArgs( func,args,calls );
}

string CppGen::orderedArgs( const string &func,const vector<string> &args,const vector<bool> &calls ){
	int n=0;
	for( size_t k=0;k<calls.size();++k ) if( calls[k] ) ++n;
	if( n<2 ){
		string t=func+"(";
		for( size_t k=0;k<args.size();++k ) t+=(k ? "," : "")+args[k];
		return t+")";
	}
	//left to right : every argument up to the last one with a call goes into a temporary
	int id=++tmp_cnt;
	size_t last=0;
	for( size_t k=0;k<calls.size();++k ) if( calls[k] ) last=k;
	string pre="({ ",call=func+"(";
	for( size_t k=0;k<args.size();++k ){
		if( k<=last ){
			string tn="_a"+itoa(id)+"_"+itoa(k);
			pre+="auto "+tn+"="+args[k]+"; ";
			call+=(k ? "," : "")+tn;
		}else{
			call+=(k ? "," : "")+args[k];
		}
	}
	return pre+call+"); })";
}

string CppGen::compare( int op,const string &l,const string &r,Type *ty ){
	string o;
	switch( op ){
	case '=':o="==";break;case NE:o="!=";break;
	case '<':o="<";break;case '>':o=">";break;
	case LE:o="<=";break;case GE:o=">=";break;
	}
	if( ty==Type::string_type ) return "(_bbStrCompare("+l+","+r+")"+o+"0)";
	if( ty->structType() ) return "(_bbObjCompare("+l+","+r+")"+o+"0)";
	return "(("+l+")"+o+"("+r+"))";
}

string CppGen::callExpr( CallNode *c ){
	FuncType *f=c->sem_decl->type->funcType();
	vector<ExprNode*> &ex=c->exprs->exprs;
	vector<string> args;
	vector<bool> calls;

	map<FuncType*,RuntimeFunc>::iterator rt=rt_funcs.find( f );

	if( f->userlib ){
		//native implementation of a .decls function : _f<name>( int / float / const char* ... )
		used_userlibs.insert( c->sem_decl );
		for( size_t k=0;k<ex.size();++k ){
			string a=expr( ex[k] );
			Type *ty=ex[k]->sem_type;
			if( ty->stringType() ) a="_bbStrToCStr("+a+")";
			else if( ty->structType() ) a="bbObjFieldsPtr("+a+")";
			else if( ty==Type::void_type ) a="bbBankDataPtr("+a+")";	//bank handle passed for a '*' parameter
			args.push_back( a );
			calls.push_back( hasCall( ex[k] ) );
		}
		string t=orderedArgs( "_f"+ident( c->sem_decl->name ),args,calls );
		if( f->returnType->stringType() ) t="_bbCStrToStr("+t+")";
		return t;
	}

	if( rt!=rt_funcs.end() ){
		//runtime command : handles are pointers in C++
		const RuntimeFunc &r=rt->second;
		for( size_t k=0;k<ex.size();++k ){
			string a=expr( ex[k] );
			string pt=k<r.params.size() ? r.params[k] : "int";
			if( pt.find( '*' )!=string::npos && pt.find( "BBStr" )==string::npos ){
				a="(("+pt+")(intptr_t)("+a+"))";
			}
			args.push_back( a );
			calls.push_back( hasCall( ex[k] ) );
		}
		string t=orderedArgs( r.cpp,args,calls );
		if( r.ret.find( '*' )!=string::npos && r.ret.find( "BBStr" )==string::npos ){
			t="((int)(intptr_t)"+t+")";
		}
		return t;
	}

	//user function
	for( size_t k=0;k<ex.size();++k ){
		args.push_back( expr( ex[k] ) );
		calls.push_back( hasCall( ex[k] ) );
	}
	return orderedArgs( "f_"+ident( c->sem_decl->name ),args,calls );
}

string CppGen::expr( ExprNode *e ){
	if( IntConstNode *c=dynamic_cast<IntConstNode*>(e) ){
		if( c->value==(int)0x80000000 ) return "((int)0x80000000)";
		return c->value<0 ? "("+itoa( c->value )+")" : itoa( c->value );
	}
	if( FloatConstNode *c=dynamic_cast<FloatConstNode*>(e) ){
		return floatLiteral( c->value );
	}
	if( StringConstNode *c=dynamic_cast<StringConstNode*>(e) ){
		return "_bbStrConst("+quote( c->value )+")";
	}
	if( CastNode *c=dynamic_cast<CastNode*>(e) ){
		Type *from=c->expr->sem_type,*to=c->sem_type;
		string t=expr( c->expr );
		if( from==Type::float_type && to==Type::int_type ) return "bbFloatToInt("+t+")";
		if( from==Type::int_type && to==Type::float_type ) return "((float)("+t+"))";
		if( from==Type::string_type && to==Type::int_type ) return "_bbStrToInt("+t+")";
		if( from==Type::int_type && to==Type::string_type ) return "_bbStrFromInt("+t+")";
		if( from==Type::string_type && to==Type::float_type ) return "_bbStrToFloat("+t+")";
		if( from==Type::float_type && to==Type::string_type ) return "_bbStrFromFloat("+t+")";
		if( from->structType() && to==Type::string_type ) return "_bbObjToStr("+t+")";
		return t;
	}
	if( CallNode *c=dynamic_cast<CallNode*>(e) ){
		return callExpr( c );
	}
	if( VarExprNode *v=dynamic_cast<VarExprNode*>(e) ){
		return load( v->var );
	}
	if( UniExprNode *u=dynamic_cast<UniExprNode*>(e) ){
		string t=expr( u->expr );
		bool i=u->sem_type==Type::int_type;
		switch( u->op ){
		case '+':return t;
		case '-':return "(-("+t+"))";
		case ABS:return (i ? "_bbAbs(" : "_bbFAbs(")+t+")";
		case SGN:return (i ? "_bbSgn(" : "_bbFSgn(")+t+")";
		}
		return t;
	}
	if( BinExprNode *b=dynamic_cast<BinExprNode*>(e) ){
		switch( b->op ){
		case AND:return binary( b->lhs,b->rhs,"&" );
		case OR:return binary( b->lhs,b->rhs,"|" );
		case XOR:return binary( b->lhs,b->rhs,"^" );
		case SHL:return binaryCall( "bbShl",b->lhs,b->rhs );
		case SHR:return binaryCall( "bbShr",b->lhs,b->rhs );
		case SAR:return binaryCall( "bbSar",b->lhs,b->rhs );
		}
	}
	if( ArithExprNode *a=dynamic_cast<ArithExprNode*>(e) ){
		if( a->sem_type==Type::string_type ) return binaryCall( "_bbStrConcat",a->lhs,a->rhs );
		if( a->sem_type==Type::int_type ){
			switch( a->op ){
			case '+':return binary( a->lhs,a->rhs,"+" );
			case '-':return binary( a->lhs,a->rhs,"-" );
			case '*':return binary( a->lhs,a->rhs,"*" );
			case '/':return binaryCall( "bbIDiv",a->lhs,a->rhs );
			case MOD:return binaryCall( "_bbMod",a->lhs,a->rhs );
			}
		}else{
			switch( a->op ){
			case '+':return binary( a->lhs,a->rhs,"+" );
			case '-':return binary( a->lhs,a->rhs,"-" );
			case '*':return binary( a->lhs,a->rhs,"*" );
			case '/':return binary( a->lhs,a->rhs,"/" );
			case MOD:return binaryCall( "_bbFMod",a->lhs,a->rhs );
			case '^':return binaryCall( "_bbFPow",a->lhs,a->rhs );
			}
		}
	}
	if( RelExprNode *r=dynamic_cast<RelExprNode*>(e) ){
		string l=expr( r->lhs ),rr=expr( r->rhs );
		if( hasCall( r->lhs ) && hasCall( r->rhs ) ){
			int n=++tmp_cnt;
			string ta="_t"+itoa(n)+"a",tb="_t"+itoa(n)+"b";
			return "({ auto "+ta+"="+l+"; auto "+tb+"="+rr+"; "+compare( r->op,ta,tb,r->opType )+"; })";
		}
		return compare( r->op,l,rr,r->opType );
	}
	if( NewNode *n=dynamic_cast<NewNode*>(e) ){
		return "_bbObjNew((BBObjType*)&t_"+ident( n->sem_type->structType()->ident )+")";
	}
	if( FirstNode *n=dynamic_cast<FirstNode*>(e) ){
		return "_bbObjFirst((BBObjType*)&t_"+ident( n->sem_type->structType()->ident )+")";
	}
	if( LastNode *n=dynamic_cast<LastNode*>(e) ){
		return "_bbObjLast((BBObjType*)&t_"+ident( n->sem_type->structType()->ident )+")";
	}
	if( AfterNode *n=dynamic_cast<AfterNode*>(e) ){
		return "_bbObjNext("+expr( n->expr )+")";
	}
	if( BeforeNode *n=dynamic_cast<BeforeNode*>(e) ){
		return "_bbObjPrev("+expr( n->expr )+")";
	}
	if( dynamic_cast<NullNode*>(e) ){
		return "((BBObj*)0)";
	}
	if( ObjectCastNode *o=dynamic_cast<ObjectCastNode*>(e) ){
		return "_bbObjFromHandle("+expr( o->expr )+",(BBObjType*)&t_"+ident( o->sem_type->structType()->ident )+")";
	}
	if( ObjectHandleNode *o=dynamic_cast<ObjectHandleNode*>(e) ){
		return "_bbObjToHandle("+expr( o->expr )+")";
	}
	throw Ex( "bb2cpp: unsupported expression node" );
}

///////////////
// variables //
///////////////

string CppGen::lvalue( VarNode *v ){
	if( DeclVarNode *d=dynamic_cast<DeclVarNode*>(v) ){
		return varName( d->sem_decl );
	}
	if( ArrayVarNode *a=dynamic_cast<ArrayVarNode*>(v) ){
		//index = e0 + e1*scales[0] + e2*scales[1]... (ArrayVarNode::translate)
		ArrayType *at=a->sem_decl->type->arrayType();
		string arr="a_"+ident( a->sem_decl->name );
		string idx;
		vector<ExprNode*> &ex=a->exprs->exprs;
		for( size_t k=0;k<ex.size();++k ){
			string e=expr( ex[k] );
			if( k ) idx="("+idx+"+("+e+")*"+arr+".scales["+itoa(k-1)+"])";
			else idx=e;
		}
		return "(("+cppType( at->elementType )+"*)"+arr+".data)["+idx+"]";
	}
	if( FieldVarNode *f=dynamic_cast<FieldVarNode*>(v) ){
		return "("+expr( f->expr )+")->fields["+itoa( f->sem_field->offset/4 )+"]."+fieldMember( f->sem_type );
	}
	if( VectorVarNode *vv=dynamic_cast<VectorVarNode*>(v) ){
		//element index = sum of subscript * product of the previous sizes (VectorVarNode::translate)
		string idx;
		int stride=1;
		vector<ExprNode*> &ex=vv->exprs->exprs;
		for( size_t k=0;k<ex.size();++k ){
			string e=expr( ex[k] );
			string term=stride==1 ? "("+e+")" : "("+e+")*"+itoa( stride );
			idx=idx.size() ? idx+"+"+term : term;
			stride*=vv->vec_type->sizes[k];
		}
		return "(("+cppType( vv->vec_type->elementType )+"*)("+expr( vv->expr )+"))["+idx+"]";
	}
	throw Ex( "bb2cpp: unsupported variable node" );
}

string CppGen::load( VarNode *v ){
	string lv=lvalue( v );
	if( v->sem_type==Type::string_type ) return "_bbStrLoad(&"+lv+")";
	return lv;
}

string CppGen::store( VarNode *v,const string &value ){
	string lv=lvalue( v );
	if( v->isObjParam() ) return lv+"="+value+";";
	if( v->sem_type->structType() ) return "_bbObjStore(&"+lv+","+value+");";
	if( v->sem_type==Type::string_type ) return "_bbStrStore(&"+lv+","+value+");";
	return lv+"="+value+";";
}

////////////////
// statements //
////////////////

void CppGen::stmts( StmtSeqNode *s ){
	if( !s ) return;
	for( size_t k=0;k<s->stmts.size();++k ) stmt( s->stmts[k] );
}

void CppGen::stmt( StmtNode *s ){
	if( IncludeNode *n=dynamic_cast<IncludeNode*>(s) ){
		line( "// Include \""+n->file+"\"" );
		stmts( n->stmts );
		return;
	}
	if( DeclStmtNode *n=dynamic_cast<DeclStmtNode*>(s) ){
		if( VarDeclNode *v=dynamic_cast<VarDeclNode*>(n->decl) ){
			if( v->sem_var && v->expr ) line( store( v->sem_var,expr( v->expr ) ) );
		}
		return;
	}
	if( DimNode *n=dynamic_cast<DimNode*>(s) ){
		string arr="a_"+ident( n->ident );
		line( "_bbUndimArray((BBArray*)&"+arr+");" );
		for( size_t k=0;k<n->exprs->exprs.size();++k ){
			line( arr+".scales["+itoa(k)+"]="+expr( n->exprs->exprs[k] )+";" );
		}
		line( "_bbDimArray((BBArray*)&"+arr+");" );
		return;
	}
	if( AssNode *n=dynamic_cast<AssNode*>(s) ){
		line( store( n->var,expr( n->expr ) ) );
		return;
	}
	if( ExprStmtNode *n=dynamic_cast<ExprStmtNode*>(s) ){
		string t=expr( n->expr );
		if( n->expr->sem_type==Type::string_type ) line( "_bbStrRelease("+t+");" );
		else line( t+";" );
		return;
	}
	if( LabelNode *n=dynamic_cast<LabelNode*>(s) ){
		*out<<labelName( n->ident )<<":;\n";
		return;
	}
	if( GotoNode *n=dynamic_cast<GotoNode*>(s) ){
		line( "goto "+labelName( n->ident )+";" );
		return;
	}
	if( dynamic_cast<GosubNode*>(s) ){
		throw Ex( "bb2cpp: Gosub is not supported" );
	}
	if( RestoreNode *n=dynamic_cast<RestoreNode*>(s) ){
		string at="0";
		if( n->sem_label ) at=itoa( n->sem_label->data_sz );
		line( "_bbRestore(__DATA+"+at+");" );
		return;
	}
	if( IfNode *n=dynamic_cast<IfNode*>(s) ){
		if( ConstNode *c=n->expr->constNode() ){
			//constant condition : only one branch exists (IfNode::translate)
			if( c->intValue() ){
				line( "{" );++indent;stmts( n->stmts );--indent;line( "}" );
			}else if( n->elseOpt ){
				line( "{" );++indent;stmts( n->elseOpt );--indent;line( "}" );
			}
			return;
		}
		line( "if("+expr( n->expr )+"){" );
		++indent;stmts( n->stmts );--indent;
		if( n->elseOpt ){
			line( "}else{" );
			++indent;stmts( n->elseOpt );--indent;
		}
		line( "}" );
		return;
	}
	if( dynamic_cast<ExitNode*>(s) ){
		line( "break;" );
		return;
	}
	if( WhileNode *n=dynamic_cast<WhileNode*>(s) ){
		if( ConstNode *c=n->expr->constNode() ){
			if( !c->intValue() ) return;
			line( "for(;;){" );
		}else{
			line( "while("+expr( n->expr )+"){" );
		}
		++indent;stmts( n->stmts );--indent;
		line( "}" );
		return;
	}
	if( ForNode *n=dynamic_cast<ForNode*>(s) ){
		//var=from; loop while !(var > to) (step > 0) or !(var < to); To is evaluated every time (ForNode::translate)
		Type *ty=n->var->sem_type;
		line( store( n->var,expr( n->fromExpr ) ) );
		int op=n->stepExpr->constNode()->floatValue()>0 ? '>' : '<';
		line( "for(;;){" );
		++indent;
		line( "if("+compare( op,load( n->var ),expr( n->toExpr ),ty )+") break;" );
		stmts( n->stmts );
		line( store( n->var,"("+load( n->var )+")+("+expr( n->stepExpr )+")" ) );
		--indent;
		line( "}" );
		return;
	}
	if( ForEachNode *n=dynamic_cast<ForEachNode*>(s) ){
		string lv=lvalue( n->var );
		string t="(BBObjType*)&t_"+ident( n->typeIdent );
		bool p=n->var->isObjParam();
		line( string( "if(" )+(p ? "_bbObjEachFirst2" : "_bbObjEachFirst")+"(&"+lv+","+t+")) do{" );
		++indent;stmts( n->stmts );--indent;
		line( string( "}while(" )+(p ? "_bbObjEachNext2" : "_bbObjEachNext")+"(&"+lv+"));" );
		return;
	}
	if( ReturnNode *n=dynamic_cast<ReturnNode*>(s) ){
		if( in_main || !n->expr ){
			line( "goto __leave;" );
			return;
		}
		line( "{ __ret="+expr( n->expr )+"; goto __leave; }" );
		return;
	}
	if( DeleteNode *n=dynamic_cast<DeleteNode*>(s) ){
		line( "_bbObjDelete("+expr( n->expr )+");" );
		return;
	}
	if( DeleteEachNode *n=dynamic_cast<DeleteEachNode*>(s) ){
		line( "_bbObjDeleteEach((BBObjType*)&t_"+ident( n->typeIdent )+");" );
		return;
	}
	if( InsertNode *n=dynamic_cast<InsertNode*>(s) ){
		string f=n->before ? "_bbObjInsBefore" : "_bbObjInsAfter";
		line( binaryCall( f,n->expr1,n->expr2 )+";" );
		return;
	}
	if( SelectNode *n=dynamic_cast<SelectNode*>(s) ){
		Type *ty=n->expr->sem_type;
		line( "{" );
		++indent;
		line( store( n->sem_temp,expr( n->expr ) ) );
		bool first=true;
		for( size_t k=0;k<n->cases.size();++k ){
			CaseNode *c=n->cases[k];
			string cond;
			for( size_t j=0;j<c->exprs->exprs.size();++j ){
				string cmp=compare( '=',load( n->sem_temp ),expr( c->exprs->exprs[j] ),ty );
				cond=cond.size() ? cond+"||"+cmp : cmp;
			}
			line( (first ? "if(" : "}else if(")+cond+"){" );
			first=false;
			++indent;stmts( c->stmts );--indent;
		}
		if( n->defStmts ){
			if( first ){
				line( "{" );
			}else{
				line( "}else{" );
			}
			++indent;stmts( n->defStmts );--indent;
			line( "}" );
		}else if( !first ){
			line( "}" );
		}
		--indent;
		line( "}" );
		return;
	}
	if( RepeatNode *n=dynamic_cast<RepeatNode*>(s) ){
		if( !n->expr ){
			line( "for(;;){" );
			++indent;stmts( n->stmts );--indent;
			line( "}" );
			return;
		}
		if( ConstNode *c=n->expr->constNode() ){
			line( c->intValue() ? "do{" : "for(;;){" );
			++indent;stmts( n->stmts );--indent;
			line( c->intValue() ? "}while(0);" : "}" );
			return;
		}
		line( "do{" );
		++indent;stmts( n->stmts );--indent;
		line( "}while(!("+expr( n->expr )+"));" );
		return;
	}
	if( ReadNode *n=dynamic_cast<ReadNode*>(s) ){
		Type *ty=n->var->sem_type;
		string t=ty==Type::int_type ? "_bbReadInt()" : (ty==Type::float_type ? "_bbReadFloat()" : "_bbReadStr()");
		line( store( n->var,t ) );
		return;
	}
	throw Ex( "bb2cpp: unsupported statement node" );
}

///////////////
// functions //
///////////////

void CppGen::functionBody( Environ *e,StmtSeqNode *body,const string &header,bool is_main ){
	env=e;
	in_main=is_main;
	ret_type=is_main ? "void" : cppType( e->returnType );
	*out<<header<<"{\n";
	indent=1;

	//function timer (runtime/gxruntime/wiiu_prof.h), named after the Blitz function
	{
		size_t paren=header.find( '(' );
		size_t sp=header.rfind( ' ',paren );
		string name=header.substr( sp+1,paren-sp-1 );
		if( name.compare( 0,2,"f_" )==0 ) name=name.substr( 2 );
		line( "BB_PROF_SCOPE(\""+name+"\");" );
	}

	//locals (all function scoped, zero initialised : createVars)
	for( size_t k=0;k<e->decls->decls.size();++k ){
		Decl *d=e->decls->decls[k];
		if( d->kind!=DECL_LOCAL ) continue;
		if( d->type->constType() ) continue;
		line( cppType( d->type )+" "+varName( d )+"=0;" );
	}
	//Blitz arrays (vectors)
	for( size_t k=0;k<e->decls->decls.size();++k ){
		Decl *d=e->decls->decls[k];
		if( d->kind==DECL_PARAM ) continue;
		VectorType *v=d->type->vectorType();
		if( !v ) continue;
		line( varName( d )+"=_bbVecAlloc((BBVecType*)&vt"+ident( v->label )+");" );
	}
	if( !is_main ) line( ret_type+" __ret=0;" );
	if( is_main ) line( "_bbRestore(__DATA);" );

	stmts( body );

	if( !is_main ){
		//falling off the end returns the type's default (0 / 0.0 / "" / Null)
		if( e->returnType==Type::string_type ) line( "__ret=_bbStrConst(\"\");" );
	}
	*out<<"__leave:;\n";
	//release locals (deleteVars)
	for( size_t k=0;k<e->decls->decls.size();++k ){
		Decl *d=e->decls->decls[k];
		Type *t=d->type;
		if( t==Type::string_type && (d->kind==DECL_LOCAL || d->kind==DECL_PARAM) ){
			line( "_bbStrRelease("+varName( d )+");" );
		}else if( t->structType() && d->kind==DECL_LOCAL ){
			line( "_bbObjRelease("+varName( d )+");" );
		}else if( VectorType *v=t->vectorType() ){
			if( d->kind==DECL_LOCAL ) line( "_bbVecFree("+varName( d )+",(BBVecType*)&vt"+ident( v->label )+");" );
		}
	}
	if( !is_main ) line( "return __ret;" );
	*out<<"}\n\n";
	indent=0;
}

//////////////////////
// program overview //
//////////////////////

void CppGen::collectEnv( Environ *e ){
	for( size_t k=0;k<e->types.size();++k ){
		if( VectorType *v=e->types[k]->vectorType() ) vectors.push_back( v );
	}
}

void CppGen::collect( ProgNode *p ){
	Environ *e=p->sem_env;
	for( size_t k=0;k<p->structs->decls.size();++k ){
		StructDeclNode *s=dynamic_cast<StructDeclNode*>(p->structs->decls[k]);
		if( s ) structs.push_back( s->sem_type );
	}
	collectEnv( e );
	for( size_t k=0;k<p->funcs->decls.size();++k ){
		FuncDeclNode *f=dynamic_cast<FuncDeclNode*>(p->funcs->decls[k]);
		if( f ) collectEnv( f->sem_env );
	}
	for( size_t k=0;k<e->decls->decls.size();++k ){
		Decl *d=e->decls->decls[k];
		if( d->kind==DECL_GLOBAL ) globals.push_back( d );
		else if( d->kind==DECL_ARRAY ) arrays.push_back( make_pair( d->name,d->type->arrayType() ) );
	}
}

static string funcHeader( FuncDeclNode *f,CppGen *g,string (*ct)( Type* ),string (*vn)( Decl* ) ){
	string t=ct( f->sem_type->returnType )+" f_"+ident( f->ident )+"(";
	DeclSeq *ps=f->sem_type->params;
	for( int k=0;k<ps->size();++k ){
		Decl *d=f->sem_env->decls->findDecl( ps->decls[k]->name );
		t+=(k ? "," : "")+ct( ps->decls[k]->type )+" "+vn( d ? d : ps->decls[k] );
	}
	return t+")";
}

void CppGen::writeHeader( const string &path ){
	ofstream o( path.c_str() );
	o<<"// GENERATED by bb2cpp from the game's BlitzBasic source -- do not edit.\n";
	o<<"#ifndef BBGAME_H\n#define BBGAME_H\n\n#include \"bbcpp.h\"\n\n";

	o<<"// custom types\n";
	for( size_t k=0;k<structs.size();++k ){
		int n=structs[k]->fields->size();
		o<<"extern BBObjTypeN<"<<(n>0 ? n : 1)<<"> t_"<<ident( structs[k]->ident )<<";\n";
	}
	o<<"\n// Blitz array (vector) types\n";
	for( size_t k=0;k<vectors.size();++k ){
		o<<"extern BBVecTypeS vt"<<ident( vectors[k]->label )<<";\n";
	}
	o<<"\n// globals\n";
	for( size_t k=0;k<globals.size();++k ){
		if( globals[k]->type->constType() ) continue;
		o<<"extern "<<cppType( globals[k]->type )<<" "<<varName( globals[k] )<<";\n";
	}
	o<<"\n// Dim arrays\n";
	for( size_t k=0;k<arrays.size();++k ){
		o<<"extern BBArrayN<"<<arrays[k].second->dims<<"> a_"<<ident( arrays[k].first )<<";\n";
	}
	o<<"\nextern BBData __DATA[];\n";
	o<<"void bbInitData();\n";

	o<<"\n// userlib (.decls) functions, implemented natively\n";
	for( set<Decl*>::iterator it=used_userlibs.begin();it!=used_userlibs.end();++it ){
		FuncType *f=(*it)->type->funcType();
		string rt=f->returnType==Type::string_type ? "const char*" : cppType( f->returnType );
		o<<rt<<" _f"<<ident( (*it)->name )<<"(";
		for( int k=0;k<f->params->size();++k ){
			Type *t=f->params->decls[k]->type;
			string pt=t==Type::string_type ? "const char*" : (t==Type::float_type ? "float" : "int");
			o<<(k ? "," : "")<<pt;
		}
		o<<");\n";
	}

	o<<"\n// user functions\n";
	for( size_t k=0;k<prog->funcs->decls.size();++k ){
		FuncDeclNode *f=dynamic_cast<FuncDeclNode*>(prog->funcs->decls[k]);
		if( f ) o<<funcHeader( f,this,cppType,varName )<<";\n";
	}
	o<<"\nvoid bbMain();\n\n#endif\n";
}

void CppGen::writeData( const string &path ){
	ofstream o( path.c_str() );
	o<<"// GENERATED by bb2cpp -- do not edit.\n#include \"bbgame.h\"\n\n";

	//custom type descriptors : same layout as the original data section (StructDeclNode::translate)
	for( size_t k=0;k<structs.size();++k ){
		StructType *s=structs[k];
		string n="t_"+ident( s->ident );
		int cnt=s->fields->size();
		o<<"BBObjTypeN<"<<(cnt>0 ? cnt : 1)<<"> "<<n<<"={ 5,";
		o<<"{0,&"<<n<<".used,&"<<n<<".used,0,-1},";
		o<<"{0,&"<<n<<".free,&"<<n<<".free,0,-1},";
		o<<cnt<<",{";
		for( int j=0;j<cnt;++j ) o<<(j ? "," : "")<<typeDescriptor( s->fields->decls[j]->type );
		if( !cnt ) o<<"0";
		o<<"} };\n";
	}
	o<<"\n";
	for( size_t k=0;k<vectors.size();++k ){
		VectorType *v=vectors[k];
		int sz=1;
		for( size_t j=0;j<v->sizes.size();++j ) sz*=v->sizes[j];
		o<<"BBVecTypeS vt"<<ident( v->label )<<"={ 6,"<<sz<<","<<typeDescriptor( v->elementType )<<" };\n";
	}
	o<<"\n";
	for( size_t k=0;k<globals.size();++k ){
		if( globals[k]->type->constType() ) continue;
		o<<cppType( globals[k]->type )<<" "<<varName( globals[k] )<<"=0;\n";
	}
	o<<"\n";
	for( size_t k=0;k<arrays.size();++k ){
		ArrayType *a=arrays[k].second;
		Type *ty=a->elementType;
		int et=ty==Type::int_type ? 1 : (ty==Type::float_type ? 2 : (ty==Type::string_type ? 3 : 5));
		o<<"BBArrayN<"<<a->dims<<"> a_"<<ident( arrays[k].first )<<"={ 0,"<<et<<","<<a->dims<<",{0} };\n";
	}

	//Data table (DataDeclNode::transdata)
	int n=0;
	for( size_t k=0;k<prog->datas->decls.size();++k ) if( dynamic_cast<DataDeclNode*>(prog->datas->decls[k]) ) ++n;
	o<<"\nBBData __DATA["<<n+1<<"];\n\nvoid bbInitData(){\n";
	int i=0;
	for( size_t k=0;k<prog->datas->decls.size();++k ){
		DataDeclNode *d=dynamic_cast<DataDeclNode*>(prog->datas->decls[k]);
		if( !d ) continue;
		ConstNode *c=d->expr->constNode();
		string at="\t__DATA["+itoa(i)+"]";
		if( d->expr->sem_type==Type::int_type ){
			o<<at<<".fieldType=1;"<<at<<".field.INT="<<c->intValue()<<";\n";
		}else if( d->expr->sem_type==Type::float_type ){
			o<<at<<".fieldType=2;"<<at<<".field.FLT="<<floatLiteral( c->floatValue() )<<";\n";
		}else{
			o<<at<<".fieldType=4;"<<at<<".field.CSTR=(char*)"<<quote( c->stringValue() )<<";\n";
		}
		++i;
	}
	o<<"\t__DATA["<<i<<"].fieldType=0;\n}\n";
}

void CppGen::generate( ProgNode *p,const string &dir,int per_file ){
	prog=p;
	collect( p );

	//main program
	{
		ofstream o( (dir+"/bbgame_main.cpp").c_str() );
		o<<"// GENERATED by bb2cpp from the game's BlitzBasic source -- do not edit.\n#include \"bbgame.h\"\n\n";
		out=&o;
		functionBody( p->sem_env,p->stmts,"void bbMain()",true );
	}

	//functions, several files so they compile in parallel
	int file_no=0,in_file=0;
	ofstream *o=0;
	for( size_t k=0;k<p->funcs->decls.size();++k ){
		FuncDeclNode *f=dynamic_cast<FuncDeclNode*>(p->funcs->decls[k]);
		if( !f ) continue;
		if( !o || in_file>=per_file ){
			delete o;
			o=new ofstream( (dir+"/bbgame_funcs_"+itoa( file_no++ )+".cpp").c_str() );
			*o<<"// GENERATED by bb2cpp from the game's BlitzBasic source -- do not edit.\n#include \"bbgame.h\"\n\n";
			in_file=0;
		}
		out=o;
		*o<<"// "<<f->file<<"\n";
		try{
			functionBody( f->sem_env,f->stmts,funcHeader( f,this,cppType,varName ),false );
		}catch( Ex &x ){
			if( x.pos<0 ) x.pos=f->pos;
			if( !x.file.size() ) x.file=f->file;
			throw;
		}
		++in_file;
	}
	delete o;

	writeData( dir+"/bbgame_data.cpp" );
	writeHeader( dir+"/bbgame.h" );

	//so the build knows which files exist
	ofstream list( (dir+"/bbgame_files.txt").c_str() );
	list<<"bbgame_main.cpp\nbbgame_data.cpp\n";
	for( int k=0;k<file_no;++k ) list<<"bbgame_funcs_"<<k<<".cpp\n";
}
