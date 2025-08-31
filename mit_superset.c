// mit_superset_production.c
// Compile: gcc mit_superset_production.c -o mit_superset
// Run: ./mit_superset script.mits

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_TOKENS 65536
#define MAX_AST_NODES 65536
#define MAX_LINE 1024
#define MAX_VECTOR_SIZE 64
#define MAX_VARS 1024
#define MAX_TASKS 256

typedef enum {TOKEN_IDENTIFIER,TOKEN_NUMBER,TOKEN_STRING,TOKEN_OPERATOR,TOKEN_KEYWORD,TOKEN_PUNCTUATION,TOKEN_VECTOR,TOKEN_ASYNCFOR,TOKEN_TASK,TOKEN_TRIPLESTRING,TOKEN_BUILTIN,TOKEN_EOF} TokenType;
typedef struct {TokenType type; char text[256];} Token;
Token tokens[MAX_TOKENS]; int token_count=0,current_token=0;

typedef enum {AST_PRINT,AST_NUMBER,AST_STRING,AST_VECTOR,AST_BINARY,AST_IDENTIFIER,AST_ASYNCFOR,AST_TASK,AST_PROGRAM,AST_BUILTIN_NODE,AST_FUNCTION,AST_RETURN,AST_IF,AST_WHILE,AST_FOR,AST_UNKNOWN} ASTType;

typedef struct ASTNode{
    ASTType type; char value[256];
    struct ASTNode* left; struct ASTNode* right;
    struct ASTNode* body[MAX_AST_NODES]; int body_count;
} ASTNode;

ASTNode ast_nodes[MAX_AST_NODES]; int ast_count=0;

Token* next_token(){return &tokens[current_token++];}
Token* peek_token(){return &tokens[current_token];}
void add_token(TokenType type,const char* text){if(token_count<MAX_TOKENS){tokens[token_count].type=type; strncpy(tokens[token_count].text,text,255); token_count++;}}
int is_keyword(const char* s){const char* k[]={"let","const","function","return","if","else","for","while","async","task","print",NULL}; for(int i=0;k[i];i++) if(strcmp(s,k[i])==0) return 1; return 0;}

void tokenize_line(char* line){
    char* p=line;
    while(*p){
        if(isspace(*p)){p++; continue;}
        if(isalpha(*p)||*p=='_'){
            char buf[256]; int n=0;
            while(isalnum(*p)||*p=='_') buf[n++]=*p++;
            buf[n]='\0';
            if(is_keyword(buf)) add_token(TOKEN_KEYWORD,buf);
            else if(strcmp(buf,"read")==0||strcmp(buf,"write")==0||strcmp(buf,"delete")==0||strcmp(buf,"exec")==0||strcmp(buf,"send")==0||strcmp(buf,"simulate")==0)
                add_token(TOKEN_BUILTIN,buf);
            else add_token(TOKEN_IDENTIFIER,buf);
        }
        else if(isdigit(*p)){
            char buf[256]; int n=0;
            while(isdigit(*p)) buf[n++]=*p++;
            buf[n]='\0';
            add_token(TOKEN_NUMBER,buf);
        }
        else if(*p=='\"'){
            p++;
            char buf[256]; int n=0;
            while(*p && *p!='\"') buf[n++]=*p++;
            if(*p=='\"') p++;
            buf[n]='\0';
            add_token(TOKEN_STRING,buf);
        }
        else if(*p=='+'||*p=='-'||*p=='*'||*p=='/'||*p=='='||*p=='<'||*p=='>'||*p=='!'||*p=='&'||*p=='|'){
            char buf[2]; buf[0]=*p++; buf[1]='\0'; add_token(TOKEN_OPERATOR,buf);
        }
        else{char buf[2]; buf[0]=*p++; buf[1]='\0'; add_token(TOKEN_PUNCTUATION,buf);}
    }
}

void tokenize_file(const char* filename){
    FILE* f=fopen(filename,"r"); if(!f){perror("File open error"); exit(1);}
    char line[MAX_LINE];
    while(fgets(line,MAX_LINE,f)){
        char* t=line; while(*t==' '||*t=='\t') t++;
        if(strncmp(t,"Vector(",7)==0) add_token(TOKEN_VECTOR,"Vector");
        else if(strncmp(t,"async for",9)==0) add_token(TOKEN_ASYNCFOR,"async for");
        else if(strncmp(t,"task",4)==0) add_token(TOKEN_TASK,"task");
        else if(strncmp(t,"\"\"\"",3)==0) add_token(TOKEN_TRIPLESTRING,"triple");
        else tokenize_line(line);
    }
    add_token(TOKEN_EOF,"EOF"); fclose(f);
}

// -------------------- AST --------------------
ASTNode* create_node(ASTType type,const char* val){ASTNode* n=&ast_nodes[ast_count++]; n->type=type; strncpy(n->value,val,255); n->left=n->right=NULL; n->body_count=0; return n;}

