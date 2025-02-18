// Copyright 2022 PingCAP, Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Storages/Transaction/LearnerRead.h>

#include "kvstore_helper.h"

namespace DB
{
namespace tests
{
TEST_F(RegionKVStoreTest, KVStoreFailRecovery)
try
{
    auto ctx = TiFlashTestEnv::getGlobalContext();
    KVStore & kvs = getKVS();
    {
        auto applied_index = 0;
        auto region_id = 1;
        {
            MockRaftStoreProxy::FailCond cond;

            proxy_instance->debugAddRegions(kvs, ctx.getTMTContext(), {1}, {{{RecordKVFormat::genKey(1, 0), RecordKVFormat::genKey(1, 10)}}});
            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            ASSERT_NE(r1, nullptr);
            ASSERT_NE(kvr1, nullptr);
            applied_index = r1->getLatestAppliedIndex();
            ASSERT_EQ(r1->getLatestAppliedIndex(), kvr1->appliedIndex());
            auto [index, term] = proxy_instance->normalWrite(region_id, {33}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
            proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index + 1);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index + 1);
            kvs.tryPersistRegion(region_id);
        }
        {
            const KVStore & kvs = reloadKVSFromDisk();
            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index + 1);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index + 1);
            ASSERT_EQ(kvr1->appliedIndex(), r1->getLatestCommitIndex());
        }
    }

    {
        auto applied_index = 0;
        auto region_id = 2;
        {
            proxy_instance->debugAddRegions(kvs, ctx.getTMTContext(), {2}, {{{RecordKVFormat::genKey(1, 10), RecordKVFormat::genKey(1, 20)}}});
            MockRaftStoreProxy::FailCond cond;
            cond.type = MockRaftStoreProxy::FailCond::Type::BEFORE_KVSTORE_WRITE;

            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            applied_index = r1->getLatestAppliedIndex();
            ASSERT_EQ(r1->getLatestAppliedIndex(), kvr1->appliedIndex());
            auto [index, term] = proxy_instance->normalWrite(region_id, {34}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
            // KVStore failed before write and advance.
            proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index);
            kvs.tryPersistRegion(region_id);
        }
        {
            KVStore & kvs = reloadKVSFromDisk();
            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index);
            ASSERT_EQ(kvr1->appliedIndex(), r1->getLatestCommitIndex() - 1);
            proxy_instance->replay(kvs, ctx.getTMTContext(), region_id, r1->getLatestCommitIndex());
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index + 1);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index + 1);
        }
    }

    {
        auto applied_index = 0;
        auto region_id = 3;
        {
            proxy_instance->debugAddRegions(kvs, ctx.getTMTContext(), {3}, {{{RecordKVFormat::genKey(1, 30), RecordKVFormat::genKey(1, 40)}}});
            MockRaftStoreProxy::FailCond cond;
            cond.type = MockRaftStoreProxy::FailCond::Type::BEFORE_KVSTORE_ADVANCE;

            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            applied_index = r1->getLatestAppliedIndex();
            ASSERT_EQ(r1->getLatestAppliedIndex(), kvr1->appliedIndex());
            auto [index, term] = proxy_instance->normalWrite(region_id, {34}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
            // KVStore failed before advance.
            proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index);
            kvs.tryPersistRegion(region_id);
        }
        {
            KVStore & kvs = reloadKVSFromDisk();
            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index);
            ASSERT_EQ(kvr1->appliedIndex(), r1->getLatestCommitIndex() - 1);
            EXPECT_THROW(proxy_instance->replay(kvs, ctx.getTMTContext(), region_id, r1->getLatestCommitIndex()), Exception);
        }
    }

    {
        auto applied_index = 0;
        auto region_id = 4;
        {
            proxy_instance->debugAddRegions(kvs, ctx.getTMTContext(), {4}, {{{RecordKVFormat::genKey(1, 50), RecordKVFormat::genKey(1, 60)}}});
            MockRaftStoreProxy::FailCond cond;
            cond.type = MockRaftStoreProxy::FailCond::Type::BEFORE_PROXY_ADVANCE;

            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            applied_index = r1->getLatestAppliedIndex();
            ASSERT_EQ(r1->getLatestAppliedIndex(), kvr1->appliedIndex());
            LOG_INFO(Logger::get(), "applied_index {}", applied_index);
            auto [index, term] = proxy_instance->normalWrite(region_id, {35}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
            // KVStore succeed. Proxy failed before advance.
            proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index + 1);
            kvs.tryPersistRegion(region_id);
        }
        {
            MockRaftStoreProxy::FailCond cond;
            KVStore & kvs = reloadKVSFromDisk();
            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index + 1);
            ASSERT_EQ(kvr1->appliedIndex(), r1->getLatestCommitIndex());
            // Proxy shall replay from handle 35.
            proxy_instance->replay(kvs, ctx.getTMTContext(), region_id, r1->getLatestCommitIndex());
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index + 1);
            auto [index, term] = proxy_instance->normalWrite(region_id, {36}, {"v2"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
            proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index + 2);
        }
    }
}
CATCH

