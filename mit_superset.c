// mit_superset_final.c
// Compile: gcc mit_superset_final.c -lm -o mit_superset_final
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <webview.h>
#include <wiringPi.h>

#define MAX_TOKENS 65536
#define MAX_VARS 1024
#define MAX_VECTOR 64
#define MAX_TASKS 64

typedef enum {TOKEN_NUMBER,TOKEN_IDENTIFIER,TOKEN_OPERATOR,TOKEN_KEYWORD,TOKEN_PUNCTUATION,TOKEN_EOF,TOKEN_BUILTIN} TokenType;
typedef struct {TokenType type; char text[64];} Token;
Token tokens[MAX_TOKENS]; int token_count=0,current_token=0;

typedef struct {char name[64]; double value;} Variable;
Variable vars[MAX_VARS]; int var_count=0;

typedef struct {double values[MAX_VECTOR]; int length;} Vector;
Vector vectors[MAX_VARS]; // vector storage

typedef struct {char cmd[128]; void* arg;} Task;
Task tasks[MAX_TASKS]; int task_count=0;

Token* next_token(){return &tokens[current_token++];}
Token* peek_token(){return &tokens[current_token];}
void add_token(TokenType type,const char* text){if(token_count<MAX_TOKENS){tokens[token_count].type=type; strncpy(tokens[token_count].text,text,63); token_count++;}}

int is_keyword(const char* s){return strcmp(s,"let")==0||strcmp(s,"print")==0||strcmp(s,"task")==0||strcmp(s,"async")==0;}

void tokenize(const char* line){
    const char* p=line;
    while(*p){
        if(isspace(*p)){p++; continue;}
        if(isalpha(*p)||*p=='_'){
            char buf[64]; int n=0;
            while(isalnum(*p)||*p=='_') buf[n++]=*p++;
            buf[n]='\0';
            if(is_keyword(buf)) add_token(TOKEN_KEYWORD,buf);
            else if(strcmp(buf,"read")==0||strcmp(buf,"write")==0||strcmp(buf,"delete")==0||strcmp(buf,"exec")==0||strcmp(buf,"send")==0||strcmp(buf,"simulate")==0)
                add_token(TOKEN_BUILTIN,buf);
            else add_token(TOKEN_IDENTIFIER,buf);
        }
        else if(isdigit(*p)||(*p=='.' && isdigit(*(p+1)))){
            char buf[64]; int n=0;
            while(isdigit(*p)||*p=='.') buf[n++]=*p++;
            buf[n]='\0'; add_token(TOKEN_NUMBER,buf);
        }
        else if(strchr("+-*/%=(),",*p)){
            char buf[2]; buf[0]=*p++; buf[1]='\0'; add_token(TOKEN_OPERATOR,buf);
        }
        else p++;
    }
    add_token(TOKEN_EOF,"EOF");
}

// ---------------- Expression evaluator ----------------
double parse_expression();
double parse_factor(){
    Token* t=next_token();
    if(t->type==TOKEN_NUMBER) return atof(t->text);
    if(t->type==TOKEN_IDENTIFIER){
        if(strcmp(t->text,"sin")==0){next_token(); double arg=parse_expression(); return sin(arg);}
        if(strcmp(t->text,"cos")==0){next_token(); double arg=parse_expression(); return cos(arg);}
        if(strcmp(t->text,"tan")==0){next_token(); double arg=parse_expression(); return tan(arg);}
        if(strcmp(t->text,"sqrt")==0){next_token(); double arg=parse_expression(); return sqrt(arg);}
        if(strcmp(t->text,"pow")==0){next_token(); double a=parse_expression(); double b=parse_expression(); return pow(a,b);}
        for(int i=0;i<var_count;i++) if(strcmp(vars[i].name,t->text)==0) return vars[i].value;
        return 0;
    }
    if(t->type==TOKEN_OPERATOR && strcmp(t->text,"(")==0){
        double val=parse_expression();
        next_token(); return val;
    }
    return 0;
}
double parse_term(){
    double val=parse_factor();
    while(peek_token()->type==TOKEN_OPERATOR && (strcmp(peek_token()->text,"*")==0 || strcmp(peek_token()->text,"/")==0 || strcmp(peek_token()->text,"%")==0)){
        Token* op=next_token();
        double right=parse_factor();
        if(strcmp(op->text,"*")==0) val*=right;
        else if(strcmp(op->text,"/")==0) val/=right;
        else if(strcmp(op->text,"%")==0) val=(int)val%(int)right;
    }
    return val;
}
double parse_expression(){
    double val=parse_term();
    while(peek_token()->type==TOKEN_OPERATOR && (strcmp(peek_token()->text,"+")==0 || strcmp(peek_token()->text,"-")==0)){
        Token* op=next_token();
        double right=parse_term();
        if(strcmp(op->text,"+")==0) val+=right;
        else val-=right;
    }
    return val;
}

