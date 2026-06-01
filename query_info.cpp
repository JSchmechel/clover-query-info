#include "clover/query_info.hpp"

std::map<uint32_t, Branch_Info> info_on_branches;

unsigned int get_number_of_constraints(const klee::Query &query) {
	return query.constraints.size();
}

unsigned int get_number_of_variables(const klee::Query &query) {
	std::set<std::string> variables;

	// Helper lambda to traverse an expression tree and collect variable names.
	auto collect_vars = [&variables](const klee::ref<klee::Expr> &expr) {
		std::vector<klee::ref<klee::Expr>> stack;
		stack.push_back(expr);
		while (!stack.empty()) {
			auto current = stack.back();
			stack.pop_back();
			if (current->getKind() == klee::Expr::Read) {
				auto readExpr = klee::dyn_cast<klee::ReadExpr>(current);
				variables.insert(readExpr->updates.root->name);
			}
			for (unsigned i = 0; i < current->getNumKids(); i++) {
				stack.push_back(current->getKid(i));
			}
		}
	};

	// Collect from all constraints.
	for (auto it = query.constraints.begin(); it != query.constraints.end(); ++it) {
		collect_vars(*it);
	}
	// Collect from the main query expression.
	collect_vars(query.expr);

	return variables.size();
}

unsigned int get_query_size(const klee::Query &query) {
	unsigned int size = 0;

	// Helper lambda to traverse an expression tree and count nodes.
	auto count_nodes = [&size](const klee::ref<klee::Expr> &expr) {
		std::vector<klee::ref<klee::Expr>> stack;
		stack.push_back(expr);
		while (!stack.empty()) {
			auto current = stack.back();
			stack.pop_back();
			size++;
			for (unsigned i = 0; i < current->getNumKids(); i++) {
				stack.push_back(current->getKid(i));
			}
		}
	};

	// Count nodes in all constraints.
	for (auto it = query.constraints.begin(); it != query.constraints.end(); ++it) {
		count_nodes(*it);
	}
	// Count nodes in the main query expression.
	count_nodes(query.expr);

	return size;
}

unsigned int get_query_depth(const klee::Query &query) {
	unsigned int max_depth = 0;

	// Helper lambda to traverse an expression tree and track depth.
	std::function<void(const klee::ref<klee::Expr> &, unsigned int)> track_depth = [&](const klee::ref<klee::Expr> &expr, unsigned int current_depth) {
		if (current_depth > max_depth) {
			max_depth = current_depth;
		}
		for (unsigned i = 0; i < expr->getNumKids(); i++) {
			track_depth(expr->getKid(i), current_depth + 1);
		}
	};

	// Track depth in all constraints.
	for (auto it = query.constraints.begin(); it != query.constraints.end(); ++it) {
		track_depth(*it, 1);
	}
	// Track depth in the main query expression.
	track_depth(query.expr, 1);

	return max_depth;
}