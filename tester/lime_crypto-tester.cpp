/*
	lime_crypto-tester.cpp
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
*/

#include "lime_log.hpp"
#include "lime-tester.hpp"
#include "lime-tester-utils.hpp"
#include "lime_keys.hpp"
#include "lime_crypto_primitives.hpp"

#include <bctoolbox/tester.h>
#include <bctoolbox/port.h>
#include <bctoolbox/exception.hh>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdio.h>
#include <string.h>

using namespace::std;
using namespace::lime;

constexpr uint64_t BENCH_TIMING_MS=200;

/* Function */
static void snprintSI(std::string &output, double x, const char *unit, const char *spacer = " ") {
	const char *small[] = {" ","m","µ","n","p"};
	const char *big[] = {" ","k","M","G","T"};

	constexpr size_t tempBufferSize = 100;
	char tempBuffer[tempBufferSize]; // hoping no one will use this function to print more than 100 chars...

	if (12+strlen(spacer)+strlen(unit)>sizeof(tempBuffer)) {// 6 digit, 1 point, 2 digits + unit + 1 prefix + spacer + NULL term
	throw BCTBX_EXCEPTION << "snprintSI tmpBuffer is too small to hold your data";
	}

	if (x < 1) {
		unsigned di=0;
		for (di=0; di<sizeof(small)/sizeof(*small)-1 && x && x < 1; di++) {
			x *= 1000.0;
		}
		snprintf(tempBuffer, sizeof(tempBuffer), "%6.2f%s%s%s", x, spacer, small[di], unit);
	} else {
		unsigned di=0;
		for (di=0; di<sizeof(big)/sizeof(*big)-1 && x && x >= 1000; di++) {
			x /= 1000.0;
		}
		snprintf(tempBuffer, sizeof(tempBuffer), "%6.2f%s%s%s", x, spacer, big[di], unit);
	}

	output = tempBuffer;
}

template <typename Curve>
void keyExchange_test(void) {
	/* We need a RNG */
	std::shared_ptr<RNG> rng = make_RNG();
	/* Create Alice and Bob ECDH context */
	std::shared_ptr<keyExchange<Curve>> Alice = make_keyExchange<Curve>();
	std::shared_ptr<keyExchange<Curve>> Bob = make_keyExchange<Curve>();

	/* Generate key pairs */
	Alice->createKeyPair(rng);
	Bob->createKeyPair(rng);

	/* Exchange keys */
	Alice->set_peerPublic(Bob->get_selfPublic());
	Bob->set_peerPublic(Alice->get_selfPublic());

	/* Compute shared secret */
	Alice->computeSharedSecret();
	Bob->computeSharedSecret();

	/* Compare them */
	BC_ASSERT_TRUE(Alice->get_sharedSecret()==Bob->get_sharedSecret());
}

template <typename Curve>
void keyExchange_bench(uint64_t runTime_ms) {
	constexpr size_t batch_size = 100;

	/* We need a RNG */
	std::shared_ptr<RNG> rng = make_RNG();

	/* Create Alice and Bob ECDH context */
	std::shared_ptr<keyExchange<Curve>> Alice = make_keyExchange<Curve>();
	std::shared_ptr<keyExchange<Curve>> Bob = make_keyExchange<Curve>();

	auto start = bctbx_get_cur_time_ms();
	uint64_t span=0;
	size_t runCount = 0;

	while (span<runTime_ms) {
		for (size_t i=0; i<batch_size; i++) {
			/* Generate key pairs */
			Alice->createKeyPair(rng);
			Bob->createKeyPair(rng);
		}
		span = bctbx_get_cur_time_ms() - start;
		runCount += batch_size;
	}

	auto freq = 2000*runCount/static_cast<double>(span);
	std::string freq_unit, period_unit;
	snprintSI(freq_unit, freq, "keys/s");
	snprintSI(period_unit, 1/freq, "s/keys");
	LIME_LOGI<<"Key generation "<<int(2*runCount)<<" ECDH keys in "<<int(span)<<" ms : "<<period_unit<<" "<<freq_unit<<endl;

	/* Exchange keys */
	Alice->set_peerPublic(Bob->get_selfPublic());
	Bob->set_peerPublic(Alice->get_selfPublic());

	start = bctbx_get_cur_time_ms();
	span=0;
	runCount = 0;

	while (span<runTime_ms) {
		for (size_t i=0; i<batch_size; i++) {
			/* Compute shared secret */
			Alice->computeSharedSecret();
		}
		span = bctbx_get_cur_time_ms() - start;
		runCount += batch_size;
	}
	freq = 1000*runCount/static_cast<double>(span);
	snprintSI(freq_unit, freq, "computations/s");
	snprintSI(period_unit, 1/freq, "s/computation");
	LIME_LOGI<<"Shared Secret "<<int(runCount)<<" computations in "<<int(span)<<" ms : "<<period_unit<<" "<<freq_unit<<endl<<endl;
}

