/**
	@file lime_ffi-tester.cpp

	@author Johan Pascal

	@copyright 	Copyright (C) 2018  Belledonne Communications SARL

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

	@note: this file is compliant with c89 but the belle-sip.h include is not...
*/

#include <bctoolbox/tester.h>

#ifdef FFI_ENABLED

#include "lime/lime_ffi.h"
#include <belle-sip/belle-sip.h>
#include <stdio.h>
#include <string.h>

/****
 * HTTP stack
 ****/
static belle_sip_stack_t *stack=NULL;
static belle_http_provider_t *prov=NULL;

static int http_before_all(void) {
	char ca_root_path[256];
	time_t t;

	stack=belle_sip_stack_new(NULL);

	prov=belle_sip_stack_create_http_provider(stack,"0.0.0.0");

	belle_tls_crypto_config_t *crypto_config=belle_tls_crypto_config_new();

	sprintf(ca_root_path, "%s/data/", bc_tester_get_resource_dir_prefix());
	belle_tls_crypto_config_set_root_ca(crypto_config, ca_root_path);
	belle_http_provider_set_tls_crypto_config(prov,crypto_config);
	belle_sip_object_unref(crypto_config);

	/* init the pseudo rng seed or will always get the same random device name */
	srand((unsigned) time(&t));
	return 0;
}

static int http_after_all(void) {
	belle_sip_object_unref(prov);
	belle_sip_object_unref(stack);
	return 0;
}

/****
 * C version of some tester-utils functions and variables
 ****/
/* global counter */
static int success_counter;
static int failure_counter;

/* message */
const char message_pattern[] = "I have come here to chew bubble gum and kick ass, and I'm all out of bubble gum.";

/* wait for a counter to reach a value or timeout to occur, gives ticks to the belle-sip stack every SLEEP_TIME */
int wait_for(belle_sip_stack_t*s1,int* counter,int value,int timeout) {
	int retry=0;
#define SLEEP_TIME 50
	while (*counter!=value && retry++ <(timeout/SLEEP_TIME)) {
		if (s1) belle_sip_stack_sleep(s1,SLEEP_TIME);
	}
	if (*counter!=value) return FALSE;
	else return TRUE;
}

/* default value for the timeout */
static const int ffi_wait_for_timeout=4000;
static const int ffi_defaultInitialOPkBatchSize=5;

/* get random names for devices */
static const char ffi_charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

char *makeRandomDeviceName(const char *basename) {
	char *ret = malloc(strlen(basename)+7);
	size_t i;
	strcpy(ret, basename);
	for (i=strlen(basename); i<strlen(basename)+6; i++) {
		ret[i] = ffi_charset[rand()%strlen(ffi_charset)];
	}
	ret[i] = '\0';
	return ret;
}

static void process_io_error(void *userData, const belle_sip_io_error_event_t *event) {
	lime_ffi_processX3DHServerResponse((lime_ffi_data_t)userData, 0, NULL, 0);
}


static void process_response(void *userData, const belle_http_response_event_t *event) {
	/* note: userData is directly set to the requested lime_ffi_data_t but it may hold a structure including it if needed */
	if (event->response){
		int code=belle_http_response_get_status_code(event->response);
		belle_sip_message_t *message = BELLE_SIP_MESSAGE(event->response);
		/* all raw data access functions in lime use uint8_t *, so safely cast the body pointer to it, it's just a data stream pointer anyway */
		const uint8_t *body = (const uint8_t *)belle_sip_message_get_body(message);
		int bodySize = belle_sip_message_get_body_size(message);
		lime_ffi_processX3DHServerResponse((lime_ffi_data_t)userData, code, body, bodySize);
	} else {
		lime_ffi_processX3DHServerResponse((lime_ffi_data_t)userData, 0, NULL, 0);
	}
}

/* this emulate a network transmission: bob got a mailbox (2 buffers actually) where we can post/retrieve data to/from */
/* Let's just hope that no message would be more than 1024 bytes long in this test */
static uint8_t bobDRmessageMailbox[1024];
static size_t bobDRmessageMailboxSize;
static uint8_t bobCipherMessageMailbox[1024];
static size_t bobCipherMessageMailboxSize;

