#include <format>
#include <functional>
#include <iostream>
#include <queue>

#include <deai/c++/deai.hh>

using namespace deai::c_api;
using namespace deai;

namespace {

class Module {
public:
	static constexpr const char *type [[maybe_unused]] = "deai.plugin.utils:Module";

	/// Solve a system of difference constraints.
	///
	/// EXPORT: utils.difference_constraints(constraints: [[:int]]) -> [:int]
	///
	/// The input is a list of constraints, which are formatted as a array of
	/// arrays of integers, array of integers at index :math:`i` describes the
	/// constraints for :math:`x_i`. Each constraint is a pair of integers, the
	/// first being the index of the other variable, and the second being the
	/// difference between the two variables. e.g. if the first is :math:`j` and
	/// the second is :math:`k`, then the constraint is :math:`x_j - x_i <= k`,
	/// note the order of the variables.
	///
	///	Returns a list of integers, the values of the variables that satisfy the
	///	constraints.
	auto difference_constraints(const std::vector<std::vector<int64_t>> &constraints_list) const
	    -> std::vector<int64_t> {
		for (const auto &constraint : constraints_list) {
			for (const auto &value : constraint) {
				std::cout << value << " ";
			}
			std::cout << "\n";
		}
		std::vector<int64_t> ret(constraints_list.size(), 0);
		std::vector<uint8_t> is_inqueue(constraints_list.size(), 1);
		std::queue<unsigned int> queue;
		unsigned int nedges = 0;
		for (unsigned int i = 0; i < constraints_list.size(); i++) {
			queue.push(i);
			if (constraints_list[i].size() % 2 != 0) {
				throw std::invalid_argument(
				    "Invalid constraints, must be pairs of integers");
			}
			nedges += constraints_list[i].size() / 2;
			for (int j = 0; j < constraints_list[i].size(); j += 2) {
				if (constraints_list[i][j] >= constraints_list.size() ||
				    constraints_list[i][j] < 0) {
					throw std::invalid_argument(
					    "Invalid constraints, index out of bounds");
				}
				if (constraints_list[i][j] == i && constraints_list[i][j + 1] < 0) {
					throw std::invalid_argument(std::format(
					    "Invalid constraints: x_{} - x_{} <= {} is impossible", i, i,
					    constraints_list[i][j + 1]));
				}
			}
		}

		// Add an extra #nodes iterations to max_iter, because first we need to go
		// over all the nodes, even those without any constraints.
		const unsigned int max_iter =
		    (constraints_list.size() - 1) * nedges + constraints_list.size();

		for (unsigned int iter = 0; iter < max_iter && !queue.empty(); iter++) {
			unsigned int i = queue.front();
			queue.pop();
			is_inqueue[i] = 0;
			const auto &constraints = constraints_list[i];
			for (unsigned int c = 0; c < constraints.size(); c += 2) {
				unsigned int j = constraints[c];
				int64_t k = constraints[c + 1];
				// x_j - x_i <= k
				if (ret[j] > ret[i] + k) {
					ret[j] = ret[i] + k;
					if (is_inqueue[j] == 0) {
						queue.push(j);
						is_inqueue[j] = 1;
					}
				}
			}
		}
		for (unsigned int i = 0; i < constraints_list.size(); i++) {
			for (unsigned int c = 0; c < constraints_list[i].size(); c += 2) {
				unsigned int j = constraints_list[i][c];
				int64_t k = constraints_list[i][c + 1];
				if (ret[i] + k < ret[j]) {
					throw std::invalid_argument("Failed to satisfy all constraints");
				}
			}
		}
		return ret;
	}
};

/// utils
///
/// EXPORT: utils: deai:module
///
/// Collection of tools that don't fit anywhere else.
auto di_new_utils(Ref<Core> &di) -> Ref<Object> {
	auto obj = util::new_object<Module>();
	auto &module = util::unsafe_to_inner<Module>(obj);
	util::add_method<&Module::difference_constraints>(module, "difference_constraints");
	return obj;
}
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = di_new_utils(di);
	static_cast<void>(di->register_module("misc", obj));
	return 0;
}
}        // namespace
