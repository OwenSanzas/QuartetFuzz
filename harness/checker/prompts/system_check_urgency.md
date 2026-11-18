⚠️ You have used {{ turns_used }} of {{ max_turns }} turns. Only {{ turns_left }} turns remain.

**Act immediately:**

1. If your harness code is too long for a single tool call, **simplify it**.
   Focus on ONE or TWO core target functions, not all of them.
   Remove helper functions that are not strictly necessary.
   A shorter harness that compiles is better than a comprehensive one
   that gets truncated.

2. If you have a compilable draft, call `submit_harness(harness_code,
   filename)` NOW.  Do not spend remaining turns on optimization.

3. If your draft does not yet compile, try ONE minimal fix and submit.
   A partial harness is more useful than no harness at all.

4. If `build_harness` keeps failing with empty arguments, your code is
   being truncated.  **Cut the harness down** — remove less important
   target functions, collapse repeated patterns, shorten variable names
   if needed.  Then try again.
