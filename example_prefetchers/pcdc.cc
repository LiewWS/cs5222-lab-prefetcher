//
// Data Prefetching Championship Simulator 2
// Seth Pugsley, seth.h.pugsley@intel.com
//

/*

  This file does NOT implement any prefetcher, and is just an outline

 */

#include <stdio.h>
#include <stdlib.h>
#include "../inc/prefetcher.h"

#define INDEX_TABLE_ENTRIES 256
#define GHB_ENTRIES         256
#define PCDC_DEGREE         4

typedef enum {
  PAIR_INVALID, PAIR_PREP0, PAIR_PREP1, PAIR_VALID
} pair_state_t;

typedef struct {
  long long int entry0;
  long long int entry1;
  pair_state_t state;
} correlation_pair_t;

typedef struct {
  correlation_pair_t pair;
  long long int next;
} ghb_entry_t;

long long int* IndexTable;
ghb_entry_t* GlobalHistoryBuffer;
long long int ghb_head;

correlation_pair_t ghb_transition(correlation_pair_t current, unsigned long long int addr) {
  correlation_pair_t next;

  switch (current.state) {
    case PAIR_INVALID:
      next.entry0 = addr;
      next.entry1 = 0;
      next.state = PAIR_PREP0;
      break;
    case PAIR_PREP0:
      next.entry0 = addr - current.entry0;
      next.entry1 = addr;
      next.state = PAIR_PREP1;
      break;
    case PAIR_PREP1:
      next.entry0 = current.entry0;
      next.entry1 = addr - current.entry1;
      next.state = PAIR_VALID;
      break;
    case PAIR_VALID:
      next.entry0 = 0;
      next.entry1 = 0;
      next.state = PAIR_INVALID;
      break;
  }

  return next;
}

long long int ghb_update(unsigned long long int addr, long long int next_ptr) {
  int count = 0;
  long long int ret = ghb_head;
  for (long long int i = ghb_head; (i >= 0) && (count < 3); --i) {
    correlation_pair_t current = GlobalHistoryBuffer[i].pair;
    GlobalHistoryBuffer[i].pair = ghb_transition(current, addr);
    ++count;
  }

  ghb_entry_t head_entry = GlobalHistoryBuffer[ret];
  if (head_entry.pair.state == PAIR_INVALID) {
    GlobalHistoryBuffer[ret].next = -1;
  } else {
    GlobalHistoryBuffer[ret].next = next_ptr;
  }

  ghb_head = (ghb_head + 1) % GHB_ENTRIES;

  return ret;
}

void dc_prefetch(int cpu_num, unsigned long long int addr, long long int ptr) {
  int fetch_count = 0;
  while ((ptr != -1) && (fetch_count < PCDC_DEGREE)) {
    ghb_entry_t current = GlobalHistoryBuffer[ptr];
    if (current.pair.state == PAIR_VALID) {
      l2_prefetch_line(cpu_num, addr, current.pair.entry0 + addr, FILL_L2);
      l2_prefetch_line(cpu_num, addr, current.pair.entry1 + addr, FILL_L2);
      fetch_count += 2;
      ptr = current.next;
    } else if (current.pair.state == PAIR_PREP1) {
      l2_prefetch_line(cpu_num, addr, current.pair.entry0 + addr, FILL_L2);
      fetch_count += 1;
      ptr = current.next;
    }
  }
}

void l2_prefetcher_initialize(int cpu_num)
{
  printf("PC/DC Prefetching\n");
  // you can inspect these knob values from your code to see which configuration you're runnig in
  printf("Knobs visible from prefetcher: %d %d %d\n", knob_scramble_loads, knob_small_llc, knob_low_bandwidth);

  IndexTable = (long long int*) calloc(INDEX_TABLE_ENTRIES, sizeof(long long int));
  if (IndexTable == NULL) {
    printf("Allocate Index Table failed\n");
    return;
  }
  GlobalHistoryBuffer = (ghb_entry_t*) calloc(GHB_ENTRIES, sizeof(ghb_entry_t));
  if (GlobalHistoryBuffer == NULL) {
    printf("Allocate GHB failed\n");
    return;
  }
  ghb_head = 0;
  for (long long int i = 0; i < GHB_ENTRIES; ++i) {
    GlobalHistoryBuffer[i].next = -1;
    GlobalHistoryBuffer[i].pair.state = PAIR_INVALID;
  }
  for (unsigned long long int i = 0; i < INDEX_TABLE_ENTRIES; ++i) {
    IndexTable[i] = -1;
  }
}

void l2_prefetcher_operate(int cpu_num, unsigned long long int addr, unsigned long long int ip, int cache_hit)
{
  // uncomment this line to see all the information available to make prefetch decisions
  //printf("(0x%llx 0x%llx %d %d %d) ", addr, ip, cache_hit, get_l2_read_queue_occupancy(0), get_l2_mshr_occupancy(0));

  if (cache_hit == 1) {
    return;
  }

  // Get GHB entry
  unsigned long long int table_idx = ip % INDEX_TABLE_ENTRIES;
  long long int next_ptr = -1;
  if (IndexTable[table_idx] != -1) {
    next_ptr = IndexTable[table_idx];
  }
  IndexTable[table_idx] = ghb_head;

  // Update GHB
  long long int ptr = ghb_update(addr, next_ptr);

  // Prefetch
  dc_prefetch(cpu_num, addr, ptr);
}

void l2_cache_fill(int cpu_num, unsigned long long int addr, int set, int way, int prefetch, unsigned long long int evicted_addr)
{
  // uncomment this line to see the information available to you when there is a cache fill event
  //printf("0x%llx %d %d %d 0x%llx\n", addr, set, way, prefetch, evicted_addr);
}

void l2_prefetcher_heartbeat_stats(int cpu_num)
{
  printf("Prefetcher heartbeat stats\n");
}

void l2_prefetcher_warmup_stats(int cpu_num)
{
  printf("Prefetcher warmup complete stats\n\n");
}

void l2_prefetcher_final_stats(int cpu_num)
{
  printf("Prefetcher final stats\n");
  free(IndexTable);
  free(GlobalHistoryBuffer);
}
