// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <gtest/gtest.h>

#include "base/random.h"
#include "file/list_file.h"
#include "file/test_util.h"
#include "util/coding/fixed.h"
#include "util/crc32c.h"

namespace file {

using namespace list_file;

using strings::Slice;
using util::Status;


namespace crc32c = util::crc32c;

// Construct a string of the specified length made out of the supplied
// partial string.
static std::string BigString(const std::string& partial_string, size_t n) {
  std::string result;
  while (result.size() < n) {
    result.append(partial_string);
  }
  result.resize(n);
  return result;
}

// Construct a string from a number
static std::string NumberString(int n) {
  char buf[50];
  snprintf(buf, sizeof(buf), "%d.", n);
  return std::string(buf);
}

// Return a skewed potentially long string
static std::string RandomSkewedString(int i, RandomBase* rnd) {
  return BigString(NumberString(i), rnd->Skewed(17));
}

class LogTest : public testing::Test  {
 private:
  class StringFile : public file::ReadonlyFile {
   public:
    Slice contents_;
    bool force_error_;
    bool returned_partial_;
    StringFile() : force_error_(false), returned_partial_(false) { }

    virtual Status Read(size_t offset, size_t length, strings::Slice* result,
                        uint8* buffer) override {
      CHECK(!returned_partial_) << "must not Read() after eof/error";
      if (force_error_) {
        force_error_ = false;
        returned_partial_ = true;
        return Status(base::StatusCode::IO_ERROR, "read error");
      }

      if (contents_.size() < length + offset) {
        return Status(base::StatusCode::IO_ERROR, "invalid range");
      }
      *result = strings::Slice(contents_, offset, length);
      return Status::OK;
    }

    virtual base::Status Close() { return Status::OK; }

    size_t Size() const { return contents_.size(); }
  };

  class ReportCollector {
   public:
    size_t dropped_bytes_;
    std::string message_;

    ReportCollector() : dropped_bytes_(0) { }

    void Corruption(size_t bytes, const Status& status) {
      dropped_bytes_ += bytes;
      message_.append(status.ToString());
      LOG(INFO) << "Corruption! " << bytes << " " << dropped_bytes_ << " " << status.ToString();
    }
    DISALLOW_EVIL_CONSTRUCTORS(ReportCollector);
  };
protected:
  util::StringSink* dest_ = nullptr;
  StringFile source_;
  ReportCollector report_;
  std::unique_ptr<ListWriter> writer_;
  std::unique_ptr<ListReader> reader_;
  uint32 list_offset_;
  uint32 block_size_ = 0;
  bool writer_flushed_ = false;

  // Record metadata for testing initial offset functionality
  std::vector<size_t> initial_offset_record_sizes_;

  ListReader::CorruptionReporter reporter_func() {
    using namespace std::placeholders;
    return std::bind(&ReportCollector::Corruption, std::ref(report_), _1, _2);
  }

 public:
  LogTest() {
    ListWriter::Options options;
    options.block_size_multiplier = 1;
    options.use_compression = false;

    SetupWriter(options);
  }

  void Write(const std::string& msg) {
    CHECK(reader_ == nullptr) << "Write() after starting to read";
    writer_->AddRecord(Slice(msg));
    writer_flushed_ = false;
  }

  size_t RecordWrittenBytes() const {
    return dest_->contents().size() - list_offset_;
  }

  void FlushWriter() {
    if (!writer_flushed_) {
      CHECK(writer_->Flush().ok());
      writer_flushed_ = true;
    }
  }

  std::string Read() {
    FlushWriter();

    if (reader_ == nullptr) {
      source_.contents_ = Slice(dest_->contents());
      reader_.reset(new ListReader(&source_, DO_NOT_TAKE_OWNERSHIP,
                                   true/*checksum*/, reporter_func()));
    }

    std::string scratch;
    Slice record;
    if (!reader_->ReadRecord(&record, &scratch)) {
      return "EOF";
    }
    return record.as_string();
  }

  void SetupWriter(const ListWriter::Options& options, bool init_writer = true) {
    dest_ = new util::StringSink;
    writer_.reset(new ListWriter(dest_, options));
    if (init_writer)
      CHECK(writer_->Init().ok());
    list_offset_ = dest_->contents().size();
    block_size_ = options.block_size_multiplier * kBlockSizeFactor;


    initial_offset_record_sizes_ =
    {10000,  // Two sizable records in first block
     10000,
     2 * block_size_ - 1000,  // Span three blocks
     1};
  }

  void IncrementByte(int offset, int delta) {
    dest_->contents()[offset + list_offset_] += delta;
  }

  void SetByte(int offset, char new_byte) {
    dest_->contents()[offset + list_offset_] = new_byte;
  }

