# Puts the HTML that md2html made from a Markdown file into a page template, replacing @MANUAL_BODY@.
# md2html does not give headings an id, so this adds GitHub's: lowercase, punctuation removed, spaces
# replaced by '-', and '-1', '-2', ... for duplicates; links like [Settings](#settings) then work both
# on GitHub and in the page.
# usage: cmake -DBODY=<md2html output> -DTEMPLATE=<page.html.in> -DOUTPUT=<page.html> -P MarkdownPage.cmake

cmake_minimum_required(VERSION 3.28)

file(READ ${BODY} rest)
set(body "")
set(slugs "")
while (TRUE)
  string(REGEX MATCH "<h[1-6]>" open "${rest}")
  if (NOT open)
    break()
  endif()
  string(SUBSTRING "${open}" 2 1 level)
  string(FIND "${rest}" "${open}" start)
  string(FIND "${rest}" "</h${level}>" end)
  string(SUBSTRING "${rest}" 0 ${start} before)
  math(EXPR textStart "${start} + 4")
  math(EXPR textLength "${end} - ${textStart}")
  string(SUBSTRING "${rest}" ${textStart} ${textLength} text)
  math(EXPR after "${end} + 5")
  string(SUBSTRING "${rest}" ${after} -1 rest)

  string(REGEX REPLACE "<[^>]*>" "" slug "${text}")
  string(REPLACE "&lt;" "<" slug "${slug}")
  string(REPLACE "&gt;" ">" slug "${slug}")
  string(REPLACE "&amp;" "&" slug "${slug}")
  string(REPLACE "&quot;" "\"" slug "${slug}")
  string(TOLOWER "${slug}" slug)
  string(REGEX REPLACE "[^a-z0-9 _-]" "" slug "${slug}")
  string(REPLACE " " "-" slug "${slug}")
  set(unique "${slug}")
  set(n 0)
  while ("${unique}" IN_LIST slugs)
    math(EXPR n "${n} + 1")
    set(unique "${slug}-${n}")
  endwhile()
  list(APPEND slugs "${unique}")

  string(APPEND body "${before}<h${level} id=\"${unique}\">${text}</h${level}>")
endwhile()
string(APPEND body "${rest}")

file(READ ${TEMPLATE} page)
string(REPLACE "@MANUAL_BODY@" "${body}" page "${page}")
file(WRITE ${OUTPUT} "${page}")
