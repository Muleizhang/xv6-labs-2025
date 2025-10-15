#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int isLetterOrNum(char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return 1;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  char *mem;
  while (1) {
    mem = sbrk(PGSIZE);
    if (mem == SBRK_ERROR) {
      printf("sbrk error\n");
      exit(1);
    }
    int start = 0;
    char *target = "This may help.";
    while(start < PGSIZE){
      if (memcmp(mem + start, target, strlen(target)) == 0) {
        printf("%s\n",mem + start + strlen(target) + 2);
      }
      start++;
    }

  }
  exit(0);
}
