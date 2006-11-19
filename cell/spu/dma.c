#include <stdlib.h>
#include <spu_intrinsics.h>
#include <cbe_mfc.h>
#include <spu_mfcio.h>
#include "fftw-spu.h"
#include "../fftw-cell.h"

#define MAX_DMA_SIZE 16384
static mfc_list_element_t list[MAX_LIST_SZ] __attribute__ ((aligned (ALIGNMENT)));

/* universal DMA transfer routine */

void X(spu_dma1d)(void *spu_addr, long long ppu_addr, size_t sz,
		  unsigned int cmdl)
{
     unsigned int tag_id = 0;
     int nlist = 0;

     while (sz > 0) {
	  /* select chunk to align ppu_addr */
	  size_t chunk = ALIGNMENT - (ppu_addr & (ALIGNMENT - 1));

	  /* if already aligned, transfer the whole thing */
	  if (chunk == ALIGNMENT || chunk > sz) 
	       chunk = sz;

	  /* ...up to MAX_DMA_SIZE */
	  if (chunk > MAX_DMA_SIZE) 
	       chunk = MAX_DMA_SIZE;

	  list[nlist].notify = 0;
	  list[nlist].size = chunk;
	  list[nlist].eal = ppu_addr;
	  ++nlist;
	  sz -= chunk; ppu_addr += chunk;
     }

     spu_mfcdma32(spu_addr, (unsigned)list, nlist * sizeof(list[0]),
		  tag_id, cmdl);
     spu_mfcstat(2);
}

/* 2D dma transfer routine, works for 
   ppu_stride_bytes == 2 * sizeof(R) or ppu_vstride_bytes == 2 * sizeof(R) */
void X(spu_dma2d)(R *A, long long ppu_addr, 
		  int n, /* int spu_stride = 2 , */ int ppu_stride_bytes,
		  int v, /* int spu_vstride = 2 * n, */
		  int ppu_vstride_bytes,
		  unsigned int cmdl,
		  R *buf, int nbuf)
{
     int vv, i, ii;
     unsigned int tag_id = 0;
     
     if (ppu_stride_bytes == 2 * sizeof(R)) { 
	  /* contiguous array on the PPU side */

	  /* if the input is a 1D contiguous array, collapse n into v */
	  if (ppu_vstride_bytes == ppu_stride_bytes * n) {
	       n *= v;
	       v = 1;
	  }
	  
	  if (v == 1 || 2 * sizeof(R) * n > MAX_DMA_SIZE) {
	       for (vv = 0; vv < v; ++vv) {
		    X(spu_dma1d)(A, ppu_addr, 2 * sizeof(R) * n, cmdl);
		    A += 2 * n;
		    ppu_addr += ppu_vstride_bytes;
	       }
	  } else {
	       int chunk = MAX_LIST_SZ;
	       for (vv = 0; vv < v; vv += chunk) {
		    if (chunk > v - vv)
			 chunk = v - vv;

		    for (ii = 0; ii < chunk; ++ii) {
			 list[ii].notify = 0;
			 list[ii].size = 2 * sizeof(R) * n;
			 list[ii].eal = ppu_addr;
			 ppu_addr += ppu_vstride_bytes;
		    }

		    spu_mfcdma32(A, (unsigned)list, chunk * sizeof(list[0]),
				 tag_id, cmdl);
		    spu_mfcstat(2);

		    A += 2 * n * chunk;
	       }
	  }
     } else { 
	  /* ppu_vstride_bytes == 2 * sizeof(R) */
	  if (n == 1 || 2 * sizeof(R) * v > MAX_DMA_SIZE) {
	       /* vector transfer of long vectors */
	       for (i = 0; i < n; ++i) {
		    R *bufalign = ALIGN_LIKE(buf, ppu_addr);
		    if (cmdl == MFC_PUTL_CMD)
			 X(spu_complex_memcpy)(bufalign, 2, A, 2 * n, v);
		    X(spu_dma1d)(bufalign, ppu_addr, v * 2 * sizeof(R), cmdl);
		    if (cmdl == MFC_GETL_CMD)
			 X(spu_complex_memcpy)(A, 2 * n, bufalign, 2, v);
		    A += 2;
		    ppu_addr += ppu_stride_bytes;
	       }
	  } else {
	       /* vector transfer of short vectors, use dma lists */
	       for (i = 0; i < n; i += nbuf) {
		    /* FIXME: I don't think that this alignment makes any
		       difference */
		    R *bufalign = ALIGN_LIKE(buf, ppu_addr);

		    if (nbuf > n - i)
			 nbuf = n - i;

		    for (ii = 0; ii < nbuf; ++ii) {
			 list[ii].notify = 0;
			 list[ii].size = 2 * sizeof(R) * v;
			 list[ii].eal = ppu_addr;
			 ppu_addr += ppu_stride_bytes;
		    }

		    if (cmdl == MFC_PUTL_CMD) 
			 for (ii = 0; ii < nbuf; ++ii) {
			      X(spu_complex_memcpy)(bufalign + ii * 2 * v, 2, 
						    A, 2 * n, v);
			      A += 2;
			 }

		    spu_mfcdma32(bufalign, (unsigned)list, nbuf * sizeof(list[0]),
				 tag_id, cmdl);
		    spu_mfcstat(2);

		    if (cmdl == MFC_GETL_CMD) 
			 for (ii = 0; ii < nbuf; ++ii) {
			      X(spu_complex_memcpy)(A, 2 * n,
						    bufalign + ii * 2 * v, 2, v);
			      A += 2;
			 }
	       }
	  }
     }
}
