# Technical User Review (Non-Coder) — powerpoint-voice

**Reviewer perspective:** 15+ years in IT operations and technical programme management. Comfortable in a
terminal, comfortable reading configuration files and technical documentation, comfortable evaluating
whether software is fit to deploy. **I do not write code.** If something can only be understood by reading
C++, I have counted that as a usability failure, not as my failure.

**Review date:** 2026-08-10
**What I reviewed:** every user-facing document in the repository, the pre-talk test script, the build and
deck-conversion scripts, the release pipeline, and — where I needed to check whether a documented promise
was real — the specific source lines that implement it. I did not build the application, did not run it,
and did not open the Confidential deck referenced outside this repository.

**Method note.** I did not read the source to review the engineering. I read it in exactly three places,
each time because a user-facing document told me the software would do something and I wanted to know
whether it does. Two of those three checks came back "no."

---

## Executive Summary

*(Written as if explaining it to a non-technical friend.)*

This is a small Mac app that puts your PowerPoint deck on the projector and lets you move through it by
saying one of five phrases, with the arrow keys always working as a backup. It genuinely does something
useful, it keeps everything on your own laptop — nothing is uploaded, nothing is recorded — and its
privacy policy is one of the clearest I have ever read from any software project. The user guide is short,
plain, and honest enough to publish its own error rate, which almost nobody does.

But it is not a product yet, and two things would stop me handing it to a colleague today. First, the
guide tells you that saying "pause presentation" turns the microphone off before you take questions. It
does not. The microphone stays on and keeps listening; only the slide controls are switched off — and the
project's own measurements show that the phrases most likely to be misheard are precisely the ones that
switch it back on. Second, the guide tells you four separate times that if voice stops working, a message
will appear on screen explaining why. That message is not wired up to anything. It is written into memory
and never shown. So the one instruction the guide gives you for the most likely failure — "check the
message on screen" — points at something that does not exist.

Neither is hard to fix, and both are documentation-versus-reality mismatches rather than deep flaws. But
they are in the two places where a presenter standing in front of a room actually depends on the words
being true.

---

## Can I Actually Use This?

Honest answers, by situation.

| I want to… | Can I? | The honest answer |
|---|---|---|
| **Present from it myself, this week, on the machine it was built for** | **Yes** | This is what it is built for and it has been tested end-to-end on that hardware. Use the P key before Q&A and understand what it really does (see Finding 1). Keep a hand near the arrow keys. |
| **Get a copy of the app** | **No, not from the documentation** | The guide's step 1 says "unzip `powerpoint-voice-test-build.zip`". Nowhere does any document say where that file comes from. There is no download link, no release, no version tag, and no checksum. Today you get it because the author hands it to you. |
| **Build it myself from the source** | **No** | There are no build instructions in any user-facing document. The information exists, scattered across a build script and one line buried in a five-day-old test report, but you would have to reverse-engineer it. |
| **Give it to a colleague to present with** | **Not yet** | They would hit the Gatekeeper security warning with no explanation of why it is safe to bypass, no version number to tell you what they have, and the two documentation defects above. |
| **Deploy it across my department** | **No** | It is unsigned, un-notarised, has no installer, no updates, no version numbering, and runs only on macOS 26 on Apple Silicon. To the project's credit, it says all of this plainly rather than pretending otherwise. |
| **Show it to my security team** | **Yes, actually** | `SECURITY.md` and `PRIVACY_POLICY.md` are strong enough to survive that meeting. This is the project's best work. Correct Finding 1 first, because the pause claim is the one thing in them a reviewer could catch out. |
| **Understand what it does with my data without asking a developer** | **Yes** | Completely. `PRIVACY_POLICY.md` is exemplary. |

---

## Phase 1 — My Onboarding, Step by Step

I went through this in the order a new person would, and wrote down every point where I stopped.

### Step 1 — I opened the folder looking for a README. There isn't one.

**This is where I got stuck first, before reading a single word of documentation.**

The project root contains 40+ entries. Not one of them is a README. Every convention I have ever worked
with says the README is the front door, and there is no front door here. What I actually saw was a wall of
files with no indication which of them was meant for me:

```
APPROVAL_LOG.md      BUGS.md (68 KB)         CHANGELOG.md (33 KB)
CLAUDE.md            FEATURES.md             PRIVACY_POLICY.md
PRODUCT_MANIFESTO.md PROJECT_BIBLE.md        PROJECT_INTAKE.md (37 KB)
RELEASE_NOTES.md     SECURITY.md             THIRD_PARTY_NOTICES.md
USER_GUIDE.md        WALK-ISSUE-LOG.md (84 KB)   WALK-STATE.md
WALK-UNBLOCK-AUDIT.md   sbom.json   ... plus 8 directories
```

I found `USER_GUIDE.md` by guessing from the name. That guess turned out to be right, and the guide turned
out to be good — but I only got there by luck. Someone less patient opens `PROJECT_BIBLE.md` or
`PROJECT_INTAKE.md` first, hits dense internal process documentation, and concludes this is not for them.

Of the 20-odd markdown files in the root, exactly **four** were written for me: `USER_GUIDE.md`,
`SECURITY.md`, `PRIVACY_POLICY.md`, and `LICENSE`. The other sixteen are build-process records. They are
clearly valuable to the people building this. They are noise to the person deciding whether to use it, and
right now they are the first thing that person sees.

### Step 2 — `USER_GUIDE.md`. This is the good part.

It is 189 lines. I read it in about eight minutes and understood the whole product. It is organised the
way I actually need it — *Before the talk*, *Running a presentation*, *Known limits*, *If something goes
wrong* — which is the order things happen, not the order the software was built. It uses plain words. It
has a real troubleshooting table with three columns: what you see, what it means, what to do. That table
is the single most useful thing in the repository.

