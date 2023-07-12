#include "kernel/types.h"
#include "user.h"
int main(int argc,char* argv[])
{
   if(argc!=2 )
   {
      fprintf(2,"Please enter the correct parameters!\n");
      exit(1);
   }
   int duration=atoi(argv[1]);
   sleep(duration);
   exit(0);
}
