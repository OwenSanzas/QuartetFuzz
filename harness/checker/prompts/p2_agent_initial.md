## Task

Investigate the API protocol for the following entry functions from
project **{{ project }}** (source at `{{ project_path }}`).

**Entry functions to investigate:**
{% for entry in entries %}
- `{{ entry }}`
{% endfor %}

**Logic Group context:**
{{ lg_description }}

{% if oss_fuzz_project_dir %}
**OSS-Fuzz project directory:** `{{ oss_fuzz_project_dir }}`
{% endif %}

## Instructions

1. For each entry function above, investigate P2.1 through P2.8.
2. Use these tools to gather evidence:
   - `lookup_symbol(name="function_name")` — find definition
   - `get_callers(name="function_name")` — find callers
   - `get_callees(name="function_name")` — find callees
   - `is_public_api(name="function_name")` — check visibility
   - `read_file(path="...")` — read source files
   - `search_files(pattern="...")` — search for patterns
3. Cite every claim with file:line references from the actual source.
4. When your investigation is complete, call `submit_p2_report(report="...")`
   with the markdown report.  **Submit early** rather than risk running
   out of turns.

Start by looking up each entry function with `lookup_symbol` to
confirm it exists, then read its header declaration.
