#include <vector>
#include <map>
#include <unordered_map>
#include <regex>
#include <boost/program_options.hpp>
#include <boost/program_options/detail/config_file.hpp>
#include <boost/thread/tss.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include "BroadcastWebsocketServer.hpp"
#undef SendMessage

#include <chrono>
#include <thread>
#include <signal.h>
#include "leveldb/db.h"
#include "leveldb/cache.h"
#include "leveldb/write_batch.h"
#include "../blockchain/BitcoinAddress.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm_serialization.h"
#include "ScriptDecoder.h"
#include "Address.h"
#include "Hash256.h"
#include "Database.h"
#include "Serialization.h"
#include "Blocks.h"
#include "AddressOperations.h"
#include "BitcoinLiveTX.h"

#include "lrucache.hpp"
#include "httpclientasio.h"

#include "CacherServer.h"
#include "BitcoinRPC.h"
#include "BitcoinTX.h"

leveldb::Cache* unspentCache;
boost::recursive_mutex cacheMutex;

lru_cache<Address, DbAddressOperationChain> addressCache(4 * 1024 * 1024);
lru_cache<Hash256, TxId> txCache(16 * 1024 * 1024);
lru_cache<TxId, Hash256> txCache2(16 * 1024 * 1024);

bool requestedQuit = false;
bool txCheck = false;

void signalhandler(int)
{
	requestedQuit = true;
}

const uint8_t BackupStateKey = 0x00;

// Store position in HDD blockchain, so that we can resume where we were at.
struct BackupState
{
	uint32_t bcBlockIndex;
	uint64_t bcBlockPosition;
	uint32_t bcBlockNumber;

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & bcBlockIndex;
		ar & bcBlockPosition;
		ar & bcBlockNumber;
	}
};

// Update UTXO DB with this TX
void ProcessUTXO(const DbTransaction& dbTx, TxId txIndex, uint32_t blockHeight, leveldb::WriteBatch& batch)
{
	int outputIndex = 0;
	for (auto output = dbTx.outputs.begin(); output != dbTx.outputs.end(); ++output, ++outputIndex)
	{
		if (output->address.IsNull())
			continue;

		// Add new UTXO
		DbAddressUTXOKey searchKey(output->address, DbTxRef(txIndex, outputIndex));
		DbAddressUTXO data(output->value, blockHeight);
		batch.Put(leveldb::Slice((const char*)&searchKey, sizeof(searchKey)), leveldb::Slice((const char*)&data, sizeof(data)));
	}
	for (auto input = dbTx.inputs.begin(); input != dbTx.inputs.end(); ++input)
	{
		if (input->address.IsNull())
			continue;

		// Delete spent UTXO
		DbAddressUTXOKey searchKey(input->address, input->txRef);
		batch.Delete(leveldb::Slice((const char*)&searchKey, sizeof(searchKey)));
	}
}

// Undo UTXO changes added previously by this TX
void UndoUTXO(const DbTransaction& dbTx, TxId txIndex, leveldb::WriteBatch& batch)
{
	int outputIndex = 0;
	for (auto output = dbTx.outputs.begin(); output != dbTx.outputs.end(); ++output, ++outputIndex)
	{
		if (output->address.IsNull())
			continue;

		// Undo add new UTXO
		DbAddressUTXOKey searchKey(output->address, DbTxRef(txIndex, outputIndex));
		batch.Delete(leveldb::Slice((const char*)&searchKey, sizeof(searchKey)));
	}

	DbTransaction dbTx2;
	std::vector<DbTransactionBlock> txBlocks;
	for (auto input = dbTx.inputs.begin(); input != dbTx.inputs.end(); ++input)
	{
		if (input->address.IsNull())
			continue;

		// Find block height of tx
		dbTx2.clear();
		txBlocks.clear();
		if (!dbHelper.txLoad(input->txRef.tx, dbTx2, NULL, &txBlocks))
			throw std::runtime_error("Could not load dependent transaction while undoing UTXO");

		// One of the block should be in current chain. Find it.
		auto blockHeight = -1;
		for (auto blockIt = txBlocks.begin(); blockIt != txBlocks.end(); ++blockIt)
		{
			auto blockHash = blockIds[blockIt->blockHash];
			auto block = blocks[blockHash];
			if (currentChain[block.height] == blockHash)
			{
				// Found a match, early exit
				blockHeight = block.height;
				break;
			}
		}

		if (blockHeight == -1)
			throw std::runtime_error("Could not find block height of dependent transaction while undoing UTXO");

		// Undo delete spent UTXO
		DbAddressUTXOKey searchKey(input->address, input->txRef);
		DbAddressUTXO data(input->value, blockHeight);
		batch.Put(leveldb::Slice((const char*)&searchKey, sizeof(searchKey)), leveldb::Slice((const char*)&data, sizeof(data)));
	}
}

