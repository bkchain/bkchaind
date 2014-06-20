#include <boost/thread/locks.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <set>
#include "CacherServer.h"
#include "Hash256.h"
#include "Database.h"
#include "Serialization.h"
#include "Blocks.h"
#include "AddressOperations.h"
#include "BitcoinLiveTX.h"
#include "../blockchain/BitcoinAddress.h"
#include "BitcoinRPC.h"

// Doesn't seem to work for difficulties smaller than 1?
//inline float FastLog(float val)
//{
//	auto exp_ptr = reinterpret_cast<int32_t*>(&val);
//	auto x = *exp_ptr;
//	auto log_2 = ((x >> 23) & 255) - 128;
//	x &= ~(255 << 23);
//	x += 127 << 23;
//	*exp_ptr = x;
//
//	val = ((-1.0f / 3) * val + 2) * val - 2.0f / 3;
//	return ((val + log_2) * 0.69314718f);
//}
//
//float Difficulty(uint32_t bits)
//{
//	static double max_body = FastLog(0x00ffff);
//	static double scaland = FastLog(256);
//	return (float)exp(max_body - FastLog(bits & 0x00ffffff) + scaland * (0x1d - ((bits & 0xff000000) >> 24)));
//}

float Difficulty(uint32_t bits)
{
	int nShift = (bits >> 24) & 0xff;

	double dDiff =
		(double)0x0000ffff / (double)(bits & 0x00ffffff);

	while (nShift < 29)
	{
		dDiff *= 256.0;
		nShift++;
	}
	while (nShift > 29)
	{
		dDiff /= 256.0;
		nShift--;
	}

	return dDiff;
}

