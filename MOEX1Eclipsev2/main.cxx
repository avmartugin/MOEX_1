/*
* main.cxx
*
*  Created on: 20 февр. 2015 г.
*      Author: avmartugin
*/

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/karma.hpp>
#include <boost/spirit/include/qi_as.hpp>
#include <boost/fusion/adapted.hpp>
#include <conio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <time.h>
#include <cstdlib>

using namespace std;
namespace qi = boost::spirit::qi;
namespace ph = boost::phoenix;
namespace karma = boost::spirit::karma;

bool file_is_empty(std::ifstream& pFile)
{
	return pFile.peek() == std::ifstream::traits_type::eof();
}

typedef long order_int;
typedef long long order_sum_int;

struct order {
	order(const order_int num_, const order_int volume_,
		const order_int price_) :
		num(num_), volume(volume_), price(price_) {
	}

	// for M orders price non initialized, but if unused price will be 0 by default... its slow
	order(const order_int num_, const order_int volume_) :
		num(num_), volume(volume_) {
	}

	order_int num;
	order_int volume;
	order_int price;
};

struct auction_order {
	auction_order(const order_int num_buy_, const order_int num_sell_,
		const order_int auction_volume_, const order_sum_int auction_value_) :
		num_buy(num_buy_), num_sell(num_sell_), auction_volume(
		auction_volume_), auction_value(auction_value_) {
	}

	order_int num_buy;
	order_int num_sell;
	order_int auction_volume;
	order_sum_int auction_value;
};

std::ostream&
operator<<(std::ostream& os, auction_order const& z) {
	os << z.num_buy << ";" << z.num_sell << ";" << z.auction_volume << ";"
		<< z.auction_value;
	return os;
}

struct auc_volumes {
	auc_volumes(const order_int buy_volume_, const order_int sell_volume_,
		const order_int limit_order_value_) :
		buy_volume(buy_volume_), sell_volume(sell_volume_), limit_order_value(
		limit_order_value_) {
	}

	auc_volumes() {
		buy_volume = 0;
		sell_volume = 0;
		limit_order_value = 0;
	}

	order_sum_int buy_volume;
	order_sum_int sell_volume;
	order_sum_int limit_order_value;
};

template<typename OutputIterator, typename Container>
bool generate_objects(OutputIterator& sink, Container const& v) {
	using boost::spirit::karma::stream;
	using boost::spirit::karma::generate;
	using boost::spirit::karma::eol;

	bool r = generate(sink,             // destination: output iterator
		stream % eol,                   // the generator
		v                               // the data to output
		);
	return r;
}

static bool BL_order_sort_for_aucton(order const& lhs,
	order const& rhs) {
	return lhs.price < rhs.price
		|| (lhs.price == rhs.price && lhs.num > rhs.num);
}
static bool SL_order_sort_for_aucton(order const& lhs,
	order const& rhs) {
	return lhs.price > rhs.price
		|| (lhs.price == rhs.price && lhs.num > rhs.num);
}
static bool M_orders_sort_for_aucton(order const& lhs,
	order const& rhs) {
	return lhs.num > rhs.num;
}

order_sum_int BL_order_vol_sum = 0;
order_sum_int BM_order_vol_sum = 0;
order_sum_int SL_order_vol_sum = 0;
order_sum_int SM_order_vol_sum = 0;

std::unordered_map<order_int, order_sum_int> BL_order_map;
std::unordered_map<order_int, order_sum_int> SL_order_map;

std::vector<order> BL_order_storage;
std::vector<order> BM_order_storage;
std::vector<order> SL_order_storage;
std::vector<order> SM_order_storage;

inline bool intersect(const order_int a_, const order_int b_,
	const order_int c_, const order_int d_) {
	order_int a = a_;
	order_int b = b_;
	order_int c = c_;
	order_int d = d_;

	if (a > b) {
		std::swap(a, b);
	}
	if (c > d) {
		std::swap(c, d);
	}

	return std::max(a, c) <= std::min(b, d);
}

order_int price = 0;
order_sum_int auction_value = 0;
std::vector<auction_order> auction;

void add_to_auction(const auction_order order_,
	const order_sum_int add_value_) {
	auction.push_back(order_);
	auction_value += add_value_;
}