Specific things it does that most projects do not:

- It tells me the **keyboard always works** and repeats it in four places. That is the thing I most need to
  believe when I am standing in front of twenty people.
- It publishes its own **error rate** — "of 102 such near-miss phrases, 60 matched a command". I have
  reviewed a lot of software documentation and I can count on one hand the number that volunteer a number
  that unflattering. That earned my trust immediately.
- It explains **why** the commands need two words, rather than just stating the rule.
- It has a **screen reader** section, which is rare for a project this size.

I have two structural criticisms of it and two accuracy criticisms. The accuracy ones are Findings 1 and 2
below, and they are serious. The structural ones:

**It never says where the app comes from.** Step 1 of "Before the talk" is "Unzip
`powerpoint-voice-test-build.zip`." I stopped there for a while, genuinely unsure whether I had missed a
download section. I had not. There is no link, no release page, no version, no checksum. If I received
this repository without receiving the zip alongside it, my onboarding ends at line 12 of the guide.

**It does not say what version I have.** There is no version anywhere in it. Combined with the empty
release notes (Step 4 below), I have no way to answer "is the copy on my laptop the one this guide
describes?" For software where the behaviour of the P key changed as recently as the day the guide was
written, that is a real gap.

### Step 3 — Following the install instructions.

The Gatekeeper instruction is **present and correct**, which is more than many projects manage:

> **First launch: right-click the app → Open**, then confirm. A normal double-click will be blocked —
> the app is not yet notarised by Apple. You only have to do this once.

It also gives a fallback command if that fails. Good. But as the person who in my day job tells staff
*never* to bypass a macOS security warning, here is what is missing for me to be comfortable:

- **It does not tell me what I will actually see.** Recent macOS versions do not always offer "Open" on the
  right-click menu any more — the flow can push you to System Settings → Privacy & Security → "Open
  Anyway". If I do not see the dialog the guide implies, I do not know whether I have done it wrong or
  whether something is genuinely broken.
- **It does not explain what "not notarised" means or why it is safe here.** The guide states the fact and
  moves on. The actual reassurance — that this is one person's project, built from source you can read,
  with no network access at all — exists in `SECURITY.md` and `PRIVACY_POLICY.md`, but nothing connects the
  scary moment to the reassuring answer. One sentence and a link would fix this.
- **There is no checksum.** I am being asked to bypass an operating system security control for an 80 MB
  binary, and I have no way to verify the file is the one intended. `SECURITY.md` describes verifying a
  build you compiled yourself, which does not help someone who received a zip.
- **The `xattr -dr com.apple.quarantine` fallback is given without explanation.** I know what it does. The
  guide's target reader may not, and it is being offered as a fix for a security prompt. That deserves a
  half-sentence.

The **microphone permission** step is handled well. It says what will be asked, says to allow it, and —
importantly — says what happens if you decline and that the app still works. That is exactly right.

**"Requires macOS 26 or newer, on Apple Silicon"** is stated. Good. It is stated *third*, after the install
and Gatekeeper steps, which is the wrong order — a compatibility requirement belongs before the
instructions that assume it.

### Step 4 — `RELEASE_NOTES.md`. It is an empty template.

This is the file I open to answer "what have I got, what changed, what is known broken." It contains:

```
## [Version] — YYYY-MM-DD

### What This Application Does

[User-facing summary — what the app does, not how it's built]
```

Unfilled placeholders throughout, including the instructional comment block explaining how to fill it in.
Combined with there being **no version tags in the repository at all**, and `CMakeLists.txt` declaring
version `0.1.0` while nothing else mentions a version, there is no answer anywhere to "which build is
this?"

This is not a cosmetic complaint. The pre-talk test script tests behaviour that changed in "this build"
versus "the last build". Those builds have no names. If I am the person receiving the second zip, I cannot
tell it apart from the first.

### Step 5 — Trying to navigate `docs/`. I got sent to the wrong product.

`docs/` contains a file called `docs/reference/user-guide.md`. Given that the root user guide is
`USER_GUIDE.md`, I opened this one expecting more detail.

It is a **1,660-line guide to a completely different product** — the "Solo Orchestrator Framework", the
development methodology used to build this app. It opens with document control tables, talks about
phase gates, git hooks, `init.sh`, insurance confirmations, and liability entities. Nothing in it is about
presenting slides.

I understand why it is there. But from where I sit, a folder called `docs/` containing a file called
`user-guide.md` that is a user guide for something else is a trap, and I fell in it.

Worse: `docs/INDEX.md` presents itself as "the doc map" and lists thirteen document locations. **It does
not list `USER_GUIDE.md`.** The one document written for the actual user is absent from the map of the
documentation. `SECURITY.md` and `PRIVACY_POLICY.md` are missing from it too.

### Step 6 — Configuration files. There are none, and that is correct.

I went looking for a settings file, a preferences pane, a config directory. There is nothing. Nothing to
configure, nothing to get wrong, no file to corrupt.

For this application that is the right call and I want to give it full credit. The flip side, stated
plainly: **you cannot change anything.** Not the five phrases, not which key pauses, not the microphone it
uses, not the sensitivity. If "next slide" does not work reliably in your accent, there is no setting to
adjust — your only recourse is the keyboard. The documentation does not say this outright, and someone
evaluating adoption should know it.

### Step 7 — The pre-talk test script. The best artifact here.

`tests/uat/2026-08-10-pre-talk-check.md` is genuinely excellent and I could run it tomorrow without
help. Specifically:

- **Ten minutes, stated up front.** I know what I am committing to.
- **Five checks in priority order**, with the most important marked and explained.
- **Do / Expect tables.** I do not have to interpret anything. Every row tells me the action and the
  correct result.
