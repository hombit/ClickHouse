#pragma once

#include <variant>
#include <Common/HashTable/HashMap.h>
#include <Common/HashTable/HashTable.h>

/// TODO feature macros

#define CASE_1_8 \
    case 1: \
    case 2: \
    case 3: \
    case 4: \
    case 5: \
    case 6: \
    case 7: \
    case 8

#define CASE_9_16 \
    case 9: \
    case 10: \
    case 11: \
    case 12: \
    case 13: \
    case 14: \
    case 15: \
    case 16

#define CASE_17_24 \
    case 17: \
    case 18: \
    case 19: \
    case 20: \
    case 21: \
    case 22: \
    case 23: \
    case 24

struct StringKey0
{
};

using StringKey8 = UInt64;
using StringKey16 = DB::UInt128;
struct StringKey24
{
    UInt64 a;
    UInt64 b;
    UInt64 c;

    bool operator==(const StringKey24 rhs) const { return a == rhs.a && b == rhs.b && c == rhs.c; }
    bool operator!=(const StringKey24 rhs) const { return !operator==(rhs); }
    bool operator==(const UInt64 rhs) const { return a == rhs && b == 0 && c == 0; }
    bool operator!=(const UInt64 rhs) const { return !operator==(rhs); }

    StringKey24 & operator=(const UInt64 rhs)
    {
        a = rhs;
        b = 0;
        c = 0;
        return *this;
    }
};

inline StringRef ALWAYS_INLINE toStringRef(const StringKey8 & n)
{
    return {reinterpret_cast<const char *>(&n), 8ul - (__builtin_clzll(n) >> 3)};
}
inline StringRef ALWAYS_INLINE toStringRef(const StringKey16 & n)
{
    return {reinterpret_cast<const char *>(&n), 16ul - (__builtin_clzll(n.high) >> 3)};
}
inline StringRef ALWAYS_INLINE toStringRef(const StringKey24 & n)
{
    return {reinterpret_cast<const char *>(&n), 24ul - (__builtin_clzll(n.c) >> 3)};
}
inline const StringRef & ALWAYS_INLINE toStringRef(const StringRef & s)
{
    return s;
}

struct StringHashTableHash
{
    size_t ALWAYS_INLINE operator()(StringKey8 key) const
    {
        size_t res = -1ULL;
        res = _mm_crc32_u64(res, key);
        return res;
    }
    size_t ALWAYS_INLINE operator()(StringKey16 key) const
    {
        size_t res = -1ULL;
        res = _mm_crc32_u64(res, key.low);
        res = _mm_crc32_u64(res, key.high);
        return res;
    }
    size_t ALWAYS_INLINE operator()(StringKey24 key) const
    {
        size_t res = -1ULL;
        res = _mm_crc32_u64(res, key.a);
        res = _mm_crc32_u64(res, key.b);
        res = _mm_crc32_u64(res, key.c);
        return res;
    }
    size_t ALWAYS_INLINE operator()(StringRef key) const
    {
        size_t res = -1ULL;
        size_t sz = key.size;
        const char * p = key.data;
        const char * lp = p + sz - 8; // starting pointer of the last 8 bytes segment
        char s = (-sz & 7) * 8; // pending bits that needs to be shifted out
        UInt64 n[3]; // StringRef in SSO map will have length > 24
        memcpy(&n, p, 24);
        res = _mm_crc32_u64(res, n[0]);
        res = _mm_crc32_u64(res, n[1]);
        res = _mm_crc32_u64(res, n[2]);
        p += 24;
        while (p + 8 < lp)
        {
            memcpy(&n[0], p, 8);
            res = _mm_crc32_u64(res, n[0]);
            p += 8;
        }
        memcpy(&n[0], lp, 8);
        n[0] >>= s;
        res = _mm_crc32_u64(res, n[0]);
        return res;
    }
};

template <typename Cell>
struct StringHashTableEmpty
{
    using Self = StringHashTableEmpty;

    Cell value;
    bool is_empty{true};

    StringHashTableEmpty() { memset(reinterpret_cast<char *>(&value), 0, sizeof(value)); }