static void sendMessageTo(const char *recipient, const uint8_t *DRmessage, const size_t DRmessageSize, const uint8_t *cipherMessage, const size_t cipherMessageSize) {
	if (strcmp(recipient,"bob") == 0) {
		memcpy(bobDRmessageMailbox, DRmessage, DRmessageSize);
		bobDRmessageMailboxSize = DRmessageSize;
		memcpy(bobCipherMessageMailbox, cipherMessage, cipherMessageSize);
		bobCipherMessageMailboxSize = cipherMessageSize;
		return;
	}

	BC_FAIL();
}

static void getMessageFor(const char *recipient, uint8_t *DRmessage, size_t *DRmessageSize, uint8_t *cipherMessage, size_t *cipherMessageSize) {
	if (strcmp(recipient,"bob") == 0) {
		memcpy(DRmessage, bobDRmessageMailbox, bobDRmessageMailboxSize);
		*DRmessageSize = bobDRmessageMailboxSize;
		bobDRmessageMailboxSize = 0; /* 'clean' the mailbox */
		memcpy(cipherMessage, bobCipherMessageMailbox,bobCipherMessageMailboxSize);
		*cipherMessageSize = bobCipherMessageMailboxSize;
		bobCipherMessageMailboxSize = 0; /* 'clean' the mailbox */
		return;
	}

	BC_FAIL();
}

/** @brief holds the data buffers where encryption output would be written
 */
typedef struct {
	lime_ffi_RecipientData_t *recipients;
	size_t recipientsSize;
	uint8_t *cipherMessage;
	size_t cipherMessageSize;
} encryptionBuffers_t;

/** @brief Post data to X3DH server.
 * Communication with X3DH server is entirely managed out of the lib lime, in this example code it is performed over HTTPS provided by belle-sip
 * Here the HTTPS stack provider prov is a static variable in global context so there is no need to capture it, it may be the case in real usage
 * This lambda prototype is defined in lime.hpp
 *
 * @param[in]	userData	pointer given when registering this callback
 * @param[in]	limeData	a pointer to an opaque structure, must be forwarded to the response callback where it is then passed back to lime
 * @param[in]	url		The URL of X3DH server
 * @param[in]	from		The local device id, used to identify user on the X3DH server, user identification and credential verification is out of lib lime scope.
 * 				Here identification is performed on test server via belle-sip authentication mechanism and providing the test user credentials
 * @param[in]	message		The data to be sent to the X3DH server
 * @param[in]	messageSize	Size of the message buffer
 *
 * Note: we do not use userData here as we access directly the http provider from a global variable but otherwise we should retrieve it using that pointer
 */
static void X3DHServerPost(void *userData, lime_ffi_data_t limeData, const char *url, const char *from, const uint8_t *message, const size_t message_size) {
	belle_http_request_listener_callbacks_t cbs={};
	belle_http_request_listener_t *l;
	belle_generic_uri_t *uri;
	belle_http_request_t *req;
	belle_sip_memory_body_handler_t *bh;

	bh = belle_sip_memory_body_handler_new_copy_from_buffer(message, message_size, NULL, NULL);

	uri=belle_generic_uri_parse(url);

	req=belle_http_request_create("POST",
			uri,
			belle_http_header_create("User-Agent", "lime"),
			belle_http_header_create("Content-type", "x3dh/octet-stream"),
			belle_http_header_create("From", from),
			NULL);

	belle_sip_message_set_body_handler(BELLE_SIP_MESSAGE(req),BELLE_SIP_BODY_HANDLER(bh));
	cbs.process_response=process_response;
	cbs.process_io_error=process_io_error;

	l=belle_http_request_listener_create_from_callbacks(&cbs, (void *)limeData);
	belle_sip_object_data_set(BELLE_SIP_OBJECT(req), "http_request_listener", l, belle_sip_object_unref); /* Ensure the listener object is destroyed when the request is destroyed */
	belle_http_provider_send_request(prov,req,l);
};

/* the status callback :
 * - when no user data is passed: just increase the success or failure global counter(we are doing a create/delete user)
 * - when user data is set: we are doing an encryption and it holds pointers to retrieve the encryption output
 */
