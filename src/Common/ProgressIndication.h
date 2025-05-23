#pragma once

#include <IO/Progress.h>
#include <Interpreters/Context_fwd.h>
#include <base/types.h>
#include <Common/Stopwatch.h>
#include <Common/EventRateMeter.h>

#include <iostream>
#include <mutex>
#include <unistd.h>
#include <unordered_map>

namespace DB
{

class WriteBufferFromFileDescriptor;

struct ThreadEventData
{
    UInt64 time() const noexcept { return user_ms + system_ms; }

    UInt64 user_ms      = 0;
    UInt64 system_ms    = 0;
    UInt64 memory_usage = 0;

    // -1 used as flag 'is not shown for old servers'
    Int64 peak_memory_usage = -1;
};

using HostToTimesMap = std::unordered_map<String, ThreadEventData>;

class ProgressIndication
{
public:

    explicit ProgressIndication
    (
        std::ostream & output_stream_ = std::cout,
        int in_fd_ = STDIN_FILENO,
        int err_fd_ = STDERR_FILENO
    )
        : output_stream(output_stream_),
        in_fd(in_fd_),
        err_fd(err_fd_)
    {
    }

    /// Write progress bar.
    void writeProgress(WriteBufferFromFileDescriptor & message, std::unique_lock<std::mutex> & message_lock);
    void clearProgressOutput(WriteBufferFromFileDescriptor & message, std::unique_lock<std::mutex> & message_lock);

    /// Write summary.
    void writeFinalProgress();

    /// Reset progress values.
    void resetProgress();

    /// Update Progress object. It can be updated from:
    /// 1. onProgress in clickhouse-client;
    /// 2. ProgressCallback via setProgressCallback methrod in:
    ///    - context (used in clickhouse-local, can also be added in arbitrary place)
    ///    - ISource (also in streams)
    ///    - readBufferFromFileDescriptor (for file processing progress)
    bool updateProgress(const Progress & value);

    /// In some cases there is a need to update progress value, when there is no access to progress_inidcation object.
    /// In this case it is added via context.
    /// `write_progress_on_update` is needed to write progress for loading files data via pipe in non-interactive mode.
    void setFileProgressCallback(ContextMutablePtr context, WriteBufferFromFileDescriptor & message, std::mutex & message_mutex);

    /// How much seconds passed since query execution start.
    double elapsedSeconds() const { return getElapsedNanoseconds() / 1e9; }

    struct MemoryUsage
    {
        UInt64 total = 0;
        UInt64 max   = 0;
        Int64 peak  = -1;
    };

    MemoryUsage getMemoryUsage() const;

    void updateThreadEventData(HostToTimesMap & new_hosts_data);

private:
    double getCPUUsage();

    UInt64 getElapsedNanoseconds() const;

    /// This flag controls whether to show the progress bar. We start showing it after
    /// the query has been executing for 0.5 seconds, and is still less than half complete.
    bool show_progress_bar = false;

    /// Width of how much has been printed currently into stderr. Used to define size of progress bar and
    /// to check whether progress output needs to be cleared.
    size_t written_progress_chars = 0;

    /// The server periodically sends information about how much data was read since last time.
    /// This information is stored here.
    Progress progress;

    /// Track query execution time on client.
    Stopwatch watch;

    bool write_progress_on_update = false;

    EventRateMeter cpu_usage_meter{static_cast<double>(clock_gettime_ns()), 2'000'000'000 /*ns*/, 4}; // average cpu utilization last 2 second, skip first 4 points
    HostToTimesMap hosts_data;
    /// In case of all of the above:
    /// - clickhouse-local
    /// - input_format_parallel_parsing=true
    /// - write_progress_on_update=true
    ///
    /// It is possible concurrent access to the following:
    /// - writeProgress() (class properties) (guarded with progress_mutex)
    /// - hosts_data/cpu_usage_meter (guarded with profile_events_mutex)
    ///
    /// It is also possible to have more races if query is cancelled, so that clearProgressOutput() is called concurrently
    mutable std::mutex profile_events_mutex;
    mutable std::mutex progress_mutex;

    std::ostream & output_stream;
    int in_fd;
    int err_fd;
};

}
