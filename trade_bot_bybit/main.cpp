#pragma warning(disable : 4996)

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.hpp"
#include "Encryption.hpp"
#include "yc_json.hpp"
#include <format>
#include <ranges>
#include <iostream>
#include <map>
#include <vector>
#include <thread>
#include "bybit_websocket.hpp"
#include "bybit_api.hpp"


static double get_qty(double margin, double price, int leverage)
{
	double r = (margin * 0.85 / price * leverage);
	return (r < 0.001) ? 0.001 : (floor(r * 1000) / 1000);
}
static double set_tprice(double p) { return floor(p) + (0.5 * ((p - floor(p)) >= 0.5)); }
static double stov(std::string side) { return (side == "Buy") ? 1 : -1; }

struct position_type_b {
	std::string side;
	double open_price;
	double size;

	double StopPNL;
	double AMPNL;
	double AMStopPNL;
	double leverage;

	price_t prices;

	double PNL;
	bool has_pos;
	bool log;
	int stack = 0;
	double a = 2.0;

	double open_target_price;
	std::string order_id_open = "";
	double close_target_price;
	std::string order_id_close = "";

	bool stack_end = false;

	double get_fee() { return (open_price * size * 0.0006) + (prices.get_close(side) * size * 0.0006); }

	void update_pnl() {
		double close_price = prices.get_close(side);
		double per = ((double)1 - (open_price / close_price)) * leverage * stov(side);
		double margin = open_price * size / leverage;
		double margin2 = margin + (margin * per) - get_fee();
		PNL = (1 - (margin / margin2)) * 100;
	}

	void update_close_order(double target_pnl) {
		if(order_id_close != "") api::cancel_order(order_id_close);
		close_target_price = set_tprice(open_price + (open_price * (target_pnl / 100 / leverage)) * stov(side));
		order_id_close = api::close_position_lim(side, size, close_target_price);
	}

	void update_open_order() {
		open_target_price = set_tprice(open_price + (open_price * (get_ampnl() / 100 / leverage)) * stov(side));
		order_id_open = api::open_position_lim(side, size, open_target_price);
	}

	void update_position(api::position_t new_pos, bool update_pos = false) {
		printf("[%s]update position!\n", side.c_str());
		if(!update_pos) if (new_pos.size == size) return;
		if (new_pos.size == 0) {
			has_pos = false;
			size = new_pos.size;
			api::cancel_order(order_id_open);
			order_id_close = "";
			if (log) printf("close[%s] [PNL : %lf]\n", side.c_str(), PNL);
			return;
		} else if (size > new_pos.size) {
			size = new_pos.size;
			printf("[%s]ÀÏºÎ Ã¼°á!\n", side.c_str());
			return;
		}
		open_price = new_pos.entry_price;
		size = new_pos.size;
		has_pos = true;
		printf("frist size : %lf\n", get_qty(((std::stod(api::get_equity()) * 0.05) / 2), open_price, leverage));
		if (size == get_qty((std::stod(api::get_equity()) * 0.05) / 2, open_price, leverage)) {
			update_open_order();
			update_close_order(StopPNL);
		} else {
			set_new_order();
			update_close_order(stack_end ? AMStopPNL : StopPNL - (StopPNL * std::min(stack * 0.2, 1.0)));
		}
	}

	void set_new_order() {
		if (log) printf("AM Mode[%s]\n", side.c_str());
		auto next_qty_usdt = ((size * (open_price + (open_price * (get_ampnl() / 100 / leverage)) * stov(side))) / leverage);

		auto current_margin = std::stod(api::get_blance());

		if (next_qty_usdt > current_margin) {
			stack_end = true;
			printf("next : %lf$ > current : %lf$\n", next_qty_usdt, current_margin);
			printf("Stack_END!\n");
			return;
		}
		else update_open_order();
		stack++;
	}

	void open(double qty) {
		has_pos = true;
		stack_end = false;
		stack = 0;
		auto p = api::open_position(side, qty);
		open_price = p.entry_price;
		if (log) printf("Open[%s] [open_price : %lf][position_margin : %lf]\n", side.c_str(), open_price, open_price * p.size / leverage);
	}

	void close() {
		has_pos = false;
		api::cancel_all_order();
		api::close_position(side, size);
		if (log) printf("close[%s] [PNL : %lf]\n", side.c_str(), PNL);
	}

	double get_ampnl() { return AMPNL - (stack * a); }

	void tick() {
		if (!has_pos) return;

		update_pnl();

		if (stack_end && (PNL <= get_ampnl())) {
			close(); return;
		}
	}
};

struct data_info_type_b_t {
	double AMPNL;
	double AMStopPNL;
	double StopPNL;
	int leverage;
	double start_position_per = 0.1;
};

