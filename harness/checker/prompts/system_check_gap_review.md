NOT SUBMITTED YET: gap review. Your build passes the submission checks and you have
{{ builds_left }} build{{ 's' if builds_left != 1 else '' }} left. Before submitting,
compare the harness with the P2 report and write the answers out:

1. **Result usage (P2.9).** List every API in P2.9 that real callers apply to the
   target's results. For each: does your harness call it on the results? If not, why
   not?
2. **Behaviour controls (P2.10).** List every control in P2.10. For each: does the fuzz
   input choose it, or is it fixed in your harness?
3. **Input expectations (P2.11).** Do most fuzz inputs get past the target's early
   checks and reach the deep processing, or are they rejected early?
4. **Real callers.** What do the target's real callers do with its results that your
   harness does not?

Prefer result usage whose work depends on the content of the input (writing the result
out, transforming it, walking it) over calls that do the same thing for every input.

If you find a gap, improve the harness, build it, run the checks again and compare
`get_coverage` with your current build; submit the better one (resubmitting an earlier
version needs a rebuild of it). If you find no gap, call `submit_harness` again with the
same code and it is accepted.
