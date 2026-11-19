#pragma once

#include <QString>

class QWidget;

namespace pkg::ui {

/// Shows `dialog` without blocking, waits for it to lay out and saves a PNG
/// grab to `path`. Returns false and fills `error` when the dialog never
/// becomes visible, renders as a blank or single-colour image, or the file
/// cannot be written.
///
/// This exists for the --screenshot-dialog diagnostic mode and for tests, so a
/// build can prove that the About and Feature status dialogs actually render
/// instead of only checking that their objects were constructed.
[[nodiscard]] bool captureDialog(QWidget* dialog, const QString& path,
                                 QString* error = nullptr);

}  // namespace pkg::ui