struct real_trade_t {
	std::vector<position_type_b> poss;
	data_info_type_b_t d;
	bool log;
	double margin;
	double pmargin;

	double win;
	double cnt;
	double a = 1;
	bool once = true;


	price_t& pr;
	real_trade_t(price_t& p) : pr(p)
	{

	}
	void init_poss()
	{
		margin = std::stod(api::get_blance());
		for (int i = 0; i < 2; i++) {
			auto p = position_type_b{};
			p.leverage = d.leverage;
			p.AMPNL = d.AMPNL;
			p.AMStopPNL = d.AMStopPNL;
			p.StopPNL = d.StopPNL;
			p.log = log;
			p.a = a;
			poss.push_back(p);
		}
		poss[0].side = "Buy";
		poss[1].side = "Sell";

		api::cancel_all_order();
		auto post_s = api::get_position();

		for (auto& i : post_s) 
			for (auto& p : poss) 
				if (i.side == p.side) p.update_position(i);
		pmargin = margin;
	}

	void tick() {
		for (auto& p : poss) {
			p.prices = pr;
		}
		if (to_vector(poss | std::views::filter([](position_type_b& x) { return x.has_pos; })).size() == 0) {
			if (!once) {
				margin = std::stod(api::get_blance());
				if (log) std::cout << std::format("{}->{}, (¼öÀÍ·ü : {}%)\n", pmargin, margin, (floor(-(1 - (margin / pmargin)) * 100 * 10) / 10));
				pmargin = margin;
			}
			api::cancel_all_order();
			once = false;
			for (auto& p : poss) {
				auto open_price = pr.get_open(p.side);
				auto qty = get_qty(margin * d.start_position_per / 2, open_price, d.leverage);
				printf("qty : %lf\n", qty);
				p.open(qty);
			}
		}
		for (auto& p : poss) {
			p.tick();
		}
	};
};

// test
int main() {



	data_info_type_b_t dinfo;
	{
		std::ifstream rf;
		std::string am_pnl;
		std::string am_stop_pnl;
		std::string stop_pnl;
		std::string leverage;

		rf.open("trade_info_data.txt");
		if (rf.is_open())
		{
			std::string s;
			while (!rf.eof())
			{
				char arr[256];
				rf.get(arr, 256);
				s.append(arr);
			}
			auto v = yc_json::split(s, '@');
			dinfo = data_info_type_b_t{
				.AMPNL = -std::stod(v[0]),
				.AMStopPNL = std::stod(v[1]),
				.StopPNL = std::stod(v[2]),
				.leverage = std::stoi(v[3]),
				.start_position_per = std::stod(v[4]),
			};
		}
		else {
			printf("[trade_info_data.txt] files not found!\n");
			return 0;
		}

		rf.close();    //ÆÄÀÏ ´Ý±â
	}
	{
		std::ifstream rf;
		std::string k;
		std::string sk;

		if (TESTNET) rf.open("bybitkeyTest.txt");
		else		rf.open("bybitkey.txt");
		if (rf.is_open())
		{
			std::string s;
			while (!rf.eof())
			{
				char arr[256];
				rf.get(arr, 256);
				s.append(arr);
			}
			api::api_key = yc_json::split(s, '@')[0];
			api::secret_key = yc_json::split(s, '@')[1];
		}
		else {
			printf("[bybitkey.txt] files not found!\n");
			return 0;
		}
		rf.close();    //ÆÄÀÏ ´Ý±â
	}

	real_trade_t trade(current_price);

	trade.margin = std::stod(api::get_blance());
	trade.log = true;
	trade.a = 7;
	trade.d = dinfo;
	trade.init_poss();

	std::map<std::string, api::position_t> prev_pos;

	prev_pos["Buy"] = api::position_t{};
	prev_pos["Sell"] = api::position_t{};

	start_web_socket([&] {
		for (auto& i : prev_pos) {
			if (updated_pos[i.first].size != i.second.size) {
				for (auto& p : trade.poss) 
					if (i.first == p.side) 
						p.update_position(updated_pos[i.first]);
				prev_pos[i.first] = updated_pos[i.first];
			}
		}
		});

	std::chrono::system_clock::time_point tp = std::chrono::system_clock::now();

	while (1) {
		Sleep(10);
		if (get_t(tp) >= 10.f) {
			tp = std::chrono::system_clock::now();
			if(api::get_order_list().size() == 0)
				for (auto& p : trade.poss) {
					if(updated_pos[p.side].size != 0) p.update_position(updated_pos[p.side], true);
				}
		}

		if (is_price_ready) {
			trade.tick();
		}
	}
}