## target: Assimp::Importer::ReadFileFromMemory

### P2.1 Init Sequence
**Claim:** Only a default-constructed `Assimp::Importer` object is required before calling `ReadFileFromMemory`. The constructor allocates a default `DefaultIOSystem`, the internal importer list and post-process-step list, so no explicit "library init" call is needed. `SetIOHandler()` may optionally be called first to pre-install a custom `IOSystem`, but `ReadFileFromMemory` internally swaps in its own `MemoryIOSystem` for the duration of the call regardless.
**Evidence:**
> include/assimp/Importer.hpp:138: `Importer();` — "Call ReadFile() to start the import process. The configuration property table is initially empty."
> code/Common/Importer.cpp:149-173: `Importer::Importer()` allocates `pimpl->mIOHandler = new DefaultIOSystem;`, `pimpl->mProgressHandler = new DefaultProgressHandler();`, `GetImporterInstanceList(pimpl->mImporter);`, `GetPostProcessingStepInstanceList(pimpl->mPostProcessingSteps);`
> code/Common/Importer.cpp:488-500: inside `ReadFileFromMemory`, `IOSystem* io = pimpl->mIOHandler;` is saved, then `pimpl->mIOHandler = nullptr;` and `SetIOHandler(new MemoryIOSystem((const uint8_t*)pBuffer,pLength,io));` — the previous handler (default or user-set) is chained into the `MemoryIOSystem` and restored afterward (`SetIOHandler(io);` at line 508/512/517).

### P2.2 Parameter Construction
**Claim:** `pBuffer` must be a non-null pointer to at least `pLength` bytes; `pLength` must be non-zero; `pHint` (if non-null) must be ≤ `Importer::MaxLenHint` (200) characters and, when non-empty, should be a bare extension string without a leading dot (e.g. "obj", "fbx", "gltf") — it is concatenated after a literal `.` onto a synthetic filename. A null `pHint` is normalized to `""`. `pFlags` is any bitwise-OR of `aiPostProcessSteps` (aiProcess_* in postprocess.h) or 0.
**Evidence:**
> include/assimp/Importer.hpp:460-464: `const aiScene *ReadFileFromMemory(const void *pBuffer, size_t pLength, unsigned int pFlags, const char *pHint = "");`
> code/Common/Importer.cpp:490-496: `if (pHint == nullptr) { pHint = ""; } if (!pBuffer || !pLength || strlen(pHint) > MaxLenHint ) { pimpl->mErrorString = "Invalid parameters passed to ReadFileFromMemory()"; return nullptr; }`
> include/assimp/Importer.hpp:129: `static const unsigned int MaxLenHint = 200;`
> code/Common/Importer.cpp:503-505: `static const size_t BufSize(Importer::MaxLenHint + 28); char fbuff[BufSize]; ai_snprintf(fbuff, BufSize, "%s.%s",AI_MEMORYIO_MAGIC_FILENAME,pHint);`

### P2.3 Object Lifecycle
**Claim:** Lifecycle is: construct `Importer` → (optional) `SetIOHandler`/`SetProperty*` → `ReadFileFromMemory` (fills internal `pimpl->mScene`) → consume via `GetScene()`/`GetOrphanedScene()` or call `ApplyPostProcessing()` again → `FreeScene()` or destroy the `Importer` (which frees everything, including the owned scene). A single `Importer` instance may be reused for multiple `ReadFile*` calls; each call frees any previous scene first.
**Evidence:**
> code/Common/Importer.cpp:607-611 (inside `ReadFile`, invoked by `ReadFileFromMemory`): `if (pimpl->mScene) { ASSIMP_LOG_DEBUG("(Deleting previous scene)"); FreeScene(); }`
> include/assimp/Importer.hpp:155-158: `~Importer()` — "The object kept ownership of the imported data, which now will be destroyed along with the object."
> code/Common/Importer.cpp:177-198: destructor deletes `pimpl->mImporter` list, post-processing steps, `pimpl->mIOHandler`, `pimpl->mProgressHandler`, `pimpl->mScene`, `pimpl->mPPShared`, then `pimpl`.

