#include "tc_catch2.h"

#include "World.h"

void DummySessionsUpdatedCallback(std::deque<uint32>::const_iterator begin, std::deque<uint32>::const_iterator end) {}

void DummySessionTrimmedCallback(uint32 sessionId) {}

TEST_CASE("Initialization", "[LoginQueue]") {
    auto q = LoginQueue();

    SECTION("initializing with sensible values") {
        q.Init(100000, 100);
        CHECK(q.GetMaxSessions() == 100000);
        CHECK(q.GetBucketSize() == 100);
    }

    SECTION("initializing with unusual values") {
        q.Init(10, 100);
        CHECK(q.GetMaxSessions() == 10);
        CHECK(q.GetBucketSize() == 100);
        for (std::size_t i = 1; i <= 10; i++) {
            q.AddSession(i);
        }
        CHECK(q.SessionCount() == 10);
        CHECK(q.BucketCount() == 1);
        CHECK(q.AddSession(11) == std::nullopt);
    }

    SECTION("initializing with unusual values 2") {
        q.Init(1, 1);
        CHECK(q.GetMaxSessions() == 1);
        CHECK(q.GetBucketSize() == 1);
        q.AddSession(1);
        CHECK(q.SessionCount() == 1);
        CHECK(q.BucketCount() == 1);
        CHECK(q.AddSession(2) == std::nullopt);
    }

    SECTION("initializing with zeroes") {
        q.Init(0, 0);
        CHECK(q.GetMaxSessions() == 0);
        CHECK(q.BucketCount() == 1);
        CHECK(q.AddSession(1) == std::nullopt);
    }
}

TEST_CASE("Add and pop", "[LoginQueue]") {
    auto q = LoginQueue();
    q.Init(1000, 100);

    SECTION("adding sessions returns the queue position of the added session") {
        for (std::size_t i = 1; i <= 1000; i++) {
            auto opt = q.AddSession(i);
            REQUIRE(opt != std::nullopt);
            REQUIRE(*opt == i);
        }
    }

    SECTION("adding 1000 sessions results in 1000 sessions in queue") {
        for (std::size_t i = 1; i <= 1000; i++) {
            q.AddSession(i);
        }
        CHECK(q.SessionCount() == 1000);
        SECTION("clearing the queue results in an empty queue") {
            q.Clear();
            CHECK(q.SessionCount() == 0);
        }
    }

    SECTION("adding 1001 sessions to a queue capped at 1000 fails") {
        for (std::size_t i = 1; i <= 1000; i++) {
            q.AddSession(i);
        }
        auto opt = q.AddSession(1001);
        CHECK(opt == std::nullopt);
    }

    SECTION("popping every session pops all 1000 of them and in FIFO order") {
        for (std::size_t i = 1; i <= 1000; i++) {
            q.AddSession(i);
        }
        std::size_t popped = 0;
        while (auto opt = q.PopSession()) {
            popped++;
            CHECK(*opt == popped);
        }
        CHECK(popped == 1000);

        SECTION("popping from an empty queue returns std::nullopt") {
            CHECK(q.PopSession() == std::nullopt);
        }
    }
}

TEST_CASE("Individual session removal", "[LoginQueue]") {
    auto q = LoginQueue();
    q.Init(100000, 100);

    SECTION("removed sessions should turn to zeroes") {
        q.AddSession(1);
        q.RemoveSessionFromQueue(1);
        auto opt = q.PopSession();
        CHECK(opt != std::nullopt);
        CHECK(*opt == 0);
    }

    SECTION("removed sessions aren't reported in queue") {
        q.AddSession(1);
        q.RemoveSessionFromQueue(1);
        CHECK(!q.IsSessionInQueue(1));
    }

    SECTION("removed sessions don't count towards total") {
        for (std::size_t i = 1; i <= 1000; i++) {
            q.AddSession(i);
        }
        CHECK(q.SessionCount() == 1000);
        for (std::size_t i = 2; i <= 1000; i += 2) {
            q.RemoveSessionFromQueue(i);
        }
        CHECK(q.SessionCount() == 500);
    }
}

TEST_CASE("IsSessionInQueue", "[LoginQueue]") {
    auto q = LoginQueue();
    q.Init(100000, 100);
    for (std::size_t i = 1; i <= 1000; i++) {
        q.AddSession(i);
    }
    CHECK(q.IsSessionInQueue(1));
    CHECK(q.IsSessionInQueue(101));
    CHECK(q.IsSessionInQueue(1000));
    CHECK(!q.IsSessionInQueue(1001));

    q.RemoveSessionFromQueue(1);
    INFO("removing a session doesn't cause blank ID to report as in queue");
    CHECK(!q.IsSessionInQueue(0));
}

