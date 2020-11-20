#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h> 
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE 256
#define LIST_SIZE 256

typedef enum STRING_TYPE{
    PROGRAM,
    REDIRECT,
    PIPE,
} string_type;

#define NORMAL 0
#define SPECIAL 1

// i cannot come up with a better name
typedef struct Object {
    char* string;
    int length;
    string_type type;
}  object;

typedef struct STRUCTURE {
    object* list;
    int length;
} structure;

// split the command between pipe
// must deal with cases like `cat tmp1 > tmp2 tmp3`
// support redirect multiple files

int redirect[BUFFER_SIZE]={1};
int redirBufIdx = 0;

structure split(char* start, char* end){
    while(*start == ' ' && start <= end) start ++;

    object* list = (object*) calloc(LIST_SIZE, sizeof(object));
    char* pleft = start;
    int idx = 0;
    string_type type = PROGRAM;
    
    for(char* pc = start; pc <= end; pc += 1){
        if(*pc == '>' || *pc == '<' || *pc == ' ' || *pc == '\0'){
            /*
            check if there is sth after >, error when not
            */
           // first, decide whether it is `fd>` cases
            char save = *pc;
            *pc = '\0';
            // merge or new one
             if(type == REDIRECT){
                list[idx].string = (char*) calloc(BUFFER_SIZE, sizeof(char));
                while(*pleft == ' ') pleft ++;
                strcat(list[idx].string, pleft);
                list[idx].type = type;
                list[idx].length = pc - pleft;
                idx ++;
            }
            else{
                if(save == '>' || save == '<'){
                    char* p;
                    for(p = pc - 1; p >= pleft && *p != ' ' && isdigit(*p); p--);
                    if(*p == ' ') {
                        type = REDIRECT;
                        *pc = save;
                        goto REDIR;
                    }
                }
                if(idx == 0){
                    list[idx].string = (char*) calloc(BUFFER_SIZE, sizeof(char));
                    list[idx].type = type;
                    list[idx].length = 0;
                    idx ++;
                }
                strcat(list[0].string + list[0].length, pleft);
                list[0].length += pc - pleft;
            }
            *pc = save;
            if(*pc == '>' || *pc == '<'){    
                REDIR:;
                // prepare for next one
                type = REDIRECT;
                pleft = pc;
                // case >>, <<
                while(*pc == *(pc + 1) && pc != end){
                    
                    pc ++;
                }
                // avoid cases like ">    tmp"
                pc++;
                while(*pc == ' ' && pc != end) pc ++;
            } 
            else {  
                type = PROGRAM;
                pleft = pc;
            }
        }
    }
    // there can only be 1 program and can be multiple redirect
    structure stru = {.list = list, .length = idx};
    return stru;
}

int parseRedirect(object* token, int type){
    if(type == SPECIAL){
        const int OPEN = 1;
        const int CLOSE = 2;
        int flag = 0;
        char* str = token->string;
        for(int i = 0; i < token->length; i++){
            char c = str[i];
            if(c == '-') flag |= CLOSE;
            if(c == '<' && str[i+1] == '>') flag |= OPEN;
        }
        if(flag & CLOSE){
            // syntax for CLOSE is fd[<|>]&-
            int fd = (str[0] == '>' ? 1 : 0);
            int i = 0;
            while(i < token->length){
                if(isdigit(str[i])) fd = fd * 10 + (str[i] - '0');
                else break;
                i ++;
            }
            if(fd > 1024) return -1; // maximum file descriptors
            close(fd);
        } else if(flag & OPEN){ // OPEN
            // syntax for OPEN is fd<>filename
            int fd = 0;
            int i = 0;
            char filename[BUFFER_SIZE] = {'\0'};
            while(i < token->length){
                if(isdigit(str[i])) fd = fd * 10 + (str[i] - '0');
                else break;
            }
            if(fd > 1024) return -1;
            i += 2;
            int idx = 0;
            while(i < token->length && idx < BUFFER_SIZE){ 
                if(str[i] <= ' ') break;
                else filename[idx++] = str[i];
            }
            // assign specific fd
            int recvFd = open(filename, O_RDWR | O_CREAT);
            dup2(fd, recvFd);
            close(recvFd);
        } else return -1;
    }
    else if(type == NORMAL){
        const int INPUT = 1;
        const int OUTPUT = 2;
        char* str = token->string;
        int i = 0;
        int srcfd = (str[0] == '>' ? 1 : 0), dstfd = 0; // srcfd because > alone is from stdout
        int flag = 0;
        char filename[BUFFER_SIZE] = {'\0'};
        while(i < token->length){
            if(isdigit(str[i])) srcfd = srcfd * 10 + str[i] - '0';
            else break;
            i++;
        }
        if(srcfd > 1024) return -1;
        // determine type
        if(str[i] == '<'){
            flag |= O_RDONLY;
        } else if(str[i] == '>'){
            flag |= O_WRONLY;
        }
        i++;
        // determine 
        DETERMINE:;
        if(str[i] == '&'){
            i ++;
            while(i < token->length){
                if(isdigit(str[i])) dstfd = dstfd * 10 + str[i] - '0';
                else break;
                i++;
            }
            if(dstfd > 1024) return -1;
        } else {
            if(str[i] == str[i-1]){
                flag |= O_APPEND;
                i ++;
                // >>&
                goto DETERMINE;
            }
            int idx = 0;
            while(i < token->length && str[i] == ' ') i++;
            while(i < token->length && idx < BUFFER_SIZE - 1){
                if(str[i] > ' ') filename[idx++] = str[i];
                else break;
                i++;
            }
            /* clear file before opening file */ 
            if(!(flag & O_APPEND)){
                FILE* fp = fopen(filename, "w");
                fclose(fp);
            }
            dstfd = open(filename, flag | O_CREAT, 0755);
           
            if(dstfd == -1){
                fprintf(stderr, "%s.\n", (strerror(errno)));
                exit(-1);
            }
        }
        dup2(dstfd, srcfd);
    }
    return 0;
}