static void exchange(void) {
#ifdef EC25519_ENABLED
	keyExchange_test<C255>();
	if (bench) {
		LIME_LOGI<<"Bench for Curve 25519:"<<endl;
		keyExchange_bench<C255>(BENCH_TIMING_MS);
	}
#endif
#ifdef EC448_ENABLED
	keyExchange_test<C448>();
	if (bench) {
		LIME_LOGI<<"Bench for Curve 448:"<<endl;
		keyExchange_bench<C448>(BENCH_TIMING_MS);
	}
#endif
}

/**
 * Testing sign, verify and DSA to keyEchange key conversion
 * Scenario:
 * - Alice and Bob generate a Signature key pair
 * - They both sign a message, exchange it and verify it
 * - each of them convert their private Signature key into a private keyExchange one and derive the matching public key
 * - each of them convert the peer Signature public key into a keyExchange public key
 * - both compute the shared secret and compare
 */
template <typename Curve>
void signAndVerify_test(void) {
	/* We need a RNG */
	auto rng = make_RNG();
	/* Create Alice, Bob, Vera Signature context */
	auto AliceDSA = make_Signature<Curve>();
	auto BobDSA = make_Signature<Curve>();
	auto Vera = make_Signature<Curve>();

	std::string aliceMessageString{"Lluchiwn ein gwydrau achos Ni yw y byd Ni yw y byd, Ni yw y byd, Carwn ein gelynion achos Ni yw y byd. Ni yw y byd, dewch bawb ynghyd, Tynnwn ein dillad achos Ni yw y byd. Ni yw y byd, Ni yw y byd, Dryswn ein cyfoedion achos Ni yw y byd. Ni yw y byd, dewch bawb ynghyd, Gwaeddwn yn llawen achos Ni yw y byd."};
	std::string bobMessageString{"Neidiwn i'r awyr achos ni yw y byd Ni yw y byd, dewch bawb ynghyd, Chwalwn ddisgyrchiant achos Ni yw y byd, Rowliwn yn y rhedyn achos Ni yw y byd. Rhyddhawn ein penblethau! Ni yw y byd, dewch bawb ynghyd, Paratown am chwyldro achos Ni yw y byd"};
	std::vector<uint8_t> aliceMessage{aliceMessageString.cbegin(), aliceMessageString.cend()};
	std::vector<uint8_t> bobMessage{bobMessageString.cbegin(), bobMessageString.cend()};

	/* Generate Signature key pairs */
	AliceDSA->createKeyPair(rng);
	BobDSA->createKeyPair(rng);

	/* Sign messages*/
	DSA<Curve, lime::DSAtype::signature> aliceSignature;
	DSA<Curve, lime::DSAtype::signature> bobSignature;
	AliceDSA->sign(aliceMessage, aliceSignature);
	BobDSA->sign(bobMessage, bobSignature);

	/* Vera check messages authenticity */
	Vera->set_public(AliceDSA->get_public());
	BC_ASSERT_TRUE(Vera->verify(aliceMessage, aliceSignature));
	BC_ASSERT_FALSE(Vera->verify(bobMessage, aliceSignature));
	Vera->set_public(BobDSA->get_public());
	BC_ASSERT_FALSE(Vera->verify(aliceMessage, bobSignature));
	BC_ASSERT_TRUE(Vera->verify(bobMessage, bobSignature));

	/* Bob and Alice create keyExchange context */
	auto AliceKeyExchange = make_keyExchange<Curve>();
	auto BobKeyExchange = make_keyExchange<Curve>();

	/* Convert keys */
	AliceKeyExchange->set_secret(AliceDSA->get_secret()); // auto convert from DSA to X format
	AliceKeyExchange->deriveSelfPublic(); // derive public from private
	AliceKeyExchange->set_peerPublic(BobDSA->get_public()); // import Bob DSA public key

	BobKeyExchange->set_secret(BobDSA->get_secret()); // convert from DSA to X format
	BobKeyExchange->set_selfPublic(BobDSA->get_public()); // convert from DSA to X format
	BobKeyExchange->set_peerPublic(AliceDSA->get_public()); // import Alice DSA public key

	/* Compute shared secret */
	AliceKeyExchange->computeSharedSecret();
	BobKeyExchange->computeSharedSecret();

	/* Compare them */
	BC_ASSERT_TRUE(AliceKeyExchange->get_sharedSecret()==BobKeyExchange->get_sharedSecret());
}

