Build the Logic Group of project `{{ project }}` (source at
`{{ project_path }}`) for this target function:

**Target function:** `{{ target_function }}`
{% if target_declaration %}
**Declaration:** `{{ target_declaration }}`
{% endif %}
{% if target_location %}
**Declared at:** `{{ target_location }}`
{% endif %}
{% if fuzzer_name %}
**Fuzzer name:** `{{ fuzzer_name }}`
{% endif %}
{% if oss_fuzz_project_dir %}
**OSS-Fuzz build directory:** `{{ oss_fuzz_project_dir }}`
{% endif %}

Follow the Workflow in your instructions and end with
`submit_logic_group(...)`, the target function as the first entry.
