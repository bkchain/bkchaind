#include <string>
#include "../json-rpc-cpp/src/jsonrpc/client.h"

class RpcClient : public jsonrpc::Client
{
public:
	RpcClient(int rpcPort, const std::string& rpcUser, const std::string& rpcPassword);
};

extern RpcClient* rpcClient;