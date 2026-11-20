# PKG Studio terms of use

Last updated: 20 November 2026. These terms supplement the proprietary
licence in [LICENSE](LICENSE). By installing or running PKG Studio you accept
both.

## 1. Authorized use only

PKG Studio is for files you own or are legally allowed to inspect, list and
modify: your own builds, homebrew packages, public test files, and content you
have explicit permission to work on.

You must not use it to:

- bypass DRM, console security, licence checks, signing or encryption;
- extract, modify, distribute or play copyrighted content you do not have
  rights to;
- break the law of your country, or the terms of a service you use.

Whether disassembly or modification of a package is lawful depends on your
jurisdiction and on the licence of that package. That determination is yours,
not the authors'. If you are unsure, get legal advice before you save a change.

## 2. Package formats and honest limits

The readable listing (the **Code** tab) disassembles *bytes* into an editable
text listing and assembles edited text back into the same bytes. It works on
any file, including PS4/PSP/PS Vita PKG headers and Android APK/ZIP containers.

It is not a CPU disassembler: it does not decode machine instructions, it does
not decrypt or decompress payloads, and it cannot rebuild a signed package.
Byte edits keep the file length; insertion and deletion are not supported.
PS5 PKG parsing is not implemented. Anything the application says it cannot do
is genuinely not done.

## 3. AI assistant

The assistant is an optional client for an OpenAI-compatible endpoint that you
choose and configure. It is disabled until you enable it.

- You are responsible for the endpoint, the model, its licence, its cost and
  its privacy practices. Use a local endpoint if you need the file to stay on
  your machine.
- Replies can be wrong. Byte edits from a model are proposals: you review them,
  apply them and save them. Do not rely on the assistant for security decisions
  or on files you cannot restore from a backup.
- Do not paste secrets, personal data or third-party material you have no right
  to share into a remote endpoint.

## 4. Your changes are yours

PKG Studio writes only what you tell it to write. Saving a modified package can
make a console reject it, void a warranty, or breach a licence or law. Back up
before saving; the application writes a timestamped backup next to the original
but that is not a substitute for your own copy.

## 5. Copies and piracy

Distribution of PKG Studio or of modified builds without written permission is
prohibited. Such copies are pirated copies; identified cases may be reported to
hosting providers, platforms or authorities and pursued as described in the
licence. Do not upload the installer to file-sharing sites or bundle it with
other products. Report suspected piracy to the project maintainer.

## 6. No warranty, limited liability

This is a development build provided "as is", without warranty of any kind. To
the maximum extent permitted by law the authors are not liable for data loss,
corrupted files, damaged devices, lost profits or any indirect damages arising
from use of the software. See sections 6 and 7 of [LICENSE](LICENSE).

## 7. Third-party components and content

Qt and other third-party libraries keep their own licences. PKG Studio includes
no game, application or console content, and grants no rights to any.

## 8. Changes

These terms may be updated with a new release; the version in this file applies
to the build you are running. Continuing to use a new build means accepting the
updated terms.

## 9. Contact

Licence, permission and piracy reports go to the project maintainer through the
channel this copy was obtained from.
