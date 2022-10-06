/*

nvcachedec.c
Unpack shader objects from Nvidia GPU driver GLCache files

Usage:
  nvcachedec cachefile.toc outdir

  The corresponding cachefile.bin must exist.

  The default location for cache files on FreeBSD, Linux, and Solaris is
    $HOME/.nv/GLCache

  The __GL_SHADER_DISK_CACHE_PATH environment variable overrides where these
    files are created when an application uses a shader.

  The default location for cache files on Microsoft Windows is
    %LOCALAPPDATA%\NVIDIA\GLCache

  Objects are extracted into files with names of the form objectNNNNN.ext
    where filename extensions are as follows:

    Raw packed data:
      raw     : object directly stored without compression
      zstd    : zstd compressed object
      rle     : RLE compressed object (old drivers)
      unknown : Compression method could not be determined.

    Unpacked object for known compression methods:
      arbbin  : Nvidia ARB assembly archive, binary header + asm text
      nvuc    : Nvidia shader microcode archive
      bin     : Unknown object type

Compiling:
  cc -o nvcachedec nvcachedec.c readfile.c -lzstd

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

#include <zstd.h>

#include "readfile.h"

ssize_t unrle (void *dst, size_t dstcap, const void *src, size_t srclen);
const char * unrle_geterror (ssize_t e);

struct magic {
  int type;
  union {
    char b[8];
    uint32_t d;
  } match;
  size_t len;
};

#define PACKFMT_RAW 1
#define PACKFMT_RLE 2
#define PACKFMT_ZSTD 3

#define OBJTYPE_ARB 1
#define OBJTYPE_NVUC 2
#define OBJTYPE_NVVM_NVUC 3
#define OBJTYPE_NVUC_FROMNVVM 4

static const struct magic packmagic[]={
  {PACKFMT_RLE,{.b="\x05NVuc"},5},
  {PACKFMT_ZSTD,{.d=0xFD2FB528},4},
  {PACKFMT_RAW,{.d=0x0000000A},4},
  {PACKFMT_RAW,{.d=0x0000000B},4},
};

static struct magic objmagic[]={
  {OBJTYPE_NVUC,{.b="NVuc"},4},
  {OBJTYPE_NVVM_NVUC,{.b="NVVMNVuc"},8},
  {OBJTYPE_ARB,{.d=0x0000000A},4},
  {OBJTYPE_ARB,{.d=0x0000000B},4},
};

int main (int argc, char **argv) {
  if (!*(char*)(uint32_t[]){0x01}) return *(char*)1;
  if (argc<3) {
    fprintf(stderr,"Usage: %s cachefile.toc outdir\n",argv[0]);
    return 1;
  }
  char * pathref =argv[1];
  char * outpath =argv[2];
  size_t pathrefl=strlen(pathref);
  if (pathrefl<4||strcmp(".toc",pathref+pathrefl-4)) {
    fprintf(stderr,"Expect .toc file\n");
    return 1;
  }
  char * pathbuf =malloc(pathrefl+1);
  memcpy(pathbuf,pathref,pathrefl-4);
  FILE * infile;

  char * toc;
  size_t tocsize;
  memcpy(pathbuf+pathrefl-4,".toc",5);
  infile=fopen(pathbuf,"r");
  if (!infile) {
    fprintf(stderr,"Input %s inaccessible: %s\n",pathbuf,strerror(errno));
    return 1;
  }
  readfile(infile,&toc,&tocsize);
  fclose(infile);

  char * bin;
  size_t binsize;
  memcpy(pathbuf+pathrefl-4,".bin",5);
  infile=fopen(pathbuf,"r");
  if (!infile) {
    fprintf(stderr,"Input %s inaccessible: %s\n",pathbuf,strerror(errno));
    return 1;
  }
  readfile(infile,&bin,&binsize);
  fclose(infile);

  free(pathbuf);

  mkdir(outpath,0777);
  if (chdir(outpath)) {
    fprintf(stderr,"Output directory %s inaccessible: %s\n",
      outpath,strerror(errno));
    return 1;
  }

  if (tocsize<0x20||(tocsize-0x20)%0x18) {
    fprintf(stderr,"TOC: Unexpected length %lu\n",tocsize);
    return 1;
  }
  int nentries=(tocsize-0x20)/0x18;

  if (memcmp(toc,"CDVN",4)) {
    fprintf(stderr,"TOC: Unexpected magic\n");
    return 1;
  }

  for (int i=0;i<nentries;i++) {
    uint32_t * entry =(void*)(toc+0x20+i*0x18);
    fprintf(stderr,"\nTOC Entry %05d\n",i);
    uint32_t binoffset=entry[4];
    uint32_t tsectsize=entry[5];
#ifdef DBG
    for (int j=0;j<6;j++) {
      printf("TOC E%02X: %02X: %08X %d\n",i,j,entry[j],entry[j]);
    }
    printf("\n");
#endif
    if (tsectsize<0x4) {
      fprintf(stderr,"Entry: Section size < 0x4\n");
      continue;
    }
    uint32_t packedsize=tsectsize-0x4;
    char * packed =bin+binoffset+0x24;
    if (binoffset>binsize||binoffset+0x24+packedsize>binsize) {
      fprintf(stderr,"TOC entry out of range for bin file\n");
      return 1;
    }
#ifdef DBG
    {
      char outpath[PATH_MAX];
      FILE * outfile;
      snprintf(outpath,PATH_MAX,"binary%05d",i);
      outfile=fopen(outpath,"w");
      if (outfile) {
        fwrite(bin+binoffset,1,0x24+packedsize,outfile);
        fclose(outfile);
      }
    }
#endif

    uint32_t hdr[9];
    memcpy(hdr,bin+binoffset,0x24);
#ifdef DBG
    for (int j=0;j<9;j++) {
      printf("bin E%02X: %02X: %08X %d\n",i,j,hdr[j],hdr[j]);
    }
#endif
    uint32_t magic=hdr[0];
    uint32_t hsectsize=hdr[7];
    uint32_t hupksize=hdr[8];
    if (hsectsize!=tsectsize) {
      fprintf(stderr,"Section header: Length disagreement with TOC entry\n");
      continue;
    }
    if (magic!=0x9846A19D) {
      fprintf(stderr,"Section header: Unexpected magic\n");
      continue;
    }

    char outpath[PATH_MAX];
    char outpathpref[PATH_MAX];
    FILE * outfile;
    snprintf(outpathpref,PATH_MAX,"object%05d",i);

    snprintf(outpath,PATH_MAX,"header%05d.bin",i);
    outfile=fopen(outpath,"w");
    if (outfile) {
      fwrite(hdr,1,sizeof(hdr),outfile);
      fclose(outfile);
    }

    int packfmt=0;
    for (int i=0;i<sizeof(packmagic)/sizeof(*packmagic);i++) {
      struct magic pm=packmagic[i];
      if (packedsize>=pm.len&&!memcmp(packed,pm.match.b,pm.len)) {
        packfmt=pm.type;
        break;
      }
    }

    if (packedsize==1&&packed[0]==0) packfmt=PACKFMT_RAW;

    if (!packfmt&&hupksize==packedsize) {
      fprintf(stderr,"WARNING: Guessing raw packing from size match\n");
      packfmt=PACKFMT_RAW;
    }

    if (!packfmt&&hupksize==unrle(0,0,packed,packedsize)) {
      fprintf(stderr,"WARNING: Guessing RLE packing from size match\n");
      packfmt=PACKFMT_RLE;
    }

    const char * rawext =0;

    if (packfmt==PACKFMT_RLE) {
      rawext="rle";
      fprintf(stderr,"  RLE compressed\n");
    }
    if (packfmt==PACKFMT_ZSTD) {
      rawext="zstd";
      fprintf(stderr,"  zstd compressed\n");
    }
    if (packfmt==PACKFMT_RAW) {
      rawext="raw";
      fprintf(stderr,"  uncompressed\n");
    }
    if (!packfmt) {
      rawext="unknown";
      fprintf(stderr,"  unknown packing");
      for (int i=0;i<8;i++) {
        if (i<packedsize) {
          fprintf(stderr," %02X",(uint8_t)packed[i]);
        }
      }
      fprintf(stderr,"\n");
    }

    snprintf(outpath,PATH_MAX,"%s.%s",outpathpref,rawext);
    outfile=fopen(outpath,"w");
    if (outfile) {
      fwrite(packed,1,packedsize,outfile);
      fclose(outfile);
    }

    if (!packfmt) continue;

    void * upk =0;
    void * upk_free =0;
    size_t upksize=0;

    if (packfmt==PACKFMT_RAW) {
      upk=packed;
      upksize=packedsize;
    }
    if (packfmt==PACKFMT_ZSTD) {
      size_t dstcap=hupksize;
      char * dst =malloc(dstcap);
      upk_free=dst;
      size_t zr=ZSTD_decompress(dst,dstcap,packed,packedsize);
      if (ZSTD_isError(zr)) {
        fprintf(stderr,"Section: zstd error: %s\n",ZSTD_getErrorName(zr));
        goto skip;
      }
      if (zr!=hupksize) {
        fprintf(stderr,"Section: Uncompressed size mismatch\n");
        goto skip;
      }
      upk=dst;
      upksize=zr;
    }
    if (packfmt==PACKFMT_RLE) {
      size_t dstcap=hupksize;
      char * dst =malloc(dstcap);
      upk_free=dst;
      ssize_t usz=unrle(dst,dstcap,packed,packedsize);
      if (usz<0) {
        fprintf(stderr,"Section: RLE error: %s\n",unrle_geterror(usz));
        goto skip;
      }
      if (usz!=hupksize) {
        fprintf(stderr,"Section: Uncompressed size mismatch\n");
        goto skip;
      }
      upk=dst;
      upksize=usz;
    }

    int objtype=0;
    for (int i=0;i<sizeof(objmagic)/sizeof(*objmagic);i++) {
      struct magic om=objmagic[i];
      if (upksize>=om.len&&!memcmp(upk,om.match.b,om.len)) {
        objtype=om.type;
        break;
      }
    }

    if (!objtype&&upksize>=8) {
      uint32_t * u =upk;
      if (u[0]==upksize-4&&u[1]==0x0A) objtype=OBJTYPE_ARB;
    }

    const char * objext =0;

    if (objtype==OBJTYPE_ARB) {
      objext="arbbin";
      fprintf(stderr,"Object: ARB assembly\n");
    }
    if (objtype==OBJTYPE_NVUC) {
      objext="nvuc";
      fprintf(stderr,"Object: NVuc binary\n");
    }
    if (objtype==OBJTYPE_NVVM_NVUC&&upksize>=0x4) {
      objext="nvuc";
      fprintf(stderr,"Object: NVVM-NVuc binary\n");
      upk+=0x4;
      upksize-=0x4;
      objtype=OBJTYPE_NVUC_FROMNVVM;
    }
    if (!objtype) {
      objext="bin";
      int empty=1;
      for (size_t i=0;i<upksize;i++) if (((uint8_t*)upk)[i]) empty=0;
      if (empty) fprintf(stderr,"Object: empty\n");
      else fprintf(stderr,"Object: Unknown object type\n");
    }

    snprintf(outpath,PATH_MAX,"%s.%s",outpathpref,objext);
    outfile=fopen(outpath,"w");
    if (outfile) {
      fwrite(upk,1,upksize,outfile);
      fclose(outfile);
    }

    skip:
    free(upk_free);
#ifdef DBG
    fflush(stdout);
    fflush(stderr);
#endif
  }
  fprintf(stderr,"\nProcessed %d entries\n",nentries);
  return 0;
}

ssize_t unrle (void *dst, size_t dstcap, const void *src, size_t srclen) {
  uint8_t *d;
  const uint8_t *r,*end;
  r=src;
  end=src+srclen;
  d=dst;
  size_t wrsz=0;
  while (r<end) {
    uint8_t b=*r,l=b&0x3F,t=b>>6;
    if (!l) return -2;
    ++r;
    for (uint8_t i=0;i<l;i++) {
      if (t<0x2&&r==end) return -1;
      if (wrsz<dstcap) d[wrsz]= t<0x2?*r: t==0x2?0xFF: 0;
      wrsz++;
      if (t==0x0) ++r;
    }
    if (t==0x1) ++r;
  }
  return wrsz;
}

const char * unrle_geterror (ssize_t e) {
  if (e==-1) return "truncated input";
  if (e==-2) return "not Nv RLE";
  else if (e<0) return "undefined error";
  else return "no error";
}

