// Copyright 2018 The Cockroach Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied.  See the License for the specific language governing
// permissions and limitations under the License.

#include <vector>
#include "file_registry.h"
#include "fmt.h"
#include "testutils.h"

using namespace cockroach;

TEST(FileRegistry, TransformPath) {
  struct TestCase {
    std::string db_dir;
    std::string input;
    std::string output;
  };

  // The db_dir as sanitized does not have a trailing slash.
  std::vector<TestCase> test_cases = {
      {"/", "/foo", "foo"},
      {"/rocksdir", "/rocksdirfoo", "/rocksdirfoo"},
      {"/rocksdir", "/rocksdir/foo", "foo"},
      // We get the occasional double-slash.
      {"/rocksdir", "/rocksdir//foo", "foo"},
      {"/mydir", "/mydir", ""},
      {"/mydir", "/mydir/", ""},
      {"/mydir", "/mydir//", ""},
      {"/mnt/otherdevice/", "/mnt/otherdevice/myfile", "myfile"},
      {"/mnt/otherdevice/myfile", "/mnt/otherdevice/myfile", ""},
  };

  int test_num = 0;
  for (auto t : test_cases) {
    SCOPED_TRACE(fmt::StringPrintf("Testing #%d", test_num++));

    FileRegistry reg(nullptr, t.db_dir, false /* read-only */);
    auto out = reg.TransformPath(t.input);
    EXPECT_EQ(t.output, out);
  }
}

TEST(FileRegistry, FileOps) {
  std::unique_ptr<rocksdb::Env> env(rocksdb::NewMemEnv(rocksdb::Env::Default()));

  FileRegistry rw_reg(env.get(), "/", false /* read-only */);
  ASSERT_OK(rw_reg.CheckNoRegistryFile());
  ASSERT_OK(rw_reg.Load());

  EXPECT_EQ(nullptr, rw_reg.GetFileEntry("/foo"));
  auto entry = std::unique_ptr<enginepb::FileEntry>(new enginepb::FileEntry());
  EXPECT_OK(rw_reg.SetFileEntry("/foo", std::move(entry)));
  EXPECT_NE(nullptr, rw_reg.GetFileEntry("/foo"));

  EXPECT_OK(rw_reg.MaybeLinkEntry("/foo", "/bar"));
  EXPECT_NE(nullptr, rw_reg.GetFileEntry("/foo"));
  EXPECT_NE(nullptr, rw_reg.GetFileEntry("/bar"));

  EXPECT_OK(rw_reg.MaybeRenameEntry("/bar", "/baz"));
  EXPECT_NE(nullptr, rw_reg.GetFileEntry("/foo"));
  EXPECT_EQ(nullptr, rw_reg.GetFileEntry("/bar"));
  EXPECT_NE(nullptr, rw_reg.GetFileEntry("/baz"));

  EXPECT_OK(rw_reg.MaybeDeleteEntry("/baz"));
  EXPECT_NE(nullptr, rw_reg.GetFileEntry("/foo"));
  EXPECT_EQ(nullptr, rw_reg.GetFileEntry("/bar"));
  EXPECT_EQ(nullptr, rw_reg.GetFileEntry("/baz"));

  // Now try with a read-only registry.
  FileRegistry ro_reg(env.get(), "/", true /* read-only */);
  // We have a registry file.
  EXPECT_ERR(ro_reg.CheckNoRegistryFile(), "registry file .* exists");
  ASSERT_OK(ro_reg.Load());

  EXPECT_NE(nullptr, ro_reg.GetFileEntry("/foo"));
  EXPECT_EQ(nullptr, ro_reg.GetFileEntry("/bar"));
  EXPECT_EQ(nullptr, ro_reg.GetFileEntry("/baz"));

  // All mutable operations fail.
  EXPECT_ERR(ro_reg.MaybeLinkEntry("/foo", "/bar"), "file registry is read-only .*");
  EXPECT_ERR(ro_reg.MaybeRenameEntry("/foo", "/baz"), "file registry is read-only .*");
  EXPECT_ERR(ro_reg.MaybeDeleteEntry("/foo"), "file registry is read-only .*");
}
