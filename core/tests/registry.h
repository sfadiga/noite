#pragma once

#include <QObject>

#include <functional>
#include <vector>

// Every test class registers itself with NOITE_TEST(Class); tests/main.cpp runs them all.
namespace noite::test {

struct Entry {
    const char* name;
    std::function<QObject*()> make;
};

inline std::vector<Entry>& registry()
{
    static std::vector<Entry> entries;
    return entries;
}

struct Registrar {
    Registrar(const char* name, std::function<QObject*()> make) { registry().push_back({name, std::move(make)}); }
};

} // namespace noite::test

#define NOITE_TEST(Class) \
    static const noite::test::Registrar noite_registrar_##Class(#Class, [] { return new Class; });