  void ShrinkSize(int bytes) {
    dest_->contents().resize(dest_->contents().size() - bytes);
  }

  void FixChecksum(int header_offset, int len) {
    // Compute crc of type/data
    Slice s(dest_->contents(), header_offset + list_offset_);
    uint32_t crc = crc32c::Value(s.data() + 8, 1 + len);
    crc = crc32c::Mask(crc);
    coding::EncodeFixed32(crc, reinterpret_cast<uint8*>(
      &dest_->contents()[header_offset + list_offset_]));
  }

  void ForceError() {
    source_.force_error_ = true;
  }

  size_t DroppedBytes() const {
    return report_.dropped_bytes_;
  }

  std::string ReportMessage() const {
    return report_.message_;
  }

  // Returns OK iff recorded error message contains "msg"
  std::string MatchError(const std::string& msg) const {
    if (report_.message_.find(msg) == std::string::npos) {
      return report_.message_;
    } else {
      return "OK";
    }
  }

#if 0
  void WriteInitialOffsetLog() {
    for (int i = 0; i < 4; i++) {
      std::string record(initial_offset_record_sizes_[i],
                         static_cast<char>('a' + i));
      Write(record);
    }
  }

  void CheckOffsetPastEndReturnsNoRecords(uint64_t offset_past_end) {
    WriteInitialOffsetLog();
    source_.contents_ = Slice(dest_->contents());
    std::unique_ptr<ListReader> offset_reader(
      new ListReader(&source_, DO_NOT_TAKE_OWNERSHIP, true,
                     RecordWrittenBytes() + offset_past_end, reporter_func()));
    Slice record;
    std::string scratch;
    ASSERT_FALSE(offset_reader->ReadRecord(&record, &scratch).ok());
  }


