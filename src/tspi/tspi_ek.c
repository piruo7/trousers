
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
#include <inttypes.h>

#include "trousers/tss.h"
#include "trousers/trousers.h"
#include "spi_internal_types.h"
#include "spi_utils.h"
#include "capabilities.h"
#include "tsplog.h"
#include "obj.h"


TSS_RESULT
Tspi_TPM_CreateEndorsementKey(TSS_HTPM hTPM,			/* in */
			      TSS_HKEY hKey,			/* in */
			      TSS_VALIDATION * pValidationData)	/* in, out */
{
	TCS_CONTEXT_HANDLE tcsContext;
	TCPA_NONCE antiReplay;
	TCPA_DIGEST digest;
	TSS_RESULT result;
	UINT32 ekSize;
	BYTE *ek;
	TCPA_KEY dummyKey;
	UINT64 offset;
	TCPA_DIGEST hash;
	//BYTE hashBlob[1024];
	UINT32 newEKSize;
	BYTE *newEK;
	TSS_HCONTEXT tspContext;
	TCPA_PUBKEY pubEK;
	Trspi_HashCtx hashCtx;

	memset(&pubEK, 0, sizeof(TCPA_PUBKEY));
	memset(&dummyKey, 0, sizeof(TCPA_KEY));

	if ((result = obj_tpm_get_tsp_context(hTPM, &tspContext)))
		return result;

	if ((result = obj_context_is_connected(tspContext, &tcsContext)))
		return result;

	if ((result = obj_rsakey_get_blob(hKey, &ekSize, &ek)))
		return result;

	offset = 0;
	if ((result = Trspi_UnloadBlob_KEY(&offset, ek, &dummyKey)))
		return result;

	offset = 0;
	Trspi_LoadBlob_KEY_PARMS(&offset, ek, &dummyKey.algorithmParms);
	free_key_refs(&dummyKey);
	ekSize = offset;

	if (pValidationData == NULL) {
		if ((result = internal_GetRandomNonce(tcsContext, &antiReplay))) {
			LogError("Failed to create random nonce");
			return TSPERR(TSS_E_INTERNAL_ERROR);
		}
	} else {
		if (pValidationData->ulExternalDataLength < sizeof(antiReplay.nonce))
			return TSPERR(TSS_E_BAD_PARAMETER);

		memcpy(antiReplay.nonce, pValidationData->rgbExternalData,
		       sizeof(antiReplay.nonce));
	}

	if ((result = TCSP_CreateEndorsementKeyPair(tcsContext, antiReplay,
						   ekSize, ek, &newEKSize, &newEK, &digest)))
		return result;

	if (pValidationData == NULL) {
		result = Trspi_HashInit(&hashCtx, TSS_HASH_SHA1);
		result |= Trspi_HashUpdate(&hashCtx, newEKSize, newEK);
		result |= Trspi_HashUpdate(&hashCtx, TCPA_SHA1_160_HASH_LEN, antiReplay.nonce);
		if ((result |= Trspi_HashFinal(&hashCtx, digest.digest)))
			return result;

		if (memcmp(hash.digest, digest.digest, TCPA_SHA1_160_HASH_LEN)) {
			LogError("Internal verification failed");
			return TSPERR(TSS_E_INTERNAL_ERROR);
		}
	} else {
		pValidationData->rgbData = calloc_tspi(tspContext, newEKSize);
		if (pValidationData->rgbData == NULL) {
			LogError("malloc of %u bytes failed.", newEKSize);
			return TSPERR(TSS_E_OUTOFMEMORY);
		}
		pValidationData->ulDataLength = newEKSize;
		memcpy(pValidationData->rgbData, newEK, newEKSize);
		memcpy(&pValidationData->rgbData[ekSize], antiReplay.nonce,
		       sizeof(antiReplay.nonce));

		pValidationData->rgbValidationData = calloc_tspi(tspContext,
								 TCPA_SHA1_160_HASH_LEN);
		if (pValidationData->rgbValidationData == NULL) {
			LogError("malloc of %d bytes failed.", TCPA_SHA1_160_HASH_LEN);
			free_tspi(tspContext, pValidationData->rgbData);
			pValidationData->rgbData = NULL;
			pValidationData->ulDataLength = 0;
			return TSPERR(TSS_E_OUTOFMEMORY);
		}
		pValidationData->ulValidationDataLength = TCPA_SHA1_160_HASH_LEN;
		memcpy(pValidationData->rgbValidationData, digest.digest, TCPA_SHA1_160_HASH_LEN);
	}

	if ((result = obj_rsakey_set_pubkey(hKey, newEK)) && pValidationData) {
		free_tspi(tspContext, pValidationData->rgbValidationData);
		free_tspi(tspContext, pValidationData->rgbData);
		pValidationData->rgbData = NULL;
		pValidationData->ulDataLength = 0;
		pValidationData->rgbValidationData = NULL;
		pValidationData->ulValidationDataLength = 0;
	}

	free(newEK);

	return result;
}

