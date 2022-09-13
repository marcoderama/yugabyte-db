// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/bounded_rocksdb_iterator.h"
#include "yb/docdb/doc_key.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/mini_master.h"

#include "yb/rocksdb/db.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_service.h"
#include "yb/tserver/tserver_error.h"

#include "yb/util/monotime.h"
#include "yb/util/string_case.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/test_util.h"

#include "yb/util/tsan_util.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_automatic_tablet_splitting);
DECLARE_bool(TEST_skip_partitioning_version_validation);
DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(TEST_partitioning_version);

using namespace std::literals;

namespace yb {
namespace pgwrapper {

class PgTabletSplitTest : public PgMiniTestBase {

 protected:
  using TabletRecordsInfo =
      std::unordered_map<std::string, std::tuple<docdb::KeyBounds, ssize_t>>;

  Status SplitSingleTablet(const TableId& table_id) {
    auto master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    if (tablets.size() != 1) {
      return STATUS_FORMAT(InternalError, "Expected single tablet, found $0.", tablets.size());
    }
    auto tablet_id = tablets.at(0)->tablet_id();

    return master->catalog_manager().SplitTablet(tablet_id, master::ManualSplit::kTrue);
  }

  Status InvokeSplitTabletRpc(const std::string& tablet_id) {
    master::SplitTabletRequestPB req;
    req.set_tablet_id(tablet_id);
    master::SplitTabletResponsePB resp;

    auto master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    RETURN_NOT_OK(master->catalog_manager_impl().SplitTablet(&req, &resp, nullptr));
    if (resp.has_error()) {
      RETURN_NOT_OK(StatusFromPB(resp.error().status()));
    }
    return Status::OK();
  }

  Status InvokeSplitTabletRpcAndWaitForSplitCompleted(tablet::TabletPeerPtr peer) {
    SCHECK_NOTNULL(peer.get());
    RETURN_NOT_OK(InvokeSplitTabletRpc(peer->tablet_id()));
    return WaitFor([&]() -> Result<bool> {
      const auto leaders =
          ListTableActiveTabletLeadersPeers(cluster_.get(), peer->tablet_metadata()->table_id());
      return leaders.size() == 2;
    }, 15s * kTimeMultiplier, "Wait for split completion.");
  }

  Status DisableCompaction(std::vector<tablet::TabletPeerPtr>* peers) {
    for (auto& peer : *peers) {
      RETURN_NOT_OK(peer->tablet()->doc_db().regular->SetOptions({
          {"level0_file_num_compaction_trigger", std::to_string(std::numeric_limits<int32>::max())}
      }));
    }
    return Status::OK();
  }

 private:
  virtual size_t NumTabletServers() override {
    return 1;
  }
};

TEST_F(PgTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(SplitDuringLongRunningTransaction)) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;

  auto conn = ASSERT_RESULT(Connect());

  auto client = ASSERT_RESULT(cluster_->CreateClient());

  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT, v INT) SPLIT INTO 1 TABLETS;"));

  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT i, 1 FROM (SELECT generate_series(1, 10000) i) t2;"));

  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  for (int i = 0; i < 10; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));

  ASSERT_OK(SplitSingleTablet(table_id));

  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return ListTableActiveTabletLeadersPeers(cluster_.get(), table_id).size() == 2;
  }, 15s * kTimeMultiplier, "Wait for split completion."));

  SleepFor(FLAGS_cleanup_split_tablets_interval_sec * 10s * kTimeMultiplier);

  for (int i = 10; i < 20; ++i) {
    ASSERT_OK(conn.ExecuteFormat("UPDATE t SET v = 2 where k = $0;", i));
  }

  ASSERT_OK(conn.CommitTransaction());
}