### P2.4 Return Value Handling
**Claim:** Return value is `const aiScene*`; `nullptr` indicates failure (bad parameters, no matching importer, parser exception, or `aiProcess_ValidateDataStructure` rejecting the scene). On failure, `GetErrorString()` holds a human-readable message and `GetException()` may hold the caught exception. On success, the returned pointer equals `pimpl->mScene` and remains owned by the `Importer` (valid until next `ReadFile*`, `FreeScene()`, or destructor).
**Evidence:**
> include/assimp/Importer.hpp:427-432: "If the import fails, nullptr is returned. A human-readable error description can be retrieved by calling GetErrorString()."
> code/Common/Importer.cpp:493-496: invalid-parameter path sets `pimpl->mErrorString` and `return nullptr;`
> code/Common/Importer.cpp:509-519: `catch(const DeadlyImportError &e) { pimpl->mErrorString = e.what(); pimpl->mException = std::current_exception(); SetIOHandler(io); return ExceptionSwallower<const aiScene*>()(); } catch(...) { pimpl->mErrorString = "Unknown exception"; ... }`
> code/Common/Importer.cpp:522: `return pimpl->mScene;` (success path, may itself be nullptr if the pipeline could not build a scene, e.g. ValidateDS failure at lines 729-735)

### P2.5 Cleanup Sequence
**Claim:** No explicit cleanup call is required by the caller between successive `ReadFileFromMemory`/`ReadFile` calls (the library frees the previous scene automatically). If the caller wants to release the scene early (without destroying the Importer) call `FreeScene()`, which deletes `pimpl->mScene` and clears error/exception state. If the caller takes ownership via `GetOrphanedScene()`, the caller — not the Importer — must `delete` the returned `aiScene*`. On program/Importer teardown, destroying the `Importer` frees the scene, IO handler, progress handler, importer list, post-process steps and shared post-process info.
**Evidence:**
> code/Common/Importer.cpp:382-393: `void Importer::FreeScene() { delete pimpl->mScene; pimpl->mScene = nullptr; pimpl->mErrorString = std::string(); pimpl->mException = std::exception_ptr(); }`
> include/assimp/Importer.hpp:532-550: `GetOrphanedScene()` doc — "The application is now responsible for deleting the scene... aiScene's are exclusively maintained, allocated and deallocated by Assimp..."
> code/Common/Importer.cpp:429-442: `GetOrphanedScene()` sets `pimpl->mScene = nullptr;` after capturing it, transferring ownership to the caller.
> code/Common/Importer.cpp:177-198: destructor frees everything including `pimpl->mScene`.

### P2.6 API Existence
**Claim:** All functions referenced below exist in the current codebase at the cited locations.
**Evidence:**
> include/assimp/Importer.hpp:460 `ReadFileFromMemory` (target)
> include/assimp/Importer.hpp:419 `ReadFile(const char*, unsigned int)`
> include/assimp/Importer.hpp:484 `ApplyPostProcessing(unsigned int)`
> include/assimp/Importer.hpp:332 `SetIOHandler(IOSystem*)`
> include/assimp/Importer.hpp:219 `SetPropertyInteger(const char*, int)`
> include/assimp/Importer.hpp:242 `SetPropertyString(const char*, const std::string&)`
> include/assimp/Importer.hpp:236 `SetPropertyFloat(const char*, ai_real)`
> include/assimp/Importer.hpp:530 `GetScene() const`
> include/assimp/Importer.hpp:551 `GetOrphanedScene()`
> include/assimp/Importer.hpp:504 `FreeScene()`
> code/Common/BaseImporter.cpp:150 `BaseImporter::SetupProperties` (core, called indirectly)
> Definitions all found in code/Common/Importer.cpp (ReadFileFromMemory:485, ReadFile:587, ApplyPostProcessing:790, SetIOHandler:297, SetPropertyInteger:1063, SetPropertyFloat:1075, SetPropertyString:1087, GetScene:421, GetOrphanedScene:429, FreeScene:382).