- **It explains why each check matters** — including admitting "it is a defect I introduced and an
  adversarial reviewer caught". That candour makes me *more* likely to test carefully, not less.
- **A "Known and accepted, do not re-report" section.** This is the detail that marks it as written by
  someone who has actually run tests with non-technical testers. Without it I would have filed the grey
  box as a bug and wasted everyone's time.
- **"Tell me which numbered step and what you saw."** A reporting instruction I can follow.

Two small gaps. It says to copy the zip across without saying from where (same gap as the guide). And its
check 1 — the highest-priority check, the one protecting the microphone during Q&A — **does not test the
failure mode that actually remains open.** It verifies that pausing blocks "next slide". It never checks
whether a *spoken* phrase can un-pause it, which per the project's own measurements is the residual risk
(Finding 1).

### Step 8 — `SECURITY.md` and `PRIVACY_POLICY.md`.

I will not bury this: these are very good, and better than what I see from commercial vendors.

`PRIVACY_POLICY.md` opens with the entire policy in one paragraph, then says: *"The rest of this document
exists because 'we collect nothing' is a claim, and a claim is worth what the detail behind it is worth."*
That is the right instinct and the rest of the document earns it. It explains the trade-off honestly —
because nothing is logged, **there is no record of what happened during a session**, so if a slide moved
unexpectedly there is no way to investigate afterwards. Volunteering the cost of your own design decision
is the behaviour I look for when deciding whether to trust a document.

It also commits that if the policy ever stops being true, it must change *in the same commit* as the code.
I would put that sentence in front of vendors.

`SECURITY.md` does something I have almost never seen: it **withdraws a previous claim in writing**.

> An earlier version of the changelog claimed the decoder was "incapable of emitting anything but the
> five commands". **That claim was false and has been withdrawn.**

That single paragraph did more for my confidence in this project than the rest of the security
documentation combined.

**Would a non-technical decision-maker understand these?** The privacy policy, yes, completely — I would
hand it to a manager unedited. `SECURITY.md`, partly: the opening and the closing sections are readable,
but the middle drops into "ZIP64 declared size cannot wrap negative", "iterative, not recursive",
"backoff bigram". A non-technical reader will skim those. That is defensible — a security policy has a
technical audience too — but there is no plain-language summary for the manager who needs to approve it.

---

## Findings

Ordered by how much they would affect my decision to use this.

### Finding 1 — "Pause presentation" does not stop the microphone listening, and every user-facing document says it does

**Severity: high. This is the one I would fix before anything else.**

Here is what the user guide tells me, in four places:

| Where | What it says |
|---|---|
| `USER_GUIDE.md` line 64 | `"pause presentation"` — **"stops listening** — use this for discussion" |
| `USER_GUIDE.md` line 92 | "P does exactly what 'pause presentation' does, so you never have to speak to **silence the microphone**" |
| `USER_GUIDE.md` line 110 | "Pausing **removes that entirely**." |
| `USER_GUIDE.md` line 144 | "say 'pause presentation' (or press P) during discussion, **which removes the risk entirely**" |

And the message the application puts on screen while paused reads: **"Paused — voice control is off"**.

Every one of those tells me the same thing: during Q&A, the microphone is off.

It is not. I checked, because this is the promise a presenter leans on hardest. The source that implements
the pause carries a comment stating the actual behaviour outright:

> *"Enter Paused once. We keep listening while paused so we can still hear 'continue presentation' — only
> nav is gated, not the mic."*

So while paused: the microphone is live, speech is still being decoded, and the phrase family that
un-pauses it is still armed. Navigation is blocked. That is a real and useful protection. It is not what
"stops listening" or "silence the microphone" or "voice control is off" describes.

**Why this is more than a wording quibble.** The project's own measurement file
(`tests/uat/sessions/2026-08-06-session-5/submissions/false-trigger-measurement.md`) reports that the
un-pause phrases are the **worst-performing group in the entire corpus**:

> **Worst case is the un-pause family** (phrases 22-25: "continue presenting", "consume the presentation",
> "presume the presentation", "resume presenting"), which still fires 2-3 of 3 voices. **That reaches the
> presenter during the discussion window that "pause presentation" exists to protect.**

So the documented mitigation for the known false-trigger risk is defeated by the phrase family most likely
to trigger falsely. Someone in the audience saying "consume the presentation" or "resume presenting"
un-pauses the microphone, and the on-screen message that said "voice control is off" disappears — at which
point the presenter, who has been told pausing removes the risk entirely, has no reason to look.

That residual risk is stated **accurately and prominently in exactly one file**: the internal UAT
measurement above. It is not in `USER_GUIDE.md`. It is not in `SECURITY.md`, which lists the near-miss risk
and then offers the pause as its mitigation without noting the hole. It is not in the pre-talk check. It is
not in `docs/test-results/2026-08-10_pre-launch-preparation.md`, which says "Pausing during discussion
removes it."

**What I would want as a user:**

1. Change the four sentences to describe what actually happens: *"Pausing stops the deck responding to
   voice. The microphone stays on so it can still hear you resume."*
2. Change the on-screen message from "Paused — voice control is off" to something true, e.g. *"Paused —
   slides will not respond to voice."*
3. Say plainly that the resume phrase remains live while paused, and that it is the phrase most likely to
   be misheard.
4. Tell me what to do if I need the microphone genuinely off — presumably quit, or use the OS-level
   microphone control. Right now the documentation offers no way to achieve what it claims pausing
   achieves.

To be clear about what is **not** wrong: nothing is being recorded. The privacy claims hold. My objection is
to the mental model, and the mental model matters when the deck is confidential and the room is full.

### Finding 2 — The guide tells you to "check the message on screen" when voice fails. That message is never displayed.

