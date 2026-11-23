# START HERE: first prompt for Claude Code

Run `claude` in the root of this folder, then paste everything inside the box below as your first message.

```
Read CLAUDE.md, docs/SPEC.md and docs/PLAN.md fully, then skim the three PDFs in docs/reference/ (the revised proposal, the final presentation, and the project policy). 

We present tomorrow (Nov 24), so we are building the "Presentation slice" in docs/PLAN.md: phases 0 to 6, in order, one checkpoint at a time.

Start now with Phase 0 and Phase 1. When Phase 1 is done, run the tests, commit, show me the test output and a 3-line summary, list any assumptions you made, and wait for me to say "go" before Phase 2.

Rules to keep in mind: every page access goes through the buffer pool; correctness against the SQLite oracle comes before performance; ask before deviating from docs/SPEC.md and record decisions in docs/DECISIONS.md; log what you build in docs/AI_USAGE.md; do not write any report or proposal text.
```

Tips for the session:
- Say "continue through phase 4" if you want it to run several phases without stopping.
- If a test fails and it is stuck, ask it to explain the failure before it patches anything. The buffer pool, external sort and Grace hash join are where silent bugs hide.
- Before the presentation, make sure you can explain the buffer pool, one join, and the oracle check in your own words. The instructor asks questions.
