#include <cstdio>
#include "humble/concurrent/rcu.hpp"

int main()
{
    hmbl::concurrent::RcuContext<const int> rcu_ctx;
    hmbl::concurrent::RcuReader rcu_rdr{rcu_ctx};
    hmbl::concurrent::RcuReader rcu_rdr2{rcu_ctx, false};

    std::thread th([rcu_rdr = std::move(rcu_rdr)]() mutable
        {
            ::printf("[th] Start thread\n");
            while (!rcu_rdr.get_resource() || *rcu_rdr.get_resource() < 5)
            {
                ::printf("[th] %p\n", (const void*)rcu_rdr.get_resource());
                if (rcu_rdr.get_resource())
                {
                    ::printf("[th] resource %d\n", *rcu_rdr.get_resource());
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
                rcu_rdr.update();
            }
        }
    );

    std::thread th2([rcu_rdr = std::move(rcu_rdr2)]() mutable
        {
            ::printf("[th2] Start thread\n");
            while (!rcu_rdr.is_active() || !rcu_rdr.get_resource() || *rcu_rdr.get_resource() < 5)
            {
                if (!rcu_rdr.is_active())
                {
                    std::this_thread::sleep_for(std::chrono::seconds(4));
                    rcu_rdr.activate();
                    ::printf("[th2] RCU reader is activated\n");
                }

                ::printf("[th2] %p\n", (const void*)rcu_rdr.get_resource());
                if (rcu_rdr.get_resource())
                {
                    ::printf("[th2] resource %d\n", *rcu_rdr.get_resource());
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
                rcu_rdr.update();
            }
        }
    );

    for (int i{}; i <= 5; ++i)
    {
        auto p = std::make_unique<int>(i);
        ::printf("new resource(%p) = %d\n", (const void*)p.get(), *p);

        rcu_ctx.update(std::move(p));
        ::printf("readers in progress %d, active: %lu\n", rcu_ctx.get_readers_in_progress_count(), rcu_ctx.get_active_readers_count());
        rcu_ctx.wait_update_complete();
        ::printf("readers in progress %d, active: %lu\n", rcu_ctx.get_readers_in_progress_count(), rcu_ctx.get_active_readers_count());
    }
    th.join();
    th2.join();
    return 0;
}