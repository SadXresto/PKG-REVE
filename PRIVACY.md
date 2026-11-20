# PKG Studio privacy policy

Last updated: 20 November 2026. Applies to PKG Studio 0.7.0 and later.

PKG Studio is a local desktop application. It has no account system, no
analytics, no crash-reporting service and no advertising. The authors do not
receive your files, your usage or your settings.

## What is stored on your computer

Everything below lives in the application data folder
(`%APPDATA%\PKG Studio`, `~/.local/share/PKG Studio`, or the folder named by the
`PKGSTUDIO_DATA_DIR` environment variable):

| Data | Purpose | Leaves the computer? |
| --- | --- | --- |
| `pkgstudio.db` (SQLite) | Settings, recent files, session, parser cache | No |
| `logs/` | Diagnostics and crash traces | No |
| `themes/` | Themes you save, including copied background images | No |
| `ai.key` | The API key for your AI endpoint, owner-read/write only | Only in requests to your endpoint |
| Backups next to edited files | `*.pkgstudio-backup-<timestamp>.bak` written before saving | No |

Session restore records the paths of open files. The analysis cache stores
parser results keyed by path and file size/timestamp, not file contents. Crash
traces contain no file contents.

## The AI assistant is opt-in and endpoint-directed

The AI assistant is disabled unless you enable it. When you send a prompt:

- the request goes **only** to the OpenAI-compatible endpoint you configure
  (by default `http://127.0.0.1:11434/v1`, a model running on your own machine);
- the request contains your prompt, the conversation so far, and — if
  "include the open file's readable listing" is enabled — the first 8 KiB of the
  open file's listing (offsets, hex bytes and ASCII);
- if you set an API key, it is sent in the `Authorization` header to that
  endpoint and stored locally in `ai.key`, readable by your user account. It is
  not encrypted, so protect your account and the file.

Use a loopback endpoint (`localhost`, `127.0.0.1`, `::1`) to keep everything on
your computer. If you point the assistant at a remote service, that service's
own privacy policy and retention rules apply to what you send. The authors of
PKG Studio never see those requests and cannot delete them for you.

## What is never collected

No telemetry, no identifiers, no usage statistics, no file uploads, no
automatic update pings, no third-party SDKs that phone home. There is no
advertising and nothing is sold or shared.

## Your control

- Delete the application data folder to remove settings, logs, themes and the
  stored AI key.
- "Forget key" in the AI assistant removes `ai.key` immediately.
- Nothing else needs an account or a server to be deleted.

## Children

PKG Studio is a developer tool and is not directed at children.

## Changes

Material changes to this policy are noted in `CHANGELOG.md` and in the help
topic *Help → Privacy policy*. The policy always describes the build you are
running.

## Contact

Privacy questions go to the project maintainer through the channel this copy
was obtained from.
