#include "system/managed_process.hpp"
#include "praktor/process_native.h"
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace Praktor::System {
namespace {
praktor_process_snapshot toC(const ManagedProcessSnapshot& s) {
  return {s.state == ManagedProcessState::Running,s.pid,s.session_id,s.instance_token,s.canonical_image_name.c_str()};
}
ManagedProcessSnapshot fromC(const praktor_process_snapshot& s) {
  return {s.running ? ManagedProcessState::Running : ManagedProcessState::NotRunning,
          s.pid,s.session_id,s.instance_token,s.canonical_image_name ? s.canonical_image_name : ""};
}
struct NativeOwner {
  praktor_process_native native{};
  praktor_process_control_host hooks{};
  NativeOwner() {
    const int status = praktor_process_native_init(&native);
    if (status != SALTS_OK) throw std::runtime_error("C native process initialization failed");
    if (praktor_process_native_bind(&native,&hooks) != SALTS_OK) {
      praktor_process_native_destroy_join(&native);
      throw std::runtime_error("C native process binding failed");
    }
  }
  ~NativeOwner() { praktor_process_native_destroy_join(&native); }
  NativeOwner(const NativeOwner&) = delete;
  NativeOwner& operator=(const NativeOwner&) = delete;
};
/* Value conversion only. Native handles, Unicode/path/argv processing, identity
 * evidence, failed-start recovery and bounded storage belong to the C host. */
class NativeBackend final : public IManagedProcessBackend {
  NativeOwner owner;
public:
  ManagedProcessResult<ManagedProcessSnapshot> query(const ManagedProcessIdentity& identity) override {
    praktor_process_snapshot snapshot{};
    const auto r = owner.hooks.query(owner.hooks.user,identity.image_name.c_str(),&snapshot);
    return {r.ok,r.ok ? fromC(snapshot) : ManagedProcessSnapshot{},r.native_error,r.message};
  }
  ManagedProcessResult<ManagedProcessSnapshot> start(const ManagedProcessParams& params) override {
    // Reserve conversion storage before native admission. Returning the C
    // snapshot must not require a new C++ allocation after successful spawn.
    ManagedProcessResult<ManagedProcessSnapshot> result;
    result.value.canonical_image_name.reserve(131072);
    result.message.reserve(1024);
    std::vector<const char*> args;
    for (const auto& arg : params.arguments) args.push_back(arg.c_str());
    const praktor_process_control_request request = {
        static_cast<praktor_process_operation>(params.operation),params.identity.image_name.c_str(),
        params.executable.c_str(),args.data(),args.size(),params.working_directory.c_str(),
        params.startup_timeout_ms,params.stop_timeout_ms,params.force_terminate};
    praktor_process_snapshot snapshot{};
    const auto r = owner.hooks.start(owner.hooks.user,&request,&snapshot);
    result.ok = r.ok; result.native_error = r.native_error; result.message.assign(r.message);
    if (r.ok) {
      result.value.state = snapshot.running ? ManagedProcessState::Running : ManagedProcessState::NotRunning;
      result.value.pid = snapshot.pid; result.value.session_id = snapshot.session_id;
      result.value.instance_token = snapshot.instance_token;
      result.value.canonical_image_name.assign(snapshot.canonical_image_name ? snapshot.canonical_image_name : "");
    }
    return result;
  }
  ManagedProcessCommandResult requestStop(const ManagedProcessSnapshot& snapshot) override {
    const auto s = toC(snapshot); const auto r = owner.hooks.request_stop(owner.hooks.user,&s);
    return {r.ok,r.native_error,r.message};
  }
  ManagedProcessCommandResult terminate(const ManagedProcessSnapshot& snapshot) override {
    const auto s = toC(snapshot); const auto r = owner.hooks.terminate(owner.hooks.user,&s);
    return {r.ok,r.native_error,r.message};
  }
};
}
std::unique_ptr<IManagedProcessBackend> createManagedProcessBackend() { return std::make_unique<NativeBackend>(); }
#ifdef _WIN32
/* Legacy test entry points delegate to the same C identity/evidence routines. */
namespace WinDetail {
ManagedProcessResult<bool> matchesManagedProcessCandidate(std::string_view configured,
    std::string_view canonical, std::uint32_t candidate_session, std::uint32_t current_session) {
  NativeOwner owner; bool matches = false;
  const auto a = std::string(configured), b = std::string(canonical);
  const auto r = praktor_process_native_match_candidate(&owner.native,a.c_str(),b.c_str(),candidate_session,current_session,&matches);
  return {r.ok,matches,r.native_error,r.message};
}
ManagedProcessResult<bool> matchesManagedProcessSnapshot(const ManagedProcessSnapshot& expected,
    const ManagedProcessSnapshot& observed, std::uint32_t current_session) {
  NativeOwner owner; bool matches = false; const auto a = toC(expected), b = toC(observed);
  const auto r = praktor_process_native_match_snapshot(&owner.native,&a,&b,current_session,&matches);
  return {r.ok,matches,r.native_error,r.message};
}
ManagedProcessResult<ManagedProcessSnapshot> observeManagedProcessEvidenceFailure(
    HANDLE process, std::uint32_t pid, DWORD error, std::string message) {
  NativeOwner owner; praktor_process_snapshot s{};
  const auto r = praktor_process_native_observe_failure(&owner.native,process,pid,error,message.c_str(),&s);
  return {r.ok,r.ok ? fromC(s) : ManagedProcessSnapshot{},r.native_error,r.message};
}
}
#endif
} // namespace Praktor::System
