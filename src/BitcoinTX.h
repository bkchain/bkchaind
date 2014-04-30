#ifndef BITCOIN_TX_H
#define BITCOIN_TX_H

#include <stdint.h>
#include <unordered_map>
#include "Database.h"

typedef uint64_t TxId;

TxId processTransaction(leveldb::WriteBatch& batch, DbTransaction& dbTx, const BlockChain::BlockTransaction& transaction, uint32_t receivedTime, bool txCheck,
	std::unordered_map<Hash256, uint32_t>* pendingTransactionIndices = NULL, std::vector<std::pair<Hash256, DbTransaction>>* pendingTransactions = NULL);

#endif
