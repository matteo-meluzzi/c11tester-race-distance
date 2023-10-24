#include "datarace.h"
#include "model.h"
#include "threads-model.h"
#include <stdio.h>
#include <cstring>
#include "mymemory.h"
#include "clockvector.h"
#include "config.h"
#include "action.h"
#include "execution.h"
#include "stl-model.h"
#include <execinfo.h>
#include "relationsgraph.h"

static struct ShadowTable *root;
static void *memory_base;
static void *memory_top;
static RaceSet * raceset;

#ifdef COLLECT_STAT
static unsigned int store8_count = 0;
static unsigned int store16_count = 0;
static unsigned int store32_count = 0;
static unsigned int store64_count = 0;

static unsigned int load8_count = 0;
static unsigned int load16_count = 0;
static unsigned int load32_count = 0;
static unsigned int load64_count = 0;
#endif

static const ModelExecution * get_execution()
{
	return model->get_execution();
}

/** This function initialized the data race detector. */
void initRaceDetector()
{
	root = (struct ShadowTable *)snapshot_calloc(sizeof(struct ShadowTable), 1);
	memory_base = snapshot_calloc(sizeof(struct ShadowBaseTable) * SHADOWBASETABLES, 1);
	memory_top = ((char *)memory_base) + sizeof(struct ShadowBaseTable) * SHADOWBASETABLES;
	raceset = new RaceSet();
}

void * table_calloc(size_t size)
{
	if ((((char *)memory_base) + size) > memory_top) {
		return snapshot_calloc(size, 1);
	} else {
		void *tmp = memory_base;
		memory_base = ((char *)memory_base) + size;
		return tmp;
	}
}

/** This function looks up the entry in the shadow table corresponding to a
 * given address.*/
static inline uint64_t * lookupAddressEntry(const void *address)
{
	struct ShadowTable *currtable = root;
#if BIT48
	currtable = (struct ShadowTable *) currtable->array[(((uintptr_t)address) >> 32) & MASK16BIT];
	if (currtable == NULL) {
		currtable = (struct ShadowTable *)(root->array[(((uintptr_t)address) >> 32) & MASK16BIT] = table_calloc(sizeof(struct ShadowTable)));
	}
#endif

	struct ShadowBaseTable *basetable = (struct ShadowBaseTable *)currtable->array[(((uintptr_t)address) >> 16) & MASK16BIT];
	if (basetable == NULL) {
		basetable = (struct ShadowBaseTable *)(currtable->array[(((uintptr_t)address) >> 16) & MASK16BIT] = table_calloc(sizeof(struct ShadowBaseTable)));
	}
	return &basetable->array[((uintptr_t)address) & MASK16BIT];
}


bool hasNonAtomicStore(const void *address) {
	uint64_t * shadow = lookupAddressEntry(address);
	uint64_t shadowval = *shadow;
	if (ISSHORTRECORD(shadowval)) {
		//Do we have a non atomic write with a non-zero clock
		return !(ATOMICMASK & shadowval);
	} else {
		if (shadowval == 0)
			return true;
		struct RaceRecord *record = (struct RaceRecord *)shadowval;
		return !record->isAtomic;
	}
}

void setAtomicStoreFlag(const void *address) {
	uint64_t * shadow = lookupAddressEntry(address);
	uint64_t shadowval = *shadow;
	if (ISSHORTRECORD(shadowval)) {
		*shadow = shadowval | ATOMICMASK;
	} else {
		if (shadowval == 0) {
			*shadow = ATOMICMASK | ENCODEOP(0, 0, 0, 0);
			return;
		}
		struct RaceRecord *record = (struct RaceRecord *)shadowval;
		record->isAtomic = 1;
	}
}

void getStoreThreadAndClock(const void *address, thread_id_t * thread, modelclock_t * clock) {
	uint64_t * shadow = lookupAddressEntry(address);
	uint64_t shadowval = *shadow;
	if (ISSHORTRECORD(shadowval) || shadowval == 0) {
		//Do we have a non atomic write with a non-zero clock
		*thread = WRTHREADID(shadowval);
		*clock = WRITEVECTOR(shadowval);
	} else {
		struct RaceRecord *record = (struct RaceRecord *)shadowval;
		*thread = record->writeThread;
		*clock = record->writeClock;
	}
}

/**
 * Compares a current clock-vector/thread-ID pair with a clock/thread-ID pair
 * to check the potential for a data race.
 * @param clock1 The current clock vector
 * @param tid1 The current thread; paired with clock1
 * @param clock2 The clock value for the potentially-racing action
 * @param tid2 The thread ID for the potentially-racing action
 * @return true if the current clock allows a race with the event at clock2/tid2
 */
static inline bool clock_may_race(ClockVector *clock1, thread_id_t tid1,
													 modelclock_t clock2, thread_id_t tid2)
{
	return tid1 != tid2 && clock2 != 0 && clock1->getClock(tid2) <= clock2;
}

/**
 * Expands a record from the compact form to the full form.  This is
 * necessary for multiple readers or for very large thread ids or time
 * stamps. */
