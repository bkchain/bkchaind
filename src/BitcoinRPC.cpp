#include "httpclientasio.h"
#include "BitcoinRPC.h"

RpcClient* rpcClient = NULL;

RpcClient::RpcClient(int rpcPort, const std::string& rpcUser, const std::string& rpcPassword)
	: jsonrpc::Client(new jsonrpc::HttpClientAsio("127.0.0.1", rpcPort, rpcUser, rpcPassword))
{}