TEST_F(RegionKVStoreTest, KVStoreInvalidWrites)
try
{
    auto ctx = TiFlashTestEnv::getGlobalContext();
    {
        auto region_id = 1;
        {
            initStorages();
            KVStore & kvs = getKVS();
            proxy_instance->bootstrapTable(ctx, kvs, ctx.getTMTContext());
            proxy_instance->bootstrapWithRegion(kvs, ctx.getTMTContext(), region_id, std::nullopt);

            MockRaftStoreProxy::FailCond cond;

            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            ASSERT_NE(r1, nullptr);
            ASSERT_NE(kvr1, nullptr);
            ASSERT_EQ(r1->getLatestAppliedIndex(), kvr1->appliedIndex());
            {
                r1->getLatestAppliedIndex();
                // This key has empty PK which is actually truncated.
                std::string k = "7480000000000001FFBD5F720000000000FAF9ECEFDC3207FFFC";
                std::string v = "4486809092ACFEC38906";
                auto strKey = Redact::hexStringToKey(k.data(), k.size());
                auto strVal = Redact::hexStringToKey(v.data(), v.size());

                auto [index, term] = proxy_instance->rawWrite(region_id, {strKey}, {strVal}, {WriteCmdType::Put}, {ColumnFamilyType::Write});
                EXPECT_THROW(proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index), Exception);
                UNUSED(term);
                EXPECT_THROW(ReadRegionCommitCache(kvr1, true), Exception);
            }
        }
    }
}
CATCH

