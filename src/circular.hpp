
#pragma once

#include <concepts>

// `T.next` must be `T *`
template <typename T>
concept implicit_node = requires(T t) {
    { t.next } -> std::same_as<T *&>;
};

// circular queue implemented with an implicit linked list
// T should have a `next` member that is a `T *`
// Copying this is equivalent to a pointer copy
template <implicit_node T>
struct circular final
{
    [[nodiscard]] constexpr bool empty() const noexcept { return !tail; }

    constexpr void push(T &node) noexcept
    {
        if (tail)
        {
            // before: [tail, head, ...]
            // after: [tail, node, head, ...]
            node.next = tail->next; // link s to head
            tail->next = &node;     // link old tail to s
            tail = &node;           // set new tail to s
        }
        else
        {
            // set head and tail to s
            tail = &node;
            tail->next = &node;
        }
    }

    [[nodiscard]]
    constexpr T *pop() noexcept
    {
        if (tail)
        {
            // special case: 1 item in list
            if (tail == tail->next)
            {
                auto tmp = tail;
                tail = nullptr;
                return tmp;
            }
            else
            {
                // set head to head->next
                auto head = tail->next;
                tail->next = head->next;
                return head;
            }
        }
        else
            return nullptr;
    }

    constexpr void clear() noexcept { tail = nullptr; }

    friend constexpr void swap(circular &lhs, circular &rhs) noexcept
    {
        std::swap(lhs.tail, rhs.tail);
    }

    T *tail = nullptr;
};