static void expandRecord(uint64_t *shadow)
{
	uint64_t shadowval = *shadow;

	modelclock_t readClock = READVECTOR(shadowval);
	thread_id_t readThread = int_to_id(RDTHREADID(shadowval));
	modelclock_t writeClock = WRITEVECTOR(shadowval);
	thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

	struct RaceRecord *record = (struct RaceRecord *)snapshot_calloc(1, sizeof(struct RaceRecord));
	record->writeThread = writeThread;
	record->writeClock = writeClock;

	if (readClock != 0) {
		record->thread = (thread_id_t *)snapshot_malloc(sizeof(thread_id_t) * INITCAPACITY);
		record->readClock = (modelclock_t *)snapshot_malloc(sizeof(modelclock_t) * INITCAPACITY);
		record->numReads = 1;
		ASSERT(readThread >= 0);
		record->thread[0] = readThread;
		record->readClock[0] = readClock;
	} else {
		record->thread = NULL;
	}
	if (shadowval & ATOMICMASK)
		record->isAtomic = 1;
	*shadow = (uint64_t) record;
}

#define FIRST_STACK_FRAME 2

unsigned int race_hash(struct DataRace *race) {
	unsigned int hash = 0;
	for(int i=FIRST_STACK_FRAME;i < race->numframes;i++) {
		hash ^= ((uintptr_t)race->backtrace[i]);
		hash = (hash >> 3) | (hash << 29);
	}
	return hash;
}

bool race_equals(struct DataRace *r1, struct DataRace *r2) {
	if (r1->numframes != r2->numframes)
		return false;
	for(int i=FIRST_STACK_FRAME;i < r1->numframes;i++) {
		if (r1->backtrace[i] != r2->backtrace[i])
			return false;
	}
	return true;
}

/** This function is called when we detect a data race.*/
static struct DataRace * reportDataRace(thread_id_t oldthread, modelclock_t oldclock, bool isoldwrite, ModelAction *newaction, bool isnewwrite, const void *address)
{
#ifdef REPORT_DATA_RACES
	struct DataRace *race = (struct DataRace *)model_malloc(sizeof(struct DataRace));
	race->oldthread = oldthread;
	race->oldclock = oldclock;
	race->isoldwrite = isoldwrite;
	race->newaction = newaction;
	race->isnewwrite = isnewwrite;
	race->address = address;
	return race;
#else
	return NULL;
#endif
}

/**
 * @brief Assert a data race
 *
 * Asserts a data race which is currently realized, causing the execution to
 * end and stashing a message in the model-checker's bug list
 *
 * @param race The race to report
 */
void assert_race(struct DataRace *race)
{
	model_print("Race detected at location: \n");
	backtrace_symbols_fd(race->backtrace, race->numframes, model_out);
	model_print("\nData race detected @ address %p:\n"
							"    Access 1: %5s in thread %2d @ clock %3u\n"
							"    Access 2: %5s in thread %2d @ clock %3u\n\n",
							race->address,
							race->isoldwrite ? "write" : "read",
							id_to_int(race->oldthread),
							race->oldclock,
							race->isnewwrite ? "write" : "read",
							id_to_int(race->newaction->get_tid()),
							race->newaction->get_seq_number()
							);

	auto exe = get_execution();
	auto action1 = exe->get_last_action(race->oldthread);
	auto action2 = race->newaction;
	auto dist = exe->relations_graph.minDistanceBetween(action1, action2);
	model_print("\nDistance between racy accesses: %d", dist);
}

/** This function does race detection for a write on an expanded record. */
struct DataRace * fullRaceCheckWrite(thread_id_t thread, const void *location, uint64_t *shadow, ClockVector *currClock)
{
	struct RaceRecord *record = (struct RaceRecord *)(*shadow);
	struct DataRace * race = NULL;

	/* Check for datarace against last read. */

	for (int i = 0;i < record->numReads;i++) {
		modelclock_t readClock = record->readClock[i];
		thread_id_t readThread = record->thread[i];

		/* Note that readClock can't actuall be zero here, so it could be
		         optimized. */

		if (clock_may_race(currClock, thread, readClock, readThread)) {
			/* We have a datarace */
			race = reportDataRace(readThread, readClock, false, get_execution()->get_parent_action(thread), true, location);
			goto Exit;
		}
	}

	/* Check for datarace against last write. */
	{
		modelclock_t writeClock = record->writeClock;
		thread_id_t writeThread = record->writeThread;

		if (clock_may_race(currClock, thread, writeClock, writeThread)) {
			/* We have a datarace */
			race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), true, location);
			goto Exit;
		}
	}
Exit:
	record->numReads = 0;
	record->writeThread = thread;
	record->isAtomic = 0;
	modelclock_t ourClock = currClock->getClock(thread);
	record->writeClock = ourClock;
	return race;
}

/** This function does race detection for a write on an expanded record. */
struct DataRace * atomfullRaceCheckWrite(thread_id_t thread, const void *location, uint64_t *shadow, ClockVector *currClock)
{
	struct RaceRecord *record = (struct RaceRecord *)(*shadow);
	struct DataRace * race = NULL;

