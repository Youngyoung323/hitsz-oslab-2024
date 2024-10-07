#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

void find(char *path, char *filename) {
    int fd;            //打开文件的文件描述符 
    struct stat st;    //文件状态信息
    struct dirent de;  //文件条目，包含文件名以及inode

    if ((fd = open(path, 0)) < 0) {         //文件无法打开
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {               //根据路径获取文件状态信息，如果获取失败
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    } 

    if (st.type == T_FILE) {                //当前路径是文件直接返回s
        fprintf(2, "find: %s not a directory\n", path);
        return;
    }

    char buf[512];  //存放待输出的文件路径/目录路径，递归执行的基础
    char *p;        //当前路径

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {  //文件路径长度超过缓冲区最大长度
        printf("find: path too long\n");    
        return;
    }

    strcpy(buf, path);      //路径长度合理则将path的路径赋值给buf,递归基础
    p = buf + strlen(buf);  //p得到buf路径并移动到路径末尾,buf和p共享的是一块区域，p改动会同步给buf
    *p++ = '/';             //末尾加上文件分隔符

    while(read(fd, &de, sizeof(de)) == sizeof(de)) {  //循环读取fd所代表目录下面每个条目的信息(就是读子文件和子目录)，并且保证每次读到都是一个完整的文件/目录项
        if(de.inum == 0) continue;  //读到空项
        if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) {  //避免递归当前目录和上级目录
            continue;
        } 
        memmove(p, de.name, DIRSIZ); //将文件名追加到p的末尾
        p[DIRSIZ] = 0;               //末尾添加字符串结束符

        if (stat(buf, &st) < 0) {    //检查新路径下的文件信息是否可以被获取
          fprintf(2, "find: cannot stat %s\n", buf);
          continue;
        }

        if(st.type == T_FILE || st.type == T_DIR) {  //新的文件路径/目录路径与filename一致直接输出
            if(strcmp(de.name, filename) == 0) {
                printf("%s\n", buf);
            }  
        }

        if(st.type == T_DIR) find(buf, filename);    //目录应该递归查询
    }
 }

int main(int argc, char *argv[]) {
    if(argc != 3) {
        printf("Wrong command format! Should be find <path> <filename>\n");
        exit(-1);
    }

    char *path = argv[1];      //路径
    char *filename = argv[2];  //所寻文件名
    find(path, filename);
    exit(0);
}