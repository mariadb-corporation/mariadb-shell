/*
 * Copyright (c) 2026, Oracle and/or its affiliates.
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

#include "modules/util/common/data_masking.h"

#include <ostream>
#include <stdexcept>
#include <string>

#include "unittest/gtest_clean.h"

namespace mysqlsh {
namespace common {

[[maybe_unused]] std::ostream &operator<<(
    std::ostream &os, const Data_masking::Policy::Authorization_type &at) {
  os << "Data_masking::Policy::Authorization_type::";

  switch (at) {
    case Data_masking::Policy::Authorization_type::ROLE:
      os << "ROLE";
      break;

    case Data_masking::Policy::Authorization_type::USER:
      os << "USER";
      break;

    case Data_masking::Policy::Authorization_type::UNKNOWN:
      os << "UNKNOWN";
      break;
  }

  return os;
}

namespace {

void expect_policy(const Data_masking::Policy &expected,
                   const Data_masking::Policy &actual, const char *file,
                   int line) {
  SCOPED_TRACE("policy '" + expected.name + "' @ " + file + ":" +
               std::to_string(line));

  EXPECT_EQ(expected.name, actual.name);
  EXPECT_EQ(expected.authorization_type, actual.authorization_type);
  EXPECT_EQ(expected.allow, actual.allow);
  EXPECT_EQ(expected.gatekeeper, actual.gatekeeper);
}

#define EXPECT_POLICY(expected, actual) \
  expect_policy(expected, actual, __FILE__, __LINE__)

TEST(Data_masking, parse_policy) {
  EXPECT_THROW(parse_policy(""), std::runtime_error);
  EXPECT_THROW(parse_policy("CREATE ROLE 'role'"), std::runtime_error);

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::ROLE,
                    true, R"(CURRENT_ROLE_IN('admin'))"),
                parse_policy(R"(CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('admin')
    THEN ssn
    ELSE ssn % 1000
  END;)"));

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::ROLE,
                    true, R"(CURRENT_ROLE_IN('\'admin\'@\'host\''))"),
                parse_policy(R"(CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('\'admin\'@\'host\'')
    THEN ssn
    ELSE ssn % 1000
  END;)"));

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::ROLE,
                    true, R"(CURRENT_ROLE_IN("\"admin\"@\"host\""))"),
                parse_policy(R"(CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN("\"admin\"@\"host\"")
    THEN ssn
    ELSE ssn % 1000
  END;)"));

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::ROLE,
                    true, R"(current_role_in(_utf8mb4'admin'))"),
                parse_policy(R"(CREATE MASKING POLICY `mask_ssn` (`ssn`)
  (
  case when current_role_in(_utf8mb4'admin')
    then `ssn`
    else (`ssn` % 1000)
  end
  ))"));

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::USER,
                    true, R"(current_USER_in('admin@%'))"),
                parse_policy(R"(CREATE MASKING POLICY `MASK_SSN` (`SSN`)
  (
  case when current_USER_in('admin@%')
    then (`ssn`)
    else (`ssn` % 1000)
  end
  ))"));

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::USER,
                    true, R"(current_USER_in('`ad``min`@`%`'))"),
                parse_policy(R"(CREATE MASKING POLICY `mask_ssn` (`jaźń`)
  (
  case when current_USER_in('`ad``min`@`%`')
    then (`JAŹŃ`)
    else (`JAŹŃ` % 1000)
  end
  ))"));

  EXPECT_POLICY(Data_masking::Policy(
                    "zażółć", Data_masking::Policy::Authorization_type::ROLE,
                    false, R"(current_role_in('"ad\"min"@"%"'))"),
                parse_policy(R"(CREATE MASKING POLICY `ZAŻÓŁĆ` (`ssn`)
  (
  case when current_role_in('"ad\"min"@"%"')
    then (`ssn` % 1000)
    else (`ssn`)
  end
  ))"));

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::ROLE,
                    false, R"(current_role_in("'ad\'min'@'%'"))"),
                parse_policy(R"(CREATE MASKING POLICY `mask_ssn` (`ssn`)
  (
  case when current_role_in("'ad\'min'@'%'")
    then (`ssn` % 1000)
    else `ssn`
  end
  ))"));

  EXPECT_POLICY(
      Data_masking::Policy("mask_ssn",
                           Data_masking::Policy::Authorization_type::ROLE, true,
                           R"(CURRENT_ROLE_IN('admin ,dev, user'))"),
      parse_policy(R"(CREATE MASKING POLICY IF NOT EXISTS mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('admin ,dev, user')
    THEN ssn
    ELSE ssn % 1000
  END;)"));

  EXPECT_POLICY(
      Data_masking::Policy(
          "mask_ssn", Data_masking::Policy::Authorization_type::ROLE, true,
          R"(CURRENT_ROLE_IN('"admin ,dev", `user,reporting `'))"),
      parse_policy(R"(CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('"admin ,dev", `user,reporting `')
    THEN ssn
    ELSE ssn % 1000
  END;)"));

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::ROLE,
                    true, R"(CURRENT_ROLE_IN("'admin ,dev', user@host"))"),
                parse_policy(R"(CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN("'admin ,dev', user@host")
    THEN ssn
    ELSE ssn % 1000
  END;)"));

  EXPECT_POLICY(
      Data_masking::Policy(
          "mask_ssn", Data_masking::Policy::Authorization_type::ROLE, true,
          R"(CURRENT_ROLE_IN("user@'host1',user@`host2`,'user' @ host3, 'user'@'host4','user'@`host5`,`user`@host6,`user`@'host7',`user`@`host8`"))"),
      parse_policy(R"(CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN("user@'host1',user@`host2`,'user' @ host3, 'user'@'host4','user'@`host5`,`user`@host6,`user`@'host7',`user`@`host8`")
    THEN ssn
    ELSE ssn % 1000
  END;)"));

  EXPECT_POLICY(
      Data_masking::Policy(
          "mask_ssn", Data_masking::Policy::Authorization_type::ROLE, true,
          R"(CURRENT_ROLE_IN('user@"host1",`user` @ "host2", "user"@host3,"user"@`host4`,"user"@"host5"'))"),
      parse_policy(R"(CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('user@"host1",`user` @ "host2", "user"@host3,"user"@`host4`,"user"@"host5"')
    THEN ssn
    ELSE ssn % 1000
  END;)"));

  EXPECT_POLICY(Data_masking::Policy(
                    "mask_ssn", Data_masking::Policy::Authorization_type::ROLE,
                    true, R"(CURRENT_ROLE_IN('`foo bar`@%'))"),
                parse_policy(R"(CREATE MASKING POLICY mask_ssn(ssn)
  CASE WHEN CURRENT_ROLE_IN('`foo bar`@%')
    THEN ssn
    ELSE ssn % 1000
  END;)"));
}

}  // namespace

}  // namespace common
}  // namespace mysqlsh
