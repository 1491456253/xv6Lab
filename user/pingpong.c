#include "kernel/types.h"
#include "user.h"
int main(int argc,char* argv[]){
    if(argc!=1){
        printf("Please enter correct arguments!\n");
        exit(1);
    }
    else{
        int pipe1[2];//用来存储由 pipe 系统调用创建的管道的文件描述符
        int pipe2[2];
        pipe(pipe1);
        pipe(pipe2);
        int pid;
        if((pid=fork())<0){//创建子进程不成功
            printf("failed to create new process\n");
            exit(1);
        }
        else if(pid==0){
            close(pipe1[1]);
            close(pipe2[0]);      
            char buf[10];	   
            read(pipe1[0],buf,4);  
            close(pipe1[0]);
            printf("%d: received %s\n",getpid(),buf);
            write(pipe2[1],"pong",strlen("pong"));
            close(pipe2[1]);
        }
        else{   
            close(pipe1[0]);
            close(pipe2[1]);      
            write(pipe1[1],"ping",strlen("ping")); 
            close(pipe1[1]);     	
            char buf[10];   
            read(pipe2[0],buf,4);
            printf("%d: received %s\n",getpid(),buf);
            close(pipe2[0]);
        }  
        exit(0);
    }
}