// ---------------- Task system ----------------
void* run_task(void* arg){Task* t=(Task*)arg; printf("[Task] Running: %s\n",t->cmd); return NULL;}
void add_task(const char* cmd){strcpy(tasks[task_count].cmd,cmd); pthread_t th; pthread_create(&th,NULL,run_task,&tasks[task_count]); task_count++;}

// ---------------- Statements ----------------
void execute_line(){
    Token* t=peek_token();
    if(t->type==TOKEN_KEYWORD && strcmp(t->text,"let")==0){
        next_token(); Token* var_name=next_token(); next_token(); double val=parse_expression();
        int found=0; for(int i=0;i<var_count;i++) if(strcmp(vars[i].name,var_name->text)==0){vars[i].value=val; found=1;}
        if(!found){strcpy(vars[var_count].name,var_name->text); vars[var_count].value=val; var_count++;}
    }
    else if(t->type==TOKEN_KEYWORD && strcmp(t->text,"print")==0){next_token(); double val=parse_expression(); printf("%g\n",val);}
    else if(t->type==TOKEN_KEYWORD && strcmp(t->text,"task")==0){next_token(); Token* cmd=next_token(); add_task(cmd->text);}
    else if(t->type==TOKEN_BUILTIN){
        Token* b=next_token(); char filename[128]; char content[256];
        if(strcmp(b->text,"read")==0){printf("File: "); fgets(filename,128,stdin); filename[strcspn(filename,"\n")]=0; FILE* f=fopen(filename,"r"); if(f){while(fgets(content,256,f)) printf("%s",content); fclose(f);}}
        else if(strcmp(b->text,"write")==0){printf("File: "); fgets(filename,128,stdin); filename[strcspn(filename,"\n")]=0; printf("Content: "); fgets(content,256,stdin); content[strcspn(content,"\n")]=0; FILE* f=fopen(filename,"w"); if(f){fprintf(f,"%s",content); fclose(f);}}
        else if(strcmp(b->text,"delete")==0){printf("Delete file: "); fgets(filename,128,stdin); filename[strcspn(filename,"\n")]=0; remove(filename);}
        else if(strcmp(b->text,"exec")==0){printf("Command: "); fgets(content,256,stdin); content[strcspn(content,"\n")]=0; system(content);}
        else if(strcmp(b->text,"send")==0){printf("Quantum send: "); fgets(content,256,stdin); content[strcspn(content,"\n")]=0; printf("Sent: %s\n",content);}
        else if(strcmp(b->text,"simulate")==0){printf("Quantum simulate: "); fgets(content,256,stdin); content[strcspn(content,"\n")]=0; printf("Simulated: %s\n",content);}
    }
}

// ---------------- Main ----------------
int main(int argc,char* argv[]){
    if(argc<2){printf("Usage: %s <file.mits>\n",argv[0]); return 1;}
    FILE* f=fopen(argv[1],"r"); if(!f){printf("File not found\n"); return 1;}
    char line[256];
    while(fgets(line,256,f)){
        token_count=0; current_token=0;
        tokenize(line);
        while(peek_token()->type!=TOKEN_EOF) execute_line();
    }
    fclose(f);
    sleep(1); // wait async tasks
    return 0;
}
// Vector support snippet for MitScripts
typedef struct {double values[MAX_VECTOR]; int length;} Vector;
Vector vectors[MAX_VARS]; // store vector variables

int vector_var_index(const char* name){
    for(int i=0;i<var_count;i++) if(strcmp(vars[i].name,name)==0) return i;
    return -1;
}

// Parse vector literal: [1,2,3]
Vector parse_vector_literal(){
    Vector v={.length=0};
    next_token(); // skip '['
    while(peek_token()->type!=TOKEN_OPERATOR || strcmp(peek_token()->text,"]")!=0){
        if(peek_token()->type==TOKEN_NUMBER){
            v.values[v.length++] = atof(next_token()->text);
            if(peek_token()->type==TOKEN_OPERATOR && strcmp(peek_token()->text,",")==0) next_token();
        }
    }
    next_token(); // skip ']'
    return v;
}

// Vector addition: v1 + v2
Vector vector_add(Vector a, Vector b){
    Vector res={.length=a.length};
    for(int i=0;i<a.length;i++) res.values[i]=a.values[i]+b.values[i];
    return res;
}

// Vector subtraction: v1 - v2
Vector vector_sub(Vector a, Vector b){
    Vector res={.length=a.length};
    for(int i=0;i<a.length;i++) res.values[i]=a.values[i]-b.values[i];
    return res;
}

