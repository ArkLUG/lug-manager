# src/utils/text: Escaping, Markdown, HTML-to-text, UTF-8, CSV, names, money.
target_sources(lug_core PRIVATE
  Csv.hpp
  HtmlEscape.hpp
  HtmlText.hpp
  JsonEscape.hpp
  MarkdownRenderer.cpp
  MarkdownRenderer.hpp
  Money.hpp
  NameMatch.cpp
  NameMatch.hpp
  Plural.hpp
  UrlEncode.hpp
  Utf8.hpp
)
