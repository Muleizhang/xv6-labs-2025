#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"    // for open, read, close, printf, fprintf, exit
#include "kernel/fcntl.h" // for O_RDONLY

// 分隔符集合
const char *separators = " -\r\t\n./,"; 

void sixfive(int fd) {
    char buf[1];
    int current_num = 0;
    int is_reading_number = 0; // 0: 寻找数字, 1: 正在读取数字

    while (read(fd, buf, 1) > 0) {
        char c = buf[0];
        
        if (c >= '0' && c <= '9') {
            // 是数字字符
            current_num = current_num * 10 + (c - '0');
            is_reading_number = 1;
        } else if (strchr(separators, c) != 0) {
            // 是分隔符
            if (is_reading_number) {
                // 数字结束，进行检查
                if (current_num != 0 && (current_num % 5 == 0 || current_num % 6 == 0)) {
                    printf("%d\n", current_num); // 使用 %d 
                }
                // 重置状态
                current_num = 0;
                is_reading_number = 0;
            }
        } else {
             // 既不是数字也不是分隔符，视为非数字字符
             current_num = 0;
             is_reading_number = 0;
        }
    }
    
    // 处理文件末尾的数字 (EOF)
    if (is_reading_number) {
        if (current_num != 0 && (current_num % 5 == 0 || current_num % 6 == 0)) {
            printf("%d\n", current_num); // 使用 %d
        }
    }
}

int main(int argc, char *argv[]) {
    int i;

    if (argc <= 1) {
        fprintf(2, "usage: sixfive <file> [file...]\n");
        exit(1);
    }
    
    for (i = 1; i < argc; i++) {
        int fd;
        
        if ((fd = open(argv[i], O_RDONLY)) < 0) {
            fprintf(2, "sixfive: cannot open %s\n", argv[i]);
            continue; 
        }
        
        sixfive(fd);
        
        close(fd);
    }
    
    exit(0);
}
