#pragma once

#include "AppOptions.h"

#include <QObject>

// The tray application: owns the data folder, the store, the native host and the capture window.
class App : public QObject {
    Q_OBJECT
public:
    explicit App(AppOptions options, QObject* parent = nullptr);
    ~App() override;

    int run();

private:
    AppOptions options_;
};
