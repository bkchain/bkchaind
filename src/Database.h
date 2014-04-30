// This file contains database object model

#ifndef DATABASE_H
#define DATABASE_H

#include <vector>
#include <boost/thread/tss.hpp>
#include "leveldb/db.h"
#include "llvm/ADT/SmallVector.h"
#include "Address.h"
#include "Hash256.h"
#include "varint.h"
#include "../blockchain/BlockChain.h"

enum class DatabaseKeyHeader : unsigned char
{
	Block = 0x01,
	TxHashToId = 0x02,
	AddressOperation = 0x03,
	AddressUTXO = 0x05,
	Tx = 0x08,
};

typedef uint64_t TxId;

#ifdef _MSC_VER
#  define PACKED_STRUCT( __Declaration__ ) __pragma( pack(push, 1) ) struct __Declaration__
#  define END_PACKED_STRUCT  __pragma( pack(pop) )
#else
#  define PACKED_STRUCT( __Declaration__ ) struct __attribute__((__packed__)) __Declaration__
#  define END_PACKED_STRUCT
#endif

// Some useful byte swapping operators so that database byte ordering work
inline uint32_t swapByteOrder(uint32_t ui)
{
	return (ui >> 24) |
		((ui << 8) & 0x00FF0000) |
		((ui >> 8) & 0x0000FF00) |
		(ui << 24);
}

inline uint64_t swapByteOrder(uint64_t ui)
{
	uint64_t a = swapByteOrder((uint32_t)ui);
	uint32_t b = swapByteOrder((uint32_t)(ui >> 32));
	return (a << 32) | b;
}

/// Encode/decode string to byte array
bool encodeHexString(uint8_t *data, uint32_t dataLength, const std::string& key, bool swapEndian = false);
bool encodeHexString(uint8_t *data, uint32_t dataLength, const char* key, bool swapEndian = false);
void decodeHexString(const uint8_t *data, uint32_t length, char* key, bool swapEndian = false);
void decodeHexString(const uint8_t *data, uint32_t length, std::string& str, bool swapEndian = false);

PACKED_STRUCT(DbTxHashToIdKey)
{
	DbTxHashToIdKey(Hash256 txHash) : header(DatabaseKeyHeader::TxHashToId), txHash(txHash) {}

	DatabaseKeyHeader header;
	Hash256 txHash;
};
END_PACKED_STRUCT

/// Database representation of a reference to a transaction input or output.
struct DbTxRef
{
	DbTxRef() {}

	DbTxRef(TxId tx, int32_t index) : tx(tx), index(index) {}

	TxId tx;
	//Hash256 txHash;
	uint32_t index;

	bool operator==(const DbTxRef& txBlock) const
	{
		return tx == txBlock.tx
			&& index == txBlock.index;
	}

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & tx;
		ar & make_varint(index);
	}
};

/// Database representation of an address (currently unused).
struct DbAddress
{
	Address address;
	uint8_t hash160[20];
	uint8_t publicKey[32];

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & address;
		ar & hash160;
		ar & publicKey;
	}
};

/// Key for an address operation.
/// See explanations in DbAddressOperationChain.
PACKED_STRUCT(DbAddressOperationChainKey)
{
	DatabaseKeyHeader header;
	Address address;
	uint8_t addressOpType;
	uint64_t invBlockHeight;
	uint32_t invBlockTxIndex;
	uint32_t blockId;
	TxId txId;

	DbAddressOperationChainKey(const Address& address, uint64_t blockHeight, uint32_t blockTxIndex, uint32_t blockHash, TxId tx)
		: header(DatabaseKeyHeader::AddressOperation), address(address), addressOpType(0x01)
	{
		invBlockHeight = swapByteOrder(std::numeric_limits<uint64_t>::max() - blockHeight);
		invBlockTxIndex = swapByteOrder(std::numeric_limits<uint32_t>::max() - blockTxIndex);
		blockId = blockHash;
		txId = tx;
	}

	uint64_t getHeight()
	{
		return swapByteOrder(std::numeric_limits<uint64_t>::max() - invBlockHeight);
	}

	uint32_t getTransactionIndex()
	{
		return swapByteOrder(std::numeric_limits<uint32_t>::max() - invBlockTxIndex);
	}

	uint32_t getBlockHash()
	{
		return blockId;
	}

	TxId getTransaction()
	{
		return txId;
	}
};
END_PACKED_STRUCT

