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

struct price_t {
	double buy;
	double sell;

	double get_open(std::string side) {
		return (side == "Buy") ? sell : buy;
	}
	double get_close(std::string side) {
		return (side == "Buy") ? buy : sell;
	}
};

struct position {
	double StopLoss;
	double LimLoss;
	double max_pnl;
	int leverage;

	double size;
	double fee;
	double PNL;

	std::string side;

	bool has_pos = false;
	bool add_margin_mode = false;
	bool log = true;
	double open_price; 
	price_t prices;

	double a = 0.01;

	double get_margin() {
		return (open_price * size / leverage) + ((open_price * size / leverage) * PNL);
	}

	void open(double qty) {
		auto p = api::open_position(side, qty);
		if (has_pos) {
			add_margin_mode = true;
			open_price = p.entry_price;
			size += qty;
			fee = size * open_price * 0.0006;
			max_pnl = 0;
			if (log) std::cout << std::format("ADD MARGIN OPEN [{}], 수량 : {}, 가격 : {}, 수수료 : {}\n", side, (floor(size * 1000) / 1000), open_price, (floor(-fee * 10) / 10) );
		}
		else {
			add_margin_mode = false;
			has_pos = true;
			size = qty;
			open_price = p.entry_price;
			fee = size * open_price * 0.0006;
			if(log) std::cout << std::format("OPEN [{}], 수량 : {}, 가격 : {}, 수수료 : {}\n", side, (floor(size * 1000) / 1000), open_price, (floor(-fee * 10) / 10));
		}
	}

	void close() {
		auto p = api::close_position(side, size);
		while (p.size != 0) {
			Sleep(10);
			auto pp = api::get_position();
			auto v = to_vector(pp | std::ranges::views::filter([&](auto p) { return (p.side == side); }));
			if(v.size()) p = v[0];
		}
		if (log) std::cout << std::format("close! side : {}, PNL : {}%\n", side, (floor((PNL * 100) * 10) / 10));
		if (log) std::cout << "margin : " << api::get_blance() << std::endl;
	}
	double get_fee() {
		return (open_price * size * 0.0006) + (prices.get_close(side) * size * 0.0006);
	}
	void tick() {
		if (!has_pos) return;
		double v = (1 - (open_price / prices.get_close(side))) * leverage * ((side == "Buy") ? 1 : -1);

		double margin = open_price * size / leverage;
		double margin2 = margin + (margin * v) - get_fee();
		PNL = (1 - (margin / margin2));
		auto pnl = PNL * 100;
		max_pnl = std::max(pnl, max_pnl);

		if (add_margin_mode) {
			if ((pnl <= (max_pnl - (LimLoss - (a * ((pnl < 0) ? 0 : pnl)))))) {
				has_pos = false;
				close();
			}
		}
		else if (pnl <= StopLoss) {
			has_pos = false;
			close();
		}
	}
};

static double get_qty(double margin, double price, int leverage)
{
	double r = margin * 0.85 / price * leverage;
	return (r < 0.001) ? 0.001 : (floor(r * 1000) / 1000);
	//return 0.001;
}

struct mo_position {
	double StopLoss;
	double LimLoss;
	double max_pnl;
	int leverage;
	double size;
	double PNL;
	std::string side;
	bool has_pos = false;
	bool add_margin_mode = false;
	bool log = true;
	double open_price;
	price_t prices;

	double a = 0.01;

