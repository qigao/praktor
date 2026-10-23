#include "praktor/process_native.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <salts/clock.h>
#include <salts/thread.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>
#define WIDE_CAP 32768u
#define UTF8_CAP (4u*WIDE_CAP)
#define ARGUMENT_CAP 16384u
#endif

typedef struct native_impl {
  bool closing;
#ifdef _WIN32
  HANDLE pending;
  wchar_t first[WIDE_CAP], second[WIDE_CAP], path[WIDE_CAP];
  wchar_t executable[WIDE_CAP], cwd[WIDE_CAP], command[WIDE_CAP], argument[WIDE_CAP];
  char query_image[UTF8_CAP], start_image[UTF8_CAP], evidence_image[UTF8_CAP], entry_image[UTF8_CAP];
  char diagnostic[1024];
#endif
} native_impl;
#ifdef _WIN32
static praktor_process_backend_result okay(void) {
  const praktor_process_backend_result r = {true,0,""}; return r;
}
#endif
static praktor_process_backend_result failure(int error, const char *message) {
  const praktor_process_backend_result r = {false,error,message}; return r;
}
#ifndef _WIN32
static praktor_process_backend_result unsupported(void) { return failure(0,"unsupported_platform"); }
#endif
#ifdef _WIN32
static bool valid_handle(HANDLE h) { return h && h != INVALID_HANDLE_VALUE; }
static void close_handle(HANDLE h) { if (valid_handle(h)) (void)CloseHandle(h); }
static praktor_process_backend_result last_failure(const char *message) {
  const int error = (int)GetLastError(); return failure(error,message);
}
/* The sole pending handle pins an unsuccessful native admission. Its slot is
 * available before CreateProcess, with no post-admission allocation. */