TEST_F(PgTabletSplitTest, YB_DISABLE_TEST_IN_TSAN(SplitKeyMatchesPartitionBound)) {
  // The intent of the test is to check that splitting is not happening when middle split key
  // matches one of the bounds (it actually can match only lower bound). Placed the test at this
  // file as it's hard to create a table of such structure with the functionality inside
  // tablet-split-itest.cc.
  auto conn = ASSERT_RESULT(Connect());
  auto client = ASSERT_RESULT(cluster_->CreateClient());

  // Create a table with combined key; this allows to have a unique DocKey with the same HASH.
  // Setting table's partitioning explicitly to have one of bounds be specified for each tablet.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t(k1 INT, k2 INT, v TEXT, PRIMARY KEY (k1 HASH, k2 ASC))"
      "  SPLIT INTO 2 TABLETS;"));

  // Make a special structure of records: it has the same HASH but different DocKey, thus from
  // tablet splitting perspective it should give middle split key that matches the partition bound.
  ASSERT_OK(conn.Execute(
      "INSERT INTO t SELECT 13402, i, i::text FROM generate_series(1, 200) as i;"));

  ASSERT_OK(cluster_->FlushTablets());

  auto table_id = ASSERT_RESULT(GetTableIDFromTableName("t"));
  auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
  ASSERT_EQ(2, peers.size());

  // Select a peer whose lower bound is specified.
  auto peer_it = std::find_if(peers.begin(), peers.end(),
      [](const tablet::TabletPeerPtr& peer){
    return !(peer->tablet_metadata()->partition()->partition_key_start().empty());
  });
  ASSERT_FALSE((peer_it == peers.end()));
  auto peer = *peer_it;

  // Make sure SST files appear to be able to split.
  ASSERT_OK(WaitForAnySstFiles(peer));

  // Have to make a low-level direct call of split middle key to verify an error.
  auto result = peer->tablet()->GetEncodedMiddleSplitKey();
  ASSERT_NOK(result);
  ASSERT_EQ(
      tserver::TabletServerError(result.status()),
      tserver::TabletServerErrorPB::TABLET_SPLIT_KEY_RANGE_TOO_SMALL);
  ASSERT_NE(result.status().ToString().find("with partition bounds"), std::string::npos);
}