TEST_F(RegionKVStoreTest, KVStoreAdminCommands)
try
{
    auto ctx = TiFlashTestEnv::getGlobalContext();
    // CompactLog and passive persistence
    {
        KVStore & kvs = getKVS();
        UInt64 region_id = 1;
        {
            auto applied_index = 0;
            proxy_instance->bootstrapWithRegion(kvs, ctx.getTMTContext(), region_id, std::nullopt);
            MockRaftStoreProxy::FailCond cond;

            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            ASSERT_NE(r1, nullptr);
            ASSERT_NE(kvr1, nullptr);
            applied_index = r1->getLatestAppliedIndex();
            ASSERT_EQ(r1->getLatestAppliedIndex(), kvr1->appliedIndex());
            auto [index, term] = proxy_instance->normalWrite(region_id, {33}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
            proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index + 1);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index + 1);

            kvr1->markCompactLog();
            kvs.setRegionCompactLogConfig(0, 0, 0);
            auto [index2, term2] = proxy_instance->compactLog(region_id, index);
            // In tryFlushRegionData we will call handleWriteRaftCmd, which will already cause an advance.
            // Notice kvs is not tmt->getKVStore(), so we can't use the ProxyFFI version.
            ASSERT_TRUE(kvs.tryFlushRegionData(region_id, false, true, ctx.getTMTContext(), index2, term));
            proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index2);
            ASSERT_EQ(r1->getLatestAppliedIndex(), applied_index + 2);
            ASSERT_EQ(kvr1->appliedIndex(), applied_index + 2);
        }
        {
            proxy_instance->normalWrite(region_id, {34}, {"v2"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
            // There shall be data to flush.
            ASSERT_EQ(kvs.needFlushRegionData(region_id, ctx.getTMTContext()), true);
            // If flush fails, and we don't insist a success.
            FailPointHelper::enableFailPoint(FailPoints::force_fail_in_flush_region_data);
            ASSERT_EQ(kvs.tryFlushRegionData(region_id, false, false, ctx.getTMTContext(), 0, 0), false);
            FailPointHelper::disableFailPoint(FailPoints::force_fail_in_flush_region_data);
            // Force flush until succeed only for testing.
            ASSERT_EQ(kvs.tryFlushRegionData(region_id, false, true, ctx.getTMTContext(), 0, 0), true);
            // Non existing region.
            // Flush and CompactLog will not panic.
            ASSERT_EQ(kvs.tryFlushRegionData(1999, false, true, ctx.getTMTContext(), 0, 0), true);
            raft_cmdpb::AdminRequest request;
            raft_cmdpb::AdminResponse response;
            request.mutable_compact_log();
            request.set_cmd_type(::raft_cmdpb::AdminCmdType::CompactLog);
            ASSERT_EQ(kvs.handleAdminRaftCmd(raft_cmdpb::AdminRequest{request}, std::move(response), 1999, 22, 6, ctx.getTMTContext()), EngineStoreApplyRes::NotFound);
        }
    }
    {
        KVStore & kvs = getKVS();
        UInt64 region_id = 2;
        proxy_instance->debugAddRegions(kvs, ctx.getTMTContext(), {region_id}, {{{RecordKVFormat::genKey(1, 10), RecordKVFormat::genKey(1, 20)}}});

        // InvalidAdmin
        raft_cmdpb::AdminRequest request;
        raft_cmdpb::AdminResponse response;

        request.set_cmd_type(::raft_cmdpb::AdminCmdType::InvalidAdmin);
        try
        {
            kvs.handleAdminRaftCmd(std::move(request), std::move(response), region_id, 110, 6, ctx.getTMTContext());
            ASSERT_TRUE(false);
        }
        catch (Exception & e)
        {
            ASSERT_EQ(e.message(), "unsupported admin command type InvalidAdmin");
        }
    }
    {
        // All "useless" commands.
        KVStore & kvs = getKVS();
        UInt64 region_id = 3;
        proxy_instance->debugAddRegions(kvs, ctx.getTMTContext(), {region_id}, {{{RecordKVFormat::genKey(1, 20), RecordKVFormat::genKey(1, 30)}}});
        raft_cmdpb::AdminRequest request;
        raft_cmdpb::AdminResponse response2;
        raft_cmdpb::AdminResponse response;

        request.mutable_compact_log();
        request.set_cmd_type(::raft_cmdpb::AdminCmdType::CompactLog);
        response = response2;
        ASSERT_EQ(kvs.handleAdminRaftCmd(raft_cmdpb::AdminRequest{request}, std::move(response), region_id, 22, 6, ctx.getTMTContext()), EngineStoreApplyRes::Persist);

        response = response2;
        ASSERT_EQ(kvs.handleAdminRaftCmd(raft_cmdpb::AdminRequest{request}, std::move(response), region_id, 23, 6, ctx.getTMTContext()), EngineStoreApplyRes::Persist);

        response = response2;
        ASSERT_EQ(kvs.handleAdminRaftCmd(raft_cmdpb::AdminRequest{request}, std::move(response), 8192, 5, 6, ctx.getTMTContext()), EngineStoreApplyRes::NotFound);

        request.set_cmd_type(::raft_cmdpb::AdminCmdType::ComputeHash);
        response = response2;
        ASSERT_EQ(kvs.handleAdminRaftCmd(raft_cmdpb::AdminRequest{request}, std::move(response), region_id, 24, 6, ctx.getTMTContext()), EngineStoreApplyRes::None);

        request.set_cmd_type(::raft_cmdpb::AdminCmdType::VerifyHash);
        response = response2;
        ASSERT_EQ(kvs.handleAdminRaftCmd(raft_cmdpb::AdminRequest{request}, std::move(response), region_id, 25, 6, ctx.getTMTContext()), EngineStoreApplyRes::None);

        {
            kvs.setRegionCompactLogConfig(0, 0, 0);
            request.set_cmd_type(::raft_cmdpb::AdminCmdType::CompactLog);
            ASSERT_EQ(kvs.handleAdminRaftCmd(std::move(request), std::move(response2), region_id, 26, 6, ctx.getTMTContext()), EngineStoreApplyRes::Persist);
        }
    }
}
CATCH

