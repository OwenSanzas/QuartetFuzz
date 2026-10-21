## Task

Investigate the target function of project **{{ project }}** and the API surface
around it, and write the P2 report.

**Target function:** `{{ entries[0] }}`
{% if target_declaration %}
**Declaration:** `{{ target_declaration }}`
{% endif %}
{% if target_location %}
**Declared at:** `{{ target_location }}`
{% endif %}
{% if lg_description %}

**Functionality it belongs to:**
{{ lg_description }}
{% endif %}
{% if lg_entries %}

**Logic Group of this feature** (from the earlier Logic Group stage):
- Entries (public API of the feature; the first is the target): {{ lg_entries | join(", ") }}
{% if lg_core %}
- Core (internal functions implementing it): {{ lg_core | join(", ") }}
{% endif %}

The harness will call the other entries too.  Besides the target, give the
call protocol of each other entry (what it needs before it, its parameters
and the values or flags that select different paths, its return value, what
to free), and how it combines with the target in one harness.  If an entry
does not exist or does not belong to this feature, say so in the report.

Check the Logic Group description's notes on input format, init rejections,
structured input and test hooks against the source and the OSS-Fuzz build
directory, and give the details a harness needs: the seed corpus / dictionary
for this fuzzer and the format they imply; each init check that rejects
parameters and the exact call that relaxes it; where valid example inputs are
(files, tables in tests); how to enable each test hook (header, symbol,
declaration).  Also name any from-memory deserializer of the object the target
takes.  Correct the notes where the source says otherwise.
{% endif %}

**Project source:** `{{ project_path }}`
{% if oss_fuzz_project_dir %}
**OSS-Fuzz build directory:** `{{ oss_fuzz_project_dir }}`
{% endif %}

Tools:
{% if static_available %}
- `get_callers`, `get_callees`, `find_path` — call graph queries (who calls a function, what it
  calls, the shortest call chain from one function to another)
{% endif %}
- `Grep(pattern="...")` — which files mention a pattern; `output_mode="content"` for the lines
- `Glob(pattern="...")` — find files by name
- `Read(file_path="...", offset=..., limit=...)` — read the lines you need

Follow the Steps in your instructions, answer P2.1-P2.11 and the build notes with
evidence, then submit the report.
