#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>
#include <vector>

using namespace drogon;
using namespace drogon::orm;

std::atomic<bool> should_exit(false);
std::atomic<size_t> transaction_error_count(0);

using TransactionFunction =
    std::function<void(std::shared_ptr<drogon::orm::Transaction> &)>;

void signalHandler(int signum)
{
  // trantor logger is signal unsafe
  /*
  WARNING: ThreadSanitizer: signal-unsafe call inside of a signal (pid=203115)
  #0 operator new(unsigned long) <null> (drogon_postgres_example+0x106c37) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #1 void std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>>::_M_construct<char const*>(char const*, char const*, std::forward_iterator_tag) /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/bits/basic_string.tcc:219:14 (drogon_postgres_example+0x2bd549) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #2 std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>>::basic_string(char const*, std::allocator<char> const&) /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/bits/basic_string.h:539:2 (drogon_postgres_example+0x2bd549)
  #3 trantor::Date::toFormattedString[abi:cxx11](bool) const /home/admin/db_race_test/build/_deps/drogon-src/trantor/trantor/utils/Date.cc:143:12 (drogon_postgres_example+0x2bd549)
  #4 trantor::Logger::formatTime() /home/admin/db_race_test/build/_deps/drogon-src/trantor/trantor/utils/Logger.cc:130:27 (drogon_postgres_example+0x2c1962) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #5 trantor::Logger::Logger(trantor::Logger::SourceFile, int) /home/admin/db_race_test/build/_deps/drogon-src/trantor/trantor/utils/Logger.cc:203:5 (drogon_postgres_example+0x2c1c35) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #6 signalHandler(int) /home/admin/db_race_test/main.cpp:21:5 (drogon_postgres_example+0x107b6a) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #7 __tsan::CallUserSignalHandler(__tsan::ThreadState*, bool, bool, int, __sanitizer::__sanitizer_siginfo*, void*) tsan_interceptors_posix.cpp.o (drogon_postgres_example+0x90145) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #8 std::future<drogon::orm::Result>::get() /usr/bin/../lib/gcc/x86_64-linux-gnu/11/../../../../include/c++/11/future:805:32 (drogon_postgres_example+0x253ad1) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #9 drogon::orm::internal::SqlBinder::exec() /home/admin/db_race_test/build/_deps/drogon-src/orm_lib/src/SqlBinder.cc:104:33 (drogon_postgres_example+0x24c20c) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #10 drogon::orm::Result drogon::orm::DbClient::execSqlSync<>(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> const&) /home/admin/db_race_test/build/_deps/drogon-src/orm_lib/inc/drogon/orm/DbClient.h:202:20 (drogon_postgres_example+0x109f44) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #11 runQueries(std::shared_ptr<drogon::orm::DbClient>, std::atomic<bool>&) /home/admin/db_race_test/main.cpp:95:35 (drogon_postgres_example+0x10853a) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)
  #12 main /home/admin/db_race_test/main.cpp:140:5 (drogon_postgres_example+0x108b46) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0)

SUMMARY: ThreadSanitizer: signal-unsafe call inside of a signal (/home/admin/db_race_test/build/drogon_postgres_example+0x106c37) (BuildId: d3d53c8c0b7a67929faa6375848cf31fed2730f0) in operator new(unsigned long)
  */
  // LOG_INFO << "Signal " << signum << " received. Shutting down...";
  should_exit.store(true);
}

void runTransaction(std::shared_ptr<DbClient> client,
                    const TransactionFunction &func,
                    std::chrono::milliseconds timeout)
{
  std::promise<bool> transaction_done_result;
  auto transaction_result_future = transaction_done_result.get_future();
  {
    auto transaction = client->newTransaction(
        [&](bool success)
        { transaction_done_result.set_value(success); });

    try
    {
      func(transaction);
    }
    catch (const DrogonDbException &e)
    {
      LOG_ERROR << "DrogonDbException: " << e.base().what();
      transaction->rollback();
      throw;
    }
    catch (const std::exception &e)
    {
      LOG_ERROR << "std::exception: " << e.what();
      transaction->rollback();
      throw;
    }
  }

  auto status = transaction_result_future.wait_for(timeout);
  if (status == std::future_status::timeout)
  {
    LOG_ERROR << "Transaction timeout error";
  }
}

void runTransactions(std::shared_ptr<DbClient> client)
{
  int transactions = 0;
  while (!should_exit)
  {
    try
    {
      runTransaction(
          client,
          [&](auto &transaction)
          {
            auto result = transaction->execSqlSync("SELECT pg_sleep(2);");
            transaction->execSqlSync("SELECT 1;");
            std::this_thread::sleep_for(std::chrono::milliseconds(5000));
            transaction->execSqlSync("SELECT pg_sleep(2);");
          },
          std::chrono::seconds(60));
    }
    catch (const DrogonDbException &e)
    {
      LOG_ERROR << "Skipped transaction DrogonDbException: " << e.base().what();
    }
    catch (const std::exception &e)
    {
      LOG_INFO << "Skipped transaction thread - std::exception: " << e.what();
    }

    transactions++;

    if (transactions % 100 == 0)
    {
      LOG_INFO << "Thread  executed " << transactions << " transactions";
    }
  }

  LOG_INFO << "Thread exiting after " << transactions << " transactions";

  // At this moment postgres is running and should not generate errors
  try
  {
    runTransaction(
        client,
        [&](auto &transaction)
        {
          transaction->execSqlSync("SELECT 1;");
          auto result = transaction->execSqlSync("SELECT pg_sleep(2);");
          transaction->execSqlSync("SELECT 1;");
          std::this_thread::sleep_for(std::chrono::milliseconds(5000));
          transaction->execSqlSync("SELECT pg_sleep(2);");
          transaction->execSqlSync("SELECT 1;");
        },
        std::chrono::seconds(60));
  }
  catch (const DrogonDbException &e)
  {
    LOG_ERROR
        << "Last transaction Transaction thread failed  DrogonDbException: "
        << e.base().what();
    ++transaction_error_count;
  }
  catch (const std::exception &e)
  {
    LOG_ERROR << "Last transaction Transaction thread failed - std::exception: "
              << e.what();
    ++transaction_error_count;
  }

  LOG_INFO << "Done";
}

int main()
{
  // Set up signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  LOG_INFO << "Connected to database. Starting query loop...";
  const int numTransactionThreads = 32;
  std::vector<std::thread> threads;
  for (int t = 0; t < numTransactionThreads; ++t)
  {
    auto client = DbClient::newPgClient(
        "postgresql://postgres:postgres@localhost:25432/mydb", 1);
    client->setTimeout(20);
    threads.emplace_back(runTransactions, client);
  }

  // Wait for all worker threads to complete
  for (auto &thread : threads)
  {
    if (thread.joinable())
    {
      thread.join();
    }
  }

  if (transaction_error_count == 0)
  {
    LOG_INFO << "All threads have stopped. Shutting down...";
    return 0;
  }
  else
  {
    LOG_ERROR << "Got transactions " << transaction_error_count.load()
              << " errors";
    return -1;
  }
}
