#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

void find(char *path, char *pattern);
void find_exec(char *path, char *pattern, char **cmd, int cmdlen);

int
main(int argc, char **argv)
{
  if(argc >= 4){ // find directory pattern --exec cmd
    if (strcmp(argv[3], "-exec") == 0){
      char *buf[MAXARG];
      
      for (int i = 0; i < argc - 4; i++) {
        buf[i] = argv[i + 4];
      }
      find_exec(argv[1], argv[2], buf, argc - 4);
      exit(0);
    }
  }

  if(argc != 3){
    fprintf(2, "usage: find [directory] [pattern]\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  buf[sizeof(buf)-1] = '\0';
  return buf;
}

void
find(char *path, char *pattern)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
    if (strcmp(fmtname(path), pattern) == 0) {
        printf("%s\n", path);
    }
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      
      // pass . and .. entries
      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
          continue;
      
      // get full path
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0; // null terminate
      
      // get file status
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }

      if (st.type == T_FILE && strcmp(de.name, pattern) == 0) {
        printf("%s\n", buf);
      }
      
      if (st.type == T_DIR) {
        find(buf, pattern);
      }
      
      *p = 0;
    }
    break;
  }
  close(fd);
}


void
find_exec(char *path, char *pattern, char **cmd, int cmdlen)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_DEVICE:
  case T_FILE:
    if (strcmp(fmtname(path), pattern) == 0) {
        printf("%s\n", path);
    }
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
      printf("ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      
      // pass . and .. entries
      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
          continue;
      
      // get full path
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0; // null terminate
      
      // get file status
      if(stat(buf, &st) < 0){
        printf("ls: cannot stat %s\n", buf);
        continue;
      }

      if (st.type == T_FILE && strcmp(de.name, pattern) == 0) {
        cmd[cmdlen] = buf;
        cmd[cmdlen + 1] = 0;
        int pid = fork();
        if (pid < 0) {
          fprintf(2, "find: fork failed\n");
        } else if (pid == 0) {
          exec(cmd[0], cmd);
          // busprocess run error
          fprintf(2, "find: subprocess [%s] run error\n", cmd[0]);
          exit(1);
        } else {
          wait((int *)0);
        }

      }
      
      if (st.type == T_DIR) {
        find_exec(buf, pattern, cmd, cmdlen);
      }
      
      *p = 0;
    }
    break;
  }
  close(fd);
}

