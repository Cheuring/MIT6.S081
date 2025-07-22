#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

char *prefix = "my very very very secret pw is:";
int
main(int argc, char *argv[])
{
  // your code here.  you should write the secret to fd 2 using write
  // (e.g., write(2, secret, 8)

  char *end, *secret;
  int len = strlen(prefix);
  while((end = sbrk(PGSIZE)) != (char*)-1){
    // null terminate string
    end[len] = 0;
    // in kfree, we reassigned end's first pointer a.k.a. the first eight bytes
    // so we skip it
    if(strcmp(end + 8, prefix + 8) == 0){
      secret = end + 32;
      write(2, secret, 8);
      exit(0);
    }
  }
  exit(1);
}
