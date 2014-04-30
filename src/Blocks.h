#ifndef BLOCKS_H
#define BLOCKS_H

#include <unordered_map>
#include <map>
#include <vector>
#include <boost/thread/mutex.hpp>
#include "Hash256.h"
#include "llvm/ADT/SmallVector.h"
#include "varint.h"

/// Block type
enum class BlockType
{
	ProofOfWork = 0,				///< Proof of Work block (PoW)
	ProofOfStake = 1,				///< Proof of Stake block (PoS)
};

/// Memory representation of a block.
/// All mined blocks will currently be in memory for easier manipulations (it could later be loaded on demand from database)
struct BlockInfo
{
	uint32_t index;					///< Block index
	uint64_t height;				///< Height of this block
	uint32_t bits;					///< This is the representation of the target; the value which the hash of the block header must not exceed in order to min the next block
	uint64_t totalBits;				///< Sum of all bits on this chain, including current block
	Hash256 previous;				///< Hash of previous block
	llvm::SmallVector<Hash256, 1> nexts;	///< Hash of next blocks
	bool longestChain;				///< True if this block is on longest chain; false otherwise
	uint32_t timeStamp;				///< Timestamp of this block
	uint32_t blockLength;			///< Length of this block
	uint64_t output;				///< Sum of all coins outputs of this block
	float coinAgeDestroyed;			///< Coin-age destroyed by this block (in satoshi*sec)
	double totalCoins;				///< Total coins available at this block
	double totalCoinAge;			///< Total coin age at this block (sum of every UTXO amount multiplied by its duration since last transaction)
	uint32_t transactionCount;		///< Number of transactions in this block

	int64_t reward;					///< Reward of this block

	BlockType type;					///< Block type
	uint64_t staked;				///< Amount of coins staked (Proof of Stake block only)
	float stakeCoinAgeDestroyed;	///< Staked coin-age destroyed (Proof of Stake block only)
};

/// Maps block height to block hashes
extern std::unordered_multimap<uint32_t, Hash256> blockHeights;

/// Maps block hashes to block infos
extern std::map<Hash256, BlockInfo> blocks;

/// Maps block index to block hashes
extern std::vector<Hash256> blockIds;

/// Maps block height to block hash for main chain
extern std::vector<Hash256> currentChain;

/// Mutex for changing any of blockHeights, blocks, blockIds and currentChain
extern boost::mutex chainMutex;

#endif