**Severity: high.** This is the answer to "if voice stops working mid-presentation, does the documentation
tell you what to do?" — and the answer is that it tells you to do something the software does not support.

`USER_GUIDE.md` directs me to an on-screen explanation four separate times:

| Where | What it says |
|---|---|
| Line 30 | "If you decline, the app keeps working. Voice turns off, **a message says so**" |
| Line 40 | "If step 2 fails, voice is not running — **check the message on screen**" |
| Line 169 (troubleshooting) | "**A message saying voice is off** \| Microphone denied, or the speech model is missing \| …" |
| Line 170 (troubleshooting) | "Slides do not respond to your voice \| … \| **check for a message on screen**" |

The application stores the reason voice is unavailable in a variable and exposes it through a small
accessor. **That accessor has no callers.** Nothing in the application reads it, and nothing renders it.
The reason is computed, saved, and discarded.

I did not discover this myself. Two independent reviewers found it during UAT session 5 on 2026-08-06 and
recorded it in `tests/uat/sessions/2026-08-06-session-5/agent-results/04-presenter-reality.md` and
`08-verify-presenter.md`, one of them noting: *"The reachable real-world trigger is a denied microphone
… and the presenter gets no message at all."*

Two things concern me about that:

- **It was never entered in the bug ledger.** `BUGS.md` runs to BUG-87 and contains no row for it. A defect
  found twice by reviewers, four days before the talk, did not reach the tracker.
- **`USER_GUIDE.md` was written or updated on 2026-08-10 — four days after it was found — and instructs the
  presenter to rely on the missing message four times.**

The practical consequence: the most likely voice failure is a denied or revoked microphone permission. When
that happens, the presenter gets **silence** — the deck works, the arrows work, voice does nothing, and no
explanation appears anywhere. Following the guide, they look for a message, find none, and now do not know
whether voice is broken, whether they are paused, or whether the app has frozen. The guide's own
troubleshooting table cannot be executed, because both of its voice rows depend on reading a message that
is not there.

The underlying design is sound — voice failing does not stop the talk, and that is the right property. It
is the *diagnosis* path that is missing, and the documentation promising a diagnosis path that does not
exist is worse than documentation that admits there isn't one.

**What I would want:** either display the reason, or change the guide to say *"there is currently no
on-screen indication that voice is unavailable — if slides do not respond to your voice and pressing P
produces no message, voice is not running. Use the keyboard, and check System Settings → Privacy &
Security → Microphone afterwards."*

### Finding 3 — There is no supported way to obtain or build the application

**Severity: high for adoption, zero for the author.**

Three separate paths, none of which works from documentation alone:

**Getting the built app.** `USER_GUIDE.md` step 1 and the pre-talk check both assume the zip already
exists on your desktop. No document says where it comes from. `docs/test-results/2026-08-10_pre-launch-preparation.md`
records it as "Delivered 2026-08-10" to a path on the author's own Desktop. There is no GitHub release, no
version tag, and no checksum.

**Building it yourself.** Nothing user-facing describes this. `scripts/make-test-build.sh` does the work
and is well-commented, but it *uses* Homebrew paths without ever stating what to install. To reconstruct
the prerequisites I had to infer them from the script's environment variables (`llvm`, `qt`, `libzip`,
`pugixml`), plus Ninja and CMake from the build commands. The one place a `brew install` line for this
project appears anywhere is line 201 of a UAT agent's report from five days earlier. That is not
documentation.

**The automated release pipeline does not work.** `.github/workflows/release.yml` still has `# TODO: Add
install command` and `# TODO: Add build command` in place of its build steps, with code signing as
`echo "TODO"`. The file says so honestly in a comment — *"the build, signing and notarisation steps below
are all unconfigured TODOs, and `scripts/make-test-build.sh` is what actually produces a working bundle
today."* I appreciate the honesty, but it means the only route to a working binary runs through one
person's laptop.

I want to be fair: `SECURITY.md` explicitly places code signing and notarisation out of scope as Phase 4
work, and the pre-launch document lists installers and checksums under "deliberately NOT ready, and does
not need to be". For the goal of one person presenting on one laptop, that is a legitimate call. I am
recording it because the moment a second person wants a copy, there is no path.

### Finding 4 — The fix for unsupported images cannot be run by the person being told to run it

`USER_GUIDE.md` handles the grey-box-with-a-cross problem well — it explains what it is, that everything
else on the slide is fine, and offers a fix:

```sh
bash scripts/convert-deck-emf.sh yourdeck.pptx yourdeck-png.pptx
```

Three unstated prerequisites, any of which stops me:

1. **You need the source repository.** I was told to unzip an app. I do not have a `scripts/` folder.
2. **You need LibreOffice installed**, at a specific path. The script checks and gives a good error
   message with the install command — but only after I have got far enough to run it, which per (1) I
   cannot.
3. **You need to be comfortable in a terminal**, in a guide that otherwise never assumes that.

The script itself is well-behaved — it does not modify your original deck, works entirely locally, and
tells you what it converted. My complaint is purely that the instruction is aimed at a reader who cannot
follow it.

The pre-talk check offers the better workaround — *"Use the `-png` converted deck if you would rather not
see them"* — implying someone has already done the conversion for you. `USER_GUIDE.md` should say the same
thing, plus the genuinely universal fix: re-export those images as PNG in PowerPoint itself. `BUGS.md`
row 9 already records that as the workaround; it just never reached the user guide.

### Finding 5 — No documented way to leave fullscreen, and two recovery shortcuts are dead

Once the deck is up it is fullscreen, Esc blanks the projector rather than exiting, and closing the window
is deliberately refused. So how do I get to another application mid-talk — a live demo, a browser, a video?

