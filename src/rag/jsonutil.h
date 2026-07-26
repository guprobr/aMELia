#pragma once

#include <QByteArray>
#include <QString>

// Hand-rolled JSON string escaping (RFC 8259 control-char/backslash/quote escaping),
// used by the cache writer's custom serializer instead of building a full QJsonDocument
// tree for every chunk -- the cache can hold tens of thousands of chunks, and streaming
// pre-escaped bytes straight to disk avoids materializing that whole tree in memory.
QByteArray jsonQuotedUtf8(const QString &value);