	void open(double qty) {
		auto new_open_price = prices.get_open(side);
		if (has_pos) {
			add_margin_mode = true;
			open_price = ((new_open_price * qty) + (open_price * size)) / (qty + size);
			size += qty;
			update_pnl();
			max_pnl = PNL;
			if (log) std::cout << std::format("[MO]ADD MARGIN OPEN [{}], 수량 : {}, 가격 : {}, 수수료 : {}\n", side, (floor(size * 1000) / 1000), open_price, (floor(-get_fee() * 10) / 10));
		} else {
			has_pos = true;
			open_price = new_open_price;
			size = qty;
			if (log) std::cout << std::format("[MO]OPEN [{}], 수량 : {}, 가격 : {}, 수수료 : {}\n", side, (floor(size * 1000) / 1000), open_price, (floor(-get_fee() * 10) / 10));
		}
	}
	void close() {
		has_pos = false;
		add_margin_mode = false;
	}
	double get_fee() {
		return (open_price * size * 0.0006) + (prices.get_close(side) * size * 0.0006);
	}
	double get_margin() {
		auto m = (open_price * size / leverage);
		return m + (m * (PNL / 100));
	}
	void update_pnl() {
		double close_price = prices.get_close(side);
		double per = ((double)1 - (open_price / close_price)) * leverage * ((side == "Buy") ? 1 : -1);
		double margin = open_price * size / leverage;
		double margin2 = margin + (margin * per) - get_fee();
		PNL = (1 - (margin/margin2)) * 100;
	}
	void add_margin_tick() {
		if (PNL > max_pnl) max_pnl = PNL;
		if (PNL < (max_pnl - (LimLoss - (a*((PNL < 0) ? 0 : PNL))  ))) close();
	}
	void tick() {
		if (!has_pos) return;
		update_pnl();
		if (add_margin_mode) add_margin_tick();
		else if (PNL < StopLoss) close();
	}
};

struct trade_result_t
{
	int cnt;
	double winrate;
	double margin;
};

trade_result_t trade(std::vector<price_t>& prices, double margin, double limloss, double stoploss, int leverage = 10, bool log = false, double a = 0.01) {
	using namespace std::ranges::views;
	using namespace std::ranges;

	mo_position pos[2];

	double win = 0;
	double cnt = 0;

	for (auto& i : pos) {
		i.leverage = leverage;
		i.LimLoss = limloss;
		i.StopLoss = stoploss;
		i.a = a;
		i.log = false;
	}
	pos[0].side = "Buy";
	pos[1].side = "Sell";

	mo_position* one_way = nullptr;
	double prev_margin = margin;
	bool add_margin_mode = true;

	auto add_margin_mode_open = [&](price_t price, mo_position& opos, mo_position& cpos) {
		add_margin_mode = true;
		margin += cpos.get_margin();
		auto open_price = price.get_open(opos.side);
		auto qty = get_qty(margin, open_price, leverage);
		opos.open(qty);
		margin -= qty * open_price / leverage;
		one_way = &opos;
	};

	for (auto& i : prices) {
		if (margin <= 0) break;
		for (auto& p : pos) {
			p.prices = i;
		}
		if (!add_margin_mode) {
			if(!pos[0].has_pos) add_margin_mode_open(i, pos[1], pos[0]);
			if(!pos[1].has_pos) add_margin_mode_open(i, pos[0], pos[1]);
		}
		else {
			if (to_vector(pos | std::views::filter([](mo_position& x) { return x.has_pos; })).size() == 0) {
				margin += one_way ? one_way->get_margin() : 0;
				win += (prev_margin <= margin);
				cnt += 1;
				if(log) std::cout << std::format("prev_margin : {}, margin : {}, (수익률 : {}%)\n", prev_margin, margin, (floor(-(1 - (margin / prev_margin)) * 100 * 10) / 10));
				prev_margin = margin;
				auto half_margin = margin / 2;
				for (auto& p : pos) {
					auto open_price = i.get_open(p.side);
					auto qty = get_qty(half_margin, open_price, leverage);
					margin -= qty * open_price / leverage;
					p.open(qty);
				}
				add_margin_mode = false;
			}
		}
		for (auto& p : pos) {
			p.tick();
		}
	}
	for (mo_position& i : pos | std::views::filter([](mo_position& x) { return x.has_pos; })) {
		margin += i.get_margin();
	}
	if (log) std::cout << std::format("prev_margin : {}, margin : {}, (수익률 : {}%)\n", prev_margin, margin, (floor(-(1 - (margin / prev_margin)) * 100 * 10) / 10));
	return trade_result_t { (int)cnt, (win / cnt) * 100, margin };
}

#define 가격불러오기		true
#define 모의투자8스레드	true

std::string api_url = "https://api.bybit.com";
std::string api_key = "bxfuVX3jk4R834ggVX";
std::string secret_key = "QSV5P8FCmqEeiHjXQA1HB7vKZwu6pbIIYHSg";

// HTTPS
httplib::Client cli("https://api.bybit.com");