int main(int argc, char *argv[]) {

	// time start

	long t_start = clock();

	// try to open file

	if (argc != 2) {
		cout << "Usage: " << argv[0] << " <filename>" << std::endl;
		_getch();
		return EXIT_FAILURE;
	}
	else {
		ifstream the_file(argv[1]);
		// can open file?
		if (!the_file.is_open()) {
			cout << "Could not open file" << std::endl;
			_getch();
			return EXIT_FAILURE;
		}
		// file is empty?
		if (file_is_empty(the_file)) {
			cout << "File is empty" << std::endl;
			_getch();
			return EXIT_FAILURE;
		}
	}

	// file to mem

	const std::string file_in_mem(
		(std::istreambuf_iterator<char>(
		*(std::auto_ptr<std::ifstream>(
		new std::ifstream(argv[1]))).get())),
		std::istreambuf_iterator<char>());

	// time start

	long t_auc_start = clock();

	// parse lambdas

	auto BL_order = [](order_int num, order_int value, order_int price)
	{
		BL_order_map[price] += value;
		BL_order_storage.push_back(order(num, value, price));
	};

	auto BM_order = [](order_int num, order_int value)
	{
		BM_order_vol_sum += value;
		BM_order_storage.push_back(order(num, value));
	};

	auto SL_order = [](order_int num, order_int value, order_int price)
	{
		SL_order_map[price] += value;
		SL_order_storage.push_back(order(num, value, price));
	};

	auto SM_order = [](order_int num, order_int value)
	{
		SM_order_vol_sum += value;
		SM_order_storage.push_back(order(num, value));
	};

	// parse

	if (!qi::phrase_parse(file_in_mem.begin(), file_in_mem.end(),
		(*((qi::skip(qi::char_("SM;,") | qi::space)[(qi::uint_ >> qi::uint_ >> -(qi::uint_))[ph::bind(SM_order, qi::_1, qi::_2)]])
		| (qi::skip(qi::char_("BM;,") | qi::space)[(qi::uint_ >> qi::uint_ >> -(qi::uint_))[ph::bind(BM_order, qi::_1, qi::_2)]])
		| (qi::skip(qi::char_("BL;,") | qi::space)[(qi::uint_ >> qi::uint_ >> qi::uint_)[ph::bind(BL_order, qi::_1, qi::_2, qi::_3)]])
		| (qi::skip(qi::char_("SL;,") | qi::space)[(qi::uint_ >> qi::uint_ >> qi::uint_)[ph::bind(SL_order, qi::_1, qi::_2, qi::_3)]])
		| (qi::as_string[qi::lexeme[+qi::graph]])
		//[std::cout << "abracadabra find: {" << qi::_1 << "}" << std::endl] - off for fast parsing, only for debug
		>>
		*qi::eol)), qi::ascii::space))
	{
		std::cout << "Parse failed!" << std::endl;
		_getch();
		return EXIT_FAILURE;
	}

	// <only market orders in a file> check

	if (BL_order_storage.empty() && SL_order_storage.empty()){
		std::cout << "Only market orders in a file" << std::endl;
		_getch();
		return EXIT_FAILURE;
	}

	// calculate highest auction price mul volume

	std::map<order_int, order_sum_int> ordered_BL_order_map(
		BL_order_map.begin(), BL_order_map.end());
	for (auto f_it = ordered_BL_order_map.rbegin();
		f_it != ordered_BL_order_map.rend(); ++f_it) {
		auto s_it = f_it;
		s_it++;
		if (s_it != ordered_BL_order_map.rend()) {
			s_it->second += f_it->second;
		}
		f_it->second += BM_order_vol_sum;
	}

	std::map<order_int, order_sum_int> ordered_SL_order_map(
		SL_order_map.begin(), SL_order_map.end());
	for (auto f_it = ordered_SL_order_map.begin();
		f_it != ordered_SL_order_map.end(); ++f_it) {
		auto s_it = f_it;
		s_it++;
		if (s_it != ordered_SL_order_map.end()) {
			s_it->second += f_it->second;
		}
		f_it->second += SM_order_vol_sum;
	}

	const auto BL_order_map_max_elem = (ordered_BL_order_map.rbegin())->first;
	const auto BL_order_map_min_elem = (ordered_BL_order_map.begin())->first;
	const auto SL_order_map_max_elem = (ordered_SL_order_map.rbegin())->first;
	const auto SL_order_map_min_elem = (ordered_SL_order_map.begin())->first;

	if (!intersect(BL_order_map_max_elem,
		BL_order_map_min_elem,
		SL_order_map_max_elem,
		SL_order_map_min_elem)){
		std::cout << "No buy limit orders prices and sell limit orders prices intersection" << std::endl;
		_getch();
		return EXIT_FAILURE;
	}

	const auto max_price = std::max(BL_order_map_max_elem,
		SL_order_map_max_elem);
	const auto min_price = std::min(BL_order_map_min_elem,
		SL_order_map_min_elem);
	std::map<order_int, auc_volumes> auc_potential_prices_and_volumes;

	std::vector<order_int> price_iter(max_price - min_price + 1);
	std::iota(std::begin(price_iter), std::end(price_iter), min_price);

	for (auto it = price_iter.begin(); it != price_iter.end(); ++it) {
		auto next = it;
		next++;

		if (ordered_SL_order_map.find(*it) != ordered_SL_order_map.end()) {
			auc_potential_prices_and_volumes[*it].sell_volume =
				ordered_SL_order_map[*it];
		}
		if (next != price_iter.end()) {
			if (ordered_SL_order_map.find(*next)
				== ordered_SL_order_map.end()) {
				ordered_SL_order_map[*next] = ordered_SL_order_map[*it];
			}
		}
	}

	for (auto it = price_iter.rbegin(); it != price_iter.rend(); ++it) {
		auto next = it;
		next++;

		if (ordered_BL_order_map.find(*it) != ordered_BL_order_map.end()) {
			auc_potential_prices_and_volumes[*it].buy_volume =
				ordered_BL_order_map[*it];
		}
		if (next != price_iter.rend()) {
			if (ordered_BL_order_map.find(*next)
				== ordered_BL_order_map.end()) {
				ordered_BL_order_map[*next] = ordered_BL_order_map[*it];
			}
		}
	}

	for (auto it = price_iter.begin(); it != price_iter.end(); ++it) {
		auc_potential_prices_and_volumes[*it].limit_order_value = (std::min(
			auc_potential_prices_and_volumes[*it].buy_volume,
			auc_potential_prices_and_volumes[*it].sell_volume)) * (*it);
	}

	auto highest_auction_price_mul_volume =
		std::max_element(auc_potential_prices_and_volumes.begin(),
		auc_potential_prices_and_volumes.end(),
		[](const pair<order_int, auc_volumes>& f, const pair<order_int, auc_volumes>& s) {return f.second.limit_order_value < s.second.limit_order_value; });

	// auction

	price = highest_auction_price_mul_volume->first;

	BL_order_storage.erase(
		std::remove_if(BL_order_storage.begin(), BL_order_storage.end(),
		[](order const & x) {return x.price < price; }),
		BL_order_storage.end());
	SL_order_storage.erase(
		std::remove_if(SL_order_storage.begin(), SL_order_storage.end(),
		[](order const & x) {return x.price > price; }),
		SL_order_storage.end());

	std::sort(BM_order_storage.begin(), BM_order_storage.end(),
		M_orders_sort_for_aucton);
	std::sort(SM_order_storage.begin(), SM_order_storage.end(),
		M_orders_sort_for_aucton);

	std::sort(BL_order_storage.begin(), BL_order_storage.end(),
		BL_order_sort_for_aucton);
	std::sort(SL_order_storage.begin(), SL_order_storage.end(),
		SL_order_sort_for_aucton);

	// vectors "glue" together (maybe take a short time, but make coding simple)

	BL_order_storage.insert(BL_order_storage.end(), BM_order_storage.begin(), BM_order_storage.end());
	SL_order_storage.insert(SL_order_storage.end(), SM_order_storage.begin(), SM_order_storage.end());

	// market orders auction

	while (!(BL_order_storage.empty() || SL_order_storage.empty())) {
		auto B_head = BL_order_storage.rbegin();
		auto S_head = SL_order_storage.rbegin();

		if (B_head->volume > S_head->volume) {
			add_to_auction(
				auction_order(B_head->num, S_head->num, S_head->volume,
				S_head->volume * price), S_head->volume * price);
			B_head->volume -= S_head->volume;
			SL_order_storage.pop_back();
			continue;
		}
		else if (B_head->volume < S_head->volume) {
			add_to_auction(
				auction_order(B_head->num, S_head->num, B_head->volume,
				B_head->volume * price), B_head->volume * price);
			S_head->volume -= B_head->volume;
			BL_order_storage.pop_back();
			continue;
		}
		else {
			add_to_auction(
				auction_order(B_head->num, S_head->num, B_head->volume,
				B_head->volume * price), B_head->volume * price);
			BL_order_storage.pop_back();
			SL_order_storage.pop_back();
			continue;
		}
	}

	// time only for algoritm
	long t_auc_end = clock();
	std::cout << "Algoritm done in " << t_auc_end - t_auc_start << " ms"
		<< std::endl;

	std::string output_file;
	output_file.append(argv[0]);
	output_file.append(".csv");

	ofstream fout;
	fout.open(output_file);

	std::string generated;
	std::back_insert_iterator<std::string> sink(generated);
	if (!generate_objects(sink, auction)) {
		fout << "-------------------------" << std::endl;
		fout << "Generating failed" << std::endl;
		fout << "-------------------------" << std::endl;
	}
	else {
		fout << "OK;" << price << ";" << auction_value << std::endl;
		fout << generated;
	}

	fout.close();

	// time end
	long t_end = clock();
	std::cout << "Done in " << t_end - t_start << " ms" << std::endl;

	_getch();
	return EXIT_SUCCESS;
}
