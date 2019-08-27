#pragma once

#include <AK/StringView.h>
#include <AK/Types.h>

namespace AK {

class Utf8View;

class Utf8CodepointIterator {
    friend class Utf8View;

public:
    ~Utf8CodepointIterator() {}

    bool operator==(const Utf8CodepointIterator&) const;
    bool operator!=(const Utf8CodepointIterator&) const;
    Utf8CodepointIterator& operator++();
    u32 operator*() const;

private:
    Utf8CodepointIterator(const char*, int);
    const unsigned char* m_ptr;
    int m_length;
};

class Utf8View {
public:
    explicit Utf8View(StringView);
    ~Utf8View() {}

    StringView as_string() const { return m_string; }

    Utf8CodepointIterator begin() const;
    Utf8CodepointIterator end() const;

private:
    StringView m_string;
};

}

using AK::Utf8View;
