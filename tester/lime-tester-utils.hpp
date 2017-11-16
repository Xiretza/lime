/*
	lime-tester-utils.hpp
	Copyright (C) 2017  Belledonne Communications SARL

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

#ifndef lime_tester_utils_hpp
#define lime_tester_utils_hpp

#include "bctoolbox/crypto.h"
#include "lime_double_ratchet.hpp"
#include "lime_localStorage.hpp"

#include "soci/sqlite3/soci-sqlite3.h"

namespace lime {

extern std::vector<std::string> lime_messages_pattern;

/**
 * @brief Create and initialise the two sessions given in parameter. Alice as sender session and Bob as receiver one
 *	Alice must then send the first message, once bob got it, sessions are fully initialised
 *	if fileName doesn't exists as a DB, it will be created, caller shall then delete it if needed
 */
template <typename Curve>
void dr_sessionsInit(std::shared_ptr<DR<Curve>> &alice, std::shared_ptr<DR<Curve>> &bob, std::shared_ptr<lime::Db> &localStorageAlice, std::shared_ptr<lime::Db> &localStorageBob, std::string dbFilenameAlice, std::string dbFilenameBob, bool initStorage=true);


/* non efficient but used friendly structure to store all details about a session */
/* the self_xx are redundants but it's for testing purpose */
template <typename Curve>
struct sessionDetails {
	std::string self_userId;
	std::size_t self_userIndex;
	std::size_t self_deviceIndex;
	std::string peer_userId;
	std::size_t peer_userIndex;
	std::size_t peer_deviceIndex;
	std::shared_ptr<DR<Curve>> DRSession; // Session to reach recipient
	std::shared_ptr<lime::Db> localStorage; // db linked to device
	sessionDetails() : self_userId{}, self_userIndex{0}, self_deviceIndex{0}, peer_userId{}, peer_userIndex{0}, peer_deviceIndex{0}, DRSession{}, localStorage{} {};
	sessionDetails(std::string &s_userId, size_t s_userIndex, size_t s_deviceIndex, std::string &p_userId, size_t p_userIndex, size_t p_deviceIndex)
		: self_userId{s_userId}, self_userIndex{s_userIndex}, self_deviceIndex{s_deviceIndex}, peer_userId{p_userId}, peer_userIndex{p_userIndex}, peer_deviceIndex{p_deviceIndex}, DRSession{}, localStorage{} {};
};

/**
 * @brief Create and initialise all requested DR sessions for specified number of devices between two or more users
 * users is a vector of users(already sized to correct size, matching usernames size), each one holds a vector of devices(already sized for each device)
 * This function will create and instanciate in each device a vector of vector of DR sessions towards all other devices: each device vector holds a bidimentionnal array indexed by userId and deviceId.
 * Session init is done considering as initial sender the lowest id user and in it the lowest id device
 * createdDBfiles is filled with all filenames of DB created to allow easy deletion
 */
template <typename Curve>
void dr_devicesInit(std::string dbBaseFilename, std::vector<std::vector<std::vector<std::vector<sessionDetails<Curve>>>>> &users, std::vector<std::string> &usernames, std::vector<std::string> &createdDBfiles);

/* return true if the message buffer is a valid DR message holding a X3DH init one in its header */
bool DR_message_holdsX3DHInit(std::vector<uint8_t> &message);

/* return true if the message buffer is a valid DR message holding a X3DH init one in its header and copy the X3DH init message in the provided buffer */
bool DR_message_extractX3DHInit(std::vector<uint8_t> &message, std::vector<uint8_t> &X3DH_initMessage);

/* Open provided DB and look for DRSessions established between selfDevice and peerDevice
 * Populate the sessionsId vector with the Ids of sessions found
 * return the id of the active session if one is found, 0 otherwise */
long int get_DRsessionsId(const std::string &dbFilename, const std::string &selfDeviceId, const std::string &peerDeviceId, std::vector<long int> &sessionsId);

/**
 * @brief append a random suffix to user name to avoid collision if test server is user by several tests runs
 *
 * @param[in] basename
 *
 * @return a shared ptr towards a string holding name+ 6 chars random suffix
 */
std::shared_ptr<std::string> makeRandomDeviceName(const char *basename);


// wait for a counter to reach a value or timeout to occur, gives ticks to the belle-sip stack every SLEEP_TIME
// structure used by callbacks to register events
struct events_counters_t {
	int operation_success;
	int operation_failed;
	events_counters_t() : operation_success{0}, operation_failed{0} {};
	bool operator==(const events_counters_t &b) const {return this->operation_success==b.operation_success && this->operation_failed==b.operation_failed;}
};

// wait for a counter to reach a value or timeout to occur, gives ticks to the belle-sip stack every SLEEP_TIME
int wait_for(belle_sip_stack_t*s1,int* counter,int value,int timeout);

// template instanciation are done in lime-tester-utils.cpp
#ifdef EC25519_ENABLED
	extern template void dr_sessionsInit<C255>(std::shared_ptr<DR<C255>> &alice, std::shared_ptr<DR<C255>> &bob, std::shared_ptr<lime::Db> &localStorageAlice, std::shared_ptr<lime::Db> &localStorageBob, std::string dbFilenameAlice, std::string dbFilenameBob, bool initStorage); 
	extern template void dr_devicesInit<C255>(std::string dbBaseFilename, std::vector<std::vector<std::vector<std::vector<sessionDetails<C255>>>>> &users, std::vector<std::string> &usernames, std::vector<std::string> &createdDBfiles);
#endif
#ifdef EC448_ENABLED
	extern template void dr_sessionsInit<C448>(std::shared_ptr<DR<C448>> &alice, std::shared_ptr<DR<C448>> &bob, std::shared_ptr<lime::Db> &localStorageAlice, std::shared_ptr<lime::Db> &localStorageBob, std::string dbFilenameAlice, std::string dbFilenameBob, bool initStorage); 
	extern template void dr_devicesInit<C448>(std::string dbBaseFilename, std::vector<std::vector<std::vector<std::vector<sessionDetails<C448>>>>> &users, std::vector<std::string> &usernames, std::vector<std::string> &createdDBfiles);
#endif
} // namespace lime

#endif
