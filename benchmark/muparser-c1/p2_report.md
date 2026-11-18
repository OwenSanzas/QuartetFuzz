## target: mu::Parser::Eval (ParserBase::Eval)

### Summary of target and overloads
`mu::Parser` publicly inherits `ParserBase`'s evaluation API (no override is declared in `muParser.h` for Eval, so the three overloads used by callers are `ParserBase::Eval()`, `ParserBase::Eval(int&)`, `ParserBase::Eval(value_type*, int)`, declared at muparser/include/muParserBase.h:118-120.

```
muparser/include/muParserBase.h:118   value_type Eval() const;
muparser/include/muParserBase.h:119   value_type* Eval(int& nStackSize) const;
muparser/include/muParserBase.h:120   void Eval(value_type* results, int nBulkSize);
```

Definitions: muparser/src/muParserBase.cpp:1849 (`Eval()`), :1862 (`Eval(int&)`), :1892 (`Eval(value_type*,int)`).

```
muparser/src/muParserBase.cpp:1849-1852
  value_type ParserBase::Eval() const
  {
      return (this->*m_pParseFormula)();
  }
```
`m_pParseFormula` is a function pointer switched between `ParseString` (first call / after any Define*/SetExpr call, via ReInit) and `ParseCmdCode`/`ParseCmdCodeShort` (subsequent calls, reusing cached RPN bytecode). This dispatch is the "lazy build-then-execute" behaviour described in the task.

### P2.1 Init Sequence
**Claim:** Before calling `Eval()` a caller must (a) construct a `Parser` object (which runs `InitCharSets/InitFun/InitConst/InitOprt` to register built-ins), and (b) call `SetExpr()` at least once to install a non-empty formula string. Any `Define*` call issued afterwards forces a re-parse on the next `Eval()` via `ReInit()`.
**Evidence:**
> muparser/src/muParser.cpp:108-117: `Parser::Parser() : ParserBase() { AddValIdent(IsVal); InitCharSets(); InitFun(); InitConst(); InitOprt(); }`
> muparser/src/muParserBase.cpp:441-453: `ParserBase::SetExpr(...)` stores the formula in the token reader and calls `ReInit()`.
> muparser/src/muParserBase.cpp:1289-1292: `CreateRPN()` (invoked from `ParseString`, itself invoked from `Eval`) throws `ecUNEXPECTED_EOF` if `m_pTokenReader->GetExpr().length()==0`, i.e. if `SetExpr` was never called (or called with an empty string equivalent after trimming).
> muparser/src/muParserBase.cpp:90-110: `ParserBase::ParserBase()` sets `m_pParseFormula=&ParserBase::ParseString` and calls `InitTokenReader()` — construction alone is not enough, a formula is still required.

### P2.2 Parameter Construction
**Claim:** `Eval()` takes no parameters. The two sibling overloads require caller-allocated buffers/ints:
- `Eval(int& nStackSize)`: `nStackSize` is an out-parameter (any int, overwritten); return value is a pointer into the parser's **internal** `m_vStackBuffer` (not caller-owned, must not be freed, valid only until next Eval/SetExpr/Define* call).
- `Eval(value_type* results, int nBulkSize)`: caller must allocate `results` with at least `nBulkSize` `value_type` (`double` by default) elements; `nBulkSize` must match the length of the "bulk" vectors (arrays) attached through `DefineVar` pointers that the bulk-mode expression indexes per-i.
**Evidence:**
> muparser/include/muParserBase.h:118-120 (signatures above).
> muparser/src/muParserBase.cpp:1862-1877: `Eval(int&)` returns `&m_vStackBuffer[1]` (internal vector) and sets `nStackSize = m_nFinalResultIdx`.
> muparser/src/muParserBase.cpp:1892-1918 `Eval(value_type*, int)`: `CreateRPN(); ... results[i] = ParseCmdCodeBulk(i, nThreadID);` for `i` in `[0,nBulkSize)` — `results` must have `nBulkSize` writable slots.
> muparser/samples/example1/example1.cpp:431-456 `CalcBulk()`: allocates `x,y,result` arrays of size `nBulkSize`, calls `parser.DefineVar("x",x); parser.DefineVar("y",y); ... parser.Eval(result, nBulkSize);`.

