#ifndef __FUNCNODE_H__
#define __FUNCNODE_H__

#include "action.h"
#include "funcinst.h"
#include "hashtable.h"
#include "hashset.h"
#include "predicate.h"
#include "history.h"

typedef ModelList<FuncInst *> func_inst_list_mt;
typedef HashTable<void *, uint64_t, uintptr_t, 4> read_map_t;

class FuncNode {
public:
	FuncNode(ModelHistory * history);
	~FuncNode();

	uint32_t get_func_id() { return func_id; }
	const char * get_func_name() { return func_name; }
	void set_func_id(uint32_t id) { func_id = id; }
	void set_func_name(const char * name) { func_name = name; }
	void set_new_exec_flag();

	void add_inst(ModelAction *act);
	FuncInst * get_inst(ModelAction *act);

	HashTable<const char *, FuncInst *, uintptr_t, 4, model_malloc, model_calloc, model_free> * getFuncInstMap() { return &func_inst_map; }
	func_inst_list_mt * get_inst_list() { return &inst_list; }
	func_inst_list_mt * get_entry_insts() { return &entry_insts; }
	void add_entry_inst(FuncInst * inst);

	void update_tree(action_list_t * act_list);
	void update_inst_tree(func_inst_list_t * inst_list);

	void store_read(ModelAction * act, uint32_t tid);
	uint64_t query_last_read(void * location, uint32_t tid);
	void clear_read_map(uint32_t tid);

	/* TODO: generate EQUALITY or NULLITY predicate based on write_history in history.cc */
	void update_predicate_tree(action_list_t * act_list);
	void deep_update(Predicate * pred);
	bool follow_branch(Predicate ** curr_pred, FuncInst * next_inst, ModelAction * next_act, HashTable<FuncInst *, ModelAction *, uintptr_t, 0>* inst_act_map, SnapVector<Predicate *> * unset_predicates);
	void gen_predicate_and_follow(Predicate ** curr_pred, FuncInst * next_inst, ModelAction * next_act, HashTable<void *, ModelAction *, uintptr_t, 0> * loc_act_map, token_t token, void * loc);

	void incr_exit_count() { exit_count++; }
	uint32_t get_exit_count() { return exit_count; }

	ModelList<action_list_t *> * get_action_list_buffer() { return &action_list_buffer; }

	void add_to_val_loc_map(uint64_t val, void * loc);
	void add_to_val_loc_map(value_set_t * values, void * loc);
	void update_loc_may_equal_map(void * new_loc, loc_set_t * old_locations);

	void print_predicate_tree();
	void print_val_loc_map();
	void print_last_read(uint32_t tid);

	MEMALLOC
private:
	uint32_t func_id;
	const char * func_name;
	ModelHistory * history;
	bool predicate_tree_initialized;
	Predicate * predicate_tree_entry;	// a dummy node whose children are the real entries

	uint32_t exit_count;

	/* Use source line number as the key of hashtable, to check if
	 * atomic operation with this line number has been added or not
	 */
	HashTable<const char *, FuncInst *, uintptr_t, 4, model_malloc, model_calloc, model_free> func_inst_map;

	/* List of all atomic actions in this function */
	func_inst_list_mt inst_list;

	/* Possible entry atomic actions in this function */
	func_inst_list_mt entry_insts;

	/* Store the values read by atomic read actions per memory location for each thread */
	//ModelVector<read_map_t *> thrd_read_map;

	/* Store action_lists when calls to update_tree are deferred */
	ModelList<action_list_t *> action_list_buffer;

	/* read_locations: set of locations read by this FuncNode
	 * val_loc_map: keep track of locations that have the same values written to;
	 * loc_may_equal_map: deduced from val_loc_map;
	 */

	loc_set_t * read_locations;
	HashTable<uint64_t, loc_set_t *, uint64_t, 0> * val_loc_map;
	HashTable<void *, loc_set_t *, uintptr_t, 0> * loc_may_equal_map;
	value_set_t * values_may_read_from;
};

#endif /* __FUNCNODE_H__ */
