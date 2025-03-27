#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void helper();
void changefdAndCloseP(int, int *);

int main(int argc, char *argv[]) {
    int p[2];
    pipe(p);

    if (fork() == 0) {
        changefdAndCloseP(0, p);
        helper();
    }

    changefdAndCloseP(1, p);
    close(0);

    for (int i = 2; i <= 35; ++i) {
        write(1, &i, 4);
    }

    close(1);
    wait(0);
    exit(0);
}

void helper() {
    int ori_num, num;
    if (read(0, &ori_num, 4) == 0) {
        exit(0);
    }

    printf("prime %d\n", ori_num);

    int p[2];
    pipe(p);
    if (fork() == 0) {
        changefdAndCloseP(0, p);
        helper();
    }

    changefdAndCloseP(1, p);

    while (read(0, &num, 4) == 4) {
        if (num % ori_num == 0) continue;

        write(1, &num, 4);
    }

    close(1);
    wait(0);
    exit(0);
}

void changefdAndCloseP(int fd, int *p) {
    close(fd);
    dup(p[fd]);
    close(p[0]);
    close(p[1]);
}