#pragma once
#include<iostream>
#include<cpprest/ws_client.h>
#include "yc_json.hpp"
#include "bybit_api.hpp"

using namespace web;
using namespace web::websockets::client;

websocket_client client;


void recvf(std::function<void(double, double)>* f) {
	client.receive().then([f](websocket_incoming_message msg) {
		return msg.extract_string();
		}).then([f](std::string body) {
			auto j = yc_json::parse(body);

			if (j.data.contains("order_book")) {
				auto ob = j["data"]["order_book"];
				auto cnt = ob.count();
				for (int i = 0; i < cnt; i++) 
					api::orderbook[std::stod(ob[i]["price"].to_string())] = 0;
			} else {
				if (j["data"].to_string().contains("delete")) {
					auto d = j["data"]["delete"];
					auto cnt = d.count();
					for (int i = 0; i < cnt; i++)
						api::orderbook.erase(std::stod(d[i]["price"].to_string()));
				}
				if (j["data"].to_string().contains("update")) {
				}
				if (j["data"].to_string().contains("insert")) {
					auto d = j["data"]["insert"];
					auto cnt = d.count();
					for (int i = 0; i < cnt; i++)
						api::orderbook[std::stod(d[i]["price"].to_string())] = 0;
				}
			}
			int os = api::orderbook.size() / 2;
			int jj = 0;
			double prices[2];
			for (auto& i : api::orderbook) {
				if (jj == os) prices[0] = i.first;
				if (jj == (os - 1)) prices[1] = i.first;
				jj++;
			}
			(* f)(prices[0], prices[1]);
			recvf(f);
			});
}

void start_web_socket(std::function<void(double, double)>* f)
{
	std::cout << "Web Socket Start!";
	auto url = U("wss://stream.bybit.com/realtime_public");
	client.connect(url).then([&, f](pplx::task<void> t) {
			printf("connect!\n");

			websocket_outgoing_message msg;
			msg.set_utf8_message("{\"op\":\"subscribe\",\"args\":[\"orderBookL2_25.BTCUSDT\"]}");
			auto send_task = client.send(msg).then([](pplx::task<void> t)
				{
					try
					{
						t.get();
					}
					catch (const websocket_exception& ex)
					{
						std::cout << ex.what();
					}
				});
			send_task.wait();
			recvf(f);
		});

	while (1) Sleep(1000);
}