#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char* basename(char* path) {
    char* p;

    for (p = path + strlen(path); p >= path && *p != '/'; p--);
    p++;

    return p;
}

void find(char* path, char* filename) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    switch (st.type) {
        case T_FILE:
            if (strcmp(basename(path), filename) == 0) {
                printf("%s\n", path);
            }
            break;

        case T_DIR:
            if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
                printf("find: path too long\n");
                break;
            }
            strcpy(buf, path);
            p = buf + strlen(buf);
            *p++ = '/';
            while(read(fd, &de, sizeof(de)) == sizeof(de)){
                if(de.inum == 0 || strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                    continue;
                int name_len = strlen(de.name);
                memmove(p, de.name, name_len);
                p[name_len] = 0;
                find(buf, filename);
            }
            break;
    }
    close(fd);
}

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        fprintf(2, "usage: find dir filename\n");
        exit(1);
    }

    if (argc == 2) {
        find(".", argv[1]);
        exit(0);
    }

    find(argv[1], argv[2]);
    exit(0);
}