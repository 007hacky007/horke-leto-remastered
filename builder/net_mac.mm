// HTTPS download through the system's NSURLSession (no bundled TLS library).
#include "pipeline.h"
#import <Foundation/Foundation.h>
#include <thread>

namespace hb {
void download(const std::string& url, const fs::path& target, const std::string& sha256, const Emit& emit) {
    if (fs::is_regular_file(target) && sha_file(target) == sha256) { emit("Download 100%"); return; }
    fs::create_directories(target.parent_path());
    __block NSError* failure = nil;
    __block NSURL* saved = nil;
    __block long status = 0;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    NSURL* address = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    NSURLSessionDownloadTask* task = [[NSURLSession sharedSession] downloadTaskWithURL:address
        completionHandler:^(NSURL* location, NSURLResponse* response, NSError* error) {
            failure = error;
            status = [(NSHTTPURLResponse*)response statusCode];
            if (location) {
                NSURL* keep = [NSURL fileURLWithPath:[NSString stringWithUTF8String:(target.string() + ".partial").c_str()]];
                [[NSFileManager defaultManager] removeItemAtURL:keep error:nil];
                if ([[NSFileManager defaultManager] moveItemAtURL:location toURL:keep error:nil]) saved = keep;
            }
            dispatch_semaphore_signal(done);
        }];
    [task resume];
    int last = -1;
    while (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC))) {
        if (cancelled) { [task cancel]; }
        int64_t total = task.countOfBytesExpectedToReceive, got = task.countOfBytesReceived;
        if (total > 0) {
            int percent = int(got * 100 / total);
            if (percent != last) { emit("Download " + std::to_string(percent) + "%"); last = percent; }
        }
    }
    check_cancelled();
    if (failure || !saved || status != 200)
        throw UserError("Výtvarný balíček nejde stáhnout. Zkontroluj připojení, nebo vyber stažený archiv.");
    fs::path partial = target.string() + ".partial";
    if (sha_file(partial) != sha256) { fs::remove(partial); throw UserError("Stažený balíček má jiný kontrolní součet; zkus to znovu."); }
    fs::rename(partial, target);
    emit("Download 100%");
}

uint64_t available_bytes(const fs::path& path) {
    std::error_code ec;
    uint64_t plain = fs::space(path, ec).available;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.string().c_str()]];
    NSNumber* value = nil;
    if ([url getResourceValue:&value forKey:NSURLVolumeAvailableCapacityForImportantUsageKey error:nil] && value)
        return std::max<uint64_t>(plain, value.unsignedLongLongValue);
    return plain;
}

bool url_reachable(const std::string& url) {
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]]];
    request.HTTPMethod = @"HEAD";
    request.timeoutInterval = 10;
    __block long status = 0;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [[[NSURLSession sharedSession] dataTaskWithRequest:request completionHandler:^(NSData*, NSURLResponse* response, NSError*) {
        status = [(NSHTTPURLResponse*)response statusCode];
        dispatch_semaphore_signal(done);
    }] resume];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
    return status == 200;
}

KeepAwake::KeepAwake() {
    NSActivityOptions options = NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityIdleSystemSleepDisabled;
    token = (__bridge_retained void*)[[NSProcessInfo processInfo] beginActivityWithOptions:options reason:@"Building Horke Leto"];
}
KeepAwake::~KeepAwake() {
    if (token) [[NSProcessInfo processInfo] endActivity:(__bridge_transfer id)token];
}
}
