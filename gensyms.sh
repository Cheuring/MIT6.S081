#!/bin/bash
# filepath: /home/astaky/xv6-labs-2021/gensyms.sh

echo "#include \"types.h\"" > kernel/symbols.c
echo "" >> kernel/symbols.c
echo "struct symtable_entry {" >> kernel/symbols.c
echo "  uint64 addr;" >> kernel/symbols.c
echo "  char *name;" >> kernel/symbols.c
echo "};" >> kernel/symbols.c
echo "" >> kernel/symbols.c
echo "struct symtable_entry symbols[] = {" >> kernel/symbols.c

# 提取符号并排序
nm kernel/kernel | grep " [T] " | sort | awk '{print "  {0x" $1 ", \"" $3 "\"},"}' >> kernel/symbols.c

echo "  {0, 0}" >> kernel/symbols.c
echo "};" >> kernel/symbols.c
echo "" >> kernel/symbols.c
echo "char* " >> kernel/symbols.c
echo "findsym(uint64 addr)" >> kernel/symbols.c
echo "{" >> kernel/symbols.c
echo "  int i;" >> kernel/symbols.c
echo "  " >> kernel/symbols.c
echo "  for(i = 0; symbols[i].addr && symbols[i+1].addr; i++) {" >> kernel/symbols.c
echo "    if(addr >= symbols[i].addr && addr < symbols[i+1].addr)" >> kernel/symbols.c
echo "      return symbols[i].name;" >> kernel/symbols.c
echo "  }" >> kernel/symbols.c
echo "  " >> kernel/symbols.c
echo "  if(symbols[i].addr && addr >= symbols[i].addr)" >> kernel/symbols.c
echo "    return symbols[i].name;" >> kernel/symbols.c
echo "  " >> kernel/symbols.c
echo "  return \"unknown\";" >> kernel/symbols.c
echo "}" >> kernel/symbols.c