#include "Blocks.h"

std::unordered_multimap<uint32_t, Hash256> blockHeights;
std::map<Hash256, BlockInfo> blocks;
std::vector<Hash256> blockIds;
std::vector<Hash256> currentChain;
boost::mutex chainMutex;
