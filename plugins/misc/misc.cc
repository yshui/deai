#include <format>
#include <functional>
#include <queue>

#include <deai/c++/deai.hh>

using namespace deai;

namespace {

struct Module {
	static constexpr const char type[] = "deai.plugin.utils:Module";
	type::ObjectBase base;
	/// Find maximum unweighted bipartite match.
	///
	/// EXPORT: misc.bipartite_match(graph: [[:int]]): [:int]
	///
	/// The input is a list of edges. There is one list for each node on the left,
	/// containing the indices of the nodes on the right that it is connected to.
	///
	/// Returns a list of integers, the indices of the nodes on the right that each
	/// node on the left is matched to. Or -1 if it is not matched
	auto
	bipartite_match(const std::vector<std::vector<int64_t>> &graph) const -> std::vector<int64_t> {
		std::vector<int64_t> ret(graph.size(), -1);
		std::vector<int64_t> right;
		std::vector<uint8_t> visited(graph.size(), 0);
		for (unsigned int i = 0; i < graph.size(); i++) {
			for (const auto &j : graph[i]) {
				if (j < 0) {
					throw std::invalid_argument("Invalid graph, index out of bounds");
				}
				if (uint64_t(j) >= right.size()) {
					right.resize(j + 1, -1);
				}
			}
		}

		std::function<bool(unsigned int)> dfs = [&](unsigned int curr) -> bool {
			for (const auto &j : graph[curr]) {
				if (visited[j] == 1) {
					continue;
				}
				visited[j] = 1;
				if (right[j] == -1 || dfs(right[j])) {
					ret[curr] = j;
					right[j] = curr;
					return true;
				}
			}
			return false;
		};
		for (unsigned int i = 0; i < graph.size(); i++) {
			std::fill(visited.begin(), visited.end(), 0);
			dfs(i);
		}
		return ret;
	}

	/// Solve a system of difference constraints.
	///
	/// EXPORT: misc.difference_constraints(constraints: [[:int]]): [:int]
	///
	/// The input is a list of constraints, which are formatted as a array of
	/// arrays of integers, array of integers at index :math:`i` describes the
	/// constraints for :math:`x_i`. Each constraint is a pair of integers, the
	/// first being the index of the other variable, and the second being the
	/// difference between the two variables. e.g. if the first is :math:`j` and
	/// the second is :math:`k`, then the constraint is :math:`x_j - x_i <= k`,
	/// note the order of the variables.
	///
	/// Returns a list of integers, the values of the variables that satisfy the
	/// constraints.
	auto difference_constraints(const std::vector<std::vector<int64_t>> &constraints_list) const
	    -> std::vector<int64_t> {
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
			for (size_t j = 0; j < constraints_list[i].size(); j += 2) {
				if (constraints_list[i][j] >= int64_t(constraints_list.size()) ||
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

		for (unsigned int iter = (constraints_list.size() - 1) * nedges;
		     iter > 0 && !queue.empty();) {
			unsigned int i = queue.front();
			queue.pop();
			is_inqueue[i] = 0;
			const auto &constraints = constraints_list[i];
			for (unsigned int c = 0; iter > 0 && c < constraints.size(); c += 2) {
				unsigned int j = constraints[c];
				int64_t k = constraints[c + 1];
				iter--;
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
}        // namespace

namespace {
/// misc
///
/// EXPORT: misc: deai:module
///
/// Collection of tools that don't fit anywhere else.
auto di_new_utils(Ref<Core> &di) -> Ref<Module> {
	auto module = util::new_object<Module>();
	util::add_method<&Module::difference_constraints>(module, "difference_constraints");
	util::add_method<&Module::bipartite_match>(module, "bipartite_match");
	return module;
}
DEAI_CPP_PLUGIN_ENTRY_POINT(di) {
	auto obj = di_new_utils(di);
	static_cast<void>(di->register_module("misc", std::move(obj).cast()));
}
}        // namespace
