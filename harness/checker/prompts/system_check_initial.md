Write a libFuzzer harness for project `{{ project }}` (source at `{{ project_path }}`).

{% if target_function %}
**Target function:** `{{ target_function }}`
{% endif %}
{% if functionality_prompt %}

**Benchmark task:**

{{ functionality_prompt }}
{% endif %}

The fuzzer binary is named `{{ default_fuzzer_name }}`.
{% if oss_fuzz_project_dir %}
The OSS-Fuzz build directory is `{{ oss_fuzz_project_dir }}`.
{% endif %}

Follow the Steps in your instructions, starting with reading the build.
{% if validation_feedback %}

Previous validation feedback you must address:

{{ validation_feedback }}
{% endif %}
