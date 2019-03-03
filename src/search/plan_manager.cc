#include "plan_manager.h"

#include "task_proxy.h"

#include "task_utils/task_properties.h"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace std;

int calculate_plan_cost(const Plan &plan, const TaskProxy &task_proxy)
{
  OperatorsProxy operators = task_proxy.get_operators();
  int plan_cost = 0;
  for (OperatorID op_id : plan)
  {
    plan_cost += operators[op_id].get_cost();
  }
  return plan_cost;
}

PlanManager::PlanManager(std::string file_name, bool dump_plan)
    : plan_filename(file_name), num_previously_generated_plans(0),
      is_part_of_anytime_portfolio(false), dump_plan(dump_plan) {}

void PlanManager::set_plan_filename(const string &plan_filename_)
{
  plan_filename = plan_filename_;
}

void PlanManager::set_num_previously_generated_plans(
    int num_previously_generated_plans_)
{
  if (num_previously_generated_plans_ < (int)found_plans.size())
  {
    found_plans.erase(found_plans.begin() + num_previously_generated_plans_,
                      found_plans.end());
  }
  num_previously_generated_plans = num_previously_generated_plans_;
}

void PlanManager::set_is_part_of_anytime_portfolio(
    bool is_part_of_anytime_portfolio_)
{
  is_part_of_anytime_portfolio = is_part_of_anytime_portfolio_;
}

void PlanManager::save_plan(const Plan &plan, const TaskProxy &task_proxy,
                            bool generates_multiple_plan_files)
{
  found_plans.push_back(plan);
  ostringstream filename;
  filename << plan_filename;
  int plan_number = num_previously_generated_plans + 1;
  if (generates_multiple_plan_files || is_part_of_anytime_portfolio)
  {
    filename << "." << plan_number;
  }
  else
  {
    assert(plan_number == 1);
  }
  ofstream outfile(filename.str());
  if (outfile.rdstate() & ofstream::failbit)
  {
    cerr << "Failed to open plan file: " << filename.str() << endl;
    utils::exit_with(utils::ExitCode::SEARCH_INPUT_ERROR);
  }
  OperatorsProxy operators = task_proxy.get_operators();
  for (OperatorID op_id : plan)
  {
    if (dump_plan)
    {
      cout << operators[op_id].get_name() << " (" << operators[op_id].get_cost()
           << ")" << endl;
    }
    outfile << "(" << operators[op_id].get_name() << ")" << endl;
  }
  int plan_cost = calculate_plan_cost(plan, task_proxy);
  bool is_unit_cost = task_properties::is_unit_cost(task_proxy);
  outfile << "; cost = " << plan_cost << " ("
          << (is_unit_cost ? "unit cost" : "general cost") << ")" << endl;
  outfile.close();
  if (dump_plan)
  {
    cout << "Plan length: " << plan.size() << " step(s)." << endl;
    cout << "Plan cost: " << plan_cost << endl;
  }
  ++num_previously_generated_plans;
}