	if (record->isAtomic)
		goto Exit;

	/* Check for datarace against last read. */

	for (int i = 0;i < record->numReads;i++) {
		modelclock_t readClock = record->readClock[i];
		thread_id_t readThread = record->thread[i];

		/* Note that readClock can't actuall be zero here, so it could be
		         optimized. */

		if (clock_may_race(currClock, thread, readClock, readThread)) {
			/* We have a datarace */
			race = reportDataRace(readThread, readClock, false, get_execution()->get_parent_action(thread), true, location);
			goto Exit;
		}
	}

	/* Check for datarace against last write. */

	{
		modelclock_t writeClock = record->writeClock;
		thread_id_t writeThread = record->writeThread;

		if (clock_may_race(currClock, thread, writeClock, writeThread)) {
			/* We have a datarace */
			race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), true, location);
			goto Exit;
		}
	}
Exit:
	record->numReads = 0;
	record->writeThread = thread;
	record->isAtomic = 1;
	modelclock_t ourClock = currClock->getClock(thread);
	record->writeClock = ourClock;
	return race;
}

/** This function does race detection on a write. */
void atomraceCheckWrite(thread_id_t thread, void *location)
{
	uint64_t *shadow = lookupAddressEntry(location);
	uint64_t shadowval = *shadow;
	ClockVector *currClock = get_execution()->get_cv(thread);
	if (currClock == NULL)
		return;

	struct DataRace * race = NULL;
	/* Do full record */
	if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
		race = atomfullRaceCheckWrite(thread, location, shadow, currClock);
		goto Exit;
	}

	{
		int threadid = id_to_int(thread);
		modelclock_t ourClock = currClock->getClock(thread);

		/* Thread ID is too large or clock is too large. */
		if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
			expandRecord(shadow);
			race = atomfullRaceCheckWrite(thread, location, shadow, currClock);
			goto Exit;
		}

		/* Can't race with atomic */
		if (shadowval & ATOMICMASK)
			goto ShadowExit;

		{
			/* Check for datarace against last read. */
			modelclock_t readClock = READVECTOR(shadowval);
			thread_id_t readThread = int_to_id(RDTHREADID(shadowval));

			if (clock_may_race(currClock, thread, readClock, readThread)) {
				/* We have a datarace */
				race = reportDataRace(readThread, readClock, false, get_execution()->get_parent_action(thread), true, location);
				goto ShadowExit;
			}
		}

		{
			/* Check for datarace against last write. */
			modelclock_t writeClock = WRITEVECTOR(shadowval);
			thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

			if (clock_may_race(currClock, thread, writeClock, writeThread)) {
				/* We have a datarace */
				race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), true, location);
				goto ShadowExit;
			}
		}

ShadowExit:
		*shadow = ENCODEOP(0, 0, threadid, ourClock) | ATOMICMASK;
	}

Exit:
	if (race) {
#ifdef REPORT_DATA_RACES
		race->numframes=backtrace(race->backtrace, sizeof(race->backtrace)/sizeof(void*));
		if (raceset->add(race))
			assert_race(race);
		else model_free(race);
#else
		model_free(race);
#endif
	}
}

/** This function does race detection for a write on an expanded record. */
void fullRecordWrite(thread_id_t thread, void *location, uint64_t *shadow, ClockVector *currClock) {
	struct RaceRecord *record = (struct RaceRecord *)(*shadow);
	record->numReads = 0;
	record->writeThread = thread;
	modelclock_t ourClock = currClock->getClock(thread);
	record->writeClock = ourClock;
	record->isAtomic = 1;
}

/** This function does race detection for a write on an expanded record. */
void fullRecordWriteNonAtomic(thread_id_t thread, void *location, uint64_t *shadow, ClockVector *currClock) {
	struct RaceRecord *record = (struct RaceRecord *)(*shadow);
	record->numReads = 0;
	record->writeThread = thread;
	modelclock_t ourClock = currClock->getClock(thread);
	record->writeClock = ourClock;
	record->isAtomic = 0;
}

/** This function just updates metadata on atomic write. */
void recordWrite(thread_id_t thread, void *location) {
	uint64_t *shadow = lookupAddressEntry(location);
	uint64_t shadowval = *shadow;
	ClockVector *currClock = get_execution()->get_cv(thread);
	/* Do full record */
	if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
		fullRecordWrite(thread, location, shadow, currClock);
		return;
	}

	int threadid = id_to_int(thread);
	modelclock_t ourClock = currClock->getClock(thread);

	/* Thread ID is too large or clock is too large. */
	if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
		expandRecord(shadow);
		fullRecordWrite(thread, location, shadow, currClock);
		return;
	}

	*shadow = ENCODEOP(0, 0, threadid, ourClock) | ATOMICMASK;
}

