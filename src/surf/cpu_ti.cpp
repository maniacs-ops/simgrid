/* Copyright (c) 2013-2015. The SimGrid Team.
 * All rights reserved.                                                     */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "cpu_ti.hpp"
#include "xbt/heap.h"
#include "src/surf/trace_mgr.hpp"

#ifndef SURF_MODEL_CPUTI_H_
#define SURF_MODEL_CPUTI_H_

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(surf_cpu_ti, surf_cpu, "Logging specific to the SURF CPU TRACE INTEGRATION module");

namespace simgrid {
namespace surf {

static inline
void cpu_ti_action_update_index_heap(void *action, int i)
{
  (static_cast<simgrid::surf::CpuTiAction*>(action))->updateIndexHeap(i);
}

/*********
 * Trace *
 *********/

CpuTiTrace::CpuTiTrace(tmgr_trace_t speedTrace)
{
  double integral = 0;
  double time = 0;
  int i = 0;
  nbPoints_ = speedTrace->event_list.size() + 1;
  timePoints_ = new double[nbPoints_];
  integral_ =  new double[nbPoints_];
  for (auto val : speedTrace->event_list) {
    timePoints_[i] = time;
    integral_[i] = integral;
    integral += val.delta * val.value;
    time += val.delta;
    i++;
  }
  timePoints_[i] = time;
  integral_[i] = integral;
}

CpuTiTrace::~CpuTiTrace()
{
  delete [] timePoints_;
  delete [] integral_;
}

CpuTiTgmr::~CpuTiTgmr()
{
  if (trace_)
    delete trace_;
}

/**
* \brief Integrate trace
*
* Wrapper around surf_cpu_integrate_trace_simple() to get
* the cyclic effect.
*
* \param a      Begin of interval
* \param b      End of interval
* \return the integrate value. -1 if an error occurs.
*/
double CpuTiTgmr::integrate(double a, double b)
{
  double first_chunk;
  double middle_chunk;
  double last_chunk;
  int a_index;
  int b_index;

  if ((a < 0.0) || (a > b)) {
    xbt_die("Error, invalid integration interval [%.2f,%.2f]. "
        "You probably have a task executing with negative computation amount. Check your code.", a, b);
  }
  if (fabs(a -b) < EPSILON)
    return 0.0;

  if (type_ == TRACE_FIXED) {
    return ((b - a) * value_);
  }

  if (fabs(ceil(a / lastTime_) - a / lastTime_) < EPSILON)
    a_index = 1 + static_cast<int>(ceil(a / lastTime_));
  else
    a_index = static_cast<int> (ceil(a / lastTime_));

  b_index = static_cast<int> (floor(b / lastTime_));

  if (a_index > b_index) {      /* Same chunk */
    return trace_->integrateSimple(a - (a_index - 1) * lastTime_, b - (b_index) * lastTime_);
  }

  first_chunk = trace_->integrateSimple(a - (a_index - 1) * lastTime_, lastTime_);
  middle_chunk = (b_index - a_index) * total_;
  last_chunk = trace_->integrateSimple(0.0, b - (b_index) * lastTime_);

  XBT_DEBUG("first_chunk=%.2f  middle_chunk=%.2f  last_chunk=%.2f\n", first_chunk, middle_chunk, last_chunk);

  return (first_chunk + middle_chunk + last_chunk);
}

/**
 * \brief Auxiliary function to compute the integral between a and b.
 *     It simply computes the integrals at point a and b and returns the difference between them.
 * \param a  Initial point
 * \param b  Final point
*/
double CpuTiTrace::integrateSimple(double a, double b)
{
  return integrateSimplePoint(b) - integrateSimplePoint(a);
}

/**
 * \brief Auxiliary function to compute the integral at point a.
 * \param a        point
 */
double CpuTiTrace::integrateSimplePoint(double a)
{
  double integral = 0;
  int ind;
  double a_aux = a;
  ind = binarySearch(timePoints_, a, 0, nbPoints_ - 1);
  integral += integral_[ind];
  XBT_DEBUG("a %f ind %d integral %f ind + 1 %f ind %f time +1 %f time %f",
       a, ind, integral, integral_[ind + 1], integral_[ind], timePoints_[ind + 1], timePoints_[ind]);
  double_update(&a_aux, timePoints_[ind], sg_maxmin_precision*sg_surf_precision);
  if (a_aux > 0)
    integral += ((integral_[ind + 1] - integral_[ind]) / (timePoints_[ind + 1] - timePoints_[ind])) *
                (a - timePoints_[ind]);
  XBT_DEBUG("Integral a %f = %f", a, integral);

  return integral;
}

/**
* \brief Computes the time needed to execute "amount" on cpu.
*
* Here, amount can span multiple trace periods
*
* \param a        Initial time
* \param amount  Amount to be executed
* \return  End time
*/
double CpuTiTgmr::solve(double a, double amount)
{
  /* Fix very small negative numbers */
  if ((a < 0.0) && (a > -EPSILON)) {
    a = 0.0;
  }
  if ((amount < 0.0) && (amount > -EPSILON)) {
    amount = 0.0;
  }

  /* Sanity checks */
  if ((a < 0.0) || (amount < 0.0)) {
    XBT_CRITICAL ("Error, invalid parameters [a = %.2f, amount = %.2f]. "
        "You probably have a task executing with negative computation amount. Check your code.", a, amount);
    xbt_abort();
  }

  /* At this point, a and amount are positive */
  if (amount < EPSILON)
    return a;

  /* Is the trace fixed ? */
  if (type_ == TRACE_FIXED) {
    return (a + (amount / value_));
  }

  XBT_DEBUG("amount %f total %f", amount, total_);
  /* Reduce the problem to one where amount <= trace_total */
  int quotient = static_cast<int>(floor(amount / total_));
  double reduced_amount = (total_) * ((amount / total_) - floor(amount / total_));
  double reduced_a = a - (lastTime_) * static_cast<int>(floor(a / lastTime_));

  XBT_DEBUG("Quotient: %d reduced_amount: %f reduced_a: %f", quotient, reduced_amount, reduced_a);

  /* Now solve for new_amount which is <= trace_total */
  double reduced_b = solveSomewhatSimple(reduced_a, reduced_amount);

/* Re-map to the original b and amount */
  double b = (lastTime_) * static_cast<int>(floor(a / lastTime_)) + (quotient * lastTime_) + reduced_b;
  return b;
}

/**
* \brief Auxiliary function to solve integral
*
* Here, amount is <= trace->total
* and a <=trace->last_time
*
*/
double CpuTiTgmr::solveSomewhatSimple(double a, double amount)
{
  double b;

  XBT_DEBUG("Solve integral: [%.2f, amount=%.2f]", a, amount);
  double amount_till_end = integrate(a, lastTime_);

  if (amount_till_end > amount) {
    b = trace_->solveSimple(a, amount);
  } else {
    b = lastTime_ + trace_->solveSimple(0.0, amount - amount_till_end);
  }
  return b;
}

/**
 * \brief Auxiliary function to solve integral.
 *  It returns the date when the requested amount of flops is available
 * \param a        Initial point
 * \param amount  Amount of flops
 * \return The date when amount is available.
*/
double CpuTiTrace::solveSimple(double a, double amount)
{
  double integral_a = integrateSimplePoint(a);
  int ind = binarySearch(integral_, integral_a + amount, 0, nbPoints_ - 1);
  double time = timePoints_[ind];
  time += (integral_a + amount - integral_[ind]) /
           ((integral_[ind + 1] - integral_[ind]) / (timePoints_[ind + 1] - timePoints_[ind]));

  return time;
}

/**
* \brief Auxiliary function to update the CPU speed scale.
*
*  This function uses the trace structure to return the speed scale at the determined time a.
* \param a        Time
* \return CPU speed scale
*/
double CpuTiTgmr::getPowerScale(double a)
{
  double reduced_a = a - floor(a / lastTime_) * lastTime_;
  int point = trace_->binarySearch(trace_->timePoints_, reduced_a, 0, trace_->nbPoints_ - 1);
  s_tmgr_event_t val = speedTrace_->event_list.at(point);
  return val.value;
}

/**
* \brief Creates a new integration trace from a tmgr_trace_t
*
* \param  speedTrace    CPU availability trace
* \param  value          Percentage of CPU speed available (useful to fixed tracing)
* \return  Integration trace structure
*/
CpuTiTgmr::CpuTiTgmr(tmgr_trace_t speedTrace, double value)
{
  double total_time = 0.0;
  trace_ = 0;

/* no availability file, fixed trace */
  if (!speedTrace) {
    type_ = TRACE_FIXED;
    value_ = value;
    XBT_DEBUG("No availability trace. Constant value = %f", value);
    return;
  }

  /* only one point available, fixed trace */
  if (speedTrace->event_list.size() == 1) {
    s_tmgr_event_t val = speedTrace->event_list.front();
    type_ = TRACE_FIXED;
    value_ = val.value;
    return;
  }

  type_ = TRACE_DYNAMIC;
  speedTrace_ = speedTrace;

  /* count the total time of trace file */
  for (auto val: speedTrace->event_list) {
    total_time += val.delta;
  }
  trace_ = new CpuTiTrace(speedTrace);
  lastTime_ = total_time;
  total_ = trace_->integrateSimple(0, total_time);

  XBT_DEBUG("Total integral %f, last_time %f ", total_, lastTime_);
}

/**
 * \brief Binary search in array.
 *  It returns the first point of the interval in which "a" is.
 * \param array    Array
 * \param a        Value to search
 * \param low     Low bound to search in array
 * \param high    Upper bound to search in array
 * \return Index of point
*/
int CpuTiTrace::binarySearch(double *array, double a, int low, int high)
{
  xbt_assert(low < high, "Wrong parameters: low (%d) should be smaller than high (%d)", low, high);

  int mid;
  do {
    mid = low + (high - low) / 2;
    XBT_DEBUG("a %f low %d high %d mid %d value %f", a, low, high, mid, array[mid]);

    if (array[mid] > a)
      high = mid;
    else
      low = mid;
  }
  while (low < high - 1);

  return low;
}

}
}

