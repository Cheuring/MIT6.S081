#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"

void
origin_test()
{
  char buf[BSIZE];
  int fd, i, blocks;

  fd = open("big.file", O_CREATE | O_WRONLY);
  if(fd < 0){
    printf("bigfile: cannot open big.file for writing\n");
    exit(-1);
  }

  blocks = 0;
  while(1){
    *(int*)buf = blocks;
    int cc = write(fd, buf, sizeof(buf));
    if(cc <= 0)
      break;
    blocks++;
    if (blocks % 100 == 0)
      printf(".");
  }

  printf("\nwrote %d blocks\n", blocks);
  if(blocks != 65803) {
    printf("bigfile: file is too small\n");
    exit(-1);
  }

  if(blocks != NINDIRECT * NINDIRECT * NINDIRECT + NINDIRECT * NINDIRECT + NINDIRECT + NDIRECT){
    printf("triple inode: file is too small\n");
    exit(-1);
  }
  
  close(fd);
  fd = open("big.file", O_RDONLY);
  if(fd < 0){
    printf("bigfile: cannot re-open big.file for reading\n");
    exit(-1);
  }
  for(i = 0; i < blocks; i++){
    int cc = read(fd, buf, sizeof(buf));
    if(cc <= 0){
      printf("bigfile: read error at block %d\n", i);
      exit(-1);
    }
    if(*(int*)buf != i){
      printf("bigfile: read the wrong data (%d) for block %d\n",
             *(int*)buf, i);
      exit(-1);
    }
  }
}

// a few convenient constants
#define N_DIRECT (NDIRECT)
#define N_INDIRECT (NINDIRECT)
#define N_D_INDIRECT (NINDIRECT*NINDIRECT)
#define N_T_INDIRECT (NINDIRECT*NINDIRECT*NINDIRECT)
#define MAX_FILE_BLOCKS (N_DIRECT + N_INDIRECT + N_D_INDIRECT + N_T_INDIRECT)

void
test_sparse_file()
{
  char buf[BSIZE];
  int fd;
  int i;

  printf("testing sparse file\n");

  // The blocks we will write to.
  int test_blocks[] = {
    0,                                  // first direct
    N_DIRECT - 1,                       // last direct
    N_DIRECT,                           // first single-indirect
    N_DIRECT + N_INDIRECT - 1,          // last single-indirect
    N_DIRECT + N_INDIRECT,              // first double-indirect
    N_DIRECT + N_INDIRECT + N_D_INDIRECT - 1, // last double-indirect
    N_DIRECT + N_INDIRECT + N_D_INDIRECT, // first triple-indirect
    MAX_FILE_BLOCKS - 1,                // last possible block
  };

  // Create and write sporadically
  if ((fd = open("big.file", O_CREATE | O_WRONLY)) < 0) {
    printf("sparse: cannot open big.file for writing\n");
    exit(-1);
  }

  for (i = 0; i < sizeof(test_blocks) / sizeof(test_blocks[0]); i++) {
    int block = test_blocks[i];
    
    if (lseek(fd, (long)block * BSIZE, 0) < 0) {
      printf("sparse: lseek failed\n");
      exit(-1);
    }
    
    // Write the block number into the block
    *(int*)buf = block;
    if (write(fd, buf, BSIZE) != BSIZE) {
      printf("sparse: write failed at block %d\n", block);
      exit(-1);
    }
  }
  close(fd);

  // Re-open and check
  if ((fd = open("big.file", O_RDONLY)) < 0) {
    printf("sparse: cannot open big.file for reading\n");
    exit(-1);
  }

  for (i = 0; i < sizeof(test_blocks) / sizeof(test_blocks[0]); i++) {
    int block = test_blocks[i];
    
    if (lseek(fd, (long)block * BSIZE, 0) < 0) {
      printf("sparse: lseek failed\n");
      exit(-1);
    }
    
    if (read(fd, buf, BSIZE) != BSIZE) {
      printf("sparse: read failed at block %d\n", block);
      exit(-1);
    }

    if (*(int*)buf != block) {
      printf("sparse: read wrong data (%d) for block %d\n", *(int*)buf, block);
      exit(-1);
    }
  }
  close(fd);

  printf("sparse file test ok\n");
}

int
main()
{
  // origin_test();
  test_sparse_file();
  
  printf("bigfile done; ok\n"); 
  exit(0);
}