static void statusCallback(void *userData, const enum lime_ffi_CallbackReturn status, const char *message) {
	if (status == lime_ffi_CallbackReturn_success) {
		success_counter++;
		/* if we have user data we're calling back from encrypt (real code shall use two different callback functions) */
		if (userData != NULL) {
			size_t i;
			/* here is the code processing the output when all went well.
			 * Send the message to recipient
			 * that function must, before returning, send or copy the data to send them later
			 * as it is likely this is our last chance to free the userData buffers
			 * In this example we know that bodDevice is in recipients[0], real code shall loop on recipients vector
			 */

			/* cast it back to encryptionBuffers */
			encryptionBuffers_t *buffers = (encryptionBuffers_t *)userData;
			/* post them to bob */
			sendMessageTo("bob", buffers->recipients[0].DRmessage, buffers->recipients[0].DRmessageSize, buffers->cipherMessage, buffers->cipherMessageSize);

			/* Bob and Alice verified each other keys before encryption, we can check that the peerStatus of Bob is trusted */
			BC_ASSERT_EQUAL(buffers->recipients[0].peerStatus, lime_ffi_PeerDeviceStatus_trusted, int, "%d");

			/* and free the user Data buffers */
			for(i=0; i<buffers->recipientsSize; i++) {
				free(buffers->recipients[i].deviceId);
				free(buffers->recipients[i].DRmessage);
			}
			free(buffers->recipients);
			free(buffers->cipherMessage);
			free(buffers);
		}
	} else {
		failure_counter++;
	}
}
 /* Basic usage scenario
 * - Alice and Bob register themselves on X3DH server(use randomized device Ids to allow test server to run several test in parallel)
 * - Alice encrypt a message for Bob (this will fetch Bob's key from server)
 * - Bob decrypt alice message
 *
 *   @param[in] curve		Lime can run with cryptographic operations based on curve25519 or curve448, set by this parameter in this test.
 *   				One X3DH server runs on one type of key and all clients must use the same
 *   @param[in]	dbBaseFilename	The local filename for users will be this base.<alice/bob>.<curve type>.sqlite3
 *   @param[x3dh_server_url]	The URL (including port) of the X3DH server
 */
