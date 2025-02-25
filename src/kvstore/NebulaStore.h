/* Copyright (c) 2018 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#ifndef KVSTORE_NEBULASTORE_H_
#define KVSTORE_NEBULASTORE_H_

#include "base/Base.h"
#include <gtest/gtest_prod.h>
#include <folly/RWSpinLock.h>
#include "kvstore/raftex/RaftexService.h"
#include "kvstore/KVStore.h"
#include "kvstore/PartManager.h"
#include "kvstore/Part.h"
#include "kvstore/KVEngine.h"

namespace nebula {
namespace kvstore {

struct SpacePartInfo {
    ~SpacePartInfo() {
        parts_.clear();
        engines_.clear();
        LOG(INFO) << "~SpacePartInfo()";
    }

    std::unordered_map<PartitionID, std::shared_ptr<Part>> parts_;
    std::vector<std::unique_ptr<KVEngine>> engines_;
};


class NebulaStore : public KVStore, public Handler {
    FRIEND_TEST(NebulaStoreTest, SimpleTest);
    FRIEND_TEST(NebulaStoreTest, PartsTest);
    FRIEND_TEST(NebulaStoreTest, ThreeCopiesTest);

public:
    NebulaStore(KVOptions options,
                std::shared_ptr<folly::IOThreadPoolExecutor> ioPool,
                HostAddr serviceAddr,
                std::shared_ptr<folly::Executor> workers)
            : ioPool_(ioPool)
            , storeSvcAddr_(serviceAddr)
            , workers_(workers)
            , raftAddr_(getRaftAddr(serviceAddr))
            , options_(std::move(options)) {
    }

    ~NebulaStore();

    // Calculate the raft service address based on the storage service address
    static HostAddr getRaftAddr(HostAddr srvcAddr) {
        if (srvcAddr == HostAddr(0, 0)) {
            return srvcAddr;
        }
        return HostAddr(srvcAddr.first, srvcAddr.second + 1);
    }

    static HostAddr getStoreAddr(HostAddr raftAddr) {
        if (raftAddr == HostAddr(0, 0)) {
            return raftAddr;
        }
        return HostAddr(raftAddr.first, raftAddr.second - 1);
    }

    // Pull meta information from the PartManager and initiate
    // the current store instance
    bool init();

    uint32_t capability() const override {
        return 0;
    }

    std::shared_ptr<folly::IOThreadPoolExecutor> getIoPool() const {
        return ioPool_;
    }

    std::shared_ptr<thread::GenericThreadPool> getWorkers() const {
        return bgWorkers_;
    }

    // Return the current leader
    ErrorOr<ResultCode, HostAddr> partLeader(GraphSpaceID spaceId, PartitionID partId) override;

    PartManager* partManager() const override {
        return options_.partMan_.get();
    }

    ResultCode get(GraphSpaceID spaceId,
                   PartitionID  partId,
                   const std::string& key,
                   std::string* value) override;

    ResultCode multiGet(GraphSpaceID spaceId,
                        PartitionID partId,
                        const std::vector<std::string>& keys,
                        std::vector<std::string>* values) override;

    // Get all results in range [start, end)
    ResultCode range(GraphSpaceID spaceId,
                     PartitionID  partId,
                     const std::string& start,
                     const std::string& end,
                     std::unique_ptr<KVIterator>* iter) override;

    // Get all results with prefix.
    ResultCode prefix(GraphSpaceID spaceId,
                      PartitionID  partId,
                      const std::string& prefix,
                      std::unique_ptr<KVIterator>* iter) override;

    // async batch put.
    void asyncMultiPut(GraphSpaceID spaceId,
                       PartitionID  partId,
                       std::vector<KV> keyValues,
                       KVCallback cb) override;

    void asyncRemove(GraphSpaceID spaceId,
                     PartitionID partId,
                     const std::string& key,
                     KVCallback cb) override;

    void asyncMultiRemove(GraphSpaceID spaceId,
                          PartitionID  partId,
                          std::vector<std::string> keys,
                          KVCallback cb) override;

    void asyncRemoveRange(GraphSpaceID spaceId,
                          PartitionID partId,
                          const std::string& start,
                          const std::string& end,
                          KVCallback cb) override;

    void asyncRemovePrefix(GraphSpaceID spaceId,
                           PartitionID partId,
                           const std::string& prefix,
                           KVCallback cb) override;

    void asyncAtomicOp(GraphSpaceID spaceId,
                       PartitionID partId,
                       raftex::AtomicOp op,
                       KVCallback cb) override;

    ErrorOr<ResultCode, std::shared_ptr<Part>> part(GraphSpaceID spaceId,
                                                    PartitionID partId) override;

    ResultCode ingest(GraphSpaceID spaceId) override;

    ResultCode setOption(GraphSpaceID spaceId,
                         const std::string& configKey,
                         const std::string& configValue);

    ResultCode setDBOption(GraphSpaceID spaceId,
                           const std::string& configKey,
                           const std::string& configValue);

    ResultCode compact(GraphSpaceID spaceId) override;

    ResultCode flush(GraphSpaceID spaceId) override;

    bool isLeader(GraphSpaceID spaceId, PartitionID partId);

private:
    /**
     * Implement four interfaces in Handler.
     * */
    void addSpace(GraphSpaceID spaceId) override;

    void addPart(GraphSpaceID spaceId, PartitionID partId) override;

    void removeSpace(GraphSpaceID spaceId) override;

    void removePart(GraphSpaceID spaceId, PartitionID partId) override;

    std::unique_ptr<KVEngine> newEngine(GraphSpaceID spaceId, const std::string& path);

    std::shared_ptr<Part> newPart(GraphSpaceID spaceId,
                                  PartitionID partId,
                                  KVEngine* engine);

    ErrorOr<ResultCode, KVEngine*> engine(GraphSpaceID spaceId, PartitionID partId);

    ErrorOr<ResultCode, std::shared_ptr<SpacePartInfo>> space(GraphSpaceID spaceId);

private:
    // The lock used to protect spaces_
    folly::RWSpinLock lock_;
    std::unordered_map<GraphSpaceID, std::shared_ptr<SpacePartInfo>> spaces_;

    std::shared_ptr<folly::IOThreadPoolExecutor> ioPool_;
    std::shared_ptr<thread::GenericThreadPool> bgWorkers_;
    HostAddr storeSvcAddr_;
    std::shared_ptr<folly::Executor> workers_;
    HostAddr raftAddr_;
    KVOptions options_;

    std::shared_ptr<raftex::RaftexService> raftService_;
};

}  // namespace kvstore
}  // namespace nebula
#endif  // KVSTORE_NEBULASTORE_H_

