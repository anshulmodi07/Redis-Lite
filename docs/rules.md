# Redis-Lite Project Rules

## 1. Source of Truth

- `guide.md` is the master plan and default version roadmap.
- Read the relevant guide section and inspect the current code before changing anything.
- Think through correctness, trade-offs, compatibility, and the effect on later versions before implementation.
- A better technique may replace the guide's approach only when its benefit is clear. Update `guide.md` in the same version so the plan and implementation never disagree.
- Do not silently skip, merge, reorder, or expand versions.

## 2. Version Workflow

For each version:

1. Review its goal, concepts, dependencies, files, tests, and common mistakes.
2. Inspect the existing implementation and preserve working behavior unless the version intentionally changes it.
3. Plan the smallest coherent change for that version.
4. Implement with a clean, modular file structure.
5. Add or update focused tests and run them.
6. Update `structure.md` whenever the folder structure or file responsibilities change.
7. Create `Redis_vX.md` describing the version before considering it complete.
8. Commit the completed, verified version before starting the next one.

Only one version should be in progress at a time unless an unavoidable dependency is documented.

## 3. Version Documentation

Every completed version must have a `Redis_vX.md` file containing:

- Goal and motivation
- Previous limitation being fixed
- Concepts taught
- Design decisions and trade-offs
- Files added or changed
- Behavior and commands added
- Testing steps and results
- Known limitations
- What the next version builds upon

Keep documentation accurate to the code, concise, and useful for revision or interviews.

## 4. Git Rules

- Make a commit at every completed version boundary.
- Do not commit incomplete or unverified version work as a completed version.
- Use short, natural, human-style messages, such as `finish resp decoder` or `add ttl expiry`.
- Keep unrelated changes out of a version commit.
- Never rewrite or discard existing user work without explicit approval.

## 5. Project Structure

- Maintain `structure.md` as the current map of folders, files, and their responsibilities.
- Prefer small modules with clear ownership over a growing monolithic `server.cpp`.
- Keep protocol, networking, storage, commands, persistence, and tests separated as the roadmap evolves.
- Avoid premature abstractions that belong to a later version.

## 6. Code Quality

- Prefer correctness and clarity before optimization.
- Validate command arity, input bounds, partial I/O, error paths, and resource cleanup.
- Preserve binary safety once RESP is introduced.
- Use RAII and type-safe C++ designs where they improve safety; document deliberate deviations from Redis's C implementation.
- Add comments for non-obvious invariants, protocol rules, ownership, algorithms, and OS-level behavior.
- Do not add comments that merely restate straightforward code.
- Remove dead code and avoid unexplained magic values.

## 7. Testing and Completion Gate

A version is complete only when:

- It meets the guide's stated goal.
- Existing behavior still works or an intentional change is documented.
- Relevant normal, edge, malformed-input, and regression cases pass.
- Resource and error paths have been considered.
- `guide.md`, `structure.md`, and `Redis_vX.md` match the implementation.
- The version has a focused git commit.

If a required tool is unavailable, document the unverified checks rather than claiming they passed.

## 8. Scope and Communication

- Follow the user's requested scope for each work session; do not begin later versions early.
- State any important assumption or guide deviation explicitly.
- Report discovered defects separately from changes made.
- Ask before making a change that materially alters the roadmap, compatibility target, platform support, or learning objective.