    using LookupResult = Cell *;
    using ConstLookupResult = const Cell *;

    template <typename KeyHolder>
    void ALWAYS_INLINE emplaceNonZero(KeyHolder &&, LookupResult & it, bool & inserted, size_t)
    {
        if (is_empty)
        {
            inserted = true;
            is_empty = false;
        }
        else
            inserted = false;
        it = &value;
    }

    template <typename Key>
    LookupResult ALWAYS_INLINE find(Key, size_t)
    {
        return is_empty ? nullptr : &value;
    }

    template <typename Key>
    ConstLookupResult ALWAYS_INLINE find(Key x) const
    {
        return const_cast<std::decay_t<decltype(*this)> *>(this)->find(x);
    }

    using Position = Cell *;
    using ConstPosition = const Cell *;

    Position startPos() { return nullptr; }
    ConstPosition startPos() const { return nullptr; }

    template <typename TSelf, typename Func, typename TPosition>
    static bool forEachCell(TSelf & self, Func && func, TPosition & pos)
    {
        using TCell = std::conditional_t<std::is_const_v<TSelf>, const Cell, Cell>;
        static constexpr bool with_key = std::is_invocable_v<Func, const StringRef &, TCell &>;
        using ReturnType = typename std::
            conditional_t<with_key, std::invoke_result<Func, const StringRef &, TCell &>, std::invoke_result<Func, TCell &>>::type;
        static constexpr bool ret_bool = std::is_same_v<bool, ReturnType>;

        if (pos == self.startPos())
        {
            if (!self.is_empty)
            {
                if constexpr (with_key)
                {
                    if constexpr (ret_bool)
                    {
                        if (func(StringRef(), self.value))
                        {
                            pos = &self.value;
                            return true;
                        }
                    }
                    else
                        func(StringRef(), self.value);
                }
                else
                {
                    if constexpr (ret_bool)
                    {
                        if (func(self.value))
                        {
                            pos = &self.value;
                            return true;
                        }
                    }
                    else
                        func(self.value);
                }
            }
        }
        pos = self.startPos();
        return false;
    }

    /// Iterate over every cell and pass non-zero cells to func.
    ///  Func should have signature (1) void(const Key &, const Cell &); or (2)  void(const Cell &).
    template <typename Func>
    auto forEachCell(Func && func) const
    {
        ConstPosition pos = startPos();
        return forEachCell(*this, std::forward<Func>(func), pos);
    }

    /// Iterate over every cell and pass non-zero cells to func.
    ///  Func should have signature (1) void(const Key &, Cell &); or (2)  void(const Cell &).
    template <typename Func>
    auto forEachCell(Func && func)
    {
        Position pos = nullptr;
        return forEachCell(*this, std::forward<Func>(func), pos);
    }

    /// Same as the above functions but with additional position variable to resume last iteration.
    template <typename Func>
    auto forEachCell(Func && func, ConstPosition & pos) const
    {
        return forEachCell(*this, std::forward<Func>(func), pos);
    }

    /// Same as the above functions but with additional position variable to resume last iteration.
    template <typename Func>
    auto forEachCell(Func && func, Position & pos)
    {
        return forEachCell(*this, std::forward<Func>(func), pos);
    }

    void write(DB::WriteBuffer & wb) const { value.write(wb); }
    void writeText(DB::WriteBuffer & wb) const { value.writeText(wb); }
    void read(DB::ReadBuffer & rb) { value.read(rb); }
    void readText(DB::ReadBuffer & rb) { value.readText(rb); }
    size_t size() const { return is_empty ? 0 : 1; }
    bool empty() const { return is_empty; }
    size_t getBufferSizeInBytes() const { return sizeof(Cell); }
    size_t getCollisions() const { return 0; }
};

template <size_t initial_size_degree = 8>
struct StringHashTableGrower : public HashTableGrower<initial_size_degree>
{
    // Smooth growing for string maps
    void increaseSize() { this->size_degree += 1; }
};

