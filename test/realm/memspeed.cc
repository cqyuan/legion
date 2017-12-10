#include "realm.h"
#include <realm/id.h>

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <csignal>
#include <cmath>

#include <time.h>
#include <unistd.h>

using namespace Realm;

Logger log_app("app");

// Task IDs, some IDs are reserved so start at first available number
enum {
  TOP_LEVEL_TASK = Processor::TASK_ID_FIRST_AVAILABLE+0,
  MEMSPEED_TASK,
};

struct SpeedTestArgs {
  Memory mem;
  RegionInstance inst;
  size_t elements;
  int reps;
  Machine::AffinityDetails affinity;
};

static size_t buffer_size = 64 << 20; // should be bigger than any cache in system

void memspeed_cpu_task(const void *args, size_t arglen, 
		       const void *userdata, size_t userlen, Processor p)
{
  const SpeedTestArgs& cargs = *(const SpeedTestArgs *)args;

  AffineAccessor<void *, 1> ra(cargs.inst, 0);

  // sequential write test
  double seqwr_bw = 0;
  {
    long long t1 = Clock::current_time_in_nanoseconds();
    for(int j = 0; j < cargs.reps; j++)
      for(size_t i = 0; i < cargs.elements; i++)
	ra[i] = 0;
    long long t2 = Clock::current_time_in_nanoseconds();
    seqwr_bw = 1.0 * cargs.reps * cargs.elements * sizeof(void *) / (t2 - t1);
  }

  // sequential read test
  double seqrd_bw = 0;
  {
    long long t1 = Clock::current_time_in_nanoseconds();
    int errors = 0;
    for(int j = 0; j < cargs.reps; j++)
      for(size_t i = 0; i < cargs.elements; i++) {
	void *ptr = *(void * volatile *)&ra[i];
	if(ptr != 0) errors++;
      }
    long long t2 = Clock::current_time_in_nanoseconds();
    assert(errors == 0);
    seqrd_bw = 1.0 * cargs.reps * cargs.elements * sizeof(void *) / (t2 - t1);
  }

  // random write test
  double rndwr_bw = 0;
  std::vector<void *> last_ptrs;  // for checking latency test
  {
    // run on many fewer elements...
    size_t count = cargs.elements >> 8;
    // quadratic stepping via "acceleration" and "velocity"
    size_t a = 548191;
    size_t v = 24819;
    size_t p = 0;
    long long t1 = Clock::current_time_in_nanoseconds();
    for(int j = 0; j < cargs.reps; j++) {
      for(size_t i = 0; i < count; i++) {
	size_t prev = p;
	p = (p + v) % cargs.elements;
	v = (v + a) % cargs.elements;
	// wrapping would be bad
	assert(p != 0);
	ra[prev] = &ra[p];
      }
      last_ptrs.push_back(&ra[p]);
    }
    long long t2 = Clock::current_time_in_nanoseconds();
    rndwr_bw = 1.0 * cargs.reps * count * sizeof(void *) / (t2 - t1);
  } 

  // random read test
  double rndrd_bw = 0;
  {
    // run on many fewer elements...
    size_t count = cargs.elements >> 8;
    // quadratic stepping via "acceleration" and "velocity"
    size_t a = 548191;
    size_t v = 24819;
    size_t p = 0;
    long long t1 = Clock::current_time_in_nanoseconds();
    int errors = 0;
    for(int j = 0; j < cargs.reps; j++)
      for(size_t i = 0; i < count; i++) {
	size_t prev = p;
	p = (p + v) % cargs.elements;
	v = (v + a) % cargs.elements;
	// wrapping would be bad
	assert(p != 0);
	void *exp = &ra[p];
	void *act = ra[prev];
	if(exp != act) {
	  //log_app.info() << "error: " << exp << " != " << act;
	  errors++;
	}
      }
    long long t2 = Clock::current_time_in_nanoseconds();
    if(errors > 0)
      log_app.warning() << errors << " errors during random read test";
    rndrd_bw = 1.0 * cargs.reps * count * sizeof(void *) / (t2 - t1);
  } 

  // latency test
  double latency = 0;
  {
    // run on many fewer elements...
    size_t count = cargs.elements >> 8;
    // quadratic stepping via "acceleration" and "velocity"
    long long t1 = Clock::current_time_in_nanoseconds();
    int errors = 0;
    void **ptr = &ra[0];
    for(int j = 0; j < cargs.reps; j++) {
      for(size_t i = 0; i < count; i++)
	ptr = (void **)*ptr;
      assert(ptr == last_ptrs[j]);
    }
    long long t2 = Clock::current_time_in_nanoseconds();
    assert(errors == 0);
    latency = 1.0 * (t2 - t1) / (cargs.reps * count);
  } 

  log_app.info() << " on proc " << p << " seqwr:" << seqwr_bw << " seqrd:" << seqrd_bw;
  log_app.info() << " on proc " << p << " rndwr:" << rndwr_bw << " rndrd:" << rndrd_bw;
  log_app.info() << " on proc " << p << " latency:" << latency;
}