static void ffi_helloworld_basic_test(const enum lime_ffi_CurveId curve, const char *dbBaseFilename, const char *x3dh_server_url) {
	/* users databases names: baseFilename.<alice/bob>.<curve id>.sqlite3 */
	char dbFilenameAlice[512];
	char dbFilenameBob[512];
	sprintf(dbFilenameAlice, "%s.alice.%s.sqlite3", dbBaseFilename, (curve == lime_ffi_CurveId_c25519)?"C25519":"C448");
	sprintf(dbFilenameBob, "%s.bob.%s.sqlite3", dbBaseFilename, (curve == lime_ffi_CurveId_c25519)?"C25519":"C448");

	remove(dbFilenameAlice); /* delete the database file if already exists */
	remove(dbFilenameBob); /* delete the database file if already exists */

	/* reset counters */
	success_counter = 0;
	failure_counter = 0;
	int expected_success=0;

	/* create Random devices names (in case we use a shared test server, devices id shall be the GRUU, X3DH/Lime does not connect user (sip:uri) and device (gruu)
	 * From Lime perspective, only devices exists and they must be uniquely identifies on the X3DH server.
	 */
	char *aliceDeviceId = makeRandomDeviceName("alice.");
	char *bobDeviceId = makeRandomDeviceName("bob.");

	/* create Managers : they will open/create the database given in first parameter, and use the function given in second one to communicate with server.
	 * Any application using Lime shall create one LimeManager only, even in case of multiple users managed by the application.
	 */
	lime_manager_t aliceManager, bobManager;
	lime_ffi_manager_init(&aliceManager, dbFilenameAlice, X3DHServerPost, NULL);
	lime_ffi_manager_init(&bobManager, dbFilenameBob, X3DHServerPost, NULL);

	/* create users */
	lime_ffi_create_user(aliceManager, aliceDeviceId, x3dh_server_url, curve, ffi_defaultInitialOPkBatchSize, statusCallback, NULL);
	BC_ASSERT_TRUE(wait_for(stack, &success_counter, ++expected_success, ffi_wait_for_timeout));

	lime_ffi_create_user(bobManager, bobDeviceId, x3dh_server_url, curve, ffi_defaultInitialOPkBatchSize, statusCallback, NULL);
	BC_ASSERT_TRUE(wait_for(stack, &success_counter, ++expected_success, ffi_wait_for_timeout));

	/************** Identity verifications ************************/
	/*  Retrieve from Managers Bob and Alice device Identity Key */
	uint8_t aliceIk[64]; /* maximum size of an ECDSA Ik shall be 57 if we're using curve 448 */
	size_t aliceIkSize = 64;
	uint8_t bobIk[64]; /* maximum size of an ECDSA Ik shall be 57 if we're using curve 448 */
	size_t bobIkSize = 64;
	BC_ASSERT_EQUAL(lime_ffi_get_selfIdentityKey(aliceManager, aliceDeviceId, aliceIk, &aliceIkSize), LIME_FFI_SUCCESS, int, "%d");
	BC_ASSERT_EQUAL(lime_ffi_get_selfIdentityKey(bobManager, bobDeviceId, bobIk, &bobIkSize), LIME_FFI_SUCCESS, int, "%d");

	/* libsignal uses fingerprints, linphone inserts the key in SDP and then build a ZRTP auxiliary secret out of it
	 * SAS validation with matching auxiliary secret confirms that keys have been exchanged correctly
	 *
	 * There is no need to provide local device reference when setting a key as all peer devices identity infornations are
	 * shared between local devices.
	 *
	 * Last parameter is the value of trust flag, it can be reset(in case of SAS reset) by calling again this function and setting it to false.
	 *
	 * This call can be performed before or after the beginning of a Lime conversation, if something is really bad happen, it will generate an exception.
	 * When calling it with true as trusted flag after a SAS validation confirms the peer identity key, if an exception is raised
	 * it MUST be reported to user as it means that all previously established Lime session with that device were actually compromised(or someone broke ZRTP)
	*/
	BC_ASSERT_EQUAL(lime_ffi_set_peerDeviceStatus(aliceManager, bobDeviceId, bobIk, bobIkSize, lime_ffi_PeerDeviceStatus_trusted), LIME_FFI_SUCCESS, int, "%d");
	BC_ASSERT_EQUAL(lime_ffi_set_peerDeviceStatus(bobManager, aliceDeviceId, aliceIk, aliceIkSize, lime_ffi_PeerDeviceStatus_trusted), LIME_FFI_SUCCESS, int, "%d");

	/************** SENDER SIDE CODE *****************************/
	/* encrypt, parameters are:
	 *      - localDeviceId to select which of the users managed by the LimeManager we shall use to perform the encryption (in our example we have only one local device).
	 *      - recipientUser: an id of the recipient user (which can hold several devices), typically its sip:uri
	 *      - RecipientData vector (see below), list all recipient devices, will hold their DR message
	 *      - plain message
	 *      - cipher message (this one must then be distributed to all recipients devices)
	 *      - a callback
	 *      Any parameter being a char * is expected to be a null terminated string

	 * [verify] before encryption we can verify that recipient identity is a trusted peer(and eventually decide to not perform the encryption if it is not)
	 * This information will be provided by the encrypt function anyway for each recipient device
	 * Here Bob's device is trusted as we just set its identity as verified
	 */
	BC_ASSERT_TRUE(lime_ffi_get_peerDeviceStatus(aliceManager, bobDeviceId) == lime_ffi_PeerDeviceStatus_trusted);

	/*** alice encrypts a message to bob, all parameters given to encrypt function are shared_ptr. ***/
	/* The encryption generates:
	 *      - one common cipher message which must be sent to all recipient devices(depends on encryption policy, message length and recipient number, it may be actually empty)
	 *      - a cipher header per recipient device, each recipient device shall receive its specific one
	 */

	/*  prepare the data: alloc memory for the recipients data */
	size_t DRmessageSize = 0;
	size_t cipherMessageSize = 0;
	/* get maximum buffer size. The returned values are maximum and both won't be reached at the same time */
	size_t message_patternSize = strlen(message_pattern)+1; /* get the NULL termination char too */
	lime_ffi_encryptOutBuffersMaximumSize(message_patternSize, curve, &DRmessageSize, &cipherMessageSize);

	/* these buffer must be allocated and not local variables as we must be able to retrieve it from the callback which would be out of the scope of this function */
	lime_ffi_RecipientData_t *recipients = malloc(1*sizeof(lime_ffi_RecipientData_t));
	uint8_t *cipherMessage = malloc(cipherMessageSize);

	/* prepare buffers, deviceId and DRmessage are allocted and will be freed when encryption is completed */
	recipients[0].deviceId = malloc(strlen(bobDeviceId)+1);
	strcpy(recipients[0].deviceId, bobDeviceId);
	recipients[0].peerStatus = lime_ffi_PeerDeviceStatus_unknown; /* be sure this value is not lime_ffi_PeerDeviceStatus_fail or this device will be ignored */
	recipients[0].DRmessage = malloc(DRmessageSize);
	recipients[0].DRmessageSize = DRmessageSize;

	/* this struct will hold pointers to the needed buffers during encryption, it also must be allocated */
	encryptionBuffers_t *userData = malloc(sizeof(encryptionBuffers_t));
	userData->recipients = recipients;
	userData->recipientsSize = 1;
	userData->cipherMessage = cipherMessage;
	userData->cipherMessageSize = cipherMessageSize;

	/* encrypt, the plain message here is char * but passed as a uint8_t *, it can hold any binary content(including '\0') its size is given by a separate parameter */
	BC_ASSERT_EQUAL(lime_ffi_encrypt(aliceManager, aliceDeviceId, "bob", recipients, 1, (const uint8_t *const)message_pattern, message_patternSize, cipherMessage, &cipherMessageSize, statusCallback, userData, lime_ffi_EncryptionPolicy_cipherMessage), LIME_FFI_SUCCESS, int, "%d");

	/* in real sending situation, the local instance of pointers are destroyed by exiting the function where they've been declared
	 * and where we called the encrypt function. (The lime_ffi_manager_t shall instead never be destroyed until the application terminates)
	 * to simulate this, we set them to NULL
	 */
	userData = NULL;
	recipients = NULL;
	cipherMessage = NULL;
	/****** end of SENDER SIDE CODE ******************************/

	/************** SYNCHRO **************************************/
	/* this is just waiting for the callback to increase the operation_success field in counters
	 * sending ticks to the belle-sip stack in order to process messages
	 */
	BC_ASSERT_TRUE(wait_for(stack, &success_counter, ++expected_success, ffi_wait_for_timeout));
	/****** end of  SYNCHRO **************************************/

	/************** RECIPIENT SIDE CODE **************************/
	/* retrieve message, in real situation the server shall fan-out only the part we need or client shall parse in the DRmessages to retrieve the one addressed to him.
	 * Note: here we just use the aliceDeviceId variable, in real situation, recipient shall extract from incoming message the sender's GRUU
	 * use buffer of 1024, it shall be OK in this test, real code would get messages in a way avoiding this.
	 */
	uint8_t bobReceivedDRmessage[1024];
	size_t bobReceivedDRmessageSize = 1024;
	uint8_t bobReceivedCipherMessage[1024];
	size_t bobReceivedCipherMessageSize;
	getMessageFor("bob", bobReceivedDRmessage, &bobReceivedDRmessageSize, bobReceivedCipherMessage, &bobReceivedCipherMessageSize);

	if (bobReceivedDRmessageSize>0 && bobReceivedCipherMessageSize>0) { /* we encrypted with cipherMessage policy, so we will have a cipher Message */
		/* before decryption we can verify that sender is a trusted peer,
		 * it is not really needed as this information will be provided by the decrypt function anyway
		 */
		BC_ASSERT_TRUE(lime_ffi_get_peerDeviceStatus(bobManager, aliceDeviceId) == lime_ffi_PeerDeviceStatus_trusted);

		/* decrypt */
		size_t decryptedMessageSize = (bobReceivedCipherMessageSize>bobReceivedDRmessageSize)?bobReceivedCipherMessageSize:bobReceivedDRmessageSize; /* actual ciphered message is either in cipherMessage or DRmessage, just allocated a buffer the size of the largest one of the two.*/
		/* it is the first time bob's Device is in communication with Alice's one via message
		 * but they already exchanged their identity keys so they Bob's device trust Alice's one since the first incoming message
		 */
		uint8_t *decryptedMessage = malloc(decryptedMessageSize);
		BC_ASSERT_TRUE(lime_ffi_decrypt(bobManager, bobDeviceId, "bob", aliceDeviceId, bobReceivedDRmessage, bobReceivedDRmessageSize, bobReceivedCipherMessage, bobReceivedCipherMessageSize, decryptedMessage, &decryptedMessageSize) == lime_ffi_PeerDeviceStatus_trusted);

		/* check we got the original message back */
		BC_ASSERT_EQUAL(message_patternSize, decryptedMessageSize, int, "%d");
		BC_ASSERT_TRUE(strncmp(message_pattern, (char *)decryptedMessage, (message_patternSize<decryptedMessageSize)?message_patternSize:decryptedMessageSize)==0);

		free(decryptedMessage);
	} else { /* we didn't got any message for Bob */
		BC_FAIL();
	}
	/******* end of RECIPIENT SIDE CODE **************************/

	/************** Users maintenance ****************************/
	/* Around once a day the update function shall be called on LimeManagers
	 * it will perform localStorage cleanings
	 * update of cryptographic material (Signed Pre-key and One-time Pre-keys)
	 * The update take as optionnal parameters :
	 *  - lower bound for One-time Pre-key available on server
	 *  - One-time Pre-key batch size to be generated and uploaded if lower limit on server is reached
	 *
	 * Important : Avoid calling this function when connection to network is impossible
	 * try to first fetch any available message on server, process anything and then update
	 *
	 * This update shall have no effect as Alice still have ffi_defaultInitialOPkBatchSize keys on X3DH server
	 */
	lime_ffi_update(aliceManager, statusCallback, NULL, ffi_defaultInitialOPkBatchSize, 3);  /* if less than ffi_defaultInitialOPkBatchSize keys are availables on server, upload a batch of 3, typical values shall be higher. */
	/* That one instead shall upload 3 new OPks to server as we used one of Bob's keys */
	lime_ffi_update(bobManager, statusCallback, NULL, ffi_defaultInitialOPkBatchSize, 3); /* if less than ffi_defaultInitialOPkBatchSize keys are availables on server, upload a batch of 3, typical values shall be higher. */
	/* wait for updates to complete */
	expected_success += 2;
	BC_ASSERT_TRUE(wait_for(stack, &success_counter, expected_success, ffi_wait_for_timeout));
	/******* end of Users maintenance ****************************/

	/******* cleaning                   *************************/
	/* delete users */
	lime_ffi_delete_user(aliceManager, aliceDeviceId, statusCallback, NULL);
	lime_ffi_delete_user(bobManager, bobDeviceId, statusCallback, NULL);
	expected_success += 2;
	BC_ASSERT_TRUE(wait_for(stack, &success_counter, expected_success, ffi_wait_for_timeout));

	lime_ffi_manager_destroy(aliceManager);
	lime_ffi_manager_destroy(bobManager);

	free(aliceDeviceId);
	free(bobDeviceId);
}

static void ffi_helloworld_basic(void) {
	/* TODO: get the server and port from command line arguments */
	char serverURL[512];
	sprintf(serverURL, "https://%s:%s", "localhost", "25519");
	/* run the test on Curve25519 and Curve448 based encryption if available */
#ifdef EC25519_ENABLED
	ffi_helloworld_basic_test(lime_ffi_CurveId_c25519, "ffi_helloworld_basic", serverURL);
#endif
#ifdef EC448_ENABLED
	sprintf(serverURL, "https://%s:%s", "localhost", "25520");
	ffi_helloworld_basic_test(lime_ffi_CurveId_c448, "ffi_helloworld_basic", serverURL);
#endif
}


static test_t tests[] = {
	TEST_NO_TAG("FFI Hello World", ffi_helloworld_basic),
};

test_suite_t lime_ffi_test_suite = {
	"FFI",
	http_before_all,
	http_after_all,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};

#else /* FFI_ENABLED */
test_suite_t lime_ffi_test_suite = {
	"FFI",
	NULL,
	NULL,
	NULL,
	NULL,
	0,
	NULL
};

#endif /* FFI_ENABLED */