TEST_CASE("GetQueuePosition and GetQueuePositionApproximate", "[LoginQueue]") {
    auto q = LoginQueue();
    q.Init(100000, 100);
    for (std::size_t i = 1; i <= 1000; i++) {
        q.AddSession(i);
    }

    CHECK(q.GetQueuePos(1) == 1);
    CHECK(q.GetQueuePos(99) == 99);
    CHECK(q.GetQueuePosApproximate(99) == 99);
    CHECK(q.GetQueuePos(150) == 150);
    CHECK(q.GetQueuePosApproximate(150) == 150);
    CHECK(q.GetQueuePosApproximate(550) == 500);
    CHECK(q.GetQueuePos(0) == std::nullopt);
    CHECK(q.GetQueuePosApproximate(0) == std::nullopt);
    CHECK(q.GetQueuePos(1001) == std::nullopt);
    CHECK(q.GetQueuePosApproximate(1001) == std::nullopt);
}

TEST_CASE("Resize", "[LoginQueue]") {
    auto q = LoginQueue();
    q.Init(100000, 100);
    for (std::size_t i = 1; i <= 1000; i++) {
        q.AddSession(i);
    }

    SECTION("changing bucket size preserves sessions") {
        INFO("adding 1000 sessions with bucketSize 100 results in 1000 sessions across 10 buckets");
        REQUIRE(q.SessionCount() == 1000);
        REQUIRE(q.BucketCount() == 10);

        INFO("resizing to bucketSize 200 results in 1000 sessions across 5 buckets");
        q.Resize(100000, 200);
        REQUIRE(q.SessionCount() == 1000);
        REQUIRE(q.BucketCount() == 5);

        SECTION("popping all sessions from a resized queue pops all 1000 of them in FIFO order") {
            std::size_t popped = 0;
            while (auto opt = q.PopSession()) {
                popped++;
                CHECK(*opt == popped);
            }
            CHECK(popped == 1000);
        }

        SECTION("changing bucket size cleans up removed (zeroed) sessions") {
            for (std::size_t i = 2; i <= 1000; i += 2) {
                q.RemoveSessionFromQueue(i);
            }
            q.Resize(100000, 100);
            REQUIRE(q.SessionCount() == 500);
            REQUIRE(q.BucketCount() == 5);
        }
    }

    SECTION("changing maxSessions truncates queue") {
        q.Resize(500, 100);
        CHECK(q.SessionCount() == 500);
        CHECK(q.BucketCount() == 5);

        SECTION("popping sessions out of a truncated queue pops 500 of them in FIFO order") {
            std::size_t popped = 0;
            while (auto opt = q.PopSession()) {
                popped++;
                CHECK(*opt == popped);
            }
            CHECK(popped == 500);
        }
    }
}

TEST_CASE("SessionsUpdatedCallback", "[LoginQueue]") {
    auto q = LoginQueue();
    q.Init(100000, 100);
    for (std::size_t i = 1; i <= 1000; i++) {
        q.AddSession(i);
    }
    std::unordered_set<uint32> updatedSessions;
    auto cb = [&](auto begin, auto end) {
        for (auto it = begin; it != end; ++it) {
            updatedSessions.insert(*it);
        }
    };
    q.SetSessionsUpdatedCallback(cb);

    SECTION("only the first two buckets get SessionsUpdatedCallback for every pop") {
        q.PopSession();
        CHECK(updatedSessions.size() == 199);
    }

    SECTION("all buckets get SessionsUpdatedCallback after a bucket is emptied") {
        for (std::size_t i = 0; i < 100; i++) {
            q.PopSession();
        }
        CHECK(updatedSessions.size() == 999);
    }
}

TEST_CASE("SessionTrimmedCallback", "[LoginQueue]") {
    auto q = LoginQueue();
    q.Init(100000, 100);
    for (std::size_t i = 1; i <= 1000; i++) {
        q.AddSession(i);
    }
    std::unordered_set<uint32> trimmedSessions;
    auto cb = [&](uint32 id) {
        trimmedSessions.insert(id);
    };
    q.SetSessionTrimmedCallback(cb);

    SECTION("resizes that trim sessions call SessionTrimmedCallback") {
        q.Resize(999, 100);
        CHECK(q.SessionCount() == 999);
        CHECK(trimmedSessions.size() == 1);
    }

    SECTION("trimming EVERY session") {
        q.Resize(0, 100);
        CHECK(q.SessionCount() == 0);
        CHECK(trimmedSessions.size() == 1000);
    }
}
