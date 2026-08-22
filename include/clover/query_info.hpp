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
 * One SMT solver query: what it cost and how complex it was, plus which executed branch it is about.
 *
 * There is exactly one query per node of the path-condition tree - a node is only ever negated once
 * (Node::randomUnnegated skips nodes whose Branch::wasNegated is set, and Trace::newQuery sets it) -
 * so a query identifies one specific branch *occurrence*, not merely a branch address. Several
 * queries can therefore share an address while describing different executions of it, which is the
 * whole reason this is recorded per query.
 */
struct Query_Info {

	/**
	 * The branch occurrence this query is about, as (run, step).
	 *
	 * IMPORTANT: this is the run in which the negated branch condition was FIRST executed - the run
	 * that discovered it - and NOT the run this query goes on to create. A query negating a branch
	 * first seen in run 3 may well produce run 9. The distinction matters because only the former
	 * identifies an event that actually exists in the trace.
	 *
	 * step is the ISS's total_num_instr, which RESTARTS AT ZERO for every run, so it is only unique
	 * when paired with run_id. Both are stamped once, when the tree node is created (see
	 * Trace::add's isPlaceholder branch); later runs re-traverse the same node without overwriting
	 * them, which is exactly what makes this the first execution rather than an arbitrary one.
	 */
	uint32_t run_id;
	uint64_t step;

	/**
	 * The run the exploration loop had just finished when this query was issued - i.e. WHICH GAP
	 * PAID FOR IT.
	 *
	 * NOT a duplicate of run_id above, and must not be merged with it. run_id says which branch
	 * occurrence the query is ABOUT; this says when the cost was incurred. They coincide only for a
	 * query that negates a branch discovered by the run that just ended and succeeds immediately.
	 * findNewPath keeps querying until it gets a satisfiable assignment, so one gap routinely pays
	 * for several queries about branches discovered in several different, much earlier runs.
	 *
	 * Without this the per-query seconds cannot be placed on a timeline at all: the trace records
	 * what every query cost and nothing about when it was spent.
	 */
	uint32_t issued_in_run;

	/**
	 * Whether the solver returned an assignment. False means this query proved a branch infeasible:
	 * real wall-clock time that produced no new path.
	 *
	 * Exactly one query per gap is satisfiable - the one that ends findNewPath's loop and creates
	 * the next run - except in the final gap, which exhausts the tree and has none. Everything else
	 * is search overhead, and separating it out is the whole point of recording this.
	 */
	bool sat;

	float seconds;
	unsigned int constraints;
	unsigned int variables;
	unsigned int nodes;
	unsigned int depth;

};

/**
 * The queries solved at one particular branch address.
 *
 * Deliberately holds no address of its own: info_on_branches is keyed by address, so a copy here
 * could only ever drift from the key. It holds no query count either - that is queries.size().
 * A single vector of structs rather than one parallel vector per field, so a field cannot be pushed
 * out of step with its neighbours.
 */
struct Branch_Info {

	std::vector<Query_Info> queries;

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
