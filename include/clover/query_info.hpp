#pragma once

#include <klee/Expr/ArrayCache.h>
#include <klee/Expr/Assignment.h>
#include <klee/Expr/Expr.h>
#include <klee/Expr/ExprBuilder.h>
#include <klee/Solver/Solver.h>
#include <klee/Support/Casting.h>
#include <map>
#include <stdint.h>
#include <vector>

/**
 * This struct contains information about the queries sovled at a particular branch, such as the time needed to solve
 * the queries and their complexity. We may need this information for optimization purposes.
 */
struct Branch_Info {

	uint32_t address;
	unsigned int num_queries = 0;

	std::vector<float> query_solving_times_in_seconds;
	std::vector<unsigned int> num_constraints;
	std::vector<unsigned int> num_variables;
	std::vector<unsigned int> num_nodes;
	std::vector<unsigned int> depth;

};

/**
 * This map tracks the time spent solving each branch condition. The key is the address of the branch instruction. This
 * is used to identify "hot" branches that take a long time to solve, which can be useful for optimization and debugging
 * purposes.
 */
extern std::map<uint32_t, Branch_Info> info_on_branches;

unsigned int get_number_of_constraints(const klee::Query &query);
unsigned int get_number_of_variables(const klee::Query &query);
unsigned int get_query_size(const klee::Query &query);
unsigned int get_query_depth(const klee::Query &query);
