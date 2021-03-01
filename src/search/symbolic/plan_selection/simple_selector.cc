#include "simple_selector.h"

#include "../../task_utils/task_properties.h"
#include <iostream>
#include <stdio.h>

namespace symbolic {

SimpleSelector::SimpleSelector(const options::Options &opts)
    : PlanDataBase(opts) {
  PlanDataBase::anytime_completness = true;
}

void SimpleSelector::add_plan(const Plan &plan) {
  if (!has_accepted_plan(plan)) {
    if (is_simple(plan))
      save_accepted_plan(plan);
    else
      save_rejected_plan(plan);
  }
}

/**
 * For simple planning:
 *      Return true iff the plan is simple.
 */
bool SimpleSelector::is_simple(const Plan &plan) {
  shared_ptr<StateRegistry> registry = sym_vars->get_state_registry();
  TaskProxy task = registry->get_task_proxy();
  OperatorsProxy operators = task.get_operators();

  GlobalState cur = registry->get_initial_state();
  unordered_set<int> visited_states = {cur.get_id().get_value()};

  for (size_t i = 0; i < plan.size(); i++) {
    cur = registry->get_successor_state(cur, operators[plan[i]]);

    auto ret = visited_states.insert(cur.get_id().get_value());
    if (!ret.second)
      return false;
  }
  return true;
}

static std::shared_ptr<PlanDataBase> _parse(OptionParser &parser) {
  PlanDataBase::add_options_to_parser(parser);

  Options opts = parser.parse();
  if (parser.dry_run())
    return nullptr;
  return std::make_shared<SimpleSelector>(opts);
}

static Plugin<PlanDataBase> _plugin("simple", _parse);

} // namespace symbolic