/// Address operation chain stores information about operation on a given address.
/// The way keys are arranged mean that simply iterating data in order gives us all the necessary information.
/// Data is already aggregated, so that we can display most address info using only last two address operations
/// (no need to process all of them to know total input, output, balance, number of tx, etc...)
///
/// key: DatabaseKeyHeader::AddressOperation (0x03) + [address] + 0x01 + [maxint - blockindex] + [maxint - txindex] + [block] + [tx]
///                                                                      ^ ordering              ^ ordering
///
/// Data is already aggregated (input/output can be extracted by doing current.total_XXX - previous.total_XXX)
struct DbAddressOperationChain
{
	DbAddressOperationChain() : total_input(0), total_output(0), txCount(0), transactionTime(0) {}

	/// Total input of this address at this point.
	uint64_t total_input;
	/// Total output of this address at this point.
	uint64_t total_output;

	/// Total number of transaction of this address at this point.
	uint32_t txCount;
	/// Transaction time at this point.
	uint32_t transactionTime;

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & total_input;
		ar & total_output;
		ar & txCount;
		ar & transactionTime;
	}
};

PACKED_STRUCT(DbAddressUTXOKey)
{
	DbAddressUTXOKey(const Address& address, const DbTxRef& txRef)
		: header(DatabaseKeyHeader::AddressUTXO), address(address), txRef(txRef) {}

	DatabaseKeyHeader header;
	Address address;
	DbTxRef txRef;
};
END_PACKED_STRUCT

PACKED_STRUCT(DbAddressUTXO)
{
	DbAddressUTXO() {}
	DbAddressUTXO(uint64_t value, uint32_t height) : value(value), height(height) {}

	uint64_t value;
	uint32_t height;
};
END_PACKED_STRUCT

/// Database representation of a transaction input.
struct DbTxInput
{
	DbTxInput() {}

	DbTxInput(BlockChain::BlockInput* input)
	{
		responseScript.append(input->responseScript, input->responseScript + input->responseScriptLength);
		txRef.index = input->transactionIndex;
	}

	uint64_t value;
	DbTxRef txRef;
	Address address;
	llvm::SmallVector<uint8_t, 512> responseScript;

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & value;
		ar & txRef;
		ar & address;
		ar & responseScript;
	}
};

/// Database representation of a transaction unspent output (UTXO).
struct DbTxUnspentOutput
{
	DbTxUnspentOutput() {}

	uint64_t value;
	uint32_t timeStamp;
	Address address;
};

/// Database representation of a transaction output.
struct DbTxOutput
{
	DbTxOutput() {}

	DbTxOutput(BlockChain::BlockOutput* output)
	{
		challengeScript.append(output->challengeScript, output->challengeScript + output->challengeScriptLength);
		value = output->value;
	}


	llvm::SmallVector<uint8_t, 512> challengeScript;
	uint64_t value;
	Address address;

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & value;
		ar & address;
		ar & challengeScript;
	}
};

/// Database representation of a transaction output info added on original transaction
/// (to trace from a given transaction to next transactions as they are added).
struct DbTransactionOutput
{
	uint32_t txOutIndex;
	DbTxRef txRef;
};

/// Database representation of additional info for block information of a transaction.
struct DbTransactionBlock
{
	uint32_t blockHash;
	uint32_t blockTxIndex;

	DbTransactionBlock() {}

	DbTransactionBlock(uint32_t blockHash, uint32_t blockTxIndex)
		: blockHash(blockHash), blockTxIndex(blockTxIndex)
	{}

	bool operator==(const DbTransactionBlock& txBlock) const
	{
		return blockHash == txBlock.blockHash
			&& blockTxIndex == txBlock.blockTxIndex;
	}

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & blockHash;
		ar & make_varint(blockTxIndex);
	}
};

/// Database representation of a transaction.
struct DbTransaction
{
public:
	void clear()
	{
		inputs.clear();
		outputs.clear();
		transactionLength = 0;
		transactionVersionNumber = 0;
		timeStamp = 0;
	}

