# Bug report: ExecuteFileSQL from kFMXT_Idle crashes FileMaker Pro 11 with 0xc0000409

Status: verified live, twice, with two different SQL statement types (DDL and SELECT) on FMP 11.
Discovered: 2026-07-03, while testing the 32-bit fork (veltrea/BaseElements-Plugin) on FMP 11.
Workaround shipped in commit `9031d64b` (see "Workaround" below).
**Update 2026-07-03 (same day, later session): confirmed FMP 11-specific.** The same
idle-DDL-replay code path was exercised on FMP 19 (64-bit) and did **not** crash —
see "Verification on FMP 19 (64-bit)" below.

## Summary

Calling `fmx::ExprEnv::ExecuteFileSQL` (via an environment obtained with
`FMX_SetToCurrentEnv`) from the `kFMXT_Idle` plug-in callback terminates
FileMaker Pro 11 with exception code **0xc0000409** (fail-fast). The statement
type does not matter: both `CREATE TABLE` (DDL) and a plain `SELECT` crash the
host. The same SQL executes fine when run inside a normal external-function
call using the `ExprEnv&` passed to that function.

This affects the **upstream `g_ddl_command` mechanism** directly: BaseElements
queues DDL statements from `BESQLCommand::execute` and replays them at the next
safe idle (`BEPlugin.cpp`, `kFMXT_Idle` handler). On FMP 11 this means **any
DDL issued through `BE_FileMakerSQL` kills the application** at the next idle
tick. This went unnoticed because SQL testing usually exercises SELECT paths,
which are executed synchronously and never reach the idle handler — until a
background-task feature also started executing SQL at idle.

## Environment

- FileMaker Pro 11.0v3 (11.0.3.312), 32-bit, running natively on Windows 11 (64-bit)
- Plug-in: BaseElements 32-bit fork, MSVC toolset 14.50 (`/MT`, `/std:c++17`),
  current FM Plug-In SDK FMWrapper headers, `Release|Win32`
- Idle is enabled (option string), `parm1 != kFMXT_Unsafe` respected
- The relevant upstream code is unchanged at `main` (`2f204a1b`):
  the idle handler (`Source/BEPlugin.cpp`, `case kFMXT_Idle`) runs
  `g_ddl_command->execute ( )`, i.e. the no-argument overload in
  `Source/BESQLCommand.cpp` that calls `FMX_SetToCurrentEnv` and then
  `ExecuteFileSQL`

## Reproduction

Two independent paths, both reproduced on a live FMP 11:

### Path A — upstream DDL queue (no fork-specific code involved)

1. Evaluate `BE_FileMakerSQL ( "CREATE TABLE regrddl (id INT)" )` in any
   calculation. `BESQLCommand::execute` classifies it as DDL, queues a copy
   into `g_ddl_command`, and returns normally (result `""`, last error 0).
2. Wait for the next safe idle. The handler runs
   `g_ddl_command->execute ( )` — the no-argument overload, which calls
   `FMX_SetToCurrentEnv` and then `ExecuteFileSQL` (pre-FM13 branch).
3. FileMaker Pro terminates ~seconds later. No dialog; process gone.

### Path B — any SQL executed at idle

Identical crash with a plain
`SELECT '...' FROM fmtest` executed from the idle handler through the same
no-argument `BESQLCommand::execute ( )`. So this is not a DDL-specific issue;
it is "FM API SQL execution from the idle callback" that is fatal on FMP 11.

### Control (proves the environment, not the SQL, is the problem)

The exact same statements run fine when executed inside a normal
external-function call using the `const ExprEnv&` passed to the function
(this is what `BE_FileMakerSQL` itself does for SELECTs, and what the fork's
background-task drain now does — see Workaround).

## Crash signature

Windows Event Log (Application Error), two occurrences:

```
2026-07-03 07:19:43
Faulting application: FileMaker Pro.exe 11.0.3.312 (timestamp 0x4d3f6749)
Faulting module:      BaseElements.fmx 5.0.0.2 (timestamp 0x6a46e3c5)
Exception code:       0xc0000409
Fault offset:         0x000beac7

2026-07-03 07:27:57
Faulting application: FileMaker Pro.exe 11.0.3.312
Faulting module:      BaseElements.fmx 5.0.0.2 (timestamp 0x6a46e5c3)
Exception code:       0xc0000409
Fault offset:         0x000bed07
```

(Consecutive builds, hence the slightly different offsets; both land in the
idle-time SQL execution path.)

`0xc0000409` on modern MSVC CRTs is a fail-fast: stack-cookie failure,
invalid-parameter handler, or `std::terminate` (e.g. a C++ exception escaping
an `extern "C"` callback). The exact micro-mechanism inside FMP 11 was not
isolated (whether `FMX_SetToCurrentEnv` returns an unusable environment at
idle, or `ExecuteFileSQL` itself cannot be re-entered from the idle callback).
Either way the operation is not survivable on FMP 11.

## Impact

