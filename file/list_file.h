// Copyright 2013, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
// Modified LevelDB implementation of log_format.
#ifndef _LIST_FILE_H_
#define _LIST_FILE_H_

#include <map>
#include "file/file.h"
#include "file/list_file_format.h"
#include "strings/slice.h"
#include "util/sinksource.h"

namespace file {

class ListWriter {
 public:
  struct Options {
    uint8 block_size_multiplier = 1;  // the block size is 64KB * multiplier
    bool use_compression = true;

    Options() {}
  };

  // Takes ownership over sink.
  ListWriter(util::Sink* sink, const Options& options = Options());

  // Create a writer that will overwrite filename with data.
  ListWriter(StringPiece filename, const Options& options = Options());
  ~ListWriter();

  // Adds user provided meta information about the file. Must be called before Init.
  void AddMeta(StringPiece key, strings::Slice value);

  util::Status Init();
  util::Status AddRecord(strings::Slice slice);
  util::Status Flush();

  uint32 records_added() const { return records_added_;}
  uint64 bytes_added() const { return bytes_added_;}
 private:
  std::unique_ptr<util::Sink> dest_;
  std::unique_ptr<uint8[]> array_store_;
  std::unique_ptr<uint8[]> compress_buf_;
  std::map<std::string, std::string> meta_;

  uint8* array_next_ = nullptr, *array_end_ = nullptr;  // wraps array_store_
  bool init_called_ = false;

  Options options_;
  uint32 array_records_ = 0;
  uint32 block_offset_ = 0;      // Current offset in block

  uint32 block_size_ = 0;
  uint32 block_leftover_ = 0;
  uint32 compress_buf_size_ = 0;

  uint32 records_added_ = 0;
  uint64 bytes_added_ = 0;

  void Construct();

  util::Status EmitPhysicalRecord(list_file::RecordType type, const uint8* ptr,
                                  size_t length);

  uint32 block_leftover() const { return block_leftover_; }

  void AddRecordToArray(strings::Slice size_enc, strings::Slice record);
  util::Status FlushArray();

  // No copying allowed
  ListWriter(const ListWriter&) = delete;
  void operator=(const ListWriter&) = delete;
};

class ListReader {
 public:
  // Create a Listreader that will return log records from "*file".
  // "*file" must remain live while this ListReader is in use.
  //
  // If "reporter" is non-NULL, it is notified whenever some data is
  // dropped due to a detected corruption.  "*reporter" must remain
  // live while this ListReader is in use.
  //
  // If "checksum" is true, verify checksums if available.
  //
  // The ListReader will start reading at the first record located at position >= initial_offset
  // relative to start of list records. In afact all positions mentioned in the API are
  // relative to list start position in the file (i.e. file header is read internally and its size
  // is not relevant for the API).
  typedef std::function<void(size_t bytes, const util::Status& status)> CorruptionReporter;

  // initial_offset - file offset AFTER the file header, i.e. offset 0 does not skip anything.
  // File header is read in any case.
  explicit ListReader(file::ReadonlyFile* file, Ownership ownership, bool checksum = false,
                      CorruptionReporter = nullptr);

  // This version reads the file and owns it.
  explicit ListReader(StringPiece filename, bool checksum = false,
                      CorruptionReporter = nullptr);

  ~ListReader();

  bool GetMetaData(std::map<std::string, std::string>* meta);

  // Read the next record into *record.  Returns true if read
  // successfully, false if we hit end of file. May use
  // "*scratch" as temporary storage.  The contents filled in *record
  // will only be valid until the next mutating operation on this
  // reader or the next mutation to *scratch.
  // If invalid record is encountered, read will continue to the next record and
  // will notify reporter about the corruption.
  bool ReadRecord(strings::Slice* record, std::string* scratch);

  // Returns the offset of the last record read by ReadRecord relative to list start position
  // in the file.
  // Undefined before the first call to ReadRecord.
  //size_t LastRecordOffset() const { return last_record_offset_; }

private:
  bool ReadHeader();

  file::ReadonlyFile* file_;
  size_t file_offset_ = 0;
  size_t file_size_ = 0;

  Ownership ownership_;
  CorruptionReporter const reporter_;
  bool const checksum_;
  std::unique_ptr<uint8[]> backing_store_;
  std::unique_ptr<uint8[]> uncompress_buf_;
  strings::Slice block_buffer_;
  std::map<std::string, std::string> meta_;

  bool eof_ = false;   // Last Read() indicated EOF by returning < kBlockSize

  // Offset of the last record returned by ReadRecord.
  // size_t last_record_offset_;
  // Offset of the first location past the end of buffer_.
  // size_t end_of_buffer_offset_ = 0;

  // Offset at which to start looking for the first record to return
  // size_t const initial_offset_;
  uint32 block_size_ = 0;
  uint32 array_records_ = 0;
  strings::Slice array_store_;

  // Extend record types with the following special values
  enum {
    kEof = list_file::kMaxRecordType + 1,
    // Returned whenever we find an invalid physical record.
    // Currently there are three situations in which this happens:
    // * The record has an invalid CRC (ReadPhysicalRecord reports a drop)
    // * The record is a 0-length record (No drop is reported)
    // * The record is below constructor's initial_offset (No drop is reported)
    kBadRecord = list_file::kMaxRecordType + 2
  };

  // Skips all blocks that are completely before "initial_offset_".
  // util::Status SkipToInitialBlock();

  // Return type, or one of the preceding special values
  unsigned int ReadPhysicalRecord(strings::Slice* result);

  // Reports dropped bytes to the reporter.
  // buffer_ must be updated to remove the dropped bytes prior to invocation.
  void ReportCorruption(size_t bytes, const string& reason);
  void ReportDrop(size_t bytes, const util::Status& reason);

  // No copying allowed
  ListReader(const ListReader&) = delete;
  void operator=(const ListReader&) = delete;
};

template<typename T> void ReadProtoRecords(file::File* file,
                                           std::function<void(T&&)> cb) {
  file::ListReader reader(file, TAKE_OWNERSHIP);
  std::string record_buf;
  strings::Slice record;
  while (reader.ReadRecord(&record, &record_buf)) {
    T item;
    CHECK(item.ParseFromArray(record.data(), record.size()));
    cb(std::move(item));
  }
}

template<typename T> void ReadProtoRecords(StringPiece name,
                                           std::function<void(T&&)> cb) {
  file::ListReader reader(name);
  std::string record_buf;
  strings::Slice record;
  while (reader.ReadRecord(&record, &record_buf)) {
    T item;
    CHECK(item.ParseFromArray(record.data(), record.size()));
    cb(std::move(item));
  }
}

}  // namespace file

#endif  // _LIST_FILE_H_
