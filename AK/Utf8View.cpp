#include <AK/Utf8View.h>

Utf8View::Utf8View(StringView string)
    : m_string(string)
{
}

AK::Utf8CodepointIterator Utf8View::begin() const
{
    return { m_string.characters_without_null_termination(), m_string.length() };
}

AK::Utf8CodepointIterator Utf8View::end() const
{
    return { m_string.characters_without_null_termination() + m_string.length(), 0 };
}

AK::Utf8CodepointIterator::Utf8CodepointIterator(const char* ptr, int length)
    : m_ptr((const unsigned char*)ptr)
    , m_length(length)
{
}

bool AK::Utf8CodepointIterator::operator==(const Utf8CodepointIterator& other) const
{
    return m_ptr == other.m_ptr && m_length == other.m_length;
}

bool AK::Utf8CodepointIterator::operator!=(const Utf8CodepointIterator& other) const
{
    return !(*this == other);
}

AK::Utf8CodepointIterator& AK::Utf8CodepointIterator::operator++()
{
    do {
        ASSERT(m_length > 0);
        m_length--;
        m_ptr++;
    } while (m_ptr[0] >> 6 == 2);

    return *this;
}

u32 AK::Utf8CodepointIterator::operator*() const
{
    ASSERT(m_length > 0);

    u32 codepoint_value_so_far;
    int codepoint_length_in_bytes;

    if ((m_ptr[0] & 128) == 0) {
        codepoint_value_so_far = m_ptr[0];
        codepoint_length_in_bytes = 1;
    } else if ((m_ptr[0] & 32) == 0) {
        codepoint_value_so_far = m_ptr[0] & 31;
        codepoint_length_in_bytes = 2;
    } else if ((m_ptr[0] & 16) == 0) {
        codepoint_value_so_far = m_ptr[0] & 15;
        codepoint_length_in_bytes = 3;
    } else if ((m_ptr[0] & 8) == 0) {
        codepoint_value_so_far = m_ptr[0] & 7;
        codepoint_length_in_bytes = 4;
    } else {
        ASSERT_NOT_REACHED();
    }

    ASSERT(codepoint_length_in_bytes == 1 || (m_ptr[0] & 64));
    ASSERT(codepoint_length_in_bytes <= m_length);

    for (int offset = 1; offset < codepoint_length_in_bytes; offset++) {
        ASSERT(m_ptr[offset] >> 6 == 2);
        codepoint_value_so_far <<= 6;
        codepoint_value_so_far |= m_ptr[offset] & 63;
    }

    return codepoint_value_so_far;
}
