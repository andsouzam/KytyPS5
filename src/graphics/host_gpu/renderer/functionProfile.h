// Opt-in diagnostic only. Does not submit work, change barriers or replace waits.
// Fixed registration/rows: no allocations or global mutex in fault-path scope entry.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include "common/config.h"
#include "common/switches.h"
#include "graphics/shader/recompiler/ir/passes/ResourceMaterializationDiagnostics.h"
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef MemoryBarrier
#else
#include <thread>
#include <functional>
#endif

namespace Libs::Graphics::FunctionProfile {
namespace MatDiag = Libs::Graphics::ShaderRecompiler::IR::MaterializationDiagnostics;
enum class Role : uint32_t { Other, Main, Gpu, Submit, DrawSpec, Upload };
enum class Site : uint32_t {
  ProcessPm4,GetGraphicsPrograms,GetGraphicsPipeline,MaterializeResources,MaterializeResourcesLogged,
  ReadsUnchanged,PrepareBindings,PrepareGraphicsBindings,FindBuffers,RebindBuffers,RebindImages,
  CommitBindings,FindImage,RefindImage,FindTexture,SynchronizeBuffer,UploadCopies,ReadMemoryAsync,
  RecordReadback,Submit,Drain,
  InvalidateMemory,DownloadBufferMemory,ReadbackGpuService,BufferPublish,PrepareBda,NativeFenceWait,
  PriorityWait,ServiceWait,QueueSubmit,RecorderPayload,RecorderIdleSpin,RecorderSignalWait,
  JoinAsyncUploads,DownloadImageMemory,ProcessDownloadImages,
  DrawAuto,DrawIndex,DispatchDirect,DispatchIndirect,FaultHandler,ProgramPrecompileWait,PrepareGraphicsStages,AdoptGraphicsStages,GraphicsInputsCheck,GraphicsProgramMutexWait,GraphicsProgramsLocked,ProgramCacheGet,AdoptSpeculation,ProgramPermutationMatch,MaterializationSelectedLeaf,Count
};
inline constexpr uint32_t SiteCount=static_cast<uint32_t>(Site::Count),MaxThreads=64,MaxDepth=32;
inline constexpr std::array<const char*,SiteCount> Names{
  "ProcessPm4","GetGraphicsPrograms","GetGraphicsPipeline","MaterializeResources","MaterializeResourcesLogged",
  "ReadsUnchanged","PrepareBindings","PrepareGraphicsBindings","FindBuffers","RebindBuffers","RebindImages",
  "CommitBindings","FindImage","RefindImage","FindTexture","SynchronizeBuffer","UploadCopies","ReadMemoryAsync",
  "RecordReadback","Submit","Drain","InvalidateMemory","DownloadBufferMemory","ReadbackGpuService","BufferPublish",
  "PrepareBda","NativeFenceWait","PriorityWait","ServiceWait","QueueSubmit","RecorderPayload","RecorderIdleSpin",
  "RecorderSignalWait","JoinAsyncUploads","DownloadImageMemory","ProcessDownloadImages",
  "DrawAuto","DrawIndex","DispatchDirect","DispatchIndirect","FaultHandler","ProgramPrecompileWait","PrepareGraphicsStages","AdoptGraphicsStages","GraphicsInputsCheck","GraphicsProgramMutexWait","GraphicsProgramsLocked","ProgramCacheGet","AdoptSpeculation","ProgramPermutationMatch","MaterializationSelectedLeaf"};
struct Row {
  std::array<std::atomic<uint64_t>,MatDiag::CounterCount> materialization_counts {};
  std::atomic<uint64_t> calls{0},sampled{0},wall_done{0},cpu_done{0},cpu_exclusive_done{0};
  std::atomic<uint64_t> wall_inclusive_ns{0},wall_exclusive_ns{0},cpu_inclusive_cycles{0},cpu_exclusive_cycles{0};
  std::atomic<uint64_t> max_wall_ns{0},query_wall_ns{0},query_errors{0},last_error{0};
  std::atomic<uint64_t> query_count{0},empty_probe_samples{0},empty_probe_cycles{0},empty_probe_errors{0};
  std::atomic<uint64_t> exclusive_invalid{0},truncated{0},outstanding{0},depth_overflow{0};
};
struct ThreadRows {
  std::atomic<uint32_t> tid{0},role{0};
  std::array<Row,SiteCount> rows;
};
inline std::array<ThreadRows,MaxThreads> records;
inline std::atomic<uint32_t> registered{0};
inline std::atomic<uint64_t> registration_overflow{0},active_epoch{0};
inline bool enabled=false,started=false,reported=false;
inline uint32_t sample_every=64;
inline uint64_t start_frame=0,stop_frame=0,deadline_ns=0,start_ns=0,stop_ns=0;
inline thread_local Role tls_role=Role::Other;
inline thread_local ThreadRows* tls_rows=nullptr;
inline thread_local bool tls_registered=false;
inline thread_local uint64_t tls_roots=0;
inline thread_local uint64_t tls_random=0;
class Scope;
inline thread_local Scope* tls_top=nullptr;
inline uint64_t NowNs() noexcept {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void Configure(bool on,uint32_t every=64) noexcept {
  enabled=on;sample_every=every==0?64:every;
  if(!on)active_epoch.store(0,std::memory_order_release);
}
inline void SetRole(Role role) noexcept {
  tls_role=role;if(tls_rows!=nullptr)tls_rows->role.store(static_cast<uint32_t>(role),std::memory_order_relaxed);
}
inline ThreadRows* CurrentRows() noexcept {
  if(!tls_registered) {
    tls_registered=true;const auto index=registered.fetch_add(1,std::memory_order_relaxed);
    if(index>=MaxThreads){registration_overflow.fetch_add(1,std::memory_order_relaxed);return nullptr;}
    tls_rows=&records[index];tls_rows->role.store(static_cast<uint32_t>(tls_role),std::memory_order_relaxed);
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
    const auto tid=GetCurrentThreadId();
#else
    const auto tid=static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()))|1u;
#endif
    tls_random=(static_cast<uint64_t>(tid)<<32u)^0x9e3779b97f4a7c15ull;
    tls_rows->tid.store(tid,std::memory_order_release);
  }
  return tls_rows;
}
inline bool Cpu(Row& row,uint64_t& value) noexcept {
#if KYTY_PLATFORM == KYTY_PLATFORM_WINDOWS
  const auto before=NowNs();ULONG64 raw=0;
  const bool ok=QueryThreadCycleTime(GetCurrentThread(),&raw)!=0;
  const DWORD error=ok?0:GetLastError();
  row.query_wall_ns.fetch_add(NowNs()-before,std::memory_order_relaxed);
  row.query_count.fetch_add(1,std::memory_order_relaxed);
  if(!ok){row.query_errors.fetch_add(1,std::memory_order_relaxed);row.last_error.store(error,std::memory_order_relaxed);}
  value=raw;return ok;
#else
  row.query_errors.fetch_add(1,std::memory_order_relaxed);
  value=0;return false; // Raw CPU cycles are unsupported on this diagnostic path.
#endif
}
class Scope final {
 public:
  explicit Scope(Site site) noexcept {
    m_epoch=active_epoch.load(std::memory_order_acquire);if(m_epoch==0)return;
    m_parent=nullptr;m_wall_begin=0;m_cpu_begin=0;m_child_wall=0;m_child_cpu=0;
    m_depth=0;m_sample=false;m_cpu_valid=false;m_child_wall_valid=true;m_child_cpu_valid=true;
    auto* thread=CurrentRows();if(thread==nullptr)return;
    m_row=&thread->rows[static_cast<uint32_t>(site)];m_row->calls.fetch_add(1,std::memory_order_relaxed);
    m_parent=tls_top;tls_top=this;m_depth=m_parent!=nullptr?m_parent->m_depth+1u:1u;
    if(m_parent!=nullptr)m_sample=m_parent->m_sample;
    else {
      ++tls_roots;
      tls_random^=tls_random<<13u;tls_random^=tls_random>>7u;tls_random^=tls_random<<17u;
      m_sample=tls_random%sample_every==0;
    }
    if(m_depth>MaxDepth){m_sample=false;m_row->depth_overflow.fetch_add(1,std::memory_order_relaxed);
      if(m_parent){m_parent->m_child_wall_valid=false;m_parent->m_child_cpu_valid=false;}return;}
    if(!m_sample)return;
    m_row->sampled.fetch_add(1,std::memory_order_relaxed);m_row->outstanding.fetch_add(1,std::memory_order_relaxed);
    if(m_parent==nullptr) {
      // Matched empty probe once per sampled ROOT, not once per nested scope.
      // Raw values remain reported separately: never subtract a noisy estimate
      // automatically or call raw cycles CPU milliseconds.
      uint64_t first=0,last=0;const bool first_ok=Cpu(*m_row,first);
      (void)NowNs();(void)NowNs();const bool last_ok=Cpu(*m_row,last);
      if(first_ok&&last_ok&&last>=first){m_row->empty_probe_samples.fetch_add(1,std::memory_order_relaxed);
        m_row->empty_probe_cycles.fetch_add(last-first,std::memory_order_relaxed);}
      else m_row->empty_probe_errors.fetch_add(1,std::memory_order_relaxed);
    }
    m_cpu_valid=Cpu(*m_row,m_cpu_begin);m_wall_begin=NowNs();
  }
  ~Scope() noexcept {
    if(m_row==nullptr)return;tls_top=m_parent;if(!m_sample)return;
    const auto end_wall=NowNs();uint64_t end_cpu=0;const bool end_ok=Cpu(*m_row,end_cpu);
    const uint64_t wall=end_wall-m_wall_begin;
    const bool cpu_valid=m_cpu_valid&&end_ok&&end_cpu>=m_cpu_begin;
    const uint64_t cycles=cpu_valid?end_cpu-m_cpu_begin:0;
    if(m_parent!=nullptr&&m_parent->m_sample) {
      m_parent->m_child_wall+=wall;m_parent->m_child_cpu+=cycles;
      // An ancestor subtracts this child's inclusive span, irrespective of whether
      // this child's own exclusive split had a failed grandchild CPU query.
      m_parent->m_child_cpu_valid&=cpu_valid;
    }
    if(active_epoch.load(std::memory_order_relaxed)!=m_epoch) {
      m_row->truncated.fetch_add(1,std::memory_order_relaxed);
    } else {
      m_row->wall_done.fetch_add(1,std::memory_order_relaxed);m_row->wall_inclusive_ns.fetch_add(wall,std::memory_order_relaxed);
      const auto max=m_row->max_wall_ns.load(std::memory_order_relaxed);
      if(wall>max)m_row->max_wall_ns.store(wall,std::memory_order_relaxed); // one writer per row
      if(m_child_wall_valid&&wall>=m_child_wall)m_row->wall_exclusive_ns.fetch_add(wall-m_child_wall,std::memory_order_relaxed);
      else m_row->exclusive_invalid.fetch_add(1,std::memory_order_relaxed);
      if(cpu_valid){m_row->cpu_done.fetch_add(1,std::memory_order_relaxed);m_row->cpu_inclusive_cycles.fetch_add(cycles,std::memory_order_relaxed);
        if(m_child_cpu_valid&&cycles>=m_child_cpu){m_row->cpu_exclusive_done.fetch_add(1,std::memory_order_relaxed);
          m_row->cpu_exclusive_cycles.fetch_add(cycles-m_child_cpu,std::memory_order_relaxed);}
        else m_row->exclusive_invalid.fetch_add(1,std::memory_order_relaxed);}
      else if(m_cpu_valid&&end_ok)m_row->query_errors.fetch_add(1,std::memory_order_relaxed); // nonmonotonic CPU raw values
    }
    m_row->outstanding.fetch_sub(1,std::memory_order_release);
  }
  [[nodiscard]] bool Sampled() const noexcept { return m_row!=nullptr&&m_sample; }
  void Count(MatDiag::Counter counter,uint64_t value=1) noexcept {
    if(Sampled())m_row->materialization_counts[static_cast<uint32_t>(counter)].fetch_add(value,std::memory_order_relaxed);
  }
  Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
 private:
  Row* m_row=nullptr;Scope* m_parent;uint64_t m_epoch,m_wall_begin,m_cpu_begin,m_child_wall,m_child_cpu;
  uint32_t m_depth;bool m_sample,m_cpu_valid,m_child_wall_valid,m_child_cpu_valid;
};
inline void Report() noexcept {
  std::printf("function-profile scope=diagnostic frames=%llu->%llu wall_ns=%llu root_sample_every=%u registered=%u overflow=%llu domain=wallns+rawCPUcycles exclusive=selected_same_thread_children includes_child_probe_overhead=1\n",
    static_cast<unsigned long long>(start_frame),static_cast<unsigned long long>(stop_frame),static_cast<unsigned long long>(stop_ns-start_ns),
    sample_every,registered.load(),static_cast<unsigned long long>(registration_overflow.load()));
  const auto count=std::min(registered.load(std::memory_order_acquire),MaxThreads);
  for(uint32_t i=0;i<count;++i) {
    const auto tid=records[i].tid.load(std::memory_order_acquire);if(tid==0)continue;
    for(uint32_t s=0;s<SiteCount;++s){auto& r=records[i].rows[s];if(r.calls.load()==0)continue;
      std::printf("function-profile tid=%u role=%u site=%s calls=%llu sampled=%llu wall_done=%llu cpu_done=%llu cpu_exclusive_done=%llu wall_inclusive_ns=%llu wall_exclusive_ns=%llu cpu_inclusive_cycles=%llu cpu_exclusive_cycles=%llu max_wall_ns=%llu query_wall_ns=%llu query_count=%llu empty_probe_samples=%llu empty_probe_cycles=%llu empty_probe_errors=%llu query_errors=%llu last_error=%llu exclusive_invalid=%llu truncated=%llu outstanding=%llu depth_overflow=%llu snapshot=atomic_fields_not_transactional\n",
        tid,records[i].role.load(),Names[s],r.calls.load(),r.sampled.load(),r.wall_done.load(),r.cpu_done.load(),r.cpu_exclusive_done.load(),
        r.wall_inclusive_ns.load(),r.wall_exclusive_ns.load(),r.cpu_inclusive_cycles.load(),r.cpu_exclusive_cycles.load(),r.max_wall_ns.load(),
        r.query_wall_ns.load(),r.query_count.load(),r.empty_probe_samples.load(),r.empty_probe_cycles.load(),r.empty_probe_errors.load(),
        r.query_errors.load(),r.last_error.load(),r.exclusive_invalid.load(),r.truncated.load(),r.outstanding.load(),r.depth_overflow.load());
      if(r.materialization_counts[static_cast<uint32_t>(MatDiag::Counter::SampledCalls)].load()!=0) {
        std::printf("function-profile-materialization tid=%u role=%u parent=%s population=selected_parent_calls",
          tid,records[i].role.load(),Names[s]);
        for(uint32_t c=0;c<MatDiag::CounterCount;++c)
          std::printf(" %s=%llu",MatDiag::CounterNames[c],r.materialization_counts[c].load());
        std::printf("\n");
      }
    }
  }
  std::fflush(stdout);
}
inline void TickFrame(uint64_t frame) noexcept {
  if(!enabled)return;const auto now=NowNs();
  if(!started&&frame>=650){started=true;start_frame=frame;start_ns=now;deadline_ns=now+10'000'000'000ull;active_epoch.store(1,std::memory_order_release);}
  if(started&&stop_ns==0&&now>=deadline_ns){stop_frame=frame;stop_ns=now;active_epoch.store(0,std::memory_order_release);}
  if(stop_ns!=0&&!reported&&now-stop_ns>=250'000'000ull){reported=true;Report();}
}
} // namespace Libs::Graphics::FunctionProfile
