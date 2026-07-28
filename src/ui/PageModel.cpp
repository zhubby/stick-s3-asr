#include "ui/PageModel.h"

#include <algorithm>

namespace stick_s3_asr {

namespace {
const std::string kEmptyPage = "Ready";
}

PageModel::PageModel(size_t columns, size_t linesPerPage)
    : columns_(std::max<size_t>(1, columns)),
      linesPerPage_(std::max<size_t>(1, linesPerPage)) {
  clear();
}

void PageModel::setText(const std::string& text) {
  sourceText_ = text;
  pageIndex_ = 0;
  rebuildPages();
}

void PageModel::setLayout(size_t columns, size_t linesPerPage) {
  columns = std::max<size_t>(1, columns);
  linesPerPage = std::max<size_t>(1, linesPerPage);
  if (columns_ == columns && linesPerPage_ == linesPerPage) {
    return;
  }
  columns_ = columns;
  linesPerPage_ = linesPerPage;
  pageIndex_ = 0;
  rebuildPages();
}

void PageModel::clear() {
  sourceText_.clear();
  pageIndex_ = 0;
  pages_.clear();
  pages_.push_back(kEmptyPage);
}

void PageModel::nextPage() {
  if (pages_.empty()) {
    pageIndex_ = 0;
    return;
  }
  pageIndex_ = (pageIndex_ + 1) % pages_.size();
}

const std::string& PageModel::page() const {
  if (pages_.empty()) {
    return kEmptyPage;
  }
  return pages_[pageIndex_ % pages_.size()];
}

size_t PageModel::utf8CodepointLength(unsigned char lead) {
  if ((lead & 0x80) == 0) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

size_t PageModel::displayWidth(const std::string& codepoint) {
  if (codepoint.empty()) return 0;
  return static_cast<unsigned char>(codepoint[0]) < 0x80 ? 1 : 2;
}

void PageModel::rebuildPages() {
  pages_.clear();
  if (sourceText_.empty()) {
    pages_.push_back(kEmptyPage);
    return;
  }

  std::vector<std::string> lines;
  std::string line;
  size_t lineWidth = 0;

  for (size_t i = 0; i < sourceText_.size();) {
    const unsigned char lead = static_cast<unsigned char>(sourceText_[i]);
    const size_t len = std::min(utf8CodepointLength(lead), sourceText_.size() - i);
    const std::string cp = sourceText_.substr(i, len);
    i += len;

    if (cp == "\n") {
      lines.push_back(line);
      line.clear();
      lineWidth = 0;
      continue;
    }

    const size_t width = displayWidth(cp);
    if (!line.empty() && lineWidth + width > columns_) {
      lines.push_back(line);
      line.clear();
      lineWidth = 0;
    }

    line += cp;
    lineWidth += width;
  }

  if (!line.empty() || lines.empty()) {
    lines.push_back(line);
  }

  for (size_t i = 0; i < lines.size(); i += linesPerPage_) {
    std::string pageText;
    const size_t end = std::min(lines.size(), i + linesPerPage_);
    for (size_t j = i; j < end; ++j) {
      if (j > i) pageText += '\n';
      pageText += lines[j];
    }
    pages_.push_back(pageText);
  }
}

}  // namespace stick_s3_asr