class PgPartitioningVersionTest :
    public PgTabletSplitTest,
    public testing::WithParamInterface<uint32_t> {
 protected:
  using PartitionBounds = std::pair<std::string, std::string>;

  void SetUp() override {
    // Additional disabling is required as YB_DISABLE_TEST_IN_TSAN is not allowed in parameterized
    // test with TEST_P and calling path cannot reach test body due to initdb timeout in TSAN mode.
    YB_SKIP_TEST_IN_TSAN();
    PgTabletSplitTest::SetUp();
  }

  // Specify indexscan_condition to force enable_indexscan
  static Status SetEnableIndexScan(PGConn* conn, bool indexscan) {
    return conn->ExecuteFormat("SET enable_indexscan = $0;", indexscan ? "on" : "off");
  }

  static Result<int64_t> FetchTableRowsCount(PGConn* conn, const std::string& table_name,
      const std::string& where_clause = std::string()) {
    auto res = VERIFY_RESULT(conn->Fetch(Format(
        "SELECT COUNT(*) FROM $0;",
        where_clause.empty() ? table_name : Format("$0 WHERE $1", table_name, where_clause))));
    SCHECK_EQ(1, PQnfields(res.get()), IllegalState, "");
    SCHECK_EQ(1, PQntuples(res.get()), IllegalState, "");
    return GetInt64(res.get(), 0, 0);
  }

  Status SplitTableWithSingleTablet(
      const std::string& table_name, uint32_t expected_partitioning_version) {
    auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    SCHECK_EQ(1, peers.size(), IllegalState,
              Format("Expected to have 1 peer only, got {0}", peers.size()));

    auto peer = peers.front();
    auto partitioning_version =
        peer->tablet()->schema()->table_properties().partitioning_version();
    SCHECK_EQ(expected_partitioning_version, partitioning_version, IllegalState,
              Format("Unexpected paritioning version {0} vs {1}",
                      expected_partitioning_version, partitioning_version));

    // Make sure SST files appear to be able to split
    RETURN_NOT_OK(WaitForAnySstFiles(peer));
    return InvokeSplitTabletRpcAndWaitForSplitCompleted(peer);
  }

  TabletRecordsInfo GetTabletRecordsInfo(
      const std::vector<tablet::TabletPeerPtr>& peers) {
    TabletRecordsInfo result;
    for (const auto& peer : peers) {
      auto db = peer->tablet()->doc_db();
      ssize_t num_records = 0;
      rocksdb::ReadOptions read_opts;
      read_opts.query_id = rocksdb::kDefaultQueryId;
      docdb::BoundedRocksDbIterator it(db.regular, read_opts, db.key_bounds);
      for (it.SeekToFirst(); it.Valid(); it.Next(), ++num_records) {}
      result.emplace(peer->tablet_id(), std::make_tuple(*db.key_bounds, num_records));
    }
    return result;
  }

  Result<TabletRecordsInfo> DiffTabletRecordsInfo(
        const TabletRecordsInfo& a, const TabletRecordsInfo& b) {
    TabletRecordsInfo result;
    for (const auto& info : b) {
      auto it = a.find(info.first);
      if (it == a.end()) {
        result.insert(info);
      } else {
        SCHECK_EQ(std::get<0>(it->second).lower, std::get<0>(info.second).lower,
                  IllegalState, "Lower bound must match");
        SCHECK_EQ(std::get<0>(it->second).upper, std::get<0>(info.second).upper,
                  IllegalState, "Upper bound must match");
        auto diff = std::get<1>(it->second) - std::get<1>(info.second);
        if (diff != 0) {
          result.emplace(it->first, std::make_tuple(std::get<0>(it->second), diff));
        }
      }
    }
    return result;
  }

  std::vector<PartitionBounds> PrepareRangePartitions(
      const std::vector<std::vector<std::string>>& range_components) {
    static const std::string kDocKeyFormat = "DocKey([], [$0])";
    static const std::string empty_key = Format(kDocKeyFormat, "");

    // Helper method to generate a single partition key
    static const auto gen_key = [](const std::vector<std::string>& components) {
      std::stringstream ss;
      for (const auto& comp : components) {
        if (ss.tellp()) {
          ss << ", ";
        }
        ss << comp;
      }
      return Format(kDocKeyFormat, ss.str());
    };

    const size_t num_partitions = range_components.size();
    std::vector<PartitionBounds> partitions;
    partitions.reserve(num_partitions + 1);
    for (size_t n = 0; n <= num_partitions; ++n) {
      if (n == 0) {
        partitions.emplace_back(empty_key, gen_key(range_components[n]));
      } else if (n == num_partitions) {
        partitions.emplace_back(gen_key(range_components[n - 1]), empty_key);
      } else {
        partitions.emplace_back(gen_key(range_components[n - 1]), gen_key(range_components[n]));
      }
    }
    return partitions;
  }

  Status ValidatePartitionsStructure(
      const std::string& table_name,
      const size_t expected_num_tablets,
      const std::vector<std::vector<std::string>>& range_partitions) {
    // Validate range components are aligned
    SCHECK(range_partitions.size() > 0, IllegalState, "Range partitions must be specified.");
    const size_t num_range_components = range_partitions[0].size();
    for (size_t n = 1; n < range_partitions.size(); ++n) {
      SCHECK_EQ(num_range_components, range_partitions[n].size(), IllegalState,
                Format("All range components must have the same size: $0 vs $1 at $2",
                       num_range_components, range_partitions[n].size(), n));
    }
    SCHECK(num_range_components > 0, IllegalState, "Range components must be specified.");

    const auto table_id = VERIFY_RESULT(GetTableIDFromTableName(table_name));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    SCHECK_EQ(expected_num_tablets, peers.size(), IllegalState,
              Format("Unexpected number of tablets: $0", peers.size()));

    // Get table partitions
    std::unordered_map<std::string, PartitionBounds> table_partitions;
    for (auto peer : peers) {
      // Make sure range partitioning is used.
      const auto meta = peer->tablet()->metadata();
      SCHECK(meta->partition_schema()->IsRangePartitioning(), IllegalState,
             "Range partitioning is expected.");

      // Decode partition bounds and validate bounds has expected structure.
      docdb::DocKey start;
      RETURN_NOT_OK(start.DecodeFrom(meta->partition()->partition_key_start(),
                                     docdb::DocKeyPart::kWholeDocKey, docdb::AllowSpecial::kTrue));
      if (!start.empty()) {
        SCHECK_EQ(num_range_components, start.range_group().size(), IllegalState,
                  Format("Unexpected number of range components: $0", start.range_group().size()));
      }
      docdb::DocKey end;
      RETURN_NOT_OK(end.DecodeFrom(meta->partition()->partition_key_end(),
                                  docdb::DocKeyPart::kWholeDocKey, docdb::AllowSpecial::kTrue));
      if (!end.empty()) {
        SCHECK_EQ(num_range_components, end.range_group().size(), IllegalState,
                  Format("Unexpected number of range components: $0", end.range_group().size()));
      }

      table_partitions[start.ToString()] = { start.ToString(), end.ToString() };
    }

    // Test table partitions match specified partitions
    const auto split_partitions = PrepareRangePartitions(range_partitions);
    SCHECK_EQ(table_partitions.size(), split_partitions.size(), IllegalState,
              Format("Unexpected number of partitions: $0", table_partitions.size()));
    for (const auto& sp : split_partitions) {
      const auto it = table_partitions.find(sp.first);
      SCHECK(it != table_partitions.end(), IllegalState,
             Format("Partition not found: $0", sp.first));
      SCHECK_EQ(it->second.first, sp.first, IllegalState, "Partitions start does not match");
      SCHECK_EQ(it->second.second, sp.second, IllegalState, "Partitions start does not match");
    }
    return Status::OK();
  }
};

