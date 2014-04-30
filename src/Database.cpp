#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include "Database.h"
#include "Serialization.h"
#include "leveldb/write_batch.h"
#include "lrucache.hpp"

int char2int(char input)
{
	if (input >= '0' && input <= '9')
		return input - '0';
	if (input >= 'A' && input <= 'F')
		return input - 'A' + 10;
	if (input >= 'a' && input <= 'f')
		return input - 'a' + 10;
	return -1;
}

char int2char(int input)
{
	if (input >= 0 && input <= 9)
		return input + '0';
	if (input >= 0xA && input <= 0xF)
		return input + 'a' - 0xA;
	return -1;
}

bool encodeHexString(uint8_t *data, uint32_t dataLength, const std::string& key, bool swapEndian)
{
	if (key.length() != dataLength * 2)
		return false;

	if (swapEndian)
	{
		for (int i = dataLength * 2 - 2; i >= 0; i -= 2)
		{
			int c1 = char2int(key[i]);
			int c2 = char2int(key[i + 1]);
			if (c1 == -1 || c2 == -1)
				return false;
			*data++ = c1 * 16 + c2;
		}
	}
	else
	{
		for (int i = 0; i < dataLength * 2; i += 2)
		{
			int c1 = char2int(key[i]);
			int c2 = char2int(key[i + 1]);
			if (c1 == -1 || c2 == -1)
				return false;
			*data++ = c1 * 16 + c2;
		}
	}

	return true;
}

bool encodeHexString(uint8_t *data, uint32_t dataLength, const char* key, bool swapEndian)
{
	if (swapEndian)
	{
		for (int i = dataLength * 2 - 2; i >= 0; i -= 2)
		{
			int c1 = char2int(key[i]);
			int c2 = char2int(key[i + 1]);
			if (c1 == -1 || c2 == -1)
				return false;
			*data++ = c1 * 16 + c2;
		}
	}
	else
	{
		for (int i = 0; i < dataLength * 2; i += 2)
		{
			int c1 = char2int(key[i]);
			int c2 = char2int(key[i + 1]);
			if (c1 == -1 || c2 == -1)
				return false;
			*data++ = c1 * 16 + c2;
		}
	}

	return true;
}

void decodeHexString(const uint8_t *data, uint32_t length, char* key, bool swapEndian)
{
	if (swapEndian)
	{
		for (int i = length - 1; i >= 0; --i)
		{
			uint8_t c = data[i];
			*key++ = int2char(c / 16);
			*key++ = int2char(c % 16);
		}
	}
	else
	{
		for (int i = 0; i < length; ++i)
		{
			uint8_t c = data[i];
			*key++ = int2char(c / 16);
			*key++ = int2char(c % 16);
		}
	}
	*key = '\0';
}

void decodeHexString(const uint8_t *data, uint32_t length, std::string& str, bool swapEndian)
{
	if (swapEndian)
	{
		for (int i = length - 1; i >= 0; --i)
		{
			uint8_t c = data[i];
			str.push_back(int2char(c / 16));
			str.push_back(int2char(c % 16));
		}
	}
	else
	{
		for (int i = 0; i < length; ++i)
		{
			uint8_t c = data[i];
			str.push_back(int2char(c / 16));
			str.push_back(int2char(c % 16));
		}
	}
}

TxId txLatestIndex = 0;
extern lru_cache<Hash256, TxId> txCache;
extern lru_cache<TxId, Hash256> txCache2;
extern boost::recursive_mutex cacheMutex;

void DatabaseHelper::txResetLastId(TxId id)
{
	txLatestIndex = id;
}

TxId DatabaseHelper::txGetOrCreateId(const Hash256& txHash)
{
	boost::lock_guard<boost::recursive_mutex> guard(cacheMutex);
	auto index = txGetId(txHash, true);
	if (index == 0xFFFFFFFF)
	{
		index = txLatestIndex++;

		DbTxHashToIdKey key(txHash);
		db->Put(leveldb::WriteOptions(), leveldb::Slice((const char*)&key, sizeof(key)), leveldb::Slice((const char*)&index, sizeof(index)));

		txCache.put(txHash, index);
		txCache2.put(index, txHash);
	}

	return index;
}

TxId DatabaseHelper::txSave(leveldb::WriteBatch& batch, const Hash256& txHash, const DbTransaction& dbTx)
{
	auto buffer = bufferTLS.get();
	if (buffer == NULL)
	{
		bufferTLS.reset(buffer = new std::string());
	}

	auto index = txGetOrCreateId(txHash);

	Serialize(*buffer, dbTx);

	char searchKey2[1 + sizeof(TxId)];
	searchKey2[0] = (uint8_t)DatabaseKeyHeader::Tx;
	*(TxId*) &searchKey2[1] = swapByteOrder(index);
	batch.Put(leveldb::Slice((const char*)searchKey2, sizeof(searchKey2)), *buffer);

	return index;
}