int main(){
    int cnt = 1;
    while(cnt <= 50){
        printf("$ ");
        fflush(stdout);
        /* read command */
        char buf[BUFFER_SIZE] = {'\0'};
        read(0, buf, BUFFER_SIZE - 1);
        puts(buf);
        buf[strlen(buf) - 1] = '\0'; 
        /* command parser */ 
        structure stru = split(buf, buf + strlen(buf));
        #ifdef DEBUG /* print command structur */
        object* struidx = stru.list;
        printf("Structure length: %d\n", stru.length);
        while(struidx != stru.list + stru.length){
            printf("%s:", (((struidx->type) == PROGRAM) ? "PROGRAM" : "REDIRECT"));
            printf("%s\n", struidx->string);
            struidx ++;
        }
        #endif
        // prepare command
        char** cmd = (char**) calloc(BUFFER_SIZE, sizeof(char**));
        int idx = 0;
        *(cmd + idx) = strtok(stru.list[0].string, " ");
        while(idx < BUFFER_SIZE - 1 && *(cmd + idx) != NULL){
            idx ++;
            *(cmd + idx) = strtok(NULL, " ");
        }
        // if `exec` variants to open/close file descriptors then parse inside parent to keep fd open across process
        if(strcmp(cmd[0], "exec") == 0){
            for(int i = 1; i < stru.length; i++){
                /* which means it is not special fd operation */
                if(parseRedirect(&stru.list[i], SPECIAL) < 0) goto NORMAL_CMD;
            }
        } else { /* if it is not `exec` on fd */
                NORMAL_CMD:;
                int pipefd[2];
                if(pipe(pipefd) < 0){
                    fprintf(stderr, "pipe() failed.\n");
                    exit(-1);
                }
                pid_t cmdpid = fork();
                if(cmdpid < 0){
                    fprintf(stderr, "fork() failed.\n");
                    exit(-1);
                } 
                else if(cmdpid > 0) {
                    close(STDIN_FILENO);
                    close(pipefd[1]);
                    dup2(pipefd[0], STDIN_FILENO);
                    char output[BUFFER_SIZE]={'\0'};
                    int rbytes = 0, rb;
                    while(1){
                        rb = read(pipefd[0], output + rbytes, BUFFER_SIZE - rbytes - 1);
                        switch (rb)
                        {
                        case -1:
                            perror("Failed reading!\n");
                            break;
                        case 0:
                            goto DONE_READING;
                        default:
                            rbytes += rb;
                            break;
                        }
                    }
                    DONE_READING:;
                    // parse redirect request
                    for(int i = 1; i < stru.length; i++){
                        if(parseRedirect(&stru.list[i], NORMAL) < 0) exit(-1);
                    }
                    // put output to default stdout
                    write(STDOUT_FILENO, output, rbytes);
                    int retcmd;
                    waitpid(cmdpid, &retcmd, 0);
                    if(retcmd != 0){
                        fprintf(stderr, "Error in executing command. Please make sure your syntax correct.\n");
                    }
                    exit(0);
                } 
                else{
                    close(STDOUT_FILENO);
                    close(STDERR_FILENO);
                    close(pipefd[0]);
                    dup2(pipefd[1], STDERR_FILENO);
                    dup2(pipefd[1], STDOUT_FILENO);

                    // because execvp only replace program when it SUCCEEDED
                    if(execvp(cmd[0], cmd) == -1){
                        fflush(stderr);
                        close(pipefd[1]);
                        exit(-1);
                    }
                }
            }
    }
}