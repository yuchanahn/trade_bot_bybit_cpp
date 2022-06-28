#pragma once
#include <cpprest/ws_client.h>
#include "yc_json.hpp"
#include "bybit_api.hpp"
#include "Encryption.hpp"

#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <thread>

using namespace web;
using namespace web::websockets::client;

websocket_client position_clnt;
websocket_client price_clnt;

struct price_t {
	double buy;
	double sell;

	double get_open(std::string side) { return (side == "Buy") ? sell : buy; }
	double get_close(std::string side) { return (side == "Buy") ? buy : sell; }
};
price_t current_price;
bool is_price_ready = false;

static std::map<std::string, api::position_t> updated_pos;

static std::map<std::string, std::function<void(std::string)>> topic_action;


std::thread* position_th = nullptr;
std::thread* price_th = nullptr;
std::mutex m;

#define TESTNET false

void send_ping(websocket_client& ws) {
	websocket_outgoing_message msg;
	msg.set_utf8_message("{\"op\":\"ping\"}");
	ws.send(msg).wait();

}

double get_t(std::chrono::system_clock::time_point& start_time) {
	return ((std::chrono::duration<float>)(std::chrono::system_clock::now() - start_time)).count();
}

template <typename F>
void start_web_socket(F position_callback)
{
	std::cout << "Web Socket Start!\n";
	auto url1 = U("wss://stream.bybit.com/realtime_public");
	auto url2 = U("wss://stream.bybit.com/realtime_private");
	if (TESTNET) {
		url1 = U("wss://stream-testnet.bybit.com/realtime_public");
		url2 = U("wss://stream-testnet.bybit.com/realtime_private");
	}
	topic_action["orderBookL2_25.BTCUSDT"] = [](std::string body) {
		auto j = yc_json::parse(body);
		bool updated = false;
		if (j.data.contains("order_book")) {
			auto ob = j["data"]["order_book"];
			auto cnt = ob.count();
			for (int i = 0; i < cnt; i++)
				api::orderbook[std::stod(ob[i]["price"].to_string())] = 0;
			updated = true;
		}
		else {
			if (j.data.contains("data")) {
				if (j["data"].to_string().contains("delete")) {
					auto d = j["data"]["delete"];
					auto cnt = d.count();
					for (int i = 0; i < cnt; i++)
						api::orderbook.erase(std::stod(d[i]["price"].to_string()));
					updated = true;
				}
				if (j["data"].to_string().contains("update")) {
				}
				if (j["data"].to_string().contains("insert")) {
					auto d = j["data"]["insert"];
					auto cnt = d.count();
					for (int i = 0; i < cnt; i++)
						api::orderbook[std::stod(d[i]["price"].to_string())] = 0;
					updated = true;
				}
			}
		}
		int os = api::orderbook.size() / 2;
		int jj = 0;
		double prices[2] = { 0.0 , 0.0 };
		for (auto& i : api::orderbook) {
			if (jj == os) prices[0] = i.first;
			if (jj == (os - 1)) prices[1] = i.first;
			jj++;
		}
		if (updated && (prices[0] != 0.0)) {
			current_price = price_t{ prices[0], prices[1] };
			is_price_ready = true;
		}
	};
	topic_action["position"] = [](std::string body) {
			auto position_data_j = yc_json::parse(body)["data"];
			auto cnt = yc_json::parse(body)["data"].count();
			for (int i = 0; i < cnt; i++) {
				api::position_t mp;
				mp.size = std::stod(position_data_j[i]["size"].to_string());
				mp.side = position_data_j[i]["side"].to_string();
				mp.entry_price = std::stod(position_data_j[i]["entry_price"].to_string());
				updated_pos[mp.side] = mp;
			}
	};
	
	price_clnt.connect(url1).then([&](pplx::task<void> t) {
		printf("connect!\n");
		websocket_outgoing_message msg;
		msg.set_utf8_message("{\"op\":\"subscribe\",\"args\":[\"orderBookL2_25.BTCUSDT\"]}");
		price_clnt.send(msg).wait();
		price_th = new std::thread([] {
			std::atomic_bool next = true;
			std::chrono::system_clock::time_point tp = std::chrono::system_clock::now();
			while (1) {
				if (get_t(tp) >= 29.f) {
					tp = std::chrono::system_clock::now();
					send_ping(price_clnt);
				}
				if (next) {
					next = false;
					try
					{
						price_clnt.receive().then([&next](websocket_incoming_message msg) {
							try
							{
								auto body = msg.extract_string().get();
								if (!body.contains("topic"))  printf("%s", "");
								else						  topic_action[yc_json::parse(body)["topic"].to_string()](body);
								next = true;
							}
							catch (const std::exception& e)
							{
								std::cout << e.what() << std::endl;
							}
							});
					}
					catch (const std::exception& e)
					{
						std::cout << e.what() << std::endl;
					}

				} else {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			});
		});
	position_clnt.connect(url2).then([&](pplx::task<void> t) {
		printf("auth, start!\n");
		websocket_outgoing_message msg;
		auto time_text = yc_json::split(std::to_string(std::round(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count())), '.')[0] + "1000";
		msg.set_utf8_message(std::format("{{\"op\":\"auth\",\"args\":[\"{}\",\"{}\",\"{}\"]}}", api::api_key, time_text, GetSignature("GET/realtime" + time_text, api::secret_key)));
		position_clnt.send(msg).wait();
		msg.set_utf8_message("{\"op\":\"subscribe\",\"args\":[\"position\"]}");
		position_clnt.send(msg).wait();

		position_th = new std::thread([position_callback] {
			std::atomic_bool next = true;
			std::chrono::system_clock::time_point tp = std::chrono::system_clock::now();
			while (1) {
				if (get_t(tp) >= 29.f) {
					tp = std::chrono::system_clock::now();
					send_ping(position_clnt);
				}
				if (next) {
					next = false;
					
					try {
						position_clnt.receive().then([&next, position_callback](websocket_incoming_message msg) {
							try
							{
								auto body = msg.extract_string().get();
								if (!body.contains("topic"))  printf("%s", "");
								else {
									topic_action[yc_json::parse(body)["topic"].to_string()](body);
									position_callback();
								}
								next = true;
							} catch (const std::exception& e) {
								std::cout << e.what() << std::endl;
							}
							});
					} catch (const std::exception& e) {
						std::cout << e.what() << std::endl;
					}


				} else {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			});
		});
}