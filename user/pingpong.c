#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int p[2];              //管道端口
    char receivedInfo[5];  //字符串接收缓冲区
    int forkReturn;        //返回的子线程端口号
    if (pipe(p) < 0) {
        printf("pipe error!");
    }

    forkReturn = fork();  //fork系统调用返回值

    if(forkReturn == 0) {
        int parent_pid;
        read(p[0], &parent_pid, sizeof(int));  //pid无论是几位数字都会占用固定大小的字节数
        read(p[0], receivedInfo, 5);           //读取ping
        close(p[0]);                
        printf("%d: received %s from pid %d\n", getpid(), receivedInfo, parent_pid);
        write(p[1], "pong", 5);                //写入pong
        close(p[1]);
        exit(0);                               //退出进程
    } 
    else if(forkReturn > 0) {
        int parent_pid = getpid();
        write(p[1], &parent_pid, sizeof(int)); //pid无论是几位数字都会占用固定大小的字节数
        sleep(1);                              //等待子进程读完父进程的pid
        write(p[1], "ping", 5);                //写入ping
        close(p[1]);
        sleep(1);                              //等待子进程读完
        read(p[0], receivedInfo, 5);           //读取pong
        printf("%d: received %s from pid %d\n", getpid(), receivedInfo, forkReturn);
        close(p[0]);
        exit(0);                               //退出进程
    }
}