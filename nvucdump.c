/*

nvucdump.c
Extracts sections from NVuc object file

Usage:
  nvucdump object.nvuc outdir

  The tool outputs files with names of the form:
    sectionI_TTTT.bin : Code section index I of type T (hex)

Compiling:
  cc -o nvucdump nvucdump.c readfile.c

  The program will fail if it is compiled for a big-endian CPU architecture!

Copyright 2022 Theron Tarigo

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.

*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include "readfile.h"

int main (int argc, char **argv) {
  if (!*(char*)(uint32_t[]){0x01}) return *(char*)1;
  if (argc<3) {
    fprintf(stderr,"Usage: %s object.nvuc outdir\n",argv[0]);
    return 1;
  }
  char * inpath =argv[1];
  char * outdirpath =argv[2];
  FILE * infile =fopen(inpath,"r");
  if (!infile) {
    fprintf(stderr,"Input %s inaccessible: %s\n",inpath,strerror(errno));
    return 1;
  }
  char * bin;
  size_t binsize;
  readfile(infile,&bin,&binsize);
  fclose(infile);
  mkdir(outdirpath,0777);
  if (chdir(outdirpath)) {
    fprintf(stderr,"Output directory %s inaccessible: %s\n",
      outdirpath,strerror(errno));
    return 1;
  }

  uint32_t * obj =(uint32_t*)bin;
  size_t objl=binsize>>2;
  size_t objsize=objl<<2;
  if (objl<8) {
    fprintf(stderr,"Object: Too short\n");
    return 1;
  }
  if (memcmp(&obj[0],"NVuc",4)) {
    fprintf(stderr,"Object: Unexpected magic\n");
    return 1;
  }
  int nsections=obj[2]&0xFFFF;
  fprintf(stderr,"Object: %d sections\n",nsections);
  if (objl<(size_t)(8+8*nsections)) {
    fprintf(stderr,"Object: Truncated section table\n");
    return 1;
  }
  for (int i=0;i<nsections;i++) {
    uint32_t * se =obj+8+8*i;
    uint32_t length=se[1];
    uint32_t offset=se[2];
    uint32_t typecode=se[0];
    fprintf(stderr,"  Section %d type 0x%04X at 0x%04X len 0x%04X\n",
      i,typecode,offset,length);
    if (offset&0x3) {
      fprintf(stderr,"Object: Unexpected section alignment\n");
      continue;
    }
    if (offset>objsize||offset+length>objsize) {
      fprintf(stderr,"Section: Section out of range\n");
      continue;
    }
    char outname[60];
    snprintf(outname,sizeof(outname),"section%d_%04X.bin",i,typecode);
    FILE * outfile =fopen(outname,"w");
    if (outfile) {
      fwrite(((uint8_t*)obj)+offset,1,length,outfile);
      fclose(outfile);
    }
  }
  return 0;
}

