
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004-2006
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>

#include "trousers/tss.h"
#include "trousers/trousers.h"
#include "trousers_types.h"
#include "trousers_types.h"
#include "spi_utils.h"
#include "capabilities.h"
#include "tsplog.h"
#include "obj.h"


TSS_UUID NULL_UUID = { 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0 } };

TSS_VERSION VERSION_1_1 = { 1, 1, 0, 0 };

UINT16
Decode_UINT16(BYTE * in)
{
	UINT16 temp = 0;
	temp = (in[1] & 0xFF);
	temp |= (in[0] << 8);
	return temp;
}

void
UINT32ToArray(UINT32 i, BYTE * out)
{
	out[0] = (BYTE) ((i >> 24) & 0xFF);
	out[1] = (BYTE) ((i >> 16) & 0xFF);
	out[2] = (BYTE) ((i >> 8) & 0xFF);
	out[3] = (BYTE) i & 0xFF;
}

void
UINT64ToArray(UINT64 i, BYTE *out)
{
	out[0] = (BYTE) ((i >> 56) & 0xFF);
	out[1] = (BYTE) ((i >> 48) & 0xFF);
	out[2] = (BYTE) ((i >> 40) & 0xFF);
	out[3] = (BYTE) ((i >> 32) & 0xFF);
	out[4] = (BYTE) ((i >> 24) & 0xFF);
	out[5] = (BYTE) ((i >> 16) & 0xFF);
	out[6] = (BYTE) ((i >> 8) & 0xFF);
	out[7] = (BYTE) i & 0xFF;
}

void
UINT16ToArray(UINT16 i, BYTE * out)
{
	out[0] = ((i >> 8) & 0xFF);
	out[1] = i & 0xFF;
}

UINT64
Decode_UINT64(BYTE *y)
{
	UINT64 x = 0;

	x = y[0];
	x = ((x << 8) | (y[1] & 0xFF));
	x = ((x << 8) | (y[2] & 0xFF));
	x = ((x << 8) | (y[3] & 0xFF));
	x = ((x << 8) | (y[4] & 0xFF));
	x = ((x << 8) | (y[5] & 0xFF));
	x = ((x << 8) | (y[6] & 0xFF));
	x = ((x << 8) | (y[7] & 0xFF));

	return x;
}

UINT32
Decode_UINT32(BYTE * y)
{
	UINT32 x = 0;

	x = y[0];
	x = ((x << 8) | (y[1] & 0xFF));
	x = ((x << 8) | (y[2] & 0xFF));
	x = ((x << 8) | (y[3] & 0xFF));

	return x;
}

UINT32
get_pcr_event_size(TSS_PCR_EVENT *e)
{
	return (sizeof(TSS_PCR_EVENT) + e->ulEventLength + e->ulPcrValueLength);
}

void
LoadBlob_AUTH(UINT64 *offset, BYTE *blob, TPM_AUTH *auth)
{
	Trspi_LoadBlob_UINT32(offset, auth->AuthHandle, blob);
	Trspi_LoadBlob(offset, 20, blob, auth->NonceOdd.nonce);
	Trspi_LoadBlob_BOOL(offset, auth->fContinueAuthSession, blob);
	Trspi_LoadBlob(offset, 20, blob, (BYTE *)&auth->HMAC);
}

void
UnloadBlob_AUTH(UINT64 *offset, BYTE *blob, TPM_AUTH *auth)
{
	Trspi_UnloadBlob(offset, 20, blob, auth->NonceEven.nonce);
	Trspi_UnloadBlob_BOOL(offset, &auth->fContinueAuthSession, blob);
	Trspi_UnloadBlob(offset, 20, blob, (BYTE *)&auth->HMAC);
}

/* If alloc is true, we allocate a new buffer for the bytes and set *data to that.
 * If alloc is false, data is really a BYTE*, so write the bytes directly to that buffer */
TSS_RESULT
get_local_random(TSS_HCONTEXT tspContext, TSS_BOOL alloc, UINT32 size, BYTE **data)
{
	FILE *f = NULL;
	BYTE *buf = NULL;

	f = fopen(TSS_LOCAL_RANDOM_DEVICE, "r");
	if (f == NULL) {
		LogError("open of %s failed: %s", TSS_LOCAL_RANDOM_DEVICE, strerror(errno));
		return TSPERR(TSS_E_INTERNAL_ERROR);
	}

	if (alloc) {
		buf = calloc_tspi(tspContext, size);
		if (buf == NULL) {
			LogError("malloc of %u bytes failed", size);
			fclose(f);
			return TSPERR(TSS_E_OUTOFMEMORY);
		}
	} else
		buf = (BYTE *)data;

	if (fread(buf, size, 1, f) == 0) {
		LogError("fread of %s failed: %s", TSS_LOCAL_RANDOM_DEVICE, strerror(errno));
		fclose(f);
		return TSPERR(TSS_E_INTERNAL_ERROR);
	}

	if (alloc)
		*data = buf;
	fclose(f);

	return TSS_SUCCESS;
}
