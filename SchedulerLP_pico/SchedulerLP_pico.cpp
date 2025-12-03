#include <SchedulerLP_pico.h>
#include "Arduino.h"

// --- NEW INCLUDES FOR PICO HARDWARE SLEEP ---
#include "pico/stdlib.h"
#include "pico/time.h"
// --------------------------------------------

/*
   Updated enterSleep for RP2040
   Uses best_effort_wfe_or_timeout to put the core into a low-power
   Wait-For-Event state.
*/
void enterSleep(int period)
{
  if (period <= 0) return;

  // Calculate the absolute time in the future to wake up
  absolute_time_t target_time = make_timeout_time_ms(period);

  // This puts the processor to sleep until the timer fires OR
  // a hardware interrupt occurs (which is good for responsiveness)
  best_effort_wfe_or_timeout(target_time);
}

struct _task_list_type *
CreateList() {

  task_list * list = (task_list *)malloc(sizeof(task_list));

  // Check to make sure we allocated memory.
  if (list == NULL) {
    return NULL;
  }

  list->head = NULL;
  list->tail = NULL;

  return list;
}

struct _task_list_type * Tasks = CreateList();

struct _task_entry_type *
CreateTask() {

  if (!Tasks) return NULL;

  task_entry * task = (task_entry *)malloc(sizeof(task_entry));

  // Check to make sure we allocated memory.
  if (task == NULL) return NULL;

  task->next = task->prev = NULL;
  task->owner = Tasks;

  return task;
}

void
RemoveTaskFromList(struct _task_entry_type * task) {

  if (!task) return;

  task_list * list = task->owner;

  if (!task->prev) {
    list->head = task->next;
  }
  else {
    task->prev->next = task->next;
  }

  if (!task->next) {
    list->tail = task->prev;
  }
  else {
    task->next->prev = task->prev;
  }

  task->prev = task->next = NULL;
}

int
DeactivateTask(struct _task_entry_type * task) {

  if (!task) return 0;

  // call function assigned to task with data
  (*task->callback)(task, task->data, 0);

  return 1;
}

int
DeleteTask(struct _task_entry_type * task) {

  if (!task) return 0;

  // Remove task from any list it is in
  DeactivateTask(task);
  free(task);
  task = NULL;

  return 1;
}

int
DeleteList(struct _task_list_type * list) {

  if (!list) return 0;

  task_entry * current = list->head;
  task_entry * next;

  // iterate through the list of tasks, deactivate each one
  while (current) {
    next = current->next;
    DeleteTask(current);
    current = next;
  }

  free (list);
  list = NULL;

  return 1;
}

static unsigned long global_seconds = 0;
static unsigned long global_milliseconds = 0;

void
GetCurrentTime (unsigned long * seconds, unsigned long * milliseconds) {
  *seconds = global_seconds;
  *milliseconds = global_milliseconds;
}

unsigned long
GetCurrentSeconds () {
  return (unsigned long)global_seconds;
}

// adds a task to the list with a delay
int
AddTaskDelay(struct _task_entry_type * task, unsigned long delay_seconds, unsigned long delay_millisecs, int(*FuncPtr)(struct _task_entry_type *, int, int), int mesgid, int data) {

  if (!task) return 0;

  task_list * list = task->owner;
  task_entry * current;

  unsigned long seconds;
  unsigned long millisecs;

  // Get the time
  GetCurrentTime(&seconds, &millisecs);

  task->callback = FuncPtr;
  task->data = data;
  task->mesgid = mesgid;

  delay_seconds = delay_seconds + seconds;

  delay_millisecs = delay_millisecs + millisecs;


  if (delay_millisecs > 1000) {
    delay_millisecs = delay_millisecs - 1000;
    delay_seconds += 1;
  }

  task->seconds = delay_seconds;
  task->millisecs = delay_millisecs;

  task->next = task->prev = NULL;

  // just add the task if the task list is empty.
  if (!list->head) {
    list->head = list->tail = task;
    return 1;
  }

  // insert the task into the time task list in the order that it is sleeping
  for ( current = list->head; current && (current->seconds < delay_seconds
      || (current->seconds == delay_seconds && current->millisecs < delay_millisecs)); ) {
    current = current->next;
  }

  // insert at the front of the list
  if (list->head == current) {

    list->head->prev = task;
    task->next = list->head;
    list->head = task;

    // insert at the end of the list
  }
  else if (!current) {

    list->tail->next = task;
    task->prev = list->tail;
    list->tail = task;

    // insert in the middle of the list
  }
  else {

    task->next = current;
    task->prev = current->prev;

    current->prev = task;
    task->prev->next = task;
  }

  return 1;
}