/** This function just updates metadata on atomic write. */
void recordCalloc(void *location, size_t size) {
	thread_id_t thread = thread_current_id();
	for(;size != 0;size--) {
		uint64_t *shadow = lookupAddressEntry(location);
		uint64_t shadowval = *shadow;
		ClockVector *currClock = get_execution()->get_cv(thread);
		/* Do full record */
		if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
			fullRecordWriteNonAtomic(thread, location, shadow, currClock);
			return;
		}

		int threadid = id_to_int(thread);
		modelclock_t ourClock = currClock->getClock(thread);

		/* Thread ID is too large or clock is too large. */
		if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
			expandRecord(shadow);
			fullRecordWriteNonAtomic(thread, location, shadow, currClock);
			return;
		}

		*shadow = ENCODEOP(0, 0, threadid, ourClock);
		location = (void *)(((char *) location) + 1);
	}
}

/** This function does race detection on a read for an expanded record. */
struct DataRace * fullRaceCheckRead(thread_id_t thread, const void *location, uint64_t *shadow, ClockVector *currClock)
{
	struct RaceRecord *record = (struct RaceRecord *) (*shadow);
	struct DataRace * race = NULL;
	/* Check for datarace against last write. */

	modelclock_t writeClock = record->writeClock;
	thread_id_t writeThread = record->writeThread;

	if (clock_may_race(currClock, thread, writeClock, writeThread)) {
		/* We have a datarace */
		race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), false, location);
	}

	/* Shorten vector when possible */

	int copytoindex = 0;

	for (int i = 0;i < record->numReads;i++) {
		modelclock_t readClock = record->readClock[i];
		thread_id_t readThread = record->thread[i];

		/*  Note that is not really a datarace check as reads cannot
		                actually race.  It is just determining that this read subsumes
		                another in the sense that either this read races or neither
		                read races. Note that readClock can't actually be zero, so it
		                could be optimized.  */

		if (clock_may_race(currClock, thread, readClock, readThread)) {
			/* Still need this read in vector */
			if (copytoindex != i) {
				ASSERT(readThread >= 0);
				record->readClock[copytoindex] = readClock;
				record->thread[copytoindex] = readThread;
			}
			copytoindex++;
		}
	}

	if (__builtin_popcount(copytoindex) <= 1) {
		if (copytoindex == 0 && record->thread == NULL) {
			int newCapacity = INITCAPACITY;
			record->thread = (thread_id_t *)snapshot_malloc(sizeof(thread_id_t) * newCapacity);
			record->readClock = (modelclock_t *)snapshot_malloc(sizeof(modelclock_t) * newCapacity);
		} else if (copytoindex>=INITCAPACITY) {
			int newCapacity = copytoindex * 2;
			thread_id_t *newthread = (thread_id_t *)snapshot_malloc(sizeof(thread_id_t) * newCapacity);
			modelclock_t *newreadClock = (modelclock_t *)snapshot_malloc(sizeof(modelclock_t) * newCapacity);
			real_memcpy(newthread, record->thread, copytoindex * sizeof(thread_id_t));
			real_memcpy(newreadClock, record->readClock, copytoindex * sizeof(modelclock_t));
			snapshot_free(record->readClock);
			snapshot_free(record->thread);
			record->readClock = newreadClock;
			record->thread = newthread;
		}
	}

	modelclock_t ourClock = currClock->getClock(thread);

	ASSERT(thread >= 0);
	record->thread[copytoindex] = thread;
	record->readClock[copytoindex] = ourClock;
	record->numReads = copytoindex + 1;
	return race;
}

/** This function does race detection on a read for an expanded record. */
struct DataRace * atomfullRaceCheckRead(thread_id_t thread, const void *location, uint64_t *shadow, ClockVector *currClock)
{
	struct RaceRecord *record = (struct RaceRecord *) (*shadow);
	struct DataRace * race = NULL;
	/* Check for datarace against last write. */
	if (record->isAtomic)
		return NULL;

	modelclock_t writeClock = record->writeClock;
	thread_id_t writeThread = record->writeThread;

	if (clock_may_race(currClock, thread, writeClock, writeThread)) {
		/* We have a datarace */
		race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), false, location);
	}
	return race;
}

/** This function does race detection on a read. */
void atomraceCheckRead(thread_id_t thread, const void *location)
{
	uint64_t *shadow = lookupAddressEntry(location);
	uint64_t shadowval = *shadow;
	ClockVector *currClock = get_execution()->get_cv(thread);
	if (currClock == NULL)
		return;

	struct DataRace * race = NULL;

	/* Do full record */
	if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
		race = atomfullRaceCheckRead(thread, location, shadow, currClock);
		goto Exit;
	}

	if (shadowval & ATOMICMASK)
		return;

	{
		/* Check for datarace against last write. */
		modelclock_t writeClock = WRITEVECTOR(shadowval);
		thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

		if (clock_may_race(currClock, thread, writeClock, writeThread)) {
			/* We have a datarace */
			race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), false, location);
			goto Exit;
		}
	}
Exit:
	if (race) {
#ifdef REPORT_DATA_RACES
		race->numframes=backtrace(race->backtrace, sizeof(race->backtrace)/sizeof(void*));
		if (raceset->add(race))
			assert_race(race);
		else model_free(race);
#else
		model_free(race);
#endif
	}
}

