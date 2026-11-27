#include "position.hpp"
#include "versioned_stack.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

namespace
{
    using Stack = lock_free::VersionedStack<lock_free::Position>;

    constexpr std::size_t reader_count = 4;
    constexpr int cycles = 100'000;
    constexpr unsigned int seed = 42;
    constexpr int max_batch = 8;
    constexpr int intervals = 32;
    constexpr int point_count = intervals + 1;
    constexpr double last_x = 4.0;
    constexpr double step = last_x / intervals;

    double parabola(double x)
    {
        return x * (last_x - x);
    }

    class Writer
    {
    public:
        explicit Writer(Stack &stack) : stack_(stack)
        {
        }

        void run()
        {
            std::srand(seed);
            int size = 0;
            append(size, point_count);

            for (int cycle = 0; cycle < cycles; ++cycle)
            {
                const int count = std::min(size, 1 + std::rand() % max_batch);
                for (int i = 0; i < count; ++i)
                {
                    if (!stack_.pop())
                        throw std::runtime_error("Unexpected empty stack");
                }
                size -= count;
                append(size, 1 + std::rand() % max_batch);
            }
        }

    private:
        void append(int &size, int count)
        {
            const int end = std::min(size + count, point_count);
            for (; size < end; ++size)
            {
                const double x = size * step;
                stack_.push({x, parabola(x)});
            }
        }

        Stack &stack_;
    };

    class Reader
    {
    public:
        void run(Stack &stack, std::size_t id, Stack::Uint previous_version,
                 const std::atomic<bool> &done, std::stop_token stop)
        {
            while (!stop.stop_requested())
            {
                const bool finished = done.load();
                const Stack::VersionedNodePointer snapshot = stack.begin_read(id);
                if (snapshot.version != previous_version)
                {
                    ++versions;
                    if (snapshot.version < previous_version || !is_consistent(snapshot.head))
                        ++errors;
                    previous_version = snapshot.version;
                }
                stack.end_read(id);
                if (finished)
                    break;
            }
        }

        Stack::Uint versions = 0;
        Stack::Uint errors = 0;

    private:
        static bool is_consistent(const Stack::Node *node)
        {
            if (node == nullptr)
                return true;

            double expected_x = node->data.x;
            for (; node != nullptr; node = node->next)
            {
                const lock_free::Position &point = node->data;
                if (!(point.x >= 0.0 && point.x <= last_x) ||
                    point.x != expected_x || point.y != parabola(point.x))
                {
                    return false;
                }
                expected_x -= step;
            }
            return expected_x == -step;
        }
    };
}

int main() try
{
    const Stack::AtomicVersionedNodePointer state{nullptr};
    const bool is_lock_free = state.is_lock_free();
    std::cout << std::boolalpha << "is_lock_free(): " << is_lock_free << std::endl;

    Stack stack(reader_count);
    const Stack::Uint initial_version = stack.begin_read(0).version;
    stack.end_read(0);

    std::atomic<bool> done{false};
    std::latch ready(reader_count);
    Reader readers[reader_count];
    std::jthread threads[reader_count];
    for (std::size_t i = 0; i < reader_count; ++i)
    {
        threads[i] = std::jthread([&, i](std::stop_token stop)
        {
            ready.count_down();
            readers[i].run(stack, i, initial_version, done, stop);
        });
    }
    ready.wait();

    Writer writer(stack);
    writer.run();
    done.store(true);

    for (std::jthread &thread : threads)
        thread.join();

    const Stack::Uint versions = stack.begin_read(0).version - initial_version;
    stack.end_read(0);
    std::cout << "Writer: " << versions << " versions\n";

    bool passed = is_lock_free && versions > 0;
    for (std::size_t i = 0; i < reader_count; ++i)
    {
        const Reader &reader = readers[i];
        std::cout << "Reader " << i + 1 << ": " << reader.versions << " versions, "
                  << reader.errors << " errors\n";
        passed = passed && reader.versions > 0 && reader.errors == 0;
    }
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
}