#ifdef USE_CUDA
extern "C" {
  double gpu_seqwr_test(void *buffer, size_t reps, size_t elements);
  double gpu_seqrd_test(void *buffer, size_t reps, size_t elements);
  double gpu_rndwr_test(void *buffer, size_t reps, size_t elements);
  double gpu_rndrd_test(void *buffer, size_t reps, size_t elements);
  double gpu_latency_test(void *buffer, size_t reps, size_t elements);
}

void memspeed_gpu_task(const void *args, size_t arglen, 
		       const void *userdata, size_t userlen, Processor p)
{
  const SpeedTestArgs& cargs = *(const SpeedTestArgs *)args;

  AffineAccessor<void *, 1> ra(cargs.inst, 0);
  assert(ra.strides[0] == sizeof(void *));

  // sequential write test
  double seqwr_bw = gpu_seqwr_test(&ra[0], cargs.reps, cargs.elements);

  // sequential read test
  double seqrd_bw = gpu_seqrd_test(&ra[0], cargs.reps, cargs.elements);

  // random write test
  double rndwr_bw = gpu_rndwr_test(&ra[0], cargs.reps, cargs.elements);

  // random read test
  double rndrd_bw = gpu_rndrd_test(&ra[0], cargs.reps, cargs.elements);

  // latency test
  double latency = gpu_latency_test(&ra[0], cargs.reps, cargs.elements);

  log_app.info() << " on proc " << p << " seqwr:" << seqwr_bw << " seqrd:" << seqrd_bw;
  log_app.info() << " on proc " << p << " rndwr:" << rndwr_bw << " rndrd:" << rndrd_bw;
  log_app.info() << " on proc " << p << " latency:" << latency;
}
#endif

std::set<Processor::Kind> supported_proc_kinds;