  void CheckInitialOffsetRecord(uint64_t initial_offset,
                                int expected_record_offset) {
    WriteInitialOffsetLog();
    source_.contents_ = Slice(dest_->contents());
    std::unique_ptr<ListReader> offset_reader(
        new ListReader(&source_, DO_NOT_TAKE_OWNERSHIP, true/*checksum*/,
                       initial_offset, reporter_func()));
    Slice record;
    std::string scratch;
    ASSERT_TRUE(offset_reader->ReadRecord(&record, &scratch).ok());
    ASSERT_EQ(initial_offset_record_sizes_[expected_record_offset],
              record.size());

    std::vector<size_t> last_record_offsets(4, 0);
    for (int i = 1; i < last_record_offsets.size(); ++i) {
      last_record_offsets[i] = last_record_offsets[i-1] +
        kBlockHeaderSz1 + initial_offset_record_sizes_[i-1];
    }
    last_record_offsets.back() += 2 * kBlockHeaderSz1;

    ASSERT_EQ(last_record_offsets[expected_record_offset],
              offset_reader->LastRecordOffset());
    ASSERT_EQ((char)('a' + expected_record_offset), record.data()[0]);
  }
#endif
};

TEST_F(LogTest, Empty) {
  ASSERT_EQ("EOF", Read());
}

TEST_F(LogTest, ReadWrite) {
  Write("foo");
  Write("bar");
  Write("");
  Write("xxxx");
  ASSERT_EQ("foo", Read());
  ASSERT_EQ("bar", Read());
  ASSERT_EQ("", Read());
  ASSERT_EQ("xxxx", Read());
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ("EOF", Read());  // Make sure reads at eof work
}

TEST_F(LogTest, ManyBlocks) {
  for (int i = 0; i < 100000; i++) {
    Write(NumberString(i));
  }
  for (int i = 0; i < 100000; i++) {
    ASSERT_EQ(NumberString(i), Read());
  }
  ASSERT_EQ("EOF", Read());
}

TEST_F(LogTest, Fragmentation) {
  Write("small");
  Write(BigString("medium", 50000));
  Write(BigString("large", 100000));
  ASSERT_EQ("small", Read());
  ASSERT_EQ(BigString("medium", 50000), Read());
  ASSERT_EQ(BigString("large", 100000), Read());
  ASSERT_EQ("EOF", Read());
}

TEST_F(LogTest, MarginalTrailer) {
  // Make a trailer that is exactly the same length as an empty record.
  const int n = block_size_ - 2*kBlockHeaderSize;
  Write(BigString("foo", n));
  // ASSERT_EQ(block_size_ - kBlockHeaderSize, RecordWrittenBytes());
  Write("");
  Write("bar");
  ASSERT_EQ(BigString("foo", n), Read());
  ASSERT_EQ("", Read());
  ASSERT_EQ("bar", Read());
  ASSERT_EQ("EOF", Read());
}

TEST_F(LogTest, MarginalTrailer2) {
  // Make a trailer that is exactly the same length as an empty record.
  const int n = block_size_ - 2*kBlockHeaderSize;
  Write(BigString("foo", n));
  // ASSERT_EQ(block_size_ - kBlockHeaderSize, RecordWrittenBytes());
  Write("bar");
  ASSERT_EQ(BigString("foo", n), Read());
  ASSERT_EQ("bar", Read());
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ(0, DroppedBytes());
  ASSERT_EQ("", ReportMessage());
}

TEST_F(LogTest, ShortTrailer) {
  const int n = block_size_ - 2*kBlockHeaderSize + 4;
  Write(BigString("foo", n));
  ASSERT_EQ(block_size_ - kBlockHeaderSize + 4, RecordWrittenBytes());
  Write("");
  Write("bar");
  ASSERT_EQ(BigString("foo", n), Read());
  ASSERT_EQ("", Read());
  ASSERT_EQ("bar", Read());
  ASSERT_EQ("EOF", Read());
}

TEST_F(LogTest, AlignedEof) {
  const int n = block_size_ - 2*kBlockHeaderSize + 4;
  Write(BigString("foo", n));
  ASSERT_EQ(block_size_ - kBlockHeaderSize + 4, RecordWrittenBytes());
  ASSERT_EQ(BigString("foo", n), Read());
  ASSERT_EQ("EOF", Read());
}

TEST_F(LogTest, RandomRead) {
  const int N = 500;
  MTRandom write_rnd(301);
  for (int i = 0; i < N; i++) {
    Write(RandomSkewedString(i, &write_rnd));
  }
  MTRandom read_rnd(301);
  for (int i = 0; i < N; i++) {
    ASSERT_EQ(RandomSkewedString(i, &read_rnd), Read());
  }
  ASSERT_EQ("EOF", Read());
}

// Tests of all the error paths in log_reader.cc follow:
TEST_F(LogTest, ReadError) {
  Write("foo");
  ForceError();
  ASSERT_EQ("EOF", Read());
  ASSERT_GT(DroppedBytes(), 0);
  ASSERT_TRUE(StringPiece(ReportMessage()).contains("read error"));
}

TEST_F(LogTest, BadRecordType) {
  Write("foo");
  FlushWriter();
  // Type is stored in header[8]
  IncrementByte(8, 100);
  FixChecksum(0, 5);
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ(5, DroppedBytes());
  ASSERT_EQ("OK", MatchError("unknown record type"));
}

TEST_F(LogTest, TruncatedTrailingRecord) {
  Write("foo");
  FlushWriter();
  ShrinkSize(4);   // Drop all payload as well as a header byte
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ(kBlockHeaderSize + 1, DroppedBytes());
  ASSERT_EQ("OK", MatchError("truncated record at"));
}

TEST_F(LogTest, BadLength) {
  Write("foo");
  FlushWriter();
  ShrinkSize(1);
  ASSERT_EQ("EOF", Read());
  EXPECT_EQ(kBlockHeaderSize + 4, DroppedBytes());
  EXPECT_EQ("OK", MatchError("bad record length"));
}

TEST_F(LogTest, ChecksumMismatch) {
  Write("foo");
  FlushWriter();
  IncrementByte(0, 10);
  ASSERT_EQ("EOF", Read());
  EXPECT_EQ(14, DroppedBytes());
  EXPECT_EQ("OK", MatchError("checksum mismatch"));
}

TEST_F(LogTest, UnexpectedMiddleType) {
  Write("foo");
  FlushWriter();
  SetByte(8, kMiddleType);
  FixChecksum(0, 5);
  ASSERT_EQ("EOF", Read());
  EXPECT_EQ(5, DroppedBytes());
  EXPECT_EQ("OK", MatchError("missing start"));
}

TEST_F(LogTest, UnexpectedLastType) {
  Write("foo");
  FlushWriter();
  SetByte(8, kLastType);
  FixChecksum(0, 5);
  ASSERT_EQ("EOF", Read());
  EXPECT_EQ(5, DroppedBytes());
  EXPECT_EQ("OK", MatchError("missing start"));
}

TEST_F(LogTest, UnexpectedFullType) {
  Write("foo");
  FlushWriter();
  Write("bar");
  FlushWriter();
  SetByte(8, kFirstType);
  FixChecksum(0, 5);
  ASSERT_EQ("bar", Read());
  ASSERT_EQ("EOF", Read());
  EXPECT_EQ(5, DroppedBytes());
  EXPECT_EQ("OK", MatchError("partial record without end"));
}

TEST_F(LogTest, UnexpectedFirstType) {
  Write("foo");
  Write(BigString("bar", 100000));
  FlushWriter();
  SetByte(8, kFirstType);
  FixChecksum(0, 5);
  ASSERT_EQ(BigString("bar", 100000), Read());
  ASSERT_EQ("EOF", Read());
  EXPECT_EQ(5, DroppedBytes());
  EXPECT_EQ("OK", MatchError("partial record without end"));
}

TEST_F(LogTest, ErrorJoinsRecords) {
  // Consider two fragmented records:
  //    first(R1) last(R1) first(R2) last(R2)
  // where the middle two fragments disappear.  We do not want
  // first(R1),last(R2) to get joined and returned as a valid record.

  // Write records that span two blocks
  Write(BigString("foo", block_size_));
  Write(BigString("bar", block_size_));
  Write("correct");
  FlushWriter();
  // Wipe the middle block
  for (int offset = block_size_; offset < 2*block_size_; offset++) {
    SetByte(offset, 'x');
  }

  ASSERT_EQ("correct", Read());
  ASSERT_EQ("EOF", Read());
  const int dropped = DroppedBytes();
  ASSERT_LE(dropped, 2*block_size_ + 100);
  ASSERT_GE(dropped, 2*block_size_);
}

TEST_F(LogTest, Compression) {
  ListWriter::Options options;
  options.block_size_multiplier = 2;
  options.use_compression = true;
  SetupWriter(options);
  for (int i = 0; i < 2000; ++i)
    Write(NumberString(i));
  Write(BigString("foo", 1000));
  Write(BigString("bar", 2000));
  Write(BigString("zoo", 1000));
  FlushWriter();
  for (int i = 0; i < 2000; i++) {
    ASSERT_EQ(NumberString(i), Read());
  }
  ASSERT_EQ(BigString("foo", 1000), Read());
  ASSERT_EQ(BigString("bar", 2000), Read());
  ASSERT_EQ(BigString("zoo", 1000), Read());
  ASSERT_EQ("EOF", Read());
}

TEST_F(LogTest, MetaData) {
  SetupWriter(ListWriter::Options(), false);
  string kMetaVal1 = "data1";
  string kMetaVal2 = "data2";
  writer_->AddMeta("key1", kMetaVal1);
  writer_->AddMeta("key2", kMetaVal2);
  CHECK(writer_->Init().ok());

  const int kNumIter = 10;
  for (int i = 0; i < kNumIter; ++i)
    Write(NumberString(i));
  FlushWriter();

  for (int i = 0; i < kNumIter; i++) {
    ASSERT_EQ(NumberString(i), Read());
  }
  std::map<string, string> meta;
  ASSERT_TRUE(reader_->GetMetaData(&meta));
  EXPECT_EQ(2, meta.size());
  EXPECT_EQ(kMetaVal1, meta["key1"]);
  EXPECT_EQ(kMetaVal2, meta["key2"]);
}

/*TEST_F(LogTest, ReadStart) {
  CheckInitialOffsetRecord(0, 0);
}

TEST_F(LogTest, ReadSecondOneOff) {
  CheckInitialOffsetRecord(1, 1);
}

TEST_F(LogTest, ReadSecondTenThousand) {
  CheckInitialOffsetRecord(10000, 1);
}

TEST_F(LogTest, ReadSecondStart) {
  CheckInitialOffsetRecord(10007, 1);
}

TEST_F(LogTest, ReadThirdOneOff) {
  CheckInitialOffsetRecord(10008, 2);
}

TEST_F(LogTest, ReadThirdStart) {
  CheckInitialOffsetRecord(20014, 2);
}

TEST_F(LogTest, ReadFourthOneOff) {
  CheckInitialOffsetRecord(20015, 3);
}

TEST_F(LogTest, ReadFourthFirstBlockTrailer) {
  CheckInitialOffsetRecord(block_size_ - 4, 3);
}

TEST_F(LogTest, ReadFourthMiddleBlock) {
  CheckInitialOffsetRecord(block_size_ + 1, 3);
}

TEST_F(LogTest, ReadFourthLastBlock) {
  CheckInitialOffsetRecord(2 * block_size_ + 1, 3);
}

TEST_F(LogTest, ReadFourthStart) {
  CheckInitialOffsetRecord(
      2 * (kBlockHeaderSize + 1000) + (2 * block_size_ - 1000) + 3 * kBlockHeaderSize,
      3);
}

TEST_F(LogTest, ReadEnd) {
  CheckOffsetPastEndReturnsNoRecords(0);
}

TEST_F(LogTest, ReadPastEnd) {
  CheckOffsetPastEndReturnsNoRecords(5);
}
*/

}  // namespace file

