# Bug report: ExecuteFileSQL from kFMXT_Idle crashes FileMaker Pro 11 with 0xc0000409

Status: verified live, twice, with two different SQL statement types (DDL and SELECT).
Discovered: 2026-07-03, while testing the 32-bit fork (veltrea/BaseElements-Plugin) on FMP 11.
Workaround shipped in commit `9031d64b` (see "Workaround" below).

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
- Unknown whether newer FileMaker versions are affected — upstream has shipped
  the idle DDL replay for years, which suggests FM 13+ tolerates it, but that
  is untested here. Worth a targeted test on a current FM before assuming.

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

## Suggested upstream actions

- Reproduce on a current FileMaker version; if it crashes there too, the DDL
  replay needs the same redesign upstream.
- If it only affects old hosts, gate the idle replay by `extnVersion` as above.
- Never let the idle handler run FM API calls without a version check on hosts
  older than the oldest version the idle path was actually tested on.

## Related findings from the same session (separate issues, same family)

- `BE_BackgroundTaskAdd`'s detached worker captured `&environment` by
  reference (dangling once the caller returns) and created FMX objects off
  the main thread; an exception escaping the detached thread terminates the
  host with the same 0xc0000409 signature. Fixed in the fork (`9031d64b`).
- `~BECurl` calls `curl_global_cleanup()` per instance; destroyed on a worker
  thread this can run libcurl's global teardown off the main thread. The fork
  holds one global-init reference for the plug-in lifetime.
