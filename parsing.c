#include "mpc.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>


#ifdef _WIN32

  static char buffer[2048];
  /*Custom readline Function for Windows*/
  char* readline(char* prompt){
    fputs(prompt, stdout);
    fgets(buffer, 2048, stdin);
    char* cpy = malloc(strlen(buffer)+1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy)-1]= '\0';
    return cpy;
  }
  /*Custom add_history Function for windows*/
  char add_history(char* unused) {}
#else 
  #include <editline/readline.h>
  #include <editline/history.h>
#endif


//Enumeration of possible lval types
enum{LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR, LVAL_FUN, LVAL_STR};
//Enumeration of possible error types
enum{LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM};

//Parser Declarations
mpc_parser_t* Number;
mpc_parser_t* Symbol;
mpc_parser_t* String;
mpc_parser_t* Sexpr;
mpc_parser_t* Qexpr;
mpc_parser_t* Expr;
mpc_parser_t* Lispy;
mpc_parser_t* Comment;

//Forward declarations
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;
void lval_print(lval* v);
lval* lval_copy(lval* v);
void lval_del(lval* lv);
lval* lval_err(char* fmt, ...);
lval* eval_sexpr(lenv* env,lval* v);
typedef lval*(*lbuiltin)(lenv*, lval*);
char* ltype_name(int t);
lval* builtin_var(lenv* e, lval* a, char* func);
lval* builtin_eval(lenv* env,lval* a);
lval* pop(lval* v, int i);
lval* builtin_list(lenv* env,lval* a);
lval* builtin_ord(lenv* env, lval* a, char* op);
lval* builtin_cmp(lenv* env, lval* a, char* op);
void lval_print_str(lval* v);
lval* lval_read_str(mpc_ast_t* t);
lval* eval(lenv* env, lval* v);


