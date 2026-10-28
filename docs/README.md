# Spoink Documentation

Spoink is a direct, local-network file-transfer system for sending multiple files between paired devices. It uses content-defined chunking to avoid retransmitting data already held by the receiver, secure transport, crash-safe resume, bounded parallelism, optional compression, and measurable telemetry.

This corpus is the source of truth for implementation after approval. Code may refine implementation details but may not silently alter requirements, accepted architectural decisions, external contracts, data invariants, or failure semantics.

## Status

**Documentation status: READY WITH CONDITIONS.**

Implementation may begin on the foundation and vertical-slice milestones. The conditions listed in [25_implementation_readiness_review.md](25_implementation_readiness_review.md) must be resolved before the affected feature or protocol is frozen.

## Reading order

1. [Project charter](00_governance/00_01_project_charter.md)
2. [Documentation governance](00_governance/00_02_documentation_governance.md)
3. [Requirements](01_requirements/01_01_requirements.md) and [user flows](01_requirements/01_02_use_cases_and_user_flows.md)
4. [Domain model](02_domain/02_01_domain_model_and_glossary.md)
5. [Architecture decisions](03_decisions/README.md)
6. [System architecture](04_architecture/04_01_system_overview.md)
7. [Data architecture](05_data/05_01_manifest_and_chunk_store.md)
8. [Wire, CLI, and internal contracts](06_contracts/06_01_wire_protocol.md)
9. Security, reliability, observability, quality, and operations
10. [Traceability matrix](00_governance/00_03_traceability_matrix.md), [consistency review](24_consistency_review.md), and [readiness review](25_implementation_readiness_review.md)

## Corpus map

| Area | Documents |
|---|---|
| Planning | [Approved documentation plan](00_documentation_plan.md) |
| Governance | [Charter](00_governance/00_01_project_charter.md), [documentation governance](00_governance/00_02_documentation_governance.md), [traceability](00_governance/00_03_traceability_matrix.md) |
| Requirements/domain | [Requirements](01_requirements/01_01_requirements.md), [flows](01_requirements/01_02_use_cases_and_user_flows.md), [domain model/glossary](02_domain/02_01_domain_model_and_glossary.md) |
| Decisions | [ADR index](03_decisions/README.md) |
| Architecture | [System overview](04_architecture/04_01_system_overview.md), [components/runtime](04_architecture/04_02_components_and_runtime_flows.md), [adaptive scheduler](04_architecture/04_03_adaptive_scheduler.md) |
| Data | [Manifest/chunk store](05_data/05_01_manifest_and_chunk_store.md), [lifecycle/recovery](05_data/05_02_data_lifecycle_and_recovery_state.md) |
| Contracts | [Wire protocol](06_contracts/06_01_wire_protocol.md), [CLI/configuration](06_contracts/06_02_cli_and_configuration.md), [internal components](06_contracts/06_03_internal_component_contracts.md) |
| Assurance | [Security/threat model](10_security/10_01_security_privacy_and_threat_model.md), [failure/recovery](11_reliability/11_01_failure_and_recovery_policy.md), [observability](12_observability/12_01_telemetry_logging_and_audit.md) |
| Engineering/quality | [Engineering standards](13_engineering/13_01_engineering_standards.md), [test strategy](14_quality/14_01_test_strategy.md), [benchmark method](14_quality/14_02_benchmark_methodology.md) |
| Delivery/operations | [CI/release/rollback](15_delivery/15_01_ci_release_and_rollback.md), [development/deployment](16_operations/16_01_local_development_and_deployment.md), [workflow](17_workflow/17_01_development_workflow.md), [coding-agent workflow](18_agents/18_01_coding_agent_workflow.md) |
| Completion | [Milestones](19_planning/19_01_milestones.md), [risks](20_risks/20_01_risk_register.md), [Definition of Done](21_definition_of_done.md), [consistency review](24_consistency_review.md), [readiness review](25_implementation_readiness_review.md) |

## Normative status

| Label | Meaning |
|---|---|
| Approved | Binding until changed through the documented process. |
| Accepted ADR | Binding architectural decision. |
| Proposed | Candidate that cannot be treated as approved. |
| Experimental | Must be selected or tuned from evidence and configuration. |
| Implementation-defined | May be chosen by the Builder while preserving documented contracts. |
| TBD | Known unresolved matter with an owner and resolution gate. |

The words **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are normative when capitalized.

## Artifact identifiers

| Prefix | Artifact |
|---|---|
| `FR` | Functional requirement |
| `NFR` | Non-functional requirement |
| `ADR` | Architecture decision record |
| `ARCH` | Architecture component or rule |
| `DB` | Persistent entity/table |
| `API` | Wire-protocol contract |
| `CLI` | Command-line contract |
| `IC` | Internal component contract |
| `TEST` | Verification requirement |
| `RISK` | Risk-register entry |
| `CFG` | Operator configuration |
| `EXP` | Experimentally selected parameter or policy |

Artifact-ID definitions appear in a Markdown heading, an explicit list item, or the first cell of a table row. References must resolve to exactly one such definition. A numbered range such as `FR-004–007` expands inclusively and every member must be defined. A wildcard such as `TEST-UNIT-CDC-*` names a planned family: it is syntax-checked but may have no members until its owning milestone defines them. Wildcards are references only and must never define an artifact.

## Classification of decisions

- Product requirements define user-visible capability and required outcomes.
- Architectural decisions define stable system boundaries and high-cost choices.
- Implementation details may change without changing behavior or contracts.
- Configurable parameters are operator-selected within validated bounds.
- Experimental parameters are selected from reproducible measurement and remain configurable unless interoperability requires a fixed profile.
- Operational concerns cover installation, identity material, storage, diagnostics, recovery, and release procedures.

The original roadmap is retained as input evidence at `E:\programs\c++\dift roadmap\dift-final-roadmap vibecode.html`; it is not normative.