### P2.7 Co-call Constraints
**Claim:** `SetIOHandler(nullptr)` / `SetIOHandler(custom)` is a self-contained setter, not paired with a "close"; the Importer destructor or a subsequent `SetIOHandler` call deletes the previous handler. `ReadFileFromMemory` internally performs its own SetIOHandler(install-memory)/SetIOHandler(restore-previous) pair around the call — callers must not assume their custom IOSystem is active during a `ReadFileFromMemory` call. `GetOrphanedScene()` is the "take ownership" counterpart to the implicit "Importer owns the scene" default; once called, the caller is responsible for `delete`-ing the scene (no library-side free pairs with it). `ValidateDSProcess::ExecuteOnScene` can itself null out `pimpl->mScene`, so any code following `ReadFile`/`ApplyPostProcessing` must re-check `GetScene()`/the return value for nullptr before further use.
**Evidence:**
> code/Common/Importer.cpp:497-500,508,512,517: save/replace/restore `pimpl->mIOHandler` around the internal `ReadFile` call.
> code/Common/Importer.cpp:306-310 (`SetIOHandler`): `delete pimpl->mIOHandler; pimpl->mIOHandler = pIOHandler;` — previous handler is deleted when a new one is installed.
> code/Common/Importer.cpp:429-441 (`GetOrphanedScene`): ownership transfer, no matching "return ownership" API.
> code/Common/Importer.cpp:729-735 / 811-817: `ValidateDSProcess ds; ds.ExecuteOnScene(this); if (!pimpl->mScene) { return nullptr; }`

### P2.8 Prerequisite State
**Claim:** No external context/connection is required; the only state that matters is the `Importer`'s own `pimpl` (IO handler, importer/post-process-step lists, property tables) which is fully initialized by the constructor. `SetPropertyInteger/Float/String` calls made before `ReadFileFromMemory` populate `pimpl->mIntProperties/mFloatProperties/mStringProperties`, which `BaseImporter::SetupProperties()` and individual `InternReadFile()` implementations read via `GetPropertyInteger/Float/String` (e.g. `AI_CONFIG_PP_RVC_FLAGS`, `AI_CONFIG_GLOB_MEASURE_TIME`) to select internal parsing/limit behavior — they must be set before the `ReadFileFromMemory` call since they are read during that call's dispatch to the importer.
**Evidence:**
> code/Common/Importer.cpp:621: `std::unique_ptr<Profiler> profiler(GetPropertyInteger(AI_CONFIG_GLOB_MEASURE_TIME, 0) ? new Profiler() : nullptr);` — read during `ReadFile`, invoked from `ReadFileFromMemory`.
> fuzz/assimp_postprocess_fuzzer.cc:108-110: `importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, static_cast<int>(rvc)); const aiScene *sc = importer.ReadFileFromMemory(data, modelSize, 0, nullptr);` — property set before the memory read.
> code/Common/BaseImporter.cpp:150: `void BaseImporter::SetupProperties(const Importer *) {}` (default no-op, overridden per-format importer) is invoked as part of `BaseImporter::ReadFile` dispatch inside `imp->ReadFile(this, pFile, pimpl->mIOHandler)` (Importer.cpp:709).

### P2.9 Result Usage
**Claim:** The returned `const aiScene*` (or the object obtained via `GetOrphanedScene()`) is consumed by: (a) `Assimp::Exporter::ExportToBlob`/`Export` to serialize the scene to another format; (b) `Importer::ApplyPostProcessing()` to run further post-process steps on the same scene without re-parsing; (c) direct traversal of the `aiScene` fields (`mRootNode`, `mMeshes`, `mMaterials`, etc.) by test code; (d) re-import round trips where the exported blob's bytes are fed back into a second `ReadFileFromMemory` call.
**Evidence:**
> fuzz/assimp_fuzzer.cc:62-69: `const aiScene *sc = importer.ReadFileFromMemory(data, dataSize, flags, nullptr); if (sc == nullptr) return 0; Exporter exporter; exporter.ExportToBlob(sc, "fbx");`
> fuzz/assimp_postprocess_fuzzer.cc:110-115: `const aiScene *sc = importer.ReadFileFromMemory(data, modelSize, 0, nullptr); if (sc == nullptr) return 0; importer.ApplyPostProcessing(flags);`
> test/unit/utObjImportExport.cpp:267-280: `scene = importer.GetOrphanedScene(); ... const aiExportDataBlob *blob = exporter.ExportToBlob(scene, "obj"); ... const aiScene *sceneReImport = importer.ReadFileFromMemory(blob->data, blob->size, aiProcess_ValidateDataStructure);`
> test/unit/utExport.cpp:87: `EXPECT_TRUE(im->ReadFileFromMemory(blob->data,blob->size,0,"dae"));` — result used directly as a boolean/pointer check.
> fuzz/assimp_roundtrip_fuzzer.cc:62 (same pattern as assimp_fuzzer.cc, re-exports to multiple formats — see file for loop over export formats).

