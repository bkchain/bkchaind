#ifndef LIVE_TRANSACTIONS_H
#define LIVE_TRANSACTIONS_H

#include <vector>
#include <unordered_map>
#include <deque>
#include <boost/thread/mutex.hpp>
#include "Hash256.h"
#include "Database.h"

const int lastPendingTransactionsSize = 10;
extern std::unordered_map<Hash256, uint32_t> pendingTransactionIndices;
extern std::unordered_multimap<Address, uint32_t> pendingTransactionsByAddress;
extern std::vector<std::pair<Hash256, DbTransaction>> pendingTransactions;
extern std::deque<std::pair<Hash256, DbTransaction>> lastPendingTransactions;
extern boost::mutex pendingTransactionsMutex;

void websocketThreadFunc(int port);
void daemonThreadFunc(BlockChain* blockChain);

#endif