// Various metrics related to Proof of stake.
void UpdateProofOfStake(BlockInfo& blockInfo, DbTransaction& dbTx)
{
	if (dbTx.inputs.size() >= 1 && dbTx.inputs[0].txRef.index != -1
		&& dbTx.outputs.size() >= 2 && dbTx.outputs[0].value == 0 && dbTx.outputs[0].challengeScript.empty())
	{
		blockInfo.type = BlockType::ProofOfStake;
		blockInfo.stakeCoinAgeDestroyed = dbTx.coinAgeDestroyed;
		for (int i = 0; i < dbTx.inputs.size(); ++i)
			blockInfo.staked += dbTx.inputs[i].value;
	}
}

// Access to websocket server
extern BroadcastWebsocketServer websocket_server;

int main(int argc, char* argv [])
{
	boost::program_options::options_description desc("Allowed options");

	// Parameters (with default values)
	std::string blockchainPath;
	std::string databasePath;
	std::string rpcUser;
	std::string rpcPassword;
	int rescanIndex = -1;
	int port = 0;
	int websocketPort = 0;
	int rpcPort = 0;
	int unspentCacheSize = 128;
	int blockCacheSize = 128;
	int writeBufferSize = 16;

	// Describe command-line options
	desc.add_options()
		("help", "produce help message")
		("blockchain", boost::program_options::value(&blockchainPath), "blockchain path")
		("database", boost::program_options::value(&databasePath), "database path")
		("port", boost::program_options::value(&port), "port (default: rpcport + 10)")
		("websocketport", boost::program_options::value(&websocketPort), "websocket port (default: rpcport + 20)")
		("unspent-cache", boost::program_options::value(&unspentCacheSize), "unspent cache size (in MB); default: 128")
		("block-cache", boost::program_options::value(&blockCacheSize), "block cache size (in MB); default: 128")
		("write-buffer", boost::program_options::value(&writeBufferSize), "write buffer size (in MB); default: 16")
		("rpcport", boost::program_options::value(&rpcPort), "rpcport")
		("rpcuser", boost::program_options::value(&rpcUser), "rpcuser")
		("rpcpassword", boost::program_options::value(&rpcPassword), "rpcpassword")
		("rescan-index", boost::program_options::value(&rescanIndex), "rescan block from given index")
		;

	// Parse command line
	boost::program_options::variables_map vm;
	boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
	boost::program_options::notify(vm);

	if (vm.count("help"))
	{
		std::cout << desc << "\n";
		exit(0);
	}

	// Need at least --blockchain and --database
	if (vm.count("blockchain") == 0 || vm.count("database") == 0)
	{
		std::cout << "\"blockchain\" and \"database\" are both required!" << "\n";
		exit(0);
	}

	// Open blockchain
	auto blockChain = createBlockChain(blockchainPath.c_str());
	if (blockChain == NULL)
	{
		printf("Could not open blockchain.\n");
		return -1;
	}

	// If no bitcoind RPC port specified, try to guess it from blockchain currency
	if (rpcPort == 0)
	{
		rpcPort = blockChain->getRpcDefaultPort();
	}

	// Try to find conf file with RPC username/password automatically
	boost::filesystem::directory_iterator end_it;
	for (boost::filesystem::directory_iterator it(blockchainPath); it != end_it; ++it)
	{
		if (boost::filesystem::is_regular_file(it->path()) && it->path().extension() == ".conf")
		{
			boost::program_options::store(boost::program_options::parse_config_file<char>(it->path().string().c_str(), desc, true), vm);
		}
	}

	// Complete options from config file
	boost::program_options::notify(vm);

	// Generate a JSON RPC port (if not specified)
	if (port == 0)
	{
		port = rpcPort + 10;
	}

	// Generate a websocket port (if not specified)
	if (websocketPort == 0)
	{
		websocketPort = rpcPort + 20;
	}

	rpcClient = new RpcClient(rpcPort, rpcUser, rpcPassword);

	// Start websocket thread
	std::thread websocketThread(websocketThreadFunc, websocketPort);

	// Start bitcoind RPC thread (live TX)
	std::thread daemonThread(daemonThreadFunc, blockChain);

	DbTransaction::transactionContainsTimestamp = blockChain->getTransactionContainsTimestamp();

	// Open or create leveldb database
	leveldb::Options options;
	options.create_if_missing = true;
	//options.max_open_files = 64;
	options.write_buffer_size = 1024 * 1024 * writeBufferSize;
	options.block_cache = leveldb::NewLRUCache(1024 * 1024 * blockCacheSize);
	leveldb::Status status = leveldb::DB::Open(options, databasePath, &db);
	if (!status.ok())
	{
		printf("Could not open database.\n");
		return -1;
	}

	dbHelper.db = db;

	int index = 0;

	Hash256 longestBlock;
	memset(&longestBlock, 0, sizeof(Hash256));
	uint64_t totalBits = 0;

	auto start_time = std::chrono::high_resolution_clock::now();

	unspentCache = leveldb::NewLRUCache(1024 * 1024 * unspentCacheSize);

	leveldb::WriteBatch batch;
	std::string serialText;
	std::unordered_map<Hash256, uint32_t> pendingTransactionIndices;
	std::vector<std::pair<Hash256, DbTransaction>> pendingTransactions;
	std::map<Address, DbAddressOperationChain> addressOperationsBlock;
	std::map<Address, DbAddressOperationChain> addressOperations;

	// Start JSON RPC server
	CacherServer serv(port, blockChain->getPubkeyAddress());
	serv.StartListening();

	std::string existingBlockText;

	// Setup signal handlers (to exit properly, even on Ctrl+C)
	signal(SIGINT, signalhandler);
	signal(SIGTERM, signalhandler);

	auto lastBackup = std::chrono::high_resolution_clock::now();

	// Restore in-memory blockchain data from database
	{
		boost::lock_guard<boost::mutex> guard(chainMutex);
		auto it = std::unique_ptr<leveldb::Iterator>(db->NewIterator(leveldb::ReadOptions()));
		int index = 0;
		uint8_t searchKey = (uint8_t)DatabaseKeyHeader::Block;
		std::string valueData;

		Hash256 longestBlock;
		memset(&longestBlock, 0, sizeof(Hash256));
		uint64_t totalBits = 0;

		for (it->Seek(leveldb::Slice((const char*)&searchKey, sizeof(searchKey))); it->Valid(); it->Next())
		{
			auto key = it->key();
			auto keyData = key.data();
			if ((key.size() != 1 + sizeof(uint32_t)) || keyData[0] != searchKey)
				break;

			auto index = swapByteOrder(*((uint32_t*)&keyData[1]));
			if (rescanIndex != -1 && index >= rescanIndex)
				break;

			auto value = it->value();
			valueData.assign(value.data(), value.size());

			DbBlock dbBlock;
			Deserialize(valueData, dbBlock);

			uint64_t previousTotalBits;
			Hash256 previousHash;

			if (dbBlock.previous != -1)
			{
				previousHash = blockIds[dbBlock.previous];
				auto& previous = blocks[previousHash];
				previous.nexts.push_back(dbBlock.hash);
				previousTotalBits = previous.totalBits;
			}
			else
			{
				previousTotalBits = 0;
				memset(&previousHash, 0, sizeof(previousHash));
			}
		
			BlockInfo blockInfo;
			blockInfo.index = index;
			blockInfo.height = dbBlock.height;
			blockInfo.bits = dbBlock.bits;
			blockInfo.totalBits = previousTotalBits + dbBlock.bits;
			blockInfo.previous = previousHash;
			blockInfo.longestChain = false;
			blockInfo.timeStamp = dbBlock.timeStamp;
			blockInfo.blockLength = dbBlock.blockLength;
			blockInfo.output = dbBlock.output;
			blockInfo.coinAgeDestroyed = dbBlock.coinAgeDestroyed;
			blockInfo.totalCoins = dbBlock.totalCoins;
			blockInfo.totalCoinAge = dbBlock.totalCoinAge;
			blockInfo.transactionCount = dbBlock.transactions.size();
			blockInfo.reward = dbBlock.reward;

			if (blockInfo.totalBits > totalBits)
			{
				totalBits = blockInfo.totalBits;
				longestBlock = dbBlock.hash;
			}

			blockInfo.type = BlockType::ProofOfWork;
			blockInfo.stakeCoinAgeDestroyed = 0;
			blockInfo.staked = 0;

			// PoS info
			if (dbBlock.transactions.size() >= 2)
			{
				DbTransaction dbTx;
				if (dbHelper.txLoad(dbBlock.transactions[1], dbTx, NULL, NULL))
				{
					UpdateProofOfStake(blockInfo, dbTx);
				}
			}

			// Update
			blockIds.push_back(dbBlock.hash);
			blocks.insert(std::make_pair(dbBlock.hash, blockInfo));
			blockHeights.insert(std::make_pair(blockInfo.height, dbBlock.hash));
		}

		// Build longest chain
		auto block = blocks.find(longestBlock);
		if (block != blocks.end())
			currentChain.resize(block->second.height + 1);
		while (block != blocks.end())
		{
			block->second.longestChain = true;
			currentChain[block->second.height] = block->first;
			block = blocks.find(block->second.previous);
		}

		// Restore BackupState
		BackupState backupState;
		std::string backupStateData;
		if (rescanIndex == -1 && db->Get(leveldb::ReadOptions(), leveldb::Slice((const char*)&BackupStateKey, sizeof(BackupStateKey)), &backupStateData).ok())
		{
			Deserialize(backupStateData, backupState);
			blockChain->setPosition(backupState.bcBlockIndex, backupState.bcBlockPosition, backupState.bcBlockNumber);
		}

		// Restore dbHelper latest index for TX
		{
			// If error, use 1
			dbHelper.txResetLastId(1);

			auto it = std::unique_ptr<leveldb::Iterator>(db->NewIterator(leveldb::ReadOptions()));

			uint8_t searchKey;
			searchKey = 0x09;

			// We want to go to last key starting with 08, so go to key starting with 09 and go backward
			it->Seek(leveldb::Slice((const char*)&searchKey, sizeof(searchKey)));
			if (it->Valid())
				it->Prev();
			else
				it->SeekToLast();

			if (it->Valid()
				&& it->key().data()[0] == (uint8_t)DatabaseKeyHeader::Tx)
			{
				auto key = it->key();
				assert(key.size() == 1 + sizeof(TxId) || key.size() == 1 + sizeof(TxId) + 1 + sizeof(DbTransactionOutput));

				// Get latest index attributed and set its incremented value as next "last index" for future TX.
				auto latestIndex = swapByteOrder(*((TxId*) &key.data()[1])) + 1;
				dbHelper.txResetLastId(latestIndex);
			}
		}
	}

	auto blockLastHeight = 0;

	// Main loop
	while (true)
	{
		auto current_time = std::chrono::high_resolution_clock::now();

		index++;

		if (index % 1000 == 0)
		{
			// Display some message every 1000 blocks
			// TODO: Every x seconds instead?
			printf("[%.3llu:%.3llu] Height %i\n", std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count(), std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count() % 1000, blockLastHeight);
			fflush(stdout);
			//uint64_t sizes[3];
			//leveldb::Range ranges[3];
			//ranges[0] = leveldb::Range(leveldb::Slice("\01"), leveldb::Slice("\02"));
			//ranges[1] = leveldb::Range(leveldb::Slice("\02"), leveldb::Slice("\03"));
			//ranges[2] = leveldb::Range(leveldb::Slice("\03"), leveldb::Slice("\04"));
			//db->GetApproximateSizes(ranges, sizeof(ranges) / sizeof(ranges[0]), sizes);
			//std::string stats;
			//db->GetProperty("leveldb.stats", &stats);
			//printf("%s\n", stats.c_str());
		}

		// Ctrl+C requested?
		if (requestedQuit)
			break;

		uint8_t *blockData;
		uint32_t blockLength;

		const BlockChain::Block* block;

		// Get current file position before reading block (so that we can go back to it in case it failed)
		uint32_t bcBlockIndex;
		uint64_t bcBlockPosition;
		uint32_t bcBlockNumber;
		blockChain->getPosition(bcBlockIndex, bcBlockPosition, bcBlockNumber);

		if (!blockChain->readBlock(blockData, blockLength) || (block = blockChain->processSingleBlock(blockData, blockLength)) == NULL)
		{
			// Get back to previous position
			blockChain->setPosition(bcBlockIndex, bcBlockPosition, bcBlockNumber);

			// Wait one second before trying again
			std::this_thread::sleep_for(std::chrono::seconds(1));

			// If we were trying to catch up with rescan index & tx check, it's over, we reached end of current blockchain on HDD
			txCheck = true;
			rescanIndex = -1;
			continue;
		}

		// Skipped (rescanning ignore first N blocks)
		if (rescanIndex > 0)
		{
			--rescanIndex;
			continue;
		}

		// Create block info
		BlockInfo blockInfo;
		blockInfo.index = blockIds.size();
		blockInfo.blockLength = block->blockLength;
		blockInfo.transactionCount = block->transactionCount;
		blockInfo.timeStamp = block->timeStamp;
		blockInfo.output = 0;
		blockInfo.height = -1;
		blockInfo.previous = block->previousBlockHash;
		blockInfo.bits = block->bits;
		blockInfo.totalBits = block->bits;
		blockInfo.longestChain = false;
		blockInfo.coinAgeDestroyed = 0.0f;
		blockInfo.totalCoins = 0.0;
		blockInfo.totalCoinAge = 0.0;
		blockInfo.reward = 0;

		blockInfo.type = BlockType::ProofOfWork;
		blockInfo.stakeCoinAgeDestroyed = 0;
		blockInfo.staked = 0;

		std::map<Hash256, BlockInfo>::iterator previousBlockInfoIt;
		uint32_t previousBlock = -1;
		bool processUTXO = false;

		// Fill some blockInfo cumulative data from previous block
		{
			boost::lock_guard<boost::mutex> guard(chainMutex);

			if (!(*(Hash256*)block->previousBlockHash).IsNull())
				previousBlock = blocks[block->previousBlockHash].index;

			{
				previousBlockInfoIt = blocks.find(blockInfo.previous);
				if (previousBlockInfoIt != blocks.end())
				{
					previousBlockInfoIt->second.nexts.push_back(block->blockHash);
					blockInfo.height = previousBlockInfoIt->second.height + 1;
					blockInfo.totalCoins = previousBlockInfoIt->second.totalCoins;
					blockInfo.totalBits += previousBlockInfoIt->second.totalBits;
				}
				else
				{
					// TODO: Assert empty hash
					blockInfo.height = 0;
				}
			}

			if (blockInfo.totalBits > totalBits)
			{
				blockInfo.longestChain = true;
				if (blockInfo.previous != longestBlock)
				{
					// Clear address cache
					// TODO: UTXO with undo?
					addressCache.clear();
				}
				else
				{
					// We were and we are still in longest chain
					processUTXO = true;
				}
			}

			blockLastHeight = blockInfo.height;
		}

		// Clear various structures (to avoid reallocation)
		batch.Clear();
		addressOperations.clear();
		addressOperationsBlock.clear();

		uint8_t blockKey[5];
		blockKey[0] = (uint8_t)DatabaseKeyHeader::Block;
		*((uint32_t*)&blockKey[1]) = swapByteOrder(blockInfo.index);

		DbBlock dbBlock;

		// Check if block has already been processed?
		if (rescanIndex == -1 && db->Get(leveldb::ReadOptions(), leveldb::Slice((const char*)blockKey, sizeof(blockKey)), &existingBlockText).ok())
		{
			// Currently unsupported, user should be using rescan index explicitely
			throw std::runtime_error("Block already existing without rescan index.");

			//Deserialize(existingBlockText, dbBlock);
			//
			//// Update BlockInfo::output
			//blockInfo.output = dbBlock.output;
			//blockInfo.reward = dbBlock.reward;
			//blockInfo.coinAgeDestroyed = dbBlock.coinAgeDestroyed;
			//
			//// PoS info
			//if (dbBlock.transactions.size() >= 2)
			//{
			//	DbTransaction dbTx;
			//	if (dbHelper.txLoad(dbBlock.transactions[1], dbTx, NULL, NULL))
			//	{
			//		UpdateProofOfStake(blockInfo, dbTx);
			//	}
			//}
			//
			//continue;
		}

		bool longestChain = blockInfo.longestChain;

		// Prepare block as stored in database
		dbBlock.input = 0;
		dbBlock.output = 0;
		dbBlock.reward = 0;
		dbBlock.hash = block->blockHash;
		dbBlock.previous = previousBlock;
		dbBlock.merkleRoot = block->merkleRoot;
		dbBlock.blockLength = block->blockLength;
		dbBlock.blockFormatVersion = block->blockFormatVersion;
		dbBlock.timeStamp = block->timeStamp;
		dbBlock.bits = block->bits;
		dbBlock.nonce = block->nonce;
		dbBlock.height = blockInfo.height;

		double coinAgeDestroyed = 0.0f;

		pendingTransactions.resize(block->transactionCount);

		for (uint32_t i = 0; i < block->transactionCount; ++i)
		{
			pendingTransactionIndices[(Hash256)block->transactions[i].transactionHash] = i;
			pendingTransactions[i].first = block->transactions[i].transactionHash;
		}

		// Add transactions
		for (int i = 0; i < block->transactionCount; ++i)
		{
			auto& transaction = block->transactions[i];
			transaction.transactionLength;

			Hash256 transactionHash = transaction.transactionHash;

			// Process transaction independently from this block (could also apply for memory pool transactions later)
			auto pendingTxIndex = pendingTransactionIndices.find(transactionHash);
			assert(pendingTxIndex != pendingTransactionIndices.end());
			auto& dbTx = pendingTransactions[pendingTxIndex->second].second;
			dbTx.clear();

			// Process transaction (register it in batch, register input's output, etc...)
			auto tx = processTransaction(batch, dbTx, transaction, dbBlock.timeStamp, txCheck, &pendingTransactionIndices, &pendingTransactions);

			// Add this block to this transaction
			DbTransactionBlock txBlock;
			txBlock.blockHash = blockInfo.index;
			txBlock.blockTxIndex = i;

			dbHelper.txSave(batch, transaction.transactionHash, dbTx);

			// Update tx blocks
			{
				// Layout: DatabaseKeyHeader::Tx (0x08), SourceTx, 0x03, DbTransactionBlock
				char searchKey[1 + sizeof(TxId) + 1 + sizeof(DbTransactionOutput)];
				searchKey[0] = (uint8_t)DatabaseKeyHeader::Tx;
				//memcpy(&searchKey[1], input.transactionHash, sizeof(Hash256));
				*(TxId*)&searchKey[1] = swapByteOrder(tx);
				searchKey[1 + sizeof(TxId)] = 0x03;
				*(DbTransactionBlock*)&searchKey[1 + sizeof(TxId) + 1] = txBlock;
				batch.Put(leveldb::Slice((const char*)searchKey, sizeof(searchKey)), leveldb::Slice());
			}

			coinAgeDestroyed += dbTx.coinAgeDestroyed;

			addressOperations.clear();

			if (i == 1)
			{
				UpdateProofOfStake(blockInfo, dbTx);
			}

			// Ignore PoS or PoW transaction amounts
			bool isReward = (blockInfo.type == BlockType::ProofOfStake && i == 1) || i == 0;

			// Process transaction for this block
			// Gather operations per address
			int outputIndex = 0;
			for (auto output = dbTx.outputs.begin(); output != dbTx.outputs.end(); ++output, ++outputIndex)
			{
				if (output->address.IsNull())
					continue;
				auto& addressOperation = addressOperations[output->address];
				addressOperation.transactionTime = dbTx.receivedTime;
				addressOperation.total_output += output->value;

				if (isReward)
					dbBlock.reward += output->value;
				else
					dbBlock.output += output->value;

				// Add new UTXO
				if (processUTXO)
				{
					DbAddressUTXOKey searchKey(output->address, DbTxRef(tx, outputIndex));
					DbAddressUTXO data(output->value, dbBlock.height);
					batch.Put(leveldb::Slice((const char*)&searchKey, sizeof(searchKey)), leveldb::Slice((const char*) &data, sizeof(data)));
				}
			}
			for (auto input = dbTx.inputs.begin(); input != dbTx.inputs.end(); ++input)
			{
				if (input->address.IsNull())
					continue;
				auto& addressOperation = addressOperations[input->address];
				addressOperation.transactionTime = dbTx.receivedTime;
				addressOperation.total_input += input->value;

				if (isReward)
					dbBlock.reward -= input->value;
				else
					dbBlock.input += input->value;

				// Delete spent UTXO
				if (processUTXO)
				{
					DbAddressUTXOKey searchKey(input->address, input->txRef);
					batch.Delete(leveldb::Slice((const char*) &searchKey, sizeof(searchKey)));
				}
			}

			// Used when we need to construct dbAddressOperationChain on the stack
			// Happens when we don't want addressCache to get corrupted while processing blocks that are not yet part of longest chain
			DbAddressOperationChain stackOperation;

			// Update address operation chain
			for (auto addressOperation = addressOperations.begin(); addressOperation != addressOperations.end(); ++addressOperation)
			{
				DbAddressOperationChain* dbAddressOperationChain;
				AddressOperation latestAddressOperation;
				std::map<Address, DbAddressOperationChain>::iterator addressOperationsBlockIt = addressOperationsBlock.end();
				if (longestChain && addressCache.exists(addressOperation->first))
				{
					// Still in cache?
					dbAddressOperationChain = &addressCache.get(addressOperation->first);
				}
				else if ((addressOperationsBlockIt = addressOperationsBlock.find(addressOperation->first)) != addressOperationsBlock.end())
				{
					// Current block cache
					dbAddressOperationChain = &addressOperationsBlockIt->second;

					if (longestChain)
					{
						dbAddressOperationChain = &addressCache.put(addressOperation->first, *dbAddressOperationChain).second;
					}
				}
				else if (getAddressOperation(addressOperation->first, latestAddressOperation, block->previousBlockHash, 0))
				{
					DbAddressOperationChain combinedAddressOperation;
					combinedAddressOperation.txCount = latestAddressOperation.txCount;
					combinedAddressOperation.total_input = latestAddressOperation.total_input;
					combinedAddressOperation.total_output = latestAddressOperation.total_output;

					if (longestChain)
					{
						dbAddressOperationChain = &addressCache.put(addressOperation->first, combinedAddressOperation).second;
					}
					else
					{
						stackOperation = combinedAddressOperation;
						dbAddressOperationChain = &stackOperation;
					}
				}
				else
				{
					if (longestChain)
					{
						dbAddressOperationChain = &addressCache.put(addressOperation->first, DbAddressOperationChain()).second;
					}
					else
					{
						stackOperation = DbAddressOperationChain();
						dbAddressOperationChain = &stackOperation;
					}
				}

				dbAddressOperationChain->txCount++;
				dbAddressOperationChain->total_input += addressOperation->second.total_input;
				dbAddressOperationChain->total_output += addressOperation->second.total_output;
				dbAddressOperationChain->transactionTime = addressOperation->second.transactionTime;

				DbAddressOperationChainKey key(addressOperation->first, dbBlock.height, i, blockInfo.index, tx);
				batch.Put(leveldb::Slice((const char*)&key, sizeof(key)), leveldb::Slice((const char*)dbAddressOperationChain, sizeof(DbAddressOperationChain)));

				// If we already updated the block address op, skip it
				// Otherwise, update it
				addressOperationsBlock[addressOperation->first] = *dbAddressOperationChain;
			}

			dbBlock.transactions.push_back(tx);
		}

		// Update BlockInfo::output
		blockInfo.output = dbBlock.output;
		blockInfo.reward = dbBlock.reward;
		blockInfo.coinAgeDestroyed = coinAgeDestroyed;

		dbBlock.coinAgeDestroyed = coinAgeDestroyed;

		double coinAge = 0.0;
		if (previousBlockInfoIt != blocks.end())
		{
			coinAge = previousBlockInfoIt->second.totalCoins * ((int64_t)dbBlock.timeStamp - (int64_t)previousBlockInfoIt->second.timeStamp);
			blockInfo.totalCoinAge = previousBlockInfoIt->second.totalCoinAge + coinAge;
		}

		blockInfo.totalCoins += (double)((int64_t)dbBlock.reward + (int64_t)dbBlock.output - (int64_t)dbBlock.input);
		blockInfo.totalCoinAge -= coinAgeDestroyed;

		dbBlock.totalCoinAge = blockInfo.totalCoinAge;
		dbBlock.totalCoins = blockInfo.totalCoins;

		// serialize obj into an std::string
		Serialize(serialText, dbBlock);
		batch.Put(leveldb::Slice((const char*)blockKey, sizeof(blockKey)), serialText);

		// Save blockchain state
		BackupState backupState;
		blockChain->getPosition(backupState.bcBlockIndex, backupState.bcBlockPosition, backupState.bcBlockNumber);
		Serialize(serialText, backupState);
		batch.Put(leveldb::Slice((const char*) &BackupStateKey, sizeof(BackupStateKey)), serialText);

		// Update blocks
		{
			boost::unique_lock<boost::mutex> guard1(pendingTransactionsMutex);
			boost::lock_guard<boost::mutex> guard2(chainMutex);

			blockIds.push_back(block->blockHash);

			if (blockInfo.longestChain)
			{
				if (blockInfo.previous != longestBlock)
				{
					// We have a new longest chain!
					// Remove blocks

					std::vector<Hash256> longestChainToAppend;

					auto previousBlock = blockInfo.previous;

					auto currentBlockHeight = blockInfo.height;

					while (currentBlockHeight > 0)
					{
						auto currentBlockInfo = blocks.find(previousBlock);
						assert(currentBlockInfo != blocks.end());

						previousBlock = currentBlockInfo->second.previous;

						currentBlockHeight = currentBlockInfo->second.height;
						if (currentBlockHeight < currentChain.size())
						{
							// Found a common block, let's stop here
							if (currentChain[currentBlockHeight] == currentBlockInfo->first)
							{
								currentBlockHeight++;
								break;
							}
							else
							{
								blocks[currentChain[currentBlockHeight]].longestChain = false;
							}
						}

						currentBlockInfo->second.longestChain = true;
						longestChainToAppend.push_back(currentBlockInfo->first);
					}

					// Undo UTXO of removed blocks
					while (currentChain.size() > currentBlockHeight)
					{
						auto lastBlockHash = currentChain.back();
						auto lastBlockIndex = blocks[lastBlockHash].index;
						DbBlock dbBlock2;
						if (!dbHelper.blockLoad(lastBlockIndex, dbBlock2))
							throw std::runtime_error("Could not undo block");

						DbTransaction dbTx;
						for (auto transaction = dbBlock2.transactions.rbegin(); transaction != dbBlock2.transactions.rend(); ++transaction)
						{
							dbTx.clear();
							auto txIndex = dbHelper.txLoad(*transaction, dbTx, NULL, NULL);
							if (txIndex == 0)
								throw std::runtime_error("Could not undo transaction");

							UndoUTXO(dbTx, txIndex, batch);
						}

						currentChain.pop_back();
					}

					currentChain.resize(currentBlockHeight);

					// Process UTXO for newly added blocks
					for (auto blockIt = longestChainToAppend.rbegin(); blockIt != longestChainToAppend.rend(); ++blockIt)
					{
						currentChain.push_back(*blockIt);

						auto currentBlock = blocks[*blockIt];

						DbBlock dbBlock2;
						if (!dbHelper.blockLoad(currentBlock.index, dbBlock2))
							throw std::runtime_error("Could not undo block");

						DbTransaction dbTx;
						for (auto transaction = dbBlock2.transactions.begin(); transaction != dbBlock2.transactions.end(); ++transaction)
						{
							dbTx.clear();
							auto txIndex = dbHelper.txLoad(*transaction, dbTx, NULL, NULL);
							if (txIndex == 0)
								throw std::runtime_error("Could not undo transaction");

							ProcessUTXO(dbTx, txIndex, currentBlock.height, batch);
						}
					}
				}
				totalBits = blockInfo.totalBits;
				longestBlock = block->blockHash;
				currentChain.push_back(block->blockHash);
			}

			blocks.insert(std::make_pair((Hash256)block->blockHash, blockInfo)).first;
			blockHeights.insert(std::make_pair(blockInfo.height, (Hash256)block->blockHash));

			// When we commit a new block, discard all pending TX
			// TODO: Somehow query RPC right away (or partial clear) so that previous live TX not in this block so that user won't see them disappearing until next request?
			pendingTransactions.clear();
			pendingTransactionIndices.clear();
			pendingTransactionsByAddress.clear();

			db->Write(leveldb::WriteOptions(), &batch);
		}

		// Only purge LRU cache after end of block so that we don't have dependent TX missing
		addressCache.purge();

		{
			// Purge TX ID cache as well
			boost::lock_guard<boost::recursive_mutex> guard(cacheMutex);
			txCache.purge();
			txCache2.purge();
		}
	}

	// Stop JSON RPC server
	serv.StopListening();

	// First stop the RPC daemon thread
	daemonThread.join();

	// Then stop bitcoin websocket thread
	websocket_server.stop();
	websocketThread.join();

	delete db;

	printf("Done...\n");
	return 0;
}