template <typename Curve>
void signAndVerify_bench(uint64_t runTime_ms ) {
	constexpr size_t batch_size = 100;

	/* We need a RNG */
	auto rng = make_RNG();
	/* Create Alice, Vera Signature context */
	auto Alice = make_Signature<Curve>();
	auto Vera = make_Signature<Curve>();

	// the message to sign is a public Key for keyExchange algo
	auto keyExchangeContext = make_keyExchange<Curve>();
	keyExchangeContext->createKeyPair(rng);
	auto XpublicKey = keyExchangeContext->get_selfPublic();

	auto start = bctbx_get_cur_time_ms();
	uint64_t span=0;
	size_t runCount = 0;

	while (span<runTime_ms) {
		for (size_t i=0; i<batch_size; i++) {
			/* Generate Signature key pairs */
			Alice->createKeyPair(rng);
		}
		span = bctbx_get_cur_time_ms() - start;
		runCount += batch_size;
	}

	auto freq = 1000*runCount/static_cast<double>(span);
	std::string freq_unit, period_unit;
	snprintSI(freq_unit, freq, "generations/s");
	snprintSI(period_unit, 1/freq, "s/generation");
	LIME_LOGI<<"Generate "<<int(runCount)<<" Signature key pairs in "<<int(span)<<" ms : "<<period_unit<<" "<<freq_unit<<endl;

	start = bctbx_get_cur_time_ms();
	span=0;
	runCount = 0;

	/* Sign messages*/
	DSA<Curve, lime::DSAtype::signature> aliceSignature;

	while (span<runTime_ms) {
		for (size_t i=0; i<batch_size; i++) {
			Alice->sign(XpublicKey, aliceSignature);
		}
		span = bctbx_get_cur_time_ms() - start;
		runCount += batch_size;
	}

	freq = 1000*runCount/static_cast<double>(span);
	snprintSI(freq_unit, freq, "signatures/s");
	snprintSI(period_unit, 1/freq, "s/signature");
	LIME_LOGI<<"Sign "<<int(runCount)<<" messages "<<int(span)<<" ms : "<<period_unit<<" "<<freq_unit<<endl;

	start = bctbx_get_cur_time_ms();
	span=0;
	runCount = 0;
	/* Vera check messages authenticity */
	Vera->set_public(Alice->get_public());
	while (span<runTime_ms) {
		for (size_t i=0; i<batch_size; i++) {
			Vera->verify(XpublicKey, aliceSignature);
		}
		span = bctbx_get_cur_time_ms() - start;
		runCount += batch_size;
	}

	freq = 1000*runCount/static_cast<double>(span);
	snprintSI(freq_unit, freq, "verifies/s");
	snprintSI(period_unit, 1/freq, "s/verify");
	LIME_LOGI<<"Verify "<<int(runCount)<<" messages "<<int(span)<<" ms : "<<period_unit<<" "<<freq_unit<<endl<<endl;

	BC_ASSERT_TRUE(Vera->verify(XpublicKey, aliceSignature));
}

static void signAndVerify(void) {
#ifdef EC25519_ENABLED
	signAndVerify_test<C255>();
	if (bench) {
		LIME_LOGI<<"Bench for Curve 25519:"<<endl;
		signAndVerify_bench<C255>(BENCH_TIMING_MS);
	}
#endif
#ifdef EC448_ENABLED
	signAndVerify_test<C448>();
	if (bench) {
		LIME_LOGI<<"Bench for Curve 448:"<<endl;
		signAndVerify_bench<C448>(BENCH_TIMING_MS);
	}
#endif
}

static void hashMac_KDF_bench(uint64_t runTime_ms, size_t IKMsize) {
	size_t batch_size = 500;
	/* Generate random input and info */
	auto rng_source = make_RNG();
	/* input lenght is the same used by X3DH */
	std::vector<uint8_t> IKM(IKMsize, 0);
	rng_source->randomize(IKM.data(), IKM.size());
	std::string info{"The lime tester info string"};
	std::vector<uint8_t> salt(SHA512::ssize(), 0); // salt is the same used in X3DH
	std::array<uint8_t, 64> output;

	auto start = bctbx_get_cur_time_ms();
	uint64_t span=0;
	size_t runCount = 0;


	while (span<runTime_ms) {
		for (size_t i=0; i<batch_size; i++) {
			/* Run the HKDF function asking for 64 bytes(no use of the HKDF function requests more than that in the lime library) */
			HMAC_KDF<SHA512>(salt, IKM, info, output.data(), output.size());
		}
		span = bctbx_get_cur_time_ms() - start;
		runCount += batch_size;
	}

	auto freq = 1000*runCount/static_cast<double>(span);
	std::string freq_unit, period_unit;
	snprintSI(freq_unit, freq, "derivations/s");
	snprintSI(period_unit, 1/freq, "s/derivation");
	LIME_LOGI<<"Derive "<<int(runCount)<<" key material in "<<int(span)<<" ms : "<<period_unit<<" "<<freq_unit<<endl<<endl;
}

