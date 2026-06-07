# Claude Project Instructions

This file is intentionally kept for Claude or other assistants that look for a
`CLAUDE.md` project context file.

Before making changes, read:

```text
AGENTS.md
docs/codex-project-goal.md
docs/bringup-record-2026-06-07.md
```

Short version:

- The user wants stable `1024x960` level, `30 fps`, four-target displacement
  output on ESP32-P4-NANO + OV5647.
- The main output is numeric displacement data.
- JPEG/full-frame image saving is not the main goal.
- Optional ROI raw grayscale output is useful only after stable displacement
  output is proven.
- The current validated version reaches about `34.3 fps` at `800x640 RAW8`.
- The final higher-resolution goal is not proven yet.
- Work only inside this repository unless the user explicitly says otherwise.

