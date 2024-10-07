#include "kernel/types.h"
#include "user.h"

int main(int argc, char* argv[]) {
    if(argc != 2) {
        printf("Sleep needs one argument!\n");  //Check the number of argument 
        exit(-1);
    }
    int ticks = atoi(argv[1]); //Convert strings to integers
    sleep(ticks);              //The system call uses sleep
    printf("(nothing happens for a little while)\n");
    exit(0); //Ensure the process exit successfully
}