template <typename Position>
struct StringHashTableLookupResult
{
    Position pos;
    StringHashTableLookupResult() {}
    StringHashTableLookupResult(Position pos_) : pos(pos_) {}
    StringHashTableLookupResult(std::nullptr_t) {}
    auto & getSecond() { return *pos; }
    auto & operator*() { return *this; }
    auto & operator*() const { return *this; }
    auto * operator->() { return this; }
    auto * operator->() const { return this; }
    operator bool() const { return pos; }
    friend bool operator==(const StringHashTableLookupResult & a, const std::nullptr_t &) { return !a.pos; }
    friend bool operator==(const std::nullptr_t &, const StringHashTableLookupResult & b) { return !b.pos; }
    friend bool operator!=(const StringHashTableLookupResult & a, const std::nullptr_t &) { return a.pos; }
    friend bool operator!=(const std::nullptr_t &, const StringHashTableLookupResult & b) { return b.pos; }
};

template <typename SubMaps>
class StringHashTable : private boost::noncopyable
{
protected:
    static constexpr size_t NUM_MAPS = 5;
    // Map for storing empty string
    using T0 = typename SubMaps::T0;

    // Short strings are stored as numbers
    using T1 = typename SubMaps::T1;
    using T2 = typename SubMaps::T2;
    using T3 = typename SubMaps::T3;

    // Long strings are stored as StringRef along with saved hash
    using Ts = typename SubMaps::Ts;
    using Self = StringHashTable;

    template <typename, typename, size_t>
    friend class TwoLevelStringHashTable;

    T0 m0;
    T1 m1;
    T2 m2;
    T3 m3;
    Ts ms;

public:
    using Key = StringRef;
    using key_type = Key;
    using value_type = typename Ts::value_type;
    using mapped_type = typename Ts::mapped_type;
    using cell_type = typename Ts::cell_type;

    StringHashTable() {}

    StringHashTable(size_t reserve_for_num_elements)
        : m1{reserve_for_num_elements / 4}
        , m2{reserve_for_num_elements / 4}
        , m3{reserve_for_num_elements / 4}
        , ms{reserve_for_num_elements / 4}
    {
    }

    StringHashTable(StringHashTable && rhs) { *this = std::move(rhs); }
    ~StringHashTable() {}

public:
    // Dispatch is written in a way that maximizes the performance:
    // 1. Always memcpy 8 times bytes
    // 2. Use switch case extension to generate fast dispatching table
    // 3. Combine hash computation along with key loading
    // 4. Funcs are named callables that can be force_inlined
    // NOTE: It relies on Little Endianness and SSE4.2
    template <typename KeyHolder, typename Func>
    decltype(auto) ALWAYS_INLINE dispatch(KeyHolder && key_holder, Func && func)
    {
        auto & x = keyHolderGetKey(key_holder);
        static constexpr StringKey0 key0{};
        size_t sz = x.size;
        const char * p = x.data;
        // pending bits that needs to be shifted out
        char s = (-sz & 7) * 8;
        size_t res = -1ULL;
        union
        {
            StringKey8 k8;
            StringKey16 k16;
            StringKey24 k24;
            UInt64 n[3];
        };
        switch (sz)
        {
            case 0:
                return func(m0, key0, 0);
            CASE_1_8 : {
                // first half page
                if ((reinterpret_cast<uintptr_t>(p) & 2048) == 0)
                {
                    memcpy(&n[0], p, 8);
                    n[0] &= -1ul >> s;
                }
                else
                {
                    const char * lp = x.data + x.size - 8;
                    memcpy(&n[0], lp, 8);
                    n[0] >>= s;
                }
                res = _mm_crc32_u64(res, n[0]);
                return func(m1, k8, res);
            }
            CASE_9_16 : {
                memcpy(&n[0], p, 8);
                res = _mm_crc32_u64(res, n[0]);
                const char * lp = x.data + x.size - 8;
                memcpy(&n[1], lp, 8);
                n[1] >>= s;
                res = _mm_crc32_u64(res, n[1]);
                return func(m2, k16, res);
            }
            CASE_17_24 : {
                memcpy(&n[0], p, 16);
                res = _mm_crc32_u64(res, n[0]);
                res = _mm_crc32_u64(res, n[1]);
                const char * lp = x.data + x.size - 8;
                memcpy(&n[2], lp, 8);
                n[2] >>= s;
                res = _mm_crc32_u64(res, n[2]);
                return func(m3, k24, res);
            }
            default: {
                memcpy(&n, x.data, 24);
                res = _mm_crc32_u64(res, n[0]);
                res = _mm_crc32_u64(res, n[1]);
                res = _mm_crc32_u64(res, n[2]);
                p += 24;
                const char * lp = x.data + x.size - 8;
                while (p + 8 < lp)
                {
                    memcpy(&n[0], p, 8);
                    res = _mm_crc32_u64(res, n[0]);
                    p += 8;
                }
                memcpy(&n[0], lp, 8);
                n[0] >>= s;
                res = _mm_crc32_u64(res, n[0]);
                return func(ms, std::forward<KeyHolder>(key_holder), res);
            }
        }
    }