// convenience function to make Add Task Delay easier
int
AddTaskNow(struct _task_entry_type * task, int(*FuncPtr)(struct _task_entry_type *, int, int), int mesgid, int data) {
  return AddTaskDelay(task, 0, 0, FuncPtr, mesgid, data);
}

// convenience function to make Add Task Delay easier
int
AddTaskMilli(struct _task_entry_type * task, unsigned long delay_millisecs, int(*FuncPtr)(struct _task_entry_type *, int, int), int mesgid, int data) {
  //	printf("Add task with delay of %d milliseconds\n", (int) delay_millisecs);
  return AddTaskDelay(task, delay_millisecs / 1000, delay_millisecs % 1000, FuncPtr, mesgid, data);
}

// convenience function to make Add Task easier
int
AddTaskSec(struct _task_entry_type * task, unsigned long delay_seconds, int(*FuncPtr)(struct _task_entry_type *, int, int), int mesgid, int data) {
  //	printf("Add task with delay of %d seconds\n", (int) delay_seconds);
  return AddTaskDelay(task, delay_seconds, 0, FuncPtr, mesgid, data);
}

void
AddTaskToTail(struct _task_list_type * list, struct _task_entry_type * task) {

  if (!task || !list) return;

  // add first task
  if (!list->head) {
    list->head = list->tail = task;
    task->next = task->prev = NULL;
    return;
  }

  list->tail->next = task;
  task->next = NULL;
  task->prev = list->tail;
  list->tail = task;
}

void
ActivateTimedTasks(struct _task_list_type * list, struct _task_list_type * runnow) {

  unsigned long seconds;
  unsigned long millisecs;

  if (!list || !runnow) return;

  struct _task_entry_type * current = list->head;
  struct _task_entry_type * next = NULL;

  // Get the time
  GetCurrentTime(&seconds, &millisecs);

  while (current) {
    if ((current->seconds > seconds) ||
        (current->seconds == seconds && current->millisecs > millisecs))
      break;

    next = current->next;
    RemoveTaskFromList(current);
    AddTaskToTail(runnow, current);
    current = next;
  }
}

struct _task_list_type * runlist = CreateList();

// this is the part that does the work
int
ExecTasks(struct _task_list_type * list) {

  if (!list) return 0;

  struct _task_entry_type * current;
  int taskcount = 0;

  // move all the items whose timer has expired to the execute now list
  // this prevents there from being confusion between what we are currently doing and what we will be doing next time
  ActivateTimedTasks(list, runlist);

  // iterate through the list of items to execute
  while (runlist->head) {
    // remove task from list
    // tasks must reschedule each time they are called
    current = runlist->head;
    runlist->head = current->next;
    current->next = NULL;
    current->prev = NULL;

    //call function assigned to task with data
    if (current->callback)
      (*current->callback)(current, current->data, current->mesgid);

    taskcount++;
  }

  if (list->head || taskcount)
    return 1;
  else return 0;

}

void
AdjustDelayedTasks(struct _task_list_type * list, long offset) {

  unsigned long seconds      = offset / 1000;
  unsigned long milliseconds = offset % 1000;

  struct _task_entry_type * current = list->head;

  while (current) {
    current->seconds -= seconds;
    long fixms = current->millisecs - milliseconds;
    if (fixms < 0 ) {
      current-> seconds -= 1;
      fixms += 1000;
    }
    current->millisecs = fixms;
    current = current->next;
  }
}

long
TimeUpdate () {
  unsigned long milliseconds;

  milliseconds = millis();
  global_seconds  = milliseconds / 1000;
  global_milliseconds = milliseconds % 1000;
  return 0;
}

int nexttasktime(struct _task_list_type * list) {
  // Safety check: if list is empty, return a default short sleep
  // to avoid crashing or sleeping forever
  if (!list || !list->head) return 100;

  long diff = (list->head->seconds - global_seconds) * 1000
              + list->head->millisecs - global_milliseconds;
              
  return (int)diff;
}

void
SleepTilNextTaskDue(struct _task_list_type * list) {
  
  if (!list || !list->head) {
    // Nothing to do, just yield slightly
    enterSleep(10);
    return;
  }

  // how much time until next task is due to run
  int duetime_ms = nexttasktime(list);

  // Use the hardware sleep
  enterSleep(duetime_ms);
}

void
DoTasks() {
  TimeUpdate();
  ExecTasks(Tasks);
  SleepTilNextTaskDue(Tasks);
}
