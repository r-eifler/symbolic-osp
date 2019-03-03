#include "original_state_space.h"

#include "../abstract_task.h"
#include "../mutex_group.h"
#include "debug_macros.h"

#include <algorithm>
#include <limits>

using namespace std;

namespace symbolic {
OriginalStateSpace::OriginalStateSpace(SymVariables *v,
                                       const SymParamsMgr &params,
                                       std::shared_ptr<PlanManager> plan_mgr)
    : SymStateSpaceManager(v, params, plan_mgr) {
  initialState =
      vars->getStateBDD(tasks::g_root_task->get_initial_state_values());
  std::vector<std::pair<int, int>> goal_facts;
  for (int i = 0; i < tasks::g_root_task->get_num_goals(); i++) {
    auto fact = tasks::g_root_task->get_goal_fact(i);
    goal_facts.emplace_back(fact.var, fact.value);
  }
  goal = vars->getPartialStateBDD(goal_facts);

  init_mutex(tasks::g_root_task->get_mutex_groups());

// Check wether we compute this in parallel
#ifdef DDLIBSYLVAN
  if (Bdd::is_parallel_search()) {
    create_single_trs_parallel();
  } else {
    create_single_trs_sequential();
  }
#else
  create_single_trs_sequential();
#endif

  init_transitions(indTRs);
}

void OriginalStateSpace::create_single_trs_sequential() {
  for (int i = 0; i < tasks::g_root_task->get_num_operators(); i++) {
    int cost = tasks::g_root_task->get_operator_cost(i, false);
    // cout << "Creating TR of op " << i << " of cost " << cost << endl;
    DEBUG_MSG(cout << "Creating TR of op " << i << " of cost " << cost
                   << endl;);
    indTRs[cost].push_back(TransitionRelation(vars, OperatorID(i), cost));
    indTRs[cost].back().init();
    if (p.mutex_type == MutexType::MUTEX_EDELETION) {
      indTRs[cost].back().edeletion(notMutexBDDsByFluentFw,
                                    notMutexBDDsByFluentBw,
                                    exactlyOneBDDsByFluent);
    }
  }
}

void OriginalStateSpace::create_single_trs_parallel() {
  // First we reserve the space sequencially
  for (int i = 0; i < tasks::g_root_task->get_num_operators(); i++) {
    int cost = tasks::g_root_task->get_operator_cost(i, false);
    // cout << "Creating TR of op " << i << " of cost " << cost << endl;
    DEBUG_MSG(cout << "Init TR of op " << i << " of cost " << cost << endl;);
    indTRs[cost].push_back(TransitionRelation(vars, OperatorID(i), cost));
  }
  // Now we build the trs by costs!
  for (const auto &pair : indTRs) {
    int cost = pair.first;
    create_single_trs_parallel_recursive(indTRs[cost], 0, indTRs[cost].size());
  }
}

void OriginalStateSpace::create_single_trs_parallel_recursive(
    std::vector<TransitionRelation> &trs, int i, int k) {
  if (k == 1) {
    trs.at(i).init();
    if (p.mutex_type == MutexType::MUTEX_EDELETION) {
      trs.at(i).edeletion(notMutexBDDsByFluentFw, notMutexBDDsByFluentBw,
                          exactlyOneBDDsByFluent);
    }
  } else {
#ifdef DDLIBSYLVAN
    LACE_ME
#endif
    create_single_trs_parallel_recursive(trs, i, k / 2);
    create_single_trs_parallel_recursive(trs, i + (k / 2), k - (k / 2));
  }
}

void OriginalStateSpace::init_mutex(
    const std::vector<MutexGroup> &mutex_groups) {
  // If (a) is initialized OR not using mutex OR edeletion does not need mutex
  if (p.mutex_type == MutexType::MUTEX_NOT) return;  // Skip mutex
                                                     // initialization

  bool genMutexBDD = true;
  bool genMutexBDDByFluent = (p.mutex_type == MutexType::MUTEX_EDELETION);

  if (genMutexBDDByFluent) {
    // Initialize structure for exactlyOneBDDsByFluent (common to both
    // init_mutex calls)
    exactlyOneBDDsByFluent.resize(tasks::g_root_task->get_num_variables());
    for (int i = 0; i < tasks::g_root_task->get_num_variables(); ++i) {
      exactlyOneBDDsByFluent[i].resize(
          tasks::g_root_task->get_variable_domain_size(i));
      for (int j = 0; j < tasks::g_root_task->get_variable_domain_size(i);
           ++j) {
        exactlyOneBDDsByFluent[i][j] = oneBDD();
      }
    }
  }
  init_mutex(mutex_groups, genMutexBDD, genMutexBDDByFluent, false);
  init_mutex(mutex_groups, genMutexBDD, genMutexBDDByFluent, true);
}
void OriginalStateSpace::init_mutex(const std::vector<MutexGroup> &mutex_groups,
                                    bool genMutexBDD, bool genMutexBDDByFluent,
                                    bool fw) {
  DEBUG_MSG(cout << "Init mutex BDDs " << (fw ? "fw" : "bw") << ": "
                 << genMutexBDD << " " << genMutexBDDByFluent << endl;);

  vector<vector<Bdd>> &notMutexBDDsByFluent =
      (fw ? notMutexBDDsByFluentFw : notMutexBDDsByFluentBw);

  vector<Bdd> &notMutexBDDs = (fw ? notMutexBDDsFw : notMutexBDDsBw);

  // BDD validStates = vars->oneBDD();
  int num_mutex = 0;
  int num_invariants = 0;

  if (genMutexBDDByFluent) {
    // Initialize structure for notMutexBDDsByFluent
    notMutexBDDsByFluent.resize(tasks::g_root_task->get_num_variables());
    for (int i = 0; i < tasks::g_root_task->get_num_variables(); ++i) {
      notMutexBDDsByFluent[i].resize(
          tasks::g_root_task->get_variable_domain_size(i));
      for (int j = 0; j < tasks::g_root_task->get_variable_domain_size(i);
           ++j) {
        notMutexBDDsByFluent[i][j] = oneBDD();
      }
    }
  }

  // Initialize mBDDByVar and invariant_bdds_by_fluent
  vector<Bdd> mBDDByVar;
  mBDDByVar.reserve(tasks::g_root_task->get_num_variables());
  vector<vector<Bdd>> invariant_bdds_by_fluent(
      tasks::g_root_task->get_num_variables());
  for (size_t i = 0; i < invariant_bdds_by_fluent.size(); i++) {
    mBDDByVar.push_back(oneBDD());
    invariant_bdds_by_fluent[i].resize(
        tasks::g_root_task->get_variable_domain_size(i));
    for (size_t j = 0; j < invariant_bdds_by_fluent[i].size(); j++) {
      invariant_bdds_by_fluent[i][j] = oneBDD();
    }
  }

  for (auto &mg : mutex_groups) {
    if (mg.pruneFW() != fw) continue;
    const vector<FactPair> &invariant_group = mg.getFacts();
    DEBUG_MSG(cout << mg << endl;);
    if (mg.isExactlyOne()) {
      Bdd bddInvariant = zeroBDD();
      int var = numeric_limits<int>::max();
      int val = 0;
      bool exactlyOneRelevant = true;

      for (auto &fluent : invariant_group) {
        bddInvariant += vars->preBDD(fluent.var, fluent.value);
        if (fluent.var < var) {
          var = fluent.var;
          val = fluent.value;
        }
      }

      if (exactlyOneRelevant) {
        num_invariants++;
        if (genMutexBDD) {
          invariant_bdds_by_fluent[var][val] *= bddInvariant;
        }
        if (genMutexBDDByFluent) {
          for (auto &fluent : invariant_group) {
            exactlyOneBDDsByFluent[fluent.var][fluent.value] *= bddInvariant;
          }
        }
      }
    }

    for (size_t i = 0; i < invariant_group.size(); ++i) {
      int var1 = invariant_group[i].var;
      int val1 = invariant_group[i].value;
      Bdd f1 = vars->preBDD(var1, val1);

      for (size_t j = i + 1; j < invariant_group.size(); ++j) {
        int var2 = invariant_group[j].var;
        int val2 = invariant_group[j].value;
        Bdd f2 = vars->preBDD(var2, val2);
        Bdd mBDD = !(f1 * f2);
        if (genMutexBDD) {
          num_mutex++;
          mBDDByVar[min(var1, var2)] *= mBDD;
          if (mBDDByVar[min(var1, var2)].nodeCount() > p.max_mutex_size) {
            notMutexBDDs.push_back(mBDDByVar[min(var1, var2)]);
            mBDDByVar[min(var1, var2)] = vars->oneBDD();
          }
        }
        if (genMutexBDDByFluent) {
          notMutexBDDsByFluent[var1][val1] *= mBDD;
          notMutexBDDsByFluent[var2][val2] *= mBDD;
        }
      }
    }
  }

  if (genMutexBDD) {
    for (int var = 0; var < tasks::g_root_task->get_num_variables(); ++var) {
      if (!mBDDByVar[var].IsOne()) {
        notMutexBDDs.push_back(mBDDByVar[var]);
      }
      for (const Bdd &bdd_inv : invariant_bdds_by_fluent[var]) {
        if (!bdd_inv.IsOne()) {
          notMutexBDDs.push_back(bdd_inv);
        }
      }
    }

    DEBUG_MSG(dumpMutexBDDs(fw););
    merge(vars, notMutexBDDs, mergeAndBDD, p.max_mutex_time, p.max_mutex_size);
    std::reverse(notMutexBDDs.begin(), notMutexBDDs.end());
    DEBUG_MSG(cout << "Mutex initialized " << (fw ? "fw" : "bw")
                   << ". Total mutex added: " << num_mutex
                   << " Invariant groups: " << num_invariants << endl;);
    DEBUG_MSG(dumpMutexBDDs(fw););
  }
}

}  // namespace symbolic