//Macro to help with error checking
#define LASSERT(args, cond, fmt, ...) \
  if(!(cond)){ \
    lval* err = lval_err(fmt, ##__VA_ARGS__);\
    lval_del(args); \
    return err;}
 
//Macro checks function passed correct num of args
#define LASSERT_NUM(func, args, num)\
  LASSERT(args, args->count == num,\
    "Function '%s'passed incorrect number of arguments."\
    "Got %i, Expected %i",\
    func, args->count, num)
#define LASSERT_TYPE(func, args, index, expect)\
  LASSERT(args, args->cell[index]->type == expect,\
    "Function '%s'passed incorrect type of argument %i"\
    "Got %s, Expected %s",\
    func, index, ltype_name(args->cell[index]->type),ltype_name(expect))

#define LASSERT_NOT_EMPTY(func, args, index)\
  LASSERT(args, args->cell[index]->count != 0, \
    "Function '%s' passed {} for argument %i.", func, index);

//List of relationships between names and values for our env
struct lenv{
  lenv* parent;
  int count;
  char** syms;
  lval** vals;
};

struct lval{
  int type;
  long num;

  //We store error and symbol type as string data
  char* sym;
  char* err;
  char* str;

  //Function
  lbuiltin builtin;
  lenv* env;
  lval* formals;
  lval* body;

  //Expression
  int count;
  lval** cell;
};

//Create a new lenv (environment)
lenv* lenv_new(void){
  lenv* env = malloc(sizeof(lenv));
  env->count = 0;
  env->syms = NULL;
  env->vals = NULL;
  env->parent = NULL;
  return env;
}

//delete an environment
void lenv_del(lenv* env){
  for(int i = 0; i < env->count; i++){
    free(env->syms[i]);
    lval_del(env->vals[i]);
  }
  free(env->syms);
  free(env->vals);
  free(env);
}

lval* lenv_get(lenv* env,lval* k){
  //Iterate over all the items in the env
  for(int i = 0; i < env->count; i++){
    //Check if the stored string matches the symbol string
    //If it does return a copy of the value
    if(strcmp(env->syms[i],k->sym) == 0){
      return lval_copy(env->vals[i]);
    }
  }
  //If no symbol was found check in parent
  if(env->parent){
    return lenv_get(env->parent, k);
  } else{
    return lval_err("unbound symbol '%s'", k->sym);
  } 
}

void lenv_put(lenv* env, lval* k, lval* var){
  //Iterate over all the items in the env to see
  // it the variable already exists
  for(int i = 0; i < env->count; i++){
    //if it exists delete the old value and replace it
    //with the value provided by the user
    if(strcmp(env->syms[i], k->sym)== 0){
      lval_del(env->vals[i]);
      env->vals[i] = lval_copy(var);
      return;
    }
  }
  //If no existing entry was found, allocate space for new entry
  env->count++;
  env->vals = realloc(env->vals, sizeof(lval*) * env->count);
  env->syms = realloc(env->syms, sizeof(char*) * env->count);

  //Copy the content of lval and symbol strings into new location
  env->vals[env->count-1] = lval_copy(var);
  env->syms[env->count-1] = malloc(strlen(k->sym)+1);
  strcpy(env->syms[env->count-1], k->sym);
}

lenv* lenv_copy(lenv* env){
  lenv* n = malloc(sizeof(lenv));
  n->parent = env->parent;
  n->count = env->count;
  n->syms = malloc(sizeof(char*)  * n->count);
  n->vals = malloc(sizeof(lval*) * n->count);
  for(int i = 0; i < env->count; i++){
    n->syms[i]= malloc(strlen(env->syms[i])+1);
    strcpy(n->syms[i], env->syms[i]);
    n->vals[i] = lval_copy(env->vals[i]);
  }
  return n;
}

void lenv_def(lenv* env, lval* k, lval* v){
  //Iterate till e has no parent
  while(env->parent){
    env = env->parent;
  }
  //put value in global environment
  lenv_put(env, k , v);
  
}

lval* lval_str(char* str){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_STR;
  v->str = malloc(strlen(str) + 1);
  strcpy(v->str, str);
  return v;
}

lval* lval_fun(lbuiltin func){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->builtin = func;
  return v;
}

lval* lval_lambda(lval* formals, lval* body){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  //Builtin is Null because this is user defined func
  v->builtin = NULL;
  v->env = lenv_new();
  v->formals = formals;
  v->body = body;
  return v;
}

lval* lval_num(long x){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval* lval_err(char* fmt, ...){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  //Create a va list and initialize it
  //va is put for variable argument list
  va_list va;
  va_start(va, fmt);

  //Allocate 512 bytes of space
  v->err = malloc(512);

  //printf the error string with a maximum of 511
  //characters
  vsnprintf(v->err, 511, fmt, va);
  //Reallocate to number of bytes actually used
  v->err = realloc(v->err, strlen(v->err)+1);

  //cleanup the va list
  va_end(va);

  return v;
}
/*Pointer to a new symbol lval*/
lval* lval_sym(char* s){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s)+1);
  strcpy(v->sym,s);
  return v;
}