static inline uint64_t * raceCheckRead_firstIt(thread_id_t thread, const void * location, uint64_t *old_val, uint64_t *new_val)
{
	uint64_t *shadow = lookupAddressEntry(location);
	uint64_t shadowval = *shadow;

	ClockVector *currClock = get_execution()->get_cv(thread);
	if (currClock == NULL)
		return shadow;

	struct DataRace * race = NULL;

	/* Do full record */
	if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
		race = fullRaceCheckRead(thread, location, shadow, currClock);
		goto Exit;
	}

	{
		int threadid = id_to_int(thread);
		modelclock_t ourClock = currClock->getClock(thread);

		/* Thread ID is too large or clock is too large. */
		if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
			expandRecord(shadow);
			race = fullRaceCheckRead(thread, location, shadow, currClock);
			goto Exit;
		}

		/* Check for datarace against last write. */
		modelclock_t writeClock = WRITEVECTOR(shadowval);
		thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

		if (clock_may_race(currClock, thread, writeClock, writeThread)) {
			/* We have a datarace */
			race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), false, location);
		}

		modelclock_t readClock = READVECTOR(shadowval);
		thread_id_t readThread = int_to_id(RDTHREADID(shadowval));

		if (clock_may_race(currClock, thread, readClock, readThread)) {
			/* We don't subsume this read... Have to expand record. */
			expandRecord(shadow);
			struct RaceRecord *record = (struct RaceRecord *) (*shadow);
			record->thread[1] = thread;
			record->readClock[1] = ourClock;
			record->numReads++;

			goto Exit;
		}

		*shadow = ENCODEOP(threadid, ourClock, id_to_int(writeThread), writeClock) | (shadowval & ATOMICMASK);

		*old_val = shadowval;
		*new_val = *shadow;
	}
Exit:
	if (race) {
#ifdef REPORT_DATA_RACES
		race->numframes=backtrace(race->backtrace, sizeof(race->backtrace)/sizeof(void*));
		if (raceset->add(race))
			assert_race(race);
		else model_free(race);
#else
		model_free(race);
#endif
	}

	return shadow;
}

static inline void raceCheckRead_otherIt(thread_id_t thread, const void * location)
{
	uint64_t *shadow = lookupAddressEntry(location);
	uint64_t shadowval = *shadow;
	ClockVector *currClock = get_execution()->get_cv(thread);
	if (currClock == NULL)
		return;

	struct DataRace * race = NULL;

	/* Do full record */
	if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
		race = fullRaceCheckRead(thread, location, shadow, currClock);
		goto Exit;
	}

	{
		int threadid = id_to_int(thread);
		modelclock_t ourClock = currClock->getClock(thread);

		/* Thread ID is too large or clock is too large. */
		if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
			expandRecord(shadow);
			race = fullRaceCheckRead(thread, location, shadow, currClock);
			goto Exit;
		}

		/* Check for datarace against last write. */
		modelclock_t writeClock = WRITEVECTOR(shadowval);
		thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

		if (clock_may_race(currClock, thread, writeClock, writeThread)) {
			/* We have a datarace */
			race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), false, location);
		}

		modelclock_t readClock = READVECTOR(shadowval);
		thread_id_t readThread = int_to_id(RDTHREADID(shadowval));

		if (clock_may_race(currClock, thread, readClock, readThread)) {
			/* We don't subsume this read... Have to expand record. */
			expandRecord(shadow);
			struct RaceRecord *record = (struct RaceRecord *) (*shadow);
			record->thread[1] = thread;
			record->readClock[1] = ourClock;
			record->numReads++;

			goto Exit;
		}

		*shadow = ENCODEOP(threadid, ourClock, id_to_int(writeThread), writeClock) | (shadowval & ATOMICMASK);
	}
Exit:
	if (race) {
#ifdef REPORT_DATA_RACES
		race->numframes=backtrace(race->backtrace, sizeof(race->backtrace)/sizeof(void*));
		if (raceset->add(race))
			assert_race(race);
		else model_free(race);
#else
		model_free(race);
#endif
	}
}

void raceCheckRead64(thread_id_t thread, const void *location)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

	uint64_t old_shadowval, new_shadowval;
	old_shadowval = new_shadowval = INVALIDSHADOWVAL;
#ifdef COLLECT_STAT
	load64_count++;
#endif
	uint64_t * shadow = raceCheckRead_firstIt(thread, location, &old_shadowval, &new_shadowval);
	if (CHECKBOUNDARY(location, 7)) {
		if (shadow[1]==old_shadowval)
			shadow[1] = new_shadowval;
		else goto L1;
		if (shadow[2]==old_shadowval)
			shadow[2] = new_shadowval;
		else goto L2;
		if (shadow[3]==old_shadowval)
			shadow[3] = new_shadowval;
		else goto L3;
		if (shadow[4]==old_shadowval)
			shadow[4] = new_shadowval;
		else goto L4;
		if (shadow[5]==old_shadowval)
			shadow[5] = new_shadowval;
		else goto L5;
		if (shadow[6]==old_shadowval)
			shadow[6] = new_shadowval;
		else goto L6;
		if (shadow[7]==old_shadowval)
			shadow[7] = new_shadowval;
		else goto L7;
		RESTORE_MODEL_FLAG(old_flag);
		return;
	}

