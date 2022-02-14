#pragma once
#include <map>
#include <iostream>
#include <string>
#include <format>
#include <ranges>
#include "Encryption.hpp"

template <std::ranges::range R>
auto to_vector(R&& r) {
    auto r_common = r | std::views::common;
    return std::vector(r_common.begin(), r_common.end());
}

namespace api
{
	using std::string;
	using std::wstring;
    using namespace std::ranges;
    using namespace std::ranges::views;

	static std::map<double, double> orderbook;
	static std::vector<double> orderbook_2;

	static void print_orderbook() {
		printf("+---------------------+\n");
		int os = orderbook.size()/2;
		int j = 0;
		orderbook_2.clear();
		for (auto& i : orderbook) {
			if ((j == os) || (j == os - 1)) {
				orderbook_2.push_back(i.first);
			}
			j++;
		}
		for (auto& i : orderbook_2) {
			std::cout << i << std::endl;
		}
		printf("+---------------------+\n");
	}

    class place_active_order
    {
    public:
        string   api_key;
        string   position_idx;
        double   qty;
        bool     reduce_only;
        string   side;
        string   timestamp;
        string   sign;

        std::string to_json() {
            return std::format(
                "{{\"api_key\":\"{}\",\"close_on_trigger\":false,\"side\":\"{}\",\"symbol\":\"BTCUSDT\",\"order_type\":\"Market\",\"position_idx\":{},\"qty\":{},\"reduce_only\":{},\"time_in_force\":\"GoodTillCancel\",\"timestamp\":{},\"sign\":\"{}\"}}",
                api_key,
                side,
                position_idx.c_str(),
                (floor((qty) * 1000) / 1000),
                (reduce_only ? "true" : "false"),
                timestamp,
                sign);
        }

        Params CreateParamStr()
        {
            return Params{
                {"api_key", api_key},
                {"close_on_trigger", "false"},
                {"order_type", "Market"},
                {"position_idx", position_idx},
                {"qty", std::format("{}", (floor((qty) * 1000) / 1000))},
                {"reduce_only", string(reduce_only ? "true" : "false")},
                {"side", side},
                {"symbol", "BTCUSDT"},
                {"time_in_force", "GoodTillCancel"},
                {"timestamp", timestamp},
            };
        }
    };

    std::string api_url = "https://api.bybit.com";
    std::string api_key = "bxfuVX3jk4R834ggVX";
    std::string secret_key = "QSV5P8FCmqEeiHjXQA1HB7vKZwu6pbIIYHSg";

    // HTTPS
    httplib::Client cli("https://api.bybit.com");

    long double get_time()
    {
        auto res = cli.Get("/v2/public/time");
        auto d = yc_json::parse(res->body.c_str());
        return std::stold(d["time_now"].to_text()) * 1000;
    }

    std::string time() {
        std::string t = std::to_string(get_time());
        t = yc_json::split(t, '.')[0];
        return t;
    }

    struct position_t
    {
        string side;
        double entry_price;
        double size;
    };

    std::vector<position_t> get_position()
    {
        auto t = time();
        std::string url = "/private/linear/position/list?";
        auto pram = Params
        {
        { "api_key", api_key },
        { "symbol","BTCUSDT"},
        { "timestamp", t}
        };
        pram["sign"] = GetSignature(pram, secret_key);
        url.append(GetParams(pram));
        auto res = cli.Get(url.c_str());


        auto j = yc_json::parse(res->body)["result"];
        std::vector<position_t> r;
        for (int i = 0; i < j.count(); i++) {
            auto data = j[i];
            r.push_back(position_t{
                data["side"].to_string(),
                std::stod(data["entry_price"].to_string()),
                std::stod(data["size"].to_string())
                });
        }
        return r;
    }

    position_t close_position(string side, double qty)
    {
        api::place_active_order order;

        order.api_key = api_key;
        order.position_idx = (side == "Buy") ? "1" : "2";
        order.qty = qty;
        order.reduce_only = true;
        order.side = (side == "Buy") ? "Sell" : "Buy";
        order.timestamp = time();
        order.sign = GetSignature(order.CreateParamStr(), secret_key);

        auto r = cli.Post("/private/linear/order/create", order.to_json(), "application/json");

        position_t m_p;
        if (r->status != -1) {
            auto p = get_position();
            m_p = to_vector(p | filter([side](auto p) { return (p.side == side); }))[0];
        }

        return m_p;
    }

    position_t open_position(string side, double qty)
    {
        api::place_active_order order;

        order.api_key = api_key;
        order.position_idx = (side == "Buy") ? "1" : "2";
        order.qty = qty;
        order.reduce_only = false;
        order.side = side;
        order.timestamp = time();
        order.sign = GetSignature(order.CreateParamStr(), secret_key);

        auto r = cli.Post("/private/linear/order/create", order.to_json(), "application/json");
        position_t m_p;
        if (r->status != -1) {
            auto p = get_position();
            m_p = to_vector(p | filter([side](auto p) { return (p.side == side); }))[0];
        }
        else {
            printf(r->body.c_str());
        }
        return m_p;
    }

    string get_blance() {
        std::string url = "/v2/private/wallet/balance?";

        auto t = time();
        auto pram = Params
        {
        { "api_key", api_key },
        { "symbol","BTCUSDT"},
        { "timestamp", t}
        };
        pram["sign"] = GetSignature(pram, secret_key);
        url.append(GetParams(pram));
        auto res = cli.Get(url.c_str());

        return yc_json::parse(res->body.c_str())["result"]["USDT"]["available_balance"].to_string();
    }
}