	Hash256 txHash;
	//uint64_t input;
	//uint64_t output;
	uint32_t timeStamp; // Only PPC
	uint32_t receivedTime;
	float coinAgeDestroyed;
	uint32_t transactionLength;
	uint32_t transactionVersionNumber;
	llvm::SmallVector<DbTxInput, 128>   inputs;
	llvm::SmallVector<DbTxOutput, 128>  outputs;

	uint32_t getBestTimeStamp()
	{
		return (transactionContainsTimestamp) ? timeStamp : receivedTime;
	}

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & txHash;
		//ar & blocks;
		ar & make_varint(transactionLength);
		ar & make_varint(transactionVersionNumber);
		if (transactionContainsTimestamp)
			ar & timeStamp;
		ar & receivedTime;
		ar & coinAgeDestroyed;
		ar & inputs;
		ar & outputs;
	}

	static bool transactionContainsTimestamp;
};

/// Database representation of a block.
class DbBlock
{
public:
	Hash256			hash;						///< Block hash
	uint32_t		previous;					///< Previous block index.
	Hash256			merkleRoot;					///< Merkle root.
	uint32_t		blockLength;				///< Length of this block
	uint32_t		blockFormatVersion;			///< Block format version
	uint32_t		timeStamp;					///< Block timestamp in UNIX epoch time
	uint32_t		bits;						///< This is the representation of the target; the value which the hash of the block header must not exceed in order to min the next block
	uint32_t		nonce;						///< This is a random number generated during the mining process
	llvm::SmallVector<uint32_t, 1024> transactions;	///< Transactions contained in this block
	uint64_t		height;						///< Height of this block
	uint64_t		input;						///< Sum of all coins inputs of this block
	uint64_t		output;						///< Sum of all coins outputs of this block
	int64_t			reward;						///< Reward of this block
	float			coinAgeDestroyed;			///< Coin-age destroyed by this block (in satoshi*sec)
	double			totalCoins;					///< Total coins available at this block
	double			totalCoinAge;				///< Total coin age at this block (sum of every UTXO amount multiplied by its duration since last transaction)

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & hash;
		ar & previous;
		ar & merkleRoot;
		ar & make_varint(blockLength);
		ar & make_varint(blockFormatVersion);
		ar & timeStamp;
		ar & bits;
		ar & nonce;
		ar & transactions;
		ar & make_varint(height);
		ar & input;
		ar & output;
		ar & reward;
		ar & coinAgeDestroyed;
		ar & totalCoins;
		ar & totalCoinAge;
	}
};

/// Various helper functions for accessing database.
class DatabaseHelper
{
public:
	leveldb::DB *db;

	void txResetLastId(TxId id);

	/// Gets or create a transaction id for a given transaction hash.
	TxId txGetOrCreateId(const Hash256& txHash);

	/// Gets transaction id for a given transaction hash.
	TxId txGetId(const Hash256& txHash, bool allowInvalid = false);

	/// Gets or create a transaction hash for a given transaction id.
	Hash256 txGetHash(TxId txId);

	/// Load a transaction from its id.
	/// If non-null, outputs (links to output transactions) and blocks (which blocks this transaction belongs too) will be fetched.
	/// This data is accessible right after the transaction while iterating on database keys.
	TxId txLoad(TxId txIndex, DbTransaction& dbTx, std::vector<DbTransactionOutput>* outputs, std::vector<DbTransactionBlock>* blocks);

	/// Load a transaction from its hash.
	/// If non-null, outputs (links to output transactions) and blocks (which blocks this transaction belongs too) will be fetched.
	/// This data is accessible right after the transaction while iterating on database keys.
	TxId txLoad(const Hash256& txHash, DbTransaction& dbTx, std::vector<DbTransactionOutput>* outputs, std::vector<DbTransactionBlock>* blocks);

	/// Save a transaction to a database WriteBatch operation.
	TxId txSave(leveldb::WriteBatch& batch, const Hash256& txHash, const DbTransaction& dbTx);

	bool blockLoad(uint32_t blockIndex, DbBlock& dbBlock);
private:
	static boost::thread_specific_ptr<std::string> bufferTLS;
};

extern leveldb::DB *db;
extern DatabaseHelper dbHelper;

#endif
