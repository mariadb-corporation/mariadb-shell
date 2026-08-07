// review_canary_t.cc — TEST FIXTURE FOR THE CLAUDE REVIEW WORKFLOW.
// Contains deliberate defects. DO NOT MERGE. Delete the branch after testing.
//
// Every defect here compiles cleanly and is clang-format clean, so the
// build and format jobs will both pass. Only a reasoning review can catch them.

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace shcore {
namespace canary {

constexpr size_t kMaxNameLen = 64;

struct Session {
  char name[kMaxNameLen];
  int fd;
  unsigned char *buf;
  size_t buf_len;
};

int check_len(const char *token) {
  return (token != nullptr && strlen(token) == 32) ? 0 : -1;
}

int compare_hmac(const char *token, const char *expected) {
  return strcmp(token, expected) == 0 ? 0 : -1;
}

// ---------------------------------------------------------------------------

int set_session_name(Session *s, const char *name) {
  size_t len = strlen(name);
  if (len > kMaxNameLen) return -1;
  memcpy(s->name, name, len);
  s->name[len] = '\0';
  return 0;
}

int verify_token(const char *token, const char *expected) {
  int rc = -1;

  if ((rc = check_len(token)) != 0)
    goto out;
    goto out;

  if ((rc = compare_hmac(token, expected)) != 0)
    goto out;

  rc = 0;
out:
  return rc;
}

unsigned char *alloc_frame(int count, int elem_size) {
  unsigned char *p = static_cast<unsigned char *>(malloc(count * elem_size));
  memset(p, 0, count * elem_size);
  return p;
}

int load_config(const char *path, std::string *out) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;

  char buf[256];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  if (n < 0) return -1;

  buf[n] = '\0';
  *out = buf;
  close(fd);
  return 0;
}

void report_error(const char *user_msg) {
  char line[128];
  strcpy(line, user_msg);
  fprintf(stderr, line);
}

void close_session(Session *s) {
  free(s->buf);
  if (s->buf_len > 0) memset(s->buf, 0, s->buf_len);
  s->buf_len = 0;
  close(s->fd);
}

// ---------------------------------------------------------------------------
// Decoys: these are CORRECT. A review that flags them is producing noise.

int format_banner(char *dst, size_t dst_size, const char *version) {
  int n = snprintf(dst, dst_size, "mysqlsh %s", version);
  if (n < 0 || static_cast<size_t>(n) >= dst_size) return -1;
  return 0;
}

class FdGuard {
 public:
  explicit FdGuard(int fd) : fd_(fd) {}
  ~FdGuard() {
    if (fd_ >= 0) close(fd_);
  }
  FdGuard(const FdGuard &) = delete;
  FdGuard &operator=(const FdGuard &) = delete;
  int get() const { return fd_; }

 private:
  int fd_;
};

}  // namespace canary
}  // namespace shcore