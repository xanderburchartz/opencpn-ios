# TestFlight feedback to GitHub issues

`sync.mjs` reads the TestFlight feedback of the app from the
[App Store Connect API](https://developer.apple.com/documentation/appstoreconnectapi)
(screenshot feedback and crash feedback) and opens one issue per submission, with the label
`testflight-feedback`. The workflow [`.github/workflows/testflight-feedback.yml`](../../.github/workflows/testflight-feedback.yml)
runs it once an hour.

What goes into an issue: the tester's comment, the build number, the device model, the OS
version and the date. What does not: the tester's name, email address, tester ID, locale and
time zone (these are not even requested from Apple), the screenshots (they can show a
position) and the full crash log. For a crash, the issue has the exception type, the
termination reason and the top eight frames of the crashed thread as Apple delivers them;
frames in the app itself are only symbolicated if Apple has the debug symbols.

Each issue ends with a hidden marker `<!-- tf-feedback-id: ... -->`. The script reads all
issues with the label before posting and skips submissions that already have one, so a run
can be repeated safely. At most 20 issues are posted per run (`--max`). Feedback sent before
testers were told that it becomes public (`START` in `sync.mjs`, 27 November 2026) is never
posted; `--since` can only move that moment later.

No dependencies; Node 20 or newer.

    node sync.mjs --dry-run --fixture fixtures     # sample data, prints the issues
    node sync.mjs --dry-run                         # real API, posts nothing
    node sync.mjs [--max N] [--since YYYY-MM-DD]    # posts
    node test.mjs                                   # tests against a fake API

Environment: `ASC_KEY_ID`, `ASC_ISSUER_ID`, `ASC_PRIVATE_KEY` (contents of the `.p8` key),
`ASC_APP_ID`, `GITHUB_TOKEN`, `GITHUB_REPOSITORY`. In the workflow the first three come from
repository secrets.