`USER_GUIDE.md` does not say. The answer would be `Ctrl+Shift+F`, except that shortcut **does nothing**:
the key handler recognises it and consumes the keypress, and the application then silently ignores it. The
same is true of `Ctrl+Shift+R` (re-render the deck), which is the natural recovery if a slide draws wrong.
Both are recorded as still-open issues under BUG-29. Because the keypress is swallowed rather than passed
on, pressing them produces no response at all — which under stage pressure reads as a frozen application.

`Ctrl+Shift+D` (move the deck to the other screen) **is** properly wired and was verified working, and the
guide documents it in the troubleshooting table. Credit where due.

The guide is arguably right not to document two shortcuts that do not work. But the gap it leaves is real:
there is no documented answer to "how do I temporarily get out of this to show something else," and a
presenter who tries the obvious chord gets silence. One line — even "you cannot leave fullscreen; quit with
⌘Q and reopen, which takes a few seconds" — would close it.

### Finding 6 — Repository navigation actively works against a new user

Collecting the smaller navigation problems:

- **No README** (Step 1 above).
- **`docs/reference/user-guide.md` is a guide to a different product** (Step 5).
- **`docs/INDEX.md`, which presents itself as the documentation map, omits `USER_GUIDE.md`,
  `SECURITY.md` and `PRIVACY_POLICY.md`** — all three of the documents written for users.
- **`RELEASE_NOTES.md` is an unfilled template** (Step 4).
- **Process artifacts dominate the root.** `WALK-ISSUE-LOG.md` (84 KB), `BUGS.md` (68 KB),
  `PROJECT_INTAKE.md` (37 KB), `CHANGELOG.md` (33 KB), `PROJECT_BIBLE.md` (25 KB),
  `PRODUCT_MANIFESTO.md` (25 KB), `WALK-UNBLOCK-AUDIT.md` (22 KB), `APPROVAL_LOG.md` (17 KB). Roughly
  310 KB of build-process record sitting in front of a 7 KB user guide.

Individually minor. Together they mean that finding the four documents that matter is a matter of luck.

---

## Phase 2 — Category Assessments

### 1. Documentation Quality — **3 / 5**

**Experience.** The document written for me is genuinely very good. Everything around it is not organised
for me at all.

**Pain Points.** No README, so no entry point. A second `user-guide.md` in `docs/` that documents a
different product. A documentation index that omits every user-facing document. Release notes that are an
empty template. No version number anywhere I can see it. Two factual defects (Findings 1 and 2) in the one
document I would rely on.

**What Works.** `USER_GUIDE.md` is task-ordered, plain-language, short, and has a genuinely useful
troubleshooting table. `PRIVACY_POLICY.md` is outstanding. `SECURITY.md` withdraws a prior false claim in
writing. The false-trigger rate is published in the user guide. `THIRD_PARTY_NOTICES.md` is thorough and
explains the licence obligations in readable prose.

**What is Missing.** A README. A filled-in `RELEASE_NOTES.md` with a version. A note on where the app comes
from. Build instructions. A plain-language summary at the top of `SECURITY.md` for a manager. Correction of
Findings 1 and 2.

### 2. Setup and Installation — **2 / 5**

**Experience.** Once someone hands you the zip, installation is about five minutes and the guide covers it
adequately. Getting the zip is undocumented, and building it yourself is undocumented.

**Pain Points.** No source for the artifact. No checksum for a binary I am being asked to let past
Gatekeeper. No build instructions. The actual Gatekeeper dialog is not described. macOS 26 + Apple Silicon
requirement stated third rather than first.

**What Works.** The right-click → Open instruction is present, correct, and explains it is a one-time step.
The `xattr` fallback is provided. The microphone permission step is handled well, including what happens if
you decline. `make-test-build.sh` fails loudly rather than producing a bundle that only works on the build
machine — a good property, even though I cannot run it without knowing the prerequisites.

**What is Missing.** A download location. A checksum. A prerequisites list (`brew install qt libzip pugixml
llvm ninja cmake`, near as I can determine). A screenshot or description of the Gatekeeper dialog. One
sentence connecting "unsigned app" to the reassurance in `SECURITY.md`.

### 3. Day-to-Day Workflow — **4 / 5**

**Experience.** Clear. Open a deck three ways, five phrases, a keyboard table, Esc to blank, ⌘Q to quit. The
model is small enough to hold in your head, which is exactly right for something you operate while talking
to a room.

**Pain Points.** "How do I know it is working correctly?" is where it weakens. The 60-second check
(guide section 3) is a good answer *before* the talk. During the talk, the honest answer is "the slide
moved." And when it does not move, Finding 2 means the diagnostic the guide points at does not exist.

**What Works.** The 60-second pre-flight check is the right idea, well-scoped, and includes the crucial
negative test (say a non-command sentence, confirm nothing happens). The troubleshooting table maps
symptoms to actions. "The keyboard is the fallback for everything" is repeated often enough to actually
stick.

**What is Missing.** Any persistent indicator of whether voice is currently live. There is a sticky message
when paused (good), but nothing when voice failed to start (Finding 2). A one-line "if it all goes wrong"
card — the pre-launch document has an excellent version of this table that never made it into the user
guide.

### 4. Configuration Complexity — **4 / 5**

**Experience.** Zero configuration files. Nothing to understand, nothing to break.

**Pain Points.** Zero configurability, too. Cannot change the phrases, the pause key, the microphone, or
anything else. If a command does not work in your accent, the keyboard is your only option. The
documentation never states this.

**What Works.** For this product, no configuration is the correct design, and it eliminates an entire class
of support problems.

**What is Missing.** One line in the guide saying there are no settings and what that means in practice.

### 5. Learning Curve — **5 / 5**

