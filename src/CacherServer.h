#ifndef CACHERSERVER_H
#define CACHERSERVER_H

#include <stdint.h>
#include "abstractcacherserver.h"

class Hash256;

class CacherServer : public AbstractCacherServer
{
public:
	CacherServer(int port, uint8_t pubkeyAddress)
		: AbstractCacherServer(new jsonrpc::HttpServer(port)), pubkeyAddress(pubkeyAddress)
	{}

	void getblock(const Hash256& hash, Json::Value& response);
	void getrawtx(const Hash256& hash, Json::Value& response);

	virtual Json::Value getaddress(const std::string& address);
	virtual Json::Value getblock(const std::string& hash);
	virtual Json::Value getblockhash(const int& height);
	virtual Json::Value getblockheight(const int& height);
	virtual Json::Value getblocks(const int& page);
	virtual Json::Value getlatesttransactions();
	virtual Json::Value getrawtx(const std::string& hash);

	// Public API

	// tx/hash/$txhash
	virtual Json::Value tx_hash(const std::string& hash);

	// tx/id/$txindex
	virtual Json::Value tx_index(const int& index);

	// tx/push
	virtual void tx_pushI(const Json::Value& request, Json::Value& response);

	// block/hash/$blockhash
	virtual Json::Value block_hash(const std::string& hash);

	// block/index/$blockindex
	virtual Json::Value block_index(const int& index);

	// TODO: block/last

	// block/height/$blockheight
	virtual Json::Value block_height(const int& index);

	// address/balance/$address
	virtual Json::Value address_balance(const std::string& address, const int& confirmations);

	// address/unspent/$address
	virtual Json::Value address_unspent(const std::string& address, const int& confirmations);

private:
	uint8_t pubkeyAddress;
};

struct BlockInfo;
struct Hash256;
void convertBlock(Json::Value& block, const Hash256& hash, const BlockInfo& blockInfo);

#endif
