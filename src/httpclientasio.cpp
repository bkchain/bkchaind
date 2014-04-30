#include "httpclientasio.h"
#include <string>
#include <string.h>
#include <cstdlib>

#include <boost/lexical_cast.hpp>

#include <iostream>

using namespace std;
using namespace boost::asio::ip;

namespace jsonrpc
{
	inline const std::string& base64_chars()
	{
		static const std::string m_base64_chars =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz"
			"0123456789+/";
		return m_base64_chars;
	}

	static inline bool is_base64(unsigned char c) {
		return (isalnum(c) || (c == '_') || (c == '-'));
	}

	inline std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {

		std::string ret;
		int i = 0;
		int j = 0;
		unsigned char char_array_3[3];
		unsigned char char_array_4[4];

		while (in_len--) {
			char_array_3[i++] = *(bytes_to_encode++);
			if (i == 3) {
				char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
				char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
				char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
				char_array_4[3] = char_array_3[2] & 0x3f;

				for (i = 0; (i <4); i++)
					ret += base64_chars()[char_array_4[i]];
				i = 0;
			}
		}

		if (i)
		{
			for (j = i; j < 3; j++)
				char_array_3[j] = '\0';

			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (j = 0; (j < i + 1); j++)
				ret += base64_chars()[char_array_4[j]];

			while ((i++ < 3))
				ret += '=';

		}

		return ret;

	}


	inline std::string base64_decode(std::string const& encoded_string) {


		int in_len = encoded_string.size();
		int i = 0;
		int j = 0;
		int in_ = 0;
		unsigned char char_array_4[4], char_array_3[3];
		std::string ret;

		while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
			char_array_4[i++] = encoded_string[in_]; in_++;
			if (i == 4) {
				for (i = 0; i <4; i++)
					char_array_4[i] = base64_chars().find(char_array_4[i]);

				char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
				char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
				char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

				for (i = 0; (i < 3); i++)
					ret += char_array_3[i];
				i = 0;
			}
		}

		if (i) {
			for (j = i; j <4; j++)
				char_array_4[j] = 0;

			for (j = 0; j <4; j++)
				char_array_4[j] = base64_chars().find(char_array_4[j]);

			char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
			char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
			char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

			for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
		}

		return ret;
	}


	boost::asio::io_service HttpClientAsio::ios;

	HttpClientAsio::HttpClientAsio(const std::string& host, int port, const std::string& rpcUser, const std::string& rpcPassword) throw(JsonRpcException)
		: AbstractClientConnector(), sock(ios), host(host), port(port)
    {
		std::string pre_encode = rpcUser + ":" + rpcPassword;
		auth = base64_encode((const unsigned char*)pre_encode.c_str(), pre_encode.size());
    }
    
	HttpClientAsio::~HttpClientAsio()
    {
    }

	void HttpClientAsio::SendMessage(const std::string& message, std::string& result) throw (JsonRpcException)
    {
		std::lock_guard<std::mutex> lock(send_mutex);

		// Try-catch that will close the socket in case of socket error
		try
		{
			if (!sock.is_open())
			{
				if (ep.port() == 0)
				{
					tcp::resolver resolver(ios);
					tcp::resolver::query q(host, std::to_string(port));
					tcp::resolver::iterator epi = resolver.resolve(q);

					auto it = boost::asio::connect(sock, epi);
					if (it != tcp::resolver::iterator())
						ep = *it;
				}
				else
				{
					sock.connect(ep);
				}
			}

			boost::asio::streambuf request;
			std::ostream request_stream(&request);

			request_stream << "POST / HTTP/1.1\r\n"
				<< "User-Agent: bitcoin-json-rpc/v0.8.6\r\n"
				<< "Host: 127.0.0.1\r\n"
				<< "Content-Type: application/json\r\n"
				<< "Content-Length: " << message.size() << "\r\n"
				<< "Connection: keep-alive\r\n"
				<< "Accept: application/json\r\n"
				<< "Authorization: Basic " << auth << "\r\n\r\n";


			request_stream << message;

			// Send the request.
			boost::asio::write(sock, request);

			// Read the response status line. The response streambuf will automatically
			// grow to accommodate the entire line. The growth may be limited by passing
			// a maximum size to the streambuf constructor.
			boost::asio::streambuf response;
			boost::asio::read_until(sock, response, "\r\n");

			std::istream response_stream(&response);
			std::string http_version;
			response_stream >> http_version;
			unsigned int status_code;
			response_stream >> status_code;
			std::string status_message;
			std::getline(response_stream, status_message);
			if (!response_stream || http_version.substr(0, 5) != "HTTP/" || status_code != 200)
			{
				sock.close();
				throw JsonRpcException(Errors::ERROR_CLIENT_CONNECTOR);
			}

			// Read the response headers, which are terminated by a blank line.
			boost::asio::read_until(sock, response, "\r\n\r\n");

			// Process the response headers.
			std::string header;
			int64_t length = -1;
			bool keepAlive = false;
			while (std::getline(response_stream, header) && header != "\r")
			{
				if (strncmp(header.c_str(), "Content-Length: ", strlen("Content-Length: ")) == 0)
				{
					auto semiColon = header.find(": ");
					length = boost::lexical_cast<std::string::size_type>(header.substr(semiColon + 2, header.length() - semiColon - 3));
				}
				else if (strncmp(header.c_str(), "Connection: ", strlen("Connection: ")) == 0)
				{
					if (header.find("keep-alive") != std::string::npos)
					{
						keepAlive = true;
					}
				}
			}

			// Write whatever content we already have to output.
			stringstream result_stream;
			auto written = response.size();
			if (response.size() > 0)
				result_stream << &response;

			boost::system::error_code error;
			if (length != -1)
			{
				auto currentPos = response_stream.tellg();
				boost::asio::read(sock, response,
					boost::asio::transfer_at_least(length - written), error);
				result_stream << &response;
			}
			else
			{

				// Write whatever content we already have to output.
				while (boost::asio::read(sock, response,
					boost::asio::transfer_at_least(1), error))
					result_stream << &response;
				if (error != boost::asio::error::eof)
					throw boost::system::system_error(error);
			}
			result = result_stream.str();

			if (!keepAlive)
			{
				sock.close();
			}
		}
		catch (boost::system::system_error& e)
		{
			sock.close();
			throw e;
		}
	}

} /* namespace jsonrpc */