    using Position
        = std::variant<typename T0::Position, typename T1::Position, typename T2::Position, typename T3::Position, typename Ts::Position>;

    using ConstPosition = std::variant<
        typename T0::ConstPosition,
        typename T1::ConstPosition,
        typename T2::ConstPosition,
        typename T3::ConstPosition,
        typename Ts::ConstPosition>;

    Position startPos() { return Position{std::in_place_index<0>, m0.startPos()}; }
    ConstPosition startPos() const { return Position{std::in_place_index<0>, m0.startPos()}; }

    template <typename TSelf, typename Func, typename TPosition>
    static bool forEachCell(TSelf & self, Func && func, TPosition & pos)
    {
        switch (pos.index())
        {
            case 0:
                if (self.m0.forEachCell(self.m0, func, std::get<0>(pos)))
                    return true;
                pos = TPosition{std::in_place_index<1>, self.m1.startPos()};
                [[fallthrough]];
            case 1:
                if (self.m1.forEachCell(self.m1, func, std::get<1>(pos)))
                    return true;
                pos = TPosition{std::in_place_index<2>, self.m2.startPos()};
                [[fallthrough]];
            case 2:
                if (self.m2.forEachCell(self.m2, func, std::get<2>(pos)))
                    return true;
                pos = TPosition{std::in_place_index<3>, self.m3.startPos()};
                [[fallthrough]];
            case 3:
                if (self.m3.forEachCell(self.m3, func, std::get<3>(pos)))
                    return true;
                pos = TPosition{std::in_place_index<4>, self.ms.startPos()};
                [[fallthrough]];
            case 4:
                if (self.ms.forEachCell(self.ms, func, std::get<4>(pos)))
                    return true;
        }
        return false;
    }

    /// Iterate over every cell and pass non-zero cells to func.
    ///  Func should have signature (1) void(const Key &, const Cell &); or (2)  void(const Cell &).
    template <typename Func>
    void forEachCell(Func && func) const
    {
        ConstPosition pos = startPos();
        forEachCell(*this, std::forward<Func>(func), pos);
    }

    /// Iterate over every cell and pass non-zero cells to func.
    ///  Func should have signature (1) void(const Key &, Cell &); or (2)  void(Cell &).
    template <typename Func>
    void forEachCell(Func && func)
    {
        Position pos = startPos();
        forEachCell(*this, std::forward<Func>(func), pos);
    }

    /// Same as the above functions but with additional position variable to resume last iteration.
    template <typename Func>
    void forEachCell(Func && func, ConstPosition & pos) const
    {
        forEachCell(*this, std::forward<Func>(func), pos);
    }

    /// Same as the above functions but with additional position variable to resume last iteration.
    template <typename Func>
    void forEachCell(Func && func, Position & pos)
    {
        forEachCell(*this, std::forward<Func>(func), pos);
    }

    using LookupResult = StringHashTableLookupResult<typename cell_type::mapped_type *>;
    using ConstLookupResult = StringHashTableLookupResult<const typename cell_type::mapped_type *>;

