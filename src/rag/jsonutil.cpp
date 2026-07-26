#include "rag/jsonutil.h"

QByteArray jsonQuotedUtf8(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    QByteArray encoded;
    encoded.reserve(utf8.size() + 2);
    encoded.push_back('"');
    for (unsigned char ch : utf8) {
        switch (ch) {
        case '\\':
            encoded += "\\\\";
            break;
        case '"':
            encoded += "\\\"";
            break;
        case '\b':
            encoded += "\\b";
            break;
        case '\f':
            encoded += "\\f";
            break;
        case '\n':
            encoded += "\\n";
            break;
        case '\r':
            encoded += "\\r";
            break;
        case '\t':
            encoded += "\\t";
            break;
        default:
            if (ch < 0x20) {
                encoded += "\\u00";
                const char digits[] = "0123456789abcdef";
                encoded.push_back(digits[(ch >> 4) & 0x0f]);
                encoded.push_back(digits[ch & 0x0f]);
            } else {
                encoded.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    encoded.push_back('"');
    return encoded;
}
