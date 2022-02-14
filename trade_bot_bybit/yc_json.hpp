#pragma once
#include <iostream>
#include <map>
#include <vector>
#include <ranges>
#include <sstream>
#include <string>

namespace yc_json {

	using std::string;

	std::vector<string> split(string str, char Delimiter) {
		std::istringstream iss(str);         
		string buffer;                    
		std::vector<string> result;
		while (getline(iss, buffer, Delimiter)) {
			result.push_back(buffer);              
		}
		return result;
	}


	string get_array(string data) {
		string r;
		bool start = false;
		int cnt = 0;
		
		for (auto& i : data) {
			if (start) {
				r.push_back(i);
				if (i == '[') {
					cnt++;
				} 
				if (i == ']') {
					if (cnt == 0) {
						r.pop_back();
						break;
					}
					else {
						cnt--;
					}
				}
			} else if (i == '[') {
				start = true;
			}
		}
		return r;
	}


	string get_object(string data) {
		string r;
		bool start = false;
		int cnt = 0;

		for (auto& i : data) {
			if (start) {
				r.push_back(i);
				if (i == '{') {
					cnt++;
				}
				if (i == '}') {
					if (cnt == 0) {
						r.pop_back();
						break;
					}
					else {
						cnt--;
					}
				}
			}
			else if (i == '{') {
				start = true;
			}
		}
		return r;
	}

	inline std::string& ltrim(std::string& s, const char* t = " \t\n\r\f\v")
	{
		s.erase(0, s.find_first_not_of(t));
		return s;
	}

	inline std::string& rtrim(std::string& s, const char* t = " \t\n\r\f\v")
	{
		s.erase(s.find_last_not_of(t) + 1);
		return s;
	}

	inline std::string& trim(std::string& s, const char* t = " \t\n\r\f\v")
	{
		return ltrim(rtrim(s, t), t);
	}


	std::pair<int, string> get_object_mem(string s) {
		string r;
		bool start = false;
		int cnt = 0;
		int i;
		for (i = 0; i < s.size(); i++) {
			if (start) {
				r.push_back(s[i]);
				if (s[i] == '{') {
					cnt++;
				}
				if (s[i] == '}') {
					if (cnt == 0) {
						//r.pop_back();
						break;
					}
					else {
						cnt--;
					}
				}
			}
			else if (s[i] == '{') {
				start = true;
			}
		}
		return std::make_pair(i,r);
	}
	std::pair<int, string> get_array_mem(string s) {
		string r;
		bool start = false;
		int cnt = 0;
		int i;
		for (i = 0; i < s.size(); i++) {
			if (start) {
				r.push_back(s[i]);
				if (s[i] == '[') {
					cnt++;
				}
				if (s[i] == ']') {
					if (cnt == 0) {
						//r.pop_back();
						break;
					}
					else {
						cnt--;
					}
				}
			}
			else if (s[i] == '[') {
				start = true;
			}
		}
		return std::make_pair(i, r);
	}

	std::map<string, string> get_object_members(string data) {
		std::map<string, string> r;

		bool start = false;
		bool start_mem = false;
		string save;

		for (int i = 0; i < data.size(); i++) {
			if (start_mem) {
				if (data[i] == ':') {

				}
				else if (data[i] == '[') {
					r[save].push_back(data[i]);
					auto s = get_array_mem(data.substr(i));
					r[save].append(s.second);
					i += s.first;
				}
				else if (data[i] == '{') {
					r[save].push_back(data[i]);
					auto s = get_object_mem(data.substr(i));
					r[save].append(s.second);
					i += s.first;
				}
				else if (data[i] == ',') {
					start_mem = false;
					auto s = r[save] | std::ranges::views::filter([](const auto& x) {  return (x != '\n') && (x != '\t') && (x != ' '); });
					r[save].assign(s.begin(), s.end());
					trim(r[save], "\"");
					trim(r[save], "}");
				} else {
					r[save].push_back(data[i]);
				}
			} else {
				if (start) {
					if (data[i] == '\"') {
						start = false;
						start_mem = true;
						r[save] = "";
					}
					else {
						save.push_back(data[i]);
					}
				}
				else if (data[i] == '\"') {
					start = true;
					save.clear();
				}
			}
		}
		auto s = r[save] | std::ranges::views::filter([](const auto& x) {  return (x != '\n') && (x != '\t') && (x!=' '); });
		r[save].assign(s.begin(), s.end());
		trim(r[save], "\"");
		trim(r[save], "}");
		return r;
	}


	std::vector<string> get_array_members(string data) {
		std::vector<string> r;
		for (int i = 0; i < data.size(); i++) {
			if (data[i] == '{') {
				auto s = get_object_mem(data.substr(i));
				i += s.first;
				r.push_back(s.second);
			}
		}

		return r;
	}


	class JToken {
	public:
		std::string data;

		JToken operator[](std::string key)
		{
			return JToken { get_object_members(data)[key].c_str() };
		}

		JToken operator[](int key)
		{
			return JToken{ get_array_members(data)[key].c_str() };
		}

		operator const char*()
		{
			trim(data, "\"");
			return data.c_str();
		}

		int count() {
			return get_array_members(data).size();
		}

		string& to_string() {
			trim(data, "\"");
			return data;
		}
		const char* to_text() {
			return *this;
		}
	};

	JToken parse(std::string json)
	{
		return JToken{ json };
	}
}