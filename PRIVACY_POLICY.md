# Privacy Policy — powerpoint-voice

**Effective:** 2026-08-10

## The whole policy, in one paragraph

**powerpoint-voice collects, stores and transmits no data whatsoever.** It has no accounts, no
telemetry, no analytics, no crash reporting, no update check and no network code of any kind.
Everything it does happens on your computer, and nothing leaves it. There is no server to send
anything to, because there is no server.

The rest of this document exists because "we collect nothing" is a claim, and a claim is worth
what the detail behind it is worth.

---

## Your presentation

Your `.pptx` file is **opened read-only**. It is never modified, never copied, never cached to
disk, and never transmitted. When you close the application, nothing about your deck remains
anywhere except the file you already had.

The application does not record **which** deck you opened. It computes a short content hash
internally to distinguish one deck from another during a single run, and that hash is never
written anywhere and does not survive the process.

## Your voice

Speech recognition runs **entirely on your machine**, using a speech model bundled inside the
application. No audio is sent anywhere. No audio is recorded to disk. No transcript is
produced, stored, displayed or logged.

Recognised text passes from the speech decoder directly into the command matcher and nowhere
else. It is not held in a variable that outlives the call, it is not written to any log, and it
is never drawn on screen. This is enforced deliberately and is checked at every security audit,
because the microphone is live in a room full of people who did not consent to being recorded —
and they are not being recorded.

The speech engine's own diagnostic logging, which would print heard words to standard error, is
**switched off before the model is loaded**.

## What is written to disk while the application runs

**Nothing.** No cache, no log, no preferences file, no recent-files list, no temporary files,
no crash dumps written by us. The speech model is unpacked into the application bundle when the
application is *built*, never at run time.

This is a deliberate design constraint rather than an omission, and it has a cost worth being
honest about: **because nothing is logged, there is no record of what happened during a
session.** If a slide changed unexpectedly, there is no audit trail to consult afterwards. That
trade was made knowingly in favour of confidentiality.

## Microphone permission

macOS asks for microphone access the first time you open a deck. The consent string you are
shown says exactly what this document says.

**If you decline, the application keeps working.** Voice control switches off, a message says
so, and the keyboard controls everything. Nothing is lost except the voice commands.

## Network

There is no network code in this application. No HTTP client, no listening socket, no update
check, no license check, no telemetry endpoint. Dragging a file onto the window explicitly
**refuses remote URLs** so that a dropped file cannot become the first network path by
accident.

You can verify this: the application runs correctly with networking disabled entirely.

## Children

The application is not directed at children and collects nothing from anyone, of any age.

## Third parties

The application bundles third-party components (Qt, Vosk, libzip, pugixml, miniaudio) listed in
`THIRD_PARTY_NOTICES.md`. **None of them is a service**, none is contacted over a network, and
none receives any data. They are libraries compiled into or shipped alongside the application.

## Your rights

Because no personal data is collected, stored or processed, there is nothing to access,
correct, export or erase, and no data-subject request this application could meaningfully
answer. GDPR, CCPA and equivalent regimes govern the processing of personal data; there is no
processing activity here to govern.

## Terms of Service

There are none, deliberately. A Terms of Service governs the use of a *service*, and this is a
program you run on your own computer with no service behind it. Your rights to use, copy and
modify the software are set by its licence (MIT — see `LICENSE`), not by terms of service.

## Changes

If this ever stops being true — if any version collects, stores or transmits anything — this
document must change **in the same commit** as the code that changes it. A privacy policy that
lags the code is worse than none, because it is believed.

## Contact

<https://github.com/kraulerson/powerpoint-voice/issues>
