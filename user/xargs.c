#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAXBUF 1024

int main(int argc, char *argv[]){
    if(argc <= 1){
        fprintf(2, "usage: xargs command args\n");
        exit(1);
    }

    char buf[MAXBUF];
    char* args[MAXARG];
    int n;

    if((n = read(0, buf, MAXBUF)) < 0){
        fprintf(2, "xargs: cannot read\n");
        exit(1);
    }

    int input_args = 0, arg_start = 0;
    while(arg_start < n && buf[arg_start] == '\n'){
        ++arg_start;
    }
    

    for(int i=arg_start+1; i<n; ++i){
        if(buf[i] == '\n'){
            buf[i] = 0;
            args[input_args++] = &buf[arg_start];
            arg_start = i+1;
        }
    }

    if(arg_start < n){
        args[input_args++] = &buf[arg_start];
    }

    char* exec_argv[argc + 1];
    exec_argv[argc] = 0; // exec 需要null终止的argv数组

    for(int i=1; i<argc; ++i){
        exec_argv[i-1] = argv[i];
    }

    for(int i=0; i<input_args; ++i){
        if(fork() == 0){
            exec_argv[argc - 1] = args[i];
            exec(exec_argv[0], exec_argv);
            fprintf(2, "exec %s failed\n", exec_argv[0]);
        }
        wait(0);
    }

    exit(0);
}