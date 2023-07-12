#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
int solution(int p1[2]){
    int prime;
    int p2[2];
    pipe(p2);
    if (read(p1[0], &prime, 4) != 4) {
        exit(1);
    }
    printf("prime %d\n", prime);
    if(fork()==0){
        int tmp;
        while (read(p1[0], &tmp, 4) == 4) {
                if (tmp % prime) write(p2[1], &tmp, 4);
            }
        close(p2[1]);
        close(p1[0]);//p1管道彻底被关了，重新开一个
        solution(p2);
        exit(0);
    }
    else{
        close(p1[0]);
      close(p2[1]);
      close(p2[0]);      
      wait(0);
      exit(0);
    }

}
int main(int argc,char* argv[]){
    int p1[2];
    pipe(p1);
    for(int i=2;i<36;i++){
        if (write(p1[1], &i, 4) != 4) {
                fprintf(2, "Failed to write! %d into the pipe\n", i);
                exit(1);
        }
    }
    close(p1[1]);
    solution(p1);
    exit(0);
    return 0;
}