// TODO (tsplit): a test for automatic splitting of index table will be added in context of #12189;
// as of now, it is ok to keep only one test as manual and automatic splitting use the same
// execution path in context of table/tablet validation.
TEST_P(PgPartitioningVersionTest, ManualSplit) {
  YB_SKIP_TEST_IN_TSAN();

  const auto expected_partitioning_version = GetParam();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = expected_partitioning_version;

  constexpr auto kNumRows = 1000;
  constexpr auto kTableName = "t1";
  constexpr auto kIdx1Name = "idx1";
  constexpr auto kIdx2Name = "idx2";

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(Format("CREATE TABLE $0(k INT PRIMARY KEY, v TEXT);", kTableName)));
  ASSERT_OK(conn.Execute(Format("CREATE INDEX $0 on $1(v ASC);", kIdx1Name, kTableName)));
  ASSERT_OK(conn.Execute(Format("CREATE INDEX $0 on $1(v HASH);", kIdx2Name, kTableName)));

  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO $0 SELECT i, i::text FROM (SELECT generate_series(1, $1) i) t2;",
      kTableName, kNumRows)));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, kTableName)));

  // Try split range partitioned index table
  {
    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(kIdx1Name));
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_EQ(1, peers.size());

    auto peer = peers.front();
    auto partitioning_version =
        peer->tablet()->schema()->table_properties().partitioning_version();
    ASSERT_EQ(partitioning_version, expected_partitioning_version);

    // Make sure SST files appear to be able to split
    ASSERT_OK(WaitForAnySstFiles(peer));

    auto status = InvokeSplitTabletRpc(peer->tablet_id());
    if (partitioning_version == 0) {
      // Index tablet split is not supported for old index tables with range partitioning
      ASSERT_EQ(status.IsNotSupported(), true) << "Unexpected status: " << status.ToString();
    } else {
      ASSERT_OK(status);
      ASSERT_OK(WaitFor([&]() -> Result<bool> {
        return ListTableActiveTabletLeadersPeers(cluster_.get(), table_id).size() == 2;
      }, 15s * kTimeMultiplier, "Wait for split completion."));

      ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, kTableName)));
    }
  }

  // Try split hash partitioned index table, it does not depend on a partition key version
  {
    ASSERT_OK(SplitTableWithSingleTablet(kIdx2Name, expected_partitioning_version));
    ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, kTableName)));
  }

  // Try split non-index tablet, it does not depend on a partition key version
  {
    ASSERT_OK(SplitTableWithSingleTablet(kTableName, expected_partitioning_version));
    ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, kTableName)));
  }
}

