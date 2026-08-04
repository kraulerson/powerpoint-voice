# Agent Result — Malicious/Chaotic User (UAT Session 2)

**Persona:** malicious/chaotic user. **Bottom line:** could NOT force a false command or a crash
(the safety property is solid); the real risk is the opposite — natural commands that return
nothing, one on the resume path.

## Confirmed SAFE (re-verified adversarially)
- **No false command** from non-navigation speech: phrase-level exact match; embedded keywords,
  homoglyphs (`ｎｅｘｔ ｓｌｉｄｅ`, Cyrillic), bidi/RTL, zero-width, combining accents, Turkish-İ all
  fail SAFE to (no command). `go to slide five, then talk` → (no command).
- **No crash/hang/DoS:** ~23MB across a few lines in 1.82s/280MB; 100k-digit numerals, 5000-token
  floods, 200k-char floods, NUL/control chars → clean exit 0. Token floods bounded by the 12-cap;
  edge-strip regex anchored/linear.

## Findings (missed-command / number — all fail-safe, no wrong-from-ambient jumps)
- **[agent SEV-1] Stuck-in-Paused escape hatch.** Once Paused, ONLY the literal
  `continue presentation` resumes. `resume`, `continue`, `unpause`, `continue the presentation`,
  `continue presentation please`, `okay lets continue` → all dropped, deck stays frozen. The
  missed-command gap sits on the RECOVERY path.
- **[SEV-2] Common nav phrasings miss** (strict equality on the 4 fixed commands): `next slide
  please`, `okay/ok/alright/so/and/um/right next slide`, `go to (the) next slide`, `move to the
  next slide`, `next slide now/thanks/everyone`, `pause the presentation`, `lets pause`,
  `resume presentation`, `continue the presentation` → (no command).
- **[SEV-2/3] "please" asymmetry:** `go to slide five please` → GoToSlide(5) works, but
  `next slide please` / `pause presentation please` → (no command). Inconsistent; worsens the
  resume gap.
- **[SEV-3] Natural numbers miss:** ordinals (`twenty-fifth`, `the fifth slide`), `one oh five`
  (oh=zero), `last` → (no command).
- **[SEV-3] Digit concatenation:** `go to slide five and six` / `five six` → GoToSlide(56);
  `one and one` → GoToSlide(11). Needs the literal prefix (not ambient), so a wrong-NUMBER jump.
- **[SEV-3] Out-of-range / inconsistent bounds:** `go to slide zero`/`0` → GoToSlide(0);
  `go to slide -5` → GoToSlide(5) (minus dropped); digit path bypasses the word path's
  kMax=100000 (`go to slide 100001` → GoToSlide(100001)). All lean on F7 to clamp (per the F4
  caller-range-checks contract).

## Prioritized fix recommendation (agent's)
1. Tolerate trailing politeness/filler + add resume/pause synonyms on the 4 fixed commands
   (fixes the SEV-1 resume gap + the common misses + the "please" asymmetry together).
2. Handle "go to the next/previous slide", ordinals, "oh"=zero for numbers.
3. Confirm F7 clamps slide 0 / negative / over-length before ship.