**Experience.** Five phrases and an arrow key. I was confident about operating it after eight minutes of
reading, and the pre-talk script would make me confident in practice after ten more.

**What Works.** The concept maps onto something everyone already knows — clicker, arrow keys. There is
nothing conceptually new to absorb. The two-word rule is explained rather than asserted.

**What is Missing.** Nothing meaningful. This is the project's strongest category, and it is a real
achievement — the simplicity is designed, not accidental.

### 6. Error Handling and Recovery — **3 / 5**

**Experience.** Recovery from the *big* failure is excellent: whatever breaks, the arrows work. Recovery
from the *small* failures is weak, because there is deliberately nothing to look at.

**Pain Points.** Finding 2 is the core of it — the documented diagnostic for the most likely failure does
not exist. Beyond that, error messages are drawn from a deliberately closed vocabulary, so "Could not open
the deck" is all you ever get, with no indication of *why*. And by design **nothing is logged at all**, so
after something goes wrong there is nothing to inspect and nothing to send to the maintainer. If I report a
problem, I can offer only my own recollection.

`PRIVACY_POLICY.md` states this trade-off explicitly and defends it, which I respect. It is still the case
that a user with a reproducible problem has no way to produce evidence of it.

**What Works.** The design guarantee that voice failure cannot stop the presentation is real, structural,
and pinned by tests. The two-step quit means a stray Esc cannot end your talk. Closing the window asks
first. These are thoughtful protections against the specific ways a presentation goes wrong.

**What is Missing.** Any indication of voice state. A "how to report a problem when there are no logs"
section. An explicit statement that no logs exist, in the user guide rather than only in the privacy
policy.

### 7. Personal Project Viability — **4 / 5**

**Experience.** For one person on one Mac, this works and adds real value. Hands free while presenting is
genuinely useful, and the offline guarantee means you can use it with a confidential deck without a
conversation with anyone.

**Pain Points.** You must either be given a build or be able to reconstruct the build yourself. The
false-trigger rate is real and you need to work around it deliberately.

**What Works.** No accounts, no subscription, no network, no telemetry, MIT licensed. Fast to learn.
Keyboard fallback means the downside of it failing is close to zero.

**Would I recommend it to a friend with my background?** For presenting — yes, with the two corrections in
Findings 1 and 2, and provided they get a working build. As something they could pick up and install
themselves from the repository — not today.

### 8. Enterprise/Team Viability — **2 / 5**

**Experience.** I could get this through a security review. I could not get it through a deployment review.

**Pain Points.** Unsigned and un-notarised, so every user must be walked through bypassing Gatekeeper —
which contradicts the security guidance I give my own staff. No installer, no update mechanism, no version
numbers, no checksums. macOS 26 on Apple Silicon only, which excludes most fleets. No logs, so no
supportability. Single maintainer, explicitly stated.

**What Works.** This is where `SECURITY.md` and `PRIVACY_POLICY.md` pay off. "No network code, nothing
written to disk, nothing recorded, no telemetry, no accounts" answers most of what a security team asks
before they ask it. An SBOM (`sbom.json`) exists with pinned hashes. Third-party licences are documented,
staged into the bundle, and the build fails if they are missing — that closes a compliance gap most small
projects never even notice. The known risks are written down and attested at a dated gate.

**Could I explain this to my manager and get approval?** For one person presenting at one event — yes,
easily, and `PRIVACY_POLICY.md` would do most of the work. For departmental rollout — no, and I would not
try.

### 9. Honesty and Expectation Setting — **3 / 5**

This is the category with the widest spread, so I want to be precise.

**Where it is exceptionally honest.** Publishing "60 of 102" in a *user guide* is rare and admirable.
Withdrawing a false claim in `SECURITY.md` in writing is rarer. Admitting in the pre-talk script that the
top-priority check exists because of a defect the author introduced. Stating in `PRIVACY_POLICY.md` that
the no-logging decision has a cost. Recording in the release workflow that its own steps do not work.
`FEATURES.md` ending a section with "**Unverified:** no live-audio test exists or can exist on this
machine." I do not often see this much candour.

**Where it fails.** The pause claim (Finding 1) is stated incorrectly in four places, on screen, and in the
pre-launch document — and it is the claim on which the project's own primary mitigation rests. The
"a message says so" claim (Finding 2) is stated four times for a message that is not displayed, four days
after two reviewers reported it.

So: the *culture* here is honest, and I believe the errors are drift rather than spin — the accurate
statement of the pause risk exists, it is just buried in an internal test file instead of the user guide.
But from where I sit, honesty is measured by the documents I am actually given, and those two claims are
wrong in the two places that matter most.

**On the specific question of whether the near-miss risk is communicated well enough to decide with:**
partly. The *number* is communicated excellently — better than anyone else does it. The *consequence* is
not, because the mitigation offered alongside it is overstated. A reader of `USER_GUIDE.md` concludes:
"there is a small risk, and pausing eliminates it." The correct conclusion is: "there is a small risk;
pausing greatly reduces it but the resume phrase stays live and is the phrase most likely to be misheard."
Those lead to different decisions about whether to present a confidential deck with the microphone open
during Q&A.

### 10. Comparison to Alternatives — **3 / 5**

**The obvious alternative is a £20 presentation clicker.** It is more reliable, works on any OS, needs no
permissions, and cannot be triggered by an audience member. For pure slide advancement it wins outright.

**What this offers that a clicker does not:** genuinely nothing in your hands, "go to slide five" as a
direct jump rather than clicking through, and a privacy story that a clicker does not need but that this
project makes a feature of.

**What PowerPoint itself already offers:** presenter view, and its own rendering — which matters, because
this app does not draw tables, charts, SmartArt, animations, transitions, or EMF/WMF images. That is a
significant fidelity gap. For a text-and-photos deck it is fine; for a typical corporate deck full of
charts and tables, PowerPoint plus a clicker is better.