TEST_P(PgPartitioningVersionTest, IndexRowsPersistenceAfterManualSplit) {
  YB_SKIP_TEST_IN_TSAN();

  // The purpose of the test is to verify operations are forwarded to the correct tablets based on
  // partition_key when it contains NULLs in user columns.
  const auto expected_partitioning_version = GetParam();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = expected_partitioning_version;
  if (expected_partitioning_version == 0) {
    // Allow tablet splitting even for partitioning_version == 0
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_partitioning_version_validation) = true;
  }

  constexpr auto kNumRows = 1000;
  auto conn = ASSERT_RESULT(Connect());

  for (const auto& idx_type : { "", "UNIQUE" }) {
    for (const auto& sort_order : { "ASC", "DESC" }) {
      // Create table and index.
      const std::string table_name = ToLowerCase(Format("table_$0_$1idx", sort_order, idx_type));
      const std::string index_name = ToLowerCase(Format("index_$0_$1idx", sort_order, idx_type));
      ASSERT_OK(conn.Execute(Format(
          "CREATE TABLE $0(k INT, i0 INT, t0 TEXT, t1 TEXT, PRIMARY KEY(k ASC));",
          table_name)));
      ASSERT_OK(conn.Execute(Format(
          "CREATE $0 INDEX $1 on $2(t0 $3, t1 $3, i0 $3);",
          idx_type, index_name, table_name, sort_order)));

      ASSERT_OK(conn.Execute(Format(
        "INSERT INTO $0 SELECT i, i, i::text, i::text FROM (SELECT generate_series(1, $1) i) t2;",
        table_name, kNumRows)));

      // Check rows count.
      ASSERT_OK(cluster_->FlushTablets());
      ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

      // Get index table id and check partitioning_version.
      const auto table_id = ASSERT_RESULT(GetTableIDFromTableName(index_name));
      auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
      ASSERT_EQ(1, tablets.size());
      auto parent_peer = tablets.front();
      const auto partitioning_version =
          parent_peer->tablet()->schema()->table_properties().partitioning_version();
      ASSERT_EQ(partitioning_version, expected_partitioning_version);

      // Make sure SST files appear to be able to split
      ASSERT_OK(WaitForAnySstFiles(parent_peer));

      // Keep split key to check future writes are done to the correct tablet for unique index idx1.
      const auto encoded_split_key =
         ASSERT_RESULT(parent_peer->tablet()->GetEncodedMiddleSplitKey());
      ASSERT_TRUE(parent_peer->tablet()->metadata()->partition_schema()->IsRangePartitioning());
      docdb::SubDocKey split_key;
      ASSERT_OK(split_key.FullyDecodeFrom(encoded_split_key, docdb::HybridTimeRequired::kFalse));
      LOG(INFO) << "Split key: " << AsString(split_key);

      // Split index table.
      ASSERT_OK(InvokeSplitTabletRpcAndWaitForSplitCompleted(parent_peer));
      ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

      // Keep current numbers of records persisted in tablets for further analyses.
      const auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
      const auto peers_info = GetTabletRecordsInfo(peers);

      // Simulate leading nulls for the index table
      ASSERT_OK(conn.Execute(
          Format("INSERT INTO $0 VALUES($1, $1, $2, $2);",
                 table_name, kNumRows + 1, "NULL")));
      ASSERT_OK(conn.Execute(
          Format("INSERT INTO $0 VALUES($1, $1, $2, $3);",
                 table_name, kNumRows + 2, "NULL", "'T'")));

      // Validate insert operation is forwarded correctly (assuming NULL LAST approach is used):
      // - for partitioning_version > 0:
      //   - for ASC ordering: all the records should be persisted in the second tablet
      //     with partition [split_key, <end>);
      //   - for DESC ordering: all the records should be persisted in the first tablet
      //     with partition [<begin>, split_key);
      // - for partitioning_version == 0:
      //   - for ASC ordering: operation is lost, no diff in peers_info;
      //   - for DESC ordering: all the records should be persisted in the first tablet
      //     with partition [<begin>, split_key).
      ASSERT_OK(SetEnableIndexScan(&conn, false));
      const auto count_off = ASSERT_RESULT(FetchTableRowsCount(&conn, table_name));
      ASSERT_EQ(kNumRows + 2, count_off);

      ASSERT_OK(SetEnableIndexScan(&conn, true));
      const auto count_on = ASSERT_RESULT(FetchTableRowsCount(&conn, table_name, "i0 > 0"));
      const auto diff =
          ASSERT_RESULT(DiffTabletRecordsInfo(GetTabletRecordsInfo(peers), peers_info));

      const bool is_asc_ordering = ToLowerCase(sort_order) == "asc";
      if (partitioning_version == 0 && is_asc_ordering) {
        ASSERT_EQ(diff.size(), 0); // Having diff.size() == 0 means the records are not written!
        ASSERT_EQ(kNumRows, count_on);
        return;
      }

      ASSERT_EQ(diff.size(), 1);
      ASSERT_EQ(kNumRows + 2, count_on);

      bool is_within_bounds = std::get</* key_bounds */ 0>(
          diff.begin()->second).IsWithinBounds(Slice(encoded_split_key));
      const bool is_correctly_forwarded = is_asc_ordering ? is_within_bounds : !is_within_bounds;
      ASSERT_TRUE(is_correctly_forwarded) <<
          "Insert operation with values matching partitions bound is forwarded incorrectly!";
    }
  }
}

