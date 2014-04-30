#ifndef ADDRESS_OPERATIONS_H
#define ADDRESS_OPERATIONS_H

#include <stdint.h>
#include <deque>
#include "Address.h"
#include "Hash256.h"

typedef uint64_t TxId;

struct AddressOperation
{
	uint64_t total_input;
	uint64_t total_output;

	uint64_t input;
	uint64_t output;

	uint32_t txCount;
	uint32_t transactionTime;

	uint64_t blockHeight;
	uint32_t blockTxIndex;
	uint32_t blockHash;
	TxId txHashIndex;
};

/// Get multiple address operations for the specified address, starting from given block.
int getAddressOperations(const Address& address, int offset, int maxCount, std::deque<AddressOperation>& addressOperations, const Hash256& chainStart);

/// Get a single address operation for the specified address, starting from given block.
/// AddressOperation::input and output won't be filled.
bool getAddressOperation(const Address& address, AddressOperation& addressOperation, const Hash256& chainStart, int confirmations);

#endif