### P2.3 Object Lifecycle
**Claim:** Create `Parser` (stack or heap) → configure via `Define*`/`SetExpr`/`SetDecSep`/`SetArgSep`/etc (any order, each resets to string-parse mode through `ReInit()`) → call one of the `Eval` overloads one or more times → optionally read results/errors → destroy object (regular C++ destructor, no explicit "close" call).
**Evidence:**
> muparser/src/muParserBase.cpp:256-262 `ReInit()`: `m_pParseFormula = &ParserBase::ParseString; m_vStringBuf.clear(); m_vRPN.clear(); m_pTokenReader->ReInit();` — called from `SetExpr` (441), `DefineVar` (637), `DefineConst` (654), `AddCallback` (395, backing `DefineFun`/`DefineOprt`/`DefineInfixOprt`/`DefinePostfixOprt`), `DefineStrConst` (613).
> muparser/src/muParserBase.cpp:140 `ParserBase::~ParserBase()` — ordinary destructor, no special teardown API required by users.
> muparser/samples/example1/example1.cpp:399-413: `mu::Parser p; p.DefineFun(...); for(...) { p.DefineStrConst(...); p.SetExpr(...); p.Eval(); p.ClearConst(); }` shows repeated configure→Eval cycles on one instance.

### P2.4 Return Value Handling
**Claim:** `Eval()`/`Eval(int&)` do not return an error code; errors are reported solely via C++ exceptions of type `mu::ParserError` (aka `ParserBase::exception_type`) thrown from inside `CreateRPN`/`ParseString`/`ParseCmdCode*`/`Error()`. Callers must wrap `Eval()` (and `SetExpr`, which can also throw on bad locale/too-long expr) in try/catch.
**Evidence:**
> muparser/src/muParserBase.cpp:1595-1598 `ParserBase::Error(...)`: `throw exception_type(a_iErrc, a_sTok, m_pTokenReader->GetExpr(), a_iPos);`
> muparser/src/muParserBase.cpp:1557-1582 `ParseString()` wraps `CreateRPN()` in try/catch that attaches the formula to the exception and rethrows.
> muparser/include/muParserError.h:74-100: `class API_EXPORT_CXX ParserError { ... GetExpr(); GetMsg(); GetPos(); GetToken(); GetCode(); }` — the accessor API used to inspect an error.
> muparser/samples/example1/example1.cpp:586-595: `catch (mu::Parser::exception_type& e) { ... e.GetMsg() ... e.GetExpr() ... e.GetToken() ... e.GetPos() ... e.GetCode() ... }`

### P2.5 Cleanup Sequence
**Claim:** No manual free of parser-owned structures is required: `m_vRPN`, `m_vStackBuffer`, `m_vStringBuf` etc. are plain members destroyed with the `Parser` object. The only caller-owned memory is (a) buffers passed to `DefineVar` (must outlive the parser / outlive any Eval call that uses them) and (b) the `results` buffer passed to bulk `Eval`, both allocated/freed by the caller, not the parser.
**Evidence:**
> muparser/src/muParserBase.cpp:623-638 `DefineVar` just stores the raw pointer in `m_VarDef[a_sName]=a_pVar;` — the parser never allocates/copies/frees it.
> muparser/samples/example1/example1.cpp:431-469 `CalcBulk()`: `value_type* x = new value_type[...]; ... parser.DefineVar("x",x); ... delete[] x; delete[] y; delete[] result;` in both normal and `catch(...)` paths — illustrating that freeing variable/result buffers is the caller's job, done after all Eval calls finish.
> muparser/src/muParserBase.cpp:140 destructor — no explicit `Close()`/`Destroy()` API exists for `Parser`/`ParserBase`.

