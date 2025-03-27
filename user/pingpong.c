#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int p[2];
    pipe(p);

    if (fork() == 0) {
        char buff[20];
        int n = read(p[0], buff, 20);

        printf("%d: received ping\n", getpid());
        write(p[1], buff, n);

        close(p[0]);
        close(p[1]);
    } else {
        write(p[1], "ping", 5);
        wait(0);

        char buff[20];
        read(p[0], buff, 20);
        printf("%d: received pong\n", getpid());

        close(p[0]);
        close(p[1]);
    }

    exit(0);
}