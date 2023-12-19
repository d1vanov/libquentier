cmake_minimum_required(VERSION 3.10.2)

set(LIBRARY_NAME ${LIBNAME}_enml)

add_library(${LIBRARY_NAME} STATIC)

target_sources(${LIBRARY_NAME}
  PRIVATE
  DecryptedTextCache.cpp
  DecryptedTextCache.h
  HtmlData.cpp
  HtmlData.h
  HtmlUtils.cpp
  HtmlUtils.h
  conversion_rules/SkipRule.cpp
  conversion_rules/SkipRule.h
  conversion_rules/SkipRuleBuilder.cpp
  conversion_rules/SkipRuleBuilder.h)

target_link_libraries(${LIBRARY_NAME}
  ${LIBNAME}_private
  Qt5::Core)
