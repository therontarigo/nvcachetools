
#include <stdio.h>
#include <stdlib.h>

#include "readfile.h"

void readfile (FILE *f, char **pbuf, size_t *plen) {
  char * buf =0;
  size_t buf_cap=0,len=0,readsize;
  do {
    buf_cap=(len+0x2000)&~0xFFF;
    buf=realloc(buf,buf_cap);
    readsize=fread(buf+len,1,0x1000,f);
    len+=readsize;
  } while(readsize);
  buf[len]=0;
  *pbuf=buf;
  *plen=len;
}