L1:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 1));
L2:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 2));
L3:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 3));
L4:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 4));
L5:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 5));
L6:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 6));
L7:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 7));
	RESTORE_MODEL_FLAG(old_flag);
}

void raceCheckRead32(thread_id_t thread, const void *location)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

	uint64_t old_shadowval, new_shadowval;
	old_shadowval = new_shadowval = INVALIDSHADOWVAL;
#ifdef COLLECT_STAT
	load32_count++;
#endif
	uint64_t * shadow = raceCheckRead_firstIt(thread, location, &old_shadowval, &new_shadowval);
	if (CHECKBOUNDARY(location, 3)) {
		if (shadow[1]==old_shadowval)
			shadow[1] = new_shadowval;
		else goto L1;
		if (shadow[2]==old_shadowval)
			shadow[2] = new_shadowval;
		else goto L2;
		if (shadow[3]==old_shadowval)
			shadow[3] = new_shadowval;
		else goto L3;
		RESTORE_MODEL_FLAG(old_flag);
		return;
	}

L1:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 1));
L2:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 2));
L3:
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 3));
	RESTORE_MODEL_FLAG(old_flag);
}

void raceCheckRead16(thread_id_t thread, const void *location)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

	uint64_t old_shadowval, new_shadowval;
	old_shadowval = new_shadowval = INVALIDSHADOWVAL;
#ifdef COLLECT_STAT
	load16_count++;
#endif
	uint64_t * shadow = raceCheckRead_firstIt(thread, location, &old_shadowval, &new_shadowval);
	if (CHECKBOUNDARY(location, 1)) {
		if (shadow[1]==old_shadowval) {
			shadow[1] = new_shadowval;
			RESTORE_MODEL_FLAG(old_flag);
			return;
		}
	}
	raceCheckRead_otherIt(thread, (const void *)(((uintptr_t)location) + 1));
	RESTORE_MODEL_FLAG(old_flag);
}

void raceCheckRead8(thread_id_t thread, const void *location)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

#ifdef COLLECT_STAT
	load8_count++;
#endif
	raceCheckRead_otherIt(thread, location);
	RESTORE_MODEL_FLAG(old_flag);
}

static inline uint64_t * raceCheckWrite_firstIt(thread_id_t thread, const void * location, uint64_t *old_val, uint64_t *new_val)
{
	uint64_t *shadow = lookupAddressEntry(location);
	uint64_t shadowval = *shadow;
	ClockVector *currClock = get_execution()->get_cv(thread);
	if (currClock == NULL)
		return shadow;

	struct DataRace * race = NULL;
	/* Do full record */
	if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
		race = fullRaceCheckWrite(thread, location, shadow, currClock);
		goto Exit;
	}

	{
		int threadid = id_to_int(thread);
		modelclock_t ourClock = currClock->getClock(thread);

		/* Thread ID is too large or clock is too large. */
		if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
			expandRecord(shadow);
			race = fullRaceCheckWrite(thread, location, shadow, currClock);
			goto Exit;
		}

		{
			/* Check for datarace against last read. */
			modelclock_t readClock = READVECTOR(shadowval);
			thread_id_t readThread = int_to_id(RDTHREADID(shadowval));

			if (clock_may_race(currClock, thread, readClock, readThread)) {
				/* We have a datarace */
				race = reportDataRace(readThread, readClock, false, get_execution()->get_parent_action(thread), true, location);
				goto ShadowExit;
			}
		}

		{
			/* Check for datarace against last write. */
			modelclock_t writeClock = WRITEVECTOR(shadowval);
			thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

			if (clock_may_race(currClock, thread, writeClock, writeThread)) {
				/* We have a datarace */
				race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), true, location);
				goto ShadowExit;
			}
		}

ShadowExit:
		*shadow = ENCODEOP(0, 0, threadid, ourClock);

		*old_val = shadowval;
		*new_val = *shadow;
	}

Exit:
	if (race) {
#ifdef REPORT_DATA_RACES
		race->numframes=backtrace(race->backtrace, sizeof(race->backtrace)/sizeof(void*));
		if (raceset->add(race))
			assert_race(race);
		else model_free(race);
#else
		model_free(race);
#endif
	}

	return shadow;
}