bool convertTime(uint32_t time, char* buffer, size_t bufferSize)
{
	time_t timeConverted = time;
	if (strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", (const tm*)(gmtime(&timeConverted))) == 0)
	{
		strcmp(buffer, "1970-01-01 00:00:00");
		return false;
	}
	return true;
}

void convertTx(uint8_t pubkeyAddress, Json::Value& tx, const DbTransaction& dbTx)
{
	tx["size"] = 0;
	char buffer[65];
	decodeHexString(dbTx.txHash.value, 32, buffer, true);
	tx["hash"] = buffer;
	convertTime(dbTx.receivedTime, buffer, sizeof(buffer));
	tx["time"] = buffer;

	uint64_t input = 0;
	uint64_t output = 0;

	for (auto in = dbTx.inputs.begin(); in != dbTx.inputs.end(); ++in)
	{
		Json::Value txIn(Json::objectValue);
		txIn["txi"] = in->txRef.index;
		Hash256 txRefHash = dbHelper.txGetHash(in->txRef.tx);
		decodeHexString(txRefHash.value, 32, buffer, true);
		txIn["tx"] = buffer;
		txIn["v"] = (double)in->value;
		input += in->value;
		if (in->txRef.index == -1)
		{
			if (in->value == 0)
				txIn["addr"] = "pos";
			else
				txIn["addr"] = "pow";
		}
		else
		{
			if (in->address.IsNull())
			{
				txIn["addr"] = "None";
			}
			else
			{
				bitcoinHash160ToAscii(pubkeyAddress, (uint8_t*)&in->address, buffer, 65);
				txIn["addr"] = buffer;
			}
		}
		tx["ins"].append(txIn);
	}
	
	for (auto out = dbTx.outputs.begin(); out != dbTx.outputs.end(); ++out)
	{
		Json::Value txOut(Json::objectValue);
		txOut["v"] = (double)out->value;
		output += out->value;
		if (out->address.IsNull())
		{
			txOut["addr"] = "None";
		}
		else
		{
			bitcoinHash160ToAscii(pubkeyAddress, (uint8_t*)&out->address, buffer, 65);
			txOut["addr"] = buffer;
		}
		tx["outs"].append(txOut);
	}

	tx["in"] = (double)input;
	tx["out"] = (double)output;
}

void convertTx(uint8_t pubkeyAddress, Json::Value& tx, const DbTransaction& dbTx, const std::vector<DbTransactionOutput>& txOutputs, const std::vector<DbTransactionBlock>& txBlocks)
{
	convertTx(pubkeyAddress, tx, dbTx);

	tx["size"] = dbTx.transactionLength;
	tx["ver"] = dbTx.transactionVersionNumber;

	bool longestChain = false;
	int32_t conf_count = 0;

	tx["blocks"] = Json::Value(Json::ValueType::arrayValue);

	char buffer[65];
	for (auto block = txBlocks.begin(); block != txBlocks.end(); ++block)
	{
		{
			boost::lock_guard<boost::mutex> guard(chainMutex);
			auto blockInfoIt = blocks.find(blockIds[block->blockHash]);
			if (blockInfoIt != blocks.end())
			{
				auto blockInfo = blockInfoIt->second;
				if (blockInfo.longestChain)
				{
					longestChain = true;
					conf_count = (int32_t)((int64_t)currentChain.size() - (int64_t)blockInfo.height);
				}
			}
		}

		decodeHexString(blockIds[block->blockHash].value, 32, buffer, true);
		tx["blocks"].append(buffer);
	}

	tx["main_chain"] = longestChain;
	tx["confirmed"] = conf_count >= 6;
	tx["confirmations"] = conf_count;
	tx["coinage_destroyed"] = dbTx.coinAgeDestroyed / 86400.0;

	for (auto output = txOutputs.begin(); output != txOutputs.end(); ++output)
	{
		Hash256 txRefHash = dbHelper.txGetHash(output->txRef.tx);
		decodeHexString(txRefHash.value, 32, buffer, true);
		tx["outs"][output->txOutIndex]["tx"] = buffer;
		tx["outs"][output->txOutIndex]["txi"] = output->txRef.index;
	}
}

Json::Value CacherServer::getblock(const std::string& hash)
{
	Json::Value response(Json::objectValue);

	Hash256 blockHash;
	if (!encodeHexString((uint8_t*)&blockHash, sizeof(Hash256), hash, true))
		return response;

	getblock(blockHash, response);

	return response;
}

Json::Value CacherServer::getrawtx(const std::string& hash)
{
	Json::Value response(Json::objectValue);

	std::string transactionText;
	Hash256 hash2;
	if (!encodeHexString(&hash2.value[0], sizeof(Hash256), hash.c_str(), true))
	{
		response["error"] = "Could not decode hash";
		return response;
	}
	DbTransaction dbTx;
	std::vector<DbTransactionOutput> txOutputs;
	std::vector<DbTransactionBlock> txBlocks;
	if (dbHelper.txLoad(hash2.value, dbTx, &txOutputs, &txBlocks))
	{
		convertTx(pubkeyAddress, response, dbTx, txOutputs, txBlocks);
	}
	else
	{
		response["error"] = "Could not find transaction";
	}

	return response;
}

void convertBlock(Json::Value& block, const Hash256& hash, const BlockInfo& blockInfo)
{
	char buffer[65];
	decodeHexString((const uint8_t*)&hash, 32, buffer, true);
	block["hash"] = buffer;
	block["height"] = (int32_t)blockInfo.height;
	convertTime(blockInfo.timeStamp, buffer, sizeof(buffer));
	block["time"] = buffer;
	block["size"] = blockInfo.blockLength;
	block["out"] = (double)blockInfo.output;
	block["reward"] = (double)blockInfo.reward;
	block["txcount"] = blockInfo.transactionCount;
	block["diff"] = Difficulty(blockInfo.bits);
	block["coinage_destroyed"] = blockInfo.coinAgeDestroyed / 86400.0;
	block["avg_coinage"] = blockInfo.totalCoinAge / blockInfo.totalCoins / 86400.0;


	if (blockInfo.type == BlockType::ProofOfStake)
	{
		block["type"] = "pos";
		block["stake_amount"] = (double)blockInfo.staked;
		block["stake_coinage_destroyed"] = blockInfo.stakeCoinAgeDestroyed / 86400.0;
		block["stake_age"] = blockInfo.stakeCoinAgeDestroyed / 86400.0 / blockInfo.staked;
	}
	else
	{
		block["type"] = "pow";
	}
}

Json::Value CacherServer::getblocks(const int& page)
{
	Json::Value response(Json::arrayValue);

	// TODO: Optimize the lock (copy hash & block infos)
	boost::lock_guard<boost::mutex> guard(chainMutex);

	if (currentChain.size() <= 20 * page)
		return response;

	int count = 0;

	for (auto blockHash = currentChain.rbegin() + 20 * page; blockHash < currentChain.rend() && count < 20; ++blockHash, ++count)
	{
		auto& blockInfo = blocks[*blockHash];

		Json::Value block(Json::objectValue);
		convertBlock(block, *(Hash256*)blockHash->value, blockInfo);
		response.append(block);
	}
	return response;
}

void processLiveTransactions(Address& address, AddressOperation& addressOperation, std::deque<AddressOperation>* addressOperations)
{
	// Get pending operations, ordered by indices
	boost::lock_guard<boost::mutex> guard(pendingTransactionsMutex);
	auto range = pendingTransactionsByAddress.equal_range(address);
	std::set<uint32_t> pendingTxIndices;
	for (auto it = range.first; it != range.second; ++it)
	{
		pendingTxIndices.insert(it->second);
	}

	// Make space for new operations
	// TODO: Support case when there is more than 50 unconfirmed transactions? (we need to check all of them to reach confirmed ones)
	//if (pendingTxIndices.size() < 50)
	//	std::copy_backward(&addressOperations[0], &addressOperations[50 - pendingTxIndices.size()], &addressOperations[50]);

	// Extract tx infos
	for (auto it = pendingTxIndices.begin(); it != pendingTxIndices.end(); ++it)
	{
		auto& dbTx = pendingTransactions[*it];

		addressOperation.txCount++;
		addressOperation.transactionTime = dbTx.second.getBestTimeStamp();
		addressOperation.blockHeight = 0;
		addressOperation.blockTxIndex = 0;
		addressOperation.blockHash = 0xFFFFFFFF;
		addressOperation.txHashIndex = dbHelper.txGetId(dbTx.first);
		addressOperation.input = 0;
		addressOperation.output = 0;

		for (auto output = dbTx.second.outputs.begin(); output != dbTx.second.outputs.end(); ++output)
		{
			if (output->address != address)
				continue;

			addressOperation.output += output->value;
		}

		for (auto input = dbTx.second.inputs.begin(); input != dbTx.second.inputs.end(); ++input)
		{
			if (input->address != address)
				continue;

			addressOperation.input += input->value;
		}

		addressOperation.total_input += addressOperation.input;
		addressOperation.total_output += addressOperation.output;

		if (addressOperations != NULL)
			addressOperations->push_front(addressOperation);
	}
}

Json::Value CacherServer::getaddress(const std::string& addressText)
{
	Json::Value response;

	//auto addressText = request["address"];
	Address address;
	if (!bitcoinAsciiToHash160(addressText.c_str(), address.value))
		return response;

	response["addr"] = addressText;

	Hash256 latestBlock;
	{
		boost::lock_guard<boost::mutex> guard(chainMutex);
		latestBlock = currentChain.back();
	}

	std::deque<AddressOperation> addressOperations;

	// Get one more operation to have correct sum in last item.
	auto operationCount = getAddressOperations(address, 0, 50, addressOperations, latestBlock);

	// If no operation, clear first one so that we have a valid base
	AddressOperation addressOperation;

	if (operationCount > 0)
	{
		addressOperation = addressOperations.front();
	}
	else
	{
		memset(&addressOperation, 0, sizeof(addressOperation));
	}

	processLiveTransactions(address, addressOperation, &addressOperations);

	if (addressOperations.size() > 50)
		addressOperations.resize(50);

	response["ops"] = Json::Value(Json::ValueType::arrayValue);

	for (auto dbAddressOp = addressOperations.begin(); dbAddressOp != addressOperations.end(); ++dbAddressOp)
	{
		DbTransaction dbTx;
		dbHelper.txLoad(dbAddressOp->txHashIndex, dbTx, NULL, NULL);

		// TODO: blocks empty? Block doesn't exist?
		bool hasBlockInfo = false;
		int32_t currentChainSize;
		BlockInfo blockInfo;
		{
			boost::lock_guard<boost::mutex> guard(chainMutex);
			currentChainSize = currentChain.size();
			auto blockInfoIt = dbAddressOp->blockHash != 0xFFFFFFFF ? blocks.find(blockIds[dbAddressOp->blockHash]) : blocks.end();
			if (blockInfoIt != blocks.end())
			{
				// We either take the first block, or the block in the longest chain if it exists.
				blockInfo = blockInfoIt->second;
				hasBlockInfo = true;
			}
		}

		int32_t confCount = 0;
		bool longestChain = false;
		if (hasBlockInfo)
		{
			confCount = currentChainSize - (int32_t)blockInfo.height;
			longestChain = blockInfo.longestChain;
		}

		auto balance = (int64_t)dbAddressOp->total_output - (int64_t)dbAddressOp->total_input;

		bool conf = confCount >= 6;

		// Count only pending transactions or longest chain if included in a block
		// Note: we should discard/ignore transactions that were rejected at some point
		//if (!hasBlockInfo || longestChain)
		//{
		//	totalInput += dbAddressOp->second.input;
		//	totalOutput += dbAddressOp->second.output;
		//
		//	if (conf)
		//	{
		//		totalInputConf += dbAddressOp->second.input;
		//		totalOutputConf += dbAddressOp->second.output;
		//	}
		//}

		Json::Value op(Json::objectValue);
		char buffer[65];
		if (hasBlockInfo)
		{
			decodeHexString(blockIds[dbAddressOp->blockHash].value, 32, buffer, true);
			op["blockhash"] = buffer;
		}
		op["confirmed"] = conf;
		op["confirmations"] = confCount;
		op["balance"] = (double)balance;
		op["main_chain"] = longestChain;
		decodeHexString(dbHelper.txGetHash(dbAddressOp->txHashIndex).value, 32, buffer, true);
		op["txhash"] = buffer;
		convertTime(dbAddressOp->transactionTime, buffer, sizeof(buffer));
		op["time"] = buffer;
		op["input"] = (double)dbAddressOp->input;
		op["output"] = (double)dbAddressOp->output;
		op["blockheight"] = hasBlockInfo ? (int32_t)blockInfo.height : -1;

		response["ops"].append(op);
	}

	auto totalInput = operationCount > 0 ? addressOperations[0].total_input : 0;
	auto totalOutput = operationCount > 0 ? addressOperations[0].total_output : 0;
	auto txCount = operationCount > 0 ? addressOperations[0].txCount : 0;

	response["txcount"] = txCount;
	response["total_input"] = (double)totalInput;
	response["total_output"] = (double)totalOutput;

	return response;
}

Json::Value CacherServer::getblockhash(const int& height)
{
	Json::Value response;

	boost::lock_guard<boost::mutex> guard(chainMutex);

	if (height >= currentChain.size() || height < 0)
		return response;

	char buffer[65];
	decodeHexString(currentChain[height].value, 32, buffer, true);
	response["hash"] = buffer;

	return response;
}

Json::Value CacherServer::getblockheight(const int& height)
{
	Json::Value response;

	boost::lock_guard<boost::mutex> guard(chainMutex);

	response["blocks"] = Json::Value(Json::ValueType::arrayValue);

	auto range = blockHeights.equal_range(height);
	for (auto it = range.first; it != range.second; ++it)
	{
		char buffer[65];
		decodeHexString(it->second.value, 32, buffer, true);
		response["blocks"].append(buffer);
	}

	return response;
}

Json::Value CacherServer::getlatesttransactions()
{
	Json::Value response(Json::arrayValue);

	boost::lock_guard<boost::mutex> guard(pendingTransactionsMutex);

	auto count = 0;
	for (auto it = lastPendingTransactions.rbegin(); it != lastPendingTransactions.rend() && count < 10; it++, ++count)
	{
		Json::Value tx;

		uint64_t totalOutput = 0;
		for (auto output = it->second.outputs.begin(); output != it->second.outputs.end(); ++output)
		{
			if (output->address.IsNull())
				continue;
			totalOutput += output->value;
		}

		char buffer[65];
		decodeHexString((uint8_t*)&it->first, 32, buffer, true);
		tx["hash"] = buffer;
		tx["output"] = (double)totalOutput;
		convertTime(it->second.getBestTimeStamp(), buffer, sizeof(buffer));
		tx["time"] = buffer;
		tx["coinage_destroyed"] = it->second.coinAgeDestroyed / 86400.0;
		response.append(tx);
	}

	return response;
}

void CacherServer::getblock(const Hash256& hash, Json::Value& response)
{
	BlockInfo blockInfo;
	{
		boost::lock_guard<boost::mutex> guard(chainMutex);
		auto blockInfoIt = blocks.find(hash);
		if (blockInfoIt == blocks.end())
		{
			response["error"] = "Could not find block";
			return;
		}
		blockInfo = blockInfoIt->second;
	}

	uint8_t hash2[1 + sizeof(uint32_t)];
	hash2[0] = (uint8_t)DatabaseKeyHeader::Block;
	*((uint32_t*) &hash2[1]) = swapByteOrder(blockInfo.index);

	std::string dbText;
	if (db->Get(leveldb::ReadOptions(), leveldb::Slice((const char*) hash2, sizeof(hash2)), &dbText).ok())
	{
		DbBlock dbBlock;
		Deserialize(dbText, dbBlock);

		//response["index"] = blockInfo.index;
		response["main_chain"] = blockInfo.longestChain;
		response["confirmations"] = (int32_t) ((int64_t) currentChain.size() - (int64_t) blockInfo.height);
		response["size"] = dbBlock.blockLength;
		response["height"] = (int32_t) dbBlock.height;
		response["out"] = (double)dbBlock.output;
		response["in"] = (double)dbBlock.input;
		response["reward"] = (double)dbBlock.reward;
		response["txcount"] = (int32_t) dbBlock.transactions.size();
		response["nonce"] = dbBlock.nonce;
		response["bits"] = dbBlock.bits;
		response["diff"] = Difficulty(dbBlock.bits);
		response["ver"] = dbBlock.blockFormatVersion;
		char buffer[65];
		decodeHexString(dbBlock.hash.value, 32, buffer, true);
		response["hash"] = buffer;
		convertTime(dbBlock.timeStamp, buffer, sizeof(buffer));
		response["time"] = buffer;
		decodeHexString(dbBlock.merkleRoot.value, 32, buffer, true);
		response["merkle"] = buffer;
		decodeHexString(blockInfo.previous.value, 32, buffer, true);
		response["prev"] = buffer;
		response["nexts"] = Json::Value(Json::ValueType::arrayValue);
		for (auto nextBlock = blockInfo.nexts.begin(); nextBlock != blockInfo.nexts.end(); ++nextBlock)
		{
			decodeHexString(nextBlock->value, 32, buffer, true);
			response["nexts"].append(buffer);
		}

		response["coinage_destroyed"] = dbBlock.coinAgeDestroyed / 86400.0;
		if(dbBlock.totalCoins > 0)
			response["avg_coinage"] = dbBlock.totalCoinAge / dbBlock.totalCoins / 86400.0;
		else
			response["avg_coinage"] = 0;

		if (blockInfo.type == BlockType::ProofOfStake)
		{
			response["type"] = "pos";
			response["stake_amount"] = (double)blockInfo.staked;
			response["stake_coinage_destroyed"] = blockInfo.stakeCoinAgeDestroyed / 86400.0;
			if(blockInfo.staked > 0)
				response["stake_age"] = blockInfo.stakeCoinAgeDestroyed / 86400.0 / blockInfo.staked;
			else
				response["stake_age"] = 0;
		}
		else
		{
			response["type"] = "pow";
		}

		DbTransaction dbTx;
		std::vector<DbTransactionOutput> txOutputs;
		std::vector<DbTransactionBlock> txBlocks;

		bool isProofOfStake = false;

		response["transactions"] = Json::Value(Json::ValueType::arrayValue);
		
		int transactionIndex = 0;
		for (auto transaction = dbBlock.transactions.begin(); transaction != dbBlock.transactions.end(); ++transaction, ++transactionIndex)
		{
			Json::Value tx(Json::objectValue);
			dbTx.clear();
			txOutputs.clear();
			txBlocks.clear();

			if (dbHelper.txLoad(*transaction, dbTx, &txOutputs, &txBlocks))
			{
				convertTx(pubkeyAddress, tx, dbTx, txOutputs, txBlocks);
				response["transactions"].append(tx);
			}
		}
	}
}

void CacherServer::getrawtx(const Hash256& hash, Json::Value& response)
{
	DbTransaction dbTx;
	std::vector<DbTransactionOutput> txOutputs;
	std::vector<DbTransactionBlock> txBlocks;
	if (dbHelper.txLoad(hash.value, dbTx, &txOutputs, &txBlocks))
	{
		convertTx(pubkeyAddress, response, dbTx, txOutputs, txBlocks);
	}
	else
	{
		response["error"] = "Could not find transaction";
	}
}

// tx/hash/$txhash
Json::Value CacherServer::tx_hash(const std::string& hash)
{
	return getrawtx(hash);
}

// tx/id/$txindex
Json::Value CacherServer::tx_index(const int& index)
{
	Json::Value response(Json::objectValue);
	DbTransaction dbTx;
	std::vector<DbTransactionOutput> txOutputs;
	std::vector<DbTransactionBlock> txBlocks;
	if (dbHelper.txLoad(index, dbTx, &txOutputs, &txBlocks))
	{
		convertTx(pubkeyAddress, response, dbTx, txOutputs, txBlocks);
	}
	else
	{
		response["error"] = "Could not find transaction";
	}

	return response;
}

// tx/push
void CacherServer::tx_pushI(const Json::Value& request, Json::Value& response)
{
	try
	{
		rpcClient->CallMethod("sendrawtransaction", request, response);
	}
	catch (...)
	{
		response = "exception";
	}
}

// block/hash/$blockhash
Json::Value CacherServer::block_hash(const std::string& hash)
{
	Json::Value response(Json::objectValue);

	Hash256 blockHash;
	if (!encodeHexString((uint8_t*) &blockHash, sizeof(Hash256), hash, true))
	{
		response["error"] = "Could not decode hash";
		return response;
	}

	getblock(blockHash, response);

	return response;
}

// block/index/$blockindex
Json::Value CacherServer::block_index(const int& index)
{
	Json::Value response(Json::objectValue);

	Hash256 blockHash;
	{
		boost::lock_guard<boost::mutex> guard(chainMutex);

		if (index >= blockIds.size())
		{
			response["error"] = "Could not find block";
			return response;
		}

		blockHash = blockIds[index];
	}

	getblock(blockHash, response);

	return response;
}

// block/height/$blockheight
Json::Value CacherServer::block_height(const int& index)
{
	Json::Value response(Json::objectValue);

	std::vector<Hash256> blockHashes;
	{
		boost::lock_guard<boost::mutex> guard(chainMutex);
		auto blockHeightRange = blockHeights.equal_range(index);

		for (auto it = blockHeightRange.first; it != blockHeightRange.second; ++it)
		{
			blockHashes.push_back(it->second);
		}
	}

	response["blocks"] = Json::Value(Json::ValueType::arrayValue);

	for (auto it = blockHashes.begin(); it != blockHashes.end(); ++it)
	{
		Json::Value block(Json::objectValue);
		getblock(*it, block);
		response["blocks"].append(block);
	}

	return response;
}

Json::Value CacherServer::address_balance(const std::string& addressesText, const int& confirmations)
{
	Json::Value response;

	std::vector<boost::iterator_range<std::string::const_iterator>> addresses;
	boost::split(addresses, addressesText, boost::is_any_of(","), boost::token_compress_off);

	std::string addressText;
	addressText.reserve(35);
	Address address;

	// TODO: Keep lock to have atomic results of chain + unconfirmed results?
	Hash256 latestBlock;
	{
		boost::lock_guard<boost::mutex> guard(chainMutex);
		latestBlock = currentChain.back();
	}

	for (auto addressIt = addresses.begin(); addressIt != addresses.end(); ++addressIt)
	{
		addressText.assign(addressIt->begin(), addressIt->end());

		if (!bitcoinAsciiToHash160(addressText.c_str(), address.value))
		{
			response["error"] = "Could not decode address";
			return response;
		}

		if (confirmations < 0)
		{
			response["error"] = "Confirmations should be a non-negative number";
			return response;
		}

		Json::Value result(Json::objectValue);

		result["address"] = addressText;

		AddressOperation addressOperation;
		getAddressOperation(address, addressOperation, latestBlock, confirmations);

		// Check unconfirmed transactions
		if (confirmations == 0)
		{
			processLiveTransactions(address, addressOperation, NULL);
		}

		result["balance"] = (double)((int64_t) addressOperation.total_output - (int64_t) addressOperation.total_input);
		result["txcount"] = addressOperation.txCount;
		result["total_input"] = (double)addressOperation.total_input;
		result["total_output"] = (double)addressOperation.total_output;

		response.append(result);
	}

	return response;
}

// Hash for DbTxRef
namespace std {
	template <>
	class hash<DbTxRef>
	{
	public:
		size_t operator()(const DbTxRef& x) const
		{
			return (size_t)x.tx ^ (size_t)x.index;
		}
	};
}

// Note: Implementation is not optimal (bruteforce all spent & unspent outputs of all tx related to this address).
// A dedicated database entries that tracks unspent output would probably be better.
// However, considering our current use case (wallet), it should be good enough.
Json::Value CacherServer::address_unspent(const std::string& addressesText, const int& confirmations)
{
	struct AddressUnspent
	{
		DbAddressUTXO utxo;
		llvm::SmallVector<uint8_t, 512> challengeScript;
		Hash256 txHash;
	};

	Json::Value response;

	std::vector<boost::iterator_range<std::string::const_iterator>> addresses;
	boost::split(addresses, addressesText, boost::is_any_of(","), boost::token_compress_off);

	std::string addressText;
	addressText.reserve(35);
	Address address;

	Hash256 latestBlock;
	{
		boost::lock_guard<boost::mutex> guard(chainMutex);
		latestBlock = currentChain.back();
	}

	std::vector<DbTransactionOutput> txOutputs;
	std::vector<DbTransactionBlock> txBlocks;
	std::vector<bool> outputsUsed;

	for (auto addressIt = addresses.begin(); addressIt != addresses.end(); ++addressIt)
	{
		addressText.assign(addressIt->begin(), addressIt->end());

		// Decode address
		if (!bitcoinAsciiToHash160(addressText.c_str(), address.value))
		{
			response["error"] = "Could not decode address";
			return response;
		}

		Json::Value result(Json::objectValue);
		result["address"] = addressText;

		result["unspent"] = Json::Value(Json::ValueType::arrayValue);

		DbAddressUTXOKey searchKey(address, DbTxRef(0, 0));
		auto it = std::unique_ptr<leveldb::Iterator>(db->NewIterator(leveldb::ReadOptions()));

		DbTransaction dbTx2;

		// TODO: Use another structure to keep order (and sort by height)?
		std::unordered_map<DbTxRef, AddressUnspent> unspentOutputs;

		// Iterate over UTXO (unspent outputs)
		for (it->Seek(leveldb::Slice((const char*) &searchKey, sizeof(searchKey))); it->Valid(); it->Next())
		{
			auto key = it->key();

			// Still processing same address?
			if (key.size() != sizeof(searchKey) || memcmp(key.data(), &searchKey, sizeof(DatabaseKeyHeader) +sizeof(Address)) != 0)
				break;

			auto keyData = (DbAddressUTXOKey*) key.data();
			auto valueData = (DbAddressUTXO*) it->value().data();

			if (currentChain.size() - (int64_t) valueData->height + 1 >= confirmations)
			{
				dbTx2.clear();
				if (!dbHelper.txLoad(keyData->txRef.tx, dbTx2, NULL, &txBlocks))
				{
					response["error"] = "Could not process transaction. Please report.";
					return response;
				}

				AddressUnspent unspent;
				unspent.utxo = *valueData;
				unspent.txHash = dbTx2.txHash;
				unspent.challengeScript = dbTx2.outputs[keyData->txRef.index].challengeScript;
				unspentOutputs[keyData->txRef] = unspent;
			}
		}

		// If confirmations is 0, also includes pending transactions (a.k.a. live tx)
		if (confirmations == 0)
		{
			boost::lock_guard<boost::mutex> guard(pendingTransactionsMutex);
			auto range = pendingTransactionsByAddress.equal_range(address);
			
			// Use std::set so that transactions will be in ascending order
			std::set<uint32_t> pendingTxIndices;
			for (auto it = range.first; it != range.second; ++it)
			{
				pendingTxIndices.insert(it->second);
			}

			// Apply pending transactions UTXO changes
			for (auto it = pendingTxIndices.begin(); it != pendingTxIndices.end(); ++it)
			{
				auto& dbTx = pendingTransactions[*it];

				for (auto input = dbTx.second.inputs.begin(); input != dbTx.second.inputs.end(); ++input)
				{
					if (input->address != address)
						continue;

					// Delete spent UTXO
					unspentOutputs.erase(input->txRef);
				}

				int outputIndex = 0;
				for (auto output = dbTx.second.outputs.begin(); output != dbTx.second.outputs.end(); ++output, ++outputIndex)
				{
					if (output->address != address)
						continue;

					// Add new UTXO
					auto txRef = DbTxRef(dbHelper.txGetId(dbTx.first), outputIndex);

					AddressUnspent unspent;
					unspent.utxo = DbAddressUTXO(output->value, currentChain.size());
					unspent.txHash = dbTx.first;
					unspent.challengeScript = output->challengeScript;

					unspentOutputs[txRef] = unspent;
				}
			}
		}

		std::string challengeScript;

		for (auto unspentOutput = unspentOutputs.begin(); unspentOutput != unspentOutputs.end(); ++unspentOutput)
		{
			// append outputs[i] to result
			Json::Value txOut(Json::objectValue);
			txOut["txi"] = unspentOutput->first.index;
			char buffer[65];
			decodeHexString(unspentOutput->second.txHash.value, 32, buffer, true);
			txOut["tx"] = buffer;
			txOut["v"] = (double)unspentOutput->second.utxo.value;

			challengeScript.clear();
			decodeHexString((const uint8_t*)&unspentOutput->second.challengeScript.front(), unspentOutput->second.challengeScript.size(), challengeScript, false);
			txOut["script"] = challengeScript;

			result["unspent"].append(txOut);
		}

		response.append(result);
	}

	return response;
}