### P2.10 Behaviour Controls
**Claim:** Three inputs select different processing paths: (1) `pHint` narrows/selects which `BaseImporter` is tried via `BaseImporter::HasExtension`/signature-sniffing `CanRead`; an empty hint forces full signature-based auto-detection across all registered importers. (2) `pFlags` is a bitmask of `aiPostProcessSteps`; `aiProcess_ValidateDataStructure` is handled specially (run via `ValidateDSProcess` before `ScenePreprocessor`, not through the generic post-process-step loop), while every other bit is matched against each registered `BaseProcess::IsActive(pFlags)` in `ApplyPostProcessing`'s loop, each one calling `BaseProcess::ExecuteOnScene`. (3) Per-format behavior is further tuned via `SetPropertyInteger/Float/String` config keys (e.g. `AI_CONFIG_PP_RVC_FLAGS` controlling `aiProcess_RemoveComponent`'s component mask, `AI_CONFIG_GLOB_MEASURE_TIME` toggling profiling) consumed by `BaseImporter::SetupProperties()` / individual `InternReadFile()`.
**Evidence:**
> code/Common/Importer.cpp:640-677 (`ReadFile`): extension-based `HasExtension` filtering then `importer.CanRead(pFile, pimpl->mIOHandler, true)` signature check, then fallback full scan `pimpl->mImporter[a]->CanRead(pFile, pimpl->mIOHandler, true)`.
> code/Common/Importer.cpp:727-736: `if (pFlags & aiProcess_ValidateDataStructure) { ValidateDSProcess ds; ds.ExecuteOnScene(this); ... }` run before `ScenePreprocessor`/general post-process pipeline.
> code/Common/Importer.cpp:834-847 (`ApplyPostProcessing`): `if( process->IsActive( pFlags)) { ... process->ExecuteOnScene( this ); }` — per-step opt-in gated by the flag bitmask.
> fuzz/assimp_postprocess_fuzzer.cc:50-64,108: `kSteps[]` array of `aiProcess_*` constants selected bit-by-bit from fuzz bytes, plus `AI_CONFIG_PP_RVC_FLAGS` built from another fuzz byte.

### P2.11 Input Expectations
**Claim:** `ReadFileFromMemory` makes no assumptions about file format beyond: `pBuffer`/`pLength` must describe a non-empty byte range (checked for null/zero), and `pHint` (if given) must be ≤200 chars. The actual format-specific header/structure/size expectations are enforced deeper, per-format, inside each `BaseImporter::CanRead`/`InternReadFile` (not in the target itself) — e.g. magic-byte/signature checks via `CanRead(..., true)` (signature-based probing) when extension alone is ambiguous or `pHint` is empty/unrecognized. The library wraps the buffer in a `MemoryIOSystem` keyed by the synthetic filename `"$$$___magic___$$$.<pHint>"`, so downstream code that special-cases "loaded from memory" (e.g. USD loader) detects this magic prefix rather than real path semantics.
**Evidence:**
> code/Common/Importer.cpp:493-496: `if (!pBuffer || !pLength || strlen(pHint) > MaxLenHint )` — the only input validation the target itself performs.
> include/assimp/MemoryIOWrapper.h:60-61: `#define AI_MEMORYIO_MAGIC_FILENAME "$$$___magic___$$$"` / `#define AI_MEMORYIO_MAGIC_FILENAME_LENGTH 17`
> code/Common/Importer.cpp:505: `ai_snprintf(fbuff, BufSize, "%s.%s",AI_MEMORYIO_MAGIC_FILENAME,pHint);` — synthetic filename used for extension/HasExtension matching.
> code/AssetLib/USD/USDLoaderImplTinyusdz.cpp:98: `bool is_load_from_mem{ pFile.substr(0, AI_MEMORYIO_MAGIC_FILENAME_LENGTH) == AI_MEMORYIO_MAGIC_FILENAME };` — a per-format consumer detecting the magic filename.
> include/assimp/BaseImporter.h:85-89: "CanRead() is used to check if the importer can handle a specific file... this function returns true, the importer then calls ReadFile() which calls InternReadFile()..." — per-format contract, not enforced by the target.

