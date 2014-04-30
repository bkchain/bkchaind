/*************************************************************************
 * libjson-rpc-cpp
 *************************************************************************
 * @file    httpclient.h
 * @date    02.01.2013
 * @author  Peter Spiess-Knafl <peter.knafl@gmail.com>
 * @license See attached LICENSE.txt
 ************************************************************************/

#ifndef HTTPCLIENTASIO_H_
#define HTTPCLIENTASIO_H_

#include "../json-rpc-cpp/src/jsonrpc/clientconnector.h"
#include "../json-rpc-cpp/src/jsonrpc/exception.h"
#include <boost/asio.hpp>
#include <mutex>
#undef SendMessage

namespace jsonrpc
{
    
    class HttpClientAsio : public AbstractClientConnector
    {
        public:
			HttpClientAsio(const std::string& host, int port, const std::string& rpcUser, const std::string& rpcPassword) throw (JsonRpcException);
			virtual ~HttpClientAsio();

            virtual void SendMessage(const std::string& message, std::string& result) throw (JsonRpcException);

        private:
			boost::asio::ip::tcp::socket sock;
			static boost::asio::io_service ios;
			boost::asio::ip::tcp::endpoint ep;

			std::string host, auth;
			int port;

			bool connected;

			std::mutex send_mutex;
    };

} /* namespace jsonrpc */
#endif /* HTTPCLIENTASIO_H_ */
