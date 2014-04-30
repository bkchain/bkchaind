#include <cctype>
#define _WEBSOCKETPP_CPP11_STL_
#ifdef _MSC_VER
#define _WEBSOCKETPP_NO_CPP11_FUNCTIONAL_
#define _WEBSOCKETPP_NOEXCEPT_TOKEN_
#define _WEBSOCKETPP_CONSTEXPR_TOKEN_
#define _WEBSOCKETPP_NULLPTR_TOKEN_ 0
#endif
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <iostream>
#include "../json-rpc-cpp/src/jsonrpc/json/reader.h"

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

using websocketpp::lib::thread;
using websocketpp::lib::mutex;
using websocketpp::lib::unique_lock;
using websocketpp::lib::condition_variable;

/// Websocket server that will broadcast data to all connections.
/// @todo: Register to listen to specific address changes.
class BroadcastWebsocketServer {
public:
	BroadcastWebsocketServer() {
		// Initialize server
		m_server.init_asio();
		m_server.set_access_channels(websocketpp::log::alevel::connect | websocketpp::log::alevel::disconnect);

		// Register handlers for open, close and messages
		m_server.set_open_handler(bind(&BroadcastWebsocketServer::on_open, this, ::_1));
		m_server.set_close_handler(bind(&BroadcastWebsocketServer::on_close, this, ::_1));
		m_server.set_message_handler(bind(&BroadcastWebsocketServer::on_message, this, ::_1, ::_2));
	}

	void on_open(connection_hdl hdl) {
		std::lock_guard<std::mutex> lock(m_mutex);

		// Add connection
		m_connections.insert(hdl);
	}

	void on_close(connection_hdl hdl) {
		std::lock_guard<std::mutex> lock(m_mutex);

		// Remove connection
		m_connections.erase(hdl);

		// Remove from livetx
		m_livetx.erase(hdl);
	}

	void on_message(connection_hdl hdl, server::message_ptr msg) {
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& payload = msg->get_payload();
		Json::Value value;

		// Get what is being subscribed to
		reader.parse(payload, value, false);
		auto op = value["subscribe"].asString();

		if (op == "livetx")
		{
			// Subscribe to livetx
			m_livetx.insert(hdl);
		}
	}

	void broadcast_livetx(const char* message) {
		std::lock_guard<std::mutex> lock(m_mutex);

		// Broadcast message to everybody who subscribed to livetx
		for (auto it : m_livetx) {
			m_server.send(it, message, websocketpp::frame::opcode::text);
		}
	}

	void run(uint16_t port) {
		// Server loop
		while (!m_server.stopped())
		{
			m_server.listen(port);
			m_server.start_accept();
			try {
				m_server.run();
			}
			catch (const std::exception & e) {
				std::cout << "websocket exception: " << e.what() << std::endl;
			}
			catch (websocketpp::lib::error_code e) {
				std::cout << "websocket exception: " << e.message() << std::endl;
			}
			catch (...) {
				std::cout << "websocket exception : other exception" << std::endl;
			}
		}
	}

	void stop() {
		// Stop server explicitely
		m_server.stop();
	}
private:
	typedef std::set<connection_hdl, std::owner_less<connection_hdl>> con_list;

	Json::Reader reader;
	server m_server;
	con_list m_connections;
	con_list m_livetx;
	std::mutex m_mutex;
};
