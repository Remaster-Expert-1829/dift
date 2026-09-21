# Documentation Governance

> Status: Approved

## Authority and hierarchy

When artifacts conflict, stop implementation and resolve the conflict. Do not choose whichever artifact is easiest to code.

1. Approved project charter and requirements define intended outcomes.
2. Accepted ADRs define architectural choices and trade-offs.
3. Approved architecture, data, and external contract documents define system behavior and boundaries.
4. Security, reliability, operational, testing, and engineering standards constrain implementation.
5. Milestones order delivery but do not override the above.
6. Code and tests implement the documentation; existing code is evidence, not automatic authority.
7. The original HTML roadmap is non-normative source material.

If two documents at the same level conflict, both are considered suspect until the Architect records a resolution.

## Ownership

| Role | Responsibility |
|---|---|
| User/Product owner | Approves scope, user-visible behavior, priorities, and material trade-offs. |
| Architect | Owns architecture coherence, ADR status, contract changes, and implementation authorization. |
| Builder | Implements approved scope, updates affected documentation, tests behavior, and reports deviations. |
| Reviewer | Independently checks repository state against requirements and returns PASS or FAIL with evidence. |

One person or agent may fill multiple roles over time, but the Reviewer pass must be a distinct review step after Builder work.

## Change process

1. Identify the affected requirement IDs, contracts, risks, and tests.
2. Classify the proposal as product, architecture, implementation, configuration, experiment, or operations.
3. For a significant architecture change, create or supersede an ADR before implementation.
4. Update normative documents and traceability in the same change as implementation, or before it.
5. Run the consistency review.
6. Obtain the required approval and record the date/status.
7. Implement and independently verify.

Emergency fixes MAY precede prose cleanup only when they do not alter a contract or architectural decision; documentation synchronization remains required before release.

## Significant architectural change test

An ADR is required when a change affects any of the following:

- peer trust or cryptographic boundary;
- persistent data compatibility or recovery semantics;
- wire compatibility;
- logical chunk identity;
- external dependency with material operational or licensing impact;
- process/service boundary;
- concurrency/connection model;
- supported platform boundary;
- data exposure or deletion behavior.

File naming, private class layout, local algorithms behind an unchanged contract, and test helper selection normally do not require ADRs.

## Terminology and IDs

- Use terminology from the domain glossary.
- IDs are stable and MUST NOT be reused after removal.
- Removed items remain recorded as deprecated or superseded when references may exist.
- Requirements state outcomes; architecture documents state responsibility; contracts state exact interactions; tests state evidence.
- Do not restate complete requirements in several documents. Cross-reference their IDs.

## Approval records

Every normative document carries a status. Accepted ADRs include their acceptance date. Git history provides authorship and change history; the document records status and rationale.

## Code/document disagreement

If implementation reveals that approved documentation is unsafe, impossible, or materially wrong:

1. Pause the affected work.
2. Record evidence and impacted IDs.
3. Propose a documentation/ADR change.
4. Obtain approval.
5. Update documentation and code together.

The Builder MUST NOT silently redefine architecture through implementation.
