
/*
 * Licensed Materials - Property of IBM
 *
 * trousers - An open source TCG Software Stack
 *
 * (C) Copyright International Business Machines Corp. 2004
 *
 */


#ifndef _REQ_MGR_H_
#define _REQ_MGR_H_

#include <pthread.h>

struct tpm_req_mgr
{
	pthread_mutex_t queue_lock;
};

TSS_RESULT req_mgr_init();
TSS_RESULT req_mgr_final();
TSS_RESULT req_mgr_submit_req(BYTE *);

#endif