/*********
 * Model *
 *********/

void surf_cpu_model_init_ti()
{
  xbt_assert(!surf_cpu_model_pm,"CPU model already initialized. This should not happen.");
  xbt_assert(!surf_cpu_model_vm,"CPU model already initialized. This should not happen.");

  surf_cpu_model_pm = new simgrid::surf::CpuTiModel();
  all_existing_models->push_back(surf_cpu_model_pm);

  surf_cpu_model_vm = new simgrid::surf::CpuTiModel();
  all_existing_models->push_back(surf_cpu_model_vm);
}

namespace simgrid {
namespace surf {

CpuTiModel::CpuTiModel() : CpuModel()
{
  runningActionSetThatDoesNotNeedBeingChecked_ = new ActionList();

  modifiedCpu_ = new CpuTiList();

  tiActionHeap_ = xbt_heap_new(8, nullptr);
  xbt_heap_set_update_callback(tiActionHeap_, cpu_ti_action_update_index_heap);
}

CpuTiModel::~CpuTiModel()
{
  surf_cpu_model_pm = nullptr;
  delete runningActionSetThatDoesNotNeedBeingChecked_;
  delete modifiedCpu_;
  xbt_heap_free(tiActionHeap_);
}

Cpu *CpuTiModel::createCpu(simgrid::s4u::Host *host, std::vector<double>* speedPerPstate, int core)
{
  return new CpuTi(this, host, speedPerPstate, core);
}

double CpuTiModel::nextOccuringEvent(double now)
{
  double min_action_duration = -1;

/* iterates over modified cpus to update share resources */
  for(CpuTiList::iterator it(modifiedCpu_->begin()), itend(modifiedCpu_->end()) ; it != itend ;) {
    CpuTi *ti = &*it;
    ++it;
    ti->updateActionsFinishTime(now);
  }

/* get the min next event if heap not empty */
  if (xbt_heap_size(tiActionHeap_) > 0)
    min_action_duration = xbt_heap_maxkey(tiActionHeap_) - now;

  XBT_DEBUG("Share resources, min next event date: %f", min_action_duration);

  return min_action_duration;
}

void CpuTiModel::updateActionsState(double now, double /*delta*/)
{
  while ((xbt_heap_size(tiActionHeap_) > 0) && (xbt_heap_maxkey(tiActionHeap_) <= now)) {
    CpuTiAction *action = static_cast<CpuTiAction*>(xbt_heap_pop(tiActionHeap_));
    XBT_DEBUG("Action %p: finish", action);
    action->finish();
    /* set the remains to 0 due to precision problems when updating the remaining amount */
    action->setRemains(0);
    action->setState(Action::State::done);
    /* update remaining amount of all actions */
    action->cpu_->updateRemainingAmount(surf_get_clock());
  }
}

/************
 * Resource *
 ************/
CpuTi::CpuTi(CpuTiModel *model, simgrid::s4u::Host *host, std::vector<double> *speedPerPstate, int core)
  : Cpu(model, host, speedPerPstate, core)
{
  xbt_assert(core==1,"Multi-core not handled by this model yet");
  coresAmount_ = core;

  actionSet_ = new ActionTiList();

  speed_.peak = speedPerPstate->front();
  XBT_DEBUG("CPU create: peak=%f", speed_.peak);

  speedIntegratedTrace_ = new CpuTiTgmr(nullptr, 1/*scale*/);
}

CpuTi::~CpuTi()
{
  modified(false);
  delete speedIntegratedTrace_;
  delete actionSet_;
}
void CpuTi::setSpeedTrace(tmgr_trace_t trace)
{
  if (speedIntegratedTrace_)
    delete speedIntegratedTrace_;

  speedIntegratedTrace_ = new CpuTiTgmr(trace, speed_.scale);

  /* add a fake trace event if periodicity == 0 */
  if (trace && trace->event_list.size() > 1) {
    s_tmgr_event_t val = trace->event_list.back();
    if (val.delta < 1e-12)
      speed_.event = future_evt_set->add_trace(tmgr_empty_trace_new(), 0.0, this);
  }
}

void CpuTi::apply_event(tmgr_trace_iterator_t event, double value)
{
  if (event == speed_.event) {
    tmgr_trace_t speedTrace;
    CpuTiTgmr *trace;

    XBT_DEBUG("Finish trace date: value %f", value);
    /* update remaining of actions and put in modified cpu swag */
    updateRemainingAmount(surf_get_clock());

    modified(true);

    speedTrace = speedIntegratedTrace_->speedTrace_;
    s_tmgr_event_t val = speedTrace->event_list.back();
    delete speedIntegratedTrace_;
    speed_.scale = val.value;

    trace = new CpuTiTgmr(TRACE_FIXED, val.value);
    XBT_DEBUG("value %f", val.value);

    speedIntegratedTrace_ = trace;

    tmgr_trace_event_unref(&speed_.event);

  } else if (event == stateEvent_) {
    if (value > 0) {
      if(isOff())
        host_that_restart.push_back(getHost());
      turnOn();
    } else {
      turnOff();
      double date = surf_get_clock();

      /* put all action running on cpu to failed */
      for(ActionTiList::iterator it(actionSet_->begin()), itend(actionSet_->end()); it != itend ; ++it) {

        CpuTiAction *action = &*it;
        if (action->getState() == Action::State::running
         || action->getState() == Action::State::ready
         || action->getState() == Action::State::not_in_the_system) {
          action->setFinishTime(date);
          action->setState(Action::State::failed);
          if (action->indexHeap_ >= 0) {
            CpuTiAction *heap_act =
                static_cast<CpuTiAction*>(xbt_heap_remove(static_cast<CpuTiModel*>(getModel())->tiActionHeap_, action->indexHeap_));
            if (heap_act != action)
              DIE_IMPOSSIBLE;
          }
        }
      }
    }
    tmgr_trace_event_unref(&stateEvent_);

  } else {
    xbt_die("Unknown event!\n");
  }
}

void CpuTi::updateActionsFinishTime(double now)
{
  CpuTiAction *action;
  double sum_priority = 0.0;
  double total_area;
  double min_finish = -1;

  /* update remaining amount of actions */
  updateRemainingAmount(now);

  for(ActionTiList::iterator it(actionSet_->begin()), itend(actionSet_->end()) ; it != itend ; ++it) {
    action = &*it;
    /* action not running, skip it */
    if (action->getStateSet() != surf_cpu_model_pm->getRunningActionSet())
      continue;

    /* bogus priority, skip it */
    if (action->getPriority() <= 0)
      continue;

    /* action suspended, skip it */
    if (action->suspended_ != 0)
      continue;

    sum_priority += 1.0 / action->getPriority();
  }
  sumPriority_ = sum_priority;

  for(ActionTiList::iterator it(actionSet_->begin()), itend(actionSet_->end()) ; it != itend ; ++it) {
    action = &*it;
    min_finish = -1;
    /* action not running, skip it */
    if (action->getStateSet() !=  surf_cpu_model_pm->getRunningActionSet())
      continue;

    /* verify if the action is really running on cpu */
    if (action->suspended_ == 0 && action->getPriority() > 0) {
      /* total area needed to finish the action. Used in trace integration */
      total_area = (action->getRemains()) * sum_priority * action->getPriority();

      total_area /= speed_.peak;

      action->setFinishTime(speedIntegratedTrace_->solve(now, total_area));
      /* verify which event will happen before (max_duration or finish time) */
      if (action->getMaxDuration() > NO_MAX_DURATION &&
          action->getStartTime() + action->getMaxDuration() < action->finishTime_)
        min_finish = action->getStartTime() + action->getMaxDuration();
      else
        min_finish = action->finishTime_;
    } else {
      /* put the max duration time on heap */
      if (action->getMaxDuration() > NO_MAX_DURATION)
        min_finish = action->getStartTime() + action->getMaxDuration();
    }
    /* add in action heap */
    XBT_DEBUG("action(%p) index %d", action, action->indexHeap_);
    if (action->indexHeap_ >= 0) {
      CpuTiAction *heap_act =
          static_cast<CpuTiAction*>(xbt_heap_remove(static_cast<CpuTiModel*>(getModel())->tiActionHeap_, action->indexHeap_));
      if (heap_act != action)
        DIE_IMPOSSIBLE;
    }
    if (min_finish > NO_MAX_DURATION)
      xbt_heap_push(static_cast<CpuTiModel*>(getModel())->tiActionHeap_, action, min_finish);

    XBT_DEBUG("Update finish time: Cpu(%s) Action: %p, Start Time: %f Finish Time: %f Max duration %f",
         getName(), action, action->getStartTime(), action->finishTime_, action->getMaxDuration());
  }
  /* remove from modified cpu */
  modified(false);
}

bool CpuTi::isUsed()
{
  return !actionSet_->empty();
}

double CpuTi::getAvailableSpeed()
{
  speed_.scale = speedIntegratedTrace_->getPowerScale(surf_get_clock());
  return Cpu::getAvailableSpeed();
}

/** @brief Update the remaining amount of actions */
void CpuTi::updateRemainingAmount(double now)
{

  /* already updated */
  if (lastUpdate_ >= now)
    return;

  /* compute the integration area */
  double area_total = speedIntegratedTrace_->integrate(lastUpdate_, now) * speed_.peak;
  XBT_DEBUG("Flops total: %f, Last update %f", area_total, lastUpdate_);

  for(ActionTiList::iterator it(actionSet_->begin()), itend(actionSet_->end()) ; it != itend ; ++it) {
    CpuTiAction *action = &*it;
    /* action not running, skip it */
    if (action->getStateSet() != getModel()->getRunningActionSet())
      continue;

    /* bogus priority, skip it */
    if (action->getPriority() <= 0)
      continue;

    /* action suspended, skip it */
    if (action->suspended_ != 0)
      continue;

    /* action don't need update */
    if (action->getStartTime() >= now)
      continue;

    /* skip action that are finishing now */
    if (action->finishTime_ >= 0 && action->finishTime_ <= now)
      continue;

    /* update remaining */
    action->updateRemains(area_total / (sumPriority_ * action->getPriority()));
    XBT_DEBUG("Update remaining action(%p) remaining %f", action, action->remains_);
  }
  lastUpdate_ = now;
}

CpuAction *CpuTi::execution_start(double size)
{
  XBT_IN("(%s,%g)", getName(), size);
  CpuTiAction *action = new CpuTiAction(static_cast<CpuTiModel*>(getModel()), size, isOff(), this);

  actionSet_->push_back(*action);

  XBT_OUT();
  return action;
}


CpuAction *CpuTi::sleep(double duration)
{
  if (duration > 0)
    duration = MAX(duration, sg_surf_precision);

  XBT_IN("(%s,%g)", getName(), duration);
  CpuTiAction *action = new CpuTiAction(static_cast<CpuTiModel*>(getModel()), 1.0, isOff(), this);

  action->maxDuration_ = duration;
  action->suspended_ = 2;
  if (duration == NO_MAX_DURATION) {
   /* Move to the *end* of the corresponding action set. This convention
      is used to speed up update_resource_state  */
  action->getStateSet()->erase(action->getStateSet()->iterator_to(*action));
    action->stateSet_ = static_cast<CpuTiModel*>(getModel())->runningActionSetThatDoesNotNeedBeingChecked_;
    action->getStateSet()->push_back(*action);
  }

  actionSet_->push_back(*action);

  XBT_OUT();
  return action;
}

void CpuTi::modified(bool modified){
  CpuTiList *modifiedCpu = static_cast<CpuTiModel*>(getModel())->modifiedCpu_;
  if (modified) {
    if (!cpu_ti_hook.is_linked()) {
      modifiedCpu->push_back(*this);
    }
  } else {
    if (cpu_ti_hook.is_linked()) {
      modifiedCpu->erase(modifiedCpu->iterator_to(*this));
    }
  }
}

/**********
 * Action *
 **********/

CpuTiAction::CpuTiAction(CpuTiModel *model_, double cost, bool failed, CpuTi *cpu)
 : CpuAction(model_, cost, failed)
{
  cpu_ = cpu;
  indexHeap_ = -1;
  cpu_->modified(true);
}

void CpuTiAction::updateIndexHeap(int i)
{
  indexHeap_ = i;
}

void CpuTiAction::setState(Action::State state)
{
  CpuAction::setState(state);
  cpu_->modified(true);
}

int CpuTiAction::unref()
{
  refcount_--;
  if (!refcount_) {
    if (action_hook.is_linked())
      getStateSet()->erase(getStateSet()->iterator_to(*this));
    /* remove from action_set */
    if (action_ti_hook.is_linked())
      cpu_->actionSet_->erase(cpu_->actionSet_->iterator_to(*this));
    /* remove from heap */
    xbt_heap_remove(static_cast<CpuTiModel*>(getModel())->tiActionHeap_, this->indexHeap_);
    cpu_->modified(true);
    delete this;
    return 1;
  }
  return 0;
}

void CpuTiAction::cancel()
{
  this->setState(Action::State::failed);
  xbt_heap_remove(getModel()->getActionHeap(), this->indexHeap_);
  cpu_->modified(true);
  return;
}

void CpuTiAction::suspend()
{
  XBT_IN("(%p)", this);
  if (suspended_ != 2) {
    suspended_ = 1;
    xbt_heap_remove(getModel()->getActionHeap(), indexHeap_);
    cpu_->modified(true);
  }
  XBT_OUT();
}

void CpuTiAction::resume()
{
  XBT_IN("(%p)", this);
  if (suspended_ != 2) {
    suspended_ = 0;
    cpu_->modified(true);
  }
  XBT_OUT();
}

void CpuTiAction::setMaxDuration(double duration)
{
  double min_finish;

  XBT_IN("(%p,%g)", this, duration);

  maxDuration_ = duration;

  if (duration >= 0)
    min_finish = (getStartTime() + getMaxDuration()) < getFinishTime() ?
                 (getStartTime() + getMaxDuration()) : getFinishTime();
  else
    min_finish = getFinishTime();

/* add in action heap */
  if (indexHeap_ >= 0) {
    CpuTiAction *heap_act = static_cast<CpuTiAction*>(xbt_heap_remove(getModel()->getActionHeap(), indexHeap_));
    if (heap_act != this)
      DIE_IMPOSSIBLE;
  }
  xbt_heap_push(getModel()->getActionHeap(), this, min_finish);

  XBT_OUT();
}

void CpuTiAction::setPriority(double priority)
{
  XBT_IN("(%p,%g)", this, priority);
  priority_ = priority;
  cpu_->modified(true);
  XBT_OUT();
}

double CpuTiAction::getRemains()
{
  XBT_IN("(%p)", this);
  cpu_->updateRemainingAmount(surf_get_clock());
  XBT_OUT();
  return remains_;
}

}
}

#endif /* SURF_MODEL_CPUTI_H_ */