**Is the complexity justified?** For the author's use case — a confidential deck, offline, hands free —
yes. As a general-purpose replacement for PowerPoint plus a clicker — not yet, mainly because of the
rendering limitations rather than the voice control.

**Where it would pay off:** presenting a text-heavy confidential deck where you want your hands free and
you cannot put the file on a network. That is a narrow but real niche, and the documentation is honest
about the fidelity limits.

### 11. Installation Experience — **2 / 5**

**Experience.** No installer. Unzip, drag, right-click → Open, grant microphone access.

**Pain Points.** A security warning on every fresh machine, with no explanation of why it is safe. No
checksum. No prerequisite handling (there are none needed at runtime, which is good — the bundle is
self-contained). Nothing to tell you which version you installed.

**What Works.** The bundle genuinely is self-contained — the build script fails rather than shipping one
that depends on Homebrew, which is exactly the failure mode that bites people. It works immediately after
install with no configuration. Uninstalling is deleting one file, and since nothing is written to disk,
nothing is left behind. That is cleaner than most commercial software manages.

**Is it signed and trusted by the OS?** No, and the documentation says so plainly rather than hiding it.

### 12. Building and Customization — **1 / 5**

**Experience.** I could not do either from the documentation.

**Pain Points.** No documented build process. Prerequisites must be inferred from a shell script. The
automated release pipeline is unconfigured TODOs. No settings of any kind, so no customisation without
rebuilding — which returns to the first problem.

**What Works.** `make-test-build.sh` is well-commented and verifies its own output aggressively. If someone
wrote down its prerequisites in a README, this category would jump several points immediately.

**What is Missing.** A README section: prerequisites, one build command, one run command. That is all it
would take.

---

## Time Investment Estimate

Realistic hours for someone with my background.

| Task | Time | Notes |
|---|---|---|
| **Read the documentation I actually need** | **25 min** | `USER_GUIDE.md` 8 min, `PRIVACY_POLICY.md` 5 min, `SECURITY.md` 10 min. Add 15-20 min of wasted time finding them, given no README. |
| **Install from a supplied zip** | **10 min** | Unzip, Gatekeeper, first launch, microphone permission. Add 10-15 min if the Gatekeeper flow differs from the guide's description. |
| **Build from source, with no documentation** | **1.5-3 hrs** | Inferring prerequisites, installing Qt via Homebrew (slow), first CMake configure, resolving the version issues the CMakeLists comments hint at. |
| **Build from source, if prerequisites were documented** | **20-30 min** | Mostly Homebrew download time. |
| **First real task — open your deck and run the 60-second check** | **15 min** | The guide's own check, done properly, with your real deck. |
| **Run the full pre-talk script** | **10-15 min** | As advertised. Accurate estimate. |
| **Become genuinely comfortable presenting with it** | **1-1.5 hrs** | One full rehearsal with the projector, deliberately practising the pause-for-questions flow and recovering from a false trigger. |
| **Total, given a working build** | **~2.5 hrs** | Reasonable. |
| **Total, if you must build it yourself** | **4-6 hrs** | Most of it spent on undocumented build setup. |

---

## Prerequisites Checklist

Everything you need before starting. **Bold items are not stated in any user-facing document.**

### To run the application

- [ ] A Mac running **macOS 26 or newer** (stated in the guide, though third rather than first)
- [ ] **Apple Silicon** — Intel Macs are excluded (stated)
- [ ] **A copy of `powerpoint-voice-test-build.zip`, obtained directly from the author** — there is no
      download location, release page, or version tag anywhere
- [ ] Administrator rights, or at least the ability to bypass Gatekeeper on your machine
- [ ] Willingness to bypass a macOS security warning for an unsigned application, and — if you are in a
      managed environment — **permission from whoever owns that policy**
- [ ] A microphone (the built-in one is fine) and the ability to grant microphone permission
- [ ] A `.pptx` deck that is **mostly text and photographs** — tables, charts, SmartArt, animations,
      transitions, and EMF/WMF images will not render as content
- [ ] **An external display or projector for a realistic rehearsal** — the multi-screen behaviour cannot
      be sensibly tested on a laptop alone

### To use the EMF image workaround

