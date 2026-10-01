#include "Core/Codec.h"

#include <cstring>
#include <limits>

namespace Bunker::Vault
{
    namespace
    {
        constexpr std::size_t kHeaderBytes = 6;

        bool HasNul(const unsigned char* p, std::size_t len)
        {
            for (std::size_t i = 0; i < len; ++i)
                if (p[i] == 0)
                    return true;
            return false;
        }
    }

    Writer Writer::Filling(const Plan& plan, unsigned char* dst, std::size_t cap)
    {
        Writer w(dst, plan.Bytes());
        w.m_planned = plan.Bytes();

        if (!dst || cap < plan.Bytes())
            w.m_overrun = true;
        return w;
    }

    void Writer::Emit(const unsigned char* src, std::size_t n)
    {
        if (Broken())
            return;

        if (m_out)
        {
            if (n > m_cap - m_size)
            {
                m_overrun = true;
                return;
            }
            if (n && src)
                std::memcpy(m_out + m_size, src, n);
        }
        m_size += n;
    }

    void Writer::Patch(std::size_t at, std::uint32_t value)
    {
        if (m_out && at + 4 <= m_size)
            StoreU32Le(m_out + at, value);
    }

    void Writer::BeginRecord(std::uint16_t tag)
    {
        if (m_depth == kMaxNesting)
        {
            m_failed = true;
            return;
        }
        unsigned char head[kHeaderBytes] = {};
        StoreU16Le(head, tag);
        StoreU32Le(head + 2, 0);
        Emit(head, kHeaderBytes);
        if (Broken())
            return;
        m_open[m_depth++] = m_size - 4;
    }

    void Writer::EndRecord()
    {
        if (m_depth == 0)
            return;
        const std::size_t lenOffset = m_open[--m_depth];
        const std::size_t payloadLen = m_size - (lenOffset + 4);
        Patch(lenOffset, static_cast<std::uint32_t>(payloadLen));
    }

    void Writer::WriteBytes(std::uint16_t tag, const unsigned char* data, std::size_t len)
    {
        unsigned char head[kHeaderBytes] = {};
        StoreU16Le(head, tag);
        StoreU32Le(head + 2, static_cast<std::uint32_t>(len));
        Emit(head, kHeaderBytes);
        if (data && len)
            Emit(data, len);
    }

    bool Writer::Measured(Plan& out) const
    {
        if (m_out || Broken() || m_depth != 0)
            return false;
        out = Plan(m_size);
        return true;
    }

    bool Writer::Filled() const
    {
        return m_out != nullptr && !Broken() && m_depth == 0 && m_size == m_planned;
    }

    void Writer::WriteU32(std::uint16_t tag, std::uint32_t value)
    {
        unsigned char v[4];
        StoreU32Le(v, value);
        WriteBytes(tag, v, sizeof(v));
    }

    void Writer::WriteU64(std::uint16_t tag, std::uint64_t value)
    {
        unsigned char v[8];
        StoreU64Le(v, value);
        WriteBytes(tag, v, sizeof(v));
    }

    void Writer::WriteString(std::uint16_t tag, const std::string& s)
    {
        WriteText(tag, s.data(), s.size());
    }

    void Writer::WriteString(std::uint16_t tag, const SecureString& s)
    {
        WriteText(tag, s.CStr(), s.Size());
    }

    void Writer::WriteString(std::uint16_t tag, const char* z)
    {
        WriteText(tag, z ? z : "", z ? std::strlen(z) : 0);
    }

    void Writer::WriteText(std::uint16_t tag, const char* p, std::size_t n)
    {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(p);
        if (HasNul(bytes, n))
        {
            m_failed = true;
            return;
        }
        WriteBytes(tag, bytes, n);
    }

    void Writer::WriteUnknown(const UnknownRecord& rec)
    {
        WriteBytes(rec.Tag, rec.Payload.data(), rec.Payload.size());
    }

    bool Reader::Next(std::uint16_t& tag, const unsigned char*& payload, std::size_t& payloadLen)
    {
        if (m_failed)
            return false;
        if (m_pos == m_len)
            return false;

        if (m_len - m_pos < kHeaderBytes)
        {
            m_failed = true;
            return false;
        }

        tag = LoadU16Le(m_data + m_pos);
        const std::uint32_t len = LoadU32Le(m_data + m_pos + 2);

        const std::size_t remaining = m_len - m_pos - kHeaderBytes;
        if (len > remaining)
        {
            m_failed = true;
            return false;
        }

        payload = m_data + m_pos + kHeaderBytes;
        payloadLen = len;
        m_pos += kHeaderBytes + len;
        return true;
    }

    bool Reader::AsU32(const unsigned char* p, std::size_t len, std::uint32_t& out)
    {
        if (!p || len != 4)
            return false;
        out = LoadU32Le(p);
        return true;
    }

    bool Reader::AsU64(const unsigned char* p, std::size_t len, std::uint64_t& out)
    {
        if (!p || len != 8)
            return false;
        out = LoadU64Le(p);
        return true;
    }

    bool Reader::AsString(const unsigned char* p, std::size_t len, std::string& out)
    {
        if (!p && len)
            return false;
        if (HasNul(p, len))
            return false;
        out.assign(reinterpret_cast<const char*>(p), len);
        return true;
    }

    bool Reader::AsString(const unsigned char* p, std::size_t len, SecureString& out)
    {
        if (!p && len)
            return false;
        if (HasNul(p, len))
            return false;
        out.Assign(p, len);
        return out.Size() == len;
    }
}
