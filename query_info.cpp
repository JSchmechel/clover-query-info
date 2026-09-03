#include "include/clover/query_info.hpp"

#include <unordered_map>
#include <unordered_set>
#include <utility>

std::map<uint32_t, Branch_Info> info_on_branches;

/* KLEE hash-conses its expressions: an Expr reached by two different parents is ONE object, not two,
 * and a query's constraints share most of their structure with each other. So the thing being walked
 * below is a DAG, and walking it as a tree - pushing every kid unconditionally - re-expands each
 * shared subexpression once per path that reaches it, which is exponential in depth.
 *
 * That is not a hypothetical. Before these traversals were memoized, a depth-48 query in
 * data/scenarios/03_budget_limited reported 7,591,543 nodes for a graph of a few hundred, and the
 * three walks below cost 148 ms - against 12 ms to actually SOLVE the same query. Because they run
 * inside Trace::findNewPath but outside its timing bracket, that cost landed in the trace as a
 * solver window no query accounted for: 0.85 s of a 1.12 s exploration, i.e. the profiler consumed
 * three quarters of the budget it was profiling and then reported the run as 95% solver time.
 *
 * Identity is the Expr pointer, which is what makes deduplication cheap and exact here.
 */
namespace {

/* Every root of a query - each constraint, plus the query expression itself - in one list, so a
 * traversal can share a single visited set across all of them. Sharing matters: the constraints
 * overlap heavily, and counting a subexpression once per query rather than once per constraint is
 * what makes `nodes` mean "how big is this query's expression graph".
 */
std::vector<const klee::Expr *> query_roots(const klee::Query &query)
{
	std::vector<const klee::Expr *> roots;
	for (auto it = query.constraints.begin(); it != query.constraints.end(); ++it)
		roots.push_back((*it).get());
	roots.push_back(query.expr.get());
	return roots;
}

} // namespace

unsigned int get_number_of_constraints(const klee::Query &query) {
	return query.constraints.size();
}

unsigned int get_number_of_variables(const klee::Query &query) {
	std::set<std::string> variables;
	std::unordered_set<const klee::Expr *> seen;

	/* The result here was always correct - `variables` is a set of names, so re-visiting a shared
	 * node could only re-insert a name it already held. Only the cost was wrong, which is why this
	 * one is a pure speedup and the other two are also corrections. */
	std::vector<const klee::Expr *> stack = query_roots(query);
	while (!stack.empty()) {
		const klee::Expr *current = stack.back();
		stack.pop_back();
		if (!seen.insert(current).second)
			continue;

		if (current->getKind() == klee::Expr::Read) {
			auto readExpr = klee::dyn_cast<klee::ReadExpr>(current);
			variables.insert(readExpr->updates.root->name);
		}
		for (unsigned i = 0; i < current->getNumKids(); i++)
			stack.push_back(current->getKid(i).get());
	}

	return variables.size();
}

unsigned int get_query_size(const klee::Query &query) {
	std::unordered_set<const klee::Expr *> seen;

	/* Counts DISTINCT nodes, which is a change in what this number means and not only in what it
	 * costs: it is now the size of the graph the solver is handed, rather than the size of that
	 * graph unfolded into a tree. Values from before this change are not comparable with values
	 * after it - they are larger by whatever the sharing factor happened to be, which on the traces
	 * to hand ranged from 2x to roughly 10,000x. */
	std::vector<const klee::Expr *> stack = query_roots(query);
	while (!stack.empty()) {
		const klee::Expr *current = stack.back();
		stack.pop_back();
		if (!seen.insert(current).second)
			continue;

		for (unsigned i = 0; i < current->getNumKids(); i++)
			stack.push_back(current->getKid(i).get());
	}

	return seen.size();
}

unsigned int get_query_depth(const klee::Query &query) {
	/* Memoizes each node's HEIGHT - the longest chain of kids below it - rather than merely marking
	 * nodes visited, because depth is not a property a visited set preserves: the same node can be
	 * reached at several depths and only the deepest arrival matters. Taking the max height over
	 * the roots gives exactly the number the previous tree walk produced, so unlike get_query_size
	 * THIS FUNCTION'S RESULT IS UNCHANGED - only its cost is.
	 *
	 * Iterative, with an explicit post-order stack, replacing a recursive lambda. Recursion here
	 * descended once per tree path rather than once per node, so its stack depth was bounded by the
	 * unfolding rather than by the graph; on a deep query that is a crash rather than a slow
	 * answer. */
	std::unordered_map<const klee::Expr *, unsigned int> height;
	std::vector<std::pair<const klee::Expr *, bool>> stack;

	unsigned int max_depth = 0;
	for (const klee::Expr *root : query_roots(query)) {
		stack.push_back({root, false});
		while (!stack.empty()) {
			auto [current, kids_done] = stack.back();
			stack.pop_back();

			if (height.count(current))
				continue;

			if (!kids_done) {
				/* Re-pushed below its kids, so it is popped again once every one of them has a
				 * height recorded. */
				stack.push_back({current, true});
				for (unsigned i = 0; i < current->getNumKids(); i++) {
					const klee::Expr *kid = current->getKid(i).get();
					if (!height.count(kid))
						stack.push_back({kid, false});
				}
				continue;
			}

			unsigned int tallest_kid = 0;
			for (unsigned i = 0; i < current->getNumKids(); i++) {
				unsigned int h = height[current->getKid(i).get()];
				if (h > tallest_kid)
					tallest_kid = h;
			}
			height[current] = tallest_kid + 1;
		}

		/* Matches the previous walk's convention, which started its roots at depth 1 rather than 0. */
		if (height[root] > max_depth)
			max_depth = height[root];
	}

	return max_depth;
}
