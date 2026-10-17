#include "App.h"

App::App(AppOptions options, QObject* parent)
    : QObject(parent)
    , options_(std::move(options))
{
}

App::~App() = default;

int App::run()
{
    return 0;
}
