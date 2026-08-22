#include <queue>

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include <clover/clover.h>
#include <klee/Expr/Constraints.h>
#include <klee/Expr/ExprUtil.h>

#include "fns.h"
#include "include/clover/query_info.hpp"

using namespace clover;

Trace::Trace(Solver &_solver)
    : solver(_solver)
{
	pathCondsRoot = new Node;
	pathCondsCurrent = nullptr;
}

Trace::~Trace(void)
{
	std::queue<Node *> nodes;

	nodes.push(pathCondsRoot);
	while (!nodes.empty()) {
		Node *node = nodes.front();
		nodes.pop();

		if (node->true_branch)
			nodes.push(node->true_branch);
		if (node->false_branch)
			nodes.push(node->false_branch);

		delete node;
	}
}

void
Trace::reset(void)
{
	pathCondsCurrent = nullptr;
}

void
Trace::add(bool condition, std::shared_ptr<BitVector> bv, uint32_t pc,
           uint32_t run_id, uint64_t step)
{
	auto c = (condition) ? bv->eqTrue() : bv->eqFalse();

	Node *node = nullptr;
	if (pathCondsCurrent != nullptr) {
		node = pathCondsCurrent;
	} else {
		node = pathCondsRoot;
	}

	assert(node);
	// Only the run that first reaches this prefix creates the node, so run_id/step end up naming
	// that first execution. Every later run replaying the shared prefix arrives here with the node
	// already populated and leaves it alone - which is what makes the stamped occurrence stable.
	if (node->isPlaceholder())
		node->value = std::make_shared<Branch>(bv, false, pc, run_id, step);

	if (condition) {
		if (!node->true_branch)
			node->true_branch = new Node;
		pathCondsCurrent = node->true_branch;
	} else {
		if (!node->false_branch)
			node->false_branch = new Node;
		pathCondsCurrent = node->false_branch;
	}
}

klee::Query
Trace::newQuery(klee::ConstraintSet &cs, Path &path)
{
	size_t query_idx = path.size() - 1;
	auto cm = klee::ConstraintManager(cs);

	for (size_t i = 0; i < path.size(); i++) {
		auto branch = path.at(i).first;
		auto cond = path.at(i).second;

		auto bv = branch->bv;
		auto bvcond = (cond) ? bv->eqTrue() : bv->eqFalse();

		if (i < query_idx) {
			cm.addConstraint(bvcond->expr);
			continue;
		}

		auto expr = cm.simplifyExpr(cs, bvcond->expr);

		// This is the last expression on the path. By negating
		// it we can potentially discover a new path.
		branch->wasNegated = true;
		return klee::Query(cs, expr).negateExpr();
	}

	throw "unreachable";
}

std::optional<klee::Assignment>
Trace::findNewPath(uint32_t current_run)
{
	std::optional<klee::Assignment> assign;

	do {
		klee::ConstraintSet cs;

		Path path;
		if (!pathCondsRoot->randomUnnegated(path))
			return std::nullopt; /* all branches exhausted */

		auto query = newQuery(cs, path);
		auto start = std::chrono::high_resolution_clock::now();
		assign = solver.getAssignment(query);
		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<float> solving_time = end - start;

		// Storing statistics. The negated branch is the last element of the path, and it carries
		// the (run, step) of the execution that created its node - which is what ties this query
		// to a specific event rather than merely to an address.
		auto &branch = *path.back().first;
		info_on_branches[branch.addr].queries.push_back(Query_Info{
		    branch.run_id,
		    branch.step,
		    // Where the cost was PAID, as opposed to which branch it was about. This loop runs
		    // until a query comes back satisfiable, so every iteration but the last is time spent
		    // proving some branch infeasible - and every one of them is charged to this same gap
		    // even though the branches were discovered in unrelated, earlier runs.
		    current_run,
		    assign.has_value(),
		    solving_time.count(),
		    get_number_of_constraints(query),
		    get_number_of_variables(query),
		    get_query_size(query),
		    get_query_depth(query),
		});
	} while (!assign.has_value()); /* loop until we found a sat assignment */

	assert(assign.has_value());
	return assign;
}

ConcreteStore
Trace::getStore(const klee::Assignment &assign)
{
	ConcreteStore store;
	for (auto const &b : assign.bindings) {
		auto array = b.first;
		auto value = b.second;

		std::string name = array->getName();
		store[name] = intFromVector(value);
	}

	return store;
}
