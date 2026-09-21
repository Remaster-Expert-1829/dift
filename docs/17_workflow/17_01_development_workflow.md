# Development Workflow

> Status: Approved

## Work item readiness

A work item is ready when it identifies:

- requirement/test IDs and milestone;
- affected architecture/data/contracts;
- product versus architecture versus implementation/config/experiment classification;
- acceptance evidence;
- security/recovery implications;
- unresolved decision, if any.

Implementation does not start on a feature whose required product/architecture decision is unresolved.

## Branches and commits

- Short-lived feature branches are recommended; direct main-branch work is allowed only for the repository owner when the same checks/review discipline is preserved.
- Keep commits coherent and buildable where practical.
- Use meaningful conventional commit subjects.
- Do not mix unrelated reformatting or generated benchmark output with behavior changes.
- Never commit private keys, real pairing state, personal files, build trees, raw captures with sensitive data, or local configuration.

## Change sequence

1. Read linked source-of-truth documents and inspect actual repository state.
2. If architecture must change, propose/approve ADR and documentation first.
3. Add/adjust tests that demonstrate acceptance or reproduce a defect.
4. Implement the smallest complete vertical behavior.
5. Run targeted tests, then required broader gates.
6. Update docs, traceability, migrations, and examples.
7. Builder reports actual files/behavior/tests and known limits.
8. Reviewer independently inspects diff/repository and returns PASS or FAIL with evidence.
9. Architect closes the change or resolves a documented conflict.

## Pull-request/review content

- Objective and non-goals.
- Linked IDs.
- User-visible and architectural effects.
- Persistence/protocol compatibility and migration.
- Security/failure cases.
- Commands/tests executed and results.
- Benchmark evidence for performance claims.
- Documentation changed.
- Remaining risks/TBDs.

Reviewers inspect code and generated behavior, not only the description. A review that cannot cite evidence is incomplete.

## Issues and decisions

- Bugs cite stable error/test/requirement IDs where relevant.
- Experimental tuning tasks cite `EXP-*` and store raw results.
- Configuration-default changes cite `CFG-*` and do not masquerade as architecture.
- Significant rejected alternatives live in ADRs, not scattered issue comments.

## Release workflow

Milestone completion is not automatically a public release. Release follows `15_01_ci_release_and_rollback.md`, updates readiness/consistency reviews, and requires independent PASS.

## Documentation synchronization

A change is incomplete when code changes:

- command/output/protocol behavior without contract updates;
- persistent schema without migration/data docs;
- failure/retry behavior without reliability docs/tests;
- metrics/events without observability schema;
- architecture without ADR/diagrams/traceability;
- benchmark method or claim without raw evidence/methodology update.
