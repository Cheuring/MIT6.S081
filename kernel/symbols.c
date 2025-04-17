#include "types.h"

struct symtable_entry {
  uint64 addr;
  char *name;
};

struct symtable_entry symbols[] = {
  {0, 0}
};

char* 
findsym(uint64 addr)
{
  int i;
  
  for(i = 0; symbols[i].addr && symbols[i+1].addr; i++) {
    if(addr >= symbols[i].addr && addr < symbols[i+1].addr)
      return symbols[i].name;
  }
  
  if(symbols[i].addr && addr >= symbols[i].addr)
    return symbols[i].name;
  
  return "unknown";
}
