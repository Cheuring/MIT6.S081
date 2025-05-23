// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5
#define SUBSH 6

#define MAXARGS 10
#define MATCH_COUNT 10
#define BUFSIZE 100
#define HISTORY_COUNT 16
#define C(x)  ((x)-'@')  // Control-x

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";
static struct {
  char history_commands[HISTORY_COUNT][BUFSIZE];
  int cursor;
  int w;
  char stage_command[BUFSIZE];
} hist;

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

struct subshcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);
int find_matches(char*, char[][DIRSIZ], int);
void stage_command(char*, int);
void swtch_command(char*, int*, int);
void add_history(char*);

// Execute cmd
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  struct subshcmd *scmd;

  if(cmd == 0)
    return;

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      return;
    if(strcmp("cd", ecmd->argv[0]) == 0){
      if(chdir(ecmd->argv[1]) < 0)
        fprintf(2, "cannot cd %s\n", ecmd->argv[1]);
      break;
    }else if(strcmp("wait", ecmd->argv[0]) == 0){
      wait(0);
      break;
    }
    if(fork1() == 0){
      exec(ecmd->argv[0], ecmd->argv);
      fprintf(2, "exec %s failed\n", ecmd->argv[0]);
      exit(1);
    }
    wait(0);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    if(fork1() == 0){
      close(rcmd->fd);
      if(open(rcmd->file, rcmd->mode) < 0){
        fprintf(2, "open %s failed\n", rcmd->file);
        return;
      }
      runcmd(rcmd->cmd);
      exit(0);
    }
    wait(0);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    runcmd(lcmd->left);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
      exit(0);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
      exit(0);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0){
      runcmd(bcmd->cmd);
      exit(0);
    }
    break;
  
  case SUBSH:
    scmd = (struct subshcmd*)cmd;
    if(fork1() == 0){
      runcmd(scmd->cmd);
      exit(0);
    }
    wait(0);
    break;
  }

}

int
getcmd(char *buf, int nbuf)
{
  struct stat st;
  if(fstat(0, &st) < 0){
    fprintf(2, "sh: cannot stat\n");
    exit(1);
  }
  if(st.type != T_FILE)
    fprintf(2, "$ ");
  
  // 替换 gets() 以支持 Tab 补全
  char c;
  int i = 0;
  memset(buf, 0, nbuf);
  
  while(i < nbuf-1) {
    if(read(0, &c, 1) != 1)
      break;
      
    if(c == '\t') {  // Tab 键
      // 找出当前要补全的部分
      int j = i;
      while(j > 0 && !strchr(whitespace, buf[j-1]) && !strchr(symbols, buf[j-1]))
        j--;
        
      char prefix[DIRSIZ+1] = {0};
      int prefix_len = i - j;
      if(prefix_len > 0) {
        strncpy(prefix, buf+j, prefix_len);
        prefix[prefix_len] = 0;
        
        // 查找匹配项
        char matches[10][DIRSIZ];  // 最多显示10个匹配项
        int match_count = find_matches(prefix, matches, 10);
        
        if(match_count == 1) {
          // 只有一个匹配项，直接补全
          strcpy(buf+j, matches[0]);
          i = j + strlen(matches[0]);
          
          // 清除当前行并重绘
          printf("\r\033[K$ %s", buf);  // ANSI 控制序列清除行
        } 
        else if(match_count > 1) {
          // 多个匹配项，显示所有可能选项
          printf("\n");
          for(int k = 0; k < match_count; k++) {
            printf("%s  ", matches[k]);
          }
          printf("\n$ %s", buf);
        }
      }
      continue;
    }
    
    if(c == '\n' || c == '\r') {
      buf[i] = '\n';
      i++;
      break;
    }
    
    if(c == '\x7f' || c == C('H')) {  // Backspace
      if(i > 0) {
        buf[--i] = '\0';
        printf("\b \b");
      }
      continue;
    }

    if(c == C('U')){
      i = 0;
      buf[i] = '\0';
      continue;
    }

    if(c == C('P')){
      stage_command(buf, i);
      swtch_command(buf, &i, -1);
      printf("\r\033[K$ %s", buf);
      continue;
    }

    if(c == C('N')){
      stage_command(buf, i);
      swtch_command(buf, &i, 1);
      printf("\r\033[K$ %s", buf);
      continue;
    }
    
    buf[i] = c;
    i++;
  }
  
  buf[i] = '\0';
  
  if(buf[0] == 0)  // 空行
    return -1;
    
  return 0;
}

// extra feature!

// 查找匹配前缀的文件
int find_matches(char *prefix, char matches[][DIRSIZ], int max_matches) {
  int fd, match_count = 0;
  struct dirent de;
  
  if((fd = open(".", 0)) < 0) {
    fprintf(2, "tab completion: cannot open current directory\n");
    return 0;
  }
  
  int prefix_len = strlen(prefix);
  
  while(read(fd, &de, sizeof(de)) == sizeof(de)) {
    if(de.inum == 0)
      continue;
      
    // 检查文件名是否匹配前缀
    if(strncmp(prefix, de.name, prefix_len) == 0) {
      if(match_count < max_matches) {
        strcpy(matches[match_count], de.name);
        match_count++;
      }
    }
  }
  
  close(fd);
  return match_count;
}

void stage_command(char *buf, int n) {
  if(hist.cursor != hist.w)
    return;
  memmove(hist.stage_command, buf, BUFSIZE);
  hist.stage_command[n] = 0;
}

void swtch_command(char *buf, int *cursor, int step) {
  int cur = hist.cursor + step;
  if(cur < 0 || cur <= hist.w - HISTORY_COUNT) {
    // fprintf(2, "\nhistory at the top !");
    return;
  }

  if(cur == hist.w){
    memmove(buf, hist.stage_command, BUFSIZE);
    *cursor = strlen(buf);
    hist.cursor = cur;
    return;
  }
  if(cur > hist.w){
    // fprintf(2, "\nhistory reach the bottom");
    return;
  }

  memmove(buf, hist.history_commands[cur % HISTORY_COUNT], BUFSIZE);
  *cursor = strlen(buf);
  hist.cursor = cur;
}

void add_history(char *buf) {
  int idx = hist.w % HISTORY_COUNT;
  memmove(hist.history_commands[idx], buf, BUFSIZE);
  // chop '\n'
  hist.history_commands[idx][strlen(buf) - 1] = '\0';
  hist.w++;
  hist.cursor = hist.w;
}

int
main(void)
{
  static char buf[BUFSIZE];
  int fd;
  hist.cursor = hist.w = 0;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    add_history(buf);
    struct cmd* cmd = parsecmd(buf);
    runcmd(cmd);
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}

struct cmd*
subshcmd(struct cmd *subcmd)
{
  struct subshcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = SUBSH;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// Parsing


int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  cmd = subshcmd(cmd);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  struct subshcmd *scmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;

  case SUBSH:
    scmd = (struct subshcmd*)cmd;
    nulterminate(scmd->cmd);
    break;
  }
  return cmd;
}
