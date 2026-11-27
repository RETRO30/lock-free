#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <utility>

namespace lock_free
{

    template <typename T>
    struct VersionedStack
    {
    public:
        using Uint = std::uint64_t;
        using AtomicUint = std::atomic<Uint>;
        struct Node
        {
            T data;
            Node *next;
        };
        using NodePointer = Node *;

        struct VersionedNodePointer
        {
            NodePointer head;
            Uint version;
        };
        using AtomicVersionedNodePointer = std::atomic<const VersionedNodePointer *>;

        // -------------------------------------------

        explicit VersionedStack(std::size_t subs_num)
            : subscribers_(std::make_unique<AtomicUint[]>(subs_num)), subs_num_(subs_num)
        {
        }

        ~VersionedStack()
        {
            const VersionedNodePointer *current_state = state_.load();
            NodePointer node = current_state->head;
            while (node != nullptr)
            {
                NodePointer next = node->next;
                delete node;
                node = next;
            }
            delete current_state;
        }

        void push(T data)
        {
            const VersionedNodePointer *current_state = state_.load();
            std::unique_ptr<Node> new_node = std::make_unique<Node>(std::move(data), current_state->head);
            VersionedNodePointer new_state{
                new_node.get(),
                current_state->version + 1,
            };

            publish(current_state, new_state);
            new_node.release();
            collect_garbage(new_state.version);
        }

        bool pop()
        {
            const VersionedNodePointer *current_state = state_.load();
            if (current_state->head == nullptr)
            {
                return false;
            }

            VersionedNodePointer new_state{
                current_state->head->next,
                current_state->version + 1,
            };

            publish(current_state, new_state, current_state->head);

            collect_garbage(new_state.version);

            return true;
        }

        VersionedNodePointer begin_read(std::size_t reader_id)
        {
            if (reader_id >= subs_num_)
            {
                throw std::out_of_range("Invalid reader id");
            }

            subscribers_[reader_id].store(ENTERING);

            VersionedNodePointer snapshot = *state_.load();
            subscribers_[reader_id].store(snapshot.version);
            return snapshot;
        }

        void end_read(std::size_t reader_id)
        {
            if (reader_id >= subs_num_)
                throw std::out_of_range("Invalid reader id");

            subscribers_[reader_id].store(INACTIVE);
        }

    private:
        struct RetiredState
        {
            std::unique_ptr<const VersionedNodePointer> state;
            std::unique_ptr<Node> removed_node;
        };

        void publish(const VersionedNodePointer *current_state, VersionedNodePointer new_state,
                     NodePointer removed_node = nullptr)
        {
            std::unique_ptr<const VersionedNodePointer> next_state = std::make_unique<const VersionedNodePointer>(new_state);
            RetiredState &retired = trash_.emplace_back();
            retired.state.reset(current_state);
            retired.removed_node.reset(removed_node);
            state_.store(next_state.release());
        }

        void collect_garbage(Uint min_version)
        {
            for (std::size_t i = 0; i < subs_num_; ++i)
            {
                Uint subscriber_version = subscribers_[i].load();

                if (subscriber_version == ENTERING)
                    return;

                if (subscriber_version != INACTIVE && subscriber_version < min_version)
                    min_version = subscriber_version;
            }

            while (!trash_.empty() && trash_.front().state->version < min_version)
            {
                trash_.pop_front();
            }
        }
        static constexpr Uint INACTIVE = 0;
        static constexpr Uint ENTERING = UINT64_MAX;

        static_assert(AtomicUint::is_always_lock_free);
        static_assert(AtomicVersionedNodePointer::is_always_lock_free);

        std::unique_ptr<AtomicUint[]> subscribers_;
        std::size_t subs_num_;
        std::deque<RetiredState> trash_;
        AtomicVersionedNodePointer state_{new const VersionedNodePointer{nullptr, 1}};
    };
}