static void validate(KVStore & kvs, std::unique_ptr<MockRaftStoreProxy> & proxy_instance, UInt64 region_id, MockRaftStoreProxy::Cf & cf_data, ColumnFamilyType cf, int sst_size, int key_count)
{
    auto kvr1 = kvs.getRegion(region_id);
    auto r1 = proxy_instance->getRegion(region_id);
    auto proxy_helper = proxy_instance->generateProxyHelper();
    auto ssts = cf_data.ssts();
    ASSERT_EQ(ssts.size(), sst_size);
    auto make_inner_func = [](const TiFlashRaftProxyHelper * proxy_helper, SSTView snap, SSTReader::RegionRangeFilter range) -> std::unique_ptr<MonoSSTReader> {
        auto parsedKind = MockRaftStoreProxy::parseSSTViewKind(buffToStrView(snap.path));
        auto reader = std::make_unique<MonoSSTReader>(proxy_helper, snap, range);
        assert(reader->sst_format_kind() == parsedKind);
        return reader;
    };
    MultiSSTReader<MonoSSTReader, SSTView> reader{proxy_helper.get(), cf, make_inner_func, ssts, Logger::get(), kvr1->getRange()};

    size_t counter = 0;
    while (reader.remained())
    {
        // repeatedly remained are called.
        reader.remained();
        reader.remained();
        counter++;
        auto v = std::string(reader.valueView().data);
        ASSERT_EQ(v, "v" + std::to_string(counter));
        reader.next();
    }
    ASSERT_EQ(counter, key_count);
}

