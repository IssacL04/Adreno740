# Adreno740

A740 kernel GPU driver.

The implementation covers A740 on SM8550. GPU commands and scanout copy have been verified on sheng. Platform inputs provide firmware, power policy, and memory resources.

Build and interface details: [documentation](https://unixnu.com/drivers/adreno740). Component sources and dependencies: [catalog](https://unixnu.com/reference/components).

The development source also contains A830 (chip 0x44050001) ring/CP, IRQ, counter and six-buffer context variants. `NativeAdrenoA830HwInit` programs Gen8 protection, UBWC and restore state; its final stage requires successful ZAP authentication and a completed secure-exit fence. The OnePlus 13T package connects these helpers to SM8750 power, GMU and SMMU operations before publishing the user client. Run `python3 scripts/check-adreno-a830.py` from the main checkout for both generations' software-peer checks. The OnePlus 13T production XNU compile and prelink have passed. The OnePlus 13T Run 78 user report confirms Metal registration, submissions/completions 1039/1039 and GPU presentation 518/0. Controlled shader correctness, measured performance and long-duration stability remain unqualified.

`NativeAdrenoDiagnostics.h` exposes a nonblocking diagnostic snapshot of plugin opens/closes, the latest opener, core method calls/status and submission/completion/fence-poll-miss/poison counts. Core method/status are captured together. It retains the last successful sample with `cached=1` when the GPU lock is busy; absent services or contention before any sample return false without changing the output. These approximate counters do not establish correct shader pixels or display FPS.

Ordinary user BOs now use system-backed pageable/wired memory, with allocation/free and fence polling outside the publication lock. Page tables and object tables grow as needed; each process has its own GPU address space. A830 retains the SQE/AQE mappings in every address space, uses its Gen8 CP table-update packet and counter address, and supplies the board context before publication. Explicit contiguous requests retain their requirement. Software-peer checks cover system memory, per-process mappings, fragmented allocation through retirement and failure retention.

Diagnostics also retain the latest nonzero core method/status pair across later successful calls. It is historical evidence and may record an ordinary timed wait; the method identifies which operation returned it.

The main update to a9a2cbb retains the upstream A740 two-ring preemption implementation. A830 explicitly rejects that preemption setup and keeps its six-buffer Gen8 context, SQE/AQE mappings, reset/table packets and counter address. On iPadOS 17 headers without GPU-role APIs, submit priority uses the existing foreground fallback; newer headers retain the upstream role policy.