TEST_P(PgPartitioningVersionTest, UniqueIndexRowsPersistenceAfterManualSplit) {
  YB_SKIP_TEST_IN_TSAN();

  // The purpose of the test is to verify operations are forwarded to the correct tablets based on
  // partition_key, where `ybuniqueidxkeysuffix` value is set to null.
  const auto expected_partitioning_version = GetParam();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = expected_partitioning_version;
  if (expected_partitioning_version == 0) {
    // Allow tablet splitting even for partitioning_version == 0
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_partitioning_version_validation) = true;
  }

  constexpr auto kNumRows = 1000;
  auto conn = ASSERT_RESULT(Connect());

  for (const auto& sort_order : { "ASC", "DESC" }) {
    // Create table and index.
    const std::string table_name = ToLowerCase(Format("table_$0", sort_order));
    const std::string index_name = ToLowerCase(Format("index_$0", sort_order));

    ASSERT_OK(conn.Execute(
        Format("CREATE TABLE $0(k INT, i0 INT, t0 TEXT, PRIMARY KEY(k ASC));", table_name)));
    ASSERT_OK(conn.Execute(
        Format("CREATE UNIQUE INDEX $0 on $1(t0 $2, i0 $2);", index_name, table_name, sort_order)));

    ASSERT_OK(conn.Execute(Format(
        "INSERT INTO $0 SELECT i, i, i::text FROM (SELECT generate_series(1, $1) i) t2;",
        table_name, kNumRows)));

    ASSERT_OK(cluster_->FlushTablets());
    ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

    auto table_id = ASSERT_RESULT(GetTableIDFromTableName(index_name));
    auto tablets = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_EQ(1, tablets.size());

    auto parent_peer = tablets.front();
    auto partitioning_version =
        parent_peer->tablet()->schema()->table_properties().partitioning_version();
    ASSERT_EQ(partitioning_version, expected_partitioning_version);

    // Make sure SST files appear to be able to split
    ASSERT_OK(WaitForAnySstFiles(parent_peer));

    // Keep split key to check future writes are done to the correct tablet for unique index idx1.
    auto encoded_split_key = ASSERT_RESULT(parent_peer->tablet()->GetEncodedMiddleSplitKey());
    ASSERT_TRUE(parent_peer->tablet()->metadata()->partition_schema()->IsRangePartitioning());
    docdb::SubDocKey split_key;
    ASSERT_OK(split_key.FullyDecodeFrom(encoded_split_key, docdb::HybridTimeRequired::kFalse));
    LOG(INFO) << "Split key: " << AsString(split_key);

    // Extract and keep split key values for unique index idx1.
    ASSERT_EQ(split_key.doc_key().range_group().size(), 3);
    ASSERT_TRUE(split_key.doc_key().range_group().at(0).IsString());
    ASSERT_TRUE(split_key.doc_key().range_group().at(1).IsInt32());
    const std::string idx1_t0 = split_key.doc_key().range_group().at(0).GetString();
    const auto idx1_i0 = split_key.doc_key().range_group().at(1).GetInt32();
    LOG(INFO) << "Split key values: t0 = \"" << idx1_t0 << "\", i0 = " << idx1_i0;

    // Split unique index table (idx1).
    ASSERT_OK(InvokeSplitTabletRpcAndWaitForSplitCompleted(parent_peer));
    ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

    // Turn compaction off to make all subsequent deletes are kept in regular db.
    auto peers = ListTableActiveTabletLeadersPeers(cluster_.get(), table_id);
    ASSERT_OK(DisableCompaction(&peers));

    // Delete all rows to make the table empty to be able to insert unique values and analyze where.
    // the row is being forwarded.
    ASSERT_OK(conn.Execute(Format("DELETE FROM $0 WHERE k > 0;", table_name)));
    ASSERT_EQ(0, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

    // Keep current numbers of records persisted in tablets for further analyses.
    auto peers_info = GetTabletRecordsInfo(peers);

    // Insert values that match the partition bound.
    ASSERT_OK(conn.Execute(Format(
        "INSERT INTO $0 VALUES($1, $1, $2);", table_name, idx1_i0, idx1_t0)));
    ASSERT_EQ(1, ASSERT_RESULT(FetchTableRowsCount(&conn, table_name)));

    // Validate insert operation is forwarded correctly (assuming NULL LAST approach is used):
    // - for partitioning_version > 0 all records should be persisted in the second tablet
    //   with partition [split_key, <end>);
    // - for partitioning_version == 0 operation is lost, no diff in peers_info.
    const auto diff = ASSERT_RESULT(DiffTabletRecordsInfo(GetTabletRecordsInfo(peers), peers_info));
    if (partitioning_version == 0) {
      ASSERT_EQ(diff.size(), 0); // Having diff.size() == 0 means the records are not written!
      return;
    }

    ASSERT_EQ(diff.size(), 1);
    bool is_correctly_forwarded =
        std::get</* key_bounds */ 0>(diff.begin()->second).IsWithinBounds(Slice(encoded_split_key));
    ASSERT_TRUE(is_correctly_forwarded) <<
        "Insert operation with values matching partitions bound is forwarded incorrectly!";
  }
}

