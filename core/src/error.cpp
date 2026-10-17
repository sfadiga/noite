#include <noite/error.h>

namespace noite {

Error::Error(ErrorCode code, const QString& message)
    : std::runtime_error(message.toStdString())
    , code_(code)
{
}

} // namespace noite
