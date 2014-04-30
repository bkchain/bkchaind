#include <stdint.h>
#include <thread>
#include <boost/thread/mutex.hpp>
#include <boost/thread/locks.hpp>
#include <leveldb/write_batch.h>
#include <unordered_map>
#include "Database.h"
#include "Address.h"
#include "Hash256.h"
#include "BroadcastWebsocketServer.hpp"
#include "BitcoinRPC.h"
#include "Blocks.h"
#include "BitcoinLiveTX.h"
#include "CacherServer.h"
#include "BitcoinTX.h"

Hash256 pendingPreviousBlock;
BroadcastWebsocketServer websocket_server;

std::unordered_map<Hash256, uint32_t> pendingTransactionIndices;
std::unordered_multimap<Address, uint32_t> pendingTransactionsByAddress;
std::vector<std::pair<Hash256, DbTransaction>> pendingTransactions;
std::deque<std::pair<Hash256, DbTransaction>> lastPendingTransactions;
boost::mutex pendingTransactionsMutex;

extern bool requestedQuit;
extern bool txCheck;

/// Run websocket server
void websocketThreadFunc(int port)
{
	websocket_server.run(port);
}

bool convertTime(uint32_t time, char* buffer, size_t bufferSize);

/// Process data from bitcoind daemon (esp. RPC getrawmempool for live transactions)
void daemonThreadFunc(BlockChain* blockChain)
{
	leveldb::WriteBatch batch;

	while (true)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));

		if (requestedQuit)
			break;

		if (!txCheck)
			continue;

		try
		{
			auto transactions = rpcClient->CallMethod("getrawmempool", Json::Value());

			BlockInfo blockInfo;
			Hash256 latestBlock;
			{
				boost::lock_guard<boost::mutex> guard(chainMutex);
				latestBlock = currentChain.back();

				auto blockInfoIt = blocks.find(latestBlock);
				if (blockInfoIt == blocks.end())
				{
					continue;
				}
				blockInfo = blockInfoIt->second;
			}

			if (pendingPreviousBlock != latestBlock)
			{
				pendingPreviousBlock = latestBlock;

				// Broadcast new block for live update
				Json::Value blockResult;
				blockResult["item"] = "block";
				convertBlock(blockResult, pendingPreviousBlock, blockInfo);
				websocket_server.broadcast_livetx(blockResult.toStyledString().c_str());

				{
					boost::lock_guard<boost::mutex> guard(pendingTransactionsMutex);
					pendingTransactionIndices.clear();
					pendingTransactions.clear();
					pendingTransactionsByAddress.clear();
				}
			}

			for (auto tx = transactions.begin(); tx != transactions.end(); ++tx)
			{
				Hash256 txHash;
				encodeHexString((uint8_t*) &txHash, 32, (*tx).asString(), true);

				batch.Clear();

				{
					// Already exists?
					boost::unique_lock<boost::mutex> guard(pendingTransactionsMutex);
					if (pendingTransactionIndices.find(txHash) != pendingTransactionIndices.end())
						continue;
				}

				{
					DbTransaction dbTx;

					Json::Value parameters(Json::arrayValue);
					parameters.append(*tx);
					auto rawTx = rpcClient->CallMethod("getrawtransaction", parameters);

					auto data = rawTx.asCString();
					auto bufferLength = strlen(data) / 2;

					llvm::SmallVector<uint8_t, 65536> buffer;
					buffer.resize(bufferLength);

					encodeHexString(buffer.begin(), bufferLength, data);

					BlockChain::BlockTransaction tx2;
					if (!blockChain->processSingleTransaction(buffer.begin(), bufferLength, tx2))
						assert("could not read tx" && false);

					assert(memcmp(tx2.transactionHash, &txHash, 32) == 0);
					uint64_t totalOutput = 0;
					bool needBroadcast = false;

					auto txIndex = dbHelper.txLoad(txHash, dbTx, NULL, NULL);
					if (txIndex == 0)
					{
						{
							boost::unique_lock<boost::mutex> guard(pendingTransactionsMutex);
							try
							{
								txIndex = processTransaction(batch, dbTx, tx2, time(NULL), false, &pendingTransactionIndices, &pendingTransactions);
							}
							catch (std::exception e)
							{
								batch.Clear();
								continue;
							}
						}

						needBroadcast = true;

						dbHelper.txSave(batch, txHash, dbTx);
						db->Write(leveldb::WriteOptions(), &batch);
					}

					{
						boost::unique_lock<boost::mutex> guard(pendingTransactionsMutex);
						pendingTransactionIndices[txHash] = pendingTransactions.size();
						pendingTransactions.emplace_back(txHash, dbTx);

						// lastPendingTransactions only holds last N items 
						if (lastPendingTransactions.size() >= lastPendingTransactionsSize)
							lastPendingTransactions.pop_front();

						lastPendingTransactions.emplace_back(txHash, dbTx);

						for (auto output = dbTx.outputs.begin(); output != dbTx.outputs.end(); ++output)
						{
							if (output->address.IsNull())
								continue;
							totalOutput += output->value;

							pendingTransactionsByAddress.emplace(output->address, pendingTransactions.size() - 1);
						}

						for (auto input = dbTx.inputs.begin(); input != dbTx.inputs.end(); ++input)
						{
							if (input->address.IsNull())
								continue;

							pendingTransactionsByAddress.emplace(input->address, pendingTransactions.size() - 1);
						}
					}

					if (needBroadcast)
					{
						Json::Value txResult;

						// Broadcast tx for live update
						txResult["item"] = "tx";
						txResult["output"] = (double)totalOutput;

						char timeBuffer[65];
						convertTime(dbTx.getBestTimeStamp(), timeBuffer, sizeof(timeBuffer));
						txResult["time"] = timeBuffer;

						txResult["coinage_destroyed"] = dbTx.coinAgeDestroyed / 86400.0;
						txResult["hash"] = *tx;

						websocket_server.broadcast_livetx(txResult.toStyledString().c_str());
					}
				}
			}
		}
		catch (std::exception e)
		{
			boost::lock_guard<boost::mutex> guard(pendingTransactionsMutex);
			pendingTransactionIndices.clear();
			pendingTransactions.clear();
			pendingTransactionsByAddress.clear();

			printf("Error processing live transactions: %s!\n", e.what());
		}
	}
}