TEST_P(PgPartitioningVersionTest, SplitAt) {
  YB_SKIP_TEST_IN_TSAN();

  const auto expected_partitioning_version = GetParam();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_automatic_tablet_splitting) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_partitioning_version) = expected_partitioning_version;

  constexpr auto kNumRows = 1000;

  using PartitionsKeys = std::vector<std::vector<std::string>>;
  static constexpr auto adjust_partitions =
      [](const uint32_t partitioning_version, PartitionsKeys partitions) -> PartitionsKeys {
    for (auto& part : partitions) {
      if (partitioning_version) {
        // Starting from paritioning version == 1, a range group of partition, created with
        // split at statement, will contain a `-Inf` (a.k.a `kLowest` a.k.a 0x00) value for
        // `ybuniqueidxkeysuffix` or `ybidxbasectid`.
        part.push_back("-Inf");
      }
    }
    return partitions;
  };

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute(Format(
      "CREATE TABLE t1(k INT, v TEXT, PRIMARY KEY (k ASC)) SPLIT AT VALUES ((500));")));
  ASSERT_OK(conn.Execute(
      "CREATE INDEX idx1 on t1(v ASC) SPLIT AT VALUES (('301'), ('601'));"));
  ASSERT_OK(conn.Execute(
      "CREATE UNIQUE INDEX idx2 on t1(v DESC) SPLIT AT VALUES(('800'), ('600'), ('400'));"));

  ASSERT_OK(conn.Execute(Format(
      "INSERT INTO t1 SELECT i, i::text FROM (SELECT generate_series(1, $0) i) t2;", kNumRows)));

  ASSERT_OK(cluster_->FlushTablets());
  ASSERT_EQ(kNumRows, ASSERT_RESULT(FetchTableRowsCount(&conn, "t1")));

  // Regular tables range partitioning does not depend on the partitioning version
  ASSERT_OK(ValidatePartitionsStructure("t1", 2, {{"500"}}));

  // Index tables range partitioning depend on the partitioning version
  ASSERT_OK(ValidatePartitionsStructure(
      "idx1", 3,
      adjust_partitions(expected_partitioning_version, {{"\"301\""}, {"\"601\""}})));
  ASSERT_OK(ValidatePartitionsStructure(
      "idx2", 4,
      adjust_partitions(expected_partitioning_version, {{"\"800\""}, {"\"600\""}, {"\"400\""}})));
}


INSTANTIATE_TEST_CASE_P(
    PgTabletSplitTest,
    PgPartitioningVersionTest,
    ::testing::Values(0U, 1U));

} // namespace pgwrapper
} // namespace yb