    struct InsertCallable
    {
        LookupResult & it;
        bool & inserted;
        const value_type & x;
        InsertCallable(LookupResult & it_, bool & inserted_, const value_type & x_) : it(it_), inserted(inserted_), x(x_) {}
        template <typename Map, typename Key>
        void ALWAYS_INLINE operator()(Map & map, const Key & key, size_t hash)
        {
            typename Map::LookupResult impl_it;
            map.emplace(key, impl_it, inserted, hash);
            it = &impl_it->getSecond();
        }
    };

    /// Insert a value. In the case of any more complex values, it is better to use the `emplace` function.
    template <typename KeyHolder>
    std::pair<LookupResult, bool> ALWAYS_INLINE insert(const value_type & x)
    {
        std::pair<LookupResult, bool> res;
        dispatch(cell_type::getKey(x), InsertCallable{res.first, res.second});
        if (res.second)
            insertSetMapped(res.first->getSecond(), x);
        return res;
    }

    struct EmplaceCallable
    {
        LookupResult & it;
        bool & inserted;
        EmplaceCallable(LookupResult & it_, bool & inserted_) : it(it_), inserted(inserted_) {}
        template <typename Map, typename KeyHolder>
        void ALWAYS_INLINE operator()(Map & map, KeyHolder && x, size_t hash)
        {
            typename Map::LookupResult impl_it;
            map.emplaceNonZero(std::forward<KeyHolder>(x), impl_it, inserted, hash);
            it = &impl_it->getSecond();
        }
    };

    template <typename KeyHolder>
    void ALWAYS_INLINE emplace(KeyHolder && x, LookupResult & it, bool & inserted)
    {
        dispatch(std::forward<KeyHolder>(x), EmplaceCallable{it, inserted});
    }

    struct FindCallable
    {
        template <typename Map, typename Key>
        LookupResult ALWAYS_INLINE operator()(Map & map, const Key & x, size_t hash)
        {
            typename Map::LookupResult it = map.find(x, hash);
            return it ? &it->getSecond() : nullptr;
        }
    };

    LookupResult ALWAYS_INLINE find(Key x) { return dispatch(x, FindCallable{}); }

    ConstLookupResult ALWAYS_INLINE find(Key x) const { return const_cast<std::decay_t<decltype(*this)> *>(this)->find(x); }

    void write(DB::WriteBuffer & wb) const
    {
        m0.write(wb);
        m1.write(wb);
        m2.write(wb);
        m3.write(wb);
        ms.write(wb);
    }

    void writeText(DB::WriteBuffer & wb) const
    {
        m0.writeText(wb);
        DB::writeChar(',', wb);
        m1.writeText(wb);
        DB::writeChar(',', wb);
        m2.writeText(wb);
        DB::writeChar(',', wb);
        m3.writeText(wb);
        DB::writeChar(',', wb);
        ms.writeText(wb);
    }

    void read(DB::ReadBuffer & rb)
    {
        m0.read(rb);
        m1.read(rb);
        m2.read(rb);
        m3.read(rb);
        ms.read(rb);
    }

    void readText(DB::ReadBuffer & rb)
    {
        m0.readText(rb);
        DB::assertChar(',', rb);
        m1.readText(rb);
        DB::assertChar(',', rb);
        m2.readText(rb);
        DB::assertChar(',', rb);
        m3.readText(rb);
        DB::assertChar(',', rb);
        ms.readText(rb);
    }

    size_t size() const { return m0.size() + m1.size() + m2.size() + m3.size() + ms.size(); }

    bool empty() const { return m0.empty() && m1.empty() && m2.empty() && m3.empty() && ms.empty(); }

    size_t getBufferSizeInBytes() const
    {
        return m0.getBufferSizeInBytes() + m1.getBufferSizeInBytes() + m2.getBufferSizeInBytes() + m3.getBufferSizeInBytes()
            + ms.getBufferSizeInBytes();
    }

    void clearAndShrink()
    {
        using Cell = decltype(m0.value);
        if (!std::is_trivially_destructible_v<Cell>)
            m0.value.~Cell();
        m1.clearAndShrink();
        m2.clearAndShrink();
        m3.clearAndShrink();
        ms.clearAndShrink();
    }
};
