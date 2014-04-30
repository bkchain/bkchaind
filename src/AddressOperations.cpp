#include <memory>
#include <boost/thread/locks.hpp>
#include "leveldb/db.h"
#include "AddressOperations.h"
#include "Database.h"
#include "Blocks.h"
#include "Serialization.h"

bool getAddressOperation(const Address& address, AddressOperation& addressOperation, const Hash256& chainStart, int confirmations)
{
	// Are we part of main chain? (faster check)
	auto currentBlockHash = chainStart;
	BlockInfo currentBlock;

	{
		boost::lock_guard<boost::mutex> guard(chainMutex);
		currentBlock = blocks[currentBlockHash];
	}

	auto startHeight = currentBlock.height;

	// Key is : DatabaseKeyHeader::AddressOperation (0x03) + address (20 bytes) + 0x01 + blockhash (32 bytes) + txhash (32 bytes)
	char searchKey[1 + sizeof(Address) +1];
	searchKey[0] = (uint8_t)DatabaseKeyHeader::AddressOperation;
	*(Address*) &searchKey[1] = address;
	searchKey[1 + sizeof(Address)] = 0x01;
	auto it = std::unique_ptr<leveldb::Iterator>(db->NewIterator(leveldb::ReadOptions()));
	int index = 0;
	for (it->Seek(leveldb::Slice(searchKey, sizeof(searchKey))); it->Valid(); it->Next())
	{
		auto key = it->key();
		if (key.size() < sizeof(searchKey) || memcmp(key.data(), searchKey, sizeof(searchKey)) != 0)
			break;

		auto addrChainKey = (DbAddressOperationChainKey*) key.data();

		auto height = addrChainKey->getHeight();
		auto blockHash = addrChainKey->getBlockHash();

		// Skip (too recent)
		if (height > currentBlock.height)
			continue;

		// Go to appropriate block
		{
			boost::lock_guard<boost::mutex> guard(chainMutex);
			while (currentBlock.height > height)
			{
				// Simplest case: use main chain
				if (currentBlock.longestChain)
				{
					currentBlockHash = currentChain[height];
					currentBlock = blocks[currentBlockHash];
					break;
				}

				currentBlockHash = currentBlock.previous;
				currentBlock = blocks[currentBlockHash];
			}

			// Check if we have the right block
			if (blockIds[blockHash] != currentBlockHash)
				continue;

			// Check if there is enough confirmations
			if (height + confirmations > startHeight + 1)
				continue;
		}

		addressOperation.blockHeight = height;
		addressOperation.blockHash = blockHash;
		addressOperation.blockTxIndex = addrChainKey->getTransactionIndex();
		addressOperation.txHashIndex = addrChainKey->getTransaction();

		auto value = it->value();

		DbAddressOperationChain dbAddressOpChain;
		Deserialize(value.ToString(), dbAddressOpChain);

		addressOperation.total_input = dbAddressOpChain.total_input;
		addressOperation.total_output = dbAddressOpChain.total_output;
		addressOperation.txCount = dbAddressOpChain.txCount;
		addressOperation.transactionTime = dbAddressOpChain.transactionTime;

		addressOperation.input = 0;
		addressOperation.output = 0;

		return true;
	}

	memset(&addressOperation, 0, sizeof(AddressOperation));
	return false;
}

int getAddressOperations(const Address& address, int offset, int maxCount, std::deque<AddressOperation>& addressOperations, const Hash256& chainStart)
{
	// Are we part of main chain? (faster check)
	auto currentBlockHash = chainStart;
	BlockInfo currentBlock;

	{
		boost::lock_guard<boost::mutex> guard(chainMutex);
		currentBlock = blocks[currentBlockHash];
	}

	// Key is : DatabaseKeyHeader::AddressOperation (0x03) + address (20 bytes) + 0x01 + blockhash (32 bytes) + txhash (32 bytes)
	char searchKey[1 + sizeof(Address)+1];
	searchKey[0] = (uint8_t)DatabaseKeyHeader::AddressOperation;
	*(Address*)&searchKey[1] = address;
	searchKey[1 + sizeof(Address)] = 0x01;
	auto it = std::unique_ptr<leveldb::Iterator>(db->NewIterator(leveldb::ReadOptions()));
	int index = 0;
	for (it->Seek(leveldb::Slice(searchKey, sizeof(searchKey))); it->Valid(); it->Next())
	{
		auto key = it->key();
		if (key.size() < sizeof(searchKey) || memcmp(key.data(), searchKey, sizeof(searchKey)) != 0)
			break;

		auto addrChainKey = (DbAddressOperationChainKey*)key.data();

		auto height = addrChainKey->getHeight();
		auto blockHash = addrChainKey->getBlockHash();

		// Skip (too recent)
		if (height > currentBlock.height)
			continue;

		// Go to appropriate block
		{
			boost::lock_guard<boost::mutex> guard(chainMutex);
			while (currentBlock.height > height)
			{
				// Simplest case: use main chain
				if (currentBlock.longestChain)
				{
					currentBlockHash = currentChain[height];
					currentBlock = blocks[currentBlockHash];
					break;
				}

				currentBlockHash = currentBlock.previous;
				currentBlock = blocks[currentBlockHash];
			}

			// Check if we have the right block
			if (blockIds[blockHash] != currentBlockHash)
				continue;
		}

		auto currentIndex = index++;
		if (currentIndex < offset)
			continue;

		AddressOperation addressOperationsLast;

		addressOperationsLast.blockHeight = height;
		addressOperationsLast.blockHash = blockHash;
		addressOperationsLast.blockTxIndex = addrChainKey->getTransactionIndex();
		addressOperationsLast.txHashIndex = addrChainKey->getTransaction();

		auto value = it->value();

		DbAddressOperationChain dbAddressOpChain;
		Deserialize(value.ToString(), dbAddressOpChain);

		addressOperationsLast.total_input = dbAddressOpChain.total_input;
		addressOperationsLast.total_output = dbAddressOpChain.total_output;
		addressOperationsLast.txCount = dbAddressOpChain.txCount;
		addressOperationsLast.transactionTime = dbAddressOpChain.transactionTime;
		
		addressOperations.push_back(addressOperationsLast);

		// Allow one more item to have proper input/output differences
		if (index >= offset + maxCount + 1)
			break;
	}
	it.reset();

	for (auto addressOperation = addressOperations.begin(); addressOperation != addressOperations.end(); ++addressOperation)
	{

		auto nextOperation = addressOperation + 1;
		if (nextOperation != addressOperations.end())
		{
			addressOperation->input = addressOperation->total_input - nextOperation->total_input;
			addressOperation->output = addressOperation->total_output - nextOperation->total_output;
		}
		else
		{
			addressOperation->input = addressOperation->total_input;
			addressOperation->output = addressOperation->total_output;
		}
	}

	// If we got one more item, delete it
	if (index == offset + maxCount + 1)
	{
		addressOperations.pop_back();
	}

	return addressOperations.size();
}