struct data_info_t {
	double a;
	double b;
	int c;
};

int main() {
	//test();
	std::vector<price_t> v[4];
	if(가격불러오기)
	{
		std::ifstream readFile;
		std::vector<std::string> files = {
			"prices4",
			"prices5",
			"prices8"
		};
		
		int cnt = 0;
		for (auto& i : files) {
			std::string datas;
			readFile.open(std::format("C:\\Users\\anyc1\\source\\repos\\trade_bot_bybit\\price\\{}.txt", i));
			if (readFile.is_open())
			{
				while (!readFile.eof())
				{
					char arr[256];
					readFile.get(arr, 256);
					datas.append(arr);
				}
			}
			readFile.close();    //파일 닫기
			yc_json::rtrim(datas, "@");
			auto vec = yc_json::split(datas, '@');
			v[cnt++] = to_vector(vec | std::views::transform([](std::string& x)->price_t { double d = atof(x.c_str()); return price_t{d,d + 0.5}; }));
		}
	}
	
	bool TEST = false;//가격불러오기;
	bool ATEST = true;
	ATEST = false;
	if (TEST) {
		// 
		
		//data_info_t d{ 21.6, -5.9, 40 };
		data_info_t d{ 20.8, -4.4, 21 };
		double a = 0.01;
		if (ATEST) {
			for (int i = 0; i < 30; i++) {
				printf("----------------[%.2lf]-----------------\n", i * 0.01);
				auto [c, w, m] = trade(v[0], 1000, d.a, d.b, d.c, false, i * 0.01);
				printf("winrate : %lf\n", w);
				auto [c2, w2, m2] = trade(v[1], m, d.a, d.b, d.c, false, i * 0.01);
				printf("winrate : %lf\n", w2);
				printf("margin : %.3lf\n", m2);
				auto [c3, w3, m3] = trade(v[2], m2, d.a, d.b, d.c, true, a);
				printf("winrate : %lf\n", w3);
				printf("margin : %.3lf\n", m3);
				printf("--------------------------------\n");
			}
		}
		else {
			auto [c, w, m] = trade(v[0], 9.7, d.a, d.b, d.c, true, a);
			printf("winrate : %lf\n", w);
			auto [c2, w2, m2] = trade(v[1], m, d.a, d.b, d.c, true, a);
			printf("winrate : %lf\n", w2);
			printf("margin : %.3lf\n", m2);
			auto [c3, w3, m3] = trade(v[2], m2, d.a, d.b, d.c, true, a);
			printf("winrate : %lf\n", w3);
			printf("margin : %.3lf\n", m3);
			printf("--------------------------------\n");
		}
	}
	while (TEST) Sleep(1000);

	if (모의투자8스레드) {
		double start_margin;
		double check_c;
		double check_winrate;
		std::cin >> start_margin >> check_c >> check_winrate;

		std::vector<std::thread> th;
		for (int ii = 0; ii < 8; ii++) {
			th.push_back(std::thread([&v, x = 10 * ii, start_margin, check_c, check_winrate]() {
				
				for (int k = x + 1; k <= (x + 10); k++) {
					if ((x == 0)) {
						std::cout << std::format("진행도 [{}%]\n", -(1 - ((double)k / (double)(x + 10)) * 100));
					}
					for (int i = 10; i < 900; i++) {
						for (int j = 10; j < 900; j++) {
							double d = 0.1;
							double mmargin = 0;
							double mwinrate = 0;
							int mc = 0;
							for (auto& vv : v) {
								auto [c, winrate, margin] = trade(vv, start_margin, (double)j * d, -(double)i * d, k);
								mmargin += margin;
								mwinrate += winrate;
								mc += c;
							}
							mwinrate /= (sizeof(v)/sizeof(std::vector<price_t>));
							mmargin /= (sizeof(v)/sizeof(std::vector<price_t>));
							if ((mwinrate > check_winrate) && (mmargin > start_margin) && (mc > check_c)) {
								printf("[C:%d][winrate : %.1lf][margin : %.1lf] => %.1lf, %.1lf, %d\n", mc, mwinrate, mmargin, (double)j * d, -(double)i * d, k);
							}
						}
					}
				}
			}));
		}
		for (auto& x : th) {
			x.join();
		}
		printf("\n\n종료!\n\n");
		getchar();
	}

	data_info_t d{ 20.8, -4.4, 21 };
	const bool log = true;

	double margin = std::stod(api::get_blance());
	position pos[2];
	for (auto& i : pos) {
		i.leverage = d.c;
		i.LimLoss  = d.a;
		i.StopLoss = d.b;
		i.log = true;
	}
	pos[0].side = "Buy";
	pos[1].side = "Sell";
	position* one_way = nullptr;
	double prev_margin = margin;
	bool add_margin_mode = true;

	auto add_margin_mode_open = [&](price_t price, position& opos, position& cpos) {
		add_margin_mode = true;
		margin = std::stod(api::get_blance());
		opos.open(get_qty(margin, price.get_open(opos.side), d.c));
		one_way = &opos;
	};

	mo_position pos2[2];

	for (auto& i : pos2) {
		i.leverage = d.c;
		i.LimLoss = d.a;
		i.StopLoss = d.b;
		i.a = 0.01,
		i.log = true;
	}
	pos2[0].side = "Buy";
	pos2[1].side = "Sell";

	mo_position* one_way2 = nullptr;
	double margin2 = margin;
	double prev_margin2 = margin2;
	bool add_margin_mode2 = true;

	auto add_margin_mode_open2 = [&](price_t price, mo_position& opos, mo_position& cpos) {
		add_margin_mode2 = true;
		printf("[MO] margin : %lf\n", floor(margin2 * 10) / 10);
		margin2 += cpos.get_margin();
		printf("[MO] margin : %lf\n", floor(margin2 * 10) / 10);
		auto open_price = price.get_open(opos.side);
		auto qty = get_qty(margin2, open_price, d.c);
		opos.open(qty);
		margin2 -= qty * open_price / d.c;
		one_way2 = &opos;
	};

	std::function<void(double, double)> f = [&](double buy, double sell)
	{ 
		price_t price{ sell, buy };// set!
		{
			for (auto& p : pos2) {
				p.prices = price;
			}
			if (!add_margin_mode2) {
				if (!pos2[0].has_pos) add_margin_mode_open2(price, pos2[1], pos2[0]);
				if (!pos2[1].has_pos) add_margin_mode_open2(price, pos2[0], pos2[1]);
			}
			else {
				if (to_vector(pos2 | std::views::filter([](mo_position& x) { return x.has_pos; })).size() == 0) {
					margin2 += one_way2 ? one_way2->get_margin() : 0;
					if (log) std::cout << std::format("[MO] prev_margin : {}, margin : {}, (수익률 : {}%)\n", prev_margin2, margin2, (floor(-(1 - (margin2 / prev_margin2)) * 100 * 10) / 10));
					prev_margin2 = margin2;
					auto half_margin = margin2 / 2;
					for (auto& p : pos2) {
						auto open_price = price.get_open(p.side);
						auto qty = get_qty(half_margin, open_price, d.c);
						margin2 -= qty * open_price / d.c;
						p.open(qty);
					}
					add_margin_mode2 = false;
				}
			}
			for (auto& p : pos2) {
				p.tick();
			}
		}
		/*{
			for (auto& p : pos) {
				p.prices = price;
			}
			if (!add_margin_mode) {
				if (!pos[0].has_pos) add_margin_mode_open(price, pos[1], pos[0]);
				if (!pos[1].has_pos) add_margin_mode_open(price, pos[0], pos[1]);
			}
			else {
				if (to_vector(pos | std::views::filter([](position& x) { return x.has_pos; })).size() == 0) {
					margin = std::stod(api::get_blance());
					if (log) std::cout << std::format("prev_margin : {}, margin : {}, (수익률 : {}%)\n", prev_margin, margin, (floor(-(1 - (margin / prev_margin)) * 100 * 10) / 10));
					prev_margin = margin;
					auto half_margin = margin / 2;
					for (auto& p : pos)
						p.open(get_qty(half_margin, price.get_open(p.side), d.c));
					add_margin_mode = false;
				}
			}
			for (auto& p : pos) {
				p.tick();
			}
		}*/
	};

	start_web_socket(&f);
	while (1) Sleep(1000);
}