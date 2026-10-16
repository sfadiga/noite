# noite — original requirements

> These are the author's original requirements (2026-10-15), edited only for spelling and grammar. The numbering is unchanged, so the traceability table in the [spec](spec.md) still matches. The [spec](spec.md) records the design decisions that answer them.

**Quick description:** a note-taking app for busy people who get shit done.

1. The app shall work on Windows (and, if possible, on Linux and macOS).
2. On first run, the app shall open a step-by-step configuration.
3. The first configuration step shall define the key combination that opens a new scratch pad.
4. When the user presses the key combination defined during setup, the app shall open a modal square area where the user can write freely.
5. Each scratch shall get metadata from its context: date/time, a title (based on the text), a color, and more (TBD).
6. At the beginning of the day, the app shall show the scratches ordered from oldest to newest, as a reminder of which ones are due.
7. The app shall allow pasting images and other objects from the clipboard.
8. The app shall persist its data.
9. The app shall work without relying on the internet or on online APIs.
10. The app shall be blazing fast (yes, I know that's a vague requirement).
11. The app is meant for capturing to-dos and other small notes.
12. The main board shall allow bundling scratches together.
13. The app shall be portable.
14. The app shall allow marking items as done.
15. The app shall allow trashing scratches.
16. Trashed scratches shall be recoverable for up to one month if needed. The recovery window shall be a setting in the step-by-step setup.
17. Although requirement 9 says the app must not need the internet or online APIs, the app shall be able to back up automatically to Google Drive. The backup location is also set in the step-by-step setup.
18. Items marked as done shall not be shown on the board, but a Completed area shall show them.
19. The app shall allow recording audio and transcribing it to text. (I said no online APIs, but if this feature needs one, that's acceptable as long as it's free and not intrusive; for example, it must not store user data.)
20. The scratch pad shall support markdown and rich-text features, such as links.
21. The app shall allow cloning scratches and merging them into other scratches.
22. The app shall allow splitting a scratch by selecting text or objects in it.
23. The app shall allow adding metadata for quick filtering on the board.
24. The board shall allow simple filtering (selecting chips based on metadata).