## Other Logic-Group Entries: call protocol and combination with the target

### Entry: `Assimp::Importer::ReadFile(const char *pFile, unsigned int pFlags)`
**Claim:** File-based sibling of the target; `ReadFileFromMemory` internally calls this overload with the synthesized magic filename (code/Common/Importer.cpp:507: `ReadFile(fbuff,pFlags);`). It is the real driver of importer selection/parsing/post-processing (see body at Importer.cpp:587-786). A harness variant could call it directly against a real on-disk temp file instead of going through `ReadFileFromMemory`, but then must manage file creation/cleanup itself and an `IOSystem` that can `Exists()`/`Open()` that path (default `DefaultIOSystem` uses real filesystem I/O). Requires prior `Importer()` construction; no extra init. Returns `const aiScene*`/nullptr exactly like the target (owned by Importer, same `GetErrorString`/`FreeScene`/`GetOrphanedScene` semantics). `pFlags` has the same `aiProcess_*` semantics as in the target.
**Evidence:**
> include/assimp/Importer.hpp:419-421: `const aiScene *ReadFile(const char *pFile, unsigned int pFlags);`
> code/Common/Importer.cpp:507: `ReadFile(fbuff,pFlags);` — called by the target.
> code/Common/Importer.cpp:613-619: `if( !pimpl->mIOHandler->Exists( pFile)) { pimpl->mErrorString = "Unable to open file \"" + pFile + "\"."; ... return nullptr; }` — requires the active IOSystem to report the path as existing (satisfied automatically for the magic buffer path because `MemoryIOSystem::Exists` recognizes the magic filename — see include/assimp/MemoryIOWrapper.h:153,169).
**Combination with target:** Not needed as a separate call in a `ReadFileFromMemory`-based harness (it's already invoked internally); only relevant if the harness wants a second, independent "file path" variant of the same fuzz input (e.g. write `data` to a temp file first).

### Entry: `Assimp::Importer::ApplyPostProcessing(unsigned int pFlags)`
**Claim:** Must be called on an `Importer` that currently holds a non-null scene (i.e., after a successful `ReadFileFromMemory`/`ReadFile`); it does nothing (returns nullptr) if `pimpl->mScene` is null, and returns the existing scene unchanged if `pFlags == 0`. It re-enters the exact same post-processing loop the target uses internally (`BaseProcess::IsActive`/`ExecuteOnScene`), so it is a direct way to fuzz different `pFlags` combinations against a scene parsed once. `aiProcess_ValidateDataStructure` in `pFlags` triggers `ValidateDSProcess::ExecuteOnScene`, which can null the scene (caller must re-check the return / `GetScene()`). Return value is `const aiScene*`, same object as `GetScene()` or nullptr; no separate free needed (still owned by Importer).
**Evidence:**
> include/assimp/Importer.hpp:484: `const aiScene *ApplyPostProcessing(unsigned int pFlags);`
> code/Common/Importer.cpp:795-802: `if (!pimpl->mScene) { return nullptr; } if (!pFlags) { return pimpl->mScene; }`
> code/Common/Importer.cpp:811-817: ValidateDS handling identical to the one inside `ReadFile`.
> code/Common/Importer.cpp:834-847: step loop `if( process->IsActive( pFlags)) { process->ExecuteOnScene ( this ); } if( !pimpl->mScene) { break; }`
**Combination with target:** `fuzz/assimp_postprocess_fuzzer.cc:110-115` pattern — call `ReadFileFromMemory(data, modelSize, 0, nullptr)` with `pFlags=0` (no post-processing at parse time) then call `ApplyPostProcessing(flags)` separately with a fuzz-derived flag combination, checking `sc == nullptr` after the read before applying.

### Entry: `Assimp::Importer::SetIOHandler(IOSystem *pIOHandler)`
**Claim:** Optional configuration call, usable before `ReadFileFromMemory`/`ReadFile`. Passing `nullptr` resets to a fresh `DefaultIOSystem`; passing a non-null custom `IOSystem*` transfers ownership to the Importer (it will `delete` the previous handler and eventually the new one in its destructor or on the next `SetIOHandler` call). Because `ReadFileFromMemory` always substitutes its own `MemoryIOSystem` for the duration of the call (chaining the previously-set handler in as a fallback — see `MemoryIOSystem` ctor `(pBuffer,pLength,io)`), a custom IOSystem set via `SetIOHandler` before `ReadFileFromMemory` matters only for secondary file accesses the memory-wrapped importer may attempt (e.g. multi-file formats like OBJ/MTL) — not for the primary buffer being read.
**Evidence:**
> include/assimp/Importer.hpp:332: `void SetIOHandler(IOSystem *pIOHandler);`
> code/Common/Importer.cpp:297-312: null→`new DefaultIOSystem()`; non-null & different → `delete pimpl->mIOHandler; pimpl->mIOHandler = pIOHandler;`
> code/Common/Importer.cpp:500: `SetIOHandler(new MemoryIOSystem((const uint8_t*)pBuffer,pLength,io));` inside the target — `io` is the previously active handler, used as a fallback for files the MemoryIOSystem doesn't own.
**Combination with target:** Call once (optionally) right after constructing `Importer`, before `ReadFileFromMemory`. No "close" call exists; cleanup happens automatically at destruction or at the next `SetIOHandler` call.

### Entry: `Assimp::Importer::SetPropertyInteger` / `SetPropertyFloat` / `SetPropertyString`
**Claim:** Optional configuration calls, no required ordering relative to each other; must be called before `ReadFileFromMemory`/`ReadFile` for a given parse to see the property, because they are read during import dispatch (`BaseImporter::SetupProperties`, and ad hoc `GetPropertyInteger/Float/String` calls inside individual `InternReadFile()` implementations and post-process steps such as `RemoveComponentsProcess` reading `AI_CONFIG_PP_RVC_FLAGS`). They store into independent per-type maps (`pimpl->mIntProperties`/`mFloatProperties`/`mStringProperties`), so setting one type does not affect lookups of another type for the same key. Return value `bool` indicates whether a previous value for that key existed (not success/failure of applying it). No corresponding "clear" API is required; values persist across multiple `ReadFile*` calls on the same `Importer` unless explicitly overwritten.
**Evidence:**
> include/assimp/Importer.hpp:219,236,242: declarations; doc at 214-218 "Property of different types (float, int, string ..) are kept on different stacks..."
> code/Common/Importer.cpp:1063-1095: `SetPropertyInteger/Float/String` each call `SetGenericProperty<T>(pimpl->mXxxProperties, szName, value)` and return the previous-existed bool.
> code/Common/Importer.cpp:621: `GetPropertyInteger(AI_CONFIG_GLOB_MEASURE_TIME, 0)` read during `ReadFile`, which is invoked by the target.
> fuzz/assimp_postprocess_fuzzer.cc:108: `importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, static_cast<int>(rvc));` set before `ReadFileFromMemory` at line 110.
**Combination with target:** Call zero or more of these right after constructing `Importer` (and optionally after `SetIOHandler`), before invoking `ReadFileFromMemory`, to steer per-format/per-post-process behavior.

### Entry: `Assimp::Importer::GetOrphanedScene()`
**Claim:** Must be called after a successful `ReadFileFromMemory`/`ReadFile` (i.e. target returned non-null, or at least a scene was previously loaded); it is legal to call even if the target returned nullptr but then it returns nullptr too (nothing to orphan). After the call, the `Importer` no longer owns the scene (`pimpl->mScene` set to null), so the caller must `delete` the returned `aiScene*` itself, and further `GetScene()`/`GetOrphanedScene()` calls on the same `Importer` return nullptr until a new `ReadFile*` is performed. `GetScene()` is the non-destructive counterpart (keeps ownership with the Importer).
**Evidence:**
> include/assimp/Importer.hpp:551 declaration; doc 532-550: "releases the scene from the ownership of the Importer instance. The application is now responsible for deleting the scene."
> code/Common/Importer.cpp:429-442: `aiScene* s = pimpl->mScene; pimpl->mScene = nullptr; ... return s;`
> test/unit/utObjImportExport.cpp:267-280: `scene = importer.GetOrphanedScene(); ... exporter.ExportToBlob(scene, "obj"); ...` — used right after a `ReadFile` to pass to an independent `Exporter`.
**Combination with target:** After `const aiScene *sc = importer.ReadFileFromMemory(...);` checked non-null, a harness may call `aiScene *owned = importer.GetOrphanedScene();` to take over the scene (then it alone is responsible for `delete owned;`), instead of letting the `Importer`'s destructor/`FreeScene()` free it.

### Not part of this feature / not found
- No separate "close"/"commit" API pairs with `ReadFileFromMemory` itself beyond the generic `FreeScene()`/destructor pattern described above; searched `Importer.hpp` for any `Close*`/`Commit*` method — none exist.
- `ApplyCustomizedPostProcessing(BaseProcess *rootProcess, bool requestValidation)` exists (include/assimp/Importer.hpp:486) but was not listed as an entry in the Logic Group; it is a lower-level variant of `ApplyPostProcessing` taking a caller-supplied `BaseProcess` tree — out of scope unless the harness specifically wants to test custom post-process trees.

## Build notes

**Claim:** Fuzzers are plain C++ translation units compiled directly against the Assimp public headers under `include/` (and generated headers under `build/include/`), then linked against the static `libassimp.a` (built with all importers/exporters enabled) plus the bundled static zlib, `pthread`, `dl`, and whatever OSS-Fuzz's `$LIB_FUZZING_ENGINE` provides. No `extern "C"` wrapping is needed for `LLVMFuzzerTestOneInput` beyond the standard OSS-Fuzz convention, since the harness itself is C++ (uses `Assimp::Importer`, `Assimp::Exporter` C++ classes directly).
**Evidence:**
> ossfuzz_view/assimp-c1/build.sh:23-34: CMake configured with `-DASSIMP_BUILD_ZLIB=ON -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=ON -DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=ON`
> ossfuzz_view/assimp-c1/build.sh:40-51 (`build_fuzzer` helper):
> `$CXX $CXXFLAGS -I../include -I../build/include -c "$source_file" -o "${fuzzer_name}.o"`
> `$CXX $CXXFLAGS $LIB_FUZZING_ENGINE "${fuzzer_name}.o" -o "$OUT/${fuzzer_name}" ./lib/libassimp.a ./contrib/zlib/libzlibstatic.a -lpthread -ldl`
> ossfuzz_view/assimp-c1/build.sh:54: `build_fuzzer "assimp_fuzzer" "../fuzz/assimp_fuzzer.cc"` — invoked per fuzzer source file (assimp_fuzzer.cc, assimp_roundtrip_fuzzer.cc, assimp_postprocess_fuzzer.cc, assimp_fuzzer_obj.cc, assimp_fuzzer_gltf.cc, etc.).
> fuzz/assimp_fuzzer.cc:41-45: `#include <assimp/cimport.h>` `#include <assimp/Importer.hpp>` `#include <assimp/Exporter.hpp>` `#include <assimp/scene.h>` `#include <assimp/postprocess.h>` — target header included via the `-I../include` path (`include/assimp/Importer.hpp`), consistent with the declaration location given in the task.
> ossfuzz_view/assimp-c1/Dockerfile:17-21: `FROM gcr.io/oss-fuzz-base/base-builder`, `apt-get install -y cmake ninja-build zip`, `git clone --depth 1 --recursive https://github.com/assimp/assimp.git`, `WORKDIR assimp`, `COPY build.sh run_tests.sh $SRC/` — confirms C++/CMake/Ninja build, no special sanitizer flags beyond what `base-builder`/`$CXXFLAGS` injects.