#pragma once

#include <QWidget>

class CaptureController;
class QLabel;
class TagField;

// The footer under a note in the capture popup and the board editor: the manual tag field,
// the inline #tags (read-only), and the due date / colour. A valid edit of the tag field goes
// to the controller; an invalid one shows "Invalid tag: x" and saves nothing.
class NoteFooterBar : public QWidget {
    Q_OBJECT
public:
    NoteFooterBar(CaptureController& controller, QWidget* parent = nullptr);

    TagField* tagField() const { return tags_; }
    void focusTags();
    QString infoText() const;   // due date and colour
    QString inlineText() const; // the note's inline tags
    QString errorText() const;
    void setReadOnly(bool readOnly);

private:
    void onTagsEdited();
    void refreshTags();
    void refreshInfo();

    CaptureController& controller_;
    TagField* tags_;
    QLabel* inline_;
    QLabel* error_;
    QLabel* info_;
};
