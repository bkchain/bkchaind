#include "BitcoinTX.h"
#include <boost/pool/pool_alloc.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include "ScriptDecoder.h"
#include "../blockchain/BitcoinAddress.h"
#include "leveldb/cache.h"
#include "leveldb/write_batch.h"

boost::fast_pool_allocator<DbTxUnspentOutput> unspentPool;
int64_t unspentPoolCount = 0;
extern leveldb::Cache* unspentCache;

void unspentOutputDeleter(const leveldb::Slice& key, void* value)
{
	unspentPool.deallocate((DbTxUnspentOutput*) value);
	unspentPoolCount--;
}

/// Process a transaction:
/// - If txCheck is true, check if it has already been added (early exit)
/// - Decode it and add it to database (in batch)
/// - Populate outputs of source transactions
TxId processTransaction(leveldb::WriteBatch& batch, DbTransaction& dbTx, const BlockChain::BlockTransaction& transaction, uint32_t receivedTime, bool txCheck,
	std::unordered_map<Hash256, uint32_t>* pendingTransactionIndices, std::vector<std::pair<Hash256, DbTransaction>>* pendingTransactions)
{
	Hash256 transactionHash = transaction.transactionHash;

	std::string existingTransaction;

	if (txCheck)
	{
		auto tx = dbHelper.txLoad(transactionHash, dbTx, NULL, NULL);
		if (tx != 0)
			return tx;
	}

	auto txIndex = dbHelper.txGetOrCreateId(transactionHash);

	dbTx.txHash = transactionHash;
	dbTx.transactionLength = transaction.transactionLength;
	dbTx.transactionVersionNumber = transaction.transactionVersionNumber;
	dbTx.receivedTime = receivedTime;
	if (DbTransaction::transactionContainsTimestamp)
		dbTx.timeStamp = transaction.timeStamp;

	uint64_t outputValue = 0;

	for (int j = 0; j < transaction.outputCount; ++j)
	{
		auto& output = transaction.outputs[j];
		dbTx.outputs.push_back(DbTxOutput(&output));

		ScriptDecoder scriptDecoder(output.challengeScript, output.challengeScriptLength);

		bool hasAddress = false;

		uint8_t opcode;
		uint8_t scriptData[512];
		uint8_t scriptDataLength = 0;

		// Decode for standard patterns:
		//std::regex regex1("^OP_DUP OP_HASH160 ([0-9A-F]{40}) OP_EQUALVERIFY OP_CHECKSIG$");
		//std::regex regex2("^OP_HASH160 ([0-9A-F]{40}) OP_EQUAL$");
		//std::regex regex3("^([0-9A-F]{66,130}) OP_CHECKSIG$");
		if ((scriptDecoder.readOpcode(opcode) && opcode == OP_DUP
			&& scriptDecoder.readOpcode(opcode) && opcode == OP_HASH160
			&& scriptDecoder.readOpcode(opcode) && opcode == 20 && scriptDecoder.readData(opcode, scriptData)
			&& scriptDecoder.readOpcode(opcode) && opcode == OP_EQUALVERIFY
			&& scriptDecoder.readOpcode(opcode) && opcode == OP_CHECKSIG)
			|| (scriptDecoder.reset() && scriptDecoder.readOpcode(opcode) && opcode == OP_HASH160
			&& scriptDecoder.readOpcode(opcode) && opcode == 20 && scriptDecoder.readData(opcode, scriptData)
			&& scriptDecoder.readOpcode(opcode) && opcode == OP_EQUAL))
		{
			memcpy(dbTx.outputs.back().address.value, scriptData, 20);
			hasAddress = true;
		}
		else if (scriptDecoder.reset()
			&& scriptDecoder.readOpcode(opcode) && (opcode == 33 || opcode == 65) && scriptDecoder.readData(scriptDataLength = opcode, scriptData)
			&& scriptDecoder.readOpcode(opcode) && opcode == OP_CHECKSIG)
		{
			bitcoinPublicKeyToHash160(scriptData, dbTx.outputs.back().address.value);
			hasAddress = true;
		}
		else
		{
			memset(dbTx.outputs.back().address.value, 0, 20);
		}

		outputValue += dbTx.outputs.back().value;

		// Save optimized unspent output
		char searchKey[sizeof(Hash256) +sizeof(uint32_t)];
		memcpy(searchKey, &transactionHash, sizeof(Hash256));
		*(uint32_t*) &searchKey[sizeof(Hash256)] = j;


		auto unspentOutput = unspentPool.allocate();
		unspentPoolCount++;
		unspentOutput->value = dbTx.outputs.back().value;
		unspentOutput->address = dbTx.outputs.back().address;
		unspentOutput->timeStamp = dbTx.getBestTimeStamp();
		auto handle = unspentCache->Insert(leveldb::Slice(searchKey, sizeof(searchKey)), unspentOutput, sizeof(searchKey) +sizeof(DbTxUnspentOutput), unspentOutputDeleter);
		unspentCache->Release(handle);
	}
	boost::multiprecision::uint128_t bnSatoshiSecond = 0;
	for (int j = 0; j < transaction.inputCount; ++j)
	{
		auto& input = transaction.inputs[j];
		dbTx.inputs.push_back(DbTxInput(&input));

		// Update inputs
		if (input.transactionIndex == 0xFFFFFFFF)
		{
			dbTx.inputs.back().value = outputValue;
			memset(&dbTx.inputs.back().address, 0, sizeof(Address));
		}
		else
		{
			DbTxOutput dbTxOutput;
			uint32_t dbTxTimestamp;
			Hash256 inputTransactionHash = input.transactionHash;
			std::unordered_map<Hash256, uint32_t>::iterator transactionIt;

			char searchKey2[sizeof(Hash256) +sizeof(uint32_t)];
			memcpy(searchKey2, &inputTransactionHash, sizeof(Hash256));
			*(uint32_t*) &searchKey2[sizeof(Hash256)] = input.transactionIndex;

			leveldb::Cache::Handle* handle;

			if (pendingTransactionIndices != NULL
				&& (transactionIt = pendingTransactionIndices->find(inputTransactionHash)) != pendingTransactionIndices->end())
			{
				dbTxOutput = (*pendingTransactions)[transactionIt->second].second.outputs[input.transactionIndex];
				dbTxTimestamp = (*pendingTransactions)[transactionIt->second].second.getBestTimeStamp();
				unspentCache->Erase(leveldb::Slice(searchKey2, sizeof(searchKey2)));
			}
			else if ((handle = unspentCache->Lookup(leveldb::Slice(searchKey2, sizeof(searchKey2)))) != NULL)
			{
				auto unspentOutput = (DbTxUnspentOutput*) unspentCache->Value(handle);
				dbTxOutput.value = unspentOutput->value;
				dbTxOutput.address = unspentOutput->address;
				dbTxTimestamp = unspentOutput->timeStamp;
				unspentCache->Release(handle);
				unspentCache->Erase(leveldb::Slice(searchKey2, sizeof(searchKey2)));
			}
			else
			{
				DbTransaction dbTx2;
				if (dbHelper.txLoad(inputTransactionHash, dbTx2, NULL, NULL))
				{
					dbTxOutput = dbTx2.outputs[input.transactionIndex];
					dbTxTimestamp = dbTx2.getBestTimeStamp();
				}
				else
				{
					// Transaction not found, probably because we are processing a non-verified memory pool transaction that reference another memory pool transaction.
					// Shouldn't happen with getrawmempool since we have "depends".
					char buffer1[65];
					char buffer2[65];
					decodeHexString(transactionHash.value, 32, buffer1, true);
					decodeHexString(input.transactionHash, 32, buffer2, true);
					char message[255];
					sprintf(message, "[Error] tx %s : Could not find source transaction %s!\n", buffer1, buffer2);
					throw std::runtime_error(message);
				}
			}

			auto inputTxIndex = dbHelper.txGetId(input.transactionHash);

			dbTx.inputs.back().value = dbTxOutput.value;
			dbTx.inputs.back().txRef.tx = inputTxIndex;

			bnSatoshiSecond += boost::multiprecision::uint128_t(dbTxOutput.value) * std::max(((int64_t) dbTx.getBestTimeStamp() - (int64_t) dbTxTimestamp), (int64_t) 0);

			memcpy(&dbTx.inputs.back().address, &dbTxOutput.address, sizeof(Address));

			//dbTx2.outputs[input.transactionIndex].txRefs.push_back(DbTxRef(transactionHash, j));

			//dbHelper.txSave(input.transactionHash, dbTx2);

			// Layout: 0x02, SourceTx, 0x02, SourceTxOutputIndex, output DbTxRef
			char searchKey[1 + sizeof(TxId) +1 + sizeof(DbTransactionOutput)];
			searchKey[0] = (uint8_t) DatabaseKeyHeader::Tx;
			//memcpy(&searchKey[1], input.transactionHash, sizeof(Hash256));
			*(uint32_t*) &searchKey[1] = swapByteOrder(inputTxIndex);
			searchKey[1 + sizeof(TxId)] = 0x02;
			auto& transactionOutput = *(DbTransactionOutput*) &searchKey[1 + sizeof(TxId) +1];
			transactionOutput.txOutIndex = input.transactionIndex;
			transactionOutput.txRef = DbTxRef(txIndex, j);
			batch.Put(leveldb::Slice((const char*) searchKey, sizeof(searchKey)), leveldb::Slice());
		}
	}

	dbTx.coinAgeDestroyed = (float) bnSatoshiSecond;

	return txIndex;
}