#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  An answer held in small blocks
//
//  The walked fetches read an answer off the socket as fast as it arrives
//  and parse it afterwards. TfL and adsb.fi end an HTTP/1.0 answer by
//  closing the connection, and parsed while it arrived, at the parser's
//  pace, TfL's 27 kB for Notting Hill Gate lost its tail (IncompleteInput).
//  One String for the whole answer needed a single block of memory as big
//  as the answer, which a busy heap often did not have, and failed the same
//  way: these are 2 kB each, wherever they fit.
//
//  read() hands the bytes back in order, for json_walk.h. No Arduino here:
//  the host test runs it.
// ============================================================

class BodyBlocks {
 public:
  static constexpr size_t BLOCK = 2048;

  explicit BodyBlocks(size_t max_bytes) : max_(max_bytes) {}
  ~BodyBlocks() {
    while (head_) { Block *n = head_->next; free(head_); head_ = n; }
  }
  BodyBlocks(const BodyBlocks &) = delete;
  BodyBlocks &operator=(const BodyBlocks &) = delete;

  // False when the answer would pass max_bytes, or a block could not be had.
  bool append(const uint8_t *p, size_t n) {
    if (total_ + n > max_) return false;
    while (n) {
      if (!tail_ || tail_->len == BLOCK) {
        Block *b = (Block *)malloc(sizeof(Block));
        if (!b) return false;
        b->next = nullptr;
        b->len = 0;
        (tail_ ? tail_->next : head_) = b;
        tail_ = b;
      }
      const size_t room = BLOCK - tail_->len;
      const size_t take = n < room ? n : room;
      memcpy(tail_->data + tail_->len, p, take);
      tail_->len += take;
      total_ += take;
      p += take;
      n -= take;
    }
    return true;
  }

  size_t size() const { return total_; }

  // The bytes from the start, one at a time; -1 after the last.
  int read() {
    if (!rb_) { if (started_) return -1; rb_ = head_; started_ = true; }
    while (rb_ && rpos_ >= rb_->len) { rb_ = rb_->next; rpos_ = 0; }
    if (!rb_) return -1;
    return rb_->data[rpos_++];
  }

 private:
  struct Block {
    Block  *next;
    size_t  len;
    uint8_t data[BLOCK];
  };
  Block  *head_ = nullptr, *tail_ = nullptr;
  Block  *rb_ = nullptr;
  size_t  rpos_ = 0;
  bool    started_ = false;
  size_t  total_ = 0;
  size_t  max_;
};