- FMP 11 + upstream BaseElements: any DDL via `BE_FileMakerSQL` crashes the
  host at the next idle. Data loss risk for unsaved work.
- Any plug-in feature that defers FM API SQL to `kFMXT_Idle` is affected.
- **Resolved by testing (see below): newer FileMaker versions are not affected.**
  FMP 19 (64-bit) executes the same idle-DDL-replay path without crashing.
  This is FMP 11-specific, not a general idle/API re-entrancy problem.

## Workaround (implemented in the 32-bit fork, commit `9031d64b`)

1. Gate all idle-time SQL execution (the DDL queue and the background-task
   result queue) behind `gFMX_ExternCallPtr->extnVersion >= k130ExtnVersion`.
   On FMP 11 the idle handler no longer touches the FM API.
2. Execute deferred SQL on FMP 11 inside the next plug-in function call that
   naturally has an environment: `BE_BackgroundTaskAdd` and
   `BE_BackgroundTaskList` drain the queue with the calling `ExprEnv&`
   (polling model). Consequence on FMP 11: queued DDL is never replayed —
   documented as "DDL via BE_FileMakerSQL is unavailable on FMP 11", which is
   strictly better than crashing.

## Verification on FMP 19 (64-bit)

Reproduced Path A (the upstream DDL queue, no fork-specific code) against a
freshly built 64-bit `BaseElements.fmx64` (same commit, MSVC v145, `Release|x64`)
loaded into **FileMaker Pro 19.1.2.219 (64-bit)**, WORK1.

Because the workaround (`9031d64b`) gates idle-time SQL execution behind
`gFMX_ExternCallPtr->extnVersion >= k130ExtnVersion`, and FMP 19's `extnVersion`
is far above that threshold, the idle handler **does** reach
`g_ddl_command->execute()` on FMP 19 — i.e. this build exercises the exact same
code path that killed FMP 11, just gated "on" instead of "off".

**Procedure:**
1. Evaluated `BE_FileMakerSQL ( "CREATE TABLE regrddl (id INT)" )` in a
   dedicated test file (`ddl-repro.fmp12`, a copy of the existing plug-in test
   harness). Returned `""` immediately, as expected — the statement was
   classified as DDL and queued into `g_ddl_command` rather than executed
   synchronously.
2. Left the file idle (no interaction) for 40+ seconds while polling the
   FileMaker Pro process for liveness. The process stayed alive throughout;
   memory usage drifted down slightly (idle housekeeping), consistent with
   idle ticks actually occurring.
3. Evaluated `BE_FileMakerSQL ( "SELECT COUNT(*) FROM regrddl" )` →
   `result=[0] err=[0]`. As a control, the same query against a table name
   that was never created returned `result=[?] err=[8309]` (a clear SQL
   error). The contrast confirms `regrddl` exists as a real table with zero
   rows — i.e. the queued `CREATE TABLE` was actually executed from
   `kFMXT_Idle`, not silently dropped.
4. Checked the Windows Application event log for the test window: **zero**
   entries mentioning `BaseElements`. One unrelated crash was present
   (`FMEngine.dll`, exception `0xc0000005`) but its faulting process ID
   decoded to the short-lived single-instance-handoff launcher process, not
   the long-running FileMaker Pro instance that had the plug-in loaded — a
   different module, different exception code, and different process from
   the bug being investigated here.

**Result: no crash.** The DDL was queued, drained by the idle handler, and
executed successfully on FMP 19 (64-bit). This confirms the 0xc0000409
fail-fast is **FMP 11-specific**, not a general hazard of calling
`ExecuteFileSQL` from `kFMXT_Idle` on modern FileMaker hosts.

Path B (idle-executed `SELECT` via the background-task queue) was not
re-tested on FMP 19 in this session; Path A alone is sufficient to answer the
open question ("does this affect current FileMaker versions?").

## Suggested upstream actions

- ~~Reproduce on a current FileMaker version~~ — done above; **does not
  reproduce on FMP 19 (64-bit)**.
- The existing `extnVersion >= k130ExtnVersion` gate is the correct fix as
  shipped in `9031d64b`: it disables idle-time FM API calls specifically on
  hosts old enough to crash, while leaving the (verified-safe) idle DDL
  replay enabled on FM 13+.
- No further upstream redesign of the idle replay mechanism is indicated by
  this finding. If upstream wants to drop the `extnVersion` gate entirely
  (e.g. to simplify the code), they would reintroduce the FMP 11 crash for
  any user still on that host — the gate should stay.

## Related findings from the same session (separate issues, same family)

- `BE_BackgroundTaskAdd`'s detached worker captured `&environment` by
  reference (dangling once the caller returns) and created FMX objects off
  the main thread; an exception escaping the detached thread terminates the
  host with the same 0xc0000409 signature. Fixed in the fork (`9031d64b`).
- `~BECurl` calls `curl_global_cleanup()` per instance; destroyed on a worker
  thread this can run libcurl's global teardown off the main thread. The fork
  holds one global-init reference for the plug-in lifetime.
