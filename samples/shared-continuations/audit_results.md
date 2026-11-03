# Additional audit receipts (2026-11-02)

These results strengthen the core primitive. They do not qualify WASIp3 or all
cooperative scheduling corner cases. No WASI P3 or Component Model conformance
assertions have been executed against either draft; see READINESS.md and the two
explicit NOT_RUN inventories.

| Platform | Runtime / check | Actual coverage | Result |
|---|---|---|---|
| macOS arm64 | TinyWasm ASan | Rust runtime, dependencies, rebuilt std and all three C clients | Pass |
| macOS arm64 | TinyWasm Miri | All six Rust C API tests, including callback memory reads around suspension/growth | Pass |
| macOS arm64 | TinyWasm nightly tail ABI | Verified tail cfg, main fixture, edges, standard C API | Pass |
| macOS arm64 | WAMR ASan | C runtime and main/edge C clients | Pass |
| macOS arm64 | WAMR CTest | Main and edge suites | 2/2 pass |
| Physical Windows x64 | TinyWasm ASan | C clients only; Rust runtime remains uninstrumented | Pass, partial coverage |
| Physical Windows x64 | TinyWasm native stable Rust | Host execution and C API unit tests | 7/7 and 6/6 pass |
| Physical Windows x64 | WAMR ASan | C/C++ runtime and main/edge C clients | Pass |

Each ASan mode first detected a deliberate heap-buffer-overflow in its isolated
canary executable. The expected canary failure is not a fixture finding. The
runtime tests then passed without address-error reports. WAMR's Windows C and C++
release flags were `/O1 /Zi /fsanitize=address /DNDEBUG`; sanitizer and LTO
measurement builds use distinct directories. These ASan-only passes do not erase
the previously documented baseline UBSan alignment findings.

The Windows audit used fresh checkouts of published fork revisions:

- TinyWasm: `485de14db5ebe7c32db0b51929ada3611eae78f7`
- WAMR: `4968dde873cd2ac9ef3713041c8b4acc790ed0f2`

Only the explicit new fixture/runner/header test overlay was then copied in; the
TinyWasm C API source additionally received the new cfg(test) memory regression.
Production runtime behavior is unchanged by this audit. The measured main C source
remains byte-for-byte unchanged. Windows checked its inputs against these hashes:

| Input | SHA-256 |
|---|---|
| fixture.c | `d38be8465b16cfbdf09e752637e5750738b461a8899795ad7f4c2667a6006cdd` |
| edges.c | `126bca9f3b5977c5a2b24ae6eb033f388524d273df5e71a150871ad754be557c` |
| run.py | `bb8aed39f39332eac04652e40c1b4693c09fe38a4317f7ae0516c82c7e360549` |
| continuation.rs, including new test | `b50af175229130b8c189d3c10f65a4ca75cfc1a98928c1ace517f49b70a427f7` |

Earlier copied-source scratch runs are not used as these audit receipts. Native
ASan does not instrument Windows Rust: the client-only row must not be promoted
to an end-to-end Rust sanitizer claim. Supported-host Rust+std ASan and Miri
provide separate evidence, not a substitute Windows pass.

`audit_receipts.csv` records the cases, coverage and hashes of preserved raw logs.
The raw receipts remain in the local/physical-device task directories. CI now
explicitly exercises Windows stable and nightly sysv64 tail calling conventions,
Windows client-only ASan, Linux Rust+std+C ASan, Miri, and WAMR Windows/Linux
runtime ASan. Job definitions are not pass receipts; inspect the draft checks for
hosted outcomes. The nightly job verifies the requested tail ABI actually enabled.
