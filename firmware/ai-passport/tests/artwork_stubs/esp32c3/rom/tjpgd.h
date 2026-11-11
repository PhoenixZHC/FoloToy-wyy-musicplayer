#pragma once
#include <stdint.h>
typedef unsigned UINT;
typedef uint8_t BYTE;
typedef struct { UINT left,right,top,bottom; } JRECT;
typedef struct { void *device; UINT width,height; } JDEC;
#define JDR_OK 0
int jd_prepare(JDEC *jd,UINT (*read_fn)(JDEC *,BYTE *,UINT),void *work,UINT length,void *device);
int jd_decomp(JDEC *jd,UINT (*write_fn)(JDEC *,void *,JRECT *),BYTE scale);