- [ ] **A clone of the source repository** (the guide's command assumes `scripts/` is present)
- [ ] **LibreOffice**, installed at `/Applications/LibreOffice.app` (`brew install --cask libreoffice`)
- [ ] **Terminal comfort** — the guide otherwise never assumes this

### To build from source

**None of this is documented anywhere. This list is my reconstruction from reading the build script and
`CMakeLists.txt`, and I cannot guarantee it is complete.**

- [ ] **Xcode command line tools**
- [ ] **Homebrew**
- [ ] **`brew install qt libzip pugixml llvm ninja cmake`** — Qt 6.2+ required, Qt 6.8+ needed for the
      screen-reader announcements to work at all
- [ ] **CMake 3.24+**, with an awareness that CMake 4.x needs a policy workaround the file handles itself
- [ ] **An internet connection for the first build** — the test framework is fetched during configure
- [ ] **Familiarity with `cmake` / `ninja` output**, enough to tell a missing dependency from a real error

### Concepts you need to already understand

- [ ] What Gatekeeper is and why macOS blocks unsigned applications
- [ ] How to grant and revoke microphone permission in System Settings
- [ ] **That "paused" does not mean the microphone is off** (see Finding 1 — you will not learn this from
      the documentation)
- [ ] **That there are no logs**, so a problem you cannot reproduce cannot be investigated

---

## What I Wish Existed

In the order I would want them.

1. **A README.** One page: what it is, who it is for, where to get it, how to install it, what it cannot
   do, and links to the four documents that matter. This single file addresses more of my complaints than
   anything else on this list.

2. **Corrected pause wording**, in the guide, in `SECURITY.md`, in the pre-launch document, and in the
   on-screen message. Plus one sentence in *Known limits* saying the resume phrase stays live while paused
   and is the phrase most likely to be misheard.

3. **Either display the voice-unavailable reason, or stop telling me to look for it.** Four instructions in
   the guide currently point at a message that does not render.

4. **A real `RELEASE_NOTES.md` with a version number**, and a version visible somewhere in the app. Right
   now I cannot tell one build from another.

5. **A "how to get it" section** — a release with the zip, a checksum, and the minimum OS stated first.

6. **Build instructions.** Six lines in the README. The information already exists inside
   `make-test-build.sh`; it just needs writing down.

7. **A one-page printable "day of the talk" card.** The excellent failure table in
   `docs/test-results/2026-08-10_pre-launch-preparation.md` is exactly this and is buried in a test-results
   folder. Move it into the user guide or ship it as a separate page.

8. **A Gatekeeper section that explains itself** — what dialog you will see (including the newer System
   Settings flow), what "ad-hoc signed" means in one sentence, and why this specific application is safe to
   allow, linking to the privacy policy.

9. **Rename `docs/reference/user-guide.md`** to something that identifies the product it documents, and add
   the three user-facing documents to `docs/INDEX.md`.

10. **A short plain-language summary at the top of `SECURITY.md`** for the manager who has to approve this
    and will not read past "backoff bigram".

11. **A "reporting a problem when there are no logs" section** — what to write down, what to include,
    given that no evidence can be produced after the fact.

12. **Move the process artifacts out of the root** into a `process/` folder. 310 KB of build history in
    front of a 7 KB user guide sends the wrong signal about who the project is for.

---

## Honest Recommendation

### Who should use this

- **The author, for the talk it was built for.** It has been tested end-to-end on the exact hardware, the
  keyboard fallback is real, and the known risks are understood. Ready.
- **Someone technically confident who is handed a build and briefed in person**, on a text-and-photos deck,
  who understands what pausing really does. It will work and it is pleasant to use.
- **Anyone who needs a presentation tool that provably touches no network.** This is the strongest case for
  it. The privacy and security documentation is better than most commercial vendors produce, and the
  no-network design is structural rather than promised.

### Who should not

- **Anyone who cannot be handed a build.** There is no route from this repository to a working application
  without either the author's laptop or several hours of undocumented build work.
- **Anyone presenting a chart-heavy or table-heavy corporate deck.** Those render as labelled placeholder
  boxes. That is a fidelity gap, not a bug, and the documentation says so — but it rules out most business
  decks.
- **Anyone who needs a genuinely silent microphone during Q&A.** Until Finding 1 is addressed, the
  documentation promises something the software does not do, and there is no documented way to achieve it.
- **Anyone deploying to a fleet.** Unsigned, un-versioned, un-installable, un-updatable, macOS 26 + Apple
  Silicon only.
- **Anyone who needs to investigate problems after the fact.** Nothing is logged, by design. This is a
  deliberate and defensible trade, and it means you get one shot at observing any problem you have.

### Alternatives

- **A £20 Bluetooth clicker** — more reliable, cross-platform, cannot be triggered by the audience. For
  pure slide advancement it beats this outright.
- **PowerPoint or Keynote with presenter view** — full rendering fidelity, animations, transitions,
  speaker notes. The reason to choose this project over them is voice control and the offline guarantee,
  not display quality.
- **macOS Voice Control** (built in) — can drive any application by voice, no installation, Apple-signed.
  Less precise for this task and not offline in the same provable way, but it is already on the machine and
  requires no Gatekeeper bypass.

### Overall Usability Rating: **3 / 5** — *usable with significant effort*

**Justification.**

The application itself is likely a 4. Five commands, an always-available keyboard, no configuration, a
five-minute learning curve, and a privacy design that is genuinely a feature rather than a checkbox. The
`USER_GUIDE.md` in isolation is a 5 — it is short, task-ordered, plain-spoken, and it publishes its own
error rate, which is rarer than it should be. `PRIVACY_POLICY.md` is the best document in the repository
and I would hand it to a manager unedited.

Three things hold it to a 3.

**You cannot get the software from the documentation.** No download, no build instructions, no version. A
tool you cannot obtain is not usable at any rating, however good it is once running.

**Two documented promises are not true**, and both are in the failure paths where a presenter has no
slack. "Pause stops listening" is wrong, and it is the mitigation the project's own risk disclosure rests
on. "A message says so when voice is off" is wrong, and it is the only diagnostic instruction the guide
offers for the most likely failure. I found both by checking three specific claims — I was not auditing the
code — which suggests the documentation has not been checked against the software recently.

**Finding what matters is a matter of luck.** No README, a doc index that omits every user-facing document,
a second "user guide" for a different product, and 310 KB of process history sitting in front of a 7 KB
user guide.

None of this is deep. Every item on my "What I Wish Existed" list is a documentation task, most of them an
afternoon's work, and the hardest technical item — displaying a message that is already computed — is
small. The engineering discipline visible in this project is well ahead of its packaging, and the culture
is plainly an honest one: the accurate statement of the pause risk *exists*, in an internal test file. It
just never made it into the document the presenter reads.

Fix the two accuracy defects and write a README, and I would move this to a 4 without hesitation. Add a
release with a version and a checksum, and I would recommend it to colleagues.

---

*Reviewed as a technically literate non-programmer evaluating adoption, installation, daily operation, and
troubleshooting. No project files were modified other than the creation of this review. The Confidential
deck referenced outside this repository was not opened.*
