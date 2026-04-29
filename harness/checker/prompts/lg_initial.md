Identify a Logic Group for project `{{ project }}` at `{{ project_path }}`
matching the following user description:

{{ functionality_prompt }}

Follow the **core-first workflow** from your system prompt:

1. `list_existing_fuzzers` and `list_directory("{{ project_path }}")`
   for context.
2. Pick the **core C** (3-5 internal functions whose bugs you want
   to find) — this is the heart of the LG.
3. Call `public_apis_reaching_batch(targets=C)` to derive E_pub via
   reverse caller search.
4. Pick the best public-API entry from E_pub (LLM-defer for
   ambiguous unknowns if needed).
5. End with `submit_logic_group(...)`.

Do NOT browse public headers and pick entries first; that is the
old forward-search pattern and the wrong starting direction.
