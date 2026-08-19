/*
 * Copyright (c) 2020, 2024, Oracle and/or its affiliates.
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "unittest/gprod_clean.h"

#include <cstdlib>

#include "modules/util/import_table/load_data.h"
#include "mysqlshdk/libs/storage/backend/memory_file.h"
#include "mysqlshdk/libs/utils/utils_string.h"
#include "unittest/gtest_clean.h"
#include "unittest/test_utils.h"

namespace {
void hexify(const std::string &data, std::string *out) {
  if (data.size() == 0) {
    out->resize(0);
    return;
  }
  out->resize(3 * data.size());

  std::string::iterator k = out->begin();

  for (const unsigned char i : data) {
    *k++ = "0123456789abcdef"[i >> 4];
    *k++ = "0123456789abcdef"[i & 0x0F];
    *k++ = ' ';
  }
  out->resize(3 * data.size() - 1);
}
}  // namespace

namespace mysqlsh {
namespace import_table {

void append_row(std::string *buffer, int length) {
  std::string s;
  s.reserve(3 * length);
  for (int i = 0; i < length; i++) {
    s.push_back('!' + i % (127 - '!'));
  }
  buffer->append(s).append("\n");
}

void test_subchunking(int max_trx_size, int net_buffer_size, int first_row_size,
                      int num_rows, int row_size, int row_size_variance) {
  // generate a chunk
  std::string data;
  append_row(&data, first_row_size);
  for (int i = 0; i < num_rows; i++) {
    append_row(
        &data,
        row_size + (row_size_variance > 0 ? rand() % row_size_variance : 0));
  }

  EXPECT_GE(data.size(), 0);
  EXPECT_EQ(data.back(), '\n');

  mysqlshdk::storage::backend::Memory_file mfile("-");
  mfile.set_content(data);

  SCOPED_TRACE(shcore::str_format(
      "debug = max_trx_size==%i && net_buffer_size==%i && first_row_size==%i "
      "&& num_rows==%i && row_size==%i && row_size_variance==%i && "
      "data.size()==%zi;",
      max_trx_size, net_buffer_size, first_row_size, num_rows, row_size,
      row_size_variance, data.size()));

  const bool debug = false;
  std::string hexdata;

  // debug = max_trx_size == 12 && net_buffer_size == 10 && first_row_size == 18
  // && num_rows == 4 && row_size == 0 && row_size_variance == 6 &&
  //         data.size() == 32;

  mfile.open(mysqlshdk::storage::Mode::READ);

  Transaction_options options;
  options.max_trx_size = max_trx_size;
  Transaction_buffer buffer(Dialect::default_(), &mfile, options);

  std::string net_buffer;
  net_buffer.resize(net_buffer_size);

  std::string full_reassembled_data;

  if (debug) {
    hexify(data, &hexdata);
    std::cout << "==begin data==\n";
    std::cout << hexdata;
    std::cout << "\n==end data==\n";
  }

  // this iterates over different transactions
  for (;;) {
    EXPECT_FALSE(buffer.flush_pending());

    std::string transaction_data;
    // this iterates over different buffer reads() of a transaction
    for (;;) {
      auto bytes = buffer.read(&net_buffer[0], net_buffer.size());
      EXPECT_GE(bytes, 0);
      if (bytes == 0) {
        break;
      }

      // no buffer overruns
      EXPECT_LE(bytes, net_buffer.size()) << "buffer overrun!";

      if (debug) {
        hexify(net_buffer.substr(0, bytes), &hexdata);
        std::cout << "FEED(" << bytes << "): " << hexdata << "\n";
      }

      transaction_data.append(&net_buffer[0], bytes);

      if (debug) {
        hexify(transaction_data, &hexdata);
        std::cout << "Tx data: " << hexdata << '\n';
      }

      // check trx overflows
      if (std::count(transaction_data.begin(), transaction_data.end(), '\n') <=
          1) {
        // the only time we can expect the trx-size to be exceeded is when
        // there's a single row that's too big
      } else {
        EXPECT_LE(transaction_data.size(), max_trx_size);
      }

      if (buffer.flush_pending()) {
        // find the biggest sub-chunk that will still fit in the trx
        std::string remaining_data =
            data.substr(full_reassembled_data.size(), max_trx_size);
        auto end = remaining_data.rfind('\n');
        auto min_expected_bytes = end;
        if (min_expected_bytes == std::string::npos)
          min_expected_bytes = max_trx_size;

        // check that the transaction isn't being flushed prematurely
        EXPECT_GE(transaction_data.size(), min_expected_bytes);
        break;
      }
    }

    if (debug) {
      std::cout << "FLUSH\n";
    }

    if (!transaction_data.empty()) {
      // make sure the last row of a trx is always complete
      EXPECT_EQ(transaction_data.back(), '\n');

      full_reassembled_data.append(transaction_data);
    } else {
      EXPECT_FALSE(buffer.flush_pending());
    }
    bool has_more;
    buffer.flush_done(&has_more);
    if (!has_more) break;
  }
  EXPECT_FALSE(buffer.flush_pending());

  // whole file should've been read
  EXPECT_EQ(mfile.tell(), data.size());

  // data must match
  EXPECT_EQ(data, full_reassembled_data);

  // if (debug) throw std::logic_error("debug stop");
}

// Feeds the whole file through the buffer using reads of 'read_size' bytes and
// returns how many rows it reported as longer than the transaction limit.
uint64_t count_oversized_rows(const std::string &data, int max_trx_size,
                              int read_size) {
  mysqlshdk::storage::backend::Memory_file mfile("-");
  mfile.set_content(data);
  mfile.open(mysqlshdk::storage::Mode::READ);

  Transaction_options options;
  options.max_trx_size = max_trx_size;
  Transaction_buffer buffer(Dialect::default_(), &mfile, options);

  uint64_t reported = 0;
  buffer.on_oversized_row([&reported](uint64_t i) { reported = i; });

  std::string net_buffer;
  net_buffer.resize(read_size);

  for (;;) {
    for (;;) {
      const auto bytes = buffer.read(&net_buffer[0], net_buffer.size());
      if (bytes <= 0 || buffer.flush_pending()) break;
    }

    bool has_more = false;
    buffer.flush_done(&has_more);
    if (!has_more) break;
  }

  return reported;
}

// A row longer than maxBytesPerTransaction has to be reported whatever size the
// client library asks the local-infile callback for: libmysqlclient sizes that
// from the connection's net buffer, libmariadb always asks for 4096 bytes - see
// MARIADB_DUMP_LOAD.md section 21.
TEST(Transaction_buffer, oversized_row_detection_does_not_depend_on_read_size) {
  constexpr int k_max_trx_size = 32;

  std::string with_oversized_row;
  append_row(&with_oversized_row, 4);
  append_row(&with_oversized_row, 3 * k_max_trx_size);
  append_row(&with_oversized_row, 4);

  std::string rows_that_fit;
  for (int i = 0; i < 6; ++i) append_row(&rows_that_fit, 4);

  for (const int read_size : {4, 7, 16, k_max_trx_size, 4 * k_max_trx_size}) {
    SCOPED_TRACE("read size " + std::to_string(read_size));

    EXPECT_EQ(1, count_oversized_rows(with_oversized_row, k_max_trx_size,
                                      read_size));
    EXPECT_EQ(0, count_oversized_rows(rows_that_fit, k_max_trx_size, read_size));
  }
}

TEST(Transaction_buffer, test_subchunking) {
  // test all combinations of different (relative) parameter values possible in
  // a load data, with hopefully all possible relative combinations of row sizes
  // in a chunk

  uint64_t count = 0;

  for (int max_trx_size = 10; max_trx_size < 20; max_trx_size++) {
    std::cout << max_trx_size << "\n";
    for (int net_buffer = 10; net_buffer < 20; net_buffer++) {
      for (int first_row_size = 0; first_row_size < 20; first_row_size++) {
        for (int num_extra_rows = 0; num_extra_rows < 5; num_extra_rows++) {
          for (int row_size = 0; row_size < 20; row_size++) {
            for (int row_size_var = 0; row_size_var < 20; row_size_var++) {
              test_subchunking(max_trx_size, net_buffer, first_row_size,
                               num_extra_rows, row_size, row_size_var);
              count++;
              if (num_extra_rows == 0) break;
            }
            if (num_extra_rows == 0) break;
          }
        }
      }
    }
  }
  std::cout << count << "\n";
}

}  // namespace import_table
}  // namespace mysqlsh