TEST_F(RegionKVStoreTest, KVStoreSnapshotV1)
try
{
    auto ctx = TiFlashTestEnv::getGlobalContext();
    ASSERT_NE(proxy_helper->sst_reader_interfaces.fn_key, nullptr);
    {
        UInt64 region_id = 1;
        TableID table_id;
        {
            initStorages();
            KVStore & kvs = getKVS();
            table_id = proxy_instance->bootstrapTable(ctx, kvs, ctx.getTMTContext());
            LOG_INFO(&Poco::Logger::get("Test"), "generated table_id {}", table_id);
            proxy_instance->bootstrapWithRegion(kvs, ctx.getTMTContext(), region_id, std::nullopt);
            auto kvr1 = kvs.getRegion(region_id);
            auto r1 = proxy_instance->getRegion(region_id);
            ctx.getTMTContext().getRegionTable().updateRegion(*kvr1);
            {
                // Only one file
                MockSSTReader::getMockSSTData().clear();
                MockRaftStoreProxy::Cf default_cf{900, 800, ColumnFamilyType::Default};
                default_cf.insert(1, "v1");
                default_cf.insert(2, "v2");
                default_cf.finish_file();
                default_cf.freeze();
                validate(kvs, proxy_instance, region_id, default_cf, ColumnFamilyType::Default, 1, 2);
            }
            {
                // Empty
                MockSSTReader::getMockSSTData().clear();
                MockRaftStoreProxy::Cf default_cf{901, 800, ColumnFamilyType::Default};
                default_cf.finish_file();
                default_cf.freeze();
                validate(kvs, proxy_instance, region_id, default_cf, ColumnFamilyType::Default, 1, 0);
            }
            {
                // Multiple files
                MockSSTReader::getMockSSTData().clear();
                MockRaftStoreProxy::Cf default_cf{902, 800, ColumnFamilyType::Default};
                default_cf.insert(1, "v1");
                default_cf.finish_file();
                default_cf.insert(2, "v2");
                default_cf.finish_file();
                default_cf.insert(3, "v3");
                default_cf.insert(4, "v4");
                default_cf.finish_file();
                default_cf.insert(5, "v5");
                default_cf.insert(6, "v6");
                default_cf.finish_file();
                default_cf.insert(7, "v7");
                default_cf.finish_file();
                default_cf.freeze();
                validate(kvs, proxy_instance, region_id, default_cf, ColumnFamilyType::Default, 5, 7);
            }

            {
                // Test of ingesting multiple files with MultiSSTReader.
                MockSSTReader::getMockSSTData().clear();
                MockRaftStoreProxy::Cf default_cf{region_id, table_id, ColumnFamilyType::Default};
                default_cf.insert(1, "v1");
                default_cf.insert(2, "v2");
                default_cf.finish_file();
                default_cf.insert(3, "v3");
                default_cf.insert(4, "v4");
                default_cf.insert(5, "v5");
                default_cf.finish_file();
                default_cf.insert(6, "v6");
                default_cf.finish_file();
                default_cf.freeze();
                validate(kvs, proxy_instance, region_id, default_cf, ColumnFamilyType::Default, 3, 6);

                kvs.mutProxyHelperUnsafe()->sst_reader_interfaces = make_mock_sst_reader_interface();
                proxy_instance->snapshot(kvs, ctx.getTMTContext(), region_id, {default_cf}, 0, 0);

                MockRaftStoreProxy::FailCond cond;
                {
                    auto [index, term] = proxy_instance->normalWrite(region_id, {9}, {"v9"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                    proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
                }
                {
                    // Test if write succeed.
                    auto [index, term] = proxy_instance->normalWrite(region_id, {1}, {"fv1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                    EXPECT_THROW(proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index), Exception);
                }
            }
            {
                // Test of ingesting single file with MultiSSTReader.
                MockSSTReader::getMockSSTData().clear();
                MockRaftStoreProxy::Cf default_cf{region_id, table_id, ColumnFamilyType::Default};
                default_cf.insert(10, "v10");
                default_cf.insert(11, "v11");
                default_cf.finish_file();
                default_cf.freeze();

                kvs.mutProxyHelperUnsafe()->sst_reader_interfaces = make_mock_sst_reader_interface();
                proxy_instance->snapshot(kvs, ctx.getTMTContext(), region_id, {default_cf}, 0, 0);

                MockRaftStoreProxy::FailCond cond;
                {
                    auto [index, term] = proxy_instance->normalWrite(region_id, {19}, {"v19"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                    proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
                }
                {
                    // Test if write succeed.
                    auto [index, term] = proxy_instance->normalWrite(region_id, {10}, {"v10"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                    EXPECT_THROW(proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index), Exception);
                }
            }
            {
                // Test of ingesting multiple cfs with MultiSSTReader.
                MockSSTReader::getMockSSTData().clear();
                MockRaftStoreProxy::Cf default_cf{region_id, table_id, ColumnFamilyType::Default};
                default_cf.insert(20, "v20");
                default_cf.insert(21, "v21");
                default_cf.finish_file();
                default_cf.freeze();
                MockRaftStoreProxy::Cf write_cf{region_id, table_id, ColumnFamilyType::Write};
                write_cf.insert(20, "v20");
                write_cf.insert(21, "v21");
                write_cf.finish_file();
                write_cf.freeze();

                kvs.mutProxyHelperUnsafe()->sst_reader_interfaces = make_mock_sst_reader_interface();
                proxy_instance->snapshot(kvs, ctx.getTMTContext(), region_id, {default_cf, write_cf}, 0, 0);

                MockRaftStoreProxy::FailCond cond;
                {
                    // Test if write succeed.
                    auto [index, term] = proxy_instance->normalWrite(region_id, {20}, {"v20"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                    // Found existing key ...
                    EXPECT_THROW(proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index), Exception);
                }
            }
            {
                // Test of ingesting duplicated key with the same value.
                MockSSTReader::getMockSSTData().clear();
                MockRaftStoreProxy::Cf default_cf{region_id, table_id, ColumnFamilyType::Default};
                default_cf.insert(21, "v21");
                default_cf.insert(21, "v21");
                default_cf.finish_file();
                default_cf.freeze();
                MockRaftStoreProxy::Cf write_cf{region_id, table_id, ColumnFamilyType::Write};
                write_cf.insert(21, "v21");
                write_cf.insert(21, "v21");
                write_cf.finish_file();
                write_cf.freeze();

                kvs.mutProxyHelperUnsafe()->sst_reader_interfaces = make_mock_sst_reader_interface();
                // Shall not panic.
                proxy_instance->snapshot(kvs, ctx.getTMTContext(), region_id, {default_cf, write_cf}, 0, 0);
            }
            {
                // Test of ingesting duplicated key with different values.
                MockSSTReader::getMockSSTData().clear();
                MockRaftStoreProxy::Cf default_cf{region_id, table_id, ColumnFamilyType::Default};
                default_cf.insert(21, "v21");
                default_cf.insert(21, "v22");
                default_cf.finish_file();
                default_cf.freeze();
                MockRaftStoreProxy::Cf write_cf{region_id, table_id, ColumnFamilyType::Write};
                write_cf.insert(21, "v21");
                write_cf.insert(21, "v21");
                write_cf.finish_file();
                write_cf.freeze();

                kvs.mutProxyHelperUnsafe()->sst_reader_interfaces = make_mock_sst_reader_interface();
                // Found existing key ...
                EXPECT_THROW(proxy_instance->snapshot(kvs, ctx.getTMTContext(), region_id, {default_cf, write_cf}, 0, 0), Exception);
            }
        }
    }
}
CATCH


TEST_F(RegionKVStoreTest, KVStoreSnapshotV2Extra)
try
{
    auto ctx = TiFlashTestEnv::getGlobalContext();
    ASSERT_NE(proxy_helper->sst_reader_interfaces.fn_key, nullptr);
    UInt64 region_id = 2;
    TableID table_id;
    {
        std::string start_str = "7480000000000000FF795F720000000000FA";
        std::string end_str = "7480000000000000FF795F720380000000FF0000004003800000FF0000017FCC000000FC";
        auto start = Redact::hexStringToKey(start_str.data(), start_str.size());
        auto end = Redact::hexStringToKey(end_str.data(), end_str.size());

        initStorages();
        KVStore & kvs = getKVS();
        table_id = proxy_instance->bootstrapTable(ctx, kvs, ctx.getTMTContext());
        proxy_instance->bootstrapWithRegion(kvs, ctx.getTMTContext(), region_id, std::make_pair(start, end));
        auto kvr1 = kvs.getRegion(region_id);
        auto r1 = proxy_instance->getRegion(region_id);
    }
    KVStore & kvs = getKVS();
    {
        std::string k = "7480000000000000FF795F720380000000FF0000026303800000FF0000017801000000FCF9DE534E2797FB83";
        MockRaftStoreProxy::Cf write_cf{region_id, table_id, ColumnFamilyType::Write};
        write_cf.insert_raw(Redact::hexStringToKey(k.data(), k.size()), "v1");
        write_cf.finish_file(SSTFormatKind::KIND_TABLET);
        write_cf.freeze();
        validate(kvs, proxy_instance, region_id, write_cf, ColumnFamilyType::Write, 1, 0);
    }
}
CATCH

TEST_F(RegionKVStoreTest, KVStoreSnapshotV2Basic)
try
{
    auto ctx = TiFlashTestEnv::getGlobalContext();
    ASSERT_NE(proxy_helper->sst_reader_interfaces.fn_key, nullptr);
    UInt64 region_id = 1;
    TableID table_id;
    {
        region_id = 2;
        initStorages();
        KVStore & kvs = getKVS();
        table_id = proxy_instance->bootstrapTable(ctx, kvs, ctx.getTMTContext());
        proxy_instance->bootstrapWithRegion(kvs, ctx.getTMTContext(), region_id, std::nullopt);
        auto kvr1 = kvs.getRegion(region_id);
        auto r1 = proxy_instance->getRegion(region_id);
        {
            // Shall filter out of range kvs.
            // RegionDefaultCFDataTrait will "reconstruct" TiKVKey, without table_id, it is correct since different tables ares in different regions.
            // so we may find conflict if we set the same handle_id here.
            // hall we remove this constraint?
            auto klo = RecordKVFormat::genKey(table_id - 1, 1, 111);
            auto klo2 = RecordKVFormat::genKey(table_id - 1, 9999, 222);
            auto kro = RecordKVFormat::genKey(table_id + 1, 0, 333);
            auto kro2 = RecordKVFormat::genKey(table_id + 1, 2, 444);
            auto kin1 = RecordKVFormat::genKey(table_id, 0, 0);
            auto kin2 = RecordKVFormat::genKey(table_id, 5, 0);
            MockSSTReader::getMockSSTData().clear();
            MockRaftStoreProxy::Cf default_cf{region_id, table_id, ColumnFamilyType::Default};
            default_cf.insert_raw(klo, "v1");
            default_cf.insert_raw(klo2, "v1");
            default_cf.insert_raw(kin1, "v1");
            default_cf.insert_raw(kin2, "v2");
            default_cf.insert_raw(kro, "v1");
            default_cf.insert_raw(kro2, "v1");
            default_cf.finish_file(SSTFormatKind::KIND_TABLET);
            default_cf.freeze();
            MockRaftStoreProxy::Cf write_cf{region_id, table_id, ColumnFamilyType::Write};
            write_cf.insert_raw(klo, "v1");
            write_cf.insert_raw(klo2, "v1");
            write_cf.insert_raw(kro, "v1");
            write_cf.insert_raw(kro2, "v1");
            write_cf.finish_file(SSTFormatKind::KIND_TABLET);
            write_cf.freeze();
            validate(kvs, proxy_instance, region_id, default_cf, ColumnFamilyType::Default, 1, 2);
            validate(kvs, proxy_instance, region_id, write_cf, ColumnFamilyType::Write, 1, 0);

            proxy_helper->sst_reader_interfaces = make_mock_sst_reader_interface();
            proxy_instance->snapshot(kvs, ctx.getTMTContext(), region_id, {default_cf, write_cf}, 0, 0);
            MockRaftStoreProxy::FailCond cond;
            {
                auto [index, term] = proxy_instance->rawWrite(region_id, {klo}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            }
            {
                auto [index, term] = proxy_instance->rawWrite(region_id, {klo2}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            }
            {
                auto [index, term] = proxy_instance->rawWrite(region_id, {kro}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            }
            {
                auto [index, term] = proxy_instance->rawWrite(region_id, {kro2}, {"v1"}, {WriteCmdType::Put}, {ColumnFamilyType::Default});
                proxy_instance->doApply(kvs, ctx.getTMTContext(), cond, region_id, index);
            }
        }
    }
}
CATCH

TEST_F(RegionKVStoreTest, LearnerRead)
try
{
    auto ctx = TiFlashTestEnv::getGlobalContext();
    auto region_id = 1;
    KVStore & kvs = getKVS();
    ctx.getTMTContext().debugSetKVStore(kvstore);
    initStorages();

    ctx.getTMTContext().debugSetWaitIndexTimeout(1);

    startReadIndexUtils(ctx);
    SCOPE_EXIT({
        stopReadIndexUtils();
    });

    auto table_id = proxy_instance->bootstrapTable(ctx, kvs, ctx.getTMTContext());
    proxy_instance->bootstrapWithRegion(kvs, ctx.getTMTContext(), region_id, std::nullopt);
    auto kvr1 = kvs.getRegion(region_id);
    ctx.getTMTContext().getRegionTable().updateRegion(*kvr1);

    std::vector<std::string> keys{RecordKVFormat::genKey(table_id, 3).toString(), RecordKVFormat::genKey(table_id, 3, 5).toString(), RecordKVFormat::genKey(table_id, 3, 8).toString()};
    std::vector<std::string> vals({RecordKVFormat::encodeLockCfValue(RecordKVFormat::CFModifyFlag::PutFlag, "PK", 3, 20).toString(), TiKVValue("value1").toString(), RecordKVFormat::encodeWriteCfValue(RecordKVFormat::CFModifyFlag::PutFlag, 5).toString()});
    auto ops = std::vector<ColumnFamilyType>{
        ColumnFamilyType::Lock,
        ColumnFamilyType::Default,
        ColumnFamilyType::Write,
    };
    auto [index, term] = proxy_instance->rawWrite(region_id, std::move(keys), std::move(vals), {WriteCmdType::Put, WriteCmdType::Put, WriteCmdType::Put}, std::move(ops));
    ASSERT_EQ(index, 6);
    ASSERT_EQ(kvr1->appliedIndex(), 5);
    ASSERT_EQ(term, 5);

    auto mvcc_query_info = MvccQueryInfo(false, 10);
    auto f = [&] {
        auto discard = doLearnerRead(
            table_id,
            mvcc_query_info,
            false,
            ctx,
            log);
        UNUSED(discard);
    };
    EXPECT_THROW(f(), RegionException);

    // We can't `doApply`, since the TiKVValue is not valid.
    auto r1 = proxy_instance->getRegion(region_id);
    r1->updateAppliedIndex(index);
    kvr1->setApplied(index, term);
    auto regions_snapshot = doLearnerRead(
        table_id,
        mvcc_query_info,
        false,
        ctx,
        log);
    // 0 unavailable regions
    ASSERT_EQ(regions_snapshot.size(), 1);

    // No throw
    auto mvcc_query_info2 = MvccQueryInfo(false, 10);
    mvcc_query_info2.regions_query_info.emplace_back(1, kvr1->version(), kvr1->confVer(), table_id, kvr1->getRange()->rawKeys());
    validateQueryInfo(mvcc_query_info2, regions_snapshot, ctx.getTMTContext(), log);
}
CATCH

} // namespace tests
} // namespace DB
