#include "mpc.h"
#include <stdio.h>
#include <string.h>


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
enum{LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR};
//Enumeration of possible error types
enum{LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM};

typedef struct lval{
  int type;
  long num;
  /*We stire error and symbol type as string data*/
  char* sym;
  char* err;

  /*Count and pointers to "lval*" */
  int count;
  struct lval** cell;

}lval;

//Forward declarations
void lval_print(lval* v);
lval* eval_sexpr(lval* v);

lval* lval_num(long x){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

lval* lval_err(char* m){
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m)+1);
  strcpy(v->err,m);
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
    if(strcmp(t->children[i]->contents, "(") == 0) {continue; }
    if(strcmp(t->children[i]->contents, ")") == 0) {continue; }
    if(strcmp(t->children[i]->contents, "{") == 0) {continue; }
    if(strcmp(t->children[i]->contents, "}") == 0) {continue; }
    if(strcmp(t->children[i]->tag, "regex") == 0) {continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
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

lval* eval(lval* v){
  //Evaluate S-Expression
  if(v->type == LVAL_SEXPR){
    return eval_sexpr(v);
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
lval* builtin_op(lval* a, char* op){
  //Make sure all arguments are numbers
  for(int i = 0; i < a->count; i++){
    if(a->cell[i]->type != LVAL_NUM){
      lval_del(a);
      return lval_err("Cannot operate on non-number");     
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


//Macro to help with error checking
#define LASSERT(args, cond, err) \
  if(!(cond)){ \
    lval_del(args); \
    return lval_err(err);\
  }

lval* builtin_head(lval* a){
  //Error checking
  LASSERT(a, a->count == 1, 
    "Too many arguments passed to function 'head'");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
  "Function head passed incorrect type" );

  LASSERT(a, a->cell[0]->count !=0, 
    "Function head passed{}!");

  //Otherwise take first argument
  lval* v = take(a, 0);
  //Delete all elements that are not head and return
  while(v->count > 1) { lval_del(pop(v,1));}

  return v;
}

lval* builtin_tail(lval* a){
  //Check for errors
  LASSERT(a, a->count == 1, 
    "'Tail' passed too many args.");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
    "'Tail' passed the wrong types.");
  LASSERT(a, a->cell[0]->count != 0,
    "Function tail passed {}!");
 
  //Take first argument
  lval* v = take(a,0);
  //Delete first element and return 
  lval_del(pop(v,0));

  return v;
}

lval* builtin_list(lval* a){
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lval* a){
  LASSERT(a, a->count == 1, 
    "Function 'eval' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR, 
    "FUnction 'eval' passed incorrect type");

  lval* x = take(a, 0);
  x->type = LVAL_SEXPR;
  return eval(x);
}

lval* lval_join(lval* x, lval* y){
  //For each cell in y add it to x
  while(y->count){
    x = lval_add(x, pop(y,0));
  }
  lval_del(y);
  return x;
}


lval* builtin_join(lval* a){
  for(int i = 0; i < a->count; i++){
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR, 
      "Function 'join' passed incorrect type.");
  }

  lval* x = pop(a,0);
  while(a->count){
    x = lval_join(x, pop(a,0));
  }

  lval_del(a);
  return x;
}

lval* builtin(lval* a, char* func){
  if (strcmp("list", func) == 0) {
    return builtin_list(a);
  }
  if (strcmp("head", func) == 0) {
    return builtin_head(a);
  }
  if (strcmp("tail", func) == 0) {
    return builtin_tail(a);
  }
  if (strcmp("join", func) == 0) {
    return builtin_join(a);
  }
  if (strcmp("eval", func) == 0) {
    return builtin_eval(a);
  }
  if(strstr("+-/*", func)){ 
    return builtin_op(a, func);
  }

  lval_del(a);
  return lval_err("Unknown Function");

}

//Evaluate a symbolic or quoted expression
lval* eval_sexpr(lval* v){
  //Evaluate the children
  for(int i=0; i < v->count; i++){
    v->cell[i] = eval(v->cell[i]);
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
    return take(v, 0);
  }

  //Check first element is symbol
  lval* f = pop(v, 0);
  if(f->type != LVAL_SYM){
    lval_del(f); lval_del(v);
    return lval_err("S-expression does not start with a symbo");
  }

  //Call builtin with operator
  lval* result = builtin(v, f->sym);
  lval_del(f);
  return result;
}



int main (int argc, char** argv){
  //Create some parsers
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new ("lispy");
  //Define the above parsers
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                           \
    number: /-?[0-9]+/;                          \
    symbol: \"list\" | \"head\" | \"tail\" |     \
            \"join\" | \"eval\" |                \
            '+' |'-' | '*'| '/' | '%';            \
    sexpr: '(' <expr>* ')';                         \
    qexpr: '{' <expr>* '}';                          \
    expr: <number> | <symbol> | <sexpr> | <qexpr>;   \
    lispy: /^/<expr>* /$/;                            \
    ",
      Number, Symbol, Sexpr, Expr, Qexpr, Lispy);
  
  puts("Welcome to Lipsy. Enter something and press enter");
  puts("Type 'exit' to quit");

  while(1){
    /*Show the prompt and get input from the user*/
    char* input = readline("lispter> ");
    /*Add the input to istory*/
    add_history(input);

    
    //When exit is typed we stop the loop.
    if(strcmp(input,"exit")==0){
      break;
    }

    //Parse the input
    mpc_result_t res;

    if(mpc_parse("<stdin>",input, Lispy, &res)){
      lval* x = eval(lval_read(res.output));
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
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  return 0;
}