static int recover(native_impl *n) {
  DWORD w;
  if (!n->pending) return SALTS_OK;
  w = WaitForSingleObject(n->pending,0);
  if (w == WAIT_FAILED) return SALTS_EIO;
  if (w != WAIT_OBJECT_0) {
    (void)TerminateProcess(n->pending,2);
    w = WaitForSingleObject(n->pending,0);
    if (w == WAIT_FAILED) return SALTS_EIO;
    if (w != WAIT_OBJECT_0) return SALTS_EBUSY;
  }
  close_handle(n->pending); n->pending = NULL; return SALTS_OK;
}
static praktor_process_backend_result available(native_impl *n) {
  if (!n || n->closing) return failure(ERROR_BUSY,"managed process backend is closing");
  if (recover(n) != SALTS_OK)
    return failure(ERROR_BUSY,"managed process startup cleanup remains pending");
  return okay();
}
static praktor_process_backend_result wide_from(const char *text, wchar_t *out) {
  size_t length = 0;
  int size;
  if (!text) text = "";
  while (length < UTF8_CAP && text[length]) ++length;
  if (length == UTF8_CAP) return failure(ERROR_BUFFER_OVERFLOW,"managed process value exceeds native text bound");
  size = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0);
  if (size <= 0) return last_failure("failed to decode UTF-8 process value");
  if ((size_t)size > WIDE_CAP) return failure(ERROR_BUFFER_OVERFLOW,"managed process value exceeds native text bound");
  if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,out,(int)WIDE_CAP))
    return last_failure("failed to decode UTF-8 process value");
  return okay();
}
static praktor_process_backend_result utf8_from(const wchar_t *text, char *out) {
  if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text,-1,out,(int)UTF8_CAP,NULL,NULL))
    return last_failure("failed to encode process path as UTF-8");
  return okay();
}
static wchar_t *basename(wchar_t *path) {
  wchar_t *name = path, *p;
  for (p = path; *p; ++p)
    if (*p == L'/' || *p == L'\\' || (p == path+1 && *p == L':')) name = p+1;
  return name;
}
static wchar_t *trim_basename(wchar_t *path) {
  wchar_t *end;
  while (*path && iswspace(*path)) ++path;
  end = path+wcslen(path);
  while (end > path && iswspace(end[-1])) --end;
  *end = L'\0'; return basename(path);
}
static praktor_process_backend_result match(native_impl *n, const char *a, const char *b, bool *matches) {
  wchar_t *first, *second;
  int comparison;
  praktor_process_backend_result r = wide_from(a,n->first);
  if (!r.ok) return r;
  r = wide_from(b,n->second); if (!r.ok) return r;
  first = trim_basename(n->first); second = trim_basename(n->second);
  if (!*first || !*second) return failure(ERROR_INVALID_PARAMETER,"managed process image identity is empty");
  SetLastError(ERROR_SUCCESS);
  comparison = CompareStringOrdinal(first,(int)wcslen(first),second,(int)wcslen(second),TRUE);
  if (!comparison) return last_failure("failed to compare managed process image identity");
  *matches = comparison == CSTR_EQUAL; return okay();
}
static praktor_process_backend_result observe(native_impl *n, HANDLE process, uint32_t pid,
                                              DWORD error, const char *message,
                                              praktor_process_snapshot *out) {
  DWORD w = WaitForSingleObject(process,25);
  int e;
  char prefix[sizeof(n->diagnostic)-96u];
  size_t size = 0;
  if (w == WAIT_OBJECT_0) { memset(out,0,sizeof(*out)); return okay(); }
  if (w == WAIT_FAILED) {
    e = (int)GetLastError();
    (void)snprintf(n->diagnostic,sizeof(n->diagnostic),
                   "failed to observe managed process %lu after evidence lookup failed",(unsigned long)pid);
    return failure(e,n->diagnostic);
  }
  /* Bounded even for a caller's diagnostic, including our previous result. */
  if (message) while (size+1u < sizeof(prefix) && message[size]) ++size;
  if (size) memcpy(prefix,message,size);
  prefix[size] = '\0';
  (void)snprintf(n->diagnostic,sizeof(n->diagnostic),"%s for pid %lu; wait_result=%lu",
                 prefix,(unsigned long)pid,(unsigned long)w);
  return failure((int)error,n->diagnostic);
}
static praktor_process_backend_result evidence(native_impl *n, HANDLE process, uint32_t pid,
                                               praktor_process_snapshot *out) {
  DWORD w = WaitForSingleObject(process,0), path_size = WIDE_CAP, session;
  FILETIME creation, exit_time, kernel, user;
  ULARGE_INTEGER token;
  praktor_process_backend_result r;
  memset(out,0,sizeof(*out));
  if (w == WAIT_OBJECT_0) return okay();
  if (w == WAIT_FAILED) return last_failure("failed to observe managed process instance");
  if (!QueryFullProcessImageNameW(process,0,n->path,&path_size))
    return observe(n,process,pid,GetLastError(),"failed to query managed process image identity",out);
  if (path_size >= WIDE_CAP) return failure(ERROR_BUFFER_OVERFLOW,"managed process image identity is too long");
  n->path[path_size] = L'\0';
  r = utf8_from(basename(n->path),n->evidence_image); if (!r.ok) return r;
  if (!ProcessIdToSessionId(pid,&session))
    return observe(n,process,pid,GetLastError(),"failed to query managed process session identity",out);
  if (!GetProcessTimes(process,&creation,&exit_time,&kernel,&user))
    return observe(n,process,pid,GetLastError(),"failed to query managed process instance token",out);
  token.LowPart = creation.dwLowDateTime; token.HighPart = creation.dwHighDateTime;
  out->running = true; out->pid = pid; out->session_id = session;
  out->instance_token = token.QuadPart; out->canonical_image_name = n->evidence_image;
  return okay();
}
static void publish(praktor_process_snapshot *out, const praktor_process_snapshot *s, char *image) {
  if (s->running) memcpy(image,s->canonical_image_name,strlen(s->canonical_image_name)+1u);
  else image[0] = '\0';
  *out = *s; out->canonical_image_name = image;
}
static praktor_process_backend_result query(void *user, const char *image, praktor_process_snapshot *out) {
  native_impl *n = user;
  HANDLE list, process;
  DWORD session, candidate_session, error;
  PROCESSENTRY32W entry;
  praktor_process_snapshot seen;
  bool matches;
  praktor_process_backend_result r = available(n);
  if (!r.ok) return r;
  if (!image || !*image || !out) return failure(ERROR_INVALID_PARAMETER,"managed process identity is empty");
  if (!ProcessIdToSessionId(GetCurrentProcessId(),&session)) return last_failure("failed to determine current process session");
  list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  if (!valid_handle(list)) return last_failure("failed to create managed process snapshot");
  memset(&entry,0,sizeof(entry)); entry.dwSize = sizeof(entry);
  if (!Process32FirstW(list,&entry)) {
    error = GetLastError(); close_handle(list);
    if (error != ERROR_NO_MORE_FILES) return failure((int)error,"failed to enumerate managed processes");
    memset(&seen,0,sizeof(seen)); publish(out,&seen,n->query_image); return okay();
  }
  do {
    r = utf8_from(entry.szExeFile,n->entry_image); if (!r.ok) goto done;
    r = match(n,image,n->entry_image,&matches); if (!r.ok) goto done;
    if (!matches) continue;
    if (!ProcessIdToSessionId(entry.th32ProcessID,&candidate_session)) {
      error = GetLastError();
      if (error == ERROR_INVALID_PARAMETER) continue;
      r = failure((int)error,"failed to query managed process candidate session"); goto done;
    }
    if (candidate_session != session) continue;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);
    if (!valid_handle(process)) {
      error = GetLastError();
      if (error == ERROR_INVALID_PARAMETER) continue;
      r = failure((int)error,"failed to open managed process candidate"); goto done;
    }
    r = evidence(n,process,entry.th32ProcessID,&seen); close_handle(process);
    if (!r.ok) goto done;
    if (!seen.running || seen.session_id != session) continue;
    r = match(n,image,seen.canonical_image_name,&matches); if (!r.ok) goto done;
    if (matches) { publish(out,&seen,n->query_image); goto done; }
  } while (Process32NextW(list,&entry));
  error = GetLastError();
  if (error != ERROR_NO_MORE_FILES) r = failure((int)error,"failed to enumerate managed processes");
  else { memset(&seen,0,sizeof(seen)); publish(out,&seen,n->query_image); r = okay(); }
 done:
  close_handle(list); return r;
}
static bool append(wchar_t *out, size_t *used, wchar_t ch, size_t count) {
  if (count > WIDE_CAP-1u-*used) return false;
  while (count--) out[(*used)++] = ch;
  out[*used] = L'\0'; return true;
}
static bool quote(wchar_t *out, size_t *used, const wchar_t *arg) {
  size_t slashes = 0;
  bool needs_quotes = !*arg || wcspbrk(arg,L" \t\n\v\"") != NULL;
  if (!needs_quotes) {
    while (*arg) if (!append(out,used,*arg++,1)) return false;
    return true;
  }
  if (!append(out,used,L'"',1)) return false;
  for (; *arg; ++arg) {
    if (*arg == L'\\') { ++slashes; continue; }
    if (!append(out,used,L'\\',slashes*(*arg == L'"' ? 2u : 1u)+(*arg == L'"' ? 1u : 0u)) ||
        !append(out,used,*arg,1)) return false;
    slashes = 0;
  }
  return append(out,used,L'\\',slashes*2u) && append(out,used,L'"',1);
}
static praktor_process_backend_result start(void *user, const praktor_process_control_request *request,
                                          praktor_process_snapshot *out) {
  native_impl *n = user;
  size_t used = 0, i;
  DWORD session, observed_session, error, wait_result;
  STARTUPINFOW startup;
  PROCESS_INFORMATION info;
  praktor_process_snapshot seen;
  bool matches;
  praktor_process_backend_result r = available(n);
  if (!r.ok) return r;
  if (!request || !out || !request->executable || !*request->executable ||
      !request->image_name || !*request->image_name || request->argument_count > ARGUMENT_CAP ||
      (request->argument_count && !request->arguments)) return failure(ERROR_INVALID_PARAMETER,"managed process parameters are invalid");
  r = match(n,request->image_name,request->image_name,&matches); if (!r.ok) return r;
  r = wide_from(request->executable,n->executable); if (!r.ok) return r;
  r = wide_from(request->working_directory,n->cwd); if (!r.ok) return r;
  if (!quote(n->command,&used,n->executable)) goto oversized;
  for (i = 0; i < request->argument_count; ++i) {
    if (!request->arguments[i]) return failure(ERROR_INVALID_PARAMETER,"managed process argument is null");
    r = wide_from(request->arguments[i],n->argument); if (!r.ok) return r;
    if (!append(n->command,&used,L' ',1) || !quote(n->command,&used,n->argument)) goto oversized;
  }
  if (!*n->cwd) {
    wchar_t *name;
    memcpy(n->cwd,n->executable,(wcslen(n->executable)+1u)*sizeof(wchar_t));
    name = basename(n->cwd);
    if (name == n->cwd) n->cwd[0] = L'\0';
    else *name = L'\0';
  }
  if (!ProcessIdToSessionId(GetCurrentProcessId(),&session)) return last_failure("failed to determine current process session");
  memset(&startup,0,sizeof(startup)); startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_SHOWNORMAL;
  memset(&info,0,sizeof(info));
  if (!CreateProcessW(n->executable,n->command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,
                       *n->cwd ? n->cwd : NULL,&startup,&info)) return last_failure("failed to start managed process");
  n->pending = info.hProcess; close_handle(info.hThread);
  if (!ProcessIdToSessionId(info.dwProcessId,&observed_session)) {
    error = GetLastError(); r = failure((int)error,"managed process started in an unexpected session"); goto reject;
  }
  if (observed_session != session) { r = failure(ERROR_INVALID_DATA,"managed process started in an unexpected session"); goto reject; }
  wait_result = WaitForSingleObject(n->pending,0);
  if (wait_result == WAIT_OBJECT_0) { r = failure(ERROR_PROCESS_ABORTED,"managed process exited during startup"); goto reject; }
  if (wait_result == WAIT_FAILED) { r = last_failure("failed to observe managed process startup"); goto reject; }
  r = evidence(n,n->pending,info.dwProcessId,&seen); if (!r.ok) goto reject;
  matches = false;
  if (seen.running && seen.session_id == session) r = match(n,request->image_name,seen.canonical_image_name,&matches);
  if (!r.ok) goto reject;
  if (!matches) { r = failure(ERROR_INVALID_DATA,"managed process started with mismatched identity evidence"); goto reject; }
  publish(out,&seen,n->start_image); close_handle(n->pending); n->pending = NULL; return okay();
 reject:
  (void)recover(n); return r;
 oversized:
  return failure(ERROR_FILENAME_EXCED_RANGE,"managed process command line exceeds native bound");
}
static praktor_process_backend_result validate(native_impl *n, const praktor_process_snapshot *snapshot,
                                               DWORD access, HANDLE *out) {
  DWORD session;
  HANDLE process;
  praktor_process_snapshot seen;
  praktor_process_backend_result r = available(n);
  bool matches;
  if (!r.ok) return r;
  if (!snapshot || !snapshot->running || !snapshot->pid || !snapshot->instance_token ||
      !snapshot->canonical_image_name || !*snapshot->canonical_image_name)
    return failure(ERROR_INVALID_PARAMETER,"managed process snapshot lacks stable identity evidence");
  if (!ProcessIdToSessionId(GetCurrentProcessId(),&session)) return last_failure("failed to determine current process session");
  process = OpenProcess(access|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,snapshot->pid);
  if (!valid_handle(process)) return last_failure("failed to open managed process instance for action");
  r = evidence(n,process,snapshot->pid,&seen); if (!r.ok) goto done;
  if (!seen.running) { r = failure(ERROR_NOT_FOUND,"managed process instance is no longer running"); goto done; }
  matches = seen.pid == snapshot->pid && seen.session_id == snapshot->session_id &&
      seen.session_id == session && seen.instance_token == snapshot->instance_token;
  if (matches) { r = match(n,snapshot->canonical_image_name,seen.canonical_image_name,&matches); if (!r.ok) goto done; }
  if (!matches) { r = failure(ERROR_INVALID_DATA,"managed process instance identity changed before action"); goto done; }
  *out = process; return okay();
 done:
  close_handle(process); return r;
}
typedef struct close_windows { DWORD pid; bool matched, posted; DWORD first_error; } close_windows;
static BOOL CALLBACK close_window(HWND window, LPARAM parameter) {
  close_windows *c = (close_windows *)parameter;
  DWORD pid = 0;
  if (!c || !IsWindow(window)) return TRUE;
  (void)GetWindowThreadProcessId(window,&pid);
  if (pid != c->pid || GetWindow(window,GW_OWNER)) return TRUE;
  c->matched = true;
  if (PostMessageW(window,WM_CLOSE,0,0)) c->posted = true;
  else if (!c->first_error) c->first_error = GetLastError();
  return TRUE;
}
static praktor_process_backend_result request_stop(void *user, const praktor_process_snapshot *snapshot) {
  native_impl *n = user;
  HANDLE process = NULL;
  close_windows c;
  DWORD error;
  praktor_process_backend_result r = validate(n,snapshot,0,&process);
  if (!r.ok) return r;
  memset(&c,0,sizeof(c)); c.pid = snapshot->pid;
  SetLastError(ERROR_SUCCESS);
  if (!EnumWindows(close_window,(LPARAM)&c)) {
    error = GetLastError(); r = failure((int)(error ? error : ERROR_GEN_FAILURE),"failed to enumerate managed process windows");
  } else if (c.matched && !c.posted) {
    r = failure((int)(c.first_error ? c.first_error : ERROR_GEN_FAILURE),"failed to request managed process close");
  }
  close_handle(process); return r;
}
static praktor_process_backend_result terminate(void *user, const praktor_process_snapshot *snapshot) {
  native_impl *n = user;
  HANDLE process = NULL;
  DWORD error, w;
  praktor_process_backend_result r = validate(n,snapshot,PROCESS_TERMINATE,&process);
  if (!r.ok) return r;
  if (!TerminateProcess(process,1)) {
    error = GetLastError(); w = WaitForSingleObject(process,0);
    if (w == WAIT_FAILED) r = last_failure("failed to observe managed process termination");
    else if (w != WAIT_OBJECT_0) r = failure((int)error,"failed to terminate managed process");
  }
  close_handle(process); return r;
}
#else
static praktor_process_backend_result query(void *u, const char *i, praktor_process_snapshot *s) {
  (void)u; (void)i; (void)s; return unsupported();
}
static praktor_process_backend_result start(void *u, const praktor_process_control_request *r, praktor_process_snapshot *s) {
  (void)u; (void)r; (void)s; return unsupported();
}
static praktor_process_backend_result request_stop(void *u, const praktor_process_snapshot *s) {
  (void)u; (void)s; return unsupported();
}
static praktor_process_backend_result terminate(void *u, const praktor_process_snapshot *s) {
  (void)u; (void)s; return unsupported();
}
#endif
int praktor_process_native_init(praktor_process_native *native) {
  if (!native || native->impl) return SALTS_EINVAL;
  native->impl = calloc(1,sizeof(native_impl));
  return native->impl ? SALTS_OK : SALTS_ENOMEM;
}
int praktor_process_native_bind(praktor_process_native *native, praktor_process_control_host *out) {
  if (!native || !native->impl || !out || ((native_impl *)native->impl)->closing) return SALTS_EINVAL;
  memset(out,0,sizeof(*out)); out->user = native->impl;
  out->query = query; out->start = start; out->request_stop = request_stop; out->terminate = terminate;
  out->poll_interval_ms = 50; return SALTS_OK;
}
int praktor_process_native_close(praktor_process_native *native, uint32_t budget_ms) {
  native_impl *n;
#ifdef _WIN32
  uint64_t began;
  int r;
#endif
  if (!native || !native->impl) return SALTS_OK;
  n = native->impl; n->closing = true;
#ifdef _WIN32
  began = cmeta_hrtime();
  do {
    r = recover(n);
    if (r == SALTS_OK) break;
    if (r == SALTS_EIO || (cmeta_hrtime()-began)/UINT64_C(1000000) >= budget_ms) return r;
    cmeta_sleep_ms(1);
  } while (true);
#else
  (void)budget_ms;
#endif
  free(n); native->impl = NULL; return SALTS_OK;
}
void praktor_process_native_destroy_join(praktor_process_native *native) {
#ifdef _WIN32
  while (praktor_process_native_close(native,10) != SALTS_OK) cmeta_sleep_ms(1);
#else
  (void)praktor_process_native_close(native,0);
#endif
}
praktor_process_backend_result praktor_process_native_match_candidate(
    praktor_process_native *native, const char *a, const char *b,
    uint32_t candidate_session, uint32_t current_session, bool *matches) {
  if (!native || !native->impl || !matches) return failure(SALTS_EINVAL,"invalid native process match request");
  *matches = false;
#ifdef _WIN32
  if (candidate_session != current_session) return okay();
  return match(native->impl,a,b,matches);
#else
  (void)a; (void)b; (void)candidate_session; (void)current_session; return unsupported();
#endif
}
praktor_process_backend_result praktor_process_native_match_snapshot(
    praktor_process_native *native, const praktor_process_snapshot *a,
    const praktor_process_snapshot *b, uint32_t current_session, bool *matches) {
  if (!native || !native->impl || !a || !b || !matches) return failure(SALTS_EINVAL,"invalid native process match request");
  *matches = false;
#ifdef _WIN32
  if (!a->running || !b->running || !a->pid || a->pid != b->pid || a->session_id != b->session_id ||
      b->session_id != current_session || !a->instance_token || a->instance_token != b->instance_token) return okay();
  return match(native->impl,a->canonical_image_name,b->canonical_image_name,matches);
#else
  (void)current_session; return unsupported();
#endif
}
praktor_process_backend_result praktor_process_native_observe_failure(
    praktor_process_native *native, void *process, uint32_t pid, uint32_t error,
    const char *message, praktor_process_snapshot *out) {
  if (!native || !native->impl || !out) return failure(SALTS_EINVAL,"invalid native process observation request");
#ifdef _WIN32
  return observe(native->impl,process,pid,error,message,out);
#else
  (void)process; (void)pid; (void)error; (void)message; return unsupported();
#endif
}