Hash256 DatabaseHelper::txGetHash(TxId txId)
{
	Hash256 txHash;

	boost::lock_guard<boost::recursive_mutex> guard(cacheMutex);
	if (txCache2.exists(txId))
	{
		txHash = txCache2.get(txId);
	}
	else
	{
		auto buffer = bufferTLS.get();
		if (buffer == NULL)
		{
			bufferTLS.reset(buffer = new std::string());
		}

		char searchKey[1 + sizeof(TxId)];
		searchKey[0] = (uint8_t)DatabaseKeyHeader::Tx;
		*((TxId*) &searchKey[1]) = swapByteOrder(txId);

		if (!db->Get(leveldb::ReadOptions(), leveldb::Slice((const char*)searchKey, sizeof(searchKey)), buffer).ok())
		{
			for (int i = 0; i < sizeof(txHash.value); ++i)
				txHash.value[i] = 0;
			return txHash;
		}

		txHash = *(Hash256*)buffer->c_str();

		txCache2.put(txId, txHash);
	}

	return txHash;
}

TxId DatabaseHelper::txGetId(const Hash256& txHash, bool allowInvalid)
{
	TxId txHashIndex;

	boost::lock_guard<boost::recursive_mutex> guard(cacheMutex);
	if (txCache.exists(txHash))
	{
		txHashIndex = txCache.get(txHash);
	}
	else
	{
		auto buffer = bufferTLS.get();
		if (buffer == NULL)
		{
			bufferTLS.reset(buffer = new std::string());
		}

		DbTxHashToIdKey searchKey(txHash);

		if (!db->Get(leveldb::ReadOptions(), leveldb::Slice((const char*)&searchKey, sizeof(searchKey)), buffer).ok())
		{
			if (!allowInvalid)
				assert("Unknown transaction hash => ID mapping");
			return 0xFFFFFFFF;
		}

		txHashIndex = *(TxId*) buffer->c_str();

		txCache.put(txHash, txHashIndex);
		txCache2.put(txHashIndex, txHash);
	}

	return txHashIndex;
}

TxId DatabaseHelper::txLoad(const Hash256& txHash, DbTransaction& dbTx, std::vector<DbTransactionOutput>* outputs, std::vector<DbTransactionBlock>* blocks)
{
	return txLoad(txGetId(txHash), dbTx, outputs, blocks);
}

TxId DatabaseHelper::txLoad(TxId txIndex, DbTransaction& dbTx, std::vector<DbTransactionOutput>* outputs, std::vector<DbTransactionBlock>* blocks)
{
	auto buffer = bufferTLS.get();
	if (buffer == NULL)
	{
		bufferTLS.reset(buffer = new std::string());
	}

	bool loadedTransaction = false;

	char searchKey2[1 + sizeof(TxId)];
	searchKey2[0] = (uint8_t)DatabaseKeyHeader::Tx;
	*((TxId*)&searchKey2[1]) = swapByteOrder(txIndex);
	auto searchKeySlice = leveldb::Slice((const char*)searchKey2, sizeof(searchKey2));

	if (outputs == NULL && blocks == NULL)
	{
		if (!db->Get(leveldb::ReadOptions(), searchKeySlice, buffer).ok())
			return 0;
	}
	else
	{
		// Slower path to get outputs and/or blocks using iterators
		auto it = std::unique_ptr<leveldb::Iterator>(db->NewIterator(leveldb::ReadOptions()));
		it->Seek(searchKeySlice);

		if (!it->Valid() || it->key() != searchKeySlice)
			return false;

		buffer->assign(it->value().data(), it->value().size());

		// Read outputs
		it->Next();
		for (; it->Valid(); it->Next())
		{
			auto key = it->key();
			if (key.size() < searchKeySlice.size()
				|| memcmp(key.data(), searchKeySlice.data(), searchKeySlice.size()) != 0)
				break;

			auto keyType = key.data()[1 + sizeof(TxId)];
			if (keyType == 0x02)
			{
				if (outputs != NULL)
				{
					auto& transactionOutput = *(DbTransactionOutput*)&key.data()[1 + sizeof(TxId) + 1];
					outputs->push_back(transactionOutput);
				}
			}
			else if (keyType == 0x03 && blocks != NULL)
			{
				if (blocks != NULL)
				{
					auto& transactionBlock = *(DbTransactionBlock*) &key.data()[1 + sizeof(TxId) + 1];
					blocks->push_back(transactionBlock);
				}
			}
			else
			{
				break;
			}
		}
	}

	Deserialize(*buffer, dbTx);
	return txIndex;
}

bool DatabaseHelper::blockLoad(uint32_t blockIndex, DbBlock& dbBlock)
{
	uint8_t hash2[1 + sizeof(uint32_t)];
	hash2[0] = (uint8_t) DatabaseKeyHeader::Block;
	*((uint32_t*) &hash2[1]) = swapByteOrder(blockIndex);

	std::string dbText;
	if (db->Get(leveldb::ReadOptions(), leveldb::Slice((const char*) hash2, sizeof(hash2)), &dbText).ok())
	{
		Deserialize(dbText, dbBlock);
		return true;
	}

	return false;
}

boost::thread_specific_ptr<std::string> DatabaseHelper::bufferTLS;
bool DbTransaction::transactionContainsTimestamp;

leveldb::DB *db;
DatabaseHelper dbHelper;
