# Solo Orchestrator — Project Intake Template

## Version 1.0

---

## Document Control

| Field | Value |
|---|---|
| **Document ID** | SOI-004-INTAKE |
| **Version** | 1.0 |
| **Classification** | Project Initialization Template |
| **Date** | 2026-08-03 |
| **Companion Documents** | SOI-002-BUILD v1.0 (Builder's Guide), SOI-003-GOV v1.0 (Enterprise Governance Framework) |

---

## Purpose

This template collects every decision, constraint, and context variable that the AI agent needs to execute the Solo Orchestrator methodology with maximum autonomy. Fill it out completely before starting Phase 0. Incomplete sections will force the agent to stop and ask — every blank field is a round-trip.

### How This Document Flows Into the Process

The Intake is the primary input to the Builder's Guide. Here's where each section goes:

| Intake Section | Consumed By | Purpose |
|---|---|---|
| **1. Project Identity** | Phase 0 initialization, Platform Module selection | Names the project, sets the track, identifies which Platform Module the agent loads |
| **2. Business Context** | Phase 0 Steps 0.1-0.2 | The agent validates and expands this into the FRD and User Journey — it doesn't re-discover it |
| **3. Constraints** | Phase 0 and Phase 1 | Timeline, budget, and user targets constrain architecture and scope |
| **4. Features & Requirements** | Phase 0 Steps 0.1, 0.4 | The agent expands logic triggers and failure states, flags gaps, produces the Manifesto |
| **5. Data & Integrations** | Phase 0 Step 0.3, Phase 1 Step 1.4 | Drives the Data Contract, data model design, and third-party integration architecture |
| **6. Technical Preferences** | Phase 1 Steps 1.2-1.6 | Hard constraints and preferences feed directly into architecture proposals; Competency Matrix determines where automated tooling is mandatory |
| **7. Revenue Model** | Phase 0 Step 0.5, Phase 1 Step 1.2 | Hosting/distribution cost ceiling constrains architecture; pricing model shapes feature decisions |
| **8. Governance Pre-Flight** | Enterprise Governance Framework pre-conditions | Maps directly to the organizational approvals required before Phase 0 can begin |
| **9. Accessibility & UX** | Phase 1 Step 1.5, Phase 3 Step 3.4 | Architectural constraints from Day 1, not Phase 3 afterthoughts |
| **10. Distribution & Operations** | Phase 4, Platform Module | Distribution channels, monitoring, update strategy — platform-dependent |
| **11. Known Risks** | Phase 1 Step 1.3 | Additional inputs for the Iron Logic Stress Test |

The more complete the Intake, the more autonomously the agent can work. Where the Intake is vague or incomplete, the Builder's Guide prompts shift from validation to discovery — the agent will ask targeted questions instead of proposing options it doesn't have enough context to evaluate.

### How to Use This Document

You can fill this out using the **intake wizard** (`bash scripts/intake-wizard.sh`) or by **editing this file directly**. The wizard offers an interactive walkthrough and tracks your progress. Either approach works, but be aware of the difference:

1. Fill out every section. Mark fields N/A where they genuinely don't apply — don't leave blanks.
2. For organizational deployments, complete the Governance Pre-Flight (Section 8) before starting. This section maps to the Enterprise Governance Framework pre-conditions.
3. Once complete, provide this document to the AI agent at the start of Phase 0 with the instruction: "This is the Project Intake. Use it as the primary constraint for all phases. Do not suggest features, architectures, or tooling that contradict it."
4. The agent will use this to generate the Product Manifesto (Phase 0) and Project Bible (Phase 1) without stopping to ask for information that should already be decided.

> **If editing manually:** Section 1 fields (project name, platform, language, track) and Section 8 (governance mode) were used during init to generate your CI pipeline, release pipeline, platform module, and phase gate rules. If you change these fields here, you must also run the reconfigure script to update the generated files:
>
> ```bash
> bash scripts/reconfigure-project.sh --field <field> --old <old_value> --new <new_value>
> ```
>
> Supported fields: `name`, `platform`, `language`. The intake wizard handles this automatically — manual editing does not.
>
> For `track` or `deployment` changes use `scripts/upgrade-project.sh` instead (it enforces the governance pre-conditions; reconfigure-project does not).

> **Provenance note (this walk):** Filled manually (documented Manual mode) by the AI agent, seeded from the recorded product interview with Karl Raulerson (2026-08-03) and from seven judgment decisions Karl answered interactively the same day. Karl reviews and approves this document via the pull-request that lands it on `main`.

---

## 1. Project Identity

| Field | Value |
|---|---|
| **Project name** | powerpoint-voice |
| **Project codename** (if different from public name) | N/A |
| **One-sentence description** | Voice-controlled presentation app: renders PowerPoint decks and responds to spoken commands |
| **Project track** | Full |
| **Platform type** | desktop |
| **Platform Module** | SOI-PM-DESKTOP |
| **Target platforms** | macOS 14+ (primary — showtime machine), Windows 10+, Ubuntu 22.04+ (secondary: portable code + CI builds pre-show; full validation post-show) |
| **Is this a personal project or organizational deployment?** | Organizational |
| **Repository URL** (if already created) | https://github.com/kraulerson/powerpoint-voice |
| **Git host** | github |
| **Repository visibility** | public — org-mode default (private) overridden 2026-08-03 via the github driver's documented recovery option 2; decision + protection verification recorded in APPROVAL_LOG.md (Approval History) and WALK-ISSUE-LOG.md ISSUE-004 |

---

## 2. Business Context

### 2.1 The Problem

```
Karl presents a PowerPoint deck to executives on ~2026-08-10. Today, advancing slides
tethers him to the laptop keyboard or a single-purpose clicker: remotes do only
next/previous, get lost, and die; jumping directly to a specific slide ("go to slide 15")
during Q&A means walking back to the machine and breaking the room's attention. Conference-
room Wi-Fi cannot be trusted, so any cloud-dependent voice solution is disqualified. He
needs hands-free, fully offline voice control of his real .pptx deck — walk the room, say
"next" / "go to slide fifteen", and the projector obeys — reliable enough to bet a live
executive presentation on, with a keyboard fallback for every command if voice degrades.
```

### 2.2 Who Has This Problem

| Field | Value |
|---|---|
| **Primary user persona** | Karl Raulerson — presenter/technologist; high technical skill; goal: deliver a live executive presentation hands-free with direct slide navigation |
| **Secondary personas** (if any) | Colleagues who present (mixed technical skill) — potential adopters at 6-12 months, internal only |
| **How do they solve this problem today?** | Keyboard/trackpad at the laptop, or a basic presenter remote (next/previous only) |
| **What's wrong with the current solution?** | Tethered to the machine or limited to linear next/previous; no direct jump-to-slide; remote is extra failure-prone hardware; nothing is voice-driven or robust to a no-network room |

### 2.3 Success Criteria

| Metric | Target | How Measured |
|---|---|---|
| Command recognition success (baseline 5 commands) | ≥95% of clearly-spoken commands correctly executed | 50-command scripted rehearsal run on the showtime MacBook; tally via the transcript overlay |
| Command-to-action latency | ≤1.5 s p95 from end of utterance to slide action | Timestamped session log during the rehearsal run |
| Network independence | 100% functionality with Wi-Fi/network disabled | Full rehearsal executed in airplane-mode/network-off |
| Rendering fidelity (real deck) | Zero content-visible defects vs. PowerPoint reference | Karl's slide-by-slide sign-off at rehearsal (deck loaded locally only) |
| Live use | 2026-08-10 presentation delivered with the app as primary control | It happened; keyboard fallback use counts as graceful degradation, not failure |

### 2.4 What This Is NOT

1. Not a slide editor — no creation, editing, or export of deck content.
2. Not a cloud/web service — no accounts, no telemetry, no network features of any kind.
3. Not a dictation or general speech product — fixed command grammar only.
4. Not an animation-faithful player — static slide rendering; no transitions, builds, embedded video/audio.
5. Not a meeting assistant — no audio recording, storage, or transcription beyond the live command overlay.

---

## 3. Constraints

### 3.1 Timeline

| Field | Value |
|---|---|
| **Target MVP date** | 2026-08-08 (rehearsal-ready; presentation ~2026-08-10) |
| **Hard deadline?** | Yes — the presentation happens on ~2026-08-10 regardless. If missed: fall back to stock PowerPoint + keyboard; the walk's findings still deliver. |
| **Orchestrator availability** | "As many as needed." (Karl, verbatim, 2026-08-03) — plan as full availability through 2026-08-09 |
| **Blocked time or interleaved?** | Effectively blocked — this is the priority through the presentation date |

### 3.2 Budget

| Field | Value |
|---|---|
| **Monthly infrastructure ceiling** | $0 — local desktop app, no hosting; GitHub free tier (approved by Karl 2026-08-03) |
| **One-time budget** (if any) | $0 — code signing deferred post-MVP; no domain/trademark spend |
| **AI subscription** | Already provisioned — consumer Claude subscription via Claude Code (see §5.1.1 ZDR exception; AI-path approval APPROVAL_LOG row 1) |
| **Who approves spending?** | Karl Raulerson (Sponsor) |

### 3.3 Users

| Field | Value |
|---|---|
| **Users at launch** | 1 — Karl (presenting) |
| **Users at 6 months** | ≤5 — colleagues may adopt (internal) |
| **Users at 12 months** | ≤10 — internal colleagues |
| **Internal only or external?** | Internal |
| **Geographic distribution** | Single location, US — no data sovereignty concerns |

---

## 4. Features & Requirements

### 4.1 Must-Have Features (MVP)

| # | Feature | Business Logic Trigger | Failure State |
|---|---|---|---|
| 1 | PPTX load & render (text+images tier, from-scratch C++ renderer) | If the user opens a .pptx (File→Open, drag-drop, or CLI argument), the system must parse the OOXML package and render every slide — text boxes with formatting, placed images, solid/picture backgrounds — and output a pixel-stable full-screen slide view | Invalid/corrupt file → error naming the failing part; app keeps running. Unsupported slide element → visible placeholder plus a load-time warning listing every unsupported item (never a silent wrong render). File >200MB or >300 slides → reject with the stated limit. |
| 2 | Voice navigation: "next" / "previous" | If the always-on recognizer matches "next" or "previous" while presenting, the system must advance/rewind exactly one slide within 1.5 s and show the command in the overlay | At last/first slide → no-op plus overlay notice. Low-confidence match → no action; overlay shows what was heard. Mic unavailable → persistent banner; keyboard fallback unaffected. |
| 3 | Recognition control: "pause" / "continue" | If "pause" is matched, the system must suspend command matching (listening only for "continue"), show PAUSED in the overlay; "continue" resumes full matching | Repeated "pause"/"continue" are idempotent. Recognizer/engine crash → automatic engine restart with an overlay alert; keyboard pause toggle works regardless of engine state. |
| 4 | "Go to slide N" with robust numbers | If "go to slide <number>" is matched and N is within the deck, the system must jump directly to slide N; accepted forms: digits ("15"), number words ("fifteen"), digit-by-digit ("one five") | N out of range → overlay "deck has M slides", no movement. Unparseable number → overlay shows heard text, no movement. |
| 5 | Live command-transcript overlay | If any utterance is processed, the system must display the heard text and the matched command (or "no match") within 500 ms, auto-fading after ~3 s | Overlay must never obscure more than the reserved strip of the slide; an overlay rendering failure must never take down the slide view. |
| 6 | Keyboard fallback for every command | If arrow keys / space / B / P / typed-number+Enter are pressed at any time, the system must execute the identical action to the corresponding voice command | Keyboard path is fully independent of the speech engine — it must work even when the engine is dead or paused. |
| 7 | Presentation mode UI (minimal dark, dual-display aware) | If a presentation is started, the system must render the full-screen minimal-dark view (slide, overlay strip, slide counter) and route the slide view to the external display when one is attached | No external display → single-screen full-screen mode. Display disconnect mid-show → fall back to the laptop screen without crashing or losing position. |

### 4.2 Should-Have Features (Post-MVP v1.1)

1. Timer / elapsed-clock in the presenter overlay (P1).
2. Practice mode with per-command recognition accuracy stats (P1).
3. Microphone input-device picker (P1).
4. Presenter-notes view on the laptop with clean projector output (P2).
5. Windows/Linux packaging and full validation (P2 — code stays portable and CI-built all along).

### 4.3 Will-Not-Have Features (Explicit Exclusions)

1. Deck editing, creation, or export of any kind.
2. Any network-dependent capability: cloud speech, accounts, telemetry, update phone-home.
3. Free-form dictation, wake words, or any speech feature beyond the fixed command grammar.
4. Animation/transition playback, embedded video/audio rendering.
5. Recording or persisting any audio.

---

## 5. Data & Integrations

### 5.1 Data Inputs

| Input | Data Type | Validation Rules | Sensitivity | Required? |
|---|---|---|---|---|
| .pptx presentation file | Binary OOXML (zip) | Valid zip containing ppt/presentation.xml; ≤200MB; ≤300 slides; unsupported elements surfaced at load | Confidential (executive deck content; never leaves the machine, never committed — fixtures are synthetic/sanitized) | Yes |
| Microphone audio | PCM stream (~16 kHz) | Processed on-device in memory only; never persisted, never transmitted | Confidential (live room audio) | Yes |
| User settings | JSON preferences file | Schema-validated keys (overlay fade, mic device, keybindings); unknown keys rejected | Internal | No |

**Sensitivity classifications:** Public, Internal, Confidential, PII, Financial, Health/Medical, Regulated

#### 5.1.1 Project-Level Data Classification (Phase 1 Gate — tier-crosscheck-6)

The **highest** classification across all rows in §5.1 is the project-level `data_classification`. It is recorded in `.claude/process-state.json::phase1_artifacts` and enforced as a Phase 1→2 hard gate by `scripts/check-phase-gate.sh`. Per docs/governance-framework.md § VII (Mandatory ZDR gate, line 299), projects classified **Internal or higher** must use a ZDR or self-hosted LLM deployment path — `phase1_artifacts.zdr_attested` (or a documented `phase1_artifacts.zdr_attestation_reason` exception) is required before Phase 1→2.

| Field | Value (one of) |
|---|---|
| **Project-level data_classification** | `confidential` |
| **ZDR attested (Zero Data Retention or self-hosted LLM)** | `false` |
| **ZDR attestation reason** _(required when `zdr_attested=false` AND classification > public)_ | Exception approved by CISO (Karl Raulerson, role-played) 2026-08-03: the Confidential asset (the real deck) is never transmitted to the LLM — the LLM processes only source code and synthetic/sanitized fixtures; the real deck is used exclusively in local UAT/rehearsal on Karl's machine. |

These three fields are captured by `scripts/intake-wizard.sh` Section 5.5, and can be corrected after-the-fact with `scripts/reconfigure-project.sh --field data_classification --new <value>` / `--field zdr_attested --new true|false`.

### 5.2 Data Outputs

| Output | Format | Latency Expectation |
|---|---|---|
| Rendered slide view | On-screen (projector/laptop) | <200 ms per slide change |
| Command-transcript overlay | On-screen text strip | <500 ms after utterance |
| Session command log | In-memory; optional local debug file (off by default) | N/A |

### 5.3 Third-Party Integrations

| Service | What Data We Send/Receive | Auth Method | Fallback if Unavailable | Existing Account? |
|---|---|---|---|---|
| None — fully offline by design | N/A | N/A | N/A | N/A |

### 5.4 Data Persistence

| Question | Answer |
|---|---|
| **What data must persist across sessions?** | User settings (JSON) and a recent-files list (paths only, never deck content) |
| **What data can be ephemeral (browser/device only)?** | All audio (never stored), deck content (re-read from the .pptx each open), transcript history (session-only) |
| **Expected data volume at 12 months** | Small — <10 MB of settings/logs |
| **Data retention requirements** | Settings until the user deletes them; no regulatory retention |
| **Backup requirements** | None — the source .pptx is the user's own file; settings are trivially recreated |

---

## 6. Technical Preferences

### 6.1 Orchestrator Technical Profile

| Field | Value |
|---|---|
| **Languages you know well** | Shell/bash, JavaScript/TypeScript, Python (drafted from prior work; Karl corrects at intake review if wrong) |
| **Frameworks you've used** | CI/CD (GitHub Actions) and scripting toolchains; no prior Qt/desktop-GUI framework claimed |
| **Languages/frameworks you're willing to learn** | C++ toolchain depth (CMake/Qt) as needed to review this project |
| **Languages/frameworks you refuse to use** | None |
| **Database experience** | Basic (SQLite-level); no DB required for this project |
| **DevOps experience level** | Intermediate — comfortable with CI/CD pipelines and GitHub administration |
| **Mobile development experience** | None — N/A for this project |

### 6.2 Competency Matrix

_For each domain, answer honestly: "Can I look at the AI's output and reliably determine if it's correct?"_ (Karl's answers, 2026-08-03.)

| Domain | Self-Assessment | Automated Tooling Required? |
|---|---|---|
| Product/UX Logic | Yes | No |
| Frontend Code (native UI for this project) | Partially | Yes — mandatory in Phase 3 |
| Backend / API Design | N/A — no server/API in this application | N/A |
| Database Design & Queries | Yes (scope: local file/JSON storage only) | No |
| Security (Auth, Injection, IDOR) | Yes | No (framework scanners still run per track) |
| DevOps / Infrastructure | Yes | No |
| Accessibility (WCAG) | Partially | Yes — mandatory in Phase 3 |
| Performance Optimization | Yes | No |
| Mobile (iOS/Android) | N/A — no mobile target | N/A |

_Every "Partially" or "No" means automated tooling is mandatory in Phase 3. The agent will factor this into architecture selection and testing strategy._

### 6.3 Development Environment

| Field | Value |
|---|---|
| **Primary development machine** | macOS (Darwin 25.4.0), Apple Silicon MacBook — also the showtime machine |
| **Secondary machines** (if any) | None |
| **IDE/Editor** | Claude Code (terminal) + system editor |
| **Docker available?** | Yes (Colima) |
| **Node.js version** | 25.9.0 (infrastructure tooling only) |
| **Python version** (if applicable) | N/A for product code |
| **Claude Code installed?** | Yes (2.1.220) |
| **AI subscription tier** | Consumer Claude subscription (see §5.1.1 ZDR exception) |

### 6.4 Architecture Preferences & Constraints

**All Platforms:**

| Field | Value | Hard Constraint or Preference? |
|---|---|---|
| **Primary language** | C++ | **Hard constraint** (Karl, interview 2026-08-03) |
| **Data storage** | Local filesystem + JSON settings; no database | Preference |
| **Authentication** | None — single-user local app, no accounts | **Hard constraint** |

**Web Applications:**

| Field | Value | Hard Constraint or Preference? |
|---|---|---|
| **Frontend framework** | N/A — not a web application | N/A |
| **Backend framework** | N/A | N/A |
| **Hosting** | N/A | N/A |

**Desktop Applications:**

| Field | Value | Hard Constraint or Preference? |
|---|---|---|
| **UI framework** | No preference — agent proposes options in Phase 1 (cross-platform C++ toolkit expected, e.g. Qt) | Preference |
| **Packaging format** | DMG for macOS primary; others deferred to P2 | Preference |
| **Auto-update strategy** | Manual download (GitHub Releases) | Preference |
| **Offline requirement** | **Fully offline** — zero network dependency at runtime, including speech recognition | **Hard constraint** (venue reliability, interview 2026-08-03) |

**Mobile Applications:**

| Field | Value | Hard Constraint or Preference? |
|---|---|---|
| **Framework** | N/A — no mobile target | N/A |
| **Minimum OS version** | N/A | N/A |
| **App store distribution** | N/A | N/A |
| **Offline requirement** | N/A | N/A |
| **Device API requirements** | N/A | N/A |
| **Biometric authentication** | N/A | N/A |

**Cross-Cutting:**

| Field | Value | Hard Constraint or Preference? |
|---|---|---|
| **Monorepo or separate repos?** | Single repo | Preference |
| **Web + Desktop, Web + Mobile, or single platform?** | Single platform: desktop (cross-OS targets within it) | **Hard constraint** |

### 6.5 Existing Infrastructure to Integrate With

| System | Details | Integration Required? |
|---|---|---|
| **SSO / Identity Provider** | None | N/A |
| **Logging / SIEM** | None — local structured logs only | N/A |
| **Monitoring** | None — GitHub Actions CI status + local logs | N/A |
| **Data Warehouse** | None | N/A |
| **Backup Infrastructure** | None required (see §5.4) | N/A |
| **CI/CD Platform** | GitHub Actions | Yes |
| **Repository Platform** | GitHub (kraulerson/powerpoint-voice) | Yes |
| **Other** | None | N/A |

---

## 7. Revenue Model (Standard+ Track — skip for internal tools)

| Field | Value |
|---|---|
| **Pricing model** | N/A — internal tool, no revenue |
| **Target price point** | N/A |
| **Competitive price range** | N/A |
| **Per-user cost estimate** (hosting, API calls, storage) | $0 — fully local |
| **Break-even user count** | N/A |
| **Hosting cost ceiling at launch** | $0 |
| **Hosting cost ceiling at 1,000 users** | N/A — internal ceiling is ≤10 users (§3.3) |
| **Hosting cost ceiling at 10,000 users** | N/A |

---

## 8. Governance Pre-Flight (Organizational Deployments Only)

_Skip this section for personal projects. For organizational deployments, every field must be completed or marked "In Progress" with an expected completion date. Phase 0 cannot begin until all "Blocking" items are resolved._

**Governance Mode:** Production

### 8.1 Pre-Conditions

| Pre-Condition | Status | Details | Blocking? |
|---|---|---|---|
| **AI deployment path approved by IT Security** | Complete | Anthropic Claude via Claude Code under existing subscription terms — APPROVAL_LOG.md row 1 (Karl as CISO, 2026-08-03) | Yes |
| **Insurance confirmation obtained** | Complete | No policies exist; risk accepted in full by owner — APPROVAL_LOG.md row 2 (Karl as broker/risk, 2026-08-03) | Yes |
| **Liability entity designated** | Complete | Karl Raulerson (individual) — APPROVAL_LOG.md row 3 (Karl as GC, 2026-08-03) | Yes |
| **Project sponsor assigned** | Complete | Name: Karl Raulerson — APPROVAL_LOG.md row 4 | Yes |
| **Backup maintainer designated** | Complete | Name: Karl Raulerson (dual-hatted; weakening acknowledged) — APPROVAL_LOG.md row 5 | Yes |
| **ITSM ticket filed / portfolio registered** | Complete | Ticket #: kraulerson/powerpoint-voice#1 — APPROVAL_LOG.md row 6 | Yes |
| **Exit criteria defined** | Complete | See Section 8.5 below | Required (not blocking — pilot prep, see Governance Framework §XIV) |
| **Orchestrator time allocation approved** | Complete | "As many as needed" through 2026-08-09 (Karl, 2026-08-03), effectively blocked time | Required (not blocking — pilot prep, see Governance Framework §XIV) |

### 8.2 Approval Authorities

| Gate | Approver Name | Approver Role |
|---|---|---|
| **Phase 0 → Phase 1** (business justification) | Karl Raulerson | Project Sponsor |
| **Phase 1 → Phase 2** (architecture approval) | Karl Raulerson | Senior Technical Authority (role-played per walk protocol) |
| **Phase 3 → Phase 4** (go-live approval) | Karl Raulerson | Application Owner AND IT Security (both sign-offs recorded separately, per walk protocol) |

### 8.3 Escalation Chain

| Level | Name | Role | Contact |
|---|---|---|---|
| 1 (first escalation) | Karl Raulerson | Orchestrator / Sponsor | In-session (walk protocol) |
| 2 | Karl Raulerson | CISO (role-played) | In-session (walk protocol) |
| 3 (final authority) | Karl Raulerson | Owner / final authority | In-session (walk protocol) |

### 8.4 Compliance Screening

_Completed with the project sponsor (Karl Raulerson), approved 2026-08-03._

| Question | Yes/No | Required Action | Status |
|---|---|---|---|
| Does this application process data used in financial reporting? | No | N/A | N/A |
| Does this application handle payment card data (even masked)? | No | N/A | N/A |
| Does this application collect personal data from users in multiple states or internationally? | No | N/A | N/A |
| Are any users or subsidiaries in the EU? | No | N/A | N/A |
| Does any subsidiary operate in a sanctioned jurisdiction? | No | N/A | N/A |
| Is data subject to records retention requirements? | No | N/A | N/A |
| Will the deployed application include AI-powered features for end users? | Yes | EU AI Act classification for deployed product | Complete — on-device speech recognition; no EU users/nexus (§3.3), outside territorial scope; note recorded here per sponsor approval 2026-08-03 |
| Does your organization require penetration testing for all production applications? | Yes | Schedule pen test for Phase 3 | Planned — Full track mandates it; scheduled as part of Phase 3 validation |

### 8.5 Exit Criteria

| Outcome | Definition | Decision Maker |
|---|---|---|
| **Success** (proceed to scale) | App used as primary control in the 2026-08-10 executive presentation; §2.3 rehearsal criteria met; framework process completed to v1.0.0 | Karl Raulerson |
| **Conditional** (proceed with modifications) | App rehearsal-ready but a criterion partially missed (e.g., recognition 90-95%) — present keyboard-primary with voice assist; remediate post-show | Karl Raulerson |
| **Failure** (stop) | Rendering or recognition unreliable at the 2026-08-08 rehearsal — fall back to stock PowerPoint; walk findings still delivered | Karl Raulerson |

---

## 9. Accessibility & UX Constraints

| Field | Value |
|---|---|
| **Accessibility requirements** | WCAG AA-equivalent for a native desktop app: full keyboard operability (already a Must-Have), text contrast ≥4.5:1 in overlay/UI, visible focus states |
| **Color vision deficiency considerations** | Yes — never rely on color alone for meaning; pair color with text/icon/position |
| **Supported browsers** | N/A — native desktop application |
| **Mobile responsive required?** | No |
| **Supported devices** | Desktop only (laptop + external display/projector) |
| **Branding / style guide** | None — minimal dark aesthetic per interview; agent's discretion within it |
| **Dark mode required?** | Yes — minimal dark IS the design (§interview); no light theme in MVP |

---

## 10. Distribution & Operations Preferences

**All Platforms:**

| Field | Value |
|---|---|
| **Notification preferences for alerts** | GitHub email notifications for CI failures; in-app surfacing for runtime errors (local app, no paging) |
| **Uptime expectation** | Best effort — local desktop application |
| **Environment strategy** | Production only (plus local dev builds) |

**Web Applications:**

| Field | Value |
|---|---|
| **Domain name** (if already acquired) | N/A |
| **SSL certificate** | N/A |
| **Maintenance window preferences** | N/A |

**Desktop Applications:**

| Field | Value |
|---|---|
| **Distribution channels** | GitHub Releases |
| **Code signing** | Deferred to post-MVP (unsigned local build acceptable for own-machine showtime use) |
| **Code signing certificates** (if required) | Need to acquire when un-deferred (Apple Developer $99/yr; Windows EV later) — recorded in Tooling Configuration deferred list |
| **Auto-update mechanism** | Manual download |
| **Minimum supported OS versions** | macOS 14+ (primary); Windows 10+, Ubuntu 22.04+ (secondary targets) |
| **Installer format preferences** | DMG (macOS); others deferred to P2 |

**Mobile Applications:**

| Field | Value |
|---|---|
| **Distribution** | N/A |
| **Developer accounts** | N/A |
| **Beta testing** | N/A |

---

## 11. Known Risks & Concerns

```
1. SCHEDULE RISK — FORMALLY LOGGED PER INTERVIEW COMMITMENT: Karl chose a from-scratch
   C++ OOXML renderer over the recommended embedded-LibreOffice engine, against the
   developer's explicit fidelity/schedule warning (interview, 2026-08-03). Mitigations:
   renderer scope fenced to the text+images tier (§4.1-1); unsupported elements always
   render visible placeholders (never silent drift); rendering fidelity is a rehearsal
   success criterion (§2.3); terminal fallback is stock PowerPoint (§8.5).
2. Hard immovable deadline: the presentation occurs ~2026-08-10 regardless of readiness.
3. Deck fonts unknown (interview: "not sure") — design defensively for embedded and
   custom fonts; font substitution must be visible, never silent.
4. Solo-organization governance friction: every merge to main requires Karl's audited
   manual un-block (WALK-UNBLOCK-AUDIT.md; WALK-ISSUE-LOG ISSUE-006).
5. Dual-purpose project: full-rigor framework dogfood walk — findings logging
   (WALK-ISSUE-LOG.md) adds overhead at every phase.
6. cpp is a framework language extension (ISSUE-001/002/003): the CI pipeline was
   authored by the walker; the release pipeline requires manual C++ configuration in
   Phase 4.
7. Speech input quality risk: built-in MacBook mic in a conference room (distance,
   acoustics). Mitigations: grammar-constrained on-device recognition, transcript overlay
   for instant diagnosis, keyboard fallback for every command.
```

---

## 11.5. Testing & Bug Tracking

| Field | Value |
|---|---|
| **Testing interval** | Every 2 features (default) |
| **Bug tracking tool** | GitHub Issues |
| **Human tester count** | 1 (Karl) |
| **Beta tester coordination** (if >1 tester) | N/A — single tester |
| **Bug severity SLAs** (Full UAT level only) | SEV-1: 24h / SEV-2: 7d / SEV-3: best effort (defaults) |

> **How this is used:** The agent pauses construction every N features to run a UAT testing session. Agent testers run automated, exploratory, and cross-platform tests in parallel while you test manually. Bugs are compiled, triaged, and fixed before construction resumes. See Steps 2.7-2.9 in the Builder's Guide.

---

## 12. Tooling Configuration

> This section is auto-populated by `init.sh` based on the tool installation matrix. It records what was installed, what needs manual setup, and what is deferred to later phases. Claude reads this to understand the available tooling environment.
>
> If this section is empty, run `init.sh` or manually populate `.claude/tool-preferences.json`.

<!-- AUTO-GENERATED BY INIT.SH — do not edit above this line -->

---

## 13. Agent Initialization Prompt

_Once this template is complete, provide it to the AI agent at the start of Phase 0 along with the Builder's Guide. Copy and customize the bracketed sections._

_The Builder's Guide contains dual-path prompts for Phase 0 and Phase 1 — one for Intake-first (validation and expansion) and one for conversational discovery (without Intake). By providing this Intake, you are activating the Intake-first path. The agent will validate, expand, and challenge your inputs rather than discovering them from scratch._

```
You are the AI execution layer for a Solo Orchestrator project. I am the
Orchestrator. I define intent, constraints, and validation. You provide
architecture, code, and documentation within the constraints I set.

ATTACHED:
1. Project Intake Template (this document) — your primary constraint
2. Solo Orchestrator Builder's Guide v1.0 — your process reference
3. Platform Module: DESKTOP — your platform-specific
   reference for architecture, tooling, testing, and distribution

DOCUMENT RELATIONSHIP:
- The Intake is the DATA SOURCE. It contains my decisions, constraints,
  requirements, technical profile, and (if organizational) governance
  pre-conditions.
- The Builder's Guide is the PROCESS. It defines the phases, steps,
  quality gates, and remediation procedures you follow.
- The Platform Module is the PLATFORM IMPLEMENTATION GUIDE. When the
  Builder's Guide shows a ⟁ PLATFORM MODULE callout, reference the
  attached Platform Module for platform-specific instructions.
- Where the Builder's Guide shows "With Intake" prompts, use those.
  They direct you to validate and expand my Intake data rather than
  re-discovering it.

RULES:
- The Project Intake is the governing constraint. Do not suggest features,
  architectures, or tooling that contradict it.
- The Builder's Guide defines the phase-by-phase process. Follow it.
- The Platform Module defines platform-specific implementation. Follow it
  at every ⟁ callout point.
- If the Intake specifies a hard constraint, respect it absolutely.
- If the Intake specifies a preference, you may recommend against it with
  justification, but defer to my decision.
- If the Intake leaves a field as "no preference," make a recommendation
  based on the constraints and explain your reasoning.
- If the Intake leaves a field blank or incomplete, flag it immediately
  and ask for the specific missing information before proceeding past
  the step that requires it.
- For any domain where my Competency Matrix (Section 6.2) says "Partially"
  or "No," default to the most conservative, well-documented option and
  ensure automated validation tooling covers that domain.
- Do not add features not in the MVP Cutline (Section 4.1).
- Do not suggest dependencies without justification.
- Every feature must have tests before implementation.
- Flag any conflict between the Intake constraints and technical feasibility
  immediately — do not silently work around it.

ACCESSIBILITY (from Section 9):
Color vision deficiency: never rely on color alone for meaning — pair
color with text, icons, or position. Full keyboard operability for every
function (this is also Must-Have feature 6). Text contrast >= 4.5:1 in
the overlay and all UI chrome. Visible focus states.

PROJECT TRACK: Full
PLATFORM: Desktop
TARGET PLATFORMS: macOS 14+ (primary — showtime), Windows 10+,
Ubuntu 22.04+ (secondary: portable + CI-built now, validated post-show)

BEGIN: Execute Phase 0, Step 0.1 using the "With Intake — Validation
Prompt" path from the Builder's Guide. Use Sections 2 and 4 of the
Intake as the primary data source. Generate the Functional Requirements
Document by expanding my business logic triggers and failure states.
Where I've been vague, make it specific and flag for my review. Where
I've been contradictory, identify the contradiction and ask me to resolve
it. Where I've omitted an implicit dependency (e.g., features that
require authentication but I didn't list authentication), flag it as a
recommended addition.
```

---

## Checklist Before Starting

- [x] Every field is filled in or explicitly marked N/A
- [x] Must-Have features all have business logic triggers (If X, then Y)
- [x] Must-Have features all have failure states defined
- [x] Will-Not-Have list has at least 3 items
- [x] Data sensitivity classifications are assigned to all inputs
- [x] Competency Matrix is completed honestly
- [x] Budget constraints are realistic (not aspirational)
- [x] Timeline includes Orchestrator availability, not just calendar dates
- [x] For organizational deployments: all Section 8 "Blocking" items are Complete
- [x] Success/failure exit criteria are defined and a decision-maker is named
- [x] This document has been saved as `PROJECT_INTAKE.md` in the project repository

---

## Document Revision History

| Version | Date | Changes |
|---|---|---|
| 1.0 | 2026-04-02 | Initial release. |
| 1.1 | 2026-08-03 | Filled completely (Manual mode) from the recorded product interview + Karl's seven interactive judgment decisions; §13 customized; pending Karl's review via PR. |

---

## Tooling Configuration

> Auto-generated by init.sh. Full machine-readable config: `.claude/tool-preferences.json`

**Resolved for:** Darwin / desktop / cpp / full track

### Installed
| Tool | Category | Version |
|---|---|---|
| Git | version_control | 2.50.1 |
| jq | json_processor | jq-1.7.1-apple |
| Node.js | runtime | 25.9.0 |
| Docker | containerization | 29.3.1 |
| Colima | containerization | 0.10.1 |
| GPG | commit_signing | 2.5.20 |
| Semgrep | SAST Scanner | 1.157.0 |
| gitleaks | Secret Detection | 8.30.1 |
| Snyk CLI | Dependency Scanner | 1.1304.1 |
| Claude Code | ai_agent | 2.1.220 (Claude Code) |
| Development Guardrails for Claude Code | dev_framework | 0396a1a |
| Superpowers | claude_plugin | installed |
| Context7 MCP | mcp_server | configured |
| Qdrant MCP | mcp_server | configured |
| Xcode Command Line Tools | desktop_build_tools | Xcode 26.6 |

### Deferred (Phase 3+)
| Tool | Phase | Category |
|---|---|---|
| Apple Developer Program (Desktop) | 4 | code_signing |
| EV Code Signing Certificate (Windows) | 4 | code_signing |
