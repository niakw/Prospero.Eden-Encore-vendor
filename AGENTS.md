# Eden Encore — contributor and agent conventions

This file applies to work authored in this repository. Its language rules complement the existing `CONTRIBUTING.md`, source-owner rules and project-specific verification gates.

## English by default for persistent project records

**Write new and materially revised project records in English (en-US), unless the repository explicitly requires another language for a user-facing translation.**

This includes:
- Technical documentation, architecture/design notes, research findings and compatibility audits.
- Markdown registers, project memory, handoff summaries, investigation notes, decision logs and release notes.
- GitHub issue titles, descriptions, triage updates, comments and acceptance checklists.
- GitHub pull request titles, descriptions, review notes, commit messages and CI/build summaries.
- Authored diagnostic documentation, change logs, troubleshooting guides and security notes.

**Use English for newly written prose, not for every byte of an existing file.** Preserve existing identifiers, paths, logs, command output, API names, cited quotations, game titles, license notices and upstream attribution verbatim. Do not mechanically translate historical documents or bulk-edit old issues merely to satisfy the policy. When a historical document is substantially revised, favor an English summary or an English replacement while retaining provenance.

## Exceptions and communication

- **Internationalized user-facing UI remains multilingual**. Maintain existing localization keys, translations and the project's 31 supported languages; documentation language must not override console locale behavior.
- Preserve original legal and third-party source text; do not change imported snapshots solely to enforce English.
- User-facing conversation may remain in the user's chosen language (including French). This does not change the language of persistent GitHub records.
- If a contributor specifically requests a translated document, provide that translation without treating it as a change in the repository's default policy.

## Engineering traceability

For investigations and fixes, record **symptom → game/Title ID → source logs → suspected root cause → upstream issue/PR/commit → active engine/vendor code path → fix/commit → host tests → native CI/build → PS5 hardware outcome → status**. Distinguish source presence, host test success, native PS5 build, installation, guest launch and gameplay validation.

Search relevant upstream open/closed issues, PRs, commits and release notes before proposing a local fix. Compare both the engine and pinned vendor repository; do not port unrelated source trees wholesale or claim a console bug is fixed by a green CI job.

### Repository boundary

- Engine: [niakw/ProsperoEden-Encore](https://github.com/niakw/ProsperoEden-Encore)
- Vendor: [niakw/Prospero.Eden-Encore-vendor](https://github.com/niakw/Prospero.Eden-Encore-vendor)

When a change spans repositories, use exact paired commit SHAs and cite the corresponding issue/PR. Protect any in-progress native build; documentation-only policy changes do not authorize repinning, rebuilding, or modifying production sources.
