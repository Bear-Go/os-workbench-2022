#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "co.h"

// #define DEBUG
#ifdef DEBUG
  #include <stdio.h>
  #define debug(format, ...) printf(format, __VA_ARGS__)
#else
  #define debug(format, ...) 
#endif


// data structure
enum co_status {
  CO_FREE = 0,  // free
  CO_NEW,       // not loaded
  CO_RUNNING,   // loaded && running
  CO_WAITING,   // loaded && not running
  CO_DEAD,      // finished
};

#define STACK_SIZE 1 << 16
#define NAME_SIZE 64
struct co {
  char name[NAME_SIZE];
  void (*func)(void *); // entry address and parameters
  void *arg;

  enum co_status status;
  jmp_buf        context;
  uint8_t        stack[STACK_SIZE];
}; 
/*** sizeof(co_pool[0]) should be a multiple of 16 ***/
/*** This is for consistency of sp alignment ***/

#define NCO 128
static struct co co_pool[NCO]; // a pool with NCO coroutines
static struct co *current; 
static int co_alive; 

// static function
static inline void stack_switch_call(void *sp, void *entry);
static void *wrapper();
static void scheduler();

// extern function
struct co *co_start(const char *name, void (*func)(void *), void *arg) {
  debug("MESSAGE: co_start() %s\n", name);
  for (int i = 0; i < NCO; ++i) {
    if (co_pool[i].status == CO_FREE) {
      strncpy(co_pool[i].name, name, NAME_SIZE);
      co_pool[i].func = func;
      co_pool[i].arg = arg;
      co_pool[i].status = CO_NEW;
      ++co_alive;
      return &co_pool[i];
    }
  }
  debug("ERROR: Coroutine number overflow %s\n", name);
  assert(0);
}

void co_wait(struct co *co) {
  debug("MESSAGE: co_wait(%s)\n", co->name);
  while (co->status != CO_DEAD) {
    if (setjmp(current->context) == 0) {
      current->status = CO_WAITING;
      scheduler(); 
    }
  }
  // recycle resource
  memset(co, 0, sizeof(co_pool[0]));
  co->status = CO_FREE;
}

void co_yield() {
  debug("MESSAGE: co_yield()%s\n", "");
  // save current env AND schedule()
  int val = setjmp(current->context);
  // sleep
  if (val == 0) {
    current->status = CO_WAITING; // change current coroutine to CO_WAITING
    scheduler(); // select next corountine
  }
  // wake up
}

void __attribute__((constructor)) co_init() {
  srand(time(NULL));
  memset(co_pool, 0, NCO * sizeof(co_pool[0]));  
  current = &co_pool[0]; // main coroutine
  strcpy(current->name, "main");
  current->status = CO_RUNNING;
  co_alive = 1;
}



/* static function */

static inline void stack_switch_call(void *sp, void *entry) {
// The address pointed by sp should be a multiple of 16
  asm volatile (
#if __x86_64__
    "movq %0, %%rsp; jmp *%1"
      : : "b"((uintptr_t)sp - 8), "d"(entry) : "memory"
#else
    "movl %0, %%esp; jmp *%1"
      : : "b"((uintptr_t)sp - 16), "d"(entry) : "memory"
#endif
  );
}

static void *wrapper() {
  debug("MESSAGE: set sp successfully%s\n", "");
  current->func(current->arg);

  debug("MESSAGE: coroutine %s is done\n", current->name);
  current->status = CO_DEAD;
  --co_alive; assert(co_alive > 0);
  scheduler();
  debug("ERROR: scheduler() return%s\n", "");
  assert(0);
  return NULL;
}

static void scheduler() {
  // select a valid coroutine AND switch current to it
  debug("MESSAGE: scheduler()%s ", "");
  assert(current->status != CO_RUNNING);
  int cnt, select;
  cnt = rand() % co_alive + 1;
  for (select = 0; select < NCO; ++select)
    if (co_pool[select].status == CO_NEW || co_pool[select].status == CO_WAITING) 
      if (--cnt == 0) break;
  debug("co_pool[%d]", select);
  assert(select < NCO);

  current = &co_pool[select];
  if (current->status == CO_NEW) {
    // change sp and func(arg)
    debug(".status: NEW%s\n", "");
    current->status = CO_RUNNING;
    // initial the func's sp & switch context
    stack_switch_call(&current->stack[STACK_SIZE], wrapper);
  }
  if (current->status == CO_WAITING) {
    debug(".status: WAITING%s\n", "");
    current->status = CO_RUNNING;
    longjmp(current->context, 1);
  }
  debug("ERROR: Wrong status: %d\n", current->status);
  assert(0);
}