static inline void raceCheckWrite_otherIt(thread_id_t thread, const void * location)
{
	uint64_t *shadow = lookupAddressEntry(location);
	uint64_t shadowval = *shadow;
	ClockVector *currClock = get_execution()->get_cv(thread);
	if (currClock == NULL)
		return;

	struct DataRace * race = NULL;
	/* Do full record */
	if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
		race = fullRaceCheckWrite(thread, location, shadow, currClock);
		goto Exit;
	}

	{
		int threadid = id_to_int(thread);
		modelclock_t ourClock = currClock->getClock(thread);

		/* Thread ID is too large or clock is too large. */
		if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
			expandRecord(shadow);
			race = fullRaceCheckWrite(thread, location, shadow, currClock);
			goto Exit;
		}

		{
			/* Check for datarace against last read. */
			modelclock_t readClock = READVECTOR(shadowval);
			thread_id_t readThread = int_to_id(RDTHREADID(shadowval));

			if (clock_may_race(currClock, thread, readClock, readThread)) {
				/* We have a datarace */
				race = reportDataRace(readThread, readClock, false, get_execution()->get_parent_action(thread), true, location);
				goto ShadowExit;
			}
		}

		{
			/* Check for datarace against last write. */
			modelclock_t writeClock = WRITEVECTOR(shadowval);
			thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

			if (clock_may_race(currClock, thread, writeClock, writeThread)) {
				/* We have a datarace */
				race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), true, location);
				goto ShadowExit;
			}
		}

ShadowExit:
		*shadow = ENCODEOP(0, 0, threadid, ourClock);
	}

Exit:
	if (race) {
#ifdef REPORT_DATA_RACES
		race->numframes=backtrace(race->backtrace, sizeof(race->backtrace)/sizeof(void*));
		if (raceset->add(race))
			assert_race(race);
		else model_free(race);
#else
		model_free(race);
#endif
	}
}

void raceCheckWrite64(thread_id_t thread, const void *location)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;
	uint64_t old_shadowval, new_shadowval;
	old_shadowval = new_shadowval = INVALIDSHADOWVAL;
#ifdef COLLECT_STAT
	store64_count++;
#endif
	uint64_t * shadow = raceCheckWrite_firstIt(thread, location, &old_shadowval, &new_shadowval);
	if (CHECKBOUNDARY(location, 7)) {
		if (shadow[1]==old_shadowval)
			shadow[1] = new_shadowval;
		else goto L1;
		if (shadow[2]==old_shadowval)
			shadow[2] = new_shadowval;
		else goto L2;
		if (shadow[3]==old_shadowval)
			shadow[3] = new_shadowval;
		else goto L3;
		if (shadow[4]==old_shadowval)
			shadow[4] = new_shadowval;
		else goto L4;
		if (shadow[5]==old_shadowval)
			shadow[5] = new_shadowval;
		else goto L5;
		if (shadow[6]==old_shadowval)
			shadow[6] = new_shadowval;
		else goto L6;
		if (shadow[7]==old_shadowval)
			shadow[7] = new_shadowval;
		else goto L7;
		RESTORE_MODEL_FLAG(old_flag);
		return;
	}

L1:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 1));
L2:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 2));
L3:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 3));
L4:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 4));
L5:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 5));
L6:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 6));
L7:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 7));
	RESTORE_MODEL_FLAG(old_flag);
}

void raceCheckWrite32(thread_id_t thread, const void *location)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

	uint64_t old_shadowval, new_shadowval;
	old_shadowval = new_shadowval = INVALIDSHADOWVAL;
#ifdef COLLECT_STAT
	store32_count++;
#endif
	uint64_t * shadow = raceCheckWrite_firstIt(thread, location, &old_shadowval, &new_shadowval);
	if (CHECKBOUNDARY(location, 3)) {
		if (shadow[1]==old_shadowval)
			shadow[1] = new_shadowval;
		else goto L1;
		if (shadow[2]==old_shadowval)
			shadow[2] = new_shadowval;
		else goto L2;
		if (shadow[3]==old_shadowval)
			shadow[3] = new_shadowval;
		else goto L3;
		RESTORE_MODEL_FLAG(old_flag);
		return;
	}

L1:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 1));
L2:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 2));
L3:
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 3));
	RESTORE_MODEL_FLAG(old_flag);
}

void raceCheckWrite16(thread_id_t thread, const void *location)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

	uint64_t old_shadowval, new_shadowval;
	old_shadowval = new_shadowval = INVALIDSHADOWVAL;
#ifdef COLLECT_STAT
	store16_count++;
#endif

	uint64_t * shadow = raceCheckWrite_firstIt(thread, location, &old_shadowval, &new_shadowval);
	if (CHECKBOUNDARY(location, 1)) {
		if (shadow[1]==old_shadowval) {
			shadow[1] = new_shadowval;
			RESTORE_MODEL_FLAG(old_flag);
			return;
		}
	}
	raceCheckWrite_otherIt(thread, (const void *)(((uintptr_t)location) + 1));
	RESTORE_MODEL_FLAG(old_flag);
}

void raceCheckWrite8(thread_id_t thread, const void *location)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

#ifdef COLLECT_STAT
	store8_count++;
#endif
	raceCheckWrite_otherIt(thread, location);
	RESTORE_MODEL_FLAG(old_flag);
}

