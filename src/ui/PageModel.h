#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace stick_s3_asr {

class PageModel {
 public:
  PageModel(size_t columns = 18, size_t linesPerPage = 6);

  void setText(const std::string& text);
  void setLayout(size_t columns, size_t linesPerPage);
  void clear();
  void nextPage();

  const std::string& page() const;
  size_t pageIndex() const { return pageIndex_; }
  size_t pageCount() const { return pages_.empty() ? 1 : pages_.size(); }
  bool empty() const { return sourceText_.empty(); }

 private:
  static size_t utf8CodepointLength(unsigned char lead);
  static size_t displayWidth(const std::string& codepoint);

  void rebuildPages();

  size_t columns_;
  size_t linesPerPage_;
  std::string sourceText_;
  std::vector<std::string> pages_;
  size_t pageIndex_ = 0;
};

}  // namespace stick_s3_asr