// Scalar multiplication: v * 2
Vector vector_scalar_mul(Vector a, double scalar){
    Vector res={.length=a.length};
    for(int i=0;i<a.length;i++) res.values[i]=a.values[i]*scalar;
    return res;
}

// Scalar division: v / 2
Vector vector_scalar_div(Vector a, double scalar){
    Vector res={.length=a.length};
    for(int i=0;i<a.length;i++) res.values[i]=a.values[i]/scalar;
    return res;
}

// Print vector
void print_vector(Vector v){
    printf("[");
    for(int i=0;i<v.length;i++){printf("%g",v.values[i]); if(i<v.length-1) printf(",");}
    printf("]\n");
}

#define LOCALSTORAGE_FILE "mitscripts_localstorage.txt"
#define MAX_LS 1024

typedef struct { char key[64]; char value[256]; } LSItem;
LSItem ls_data[MAX_LS]; int ls_count = 0;

// Load local storage from file
void LS_load() {
    FILE* f = fopen(LOCALSTORAGE_FILE, "r");
    if(f) {
        char k[64], v[256];
        while(fscanf(f, "%63s %255[^\n]", k, v) == 2) {
            strcpy(ls_data[ls_count].key, k);
            strcpy(ls_data[ls_count].value, v);
            ls_count++;
        }
        fclose(f);
    }
}

// Save local storage to file
void LS_save() {
    FILE* f = fopen(LOCALSTORAGE_FILE, "w");
    if(f) {
        for(int i = 0; i < ls_count; i++)
            fprintf(f, "%s %s\n", ls_data[i].key, ls_data[i].value);
        fclose(f);
    }
}

// Set a key/value pair
void LS_setItem(const char* k, const char* v) {
    for(int i = 0; i < ls_count; i++) {
        if(strcmp(ls_data[i].key, k) == 0) {
            strcpy(ls_data[i].value, v);
            LS_save();
            return;
        }
    }
    strcpy(ls_data[ls_count].key, k);
    strcpy(ls_data[ls_count].value, v);
    ls_count++;
    LS_save();
}

// Get a value by key
const char* LS_getItem(const char* k) {
    for(int i = 0; i < ls_count; i++)
        if(strcmp(ls_data[i].key, k) == 0) return ls_data[i].value;
    return NULL;
}

// Remove a key
void LS_removeItem(const char* k) {
    for(int i = 0; i < ls_count; i++) {
        if(strcmp(ls_data[i].key, k) == 0) {
            for(int j = i; j < ls_count-1; j++)
                ls_data[j] = ls_data[j+1];
            ls_count--;
            LS_save();
            return;
        }
    }
}

// Clear all keys
void LS_clear() {
    ls_count = 0;
    LS_save();
}

// Initialize on runtime start
void LS_init() { LS_load(); }

// ---------------- HTML Embedding ----------------
// Create an HTML window and render content
void htmlcontent(const char* html) {
    // Parameters: title, HTML string, width, height, resizable
    webview("MitScripts HTML Window", html, 800, 600, 1);
}

// Example usage
int main() {
    const char* html = "<html><body>"
                       "<h1>Hello from MitScripts C!</h1>"
                       "<img src='https://via.placeholder.com/150'><br>"
                       "<video width='300' controls><source src='video.mp4' type='video/mp4'></video><br>"
                       "<audio controls><source src='audio.mp3' type='audio/mpeg'></audio><br>"
                       "<button onclick='alert(\"Button Clicked!\")'>Click Me</button>"
                       "</body></html>";

    htmlcontent(html);
    return 0;
}

// ---------------- Hardware ----------------
void HW_init() {
    if (wiringPiSetup() == -1) {
        printf("Failed to initialize WiringPi\n");
        exit(1);
    }
    printf("[Hardware Initialized]\n");
}

// LED control
void HW_led(int pin, int state) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, state ? HIGH : LOW);
    printf("[LED] Pin %d -> %s\n", pin, state ? "ON" : "OFF");
}

// Digital write
void HW_digitalWrite(int pin, int val) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, val);
    printf("[Digital Write] Pin %d -> %d\n", pin, val);
}

// Digital read
int HW_digitalRead(int pin) {
    pinMode(pin, INPUT);
    int val = digitalRead(pin);
    printf("[Digital Read] Pin %d -> %d\n", pin, val);
    return val;
}

// Servo control (using PWM)
void HW_servo(int pin, int deg) {
    pinMode(pin, PWM_OUTPUT);
    int pwm_val = (int)(1024 * deg / 180.0); // simple mapping
    pwmWrite(pin, pwm_val);
    printf("[Servo] Pin %d -> %d degrees\n", pin, deg);
}