TSS_RESULT
Tspi_TPM_GetPubEndorsementKey(TSS_HTPM hTPM,			/* in */
			      TSS_BOOL fOwnerAuthorized,	/* in */
			      TSS_VALIDATION *pValidationData,	/* in, out */
			      TSS_HKEY *phEndorsementPubKey)	/* out */
{
	TCPA_DIGEST digest;
	TSS_RESULT result;
	TCS_CONTEXT_HANDLE tcsContext;
	TPM_AUTH ownerAuth;
	UINT64 offset;
	TSS_HPOLICY hPolicy;
	UINT32 pubEKSize;
	BYTE *pubEK;
	TCPA_NONCE antiReplay;
	TCPA_DIGEST checkSum;
	TSS_HOBJECT retKey;
	TSS_HCONTEXT tspContext;
	TCPA_PUBKEY pubKey;
	Trspi_HashCtx hashCtx;

	memset(&pubKey, 0, sizeof(TCPA_PUBKEY));

	if (phEndorsementPubKey == NULL)
		return TSPERR(TSS_E_BAD_PARAMETER);

	if ((result = obj_tpm_is_connected(hTPM, &tcsContext)))
		return result;

	if ((result = obj_tpm_get_tsp_context(hTPM, &tspContext)))
		return result;

	if (fOwnerAuthorized) {
		if ((result = obj_tpm_get_policy(hTPM, &hPolicy)))
			return result;

		result = Trspi_HashInit(&hashCtx, TSS_HASH_SHA1);
		result |= Trspi_Hash_UINT32(&hashCtx, TPM_ORD_OwnerReadPubek);
		if ((result |= Trspi_HashFinal(&hashCtx, digest.digest)))
			return result;

		if ((result = secret_PerformAuth_OIAP(hTPM, TPM_ORD_OwnerReadPubek,
						      hPolicy, &digest,
						      &ownerAuth)))
			return result;

		if ((result = TCSP_OwnerReadPubek(tcsContext, &ownerAuth, &pubEKSize, &pubEK)))
			return result;

		result = Trspi_HashInit(&hashCtx, TSS_HASH_SHA1);
		result |= Trspi_Hash_UINT32(&hashCtx, result);
		result |= Trspi_Hash_UINT32(&hashCtx, TPM_ORD_OwnerReadPubek);
		result |= Trspi_HashUpdate(&hashCtx, pubEKSize, pubEK);
		if ((result |= Trspi_HashFinal(&hashCtx, digest.digest)))
			return result;

		if ((result = obj_policy_validate_auth_oiap(hPolicy, &digest, &ownerAuth)))
			goto done;
	} else {
		if (pValidationData == NULL) {
			if ((result = internal_GetRandomNonce(tcsContext, &antiReplay))) {
				LogDebug("Failed to generate random nonce");
				return TSPERR(TSS_E_INTERNAL_ERROR);
			}
		} else {
			if (pValidationData->ulExternalDataLength < sizeof(antiReplay.nonce))
				return TSPERR(TSS_E_BAD_PARAMETER);

			memcpy(antiReplay.nonce, pValidationData->rgbExternalData,
			       sizeof(antiReplay.nonce));
		}

		/* call down to the TPM */
		if ((result = TCSP_ReadPubek(tcsContext, antiReplay, &pubEKSize, &pubEK, &checkSum)))
			return result;

		/* validate the returned hash, or set up the return so that the user can */
		if (pValidationData == NULL) {
			result = Trspi_HashInit(&hashCtx, TSS_HASH_SHA1);
			result |= Trspi_HashUpdate(&hashCtx, pubEKSize, pubEK);
			result |= Trspi_HashUpdate(&hashCtx, TCPA_SHA1_160_HASH_LEN,
						   antiReplay.nonce);
			if ((result |= Trspi_HashFinal(&hashCtx, digest.digest)))
				return result;

			/* check validation of the entire pubkey structure */
			if (memcmp(digest.digest, checkSum.digest, TCPA_SHA1_160_HASH_LEN)) {
				/* validation failed, unload the pubEK in order to hash
				 * just the pubKey portion of the pubEK. This is done on
				 * Atmel chips specifically.
				 */
				offset = 0;
				memset(&pubKey, 0, sizeof(TCPA_PUBKEY));
				if ((result = Trspi_UnloadBlob_PUBKEY(&offset, pubEK, &pubKey)))
					goto done;

				result = Trspi_HashInit(&hashCtx, TSS_HASH_SHA1);
				result |= Trspi_HashUpdate(&hashCtx, pubKey.pubKey.keyLength,
							   pubKey.pubKey.key);
				result |= Trspi_HashUpdate(&hashCtx, TCPA_SHA1_160_HASH_LEN,
							   antiReplay.nonce);
				if ((result |= Trspi_HashFinal(&hashCtx, digest.digest)))
					return result;

				if (memcmp(digest.digest, checkSum.digest, TCPA_SHA1_160_HASH_LEN)) {
					result = TSPERR(TSS_E_VALIDATION_FAILED);
					goto done;
				}
			}
		} else {
			/* validate the entire TCPA_PUBKEY structure */
			pValidationData->ulDataLength = pubEKSize + TCPA_SHA1_160_HASH_LEN;
			pValidationData->rgbData = calloc_tspi(tspContext,
							       pValidationData->ulDataLength);
			if (pValidationData->rgbData == NULL) {
				LogError("malloc of %u bytes failed.",
					 pValidationData->ulDataLength);
				pValidationData->ulDataLength = 0;
				return TSPERR(TSS_E_OUTOFMEMORY);
			}

			memcpy(pValidationData->rgbData, pubEK, pubEKSize);
			memcpy(&pValidationData->rgbData[pubEKSize], antiReplay.nonce,
			       TCPA_SHA1_160_HASH_LEN);

			pValidationData->ulValidationDataLength = TCPA_SHA1_160_HASH_LEN;
			pValidationData->rgbValidationData = calloc_tspi(tspContext,
									 TCPA_SHA1_160_HASH_LEN);
			if (pValidationData->rgbValidationData == NULL) {
				LogError("malloc of %d bytes failed.", TCPA_SHA1_160_HASH_LEN);
				pValidationData->ulValidationDataLength = 0;
				pValidationData->ulDataLength = 0;
				free_tspi(tspContext,pValidationData->rgbData);
				result = TSPERR(TSS_E_OUTOFMEMORY);
				goto done;
			}

			memcpy(pValidationData->rgbValidationData, checkSum.digest,
			       TCPA_SHA1_160_HASH_LEN);
		}
	}

	if ((result = obj_rsakey_add(tspContext, TSS_KEY_SIZE_2048|TSS_KEY_TYPE_LEGACY, &retKey)))
		return result;

	if ((result = obj_rsakey_set_pubkey(retKey, pubEK)))
		goto done;

	*phEndorsementPubKey = retKey;

done:
	free(pubEK);
	return result;
}