### P2.6 API Existence
**Claim:** All project functions cited below exist in the current source tree at the given locations (checked by direct grep/read, not inferred).
**Evidence:** see per-check citations throughout this report (all point to real file:line matches found via Grep/Read in muparser/include/muParserBase.h and muparser/src/muParserBase.cpp, muparser/src/muParser.cpp).

### P2.7 Co-call Constraints
**Claim:** There is no lock/unlock pairing; the relevant "pairing" is configuration-call → implicit `ReInit()` → next `Eval()` re-tokenizes. Also `Eval(int&)`/`GetNumResults()` are paired: `GetNumResults()` must be called after `Eval(int&)` (or `Eval()`) to learn how many of the returned stack slots are valid, OR `Eval(int&)`'s own out-parameter `nStackSize` can be used instead (both reflect `m_nFinalResultIdx`).
**Evidence:**
> muparser/src/muParserBase.cpp:1862-1877: `Eval(int&)` sets `nStackSize = m_nFinalResultIdx;`
> muparser/src/muParserBase.cpp:1886-1889: `GetNumResults() { return m_nFinalResultIdx; }`
> muparser/samples/example1/example1.cpp:568-584: `parser.Eval(); ... int nNum = parser.GetNumResults(); if (nNum>1) { value_type* v = parser.Eval(nNum); ... }` — calls plain `Eval()` first, then `GetNumResults()`, then `Eval(int&)` to fetch the multi-result array; shows the co-call pattern explicitly.

### P2.8 Prerequisite State
**Claim:** Before `Eval()` succeeds, the token reader/tokenizer state must be consistent with: a non-empty formula string (`SetExpr`), a locale/decimal-separator setup that doesn't collide with the argument separator, and (for `cmVAR` tokens to resolve) all variable names used in the expression previously registered via `DefineVar` (or a var-factory set through `SetVarFactory`). Undefined variables make `CreateRPN` throw unless `IgnoreUndefVar` is set (only done internally by `GetUsedVar`).
**Evidence:**
> muparser/src/muParserBase.cpp:441-445 `SetExpr`: `if (m_pTokenReader->GetArgSep() == std::use_facet<numpunct<char_type>>(s_locale).decimal_point()) Error(ecLOCALE);`
> muparser/src/muParserBase.cpp:722-739 `GetUsedVar()`: temporarily calls `m_pTokenReader->IgnoreUndefVar(true)` before `CreateRPN()` to tolerate unknown vars; restores to `false` afterward — showing that by default (outside GetUsedVar) undefined variables are a hard error during `CreateRPN`/`Eval`.
> muparser/src/muParser.cpp:362-365 `SetVarFactory` installs a var-creation callback as an alternative to explicit `DefineVar` calls, consumed by the tokenizer when it meets unknown identifiers (`ParserTokenReader`).