// -------------------- Parser --------------------
ASTNode* parse_statement(); ASTNode* parse_program();
ASTNode* parse_print(){ASTNode* n=create_node(AST_PRINT,"print"); Token* t=next_token(); if(t->type==TOKEN_NUMBER||t->type==TOKEN_STRING||t->type==TOKEN_IDENTIFIER) n->left=create_node(AST_STRING,t->text); return n;}
ASTNode* parse_vector(){ASTNode* n=create_node(AST_VECTOR,"Vector"); return n;}
ASTNode* parse_asyncfor(){ASTNode* n=create_node(AST_ASYNCFOR,"async for"); return n;}
ASTNode* parse_task(){ASTNode* n=create_node(AST_TASK,"task"); return n;}
ASTNode* parse_builtin(){ASTNode* n=create_node(AST_BUILTIN_NODE,"builtin"); Token* t=next_token(); if(t) strncpy(n->value,t->text,255); return n;}
ASTNode* parse_statement(){
    Token* t=peek_token();
    if(strcmp(t->text,"print")==0) return parse_print();
    if(strcmp(t->text,"Vector")==0) return parse_vector();
    if(strcmp(t->text,"async for")==0) return parse_asyncfor();
    if(strcmp(t->text,"task")==0) return parse_task();
    if(t->type==TOKEN_BUILTIN) return parse_builtin();
    next_token(); return create_node(AST_UNKNOWN,"unknown");
}
ASTNode* parse_program(){ASTNode* r=create_node(AST_PROGRAM,"program"); while(peek_token()->type!=TOKEN_EOF){ASTNode* s=parse_statement(); r->body[r->body_count++]=s;} return r;}

// -------------------- Runtime --------------------
typedef struct{int values[MAX_VECTOR_SIZE]; int length;} VectorVal;
typedef struct{char name[64]; int value;} Variable;
Variable vars[MAX_VARS]; int var_count=0;
VectorVal vector_create(int* arr,int len){VectorVal v; v.length=len; for(int i=0;i<len;i++) v.values[i]=arr[i]; return v;}

// Built-in functions
void builtin_exec(const char* cmd){int status=system(cmd); if(status==-1) printf("Command failed\n");}
void builtin_write(const char* filename,const char* content){FILE* f=fopen(filename,"w"); if(!f){printf("Write failed\n"); return;} fprintf(f,"%s",content); fclose(f);}
void builtin_read(const char* filename){FILE* f=fopen(filename,"r"); if(!f){printf("Read failed\n"); return;} char buf[1024]; while(fgets(buf,1024,f)) printf("%s",buf); fclose(f);}
void builtin_delete(const char* filename){if(remove(filename)==0) printf("Deleted %s\n",filename); else printf("Delete failed\n");}
void builtin_send(const char* instr){printf("Quantum send: %s\n",instr);}
void builtin_simulate(const char* instr){printf("Quantum simulate: %s\n",instr);}

// -------------------- Executor --------------------
void execute_node(ASTNode* n){
    if(!n) return;
    switch(n->type){
        case AST_PRINT: if(n->left) printf("%s\n",n->left->value); break;
        case AST_VECTOR: printf("<Vector>\n"); break;
        case AST_ASYNCFOR: printf("<AsyncFor>\n"); break;
        case AST_TASK: printf("<Task>\n"); break;
        case AST_BUILTIN_NODE:
            if(strcmp(n->value,"exec")==0){char c[256]; printf("Command: "); fgets(c,256,stdin); c[strcspn(c,"\n")]=0; builtin_exec(c);}
            else if(strcmp(n->value,"read")==0){char f[256]; printf("Read file: "); fgets(f,256,stdin); f[strcspn(f,"\n")]=0; builtin_read(f);}
            else if(strcmp(n->value,"write")==0){char f[256],ct[256]; printf("Write file: "); fgets(f,256,stdin); f[strcspn(f,"\n")]=0; printf("Content: "); fgets(ct,256,stdin); ct[strcspn(ct,"\n")]=0; builtin_write(f,ct);}
            else if(strcmp(n->value,"delete")==0){char f[256]; printf("Delete file: "); fgets(f,256,stdin); f[strcspn(f,"\n")]=0; builtin_delete(f);}
            else if(strcmp(n->value,"send")==0){char c[256]; printf("Send instr: "); fgets(c,256,stdin); c[strcspn(c,"\n")]=0; builtin_send(c);}
            else if(strcmp(n->value,"simulate")==0){char c[256]; printf("Simulate instr: "); fgets(c,256,stdin); c[strcspn(c,"\n")]=0; builtin_simulate(c);}
            break;
        default: break;
    }
    for(int i=0;i<n->body_count;i++) execute_node(n->body[i]);
}

void execute_program(ASTNode* root){for(int i=0;i<root->body_count;i++) execute_node(root->body[i]);}

// -------------------- Main --------------------
int main(int argc,char* argv[]){
    if(argc<2){printf("Usage: %s <file.mits>\n",argv[0]); return 1;}
    tokenize_file(argv[1]);
    ASTNode* root=parse_program();
    execute_program(root);
    return 0;
}
