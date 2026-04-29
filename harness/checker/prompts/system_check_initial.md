Generate a LibFuzzer harness for the project at `{{ project_path }}`.

{% if target_function %}
Target entry function: **`{{ target_function }}`** (pre-selected).
{% endif %}
{% if functionality_prompt %}
{% if target_function %}Additional context on what to exercise:{% else %}Targets the following functionality:{% endif %}

{{ functionality_prompt }}
{% endif %}

Project: `{{ project }}`
Target binary name convention: `{{ default_fuzzer_name }}`

{% if oss_fuzz_project_dir %}
The OSS-Fuzz build configuration for this project is at
`{{ oss_fuzz_project_dir }}`. You can `read_file` the `build.sh`,
`Dockerfile`, and any existing fuzzers there to understand how harnesses
are compiled for this project (includes, link flags, source conventions).
Do this early — it will save you time in Phase 5 (build).
{% endif %}

{% if target_function %}
Start with Phase 1 (Verify entry function). The entry is already
chosen — call `lookup_symbol`, `reachable_from`, `is_public_api`, and
`get_callers` on `{{ target_function }}` to verify it and gather
context, then proceed to Phase 2 (read real callers).
{% else %}
Start with Phase 1 (Locate entry function). Parse the functionality
prompt above into concrete function-name candidates, then use
`search_files` and `static_analysis` tools to narrow down to ONE entry
function before writing any harness code.
{% endif %}

{% if validation_feedback %}
Previous validation feedback you must address:

{{ validation_feedback }}
{% endif %}
