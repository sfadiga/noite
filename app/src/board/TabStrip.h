#pragma once

#include <noite/model.h>

#include <QAbstractButton>
#include <QList>
#include <QWidget>

class QButtonGroup;

// One board tab: an all-caps label with the tab's note count under it.
class TabButton : public QAbstractButton {
    Q_OBJECT
public:
    TabButton(const QString& label, QWidget* parent = nullptr);

    QString label() const { return label_; }
    QString countText() const { return count_; }
    void setCount(int count);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString label_;
    QString count_;
};

// The board's tab row (D17): Today · All · Completed · Deferred · Trash as equal-width buttons, each
// showing how many notes it holds under the current search. Indexed like noite::Tab.
class TabStrip : public QWidget {
    Q_OBJECT
public:
    explicit TabStrip(QWidget* parent = nullptr);

    int count() const { return int(buttons_.size()); }
    int currentIndex() const;
    // Emits currentChanged when the index changes.
    void setCurrentIndex(int index);
    void setCounts(const noite::TabCounts& counts);

    TabButton* button(int index) const { return buttons_.at(index); }
    QString label(int index) const { return buttons_.at(index)->label(); }
    QString countText(int index) const { return buttons_.at(index)->countText(); }
    int buttonHeight() const;

signals:
    void currentChanged(int index);

protected:
    void changeEvent(QEvent* event) override;

private:
    void sizeButtons();

    QButtonGroup* group_;
    QList<TabButton*> buttons_;
};