### P2.9 Result Usage
**Claim:** The direct scalar result of `Eval()` is typically only printed/compared by callers (it is a `value_type`, no further parser API consumes it). The pointer result of `Eval(int&)` is consumed by simple array indexing (`v[i]`) up to the returned/queried count; the `results` buffer filled by bulk `Eval` is likewise read by plain indexing by the caller, not fed back into another parser API. `GetNumResults()` is the dedicated consumer of the "how many results are valid" question tied to `Eval(int&)`.
**Evidence:**
> muparser/samples/example1/example1.cpp:568 `mu::console() << ... << parser.Eval() << ...` — scalar result just printed.
> muparser/samples/example1/example1.cpp:571-584 `int nNum = parser.GetNumResults(); ... value_type* v = parser.Eval(nNum); for (i=0;i<nNum;++i) console()<<v[i];` — `GetNumResults()`/`Eval(int&)` combo, result array consumed by indexed read only.
> muparser/src/muParserDLL.cpp:224,237,247: the C DLL wrappers `mupEval`, `mupEvalMulti`, `mupEvalBulk` simply forward to `p->Eval()`, `p->Eval(*nNum)`, `p->Eval(a_res,nSize)` and return/propagate the same raw values/pointers to C callers — no further internal library consumer reprocesses the Eval result.
> muparser/src/muParserBase.cpp:1862-1877 (`Eval(int&)`'s only "sink" of its own is `m_vStackBuffer`, which is overwritten on next `Eval`/`CreateRPN`, so a caller must copy out values it needs before calling Eval again).

### P2.10 Behaviour Controls
**Claim:** Which code path `Eval()`'s internal dispatch takes is controlled by: (1) whether the RPN byte code is empty (`m_vRPN.GetSize()`) — selects `ParseString` (tokenize+build) vs `ParseCmdCode`/`ParseCmdCodeShort` (execute cached bytecode); (2) whether the formula reduces to a single value-only RPN of size 2, selecting the fast `ParseCmdCodeShort` path vs general `ParseCmdCode`; (3) which `Eval` overload is called (scalar/multi/bulk) selects `ParseCmdCode` vs `ParseCmdCodeBulk`; (4) `EnableOptimizer`/`EnableBuiltInOprt` toggle constant folding and built-in operator availability (affecting what `AddCallback`/`DefineOprt` will accept without `ecBUILTIN_OVERLOAD`); (5) `SetDecSep`/`SetThousandsSep`/`SetArgSep` change numeric-literal and separator tokenizing in the next `CreateRPN`; (6) `DefineNameChars`/`DefineOprtChars`/`DefineInfixOprtChars` change which characters the tokenizer accepts for identifiers/operators, directly gating `IsVar`/`IsFunTok`/`IsOprt` recognition; (7) `SetVarFactory` changes how unresolved identifiers are treated (auto-create variable vs. `ecUNASSIGNABLE_TOKEN`/`ecUNDEFINED` errors).
**Evidence:**
> muparser/src/muParserBase.cpp:1862-1871 `Eval(int&)`: branches on `if (m_vRPN.GetSize() > 0) ParseCmdCode(); else ParseString();`
> muparser/src/muParserBase.cpp:1557-1575 `ParseString()`: `if (m_vRPN.GetSize() == 2) { ...; m_pParseFormula = &ParserBase::ParseCmdCodeShort; ...} else { ...; m_pParseFormula = &ParserBase::ParseCmdCode; ...}`
> muparser/include/muParserBase.h:131-132 `EnableOptimizer(bool)`, `EnableBuiltInOprt(bool)`.
> muparser/src/muParserBase.cpp:580-595 `DefineOprt(...)`: `for (int i = 0; m_bBuiltInOp && i < cmENDIF; ++i) if (a_sName==c_DefaultOprt[i]) Error(ecBUILTIN_OVERLOAD,...)` — gated by `m_bBuiltInOp` (set via `EnableBuiltInOprt`).
> muparser/include/muParserBase.h:127-129 `SetDecSep(char_type)`, `SetThousandsSep(char_type=0)`, `ResetLocale()`.
> muparser/src/muParserBase.cpp:468-489 `DefineNameChars/DefineOprtChars/DefineInfixOprtChars` set `m_sNameChars/m_sOprtChars/m_sInfixOprtChars` used later by `ValidNameChars()/ValidOprtChars()/ValidInfixOprtChars()` (lines 495-516) which gate `CheckName`/`CheckOprt`.
> muparser/src/muParserBase.cpp:362-365 `SetVarFactory(...)`.
> OSS-Fuzz c_api_fuzzer.cc:63-87 (via mupDefineInfixOprt/mupDefinePostfixOprt/mupDefineOprt/mupSetVarFactory/mupAddValIdent/mupDefineNameChars/mupDefineOprtChars, all gated by config bits) and :101-104 (`config2 & 0x01` selects `mupEvalBulk` vs `mupEval`+`mupEvalMulti`) is existing project-adjacent evidence of exactly these controls being fuzzed.

### P2.11 Input Expectations
**Claim:** The formula string given to `SetExpr` (and thus tokenized by `Eval`'s first call) must be: non-empty after trimming (else `ecUNEXPECTED_EOF`), shorter than `MaxLenExpression` (20000 chars) (else `ecEXPRESSION_TOO_LONG`), composed of identifier/operator characters drawn from the currently configured charsets (`ValidNameChars`/`ValidOprtChars`/`ValidInfixOprtChars`, default alnum+`_` for names and a fixed operator-symbol set), and must not use an argument separator character equal to the active decimal point. `SetExpr` appends a trailing space to the stored formula (helps lookahead in the tokenizer). Identifiers registered with `DefineFun`/`DefineVar`/`DefineOprt`/etc. are limited to `MaxLenIdentifier` (100) characters.
**Evidence:**
> muparser/src/muParserBase.cpp:1289-1292 `CreateRPN()`: `if (!m_pTokenReader->GetExpr().length()) Error(ecUNEXPECTED_EOF, 0);`
> muparser/src/muParserBase.cpp:441-453 `SetExpr`: length check `if (a_sExpr.length() >= MaxLenExpression) Error(ecEXPRESSION_TOO_LONG,...)`; `m_pTokenReader->SetFormula(a_sExpr + _T(" "));`
> muparser/include/muParserDef.h:511-512: `static const int MaxLenExpression = 20000; static const int MaxLenIdentifier = 100;`
> muparser/src/muParserBase.cpp:424-432 `CheckName(...)`: rejects empty names, names with chars outside `a_szCharSet`, or names starting with a digit → `Error(ecINVALID_NAME)`.
> muparser/src/muParserBase.cpp:582-583,628-629,649-650: `DefineOprt/DefineVar/DefineConst` all check `a_sName.length() > MaxLenIdentifier → Error(ecIDENTIFIER_TOO_LONG)`.

## Other entries of this Logic Group (API surface / call protocol)

### mu::Parser::SetExpr (ParserBase::SetExpr)
**Signature:** `void SetExpr(const string_type& a_sExpr);` — muparser/include/muParserBase.h:124; defined muparser/src/muParserBase.cpp:441-453.
**Needs before it:** A constructed `Parser` object (so `m_pTokenReader`/charsets exist — `InitTokenReader()` is run in the `ParserBase` ctor, muparser/src/muParserBase.cpp:109). No prior `Eval()` is required; `SetExpr` can be called repeatedly to change expressions.
**Parameters/flags selecting paths:** the string itself (length must be `< MaxLenExpression=20000`, else throws `ecEXPRESSION_TOO_LONG` — muparser/src/muParserBase.cpp:448-449); current `SetArgSep`/`SetDecSep` state affects an internal locale check (`ecLOCALE` if arg separator == decimal point, line 444-445).
**Return value:** none (void); throws `mu::ParserError` on invalid input (too long, locale clash).
**What to free:** nothing caller-owned; internally resets `m_vStringBuf`/`m_vRPN` via `ReInit()` (muparser/src/muParserBase.cpp:452, 256-262).
**Combination with target:** Must be called at least once before `Eval()` can succeed (CreateRPN requires non-empty expr, muparser/src/muParserBase.cpp:1291-1292). Each call to `SetExpr` forces the *next* `Eval()` call to re-tokenize from scratch (string-parse mode) rather than reuse bytecode.

### mu::ParserBase::GetUsedVar
**Signature:** `const varmap_type& GetUsedVar() const;` — muparser/include/muParserBase.h:188; defined muparser/src/muParserBase.cpp:722-744.
**Needs before it:** `SetExpr()` must have been called (same precondition as `Eval`, since it also runs `CreateRPN()`). Any `DefineVar`/`DefineFun`/etc. desired for proper name resolution should be done first, though `GetUsedVar` tolerates undefined variables.
**Behaviour:** Temporarily sets `m_pTokenReader->IgnoreUndefVar(true)`, calls `CreateRPN()` (same tokenizer/RPN-builder core as `Eval`), forces `m_pParseFormula` back to `&ParserBase::ParseString` afterward (so the *next* `Eval()` will redo full parsing — the RPN built here is discarded for `Eval` purposes), restores `IgnoreUndefVar(false)`.
**Return value:** `const varmap_type&` — a map of variable name → pointer that were referenced in the parsed expression; on exception it rethrows after restoring parse-mode (no partial map left in a bad state beyond what was already collected by the tokenizer).
**What to free:** nothing; returns a const reference to internal state (`m_UsedVar`), do not store beyond the parser's lifetime / beyond the next `CreateRPN`.
**Combination with target:** Typically called once after `SetExpr` and before/instead of trusting `Eval()` to validate variable names, specifically to discover which identifiers in the formula are variables (useful to then call `DefineVar` for each before calling `Eval()` for real). Evidence: muparser/src/muParserBase.cpp:722-744 (full body) — comment at 1836-1840 explicitly says `GetUsedVar` is non-const because it "explicitly calls Eval()-equivalent forcing this update" (i.e., runs `CreateRPN`).

### mu::ParserBase::DefineVar
**Signature:** `void DefineVar(const string_type& a_sName, value_type* a_pVar);` — muparser/include/muParserBase.h:175; defined muparser/src/muParserBase.cpp:623-638.
**Needs before it:** a valid non-null `value_type*` pointing to caller-owned storage that remains valid for the lifetime of all subsequent `Eval()` calls that reference this variable (the parser stores the raw pointer, not a copy — line 636 `m_VarDef[a_sName] = a_pVar;`).
**Parameters/flags:** `a_sName` checked against `ValidNameChars()` and `MaxLenIdentifier` (100); throws `ecINVALID_VAR_PTR` if `a_pVar==0` (line 625-626), `ecIDENTIFIER_TOO_LONG` if name too long (628-629), `ecNAME_CONFLICT` if name already a constant (632-633).
**Return value:** void; throws `ParserError` on bad name/pointer.
**What to free:** the pointed-to buffer is NOT freed/owned by the parser; caller must keep it alive and free it only after parser no longer needs it (see `CalcBulk` example which `delete[]`s the var arrays after `Eval` calls finish — muparser/samples/example1/example1.cpp:434-469).
**Combination with target:** Calling this always triggers `ReInit()` (line 637) putting the parser back in string-parse mode — so it must be called *before* the `Eval()` call that is meant to use the cached bytecode, or it invalidates existing bytecode and forces a reparse on the next `Eval()`.

### mu::ParserBase::DefineFun (template, calls AddCallback)
**Signature:** `template<typename T> void DefineFun(const string_type& a_strName, T a_pFun, bool a_bAllowOpt = true);` — muparser/include/muParserBase.h:143-147; also `DefineFunUserData` variant at 156-160. Implementation goes through `AddCallback(...)` — muparser/src/muParserBase.cpp:369-396.
**Needs before it:** none beyond a constructed `Parser`. `a_pFun` must be a valid callable matching one of the parser's internal `fun_type0..fun_typeN`/`multfun_type` signatures (enforced by overload resolution of `ParserCallback`'s constructor); `AddCallback` further validates with `a_Callback.IsValid()` (line 375) else throws `ecINVALID_FUN_PTR`.
**Parameters/flags:** name-conflict checks across `m_FunDef`, `m_PostOprtDef`, `m_InfixOprtDef`, `m_OprtDef` maps (lines 381-391) → `ecNAME_CONFLICT`; `CheckOprt`/`CheckName` validate characters (line 393, 403-418) → `ecINVALID_NAME`/`ecINVALID_POSTFIX_IDENT`/`ecINVALID_INFIX_IDENT`; `a_bAllowOpt` flag controls whether the optimizer (if enabled via `EnableOptimizer`) may constant-fold calls to this function.
**Return value:** void; throws on conflict/invalid pointer/invalid name.
**What to free:** nothing; the callback is stored by value (`ParserCallback`) in internal maps.
**Combination with target:** Always calls `ReInit()` (line 395) — same effect as `DefineVar`: must happen before the `Eval()` call meant to exercise this function, and invalidates any previously-built bytecode.
**Related sibling entries in this group (same `AddCallback` core, same protocol):** `DefineOprt` (muparser/include/muParserBase.h:172, impl :580-595 — also checks against built-in operator names when `EnableBuiltInOprt`/`m_bBuiltInOp` is true, else `ecBUILTIN_OVERLOAD`), `DefineInfixOprt` (:177, impl :555), `DefinePostfixOprt` (:176, impl :525). All share: name-length/char checks, conflict checks, `ReInit()` on success.

### mu::ParserBase::DefineOprt
**Signature:** `void DefineOprt(const string_type& a_strName, fun_type2 a_pFun, unsigned a_iPri = 0, EOprtAssociativity a_eAssociativity = oaLEFT, bool a_bAllowOpt = false);` — muparser/include/muParserBase.h:172; defined muparser/src/muParserBase.cpp:580-595.
**Needs before it:** constructed `Parser`; `a_pFun` a valid 2-arg callback.
**Parameters/flags:** `a_iPri` sets operator precedence consumed later by `GetOprtPrecedence` (muparser/src/muParserBase.cpp:661-691, case `cmOPRT_BIN: return a_Tok.GetPri();`); `a_eAssociativity` consumed by `GetOprtAssociativity` (:697-718, case `cmOPRT_BIN: return a_Tok.GetAssociativity();`) — both directly drive `CreateRPN`'s shunting-yard decisions for this operator token. If `EnableBuiltInOprt(true)` (default) and name collides with a built-in symbol (`c_DefaultOprt[]`), throws `ecBUILTIN_OVERLOAD` (lines 586-592).
**Return value:** void; throws on identifier-too-long/conflict/builtin-overload/invalid-chars.
**What to free:** nothing.
**Combination with target:** same as DefineFun — calls `AddCallback`→`ReInit()`; must precede the `Eval()` call meant to use the new operator; the tokenizer (`ParserTokenReader::IsOprt`) only recognises names present in `m_OprtDef` at the time it next tokenizes (i.e. after the forced `ReInit`).

### mu::ParserBase::DefineConst
**Signature:** `void DefineConst(const string_type& a_sName, value_type a_fVal);` — muparser/include/muParserBase.h:173; defined muparser/src/muParserBase.cpp:647-655.
**Needs before it:** constructed `Parser`. Checks name length (`ecIDENTIFIER_TOO_LONG`) and valid chars (`CheckName` → `ecINVALID_NAME`); no uniqueness check against existing vars is shown here directly (compare with `DefineVar` which checks against `m_ConstDef`) — conflicts are instead surfaced when a variable of the same name is later defined.
**Return value:** void; throws on bad name.
**What to free:** nothing (stores `value_type` by value in `m_ConstDef`).
**Combination with target:** calls `ReInit()` (line 654) — same "must precede Eval that uses it, invalidates existing bytecode" rule as DefineVar/DefineFun/DefineOprt.

## How the entries combine in one harness
1. Construct `mu::Parser p;` (runs built-in Init*).
2. Optionally call configuration entries in any order — `DefineVar`, `DefineConst`, `DefineFun`, `DefineOprt`, `DefineInfixOprt`, `DefinePostfixOprt`, `SetDecSep`/`SetThousandsSep`/`SetArgSep`, `DefineNameChars`/`DefineOprtChars`/`DefineInfixOprtChars`, `SetVarFactory`, `EnableOptimizer`/`EnableBuiltInOprt` — each of the Define* calls internally forces `ReInit()`, so their relative order with respect to each other does not matter, but **all must happen before the `Eval()` call they are meant to affect**, since any one of them after an `Eval()` invalidates the cached bytecode and forces a reparse on the next `Eval()`.
3. Call `p.SetExpr(fuzzer_controlled_string)` — this is the main untrusted-data injection point together with the Define* names if also taken from fuzz data.
4. Optionally call `p.GetUsedVar()` to exercise the tolerant-parsing path (discards its own RPN for `Eval` purposes but shares the tokenizer/CreateRPN core with `Eval`).
5. Call `p.Eval()` / `p.Eval(nStackSize)` / `p.Eval(results, nBulkSize)` (choose one, or several in sequence — all legal), wrapped in `try { ... } catch (mu::Parser::exception_type& e) { ... }` since malformed expressions are expected to throw, not crash.
6. Read back results (scalar value, `v[i]` for `0<=i<nStackSize`/`GetNumResults()`, or `results[i]` for bulk) — no project API further consumes these; they are terminal.
7. Destroy `p` (or let it go out of scope); free any caller-owned `DefineVar` buffers / bulk `results` buffer only after all `Eval` calls referencing them are done.

Evidence supporting this combination: muparser/samples/example1/example1.cpp:399-413 (loop of DefineStrConst/SetExpr/Eval/ClearConst), :431-469 (`CalcBulk`: DefineVar then Eval bulk then free), :556-595 (SetExpr, Eval, GetNumResults, Eval(int&), try/catch(exception_type)); OSS-Fuzz muparser-c1/c_api_fuzzer.cc:45-115 (constructs parser via C API, conditionally calls Define* variants gated by fuzz-data config bits, then `SetExpr`, then chooses `mupEvalBulk` vs `mupEval`+`mupEvalMulti` based on another fuzz-data bit, all inside the DLL wrapper that forwards 1:1 to the C++ `Parser::Eval` family — muparser/src/muParserDLL.cpp:224,237,247).

## Build notes
**Language:** C++ (library built as C++14/17 internally, fuzzer compiled as C++11 per build flags below). The two existing OSS-Fuzz harnesses are `.cc` files compiled with `$CXX`.
**Evidence:**
> ossfuzz_view/muparser-c1/build.sh:22 `cmake . -DBUILD_SHARED_LIBS=OFF -DENABLE_OPENMP=OFF` then `make -j$(nproc)` and `make install; ldconfig` (builds and installs libmuparser + headers via its own CMake/pkg-config files).
> ossfuzz_view/muparser-c1/build.sh:30-31 `MU_CXXFLAGS=$(pkg-config muparser --cflags)`; `MU_LIBS=$(pkg-config muparser --libs)` — the target's header (`muParser.h`/`muParserBase.h`, or `muParserDLL.h` for the C API) is located via the installed pkg-config include path, not a relative `-I` to the source tree.
> ossfuzz_view/muparser-c1/build.sh:33-36 `$CXX -std=c++11 $CXXFLAGS -I. $MU_CXXFLAGS $MU_LIBS $SRC/set_eval_fuzzer.cc -o $OUT/set_eval_fuzzer $LIB_FUZZING_ENGINE libmuparser.a` — static link against `libmuparser.a`.
> ossfuzz_view/muparser-c1/build.sh:38-41: identical recipe for `c_api_fuzzer.cc`.
> ossfuzz_view/muparser-c1/Dockerfile:17-22: base image `gcr.io/oss-fuzz-base/base-builder`, clones `https://github.com/beltoforion/muparser.git`, copies `build.sh`/`run_tests.sh`/`*_fuzzer.cc` into `$SRC`.
> ossfuzz_view/muparser-c1/c_api_fuzzer.cc:22 `#include "muParserDLL.h"` — shows the C-API harness includes the DLL/C wrapper header, found via the pkg-config include path set up by `make install`; a C++-API harness (to directly exercise `mu::Parser::Eval`) would instead `#include "muParser.h"` and link the same `libmuparser.a`.
**Note:** `set_eval_fuzzer.cc` in the current tree is a stub (empty `LLVMFuzzerTestOneInput`, comment "Gold harness replaced by an empty stub for baseline generation" — ossfuzz_view/muparser-c1/set_eval_fuzzer.cc:1,9-13), so it is not usable as calling-convention evidence; `c_api_fuzzer.cc` is the authoritative, non-stub harness showing real API usage for this project under OSS-Fuzz.