void top_level_task(const void *args, size_t arglen, 
		    const void *userdata, size_t userlen, Processor p)
{
  log_app.print() << "Realm memory speed test";

  size_t elements = buffer_size / sizeof(void *);
  IndexSpace<1> d = Rect<1>(0, elements - 1);

  // iterate over memories, create and instance, and then let each processor beat on it
  Machine machine = Machine::get_machine();
  for(Machine::MemoryQuery::iterator it = Machine::MemoryQuery(machine).begin(); it; ++it) {
    Memory m = *it;
    size_t capacity = m.capacity();
    if(capacity < buffer_size) {
      log_app.info() << "skipping memory " << m << " (kind=" << m.kind() << ") - insufficient capacity";
      continue;
    }
    if(m.kind() == Memory::GLOBAL_MEM) {
      log_app.info() << "skipping memory " << m << " (kind=" << m.kind() << ") - slow global memory";
      continue;
    }
    if(ID(m).is_ib_memory()) {
      log_app.info() << "skipping memory " << m << " (kind=" << m.kind() << ") - intermediate buffer memory";
      continue;
    }

    log_app.print() << "Memory: " << m << " Kind:" << m.kind() << " Capacity: " << capacity;
    std::vector<size_t> field_sizes(1, sizeof(void *));
    RegionInstance inst;
    RegionInstance::create_instance(inst, m, d,
				    std::vector<size_t>(1, sizeof(void *)),
				    0 /*SOA*/,
				    ProfilingRequestSet()).wait();
    assert(inst.exists());

    // clear the instance first - this should also take care of faulting it in
    void *fill_value = 0;
    std::vector<CopySrcDstField> sdf(1);
    sdf[0].inst = inst;
    sdf[0].field_id = 0;
    sdf[0].size = sizeof(void *);
    d.fill(sdf, ProfilingRequestSet(), &fill_value, sizeof(fill_value)).wait();

    Machine::ProcessorQuery pq = Machine::ProcessorQuery(machine).has_affinity_to(m);
    for(Machine::ProcessorQuery::iterator it2 = pq.begin(); it2; ++it2) {
      Processor p = *it2;

      SpeedTestArgs cargs;
      cargs.mem = m;
      cargs.inst = inst;
      cargs.elements = elements;
      cargs.reps = 8;
      bool ok = machine.has_affinity(p, m, &cargs.affinity);
      assert(ok);

      log_app.info() << "  Affinity: " << p << " BW: " << cargs.affinity.bandwidth
		     << " Latency: " << cargs.affinity.latency;

      if(supported_proc_kinds.count(p.kind()) == 0) {
	log_app.info() << "processor " << p << " is of unsupported kind " << p.kind() << " - skipping";
	continue;
      }

      Event e = p.spawn(MEMSPEED_TASK, &cargs, sizeof(cargs));

      e.wait();
    }

    inst.destroy();
  }
}

int main(int argc, char **argv)
{
  Runtime rt;

  rt.init(&argc, &argv);

  for(int i = 1; i < argc; i++) {
    if(!strcmp(argv[i], "-b")) {
      buffer_size = strtoll(argv[++i], 0, 10);
      continue;
    }

  }

  rt.register_task(TOP_LEVEL_TASK, top_level_task);

  Processor::register_task_by_kind(Processor::LOC_PROC, false /*!global*/,
				   MEMSPEED_TASK,
				   CodeDescriptor(memspeed_cpu_task),
				   ProfilingRequestSet(),
				   0, 0).wait();
  supported_proc_kinds.insert(Processor::LOC_PROC);

  Processor::register_task_by_kind(Processor::UTIL_PROC, false /*!global*/,
				   MEMSPEED_TASK,
				   CodeDescriptor(memspeed_cpu_task),
				   ProfilingRequestSet(),
				   0, 0).wait();
  supported_proc_kinds.insert(Processor::UTIL_PROC);

  Processor::register_task_by_kind(Processor::IO_PROC, false /*!global*/,
				   MEMSPEED_TASK,
				   CodeDescriptor(memspeed_cpu_task),
				   ProfilingRequestSet(),
				   0, 0).wait();
  supported_proc_kinds.insert(Processor::IO_PROC);

#ifdef USE_CUDA
  Processor::register_task_by_kind(Processor::TOC_PROC, false /*!global*/,
				   MEMSPEED_TASK,
				   CodeDescriptor(memspeed_gpu_task),
				   ProfilingRequestSet(),
				   0, 0).wait();
  supported_proc_kinds.insert(Processor::TOC_PROC);
#endif

  // select a processor to run the top level task on
  Processor p = Machine::ProcessorQuery(Machine::get_machine())
    .only_kind(Processor::LOC_PROC)
    .first();
  assert(p.exists());

  // collective launch of a single task - everybody gets the same finish event
  Event e = rt.collective_spawn(p, TOP_LEVEL_TASK, 0, 0);

  // request shutdown once that task is complete
  rt.shutdown(e);

  // now sleep this thread until that shutdown actually happens
  rt.wait_for_shutdown();
  
  return 0;
}