void raceCheckWriteMemop(thread_id_t thread, const void *location, size_t size)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

	ClockVector *currClock = get_execution()->get_cv(thread);
	if (currClock == NULL) {
		RESTORE_MODEL_FLAG(old_flag);
		return;
	}

	bool alreadyHasRace = false;
	for (uint i = 0; i < size; i++) {
		uint64_t *shadow = lookupAddressEntry(location);
		uint64_t shadowval = *shadow;

		struct DataRace * race = NULL;
		/* Do full record */
		if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
			race = fullRaceCheckWrite(thread, location, shadow, currClock);
			goto Exit;
		}

		{
			int threadid = id_to_int(thread);
			modelclock_t ourClock = currClock->getClock(thread);

			/* Thread ID is too large or clock is too large. */
			if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
				expandRecord(shadow);
				race = fullRaceCheckWrite(thread, location, shadow, currClock);
				goto Exit;
			}

			{
				/* Check for datarace against last read. */
				modelclock_t readClock = READVECTOR(shadowval);
				thread_id_t readThread = int_to_id(RDTHREADID(shadowval));

				if (clock_may_race(currClock, thread, readClock, readThread)) {
					/* We have a datarace */
					race = reportDataRace(readThread, readClock, false, get_execution()->get_parent_action(thread), true, location);
					goto ShadowExit;
				}
			}

			{
				/* Check for datarace against last write. */
				modelclock_t writeClock = WRITEVECTOR(shadowval);
				thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

				if (clock_may_race(currClock, thread, writeClock, writeThread)) {
					/* We have a datarace */
					race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), true, location);
					goto ShadowExit;
				}
			}

ShadowExit:
			*shadow = ENCODEOP(0, 0, threadid, ourClock);
		}

Exit:
		if (race) {
#ifdef REPORT_DATA_RACES
			if (!alreadyHasRace) {
				alreadyHasRace = true;
				race->numframes=backtrace(race->backtrace, sizeof(race->backtrace)/sizeof(void*));
				if (raceset->add(race))
					assert_race(race);
				else model_free(race);
			} else {
				model_free(race);
			}
#else
			model_free(race);
#endif
		}
	}
	RESTORE_MODEL_FLAG(old_flag);
}

void raceCheckReadMemop(thread_id_t thread, const void * location, size_t size)
{
	int old_flag = GET_MODEL_FLAG;
	ENTER_MODEL_FLAG;

	ClockVector *currClock = get_execution()->get_cv(thread);
	if (currClock == NULL) {
		RESTORE_MODEL_FLAG(old_flag);
		return;
	}

	bool alreadyHasRace = false;
	for (uint i = 0; i < size; i++) {
		uint64_t *shadow = lookupAddressEntry(location);
		uint64_t shadowval = *shadow;
		struct DataRace * race = NULL;

		/* Do full record */
		if (shadowval != 0 && !ISSHORTRECORD(shadowval)) {
			race = fullRaceCheckRead(thread, location, shadow, currClock);
			goto Exit;
		}

		{
			int threadid = id_to_int(thread);
			modelclock_t ourClock = currClock->getClock(thread);

			/* Thread ID is too large or clock is too large. */
			if (threadid > MAXTHREADID || ourClock > MAXWRITEVECTOR) {
				expandRecord(shadow);
				race = fullRaceCheckRead(thread, location, shadow, currClock);
				goto Exit;
			}

			/* Check for datarace against last write. */
			modelclock_t writeClock = WRITEVECTOR(shadowval);
			thread_id_t writeThread = int_to_id(WRTHREADID(shadowval));

			if (clock_may_race(currClock, thread, writeClock, writeThread)) {
				/* We have a datarace */
				race = reportDataRace(writeThread, writeClock, true, get_execution()->get_parent_action(thread), false, location);
			}

			modelclock_t readClock = READVECTOR(shadowval);
			thread_id_t readThread = int_to_id(RDTHREADID(shadowval));

			if (clock_may_race(currClock, thread, readClock, readThread)) {
				/* We don't subsume this read... Have to expand record. */
				expandRecord(shadow);
				struct RaceRecord *record = (struct RaceRecord *) (*shadow);
				record->thread[1] = thread;
				record->readClock[1] = ourClock;
				record->numReads++;

				goto Exit;
			}

			*shadow = ENCODEOP(threadid, ourClock, id_to_int(writeThread), writeClock) | (shadowval & ATOMICMASK);
		}
Exit:
		if (race) {
#ifdef REPORT_DATA_RACES
			if (!alreadyHasRace) {
				race->numframes=backtrace(race->backtrace, sizeof(race->backtrace)/sizeof(void*));
				if (raceset->add(race))
					assert_race(race);
				else model_free(race);
			} else {
				model_free(race);
			}
#else
			model_free(race);
#endif
		}
	}
	RESTORE_MODEL_FLAG(old_flag);
}

#ifdef COLLECT_STAT
void print_normal_accesses()
{
	model_print("store 8  count: %u\n", store8_count);
	model_print("store 16 count: %u\n", store16_count);
	model_print("store 32 count: %u\n", store32_count);
	model_print("store 64 count: %u\n", store64_count);

	model_print("load  8  count: %u\n", load8_count);
	model_print("load  16 count: %u\n", load16_count);
	model_print("load  32 count: %u\n", load32_count);
	model_print("load  64 count: %u\n", load64_count);
}
#endif