static void hashMac_KDF(void) {
	/* test patterns from RFC5869 generated for SHA512 using https://github.com/casebeer/python-hkdf */
	/* test A.1 */
	std::vector<uint8_t> IKM{0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
	std::vector<uint8_t> salt{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
	std::vector<uint8_t> info{0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
	std::vector<uint8_t> OKM{0x83, 0x23, 0x90, 0x08, 0x6c, 0xda, 0x71, 0xfb, 0x47, 0x62, 0x5b, 0xb5, 0xce, 0xb1, 0x68, 0xe4, 0xc8, 0xe2, 0x6a, 0x1a, 0x16, 0xed, 0x34, 0xd9, 0xfc, 0x7f, 0xe9, 0x2c, 0x14, 0x81, 0x57, 0x93, 0x38, 0xda, 0x36, 0x2c, 0xb8, 0xd9, 0xf9, 0x25, 0xd7, 0xcb};
	std::vector<uint8_t> output;
	output.resize(OKM.size());
	HMAC_KDF<SHA512>(salt, IKM, info, output.data(), output.size());
	BC_ASSERT_TRUE(OKM==output);

	/* test A.2 */
	IKM.assign({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f});
	salt.assign({0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf});
	info.assign({0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff});
	OKM.assign({0xce, 0x6c, 0x97, 0x19, 0x28, 0x05, 0xb3, 0x46, 0xe6, 0x16, 0x1e, 0x82, 0x1e, 0xd1, 0x65, 0x67, 0x3b, 0x84, 0xf4, 0x00, 0xa2, 0xb5, 0x14, 0xb2, 0xfe, 0x23, 0xd8, 0x4c, 0xd1, 0x89, 0xdd, 0xf1, 0xb6, 0x95, 0xb4, 0x8c, 0xbd, 0x1c, 0x83, 0x88, 0x44, 0x11, 0x37, 0xb3, 0xce, 0x28, 0xf1, 0x6a, 0xa6, 0x4b, 0xa3, 0x3b, 0xa4, 0x66, 0xb2, 0x4d, 0xf6, 0xcf, 0xcb, 0x02, 0x1e, 0xcf, 0xf2, 0x35, 0xf6, 0xa2, 0x05, 0x6c, 0xe3, 0xaf, 0x1d, 0xe4, 0x4d, 0x57, 0x20, 0x97, 0xa8, 0x50, 0x5d, 0x9e, 0x7a, 0x93});
	output.resize(OKM.size());
	HMAC_KDF<SHA512>(salt, IKM, info, output.data(), output.size());
	BC_ASSERT_TRUE(OKM==output);

	/* test A.3 */
	IKM.assign({0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b});
	salt.clear();
	info.clear();
	OKM.assign({0xf5, 0xfa, 0x02, 0xb1, 0x82, 0x98, 0xa7, 0x2a, 0x8c, 0x23, 0x89, 0x8a, 0x87, 0x03, 0x47, 0x2c, 0x6e, 0xb1, 0x79, 0xdc, 0x20, 0x4c, 0x03, 0x42, 0x5c, 0x97, 0x0e, 0x3b, 0x16, 0x4b, 0xf9, 0x0f, 0xff, 0x22, 0xd0, 0x48, 0x36, 0xd0, 0xe2, 0x34, 0x3b, 0xac});
	output.resize(OKM.size());
	HMAC_KDF<SHA512>(salt, IKM, info, output.data(), output.size());
	BC_ASSERT_TRUE(OKM==output);

	/* test A.4 */
	IKM.assign({0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b});
	salt.assign({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c});
	info.assign({0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9});
	OKM.assign({0x74, 0x13, 0xe8, 0x99, 0x7e, 0x02, 0x06, 0x10, 0xfb, 0xf6, 0x82, 0x3f, 0x2c, 0xe1, 0x4b, 0xff, 0x01, 0x87, 0x5d, 0xb1, 0xca, 0x55, 0xf6, 0x8c, 0xfc, 0xf3, 0x95, 0x4d, 0xc8, 0xaf, 0xf5, 0x35, 0x59, 0xbd, 0x5e, 0x30, 0x28, 0xb0, 0x80, 0xf7, 0xc0, 0x68});
	output.resize(OKM.size());
	HMAC_KDF<SHA512>(salt, IKM, info, output.data(), output.size());
	BC_ASSERT_TRUE(OKM==output);

	/* test A.7 */
	IKM.assign({0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c});
	salt.clear();
	info.clear();
	OKM.assign({0x14, 0x07, 0xd4, 0x60, 0x13, 0xd9, 0x8b, 0xc6, 0xde, 0xce, 0xfc, 0xfe, 0xe5, 0x5f, 0x0f, 0x90, 0xb0, 0xc7, 0xf6, 0x3d, 0x68, 0xeb, 0x1a, 0x80, 0xea, 0xf0, 0x7e, 0x95, 0x3c, 0xfc, 0x0a, 0x3a, 0x52, 0x40, 0xa1, 0x55, 0xd6, 0xe4, 0xda, 0xa9, 0x65, 0xbb});
	output.resize(OKM.size());
	HMAC_KDF<SHA512>(salt, IKM, info, output.data(), output.size());
	BC_ASSERT_TRUE(OKM==output);


	/* Run benchmarks */
	if (bench) {
		size_t IKMsize = 0;
	#ifdef EC25519_ENABLED
		IKMsize = DSA<C255, lime::DSAtype::publicKey>::ssize()+4*X<C255, lime::Xtype::sharedSecret>::ssize();
		LIME_LOGI<<"Bench for SHA512 on Curve 25519 X3DH sized IKM("<<IKMsize<<" bytes)"<<endl;
		hashMac_KDF_bench(BENCH_TIMING_MS, IKMsize);
	#endif
	#ifdef EC448_ENABLED
		IKMsize = DSA<C448, lime::DSAtype::publicKey>::ssize()+4*X<C448, lime::Xtype::sharedSecret>::ssize();
		LIME_LOGI<<"Bench for SHA512 on Curve 448 X3DH sized IKM("<<IKMsize<<" bytes)"<<endl;
		hashMac_KDF_bench(BENCH_TIMING_MS, IKMsize);
	#endif
	}
}

static void AEAD(void) {
	std::vector<uint8_t> cipher{};
	std::vector<uint8_t> tag{};
	std::vector<uint8_t> plain{};

	/* Test vectors for AES256-GCM128 from IEEE P1619.1/D22 - Annex D.3 */
	tag.resize(AES256GCM::tagSize());
	/* Test D3.1*/
	std::vector<uint8_t> key{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	std::vector<uint8_t> IV{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	std::vector<uint8_t> pattern_plain{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	std::vector<uint8_t> pattern_cipher{0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e, 0x07, 0x4e, 0xc5, 0xd3, 0xba, 0xf3, 0x9d, 0x18};
	std::vector<uint8_t> pattern_tag{0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99, 0x6b, 0xf0, 0x26, 0x5b, 0x98, 0xb5, 0xd4, 0x8a, 0xb9, 0x19};
	std::vector<uint8_t> AD{};
	AD.clear();
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.2 */
	key.assign({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	IV.assign({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	AD.assign({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	pattern_plain.clear();
	pattern_cipher.clear();
	pattern_tag.assign({0x2d, 0x45, 0x55, 0x2d, 0x85, 0x75, 0x92, 0x2b, 0x3c, 0xa3, 0xcc, 0x53, 0x84, 0x42, 0xfa, 0x26});
	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(tag==pattern_tag);

	/* Test D3.3 */
	key.assign({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	IV.assign({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	AD.assign({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	pattern_plain.assign({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	pattern_cipher.assign({0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e, 0x07, 0x4e, 0xc5, 0xd3, 0xba, 0xf3, 0x9d, 0x18});
	pattern_tag.assign({0xae, 0x9b, 0x17, 0x71, 0xdb, 0xa9, 0xcf, 0x62, 0xb3, 0x9b, 0xe0, 0x17, 0x94, 0x03, 0x30, 0xb4});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.4 */
	key.assign({0xfb, 0x76, 0x15, 0xb2, 0x3d, 0x80, 0x89, 0x1d, 0xd4, 0x70, 0x98, 0x0b, 0xc7, 0x95, 0x84, 0xc8, 0xb2, 0xfb, 0x64, 0xce, 0x60, 0x97, 0x8f, 0x4d, 0x17, 0xfc, 0xe4, 0x5a, 0x49, 0xe8, 0x30, 0xb7});
	IV.assign({0xdb, 0xd1, 0xa3, 0x63, 0x60, 0x24, 0xb7, 0xb4, 0x02, 0xda, 0x7d, 0x6f});
	AD.clear();
	pattern_plain.assign({0xa8, 0x45, 0x34, 0x8e, 0xc8, 0xc5, 0xb5, 0xf1, 0x26, 0xf5, 0x0e, 0x76, 0xfe, 0xfd, 0x1b, 0x1e});
	pattern_cipher.assign({0x5d, 0xf5, 0xd1, 0xfa, 0xbc, 0xbb, 0xdd, 0x05, 0x15, 0x38, 0x25, 0x24, 0x44, 0x17, 0x87, 0x04});
	pattern_tag.assign({0x4c, 0x43, 0xcc, 0xe5, 0xa5, 0x74, 0xd8, 0xa8, 0x8b, 0x43, 0xd4, 0x35, 0x3b, 0xd6, 0x0f, 0x9f});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.5 */
	key.assign({0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f});
	IV.assign({0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b});
	AD.assign({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13});
	pattern_plain.assign({0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37});
	pattern_cipher.assign({0x59, 0x1b, 0x1f, 0xf2, 0x72, 0xb4, 0x32, 0x04, 0x86, 0x8f, 0xfc, 0x7b, 0xc7, 0xd5, 0x21, 0x99, 0x35, 0x26, 0xb6, 0xfa, 0x32, 0x24, 0x7c, 0x3c});
	pattern_tag.assign({0x7d, 0xe1, 0x2a, 0x56, 0x70, 0xe5, 0x70, 0xd8, 0xca, 0xe6, 0x24, 0xa1, 0x6d, 0xf0, 0x9c, 0x08});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.6 */
	key.assign({0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f});
	IV.assign({0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b});
	AD.assign({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff});
	pattern_plain.assign({0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f});
	pattern_cipher.assign({0x59, 0x1b, 0x1f, 0xf2, 0x72, 0xb4, 0x32, 0x04, 0x86, 0x8f, 0xfc, 0x7b, 0xc7, 0xd5, 0x21, 0x99, 0x35, 0x26, 0xb6, 0xfa, 0x32, 0x24, 0x7c, 0x3c, 0x40, 0x57, 0xf3, 0xea, 0xe7, 0x54, 0x8c, 0xef});
	pattern_tag.assign({0xa1, 0xde, 0x55, 0x36, 0xe9, 0x7e, 0xdd, 0xdc, 0xcd, 0x26, 0xee, 0xb1, 0xb5, 0xff, 0x7b, 0x32});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());
	std::vector<uint8_t> repeatAD{};
	for (auto i=0; i<256; i++) {
		repeatAD.insert(repeatAD.end(), AD.cbegin(), AD.cend());
	}

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), repeatAD.data(), repeatAD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), repeatAD.data(), repeatAD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.7 */
	key.assign({0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f});
	IV.assign({0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b});
	AD.assign({0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f});
	pattern_plain.assign({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff});
	pattern_cipher.assign({0x79, 0x3b, 0x3f, 0xd2, 0x52, 0x94, 0x12, 0x24, 0xa6, 0xaf, 0xdc, 0x5b, 0xe7, 0xf5, 0x01, 0xb9, 0x15, 0x06, 0x96, 0xda, 0x12, 0x04, 0x5c, 0x1c, 0x60, 0x77, 0xd3, 0xca, 0xc7, 0x74, 0xac, 0xcf, 0xc3, 0xd5, 0x30, 0xd8, 0x48, 0xd6, 0x65, 0xd8, 0x1a, 0x49, 0xcb, 0xb5, 0x00, 0xb8, 0x8b, 0xbb, 0x62, 0x4a, 0xe6, 0x1d, 0x16, 0x67, 0x22, 0x9c, 0x30, 0x2d, 0xc6, 0xff, 0x0b, 0xb4, 0xd7, 0x0b, 0xdb, 0xbc, 0x85, 0x66, 0xd6, 0xf5, 0xb1, 0x58, 0xda, 0x99, 0xa2, 0xff, 0x2e, 0x01, 0xdd, 0xa6, 0x29, 0xb8, 0x9c, 0x34, 0xad, 0x1e, 0x5f, 0xeb, 0xa7, 0x0e, 0x7a, 0xae, 0x43, 0x28, 0x28, 0x9c, 0x36, 0x29, 0xb0, 0x58, 0x83, 0x50, 0x58, 0x1c, 0xa8, 0xb9, 0x7c, 0xcf, 0x12, 0x58, 0xfa, 0x3b, 0xbe, 0x2c, 0x50, 0x26, 0x04, 0x7b, 0xa7, 0x26, 0x48, 0x96, 0x9c, 0xff, 0x8b, 0xa1, 0x0a, 0xe3, 0x0e, 0x05, 0x93, 0x5d, 0xf0, 0xc6, 0x93, 0x74, 0x18, 0x92, 0xb7, 0x6f, 0xaf, 0x67, 0x13, 0x3a, 0xbd, 0x2c, 0xf2, 0x03, 0x11, 0x21, 0xbd, 0x8b, 0xb3, 0x81, 0x27, 0xa4, 0xd2, 0xee, 0xde, 0xea, 0x13, 0x27, 0x64, 0x94, 0xf4, 0x02, 0xcd, 0x7c, 0x10, 0x7f, 0xb3, 0xec, 0x3b, 0x24, 0x78, 0x48, 0x34, 0x33, 0x8e, 0x55, 0x43, 0x62, 0x87, 0x09, 0x2a, 0xc4, 0xa2, 0x6f, 0x5e, 0xa7, 0xea, 0x4a, 0xd6, 0x8d, 0x73, 0x15, 0x16, 0x39, 0xb0, 0x5b, 0x24, 0xe6, 0x8b, 0x98, 0x16, 0xd1, 0x39, 0x83, 0x76, 0xd8, 0xe4, 0x13, 0x85, 0x94, 0x75, 0x8d, 0xb9, 0xad, 0x3b, 0x40, 0x92, 0x59, 0xb2, 0x6d, 0xcf, 0xc0, 0x6e, 0x72, 0x2b, 0xe9, 0x87, 0xb3, 0x76, 0x7f, 0x70, 0xa7, 0xb8, 0x56, 0xb7, 0x74, 0xb1, 0xba, 0x26, 0x85, 0xb3, 0x68, 0x09, 0x14, 0x29, 0xfc, 0xcb, 0x8d, 0xcd, 0xde, 0x09, 0xe4});
	pattern_tag.assign({0x87, 0xec, 0x83, 0x7a, 0xbf, 0x53, 0x28, 0x55, 0xb2, 0xce, 0xa1, 0x69, 0xd6, 0x94, 0x3f, 0xcd});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.8 */
	key.assign({0xfb, 0x76, 0x15, 0xb2, 0x3d, 0x80, 0x89, 0x1d, 0xd4, 0x70, 0x98, 0x0b, 0xc7, 0x95, 0x84, 0xc8, 0xb2, 0xfb, 0x64, 0xce, 0x60, 0x97, 0x87, 0x8d, 0x17, 0xfc, 0xe4, 0x5a, 0x49, 0xe8, 0x30, 0xb7});
	IV.assign({0xdb, 0xd1, 0xa3, 0x63, 0x60, 0x24, 0xb7, 0xb4, 0x02, 0xda, 0x7d, 0x6f});
	AD.assign({0x36});
	pattern_plain.assign({0xa9});
	pattern_cipher.assign({0x0a});
	pattern_tag.assign({0xbe, 0x98, 0x7d, 0x00, 0x9a, 0x4b, 0x34, 0x9a, 0xa8, 0x0c, 0xb9, 0xc4, 0xeb, 0xc1, 0xe9, 0xf4});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.9 */
	key.assign({0xf8, 0xd4, 0x76, 0xcf, 0xd6, 0x46, 0xea, 0x6c, 0x23, 0x84, 0xcb, 0x1c, 0x27, 0xd6, 0x19, 0x5d, 0xfe, 0xf1, 0xa9, 0xf3, 0x7b, 0x9c, 0x8d, 0x21, 0xa7, 0x9c, 0x21, 0xf8, 0xcb, 0x90, 0xd2, 0x89});
	IV.assign({0xdb, 0xd1, 0xa3, 0x63, 0x60, 0x24, 0xb7, 0xb4, 0x02, 0xda, 0x7d, 0x6f});
	AD.assign({0x7b, 0xd8, 0x59, 0xa2, 0x47, 0x96, 0x1a, 0x21, 0x82, 0x3b, 0x38, 0x0e, 0x9f, 0xe8, 0xb6, 0x50, 0x82, 0xba, 0x61, 0xd3});
	pattern_plain.assign({0x90, 0xae, 0x61, 0xcf, 0x7b, 0xae, 0xbd, 0x4c, 0xad, 0xe4, 0x94, 0xc5, 0x4a, 0x29, 0xae, 0x70, 0x26, 0x9a, 0xec, 0x71});
	pattern_cipher.assign({0xce, 0x20, 0x27, 0xb4, 0x7a, 0x84, 0x32, 0x52, 0x01, 0x34, 0x65, 0x83, 0x4d, 0x75, 0xfd, 0x0f, 0x07, 0x29, 0x75, 0x2e});
	pattern_tag.assign({0xac, 0xd8, 0x83, 0x38, 0x37, 0xab, 0x0e, 0xde, 0x84, 0xf4, 0x74, 0x8d, 0xa8, 0x89, 0x9c, 0x15});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.10 */
	key.assign({0xdb, 0xbc, 0x85, 0x66, 0xd6, 0xf5, 0xb1, 0x58, 0xda, 0x99, 0xa2, 0xff, 0x2e, 0x01, 0xdd, 0xa6, 0x29, 0xb8, 0x9c, 0x34, 0xad, 0x1e, 0x5f, 0xeb, 0xa7, 0x0e, 0x7a, 0xae, 0x43, 0x28, 0x28, 0x9c});
	IV.assign({0xcf, 0xc0, 0x6e, 0x72, 0x2b, 0xe9, 0x87, 0xb3, 0x76, 0x7f, 0x70, 0xa7, 0xb8, 0x56, 0xb7, 0x74});
	AD.clear();
	pattern_plain.assign({0xce, 0x20, 0x27, 0xb4, 0x7a, 0x84, 0x32, 0x52, 0x01, 0x34, 0x65, 0x83, 0x4d, 0x75, 0xfd, 0x0f});
	pattern_cipher.assign({0xdc, 0x03, 0xe5, 0x24, 0x83, 0x0d, 0x30, 0xf8, 0x8e, 0x19, 0x7f, 0x3a, 0xca, 0xce, 0x66, 0xef});
	pattern_tag.assign({0x99, 0x84, 0xef, 0xf6, 0x90, 0x57, 0x55, 0xd1, 0x83, 0x6f, 0x2d, 0xb0, 0x40, 0x89, 0x63, 0x4c});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.11 */
	key.assign({0x0e, 0x05, 0x93, 0x5d, 0xf0, 0xc6, 0x93, 0x74, 0x18, 0x92, 0xb7, 0x6f, 0xaf, 0x67, 0x13, 0x3a, 0xbd, 0x2c, 0xf2, 0x03, 0x11, 0x21, 0xbd, 0x8b, 0xb3, 0x81, 0x27, 0xa4, 0xd2, 0xee, 0xde, 0xea});
	IV.assign({0x74, 0xb1, 0xba, 0x26, 0x85, 0xb3, 0x68, 0x09, 0x14, 0x29, 0xfc, 0xcb, 0x8d, 0xcd, 0xde, 0x09, 0xe4});
	AD.assign({0x7b, 0xd8, 0x59, 0xa2, 0x47, 0x96, 0x1a, 0x21, 0x82, 0x3b, 0x38, 0x0e, 0x9f, 0xe8, 0xb6, 0x50, 0x82, 0xba, 0x61, 0xd3});
	pattern_plain.assign({0x90, 0xae, 0x61, 0xcf, 0x7b, 0xae, 0xbd, 0x4c, 0xad, 0xe4, 0x94, 0xc5, 0x4a, 0x29, 0xae, 0x70, 0x26, 0x9a, 0xec, 0x71});
	pattern_cipher.assign({0x6b, 0xe6, 0x5e, 0x56, 0x06, 0x6c, 0x40, 0x56, 0x73, 0x8c, 0x03, 0xfe, 0x23, 0x20, 0x97, 0x4b, 0xa3, 0xf6, 0x5e, 0x09});
	pattern_tag.assign({0x61, 0x08, 0xdc, 0x41, 0x7b, 0xf3, 0x2f, 0x7f, 0xb7, 0x55, 0x4a, 0xe5, 0x2f, 0x08, 0x8f, 0x87});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);

	/* Test D3.12 */
	key.assign({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	IV.assign({0x02, 0xcb, 0xbc, 0x7a, 0x03, 0xeb, 0x4d, 0xe3, 0x9d, 0x80, 0xd1, 0xeb, 0xc9, 0x88, 0xbf, 0xdf});
	AD.assign({0x68, 0x8e, 0x1a, 0xa9, 0x84, 0xde, 0x92, 0x6d, 0xc7, 0xb4, 0xc4, 0x7f, 0x44});
	pattern_plain.assign({0xa2, 0xaa, 0xb3, 0xad, 0x8b, 0x17, 0xac, 0xdd, 0xa2, 0x88, 0x42, 0x6c, 0xd7, 0xc4, 0x29, 0xb7, 0xca, 0x86, 0xb7, 0xac, 0xa0, 0x58, 0x09, 0xc7, 0x0c, 0xe8, 0x2d, 0xb2, 0x57, 0x11, 0xcb, 0x53, 0x02, 0xeb, 0x27, 0x43, 0xb0, 0x36, 0xf3, 0xd7, 0x50, 0xd6, 0xcf, 0x0d, 0xc0, 0xac, 0xb9, 0x29, 0x50, 0xd5, 0x46, 0xdb, 0x30, 0x8f, 0x93, 0xb4, 0xff, 0x24, 0x4a, 0xfa, 0x9d, 0xc7, 0x2b, 0xcd, 0x75, 0x8d, 0x2c});
	pattern_cipher.assign({0xee, 0x62, 0x55, 0x2a, 0xeb, 0xc0, 0xc3, 0xc7, 0xda, 0xae, 0x12, 0xbb, 0x6c, 0x32, 0xca, 0x5a, 0x00, 0x5f, 0x4a, 0x1a, 0xaa, 0xb0, 0x04, 0xed, 0x0f, 0x0b, 0x30, 0xab, 0xbf, 0x15, 0xac, 0xf4, 0xc5, 0x0c, 0x59, 0x66, 0x2d, 0x4b, 0x44, 0x68, 0x41, 0x95, 0x44, 0xe7, 0xf9, 0x81, 0x97, 0x35, 0x63, 0xce, 0x55, 0x6a, 0xe5, 0x08, 0x59, 0xee, 0x09, 0xb1, 0x4d, 0x31, 0xa0, 0x53, 0x98, 0x6f, 0x9a, 0xc8, 0x9b});
	pattern_tag.assign({0x9c, 0xd0, 0xdb, 0x93, 0x6e, 0x26, 0xd4, 0x4b, 0xe9, 0x74, 0xba, 0x86, 0x82, 0x85, 0xa2, 0xe1});
	cipher.resize(pattern_plain.size());
	plain.resize(pattern_plain.size());

	AEAD_encrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_plain.data(), pattern_plain.size(), AD.data(), AD.size(), tag.data(), tag.size(), cipher.data());
	BC_ASSERT_TRUE(cipher==pattern_cipher);
	BC_ASSERT_TRUE(tag==pattern_tag);
	BC_ASSERT_TRUE(AEAD_decrypt<AES256GCM>(key.data(), key.size(), IV.data(), IV.size(), pattern_cipher.data(), pattern_cipher.size(), AD.data(), AD.size(), pattern_tag.data(), pattern_tag.size(), plain.data()));
	BC_ASSERT_TRUE(plain==pattern_plain);
}



static test_t tests[] = {
	TEST_NO_TAG("Key Exchange", exchange),
	TEST_NO_TAG("Signature", signAndVerify),
	TEST_NO_TAG("HKDF", hashMac_KDF),
	TEST_NO_TAG("AEAD", AEAD),
};

test_suite_t lime_crypto_test_suite = {
	"Crypto",
	NULL,
	NULL,
	NULL,
	NULL,
	sizeof(tests) / sizeof(tests[0]),
	tests
};
