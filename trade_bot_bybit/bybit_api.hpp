#pragma once
#include <map>
#include <iostream>
#include <string>
#include <format>
#include <ranges>
#include "Encryption.hpp"

#define TESTNET false


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


    class place_active_order_lim
    {
    public:
        string   api_key;
        string   position_idx;
        double   qty;
        double   price;
        bool     reduce_only;
        string   side;
        string   timestamp;
        string   sign;

        std::string to_json() {
            return std::format(
                "{{\"api_key\":\"{}\",\"close_on_trigger\":false,\"side\":\"{}\",\"symbol\":\"BTCUSDT\",\"order_type\":\"Limit\",\"position_idx\":{},\"price\":{},\"qty\":{},\"reduce_only\":{},\"time_in_force\":\"GoodTillCancel\",\"timestamp\":{},\"sign\":\"{}\"}}",
                api_key,
                side,
                position_idx.c_str(),
                std::format("{}", price),
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
                {"order_type", "Limit"},
                {"position_idx", position_idx},
                {"price", std::format("{}", price)},
                {"qty", std::format("{}", (floor((qty) * 1000) / 1000))},
                {"reduce_only", string(reduce_only ? "true" : "false")},
                {"side", side},
                {"symbol", "BTCUSDT"},
                {"time_in_force", "GoodTillCancel"},
                {"timestamp", timestamp},
            };
        }
    };

    class cancel_active_order {
    public:
        string   api_key;
        string   order_id;
        string   timestamp;
        string   sign;

        std::string to_json() {
            return std::format(
                "{{\"api_key\":\"{}\",\"symbol\":\"BTCUSDT\",\"order_id\":\"{}\",\"timestamp\":{},\"sign\":\"{}\"}}",
                api_key,
                order_id,
                timestamp,
                sign);
        }

        Params CreateParamStr()
        {
            return Params{
                {"api_key", api_key},
                {"order_id", order_id},
                {"symbol", "BTCUSDT"},
                {"timestamp", timestamp},
            };
        }
    };


    class cancel_all_active_order {
    public:
        string   api_key;
        string   timestamp;
        string   sign;

        std::string to_json() {
            return std::format(
                "{{\"api_key\":\"{}\",\"symbol\":\"BTCUSDT\",\"timestamp\":{},\"sign\":\"{}\"}}",
                api_key,
                timestamp,
                sign);
        }

        Params CreateParamStr()
        {
            return Params{
                {"api_key", api_key},
                {"symbol", "BTCUSDT"},
                {"timestamp", timestamp},
            };
        }
    };

    std::string api_key = "";
    std::string secret_key = "";


    // HTTPS
    httplib::Client cli(TESTNET ? "https://api-testnet.bybit.com" : "https://api.bybit.com");



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

    std::vector<std::string> get_order_list()
    {
        auto t = time();
        std::string url = "/private/linear/order/search?";
        auto pram = Params
        {
        { "api_key", api_key },
        { "symbol","BTCUSDT"},
        { "timestamp", t}
        };
        pram["sign"] = GetSignature(pram, secret_key);
        url.append(GetParams(pram));
        auto res = cli.Get(url.c_str());

        std::vector<std::string> list;
        auto j = yc_json::parse(res->body)["result"];

        for (int i = 0; i < j.count(); i++) 
            list.push_back(j[i].to_string());
        
        return list;
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


    string open_position_lim(string side, double qty, double price)
    {
        api::place_active_order_lim order;

        order.api_key = api_key;
        order.position_idx = (side == "Buy") ? "1" : "2";
        order.qty = qty;
        order.price = price;
        order.reduce_only = false;
        order.side = side;
        order.timestamp = time();
        order.sign = GetSignature(order.CreateParamStr(), secret_key);

        auto r = cli.Post("/private/linear/order/create", order.to_json(), "application/json");
        
        return yc_json::parse(r->body.c_str())["result"]["order_id"].to_string();
    }

    string close_position_lim(string side, double qty, double price)
    {
        api::place_active_order_lim order;

        order.api_key = api_key;
        order.position_idx = (side == "Buy") ? "1" : "2";
        order.qty = qty;
        order.price = price;
        order.reduce_only = true;
        order.side = (side == "Buy") ? "Sell" : "Buy";
        order.timestamp = time();
        order.sign = GetSignature(order.CreateParamStr(), secret_key);

        auto r = cli.Post("/private/linear/order/create", order.to_json(), "application/json");

        return yc_json::parse(r->body.c_str())["result"]["order_id"].to_string();
    }

    string cancel_order(string order_id)
    {
        api::cancel_active_order order;

        order.api_key = api_key;
        order.order_id = order_id;
        order.timestamp = time();
        order.sign = GetSignature(order.CreateParamStr(), secret_key);

        auto r = cli.Post("/private/linear/order/cancel", order.to_json(), "application/json");

        return yc_json::parse(r->body.c_str()).to_string();
    }

    string cancel_all_order()
    {
        api::cancel_all_active_order order;

        order.api_key = api_key;
        order.timestamp = time();
        order.sign = GetSignature(order.CreateParamStr(), secret_key);

        auto r = cli.Post("/private/linear/order/cancel-all", order.to_json(), "application/json");

        return yc_json::parse(r->body.c_str()).to_string();
    }


    string get_equity() {
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

        return yc_json::parse(res->body.c_str())["result"]["USDT"]["equity"].to_string();
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