/*Pointer to a new empty s-expression*/
lval* lval_sexpr(void){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;

  v->cell = NULL;

  return v;
}
//A pointer to a new empty Qexpr lval
lval* lval_qexpr(void){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

void lval_del(lval* lv){

  switch(lv->type){
    case LVAL_STR:
      free(lv->str);
      break;
    case LVAL_FUN:
      if(!lv->builtin){
        lenv_del(lv->env);
        lval_del(lv->formals);
        lval_del(lv->body);
      }
      break;
    case LVAL_NUM:
      break;
    case LVAL_ERR: 
      free(lv->err);
      break;
    case LVAL_SYM:
      free(lv->sym);
      break;
    //If S-Expr or Q-Expr delete all elements inside
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      for(int i =0; i < lv->count; i++){
        lval_del(lv->cell[i]);
      }
      //free the memory allocated to contain the pointers
      free(lv->cell);
      break;
  }

  //Free the memory allocated for the lval struct itself
  free(lv);
}

lval* lval_read_num(mpc_ast_t* t){

  errno = 0;
  long x = strtol(t->contents,NULL,10);
  if(errno != ERANGE){
    return lval_num(x);
  }
  else{
    return lval_err("invalid number");
  }

}

lval* lval_add(lval* v, lval* x){
  v->count++;
  v->cell = realloc(v->cell,sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval* lval_read(mpc_ast_t* t){
  if(strstr(t->tag,"string")){return lval_read_str(t);}
  /*If symbol or number return conversion to that type*/
  if(strstr(t->tag,"number")){ return lval_read_num(t);}
  if(strstr(t->tag,"symbol")){ return lval_sym(t->contents); }
  /*ifroot (>) or sexpr then create an empty list*/
  lval* x = NULL;
  if(strcmp(t->tag,">")==0){x = lval_sexpr();}
  if(strstr(t->tag,"sexpr")){x = lval_sexpr();}
  if(strstr(t->tag,"qexpr")){x = lval_qexpr();}

  /*Fill in this list with any valid expressions contained within*/
  for(int i = 0; i < t->children_num; i++){
    if(strstr(t->children[i]->tag, "comment")){continue;}
    if(strcmp(t->children[i]->contents, "(") == 0) {continue; }
    if(strcmp(t->children[i]->contents, ")") == 0) {continue; }
    if(strcmp(t->children[i]->contents, "{") == 0) {continue; }
    if(strcmp(t->children[i]->contents, "}") == 0) {continue; }
    if(strcmp(t->children[i]->tag, "regex") == 0) {continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

lval* lval_read_str(mpc_ast_t* t){
  //Remove the final quote character
  t->contents[strlen(t->contents)-1]='\0';
  //copy the sring that is missing the first quote char
  char* unescaped = malloc(strlen(t->contents+1)+1);
  strcpy(unescaped, t->contents+1);
  //pass it through the unescaped func
  unescaped = mpcf_unescape(unescaped);
  //Create a new lval using the string
  lval* str = lval_str(unescaped);
  free(unescaped);
  return str;
}

//print an s-expr
void lval_expr_print(lval* v, char open, char close){
  putchar(open);
  //Print value contained within
  for(int i = 0; i < v->count; i++){
    lval_print(v->cell[i]);

    //Don't print trailing space if last element
    if(i!=(v->count-1)){
      putchar(' ');
    }

  }

  putchar(close);
}

//Print an lval
void lval_print(lval* v){
  switch(v->type){
    case LVAL_STR:
      lval_print_str(v);
      break;
    case LVAL_FUN:
      if(v->builtin){
        printf("< builtin function>");
      } else{
        printf("(\\"); lval_print(v->formals);
        putchar(' '); lval_print(v->body);
        putchar(')');
      }
      break;
    case LVAL_NUM:
      printf("%li",v->num);
      break;
    case LVAL_ERR:
      printf("ERROR: %s", v->err);
      break;
    case LVAL_SEXPR:
      lval_expr_print(v,'(',')' );
      break;
    case LVAL_SYM:
      printf("SYM: %s",v->sym);
      break;
    case LVAL_QEXPR:
      lval_expr_print(v,'{', '}');
      break;
  }
}

//Print an lval followed by a new line
void lval_println(lval* v){
  lval_print(v);
  printf("\n");
}

//Print a string
void lval_print_str(lval* v){
  //Make a copy of the string
  char* escaped = malloc(strlen(v->str)+1);
  strcpy(escaped, v->str);
  //Pass it through the escaped function
  escaped = mpcf_escape(escaped);
  //Print it between characters
  printf("\"%s\"",escaped); 
  free(escaped);
}

//Copy an lval
lval* lval_copy(lval* v){
  lval* x = malloc(sizeof(lval));
  x->type = v->type;

  switch(v->type){
    case LVAL_STR:
      x->str = malloc(strlen(v->str)+1);
      strcpy(x->str, v->str);
      break;
    //Copy functions and numbers directly
    case LVAL_FUN:
      if(v->builtin){
        x->builtin = v->builtin;
      } else {
        x->builtin = NULL;
        x->formals = lval_copy(v->formals);
        x->body = lval_copy(v->body);
        x->env = lenv_copy(v->env);
      }
      break;
    case LVAL_NUM:
      x->num = v->num;
      break;
    //Copy strings and symbols with malloc and strcpy
    case LVAL_SYM:
      x->sym = malloc(strlen(v->sym) + 1);
      strcpy(x->sym, v->sym);
      break;

    case LVAL_ERR:
      x->err = malloc(strlen(v->err) + 1 );
      strcpy(x->err, v->err);
      break;
    //Copy expressions one element at a time
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      for(int i = 0; i < x->count; i++){
        x->cell[i] = lval_copy(v->cell[i]);
      }
      break;
  }

  return x;
}

lval* lval_call(lenv* e, lval* f, lval* a){
  //If Builtin then call it
  if(f->builtin){
    return f->builtin(e, a);
  }
  //Record Argument counts
  int given = a->count;
  int total = f->formals->count;
  //While there are args still to be processed
  while(a->count){
    //If there are no more formal args to bind
    if(f->formals->count == 0){
      lval_del(a); 
      return lval_err("Function passed too many args"
        "Got %i, Expected %i", given, total);
    }

    //Pop first symbol from formals
    lval* sym = pop(f->formals, 0);
    //Special Case to deal with '&'
    if (strcmp(sym->sym, "&") == 0){

      //Ensure & is followed by another symbol
      if(f->formals->count != 1){
        lval_del(a);
        return lval_err("Function format invalid. "
          "Symbol '&' not followed by single symbol"
        );
      }

      //Formals is bound to remaining args
      lval* newsym = pop(f->formals, 0);
      lenv_put(f->env, newsym, builtin_list(e,a));
      lval_del(sym); 
      lval_del(newsym);
      break;
    }

    //Pop next arg from the list
    lval* val = pop(a, 0);

    //Bind copy into the function env
    lenv_put(f->env, sym, val);
    //Delete symbol and value
    lval_del(sym);
    lval_del(val);
  }

  //Clean up arg list
  lval_del(a);
  //if '&' remains in formal list bind to empty list
  if (f->formals->count > 0 && 
    strcmp(f->formals->cell[0]->sym, "&")==0){
    //check that & is not passed invalidly
    if(f->formals->count != 2){
      return lval_err("Function format invalid. "
      "Symbol '&' not followed by single symbol");
    }

    //Pop and delete the symbol '&'
    lval_del(pop(f->formals, 0));
    //pop next symbol and create empty list
    lval* sym = pop(f->formals, 0);
    lval* val = lval_qexpr();

    //Bind to env and delete
    lenv_put(f->env, sym, val);
    lval_del(sym); lval_del(val);
  }

  //if all formals have been bound evaluate
  if(f->formals->count == 0){
    //set env parent to evaluation env
    f->env->parent = e;
    //Evaluate and return
    return builtin_eval(f->env, lval_add(lval_sexpr(),
    lval_copy(f->body)));
  }else{
    //Otherwise return partially evaluated func
    return lval_copy(f);
  } 
}

lval* lval_eq(lval* x, lval* y){
  //Different types are unequal
  if(x->type != y->type){
    return 0;
  }
  //Type based comparison
  switch(x->type){
    case LVAL_STR:
      return (strcmp(x->str, y->str) == 0);
      break;
    case LVAL_NUM:
      return x->num == y->num;
      break;
    case LVAL_ERR:
      return (strcmp(x->err, y->err) == 0);
      break;
    case LVAL_SYM:
      return (strcmp(x->sym, y->sym) == 0);
      break;
    //Compare if builtin. Else compare formal and body
    case LVAL_FUN:
      if(x->builtin || y->builtin){
        return x->builtin == y->builtin;
      }
      else{
        return lval_eq(x->formals, y->formals) && lval_eq(x->body, y->body); 
      }
      break;
    case LVAL_SEXPR:
    case LVAL_QEXPR:
      if(x->count != y->count) return 0;
      for(int i = 0; i < x->count; i++){
        if(!lval_eq(x->cell[i],y->cell[i])){
          return 0;
        }
      }
      return 1;
  }
  return 0;
}

lval* builtin_load(lenv* env, lval* a){
  LASSERT_NUM("load", a, 1);
  LASSERT_TYPE("load", a, 0, LVAL_STR);

  //Parse a file given by a string name
  mpc_result_t r;
  if(mpc_parse_contents(a->cell[0]->str, Lispy, &r)){
    //Read contents
    lval* expr = lval_read(r.output);
    mpc_ast_delete(r.output);

    //Evaluate each expression
    while(expr->count){
      lval* x = eval(env, pop(expr, 0));
      //if error during evaluation, just print it
      if(x->type == LVAL_ERR){lval_println(x);}
      lval_del(x);
    }
    //Delete expr and args
    lval_del(expr);
    lval_del(a); 
    //Return empty list
    return lval_sexpr();
  }else{
    //Get the parser error as string
    char* err_msg = mpc_err_string(r.error);
    mpc_err_delete(r.error);
    //Create new error message using it
    lval* err = lval_err("Could not load library %s", err_msg);
    free(err_msg);
    lval_del(a);
    //Clean up and return error
    return err;
  }
}

lval* eval(lenv* env, lval* v){
  if (v->type == LVAL_SYM){
    lval* x = lenv_get(env, v);
    lval_del(v);
    return x;
  }
  //Evaluate S-Expression
  if(v->type == LVAL_SEXPR){
    return eval_sexpr(env,v);
  }
  //All other types of lval remain the same
  return v;
}

//Extract a single element from an lval list and 
//shift the rest of the list so that it no longer
//contains that lval
lval* pop(lval* v, int i){
  //The item at i
  lval* x = v->cell[i];

  //Shift memory after the item at "i" over the top
  memmove(&v->cell[i], &v->cell[i+1],
    sizeof(lval*) * (v->count-i-1)
  );

  v->count--;

  //Reallocate the memory used
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);

  return x;

}
//Similar to pop but it deletes the list 
//it has extracted the value from. 
lval* take(lval* v, int i){
  lval* x = pop(v, i);
  lval_del(v);
  return x;
}

//Builtin operator function
lval* builtin_op(lenv* env, lval* a, char* op){
  //Make sure all arguments are numbers
  for(int i = 0; i < a->count; i++){
    if(a->cell[i]->type != LVAL_NUM){
      lval_del(a);
      return lval_err("Function '%s' passed incorrect type for argument 1."
      "Got %s. Expected %s",op, ltype_name(a->cell[i]->type),ltype_name(LVAL_NUM));     
    }
  }

  // Pop the first element
  lval* x = pop(a, 0);

  //If there are no arguments and substraction we 
  //do unary negation
  if((strcmp(op,"-") == 0) && a->count == 0){
    x->num = -x->num;
  }

  //While there are more elements remaining
  while(a->count > 0){
    lval* y = pop(a,0);
    if (strcmp(op,"+")== 0){
      x->num += y->num;
    }

    if (strcmp(op,"-") == 0){
      x->num -= y->num;
    }

    if (strcmp(op,"*") == 0){
      x->num *= y->num;
    }

    if (strcmp(op,"%") == 0){
      x->num %= y->num;
    }

    if (strcmp(op,"/") == 0){
      if ( y->num == 0){
        lval_del(x); lval_del(y);
        x = lval_err("Division By zero!");
        break;
      }
      x->num /= y->num;
    }

    lval_del(y);
  }

  lval_del(a); return x;

}

char* ltype_name(int t){
  switch(t){
    case LVAL_STR:
      return "String";
    case LVAL_FUN: 
      return "Function";
    case LVAL_NUM:
      return "Number";
    case LVAL_ERR:
      return "Error";
    case LVAL_SYM:
      return "Symbol";
    case LVAL_QEXPR:
      return "Q-Expression";
    case LVAL_SEXPR:
      return "S-Expression";
    default:
      return "Unknown";
  }
}


lval* builtin_lambda(lenv* e, lval* a){
  //Check 2 arguments, each of which is
  //a q-expression
  LASSERT_NUM("\\", a, 2);
  LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
  LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

  //Check first Q-expression contains symbols only
  for(int i = 0; i < a->cell[0]->count; i++){
    LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
      "Can't define non-symbol. Got %s, Expected %s",
      ltype_name(a->cell[0]->cell[i]->type),ltype_name(LVAL_SYM));
  }

  //pop the first 2 arguments and create an lval_lambda
  lval* formals = pop(a,0);
  lval* body = pop(a, 0);
  lval_del(a);
  return lval_lambda(formals, body);
}

lval* builtin_head(lenv* env,lval* a){
  //Error checking
  LASSERT(a, a->count == 1, 
    "Too many arguments passed to function 'head'. "
    "Got %i, Expected %i.", a->count,1);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
  "Function head passed incorrect type. "
  "Got a %s, Expected %s", 
    ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR) );

  LASSERT(a, a->cell[0]->count !=0, 
    "Function head passed {}!");
  //Otherwise take first argument
  lval* v = take(a, 0);
  //Delete all elements that are not head and return
  while(v->count > 1) { lval_del(pop(v,1));}

  return v;
}

lval* builtin_tail(lenv* env,lval* a){
  //Check for errors
  LASSERT(a, a->count == 1, 
    "'Tail' passed too many args."
    "Got %i, Expected %i.", a->count,1);

  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "'Tail' passed the wrong types."
    "Got a %s, Expected %s", 
    ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

  LASSERT(a, a->cell[0]->count != 0,
    "Function tail passed {}!");
  //Take first argument
  lval* v = take(a,0);
  //Delete first element and return 
  lval_del(pop(v,0));

  return v;
}

lval* builtin_list(lenv* env,lval* a){
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lenv* env,lval* a){
  LASSERT(a, a->count == 1, 
    "Function 'eval' passed too many arguments!"
    "Got %i, Expected %i.", a->count,1);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, 
    "Function 'eval' passed incorrect type!"
    "Got a %s, Expected %s", 
    ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

  lval* x = take(a, 0);
  x->type = LVAL_SEXPR;
  return eval(env, x);
}

lval* lval_join(lval* x, lval* y){
  //For each cell in y add it to x
  while(y->count){
    x = lval_add(x, pop(y,0));
  }
  lval_del(y);
  return x;
}


lval* builtin_join(lenv* env,lval* a){
  for(int i = 0; i < a->count; i++){
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR, 
      "Function 'join' passed incorrect type."
      "Got a %s, Expected %s", 
    ltype_name(a->cell[i]->type), ltype_name(LVAL_QEXPR));
  }

  lval* x = pop(a,0);
  while(a->count){
    x = lval_join(x, pop(a,0));
  }

  lval_del(a);
  return x;
}

lval* builtin_mod(lenv* env, lval* a){
  return builtin_op(env, a, "%");
}

lval* builtin_add(lenv* env, lval* a){
  return builtin_op(env, a, "+");
}

lval* builtin_sub(lenv* env, lval* a){
  return builtin_op(env, a, "-");
}

lval* builtin_mul(lenv* env, lval* a){
  return builtin_op(env, a, "*");
}

lval* builtin_div(lenv* env, lval* a){
  return builtin_op(env, a, "/");
}

lval* builtin_gt(lenv* env, lval* a){
  return builtin_ord(env, a, ">");
}
lval* builtin_lt(lenv* env, lval* a){
  return builtin_ord(env, a, "<");
}
lval* builtin_le(lenv* env, lval* a){
  return builtin_ord(env, a, "<=");
}
lval* builtin_ge(lenv* env, lval* a){
  return builtin_ord(env, a, ">=");
}
lval* builtin_eq(lenv* env, lval* a){
  return builtin_cmp(env, a , "==");
}
lval* builtin_neq(lenv* env, lval* a){
  return builtin_cmp(env, a , "!=");
}
lval* builtin_put(lenv* env, lval* a){
  return builtin_var(env, a, "=");
}

lval* builtin_def(lenv* env, lval* a){
  return builtin_var(env, a, "def");
}


lval* builtin(lenv* env, lval* a, char* func){
  if (strcmp("list", func) == 0) {
    return builtin_list(env,a);
  }
  if (strcmp("head", func) == 0) {
    return builtin_head(env,a);
  }
  if (strcmp("tail", func) == 0) {
    return builtin_tail(env,a);
  }
  if (strcmp("join", func) == 0) {
    return builtin_join(env,a);
  }
  if (strcmp("eval", func) == 0) {
    return builtin_eval(env,a);
  }
  if(strstr("+-/*", func)){ 
    return builtin_op(env, a, func);
  }

  lval_del(a);
  return lval_err("Unknown Function '%s'",func);

}

lval* builtin_ord(lenv* env, lval* a, char* op){
  LASSERT_NUM(op, a, 2);
  LASSERT_TYPE(op, a, 0, LVAL_NUM);
  LASSERT_TYPE(op, a, 1, LVAL_NUM);
 
  int result;

  if(strcmp(op, ">")== 0){
    result = (a->cell[0]->num > a->cell[1]->num);
  }
  else if(strcmp(op, "<")== 0){
    result = (a->cell[0]->num < a->cell[1]->num);
  }
  else if(strcmp(op, "<=")== 0){
    result = (a->cell[0]->num <= a->cell[1]->num);
  }
  else if(strcmp(op, ">=")== 0){
    result = (a->cell[0]->num >= a->cell[1]->num);
  }
  lval_del(a);
  return lval_num(result);
}

lval* builtin_cmp(lenv* env, lval* a, char* op){
  LASSERT_NUM(op, a, 2);
  int result;
  if(strcmp(op, "==") == 0){
    result = lval_eq(a->cell[0], a->cell[1]);
  }
  else if(strcmp(op, "!=") == 0){
    result = (!lval_eq(a->cell[0], a->cell[1]));
  }
  lval_del(a);

  return lval_num(result);
}

lval* builtin_var(lenv* e, lval* a, char* func){
  LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

  lval* syms = a->cell[0];
  for(int i = 0; i < syms->count; i++){
    LASSERT(a, (syms->cell[i]->type == LVAL_SYM), 
    "Function '%s' cannot define non-symbol. "
    "Got %s, Expected %s.", func,
    ltype_name(syms->cell[i]->type),
    ltype_name(LVAL_SYM));
  }

  LASSERT(a, (syms->count == a->count -1),
    "Function '%s' passed too many arguments for symbols. "
    "Got %i, Expected %i", func, syms->count, a->count-1);

  for (int i = 0; i<syms->count; i++){
    //If 'def' define globally. If 'put' define locally
    if(strcmp(func, "def") == 0){
      lenv_def(e, syms->cell[i], a->cell[i+1]);
    }
    if(strcmp(func, "=") == 0){
      lenv_put(e, syms->cell[i], a->cell[i+1]);
    }
  }
  lval_del(a);
  return lval_sexpr();
}

//This is similar to C's ternary operator.
lval* builtin_if(lenv* env, lval* a){
  LASSERT_NUM("if", a, 3);
  LASSERT_TYPE("if", a,0,LVAL_NUM);
  LASSERT_TYPE("if", a,1,LVAL_QEXPR);
  LASSERT_TYPE("if", a,2,LVAL_QEXPR);

  //Make both expressions so they can be evaluated
  lval* x;
  a->cell[1]->type = LVAL_SEXPR;
  a->cell[2]->type = LVAL_SEXPR;

  if(a->cell[0]->num){
    //if condition is true eval first expression
    x = eval(env, pop(a,1));
  }else{
    //otherwise evaluate the second expression
    x = eval(env, pop(a, 2));
  }
//Delete a
  lval_del(a);
  return x;
}

lval* builtin_print(lenv* env, lval* a){
  //Print each arg followed by a space
  for(int i=0; i< a->count; i++){
    lval_print(a->cell[i]);
    putchar(' ');
  }

  //Print a newline and delete args
  putchar('\n');
  lval_del(a);

  return lval_sexpr();
}

lval* builtin_error(lenv* env, lval* a){
  LASSERT_NUM("error", a, 1);
  LASSERT_TYPE("error", a, 0, LVAL_STR);
  //Construct error
  lval* err = lval_err(a->cell[0]->str);
  //Delete args and return
  lval_del(a);
  return err;
}

//For each builtin we create a function lval and and symbol lval
//with the given name. We then register them with the environment 
//using lenv_put() 
void lenv_add_builtin(lenv* env, char* name, lbuiltin func){
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(env, k, v);
  lval_del(k); lval_del(v);
}

void len_add_builtins(lenv* env){
  //List functions
  lenv_add_builtin(env, "\\", builtin_lambda);
  lenv_add_builtin(env, "list", builtin_list);
  lenv_add_builtin(env, "head", builtin_head);
  lenv_add_builtin(env, "tail", builtin_tail);
  lenv_add_builtin(env, "join", builtin_join);
  lenv_add_builtin(env, "eval", builtin_eval);
  //Arithmetic Functons
  lenv_add_builtin(env, "+", builtin_add);
  lenv_add_builtin(env, "*", builtin_mul);
  lenv_add_builtin(env, "-", builtin_sub);
  lenv_add_builtin(env, "/", builtin_div);
  lenv_add_builtin(env, "%", builtin_mod);
  //Variable functions
  lenv_add_builtin(env, "def", builtin_def);
  lenv_add_builtin(env, "=", builtin_put);
  lenv_add_builtin(env, "error", builtin_error);
  lenv_add_builtin(env, "load", builtin_load);
  lenv_add_builtin(env, "print", builtin_print);
  //Ordering functions
  lenv_add_builtin(env, ">", builtin_gt);
  lenv_add_builtin(env, "<", builtin_lt);
  lenv_add_builtin(env, "<=", builtin_le);
  lenv_add_builtin(env, ">=", builtin_ge);
  //Equality ops
  lenv_add_builtin(env, "==",builtin_eq);
  lenv_add_builtin(env, "!=",builtin_neq);
  //Condifitional
  lenv_add_builtin(env, "if", builtin_if);
}


//Evaluate a symbolic or quoted expression
lval* eval_sexpr(lenv* env, lval* v){
  //Evaluate the children
  for(int i=0; i < v->count; i++){
    v->cell[i] = eval(env, v->cell[i]);
  }

  //Check for errors
  for(int i=0; i < v->count; i++){
    if(v->cell[i]->type == LVAL_ERR) {
      return take(v,i);
    }
  }

  //Empty eexpression 
  if(v->count == 0) { 
    return v;
  }

  //Single expression
  if(v-> count == 1){
    return eval(env,take(v, 0));
  }

  //Check first element is function after evaluation
  lval* f = pop(v, 0);
  if(f->type != LVAL_FUN){
    lval* err = lval_err(
      "S-Expression Starts with incorrect type."
      "Got %s, Expected %s ", 
      ltype_name(f->type), ltype_name(LVAL_FUN)
      );
    lval_del(v); lval_del(f);
    return err;
  }

  //If it is call function to get result
  lval* result = lval_call(env,f,v);
  lval_del(f);
  return result;
}




int main (int argc, char** argv){
  //Create parsers
  Number = mpc_new("number");
  Symbol = mpc_new("symbol");
  String = mpc_new("string");
  Sexpr = mpc_new("sexpr");
  Qexpr = mpc_new("qexpr");
  Expr = mpc_new("expr");
  Lispy = mpc_new("lispy");
  Comment = mpc_new("comment");
  //Define the above parsers
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                              \
    number: /-?[0-9]+/;                             \
    comment: /;[^\\r\\n]*/;                          \
    symbol: /[a-zA-Z0-9_+\\-*\\/\\\\%=<>!&]+/;        \
    string: /\"(\\\\.|[^\"])*\"/;                      \
    sexpr: '(' <expr>* ')';                             \
    qexpr: '{' <expr>* '}';                              \
    expr: <number> | <symbol> | <sexpr> |                 \
          <qexpr> | <string> | <comment>;                  \
    lispy: /^/<expr>* /$/;                                  \
    ",
      Number, Symbol, String, Comment, Sexpr, Expr, Qexpr, Lispy);

  //Create a new env and add the builtins
  lenv* env = lenv_new();
  assert(env != NULL);
  len_add_builtins(env);


  //Supplied with list of files name
  if(argc >= 2){

    for(int i=1; i < argc; i++){
      //Argument list -- the filename
      lval* args = lval_add(lval_sexpr(), lval_str(argv[i]));

      lval* x = builtin_load(env, args);

      if(x->type == LVAL_ERR){ lval_println(x);}
      lval_del(x);
    }

  }
  //Interactive Prompt
  if(argc == 1){
    
    puts("Welcome to Lispter. Enter a Lisp command and press enter");
    puts("Type 'exit' to quit");
  
    while(1){
      /*Show the prompt and get input from the user*/
      char* input = readline("lispter> ");
      /*Add the input to istory*/
      add_history(input);
  
      
      //When exit is typed we stop the loop/exit the program.
      if(strcmp(input,"exit")==0){
        break;
      }
  
      //Parse the input
      mpc_result_t res;
  
      if(mpc_parse("<stdin>",input, Lispy, &res)){
        lval* x = eval(env, lval_read(res.output));
        lval_println(x);
        lval_del(x);
        
      }else {
        /*If error print the error message*/
        mpc_err_print(res.error);
        mpc_err_delete(res.error);
      }
      /*free the input memory location*/
      free(input);
    }
  }

  lenv_del(env);
  mpc_cleanup(8, Number, Symbol, String, Comment, Sexpr, Qexpr, Expr, Lispy);
  return 0;
}